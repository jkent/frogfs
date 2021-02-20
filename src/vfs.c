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

#include <stdbool.h>
#include <stdlib.h>
#include <sys/fcntl.h>


typedef struct {
    espfs_fs_t *fs;
    char base_path[ESP_VFS_PATH_MAX + 1];
    espfs_file_t** files;
    size_t max_files;
} vfs_espfs_t;

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

static int vfs_espfs_open(void *ctx, const char *path, int flags, int mode)
{
    vfs_espfs_t *vfs_espfs = (vfs_espfs_t *) ctx;

    if (flags & (O_CREAT | O_WRONLY | O_RDWR)) {
        return -1;
    }

    int fd;
    for (fd = 0; fd < vfs_espfs->max_files; fd++) {
        if (vfs_espfs->files[fd] == NULL) {
            break;
        }
    }

    if (fd >= vfs_espfs->max_files) {
        return -1;
    }

    vfs_espfs->files[fd] = espfs_open(vfs_espfs->fs, path);
    if (vfs_espfs->files[fd] == NULL) {
        return -1;
    }

    return fd;
}

static int vfs_espfs_fstat(void *ctx, int fd, struct stat *st)
{
    vfs_espfs_t *vfs_espfs = (vfs_espfs_t *) ctx;

    if (fd < 0 || fd >= vfs_espfs->max_files) {
        return -1;
    }

    espfs_file_t *fp = vfs_espfs->files[fd];
    if (fp == NULL) {
        return -1;
    }

    memset(st, 0, sizeof(struct stat));
    st->st_size = fp->header->file_len;
    st->st_mode = S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH | S_IFREG;
    st->st_spare4[0] = ESPFS_MAGIC;
    st->st_spare4[1] = fp->header->flags;
    return 0;
}

static int vfs_espfs_close(void *ctx, int fd);
static int vfs_espfs_stat(void *ctx, const char *path, struct stat *st)
{
    vfs_espfs_t *vfs_espfs = (vfs_espfs_t *) ctx;

    espfs_stat_t s;
    if (!espfs_stat(vfs_espfs->fs, path, &s)) {
        return -1;
    }

    st->st_mode = S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
    st->st_mode |= (s.type == ESPFS_TYPE_FILE) ? S_IFREG : S_IFDIR;
    st->st_size = s.size;
    st->st_spare4[0] = ESPFS_MAGIC;
    st->st_spare4[1] = s.flags;
    return 0;
}

static ssize_t vfs_espfs_read(void *ctx, int fd, void *data, size_t size)
{
    vfs_espfs_t *vfs_espfs = (vfs_espfs_t *) ctx;

    if (fd < 0 || fd >= vfs_espfs->max_files) {
        return -1;
    }

    espfs_file_t *fp = vfs_espfs->files[fd];
    if (fp == NULL) {
        return -1;
    }
    return espfs_read(fp, data, size);
}

static ssize_t vfs_espfs_write(void *ctx, int fd, const void *data, size_t size)
{
    return -1;
}

static off_t vfs_espfs_lseek(void *ctx, int fd, off_t size, int mode)
{
    vfs_espfs_t *vfs_espfs = (vfs_espfs_t *) ctx;

    if (fd < 0 || fd >= vfs_espfs->max_files) {
        return -1;
    }

    espfs_file_t *fp = vfs_espfs->files[fd];
    if (fp == NULL) {
        return -1;
    }

    return espfs_seek(fp, size, mode);
}

static int vfs_espfs_close(void *ctx, int fd)
{
    vfs_espfs_t *vfs_espfs = (vfs_espfs_t *) ctx;

    if (fd < 0 || fd >= vfs_espfs->max_files) {
        return -1;
    }

    espfs_file_t *fp = vfs_espfs->files[fd];
    if (fp == NULL) {
        return -1;
    }

    espfs_close(fp);
    vfs_espfs->files[fd] = NULL;
    return 0;
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
    };

    int index;
    if (esp_espfs_get_empty(&index) != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }

    vfs_espfs_t *vfs_espfs = calloc(1, sizeof(vfs_espfs_t));
    if (vfs_espfs == NULL) {
        ESPFS_LOGE(__func__, "esp_espfs could not be molloc'd");
        return ESP_ERR_NO_MEM;
    }

    vfs_espfs->fs = conf->fs;
    vfs_espfs->max_files = conf->max_files;
    vfs_espfs->files = calloc(conf->max_files, sizeof(espfs_file_t*));
    if (vfs_espfs->files == NULL) {
        ESPFS_LOGE(__func__, "allocating files failed");
        free(vfs_espfs);
        return ESP_ERR_NO_MEM;
    }

    strlcat(vfs_espfs->base_path, conf->base_path, ESP_VFS_PATH_MAX + 1);
    esp_err_t err = esp_vfs_register(conf->base_path, &vfs, vfs_espfs);
    if (err != ESP_OK) {
        free(vfs_espfs);
        return err;
    }

    s_vfs_espfs[index] = vfs_espfs;
    return ESP_OK;
}
