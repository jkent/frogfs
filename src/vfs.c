/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "frogfs_priv.h"
#include "log.h"
#include "frogfs/frogfs.h"
#include "frogfs/vfs.h"

#include <esp_err.h>
#include <esp_vfs.h>

#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/fcntl.h>
#include <sys/stat.h>


#ifndef CONFIG_FROGFS_MAX_PARTITIONS
# define CONFIG_FROGFS_MAX_PARTITIONS 1
#endif

typedef enum {
    VFS_FROGFS_USE_OVERLAY = (1 << 0),
} vfs_frogfs_flags_t;

typedef struct {
    bool used;
    bool overlay;
    union {
        int fd;
        frogfs_file_t *file;
    };
} vfs_frogfs_file_t;

typedef struct {
    frogfs_fs_t *fs;
    vfs_frogfs_flags_t flags;
    char base_path[ESP_VFS_PATH_MAX + 1];
    char overlay_path[ESP_VFS_PATH_MAX + 1];
    size_t max_files;
    vfs_frogfs_file_t files[];
} vfs_frogfs_t;

typedef struct {
    DIR *overlay_dir;
    int offset;
    char path[256];
    bool done;
    struct dirent e;
} vfs_frogfs_dir_t;

static vfs_frogfs_t *s_vfs_frogfs[CONFIG_FROGFS_MAX_PARTITIONS];

esp_err_t esp_vfs_register_common(const char* base_path, size_t len, const esp_vfs_t* vfs, void* ctx, int *vfs_index);

