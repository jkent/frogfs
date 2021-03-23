/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "espfs_priv.h"
#include "log.h"
#include "libespfs/espfs.h"
#include "libespfs/vfs.h"

#include <esp_err.h>
#include <esp_log.h>
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


typedef enum {
    VFS_ESPFS_USE_OVERLAY = (1 << 0),
} vfs_espfs_flags_t;

typedef struct {
    bool used;
    bool overlay;
    union {
        int fd;
        espfs_file_t *file;
    };
} vfs_espfs_file_t;

typedef struct {
    espfs_fs_t *fs;
    vfs_espfs_flags_t flags;
    char base_path[ESP_VFS_PATH_MAX + 1];
    char overlay_path[ESP_VFS_PATH_MAX + 1];
    size_t max_files;
    vfs_espfs_file_t files[];
} vfs_espfs_t;

typedef struct {
    DIR *overlay_dir;
    int offset;
    char path[256];
    bool done;
    struct dirent e;
} vfs_espfs_dir_t;

static vfs_espfs_t *s_vfs_espfs[CONFIG_ESPFS_MAX_PARTITIONS];

static esp_err_t esp_espfs_get_empty(int *index)
{
    int i;

    for (i = 0; i < CONFIG_ESPFS_MAX_PARTITIONS; i++) {
        if (s_vfs_espfs[i] == NULL) {
            *index = i;
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

static ssize_t vfs_espfs_write(void *ctx, int fd, const void *data, size_t size)
{
    vfs_espfs_t *vfs_espfs = (vfs_espfs_t *) ctx;

    if (fd < 0 || fd >= vfs_espfs->max_files) {
        return -1;
    }

    if (!vfs_espfs->files[fd].used) {
        return -1;
    }

    if (vfs_espfs->files[fd].overlay) {
        return write(vfs_espfs->files[fd].fd, data, size);
    }

    return -1;
}

static off_t vfs_espfs_lseek(void *ctx, int fd, off_t size, int mode)
{
    vfs_espfs_t *vfs_espfs = (vfs_espfs_t *) ctx;

    if (fd < 0 || fd >= vfs_espfs->max_files) {
        return -1;
    }

    if (!vfs_espfs->files[fd].used) {
        return -1;
    }

    if (vfs_espfs->files[fd].overlay) {
        return lseek(vfs_espfs->files[fd].fd, size, mode);
    }

    espfs_file_t *fp = vfs_espfs->files[fd].file;
    return espfs_fseek(fp, size, mode);
}

static ssize_t vfs_espfs_read(void *ctx, int fd, void *data, size_t size)
{
    vfs_espfs_t *vfs_espfs = (vfs_espfs_t *) ctx;

    if (fd < 0 || fd >= vfs_espfs->max_files) {
        return -1;
    }

    if (!vfs_espfs->files[fd].used) {
        return -1;
    }

    if (vfs_espfs->files[fd].overlay) {
        return read(vfs_espfs->files[fd].fd, data, size);
    }

    espfs_file_t *fp = vfs_espfs->files[fd].file;
    return espfs_fread(fp, data, size);
}

static int vfs_espfs_open(void *ctx, const char *path, int flags, int mode)
{
    vfs_espfs_t *vfs_espfs = (vfs_espfs_t *) ctx;

    int fd;
    for (fd = 0; fd < vfs_espfs->max_files; fd++) {
        if (!vfs_espfs->files[fd].used) {
            break;
        }
    }

    if (fd >= vfs_espfs->max_files) {
        return -1;
    }

    if (vfs_espfs->flags & VFS_ESPFS_USE_OVERLAY) {
        char overlay_path[256];
        int len = strlcpy(overlay_path, vfs_espfs->overlay_path,
                sizeof(overlay_path));
        len += strlcpy(overlay_path + len, "/", sizeof(overlay_path) - len);
        strlcpy(overlay_path + len, path, sizeof(overlay_path) - len);

        vfs_espfs->files[fd].fd = open(overlay_path, flags, mode);
        if (vfs_espfs->files[fd].fd >= 0) {
            vfs_espfs->files[fd].used = true;
            vfs_espfs->files[fd].overlay = true;
            return fd;
        }
    }

    if (flags & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC)) {
        return -1;
    }

    vfs_espfs->files[fd].file = espfs_fopen(vfs_espfs->fs, path);
    if (vfs_espfs->files[fd].file != NULL) {
        vfs_espfs->files[fd].used = true;
        vfs_espfs->files[fd].overlay = false;
        return fd;
    }

    return -1;
}

static int vfs_espfs_close(void *ctx, int fd)
{
    vfs_espfs_t *vfs_espfs = (vfs_espfs_t *) ctx;

    if (fd < 0 || fd >= vfs_espfs->max_files) {
        return -1;
    }

    if (!vfs_espfs->files[fd].used) {
        return -1;
    }

    vfs_espfs->files[fd].used = false;

    if (vfs_espfs->files[fd].overlay) {
        return close(vfs_espfs->files[fd].fd);
    }

    espfs_file_t *fp = vfs_espfs->files[fd].file;
    espfs_fclose(fp);
    return 0;
}

static int vfs_espfs_fstat(void *ctx, int fd, struct stat *st)
{
    vfs_espfs_t *vfs_espfs = (vfs_espfs_t *) ctx;

    if (fd < 0 || fd >= vfs_espfs->max_files) {
        return -1;
    }

    if (!vfs_espfs->files[fd].used) {
        return -1;
    }

    if (vfs_espfs->files[fd].overlay) {
        return fstat(vfs_espfs->files[fd].fd, st);
    }

    espfs_file_t *fp = vfs_espfs->files[fd].file;
    memset(st, 0, sizeof(struct stat));
    st->st_mode = S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH |
            S_IFREG;
    st->st_size = fp->fh->file_len;
    st->st_spare4[0] = ESPFS_MAGIC;
    st->st_spare4[1] = fp->fh->flags | (fp->fh->compression << 16);
    return 0;
}

static int vfs_espfs_stat(void *ctx, const char *path, struct stat *st)
{
    vfs_espfs_t *vfs_espfs = (vfs_espfs_t *) ctx;

    if (vfs_espfs->flags & VFS_ESPFS_USE_OVERLAY) {
        char overlay_path[256];
        int len = strlcpy(overlay_path, vfs_espfs->overlay_path,
                sizeof(overlay_path));
        len += strlcpy(overlay_path + len, "/", sizeof(overlay_path) - len);
        strlcpy(overlay_path + len, path, sizeof(overlay_path) - len);

        int res = stat(overlay_path, st);
        if (res >= 0) {
            return res;
        }
    }

    espfs_stat_t s;
    if (!espfs_stat(vfs_espfs->fs, path, &s)) {
        return -1;
    }

    memset(st, 0, sizeof(struct stat));
    st->st_mode = S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
    st->st_mode |= (s.type == ESPFS_TYPE_FILE) ? S_IFREG : S_IFDIR;
    st->st_size = s.size;
    st->st_spare4[0] = ESPFS_MAGIC;
    st->st_spare4[1] = s.flags | (s.compression << 16);
    return 0;
}

static int vfs_espfs_link(void *ctx, const char *n1, const char *n2)
{
    vfs_espfs_t *vfs_espfs = (vfs_espfs_t *) ctx;

    if (!vfs_espfs->flags & VFS_ESPFS_USE_OVERLAY) {
        return -1;
    }

    char overlay_n1[256];
    int len = strlcpy(overlay_n1, vfs_espfs->overlay_path, sizeof(overlay_n1));
    len += strlcpy(overlay_n1 + len, "/", sizeof(overlay_n1) - len);
    strlcpy(overlay_n1 + len, n1, sizeof(overlay_n1) - len);

    char overlay_n2[256];
    len = strlcpy(overlay_n2, vfs_espfs->overlay_path, sizeof(overlay_n2));
    len += strlcpy(overlay_n2 + len, "/", sizeof(overlay_n2) - len);
    strlcpy(overlay_n2 + len, n1, sizeof(overlay_n2) - len);

    return link(overlay_n1, overlay_n2);
}

static int vfs_espfs_unlink(void *ctx, const char *path)
{
    vfs_espfs_t *vfs_espfs = (vfs_espfs_t *) ctx;

    if (!vfs_espfs->flags & VFS_ESPFS_USE_OVERLAY) {
        return -1;
    }

    char overlay_path[256];
    int len = strlcpy(overlay_path, vfs_espfs->overlay_path,
            sizeof(overlay_path));
    len += strlcpy(overlay_path + len, "/", sizeof(overlay_path) - len);
    strlcpy(overlay_path + len, path, sizeof(overlay_path) - len);

    return unlink(overlay_path);
}

static int vfs_espfs_rename(void *ctx, const char *src, const char *dst)
{
    vfs_espfs_t *vfs_espfs = (vfs_espfs_t *) ctx;

    if (!vfs_espfs->flags & VFS_ESPFS_USE_OVERLAY) {
        return -1;
    }

    char overlay_src[256];
    int len = strlcpy(overlay_src, vfs_espfs->overlay_path,
            sizeof(overlay_src));
    len += strlcpy(overlay_src + len, "/", sizeof(overlay_src) - len);
    strlcpy(overlay_src + len, src, sizeof(overlay_src) - len);

    char overlay_dst[256];
    len = strlcpy(overlay_dst, vfs_espfs->overlay_path, sizeof(overlay_dst));
    len += strlcpy(overlay_dst + len, "/", sizeof(overlay_dst) - len);
    strlcpy(overlay_dst + len, dst, sizeof(overlay_dst) - len);

    struct stat st;
    espfs_stat_t s;
    if (stat(overlay_src, &st) < 0 && espfs_stat(vfs_espfs->fs, src, &s)) {
        espfs_file_t *src_file = espfs_fopen(vfs_espfs->fs, src);
        if (!src_file) {
            return -1;
        }

        int dst_fd = open(overlay_dst, O_RDWR | O_CREAT | O_TRUNC,
                S_IRUSR | S_IWUSR);
        if (dst_fd < 0) {
            espfs_fclose(src_file);
            return -1;
        }

        int chunk;
        char buf[512];
        while ((chunk = espfs_fread(src_file, buf, sizeof(buf))) > 0) {
            write(dst_fd, buf, chunk);
        }
        espfs_fclose(src_file);
        close(dst_fd);
        return 0;
    }

    return rename(overlay_src, overlay_dst);
}

static DIR* vfs_espfs_opendir(void *ctx, const char *name)
{
    vfs_espfs_t *vfs_espfs = (vfs_espfs_t *) ctx;
    vfs_espfs_dir_t *dir = calloc(1, sizeof(vfs_espfs_dir_t));
    if (!dir) {
        errno = ENOMEM;
        return NULL;
    }

    strlcpy(dir->path, name, sizeof(dir->path));

    espfs_stat_t s;
    if (!espfs_stat(vfs_espfs->fs, dir->path, &s) ||
            s.type != ESPFS_TYPE_DIR) {
        free(dir);
        return NULL;
    }
    dir->offset = s.index + 1;

    return (DIR *) dir;
}

static int vfs_espfs_readdir_r(void *ctx, DIR *pdir, struct dirent *entry,
        struct dirent **out_dirent);
static struct dirent *vfs_espfs_readdir(void *ctx, DIR *pdir)
{
    vfs_espfs_dir_t *dir = (vfs_espfs_dir_t *) pdir;
    struct dirent *out_dirent;
    int err = vfs_espfs_readdir_r(ctx, pdir, &dir->e, &out_dirent);
    if (err != 0) {
        errno = err;
        return NULL;
    }
    return out_dirent;
}

static int vfs_espfs_readdir_r(void *ctx, DIR *pdir, struct dirent *entry,
        struct dirent **out_dirent)
{
    vfs_espfs_t *vfs_espfs = (vfs_espfs_t *) ctx;
    vfs_espfs_dir_t *dir = (vfs_espfs_dir_t *) pdir;

    if (dir->done) {
        *out_dirent = NULL;
        return 0;
    }

    while (dir->offset < vfs_espfs->fs->header->num_objects) {
        const char *path = espfs_get_path(vfs_espfs->fs, dir->offset);

        if (vfs_espfs->flags & VFS_ESPFS_USE_OVERLAY) {
            char overlay_path[256];
            int len = strlcpy(overlay_path, vfs_espfs->overlay_path,
                    sizeof(overlay_path));
            len += strlcpy(overlay_path + len, "/", sizeof(overlay_path) - len);
            strlcpy(overlay_path + len, path, sizeof(overlay_path) - len);

            struct stat st;
            if (stat(path, &st)) {
                dir->offset++;
                continue;
            }
        }

        espfs_stat_t s;
        espfs_stat(vfs_espfs->fs, path, &s);

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
        entry->d_type = (s.type == ESPFS_TYPE_DIR) ? DT_DIR : DT_REG;
        strlcpy(entry->d_name, path, sizeof(entry->d_name));
        dir->offset++;
        *out_dirent = entry;
        return 0;
    }

    if (vfs_espfs->flags & VFS_ESPFS_USE_OVERLAY) {
        if (!dir->overlay_dir) {
            char overlay_path[256];
            int len = strlcpy(overlay_path, vfs_espfs->overlay_path,
                    sizeof(overlay_path));
            len += strlcpy(overlay_path + len, "/", sizeof(overlay_path) - len);
            strlcpy(overlay_path + len, dir->path, sizeof(overlay_path) - len);
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

static long vfs_espfs_telldir(void *ctx, DIR *pdir)
{
    vfs_espfs_dir_t *dir = (vfs_espfs_dir_t *) pdir;
    return dir->offset;
}

static void vfs_espfs_seekdir(void *ctx, DIR *pdir, long offset)
{
    vfs_espfs_t *vfs_espfs = (vfs_espfs_t *) ctx;
    vfs_espfs_dir_t *dir = (vfs_espfs_dir_t *) pdir;

    if (offset < dir->offset) {
        if (dir->overlay_dir) {
            closedir(dir->overlay_dir);
        }
        dir->offset = 0;
    }
    if (offset < vfs_espfs->fs->header->num_objects) {
        dir->offset = offset;
    } else {
        dir->offset = vfs_espfs->fs->header->num_objects;
    }
    while (dir->offset < offset) {
        struct dirent *tmp;
        vfs_espfs_readdir_r(ctx, pdir, &dir->e, &tmp);
        if (tmp == NULL) {
            break;
        }
    }
}

static int vfs_espfs_closedir(void *ctx, DIR *pdir)
{
    vfs_espfs_dir_t *dir = (vfs_espfs_dir_t *) pdir;

    if (dir->overlay_dir) {
        closedir(dir->overlay_dir);
    }
    free(dir);
    return 0;
}

static int vfs_espfs_mkdir(void *ctx, const char *name, mode_t mode)
{
    vfs_espfs_t *vfs_espfs = (vfs_espfs_t *) ctx;

    if (vfs_espfs->flags & VFS_ESPFS_USE_OVERLAY) {
        char overlay_path[256];
        int len = strlcpy(overlay_path, vfs_espfs->overlay_path,
                sizeof(overlay_path));
        len += strlcpy(overlay_path + len, "/", sizeof(overlay_path) - len);
        strlcpy(overlay_path + len, name, sizeof(overlay_path) - len);

        return mkdir(overlay_path, mode);
    }

    return -1;
}

static int vfs_espfs_rmdir(void *ctx, const char *name)
{
    vfs_espfs_t *vfs_espfs = (vfs_espfs_t *) ctx;

    if (vfs_espfs->flags & VFS_ESPFS_USE_OVERLAY) {
        char overlay_path[256];
        int len = strlcpy(overlay_path, vfs_espfs->overlay_path,
                sizeof(overlay_path));
        len += strlcpy(overlay_path + len, "/", sizeof(overlay_path) - len);
        strlcpy(overlay_path + len, name, sizeof(overlay_path) - len);

        return rmdir(overlay_path);
    }

    return -1;
}


static int vfs_espfs_fcntl(void *ctx, int fd, int cmd, int arg)
{
    vfs_espfs_t *vfs_espfs = (vfs_espfs_t *) ctx;

    if (fd < 0 || fd >= vfs_espfs->max_files) {
        return -1;
    }

    if (!vfs_espfs->files[fd].used) {
        return -1;
    }

    if (vfs_espfs->files[fd].overlay) {
        return fcntl(vfs_espfs->files[fd].fd, cmd, arg);
    }

    return -1;
}

static int vfs_espfs_ioctl(void *ctx, int fd, int cmd, va_list args)
{
    vfs_espfs_t *vfs_espfs = (vfs_espfs_t *) ctx;

    if (fd < 0 || fd >= vfs_espfs->max_files) {
        return -1;
    }

    if (!vfs_espfs->files[fd].used) {
        return -1;
    }

    if (vfs_espfs->files[fd].overlay) {
        char *argp = va_arg(args, char *);
        return ioctl(vfs_espfs->files[fd].fd, cmd, argp);
    }

    return -1;
}

static int vfs_espfs_fsync(void *ctx, int fd)
{
    vfs_espfs_t *vfs_espfs = (vfs_espfs_t *) ctx;

    if (fd < 0 || fd >= vfs_espfs->max_files) {
        return -1;
    }

    if (!vfs_espfs->files[fd].used) {
        return -1;
    }

    if (vfs_espfs->files[fd].overlay) {
        return fsync(vfs_espfs->files[fd].fd);
    }

    return -1;
}

esp_err_t esp_vfs_espfs_register(const esp_vfs_espfs_conf_t *conf)
{
    assert(conf->base_path != NULL);
    assert(conf->fs != NULL);

    const esp_vfs_t vfs = {
        .flags = ESP_VFS_FLAG_CONTEXT_PTR,
        .write_p = &vfs_espfs_write,
        .lseek_p = &vfs_espfs_lseek,
        .read_p = &vfs_espfs_read,
        .open_p = &vfs_espfs_open,
        .close_p = &vfs_espfs_close,
        .fstat_p = &vfs_espfs_fstat,
        .stat_p = &vfs_espfs_stat,
        .link_p = &vfs_espfs_link,
        .unlink_p = &vfs_espfs_unlink,
        .rename_p = &vfs_espfs_rename,
        .opendir_p = &vfs_espfs_opendir,
        .readdir_p = &vfs_espfs_readdir,
        .readdir_r_p = &vfs_espfs_readdir_r,
        .telldir_p = &vfs_espfs_telldir,
        .seekdir_p = &vfs_espfs_seekdir,
        .closedir_p = &vfs_espfs_closedir,
        .mkdir_p = &vfs_espfs_mkdir,
        .rmdir_p = &vfs_espfs_rmdir,
        .fcntl_p = &vfs_espfs_fcntl,
        .ioctl_p = &vfs_espfs_ioctl,
        .fsync_p = &vfs_espfs_fsync,
    };

    int index;
    if (esp_espfs_get_empty(&index) != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }

    vfs_espfs_t *vfs_espfs = calloc(1, sizeof(vfs_espfs_t) +
            (sizeof(vfs_espfs_file_t *) * conf->max_files));
    if (vfs_espfs == NULL) {
        ESPFS_LOGE(__func__, "vfs_espfs could not be alloc'd");
        return ESP_ERR_NO_MEM;
    }

    vfs_espfs->fs = conf->fs;
    vfs_espfs->max_files = conf->max_files;
    strlcpy(vfs_espfs->base_path, conf->base_path, ESP_VFS_PATH_MAX + 1);
    if (conf->overlay_path) {
        vfs_espfs->flags |= VFS_ESPFS_USE_OVERLAY;
        strlcpy(vfs_espfs->overlay_path, conf->overlay_path,
                ESP_VFS_PATH_MAX + 1);
    }

    esp_err_t err = esp_vfs_register(vfs_espfs->base_path, &vfs, vfs_espfs);
    if (err != ESP_OK) {
        free(vfs_espfs);
        return err;
    }

    s_vfs_espfs[index] = vfs_espfs;
    return ESP_OK;
}
