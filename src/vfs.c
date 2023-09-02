/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "log.h"
#include "frogfs/frogfs.h"
#include "frogfs/vfs.h"

#include "esp_err.h"
#include "esp_vfs.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
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

#define HAVE_OVERLAY() (vfs->overlay[0])

typedef struct {
    int fd;
    frogfs_f_t *f;
} frogfs_vfs_f_t;

#ifdef CONFIG_VFS_SUPPORT_DIR
typedef struct {
    DIR *dir;
#ifdef CONFIG_FROGFS_SUPPORT_DIR
    frogfs_d_t *d;
#endif
    long offset;
    struct dirent e;
} frogfs_vfs_d_t;
#endif

typedef struct {
    frogfs_fs_t *fs;
    char base_path[ESP_VFS_PATH_MAX + 1];
    char overlay[ESP_VFS_PATH_MAX + 1];
    size_t max_files;
    frogfs_vfs_f_t files[];
} frogfs_vfs_t;

static frogfs_vfs_t *s_frogfs_vfs[CONFIG_FROGFS_MAX_PARTITIONS];

static esp_err_t frogfs_get_empty(int *index)
{
    int i;

    for (i = 0; i < CONFIG_FROGFS_MAX_PARTITIONS; i++) {
        if (s_frogfs_vfs[i] == NULL) {
            *index = i;
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

char *frogfs_get_overlay(frogfs_vfs_t *vfs, const char *path)
{
    char *overlay = malloc(strlen(vfs->overlay) + strlen(path) + 2);
    if (overlay == NULL) {
        return NULL;
    }

    strcpy(overlay, vfs->overlay);
    strcat(overlay, "/");
    strcat(overlay, path);
    return overlay;
}

static ssize_t frogfs_vfs_write(void *ctx, int fd, const void *data,
                                size_t size)
{
    frogfs_vfs_t *vfs = (frogfs_vfs_t *) ctx;

    if (fd < 0 || fd >= vfs->max_files) {
        return -1;
    }

    if (vfs->files[fd].fd >= 0) {
        return write(vfs->files[fd].fd, data, size);
    }

    return -1;
}

static off_t frogfs_vfs_lseek(void *ctx, int fd, off_t size, int mode)
{
    frogfs_vfs_t *vfs = (frogfs_vfs_t *) ctx;

    if (fd < 0 || fd >= vfs->max_files) {
        return -1;
    }

    if (vfs->files[fd].fd >= 0) {
        return lseek(vfs->files[fd].fd, size, mode);
    }

    if (vfs->files[fd].f != NULL) {
        frogfs_f_t *f = vfs->files[fd].f;
        return frogfs_seek(f, size, mode);
    }

    return -1;
}

static ssize_t frogfs_vfs_read(void *ctx, int fd, void *data, size_t size)
{
    frogfs_vfs_t *vfs = (frogfs_vfs_t *) ctx;

    if (fd < 0 || fd >= vfs->max_files) {
        return -1;
    }

    if (vfs->files[fd].fd >= 0) {
        return read(vfs->files[fd].fd, data, size);
    }

    if (vfs->files[fd].f != NULL) {
        frogfs_f_t *f = vfs->files[fd].f;
        return frogfs_read(f, data, size);
    }

    return -1;
}

static int frogfs_vfs_open(void *ctx, const char *path, int flags, int mode)
{
    frogfs_vfs_t *vfs = (frogfs_vfs_t *) ctx;

    int fd;
    for (fd = 0; fd < vfs->max_files; fd++) {
        if (vfs->files[fd].fd < 0 && vfs->files[fd].f == NULL) {
            break;
        }
    }
    if (fd >= vfs->max_files) {
        return -1;
    }

    if (HAVE_OVERLAY()) {
        char *overlay = frogfs_get_overlay(vfs, path);
        if (!overlay) {
            return -1;
        }

        vfs->files[fd].fd = open(overlay, flags, mode);
        free(overlay);

        if (vfs->files[fd].fd >= 0) {
            return fd;
        }
    }

    if (flags & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC)) {
        return -1;
    }

    const frogfs_obj_t *obj = frogfs_obj_from_path(vfs->fs, path);
    if (obj == NULL) {
        return -1;
    }

    vfs->files[fd].f = frogfs_open(vfs->fs, obj, 0);
    if (vfs->files[fd].f != NULL) {
        return fd;
    }

    return -1;
}

static int frogfs_vfs_close(void *ctx, int fd)
{
    frogfs_vfs_t *vfs = (frogfs_vfs_t *) ctx;

    if (fd < 0 || fd >= vfs->max_files) {
        return -1;
    }

    if (vfs->files[fd].fd >= 0) {
        int overlay_fd = vfs->files[fd].fd;
        vfs->files[fd].fd = -1;
        return close(overlay_fd);
    }

    if (vfs->files[fd].f) {
        frogfs_f_t *f = vfs->files[fd].f;
        frogfs_close(f);
        vfs->files[fd].f = NULL;
        return 0;
    }

    return -1;
}

static int frogfs_vfs_fstat(void *ctx, int fd, struct stat *st)
{
    frogfs_vfs_t *vfs = (frogfs_vfs_t *) ctx;

    if (fd < 0 || fd >= vfs->max_files) {
        return -1;
    }

    if (vfs->files[fd].fd >= 0) {
        return fstat(vfs->files[fd].fd, st);
    }

    if (vfs->files[fd].f != NULL) {
        frogfs_f_t *f = vfs->files[fd].f;
        memset(st, 0, sizeof(struct stat));
        st->st_mode = S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH |
                S_IXOTH | S_IFREG;

        frogfs_stat_t fst;
        frogfs_stat(vfs->fs, (void *) f->file, &fst);
        st->st_size = (f->flags & FROGFS_OPEN_RAW && fst.size_compressed) ?
                fst.size_compressed : fst.size;
        st->st_spare4[0] = FROGFS_MAGIC;
        st->st_spare4[1] = fst.compression;
        return 0;
    }

    return -1;
}

static int frogfs_vfs_stat(void *ctx, const char *path, struct stat *st)
{
    frogfs_vfs_t *vfs = (frogfs_vfs_t *) ctx;

    if (HAVE_OVERLAY()) {
        char *overlay = frogfs_get_overlay(vfs, path);
        if (!overlay) {
            return -1;
        }

        int res = stat(overlay, st);
        free(overlay);

        if (res >= 0) {
            return res;
        }
    }

    const frogfs_obj_t *obj = frogfs_obj_from_path(vfs->fs, path);
    if (obj == NULL) {
        return -1;
    }

    frogfs_stat_t s;
    frogfs_stat(vfs->fs, obj, &s);
    memset(st, 0, sizeof(struct stat));
    st->st_mode = S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
    st->st_mode |= (s.type == FROGFS_OBJ_TYPE_FILE) ? S_IFREG : S_IFDIR;
    st->st_size = s.size;
    st->st_spare4[0] = FROGFS_MAGIC;
    st->st_spare4[1] = s.compression;
    return 0;
}

static int frogfs_vfs_link(void *ctx, const char *n1, const char *n2)
{
    frogfs_vfs_t *vfs = (frogfs_vfs_t *) ctx;

    if (!HAVE_OVERLAY()) {
        return -1;
    }

    char *overlay_n1 = frogfs_get_overlay(vfs, n1);
    if (overlay_n1 == NULL) {
        return -1;
    }

    char *overlay_n2 = frogfs_get_overlay(vfs, n2);
    if (overlay_n2 == NULL) {
        free(overlay_n1);
        return -1;
    }

    int ret = link(overlay_n1, overlay_n2);
    free(overlay_n1);
    free(overlay_n2);
    return ret;
}

static int frogfs_vfs_unlink(void *ctx, const char *path)
{
    frogfs_vfs_t *vfs = (frogfs_vfs_t *) ctx;

    if (!HAVE_OVERLAY()) {
        return -1;
    }

    char *overlay = frogfs_get_overlay(vfs, path);
    if (overlay == NULL) {
        return -1;
    }

    int ret = unlink(overlay);
    free(overlay);
    return ret;
}

static int frogfs_vfs_rename(void *ctx, const char *src, const char *dst)
{
    frogfs_vfs_t *vfs = (frogfs_vfs_t *) ctx;
    int ret = 0;

    if (!HAVE_OVERLAY()) {
        return -1;
    }

    char *overlay_src = frogfs_get_overlay(vfs, src);
    if (overlay_src == NULL) {
        return -1;
    }

    char *overlay_dst = frogfs_get_overlay(vfs, dst);
    if (overlay_dst == NULL) {
        free(overlay_src);
        return -1;
    }

    struct stat st;
    if (stat(overlay_src, &st) >= 0) {
        ret = rename(overlay_src, overlay_dst);
        goto out;
    }

    const frogfs_obj_t *obj = frogfs_obj_from_path(vfs->fs, src);
    if (obj == NULL) {
        ret = -1;
        goto out;
    }

    frogfs_f_t *src_file = frogfs_open(vfs->fs, obj, 0);
    if (src_file == NULL) {
        ret = -1;
        goto out;
    }

    int dst_fd = open(overlay_dst, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR |
            S_IWUSR);
    if (dst_fd < 0) {
        frogfs_close(src_file);
        ret = -1;
        goto out;
    }

    int chunk;
    char buf[512];
    while ((chunk = frogfs_read(src_file, buf, sizeof(buf))) > 0) {
        write(dst_fd, buf, chunk);
    }
    frogfs_close(src_file);
    close(dst_fd);

out:
    free(overlay_src);
    free(overlay_dst);
    return ret;
}

#ifdef CONFIG_VFS_SUPPORT_DIR
static DIR* frogfs_vfs_opendir(void *ctx, const char *path)
{
    frogfs_vfs_t *vfs = (frogfs_vfs_t *) ctx;

    frogfs_vfs_d_t *d = calloc(1, sizeof(frogfs_vfs_d_t));
    if (d == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    if (HAVE_OVERLAY()) {
        char *overlay = frogfs_get_overlay(vfs, path);
        if (overlay == NULL) {
            return NULL;
        }
        d->dir = opendir(overlay);
        free(overlay);
    }

#ifdef CONFIG_FROGFS_SUPPORT_DIR
    const frogfs_obj_t *obj = frogfs_obj_from_path(vfs->fs, path);
    if (obj != NULL) {
        d->d = frogfs_opendir(vfs->fs, obj);
    }
#endif

    return (DIR *) d;
}

static int frogfs_vfs_readdir_r(void *ctx, DIR *pdir, struct dirent *entry,
        struct dirent **out_dirent);
static struct dirent *frogfs_vfs_readdir(void *ctx, DIR *pdir)
{
    frogfs_vfs_d_t *dir = (frogfs_vfs_d_t *) pdir;
    struct dirent *out_dirent;
    int err = frogfs_vfs_readdir_r(ctx, pdir, &dir->e, &out_dirent);
    if (err != 0) {
        errno = err;
        return NULL;
    }
    return out_dirent;
}

static int frogfs_vfs_readdir_r(void *ctx, DIR *pdir, struct dirent *entry,
        struct dirent **out_dirent)
{
    frogfs_vfs_t *vfs = (frogfs_vfs_t *) ctx;
    frogfs_vfs_d_t *d = (frogfs_vfs_d_t *) pdir;
    bool end = true;

    if (HAVE_OVERLAY() && d->dir) {
        struct dirent *e;
        char name[256] = {0};
        rewinddir(d->dir);
        while ((e = readdir(d->dir))) {
            if (strcmp(e->d_name, entry->d_name) <= 0) {
                continue;
            }
            if (name[0] == '\0' || strcmp(e->d_name, name) < 0) {
                entry->d_ino = 0;
                entry->d_type = e->d_type;
                strcpy(entry->d_name, name);
                end = false;
            }
        }
    }

#ifdef CONFIG_FROGFS_SUPPORT_DIR
    if (d->d) {
        do {
            uint16_t pos = frogfs_telldir(d->d);
            const frogfs_obj_t *obj = frogfs_readdir(d->d);

            if (obj == NULL) {
                break;
            }

            const char *path = frogfs_path_from_obj(obj);
            const char *name = strrchr(path, '/');
            if (name == NULL) {
                name = path;
            } else {
                name++;
            }

            if (HAVE_OVERLAY()) {
                int n = strcmp(name, entry->d_name);
                if (n == 0) {
                    continue;
                }

                if (n > 0) {
                    frogfs_seekdir(d->d, pos);
                    break;
                }
            }

            entry->d_ino = 0;
            if (obj->type == FROGFS_OBJ_TYPE_FILE) {
                entry->d_type = DT_REG;
            } else if (obj->type == FROGFS_OBJ_TYPE_DIR) {
                entry->d_type = DT_DIR;
            } else {
                entry->d_type = DT_UNKNOWN;
            }
            strlcpy(entry->d_name, name, sizeof(entry->d_name));
            end = false;
        } while (false);
    }
#endif

    if (end) {
        *out_dirent = NULL;
        return 0;
    }

    d->offset++;
    *out_dirent = entry;
    return 0;
}

static long frogfs_vfs_telldir(void *ctx, DIR *pdir)
{
    frogfs_vfs_d_t *d = (frogfs_vfs_d_t *) pdir;

    return d->offset;
}

static void frogfs_vfs_seekdir(void *ctx, DIR *pdir, long offset)
{
    frogfs_vfs_t *vfs = (frogfs_vfs_t *) ctx;
    frogfs_vfs_d_t *d = (frogfs_vfs_d_t *) pdir;

    if (HAVE_OVERLAY()) {
        rewinddir(d->dir);
    }
#ifdef CONFIG_FROGFS_SUPPORT_DIR
    frogfs_rewinddir(d->d);
#endif
    d->offset = 0;

    while (d->offset < offset) {
        if (frogfs_vfs_readdir(ctx, pdir) == NULL) {
            break;
        }
    }
}

static int frogfs_vfs_closedir(void *ctx, DIR *pdir)
{
    frogfs_vfs_t *vfs = (frogfs_vfs_t *) ctx;
    frogfs_vfs_d_t *d = (frogfs_vfs_d_t *) pdir;

    if (HAVE_OVERLAY()) {
        closedir(d->dir);
    }
#ifdef CONFIG_FROGFS_SUPPORT_DIR
    frogfs_closedir(d->d);
#endif
    free(d);
    return 0;
}

static int frogfs_vfs_mkdir(void *ctx, const char *path, mode_t mode)
{
    frogfs_vfs_t *vfs = (frogfs_vfs_t *) ctx;

    if (!HAVE_OVERLAY()) {
        return -1;
    }

    char *overlay = frogfs_get_overlay(vfs, path);
    if (overlay == NULL) {
        return -1;
    }

    int ret = mkdir(overlay, mode);
    free(overlay);
    return ret;
}

static int frogfs_vfs_rmdir(void *ctx, const char *path)
{
    frogfs_vfs_t *vfs = (frogfs_vfs_t *) ctx;

    if (!HAVE_OVERLAY()) {
        return -1;
    }

    char *overlay = frogfs_get_overlay(vfs, path);
    if (overlay == NULL) {
        return -1;
    }

    int ret = rmdir(overlay);
    free(overlay);
    return ret;
}
#endif

static int frogfs_vfs_fcntl(void *ctx, int fd, int cmd, int arg)
{
    frogfs_vfs_t *vfs = (frogfs_vfs_t *) ctx;

    if (fd < 0 || fd >= vfs->max_files) {
        return -1;
    }

    if (vfs->files[fd].fd >= 0) {
        return fcntl(vfs->files[fd].fd, cmd, arg);
    }

    if (vfs->files[fd].f != NULL) {
        if (cmd == F_REOPEN_RAW) {
            const frogfs_fs_t *fs = vfs->files[fd].f->fs;
            const frogfs_obj_t *obj = (const void *) vfs->files[fd].f->file;
            frogfs_close(vfs->files[fd].f);
            vfs->files[fd].f = frogfs_open(fs, obj, FROGFS_OPEN_RAW);
            return 0;
        }
    }

    return -1;
}

static int frogfs_vfs_ioctl(void *ctx, int fd, int cmd, va_list args)
{
    frogfs_vfs_t *vfs = (frogfs_vfs_t *) ctx;

    if (fd < 0 || fd >= vfs->max_files) {
        return -1;
    }

    if (vfs->files[fd].fd >= 0) {
        char *argp = va_arg(args, char *);
        return ioctl(vfs->files[fd].fd, cmd, argp);
    }

    return -1;
}

static int frogfs_vfs_fsync(void *ctx, int fd)
{
    frogfs_vfs_t *vfs = (frogfs_vfs_t *) ctx;

    if (fd < 0 || fd >= vfs->max_files) {
        return -1;
    }

    if (vfs->files[fd].fd >= 0) {
        return fsync(vfs->files[fd].fd);
    }

    return -1;
}

/* TODO: frogfs needs this too */
static int frogfs_vfs_access(void *ctx, const char *path, int amode)
{
    frogfs_vfs_t *vfs = (frogfs_vfs_t *) ctx;

    if (HAVE_OVERLAY()) {
        char *overlay = frogfs_get_overlay(vfs, path);
        if (overlay == NULL) {
            return -1;
        }

        int ret = access(overlay, amode);
        free(overlay);
        return ret;
    }

    return -1;
}

/* TODO: if file doesn't exist on overlay; copy length bytes */
static int frogfs_vfs_truncate(void *ctx, const char *path, off_t length)
{
    frogfs_vfs_t *vfs = (frogfs_vfs_t *) ctx;

    if (!HAVE_OVERLAY()) {
        return -1;
    }

    char *overlay = frogfs_get_overlay(vfs, path);
    if (overlay == NULL) {
        return -1;
    }

    int ret = truncate(overlay, length);
    free(overlay);
    return ret;
}

/* TODO: if file doesn't exist on overlay; copy length bytes and reopen */
static int frogfs_vfs_ftruncate(void *ctx, int fd, off_t length)
{
    frogfs_vfs_t *vfs = (frogfs_vfs_t *) ctx;

    if (fd < 0 || fd >= vfs->max_files) {
        return -1;
    }

    if (vfs->files[fd].fd >= 0) {
        return ftruncate(vfs->files[fd].fd, length);
    }

    return -1;
}

static int frogfs_vfs_utime(void *ctx, const char *path,
        const struct utimbuf *times)
{
    frogfs_vfs_t *vfs = (frogfs_vfs_t *) ctx;

    if (!HAVE_OVERLAY()) {
        return -1;
    }

    char *overlay = frogfs_get_overlay(vfs, path);
    if (overlay == NULL) {
        return -1;
    }

    int ret = utime(overlay, times);
    free(overlay);
    return ret;
}

esp_err_t frogfs_vfs_register(const frogfs_vfs_conf_t *conf)
{
    assert(conf != NULL);
    assert(conf->fs != NULL);

    const esp_vfs_t funcs = {
        .flags = ESP_VFS_FLAG_CONTEXT_PTR,
        .write_p = &frogfs_vfs_write,
        .lseek_p = &frogfs_vfs_lseek,
        .read_p = &frogfs_vfs_read,
        .open_p = &frogfs_vfs_open,
        .close_p = &frogfs_vfs_close,
        .fstat_p = &frogfs_vfs_fstat,
#ifdef CONFIG_VFS_SUPPORT_DIR
        .stat_p = &frogfs_vfs_stat,
        .link_p = &frogfs_vfs_link,
        .unlink_p = &frogfs_vfs_unlink,
        .rename_p = &frogfs_vfs_rename,
        .opendir_p = &frogfs_vfs_opendir,
        .readdir_p = &frogfs_vfs_readdir,
        .readdir_r_p = &frogfs_vfs_readdir_r,
        .telldir_p = &frogfs_vfs_telldir,
        .seekdir_p = &frogfs_vfs_seekdir,
        .closedir_p = &frogfs_vfs_closedir,
        .mkdir_p = &frogfs_vfs_mkdir,
        .rmdir_p = &frogfs_vfs_rmdir,
#endif
        .fcntl_p = &frogfs_vfs_fcntl,
        .ioctl_p = &frogfs_vfs_ioctl,
        .fsync_p = &frogfs_vfs_fsync,
        .access_p = &frogfs_vfs_access,
        .truncate_p = &frogfs_vfs_truncate,
        .ftruncate_p = &frogfs_vfs_ftruncate,
        .utime_p = &frogfs_vfs_utime,
    };

    int index;
    if (frogfs_get_empty(&index) != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }

    frogfs_vfs_t *vfs = calloc(1, sizeof(frogfs_vfs_t) +
            (sizeof(frogfs_vfs_f_t) * conf->max_files));
    if (vfs == NULL) {
        LOGE("vfs could not be alloc'd");
        return ESP_ERR_NO_MEM;
    }

    vfs->fs = conf->fs;
    vfs->max_files = conf->max_files;
    if (conf->base_path) {
        strlcpy(vfs->base_path, conf->base_path, sizeof(vfs->base_path));
    }
    if (conf->overlay) {
        strlcpy(vfs->overlay, conf->overlay, sizeof(vfs->overlay));
    }
    for (int fd = 0; fd < vfs->max_files; fd++) {
        vfs->files[fd].fd = -1;
    }

    esp_err_t err = esp_vfs_register(vfs->base_path, &funcs, vfs);
    if (err != ESP_OK) {
        free(vfs);
        return err;
    }

    s_frogfs_vfs[index] = vfs;
    return ESP_OK;
}
