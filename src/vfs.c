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
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/fcntl.h>
#include <sys/stat.h>


#define CHUNK_SIZE 1024U

#ifndef CONFIG_FROGFS_MAX_PARTITIONS
# define CONFIG_FROGFS_MAX_PARTITIONS 1
#endif

#define MIN(a, b) ({ \
    __typeof__(a) _a = a; \
    __typeof__(b) _b = b; \
    _a < _b ? _a : _b; \
})

typedef struct {
    int fd;
    frogfs_f_t *f;
} frogfs_vfs_f_t;

#ifdef CONFIG_VFS_SUPPORT_DIR
typedef struct {
    DIR *dir;
    DIR *overlay_dir;
    struct dirent overlay_ent;
    char overlay_path[PATH_MAX];
    frogfs_d_t *frogfs_dir;
    long offset;
} frogfs_vfs_d_t;
#endif

typedef struct {
    frogfs_fs_t *fs;
    char base_path[ESP_VFS_PATH_MAX + 1];
    char overlay[ESP_VFS_PATH_MAX + 1];
    bool have_overlay;
    bool flat;
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
    char *overlay;

    if (!vfs->have_overlay) {
        return NULL;
    }

    if (asprintf(&overlay, "%s%s", vfs->overlay, path) < 0) {
        return NULL;
    }

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

    char *overlay = frogfs_get_overlay(vfs, path);
    if (overlay != NULL) {
        vfs->files[fd].fd = open(overlay, flags, mode);
        free(overlay);

        if (vfs->files[fd].fd >= 0) {
            return fd;
        }
    }

    if (((flags & O_ACCMODE) == O_WRONLY) | ((flags & O_ACCMODE) == O_RDWR)) {
        return -1;
    }
    if (flags & (O_APPEND | O_CREAT | O_TRUNC)) {
        return -1;
    }

    const frogfs_entry_t *entry = frogfs_get_entry(vfs->fs, path);
    if (entry == NULL) {
        return -1;
    }

    vfs->files[fd].f = frogfs_open(vfs->fs, entry, 0);
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
        st->st_size = f->flags & FROGFS_OPEN_RAW ? fst.data_sz : fst.real_sz;
        st->st_spare4[0] = FROGFS_MAGIC;
        st->st_spare4[1] = fst.compression;
        return 0;
    }

    return -1;
}

static int frogfs_vfs_stat(void *ctx, const char *path, struct stat *st)
{
    frogfs_vfs_t *vfs = (frogfs_vfs_t *) ctx;

    char *overlay = frogfs_get_overlay(vfs, path);
    if (overlay != NULL) {
        int res = stat(overlay, st);
        free(overlay);

        if (res >= 0) {
            return res;
        }
    }

    const frogfs_entry_t *entry = frogfs_get_entry(vfs->fs, path);
    if (entry == NULL) {
        return -1;
    }

    frogfs_stat_t s;
    frogfs_stat(vfs->fs, entry, &s);
    memset(st, 0, sizeof(struct stat));
    st->st_mode = S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
    st->st_mode |= (s.type == FROGFS_ENTRY_TYPE_FILE) ? S_IFREG : S_IFDIR;
    st->st_size = s.real_sz;
    st->st_spare4[0] = FROGFS_MAGIC;
    st->st_spare4[1] = s.compression;
    return 0;
}

static int frogfs_vfs_link(void *ctx, const char *n1, const char *n2)
{
    frogfs_vfs_t *vfs = (frogfs_vfs_t *) ctx;

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

    const frogfs_entry_t *entry = frogfs_get_entry(vfs->fs, src);
    if (entry == NULL) {
        ret = -1;
        goto out;
    }

    frogfs_f_t *src_file = frogfs_open(vfs->fs, entry, 0);
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

    if (vfs->flat && strcmp(path, "/") != 0) {
        return NULL;
    }

    frogfs_vfs_d_t *d = calloc(1, sizeof(frogfs_vfs_d_t));
    if (d == NULL) {
        errno = ENOMEM;
        return NULL;
    }

#if defined(CONFIG_FROGFS_OVERLAY_DIR)
    if (vfs->have_overlay) {
        snprintf(d->overlay_path, sizeof(d->overlay_path), "%s%s",
                vfs->overlay, path);
        d->overlay_dir = opendir(d->overlay_path);
    }
#endif

    if (vfs->flat) {
        d->frogfs_dir = frogfs_opendir(vfs->fs, NULL);
    } else {
        const frogfs_entry_t *entry = frogfs_get_entry(vfs->fs, path);
        if (entry != NULL) {
            d->frogfs_dir = frogfs_opendir(vfs->fs, entry);
        }
    }

    return (DIR *) d;
}