static esp_err_t frogfs_get_empty(int *index)
{
    int i;

    for (i = 0; i < CONFIG_FROGFS_MAX_PARTITIONS; i++) {
        if (s_vfs_frogfs[i] == NULL) {
            *index = i;
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

static void frogfs_get_overlay(vfs_frogfs_t *vfs, char *overlay_path, char *path,
                               size_t len)
{
    size_t out_len;

    strncpy(overlay_path, vfs->overlay_path, len - 1);
    overlay_path[len - 1] = '\0';
    out_len = strlen(overlay_path);
    if (len - out_len > 1) {
        strncpy(overlay_path + out_len, "/", len - out_len - 1);
        out_len += 1;
        strncpy(overlay_path + out_len, path, len - out_len - 1);
        overlay_path[len - out_len - 1] = '\0';
    }
}

static ssize_t vfs_frogfs_write(void *ctx, int fd, const void *data,
                                size_t size)
{
    vfs_frogfs_t *vfs_frogfs = (vfs_frogfs_t *) ctx;

    if (fd < 0 || fd >= vfs_frogfs->max_files) {
        return -1;
    }

    if (!vfs_frogfs->files[fd].used) {
        return -1;
    }

    if (vfs_frogfs->files[fd].overlay) {
        return write(vfs_frogfs->files[fd].fd, data, size);
    }

    return -1;
}

static off_t vfs_frogfs_lseek(void *ctx, int fd, off_t size, int mode)
{
    vfs_frogfs_t *vfs_frogfs = (vfs_frogfs_t *) ctx;

    if (fd < 0 || fd >= vfs_frogfs->max_files) {
        return -1;
    }

    if (!vfs_frogfs->files[fd].used) {
        return -1;
    }

    if (vfs_frogfs->files[fd].overlay) {
        return lseek(vfs_frogfs->files[fd].fd, size, mode);
    }

    frogfs_file_t *fp = vfs_frogfs->files[fd].file;
    return frogfs_fseek(fp, size, mode);
}

static ssize_t vfs_frogfs_read(void *ctx, int fd, void *data, size_t size)
{
    vfs_frogfs_t *vfs_frogfs = (vfs_frogfs_t *) ctx;

    if (fd < 0 || fd >= vfs_frogfs->max_files) {
        return -1;
    }

    if (!vfs_frogfs->files[fd].used) {
        return -1;
    }

    if (vfs_frogfs->files[fd].overlay) {
        return read(vfs_frogfs->files[fd].fd, data, size);
    }

    frogfs_file_t *fp = vfs_frogfs->files[fd].file;
    return frogfs_fread(fp, data, size);
}

static int vfs_frogfs_open(void *ctx, const char *path, int flags, int mode)
{
    vfs_frogfs_t *vfs_frogfs = (vfs_frogfs_t *) ctx;

    int fd;
    for (fd = 0; fd < vfs_frogfs->max_files; fd++) {
        if (!vfs_frogfs->files[fd].used) {
            break;
        }
    }

    if (fd >= vfs_frogfs->max_files) {
        return -1;
    }

    if (vfs_frogfs->flags & VFS_FROGFS_USE_OVERLAY) {
        char overlay_path[256];

        frogfs_get_overlay(vfs_frogfs, overlay_path, path,
                           sizeof(overlay_path));
        vfs_frogfs->files[fd].fd = open(overlay_path, flags, mode);
        if (vfs_frogfs->files[fd].fd >= 0) {
            vfs_frogfs->files[fd].used = true;
            vfs_frogfs->files[fd].overlay = true;
            return fd;
        }
    }

    if (flags & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC)) {
        return -1;
    }

    vfs_frogfs->files[fd].file = frogfs_fopen(vfs_frogfs->fs, path);
    if (vfs_frogfs->files[fd].file != NULL) {
        vfs_frogfs->files[fd].used = true;
        vfs_frogfs->files[fd].overlay = false;
        return fd;
    }

    return -1;
}

static int vfs_frogfs_close(void *ctx, int fd)
{
    vfs_frogfs_t *vfs_frogfs = (vfs_frogfs_t *) ctx;

    if (fd < 0 || fd >= vfs_frogfs->max_files) {
        return -1;
    }

    if (!vfs_frogfs->files[fd].used) {
        return -1;
    }

    vfs_frogfs->files[fd].used = false;

    if (vfs_frogfs->files[fd].overlay) {
        return close(vfs_frogfs->files[fd].fd);
    }

    frogfs_file_t *fp = vfs_frogfs->files[fd].file;
    frogfs_fclose(fp);
    return 0;
}

static int vfs_frogfs_fstat(void *ctx, int fd, struct stat *st)
{
    vfs_frogfs_t *vfs_frogfs = (vfs_frogfs_t *) ctx;

    if (fd < 0 || fd >= vfs_frogfs->max_files) {
        return -1;
    }

    if (!vfs_frogfs->files[fd].used) {
        return -1;
    }

    if (vfs_frogfs->files[fd].overlay) {
        return fstat(vfs_frogfs->files[fd].fd, st);
    }

    frogfs_file_t *fp = vfs_frogfs->files[fd].file;
    memset(st, 0, sizeof(struct stat));
    st->st_mode = S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH |
            S_IFREG;
    st->st_size = fp->fh->file_len;
    st->st_spare4[0] = FROGFS_MAGIC;
    st->st_spare4[1] = fp->fh->flags | (fp->fh->compression << 16);
    return 0;
}

static int vfs_frogfs_stat(void *ctx, const char *path, struct stat *st)
{
    vfs_frogfs_t *vfs_frogfs = (vfs_frogfs_t *) ctx;

    if (vfs_frogfs->flags & VFS_FROGFS_USE_OVERLAY) {
        char overlay_path[256];
        frogfs_get_overlay(vfs_frogfs, overlay_path, path,
                           sizeof(overlay_path));

        int res = stat(overlay_path, st);
        if (res >= 0) {
            return res;
        }
    }

    frogfs_stat_t s;
    if (!frogfs_stat(vfs_frogfs->fs, path, &s)) {
        return -1;
    }

    memset(st, 0, sizeof(struct stat));
    st->st_mode = S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
    st->st_mode |= (s.type == FROGFS_TYPE_FILE) ? S_IFREG : S_IFDIR;
    st->st_size = s.size;
    st->st_spare4[0] = FROGFS_MAGIC;
    st->st_spare4[1] = s.flags | (s.compression << 16);
    return 0;
}

static int vfs_frogfs_link(void *ctx, const char *n1, const char *n2)
{
    vfs_frogfs_t *vfs_frogfs = (vfs_frogfs_t *) ctx;

    if (!vfs_frogfs->flags & VFS_FROGFS_USE_OVERLAY) {
        return -1;
    }

    char overlay_n1[256], overlay_n2[256];
    frogfs_get_overlay(vfs_frogfs, overlay_n1, (char *)n1, sizeof(overlay_n1));
    frogfs_get_overlay(vfs_frogfs, overlay_n2, (char *)n2, sizeof(overlay_n2));

    return link(overlay_n1, overlay_n2);
}

static int vfs_frogfs_unlink(void *ctx, const char *path)
{
    vfs_frogfs_t *vfs_frogfs = (vfs_frogfs_t *) ctx;

    if (!vfs_frogfs->flags & VFS_FROGFS_USE_OVERLAY) {
        return -1;
    }

    char overlay_path[256];
    frogfs_get_overlay(vfs_frogfs, overlay_path, (char *)path, sizeof(overlay_path));

    return unlink(overlay_path);
}

static int vfs_frogfs_rename(void *ctx, const char *src, const char *dst)
{
    vfs_frogfs_t *vfs_frogfs = (vfs_frogfs_t *) ctx;

    if (!vfs_frogfs->flags & VFS_FROGFS_USE_OVERLAY) {
        return -1;
    }

    char overlay_src[256], overlay_dst[256];
    frogfs_get_overlay(vfs_frogfs, overlay_src, (char *)src, sizeof(overlay_src));
    frogfs_get_overlay(vfs_frogfs, overlay_dst, (char *)dst, sizeof(overlay_dst));

    struct stat st;
    frogfs_stat_t s;
    if (stat(overlay_src, &st) < 0 && frogfs_stat(vfs_frogfs->fs, src, &s)) {
        frogfs_file_t *src_file = frogfs_fopen(vfs_frogfs->fs, src);
        if (!src_file) {
            return -1;
        }

        int dst_fd = open(overlay_dst, O_RDWR | O_CREAT | O_TRUNC,
                S_IRUSR | S_IWUSR);
        if (dst_fd < 0) {
            frogfs_fclose(src_file);
            return -1;
        }

        int chunk;
        char buf[512];
        while ((chunk = frogfs_fread(src_file, buf, sizeof(buf))) > 0) {
            write(dst_fd, buf, chunk);
        }
        frogfs_fclose(src_file);
        close(dst_fd);
        return 0;
    }

    return rename(overlay_src, overlay_dst);
}

static DIR* vfs_frogfs_opendir(void *ctx, const char *name)
{
    vfs_frogfs_t *vfs_frogfs = (vfs_frogfs_t *) ctx;
    vfs_frogfs_dir_t *dir = calloc(1, sizeof(vfs_frogfs_dir_t));
    if (!dir) {
        errno = ENOMEM;
        return NULL;
    }

    strncpy(dir->path, name, sizeof(dir->path) - 1);
    dir->path[sizeof(dir->path) - 1] = '\0';

    frogfs_stat_t s;
    if (!frogfs_stat(vfs_frogfs->fs, dir->path, &s) ||
            s.type != FROGFS_TYPE_DIR) {
        free(dir);
        return NULL;
    }
    dir->offset = s.index + 1;

    return (DIR *) dir;
}

static int vfs_frogfs_readdir_r(void *ctx, DIR *pdir, struct dirent *entry,
        struct dirent **out_dirent);
static struct dirent *vfs_frogfs_readdir(void *ctx, DIR *pdir)
{
    vfs_frogfs_dir_t *dir = (vfs_frogfs_dir_t *) pdir;
    struct dirent *out_dirent;
    int err = vfs_frogfs_readdir_r(ctx, pdir, &dir->e, &out_dirent);
    if (err != 0) {
        errno = err;
        return NULL;
    }
    return out_dirent;
}

static int vfs_frogfs_readdir_r(void *ctx, DIR *pdir, struct dirent *entry,
        struct dirent **out_dirent)
{
    vfs_frogfs_t *vfs_frogfs = (vfs_frogfs_t *) ctx;
    vfs_frogfs_dir_t *dir = (vfs_frogfs_dir_t *) pdir;

    if (dir->done) {
        *out_dirent = NULL;
        return 0;
    }

    while (dir->offset < vfs_frogfs->fs->header->num_objects) {
        const char *path = frogfs_get_path(vfs_frogfs->fs, dir->offset);

        if (vfs_frogfs->flags & VFS_FROGFS_USE_OVERLAY) {
            char overlay_path[256];
            frogfs_get_overlay(vfs_frogfs, overlay_path, (char *)path,
                               sizeof(overlay_path));

            struct stat st;
            if (stat(path, &st)) {
                dir->offset++;
                continue;
            }
        }

        frogfs_stat_t s;
        frogfs_stat(vfs_frogfs->fs, path, &s);

        const char *p = dir->path;
        while (*p && *p++ == *path++);

        if (path[0] != '/') {
            dir->done = true;
            *out_dirent = NULL;
            return 0;
        }
        path++;
        if (strchr(path, '/')) {
            dir->offset++;
            continue;
        }

        entry->d_ino = 0;
        entry->d_type = (s.type == FROGFS_TYPE_DIR) ? DT_DIR : DT_REG;
        strncpy(entry->d_name, path, sizeof(entry->d_name) - 1);
        entry->d_name[sizeof(entry->d_name) - 1] = '\0';
        dir->offset++;
        *out_dirent = entry;
        return 0;
    }

    if (vfs_frogfs->flags & VFS_FROGFS_USE_OVERLAY) {
        if (!dir->overlay_dir) {
            char overlay_path[256];
            frogfs_get_overlay(vfs_frogfs, overlay_path, dir->path,
                               sizeof(overlay_path));
            dir->overlay_dir = opendir(overlay_path);
            if (dir->overlay_dir == NULL) {
                *out_dirent = NULL;
                return 0;
            }
        }

        int ret = readdir_r(dir->overlay_dir, entry, out_dirent);
        if (ret != 0 || *out_dirent == NULL) {
            dir->offset++;
            return ret;
        }
    }

    *out_dirent = NULL;
    return 0;
}

static long vfs_frogfs_telldir(void *ctx, DIR *pdir)
{
    vfs_frogfs_dir_t *dir = (vfs_frogfs_dir_t *) pdir;
    return dir->offset;
}

static void vfs_frogfs_seekdir(void *ctx, DIR *pdir, long offset)
{
    vfs_frogfs_t *vfs_frogfs = (vfs_frogfs_t *) ctx;
    vfs_frogfs_dir_t *dir = (vfs_frogfs_dir_t *) pdir;

    if (offset < dir->offset) {
        if (dir->overlay_dir) {
            closedir(dir->overlay_dir);
        }
        dir->offset = 0;
    }
    if (offset < vfs_frogfs->fs->header->num_objects) {
        dir->offset = offset;
    } else {
        dir->offset = vfs_frogfs->fs->header->num_objects;
    }
    while (dir->offset < offset) {
        struct dirent *tmp;
        vfs_frogfs_readdir_r(ctx, pdir, &dir->e, &tmp);
        if (tmp == NULL) {
            break;
        }
    }
}

static int vfs_frogfs_closedir(void *ctx, DIR *pdir)
{
    vfs_frogfs_dir_t *dir = (vfs_frogfs_dir_t *) pdir;

    if (dir->overlay_dir) {
        closedir(dir->overlay_dir);
    }
    free(dir);
    return 0;
}

static int vfs_frogfs_mkdir(void *ctx, const char *name, mode_t mode)
{
    vfs_frogfs_t *vfs_frogfs = (vfs_frogfs_t *) ctx;

    if (vfs_frogfs->flags & VFS_FROGFS_USE_OVERLAY) {
        char overlay_path[256];
        frogfs_get_overlay(vfs_frogfs, overlay_path, (char *)name,
                           sizeof(overlay_path));

        return mkdir(overlay_path, mode);
    }

    return -1;
}

static int vfs_frogfs_rmdir(void *ctx, const char *name)
{
    vfs_frogfs_t *vfs_frogfs = (vfs_frogfs_t *) ctx;

    if (vfs_frogfs->flags & VFS_FROGFS_USE_OVERLAY) {
        char overlay_path[256];
        frogfs_get_overlay(vfs_frogfs, overlay_path, (char *)name,
                           sizeof(overlay_path));

        return rmdir(overlay_path);
    }

    return -1;
}


static int vfs_frogfs_fcntl(void *ctx, int fd, int cmd, int arg)
{
    vfs_frogfs_t *vfs_frogfs = (vfs_frogfs_t *) ctx;

    if (fd < 0 || fd >= vfs_frogfs->max_files) {
        return -1;
    }

    if (!vfs_frogfs->files[fd].used) {
        return -1;
    }

    if (vfs_frogfs->files[fd].overlay) {
        return fcntl(vfs_frogfs->files[fd].fd, cmd, arg);
    }

    return -1;
}

static int vfs_frogfs_ioctl(void *ctx, int fd, int cmd, va_list args)
{
    vfs_frogfs_t *vfs_frogfs = (vfs_frogfs_t *) ctx;

    if (fd < 0 || fd >= vfs_frogfs->max_files) {
        return -1;
    }

    if (!vfs_frogfs->files[fd].used) {
        return -1;
    }

    if (vfs_frogfs->files[fd].overlay) {
        char *argp = va_arg(args, char *);
        return ioctl(vfs_frogfs->files[fd].fd, cmd, argp);
    }

    return -1;
}

static int vfs_frogfs_fsync(void *ctx, int fd)
{
    vfs_frogfs_t *vfs_frogfs = (vfs_frogfs_t *) ctx;

    if (fd < 0 || fd >= vfs_frogfs->max_files) {
        return -1;
    }

    if (!vfs_frogfs->files[fd].used) {
        return -1;
    }

    if (vfs_frogfs->files[fd].overlay) {
        return fsync(vfs_frogfs->files[fd].fd);
    }

    return -1;
}

esp_err_t esp_vfs_frogfs_register(const esp_vfs_frogfs_conf_t *conf)
{
    // assert(conf->base_path != NULL);
    assert(conf->fs != NULL);

    const esp_vfs_t vfs = {
        .flags = ESP_VFS_FLAG_CONTEXT_PTR,
        .write_p = &vfs_frogfs_write,
        .lseek_p = &vfs_frogfs_lseek,
        .read_p = &vfs_frogfs_read,
        .open_p = &vfs_frogfs_open,
        .close_p = &vfs_frogfs_close,
        .fstat_p = &vfs_frogfs_fstat,
        .stat_p = &vfs_frogfs_stat,
        .link_p = &vfs_frogfs_link,
        .unlink_p = &vfs_frogfs_unlink,
        .rename_p = &vfs_frogfs_rename,
        .opendir_p = &vfs_frogfs_opendir,
        .readdir_p = &vfs_frogfs_readdir,
        .readdir_r_p = &vfs_frogfs_readdir_r,
        .telldir_p = &vfs_frogfs_telldir,
        .seekdir_p = &vfs_frogfs_seekdir,
        .closedir_p = &vfs_frogfs_closedir,
        .mkdir_p = &vfs_frogfs_mkdir,
        .rmdir_p = &vfs_frogfs_rmdir,
        .fcntl_p = &vfs_frogfs_fcntl,
        .ioctl_p = &vfs_frogfs_ioctl,
        .fsync_p = &vfs_frogfs_fsync,
    };

    int index;
    if (frogfs_get_empty(&index) != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }

    vfs_frogfs_t *vfs_frogfs = calloc(1, sizeof(vfs_frogfs_t) +
            (sizeof(vfs_frogfs_file_t *) * conf->max_files));
    if (vfs_frogfs == NULL) {
        LOGE(__func__, "vfs_frogfs could not be alloc'd");
        return ESP_ERR_NO_MEM;
    }

    vfs_frogfs->fs = conf->fs;
    vfs_frogfs->max_files = conf->max_files;
    if (conf->base_path) {
        strncpy(vfs_frogfs->base_path, conf->base_path,
                sizeof(vfs_frogfs->base_path) - 1);
        vfs_frogfs->base_path[sizeof(vfs_frogfs->base_path) - 1] = '\0';
    } else { 
        vfs_frogfs->base_path[0] = '\0';
    }
    if (conf->overlay_path) {
        vfs_frogfs->flags |= VFS_FROGFS_USE_OVERLAY;
        strncpy(vfs_frogfs->overlay_path, conf->overlay_path,
                sizeof(vfs_frogfs->overlay_path));
        vfs_frogfs->overlay_path[sizeof(vfs_frogfs->overlay_path) - 1] = '\0';
    }

    esp_err_t err = esp_vfs_register_common(vfs_frogfs->base_path, (vfs_frogfs->base_path[0])?strlen(vfs_frogfs->base_path):SIZE_MAX, &vfs, vfs_frogfs, NULL);
    if (err != ESP_OK) {
        free(vfs_frogfs);
        return err;
    }

    s_vfs_frogfs[index] = vfs_frogfs;
    return ESP_OK;
}