static int frogfs_vfs_readdir_r(void *ctx, DIR *pdir, struct dirent *entry,
        struct dirent **out_ent);
static struct dirent *frogfs_vfs_readdir(void *ctx, DIR *pdir)
{
    frogfs_vfs_d_t *d = (frogfs_vfs_d_t *) pdir;
    struct dirent *out_ent;

    int err = frogfs_vfs_readdir_r(ctx, pdir, &d->overlay_ent, &out_ent);
    if (err != 0) {
        errno = err;
        return NULL;
    }

    return out_ent;
}

static int frogfs_vfs_readdir_r(void *ctx, DIR *pdir, struct dirent *ent,
        struct dirent **out_ent)
{
    frogfs_vfs_t *vfs = (frogfs_vfs_t *) ctx;
    frogfs_vfs_d_t *d = (frogfs_vfs_d_t *) pdir;
    char *s, path[PATH_MAX];

#if defined(CONFIG_FROGFS_OVERLAY_DIR)
    if (!(d->offset & 0x1000000)) {
        if (vfs->have_overlay && d->overlay_dir) {
            struct dirent *new_ent;
            if ((new_ent = readdir(d->overlay_dir)) != NULL) {
                ent->d_ino = new_ent->d_ino;
                ent->d_type = new_ent->d_type;
                snprintf(ent->d_name, sizeof(ent->d_name), "/%.254s",
                        new_ent->d_name);
                *out_ent = ent;
                d->offset++;
                return 0;
            }
        }
        d->offset = 0x1000000;
    }
#else
    d->offset |= 0x1000000;
#endif

    if (d->frogfs_dir) {
        while ((d->offset & ~0x1000000) < vfs->fs->num_entries) {
            const frogfs_entry_t *entry = frogfs_readdir(d->frogfs_dir);
            if (!entry) {
                *out_ent = NULL;
                return 0;
            }

            d->offset += 1;
            s = frogfs_get_path(vfs->fs, entry);

#if defined(CONFIG_FROGFS_OVERLAY_DIR)
            if (vfs->have_overlay && !FROGFS_ISDIR(entry)) {
                snprintf(path, sizeof(path), "%s/%s", vfs->overlay, s);
                struct stat st;
                if (stat(path, &st) == 0) {
                    continue;
                }
            }
#endif
            snprintf(path, sizeof(path), "/%s", s);
            free(s);

            ent->d_ino = 0;
            strlcpy(ent->d_name, path, sizeof(ent->d_name));
            if (FROGFS_ISDIR(entry)) {
                ent->d_type = DT_DIR;
            } else {
                ent->d_type = DT_REG;
            }

            *out_ent = ent;
            return 0;
        }
    }

    *out_ent = NULL;
    return 0;
}

static long frogfs_vfs_telldir(void *ctx, DIR *pdir)
{
    frogfs_vfs_d_t *d = (frogfs_vfs_d_t *) pdir;

    return d->offset;
}

static void frogfs_vfs_seekdir(void *ctx, DIR *pdir, long offset)
{
    frogfs_vfs_d_t *d = (frogfs_vfs_d_t *) pdir;

#if defined(CONFIG_FROGFS_OVERLAY_DIR)
    if (d->overlay_dir) {
        rewinddir(d->overlay_dir);
    }
#endif
    frogfs_rewinddir(d->frogfs_dir);
    d->offset = 0;

    while (d->offset < offset) {
        if (frogfs_vfs_readdir(ctx, pdir) == NULL) {
            break;
        }
    }
}

static int frogfs_vfs_closedir(void *ctx, DIR *pdir)
{
    frogfs_vfs_d_t *d = (frogfs_vfs_d_t *) pdir;

#if defined(CONFIG_FROGFS_OVERLAY_DIR)
    if (d->overlay_dir) {
        closedir(d->overlay_dir);
    }
#endif
    frogfs_closedir(d->frogfs_dir);
    free(d);
    return 0;
}

static int frogfs_vfs_mkdir(void *ctx, const char *path, mode_t mode)
{
    frogfs_vfs_t *vfs = (frogfs_vfs_t *) ctx;

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
            const frogfs_entry_t *entry = (const void *) vfs->files[fd].f->file;
            frogfs_close(vfs->files[fd].f);
            vfs->files[fd].f = frogfs_open(fs, entry, FROGFS_OPEN_RAW);
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

static int frogfs_vfs_access(void *ctx, const char *path, int amode)
{
    frogfs_vfs_t *vfs = (frogfs_vfs_t *) ctx;

    char *overlay = frogfs_get_overlay(vfs, path);
    if (overlay != NULL) {
        int ret = access(overlay, amode);
        free(overlay);
        if (ret == 0) {
            return 0;
        }
    }

    const frogfs_entry_t *entry = frogfs_get_entry(vfs->fs, path);
    if (entry == NULL) {
        errno = ENOENT;
        return -1;
    }

    if (amode & (W_OK | X_OK)) {
        errno = EACCES;
        return -1;
    }

    return -1;
}

static int frogfs_vfs_truncate(void *ctx, const char *path, off_t length)
{
    frogfs_vfs_t *vfs = (frogfs_vfs_t *) ctx;

    char *overlay = frogfs_get_overlay(vfs, path);
    if (overlay != NULL) {
        ssize_t ret = truncate(overlay, length);
        if (ret == 0) {
            free(overlay);
            return 0;
        }

        const frogfs_entry_t *entry = frogfs_get_entry(vfs->fs, path);
        if (!entry) {
            free(overlay);
            return -1;
        }

        int overlay_fd = open(overlay, O_RDWR | O_CREAT | O_TRUNC,
                S_IRWXU | S_IRWXG | S_IRWXO);
        if (overlay_fd < 0) {
            free(overlay);
            return -1;
        }

        frogfs_f_t *f = frogfs_open(vfs->fs, entry, 0);
        if (!f) {
            close(overlay_fd);
            truncate(overlay, length);
            free(overlay);
            return 0;
        }
        free(overlay);

        char *buf = malloc(CHUNK_SIZE);
        if (!buf) {
            frogfs_close(f);
            close(overlay_fd);
            return -1;
        }

        ssize_t read_chunk, written;

        while (length != 0) {
            ret = frogfs_read(f, buf, MIN(length, CHUNK_SIZE));
            if (ret < 0) {
                free(buf);
                frogfs_close(f);
                close(overlay_fd);
                return ret;
            }
            read_chunk = ret;

            written = 0;
            while (written < read_chunk) {
                ret = write(overlay_fd, buf + written, read_chunk - written);
                if (ret < 0) {
                    free(buf);
                    frogfs_close(f);
                    close(overlay_fd);
                    return ret;
                }
                written += ret;
            }

            length -= written;
        }

        free(buf);
        frogfs_close(f);
        close(overlay_fd);
        return 0;
    }

    return -1;
}

static int frogfs_vfs_ftruncate(void *ctx, int fd, off_t length)
{
    frogfs_vfs_t *vfs = (frogfs_vfs_t *) ctx;

    if (fd < 0 || fd >= vfs->max_files) {
        return -1;
    }

    if (vfs->files[fd].fd >= 0) {
        return ftruncate(vfs->files[fd].fd, length);
    }

    if (vfs->files[fd].f != NULL) {
        frogfs_f_t *f = vfs->files[fd].f;
        char *path = frogfs_get_path(vfs->fs, &f->file->entry);
        char *overlay = frogfs_get_overlay(vfs, path);
        free(path);
        if (overlay == NULL) {
            return -1;
        }

        char *buf = malloc(CHUNK_SIZE);
        if (!buf) {
            free(overlay);
            errno = ENOMEM;
            return -1;
        }

        int overlay_fd = open(overlay, O_RDWR | O_CREAT | O_TRUNC,
                S_IRWXU | S_IRWXG | S_IRWXO);
        free(overlay);
        if (overlay_fd < 0) {
            errno = EACCES;
            return -1;
        }

        vfs->files[fd].fd = overlay_fd;
        vfs->files[fd].f = NULL;
        frogfs_seek(f, 0, SEEK_SET);

        ssize_t ret, read_chunk, written;

        while (length != 0) {
            ret = frogfs_read(f, buf, MIN(length, CHUNK_SIZE));
            if (ret < 0) {
                free(buf);
                frogfs_close(f);
                return ret;
            }
            read_chunk = ret;

            written = 0;
            while (written < read_chunk) {
                ret = write(overlay_fd, buf + written, read_chunk - written);
                if (ret < 0) {
                    free(buf);
                    frogfs_close(f);
                    return ret;
                }
                written += ret;
            }

            length -= written;
        }

        free(buf);
        frogfs_close(f);
        return 0;
    }

    return -1;
}

static int frogfs_vfs_utime(void *ctx, const char *path,
        const struct utimbuf *times)
{
    frogfs_vfs_t *vfs = (frogfs_vfs_t *) ctx;

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
    assert(conf->base_path != NULL);

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
    strlcpy(vfs->base_path, conf->base_path, sizeof(vfs->base_path));
    if (conf->overlay) {
        vfs->have_overlay = true;
        strlcpy(vfs->overlay, conf->overlay, sizeof(vfs->overlay));
    }
    vfs->flat = conf->flat;
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
