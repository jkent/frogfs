#include <esp_err.h>
#include <esp_log.h>
#include <esp_vfs.h>
#include <espfs.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/fcntl.h>

#include "esp_partition.h"
#include "sdkconfig.h"

#include "espfs_image.h"
#include "espfs_priv.h"
#include "espfs_vfs.h"

static const char *TAG = "ESPFS";

typedef struct {
    EspFs* fs;
    char base_path[ESP_VFS_PATH_MAX+1];
    EspFsFile** files;
    size_t max_files;
} esp_espfs_t;
static esp_espfs_t *_efs[CONFIG_ESPFS_MAX_PARTITIONS];


static esp_err_t esp_espfs_get_empty(int* index)
{
    int i;

    for (i = 0; i < CONFIG_ESPFS_MAX_PARTITIONS; i++) {
        if (_efs[i] == NULL) {
            *index = i;
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}


static int vfs_espfs_open(void* ctx, const char *path, int flags, int mode)
{
    esp_espfs_t* efs = (esp_espfs_t *)ctx;

    if (flags & (O_CREAT | O_WRONLY | O_RDWR)) {
        return -1;
    }

    int fd;
    for (fd = 0; fd < efs->max_files; fd++) {
        if (efs->files[fd] == NULL) {
            break;
        }
    }

    if (fd >= efs->max_files) {
        return -1;
    }

    efs->files[fd] = espFsOpen(efs->fs, path);
    if (efs->files[fd] == NULL) {
        return -1;
    }

    return fd;
}


static int vfs_espfs_fstat(void* ctx, int fd, struct stat* st)
{
    esp_espfs_t* efs = (esp_espfs_t *)ctx;

    if (fd < 0 || fd >= efs->max_files) {
        return -1;
    }

    EspFsFile *fp = efs->files[fd];
    memset(st, 0, sizeof(struct stat));
    st->st_size = fp->header->fileLenDecomp;
    st->st_mode = S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH | S_IFREG;
    st->st_spare4[0] = ESPFS_MAGIC;
    st->st_spare4[1] = fp->header->flags;
    return 0;
}


static int vfs_espfs_close(void* ctx, int fd);
static int vfs_espfs_stat(void* ctx, const char* path, struct stat* st)
{
    esp_espfs_t *efs = (esp_espfs_t *)ctx;

    EspFsStat s;
    if (!espFsStat(efs->fs, path, &s)) {
        return -1;
    }

    st->st_mode = S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
    st->st_mode |= (s.type == ESPFS_TYPE_FILE) ? S_IFREG : S_IFDIR;
    st->st_size = s.size;
    st->st_spare4[0] = ESPFS_MAGIC;
    st->st_spare4[1] = s.flags;
    return 0;
}


static ssize_t vfs_espfs_read(void* ctx, int fd, void* data, size_t size)
{
    esp_espfs_t* efs = (esp_espfs_t *)ctx;

    if (fd < 0 || fd >= efs->max_files) {
        return -1;
    }

    EspFsFile *fp = efs->files[fd];
    if (fp == NULL) {
        return -1;
    }
    return espFsRead(fp, data, size);
}


static ssize_t vfs_espfs_write(void* ctx, int fd, const void* data, size_t size)
{
    return -1;
}


static off_t vfs_espfs_lseek(void* ctx, int fd, off_t size, int mode)
{
    esp_espfs_t* efs = (esp_espfs_t *)ctx;

    if (fd < 0 || fd >= efs->max_files) {
        return -1;
    }

    EspFsFile *fp = efs->files[fd];
    if (fp == NULL) {
        return -1;
    } 

    return espFsSeek(fp, size, mode);
}


static int vfs_espfs_close(void* ctx, int fd)
{
    esp_espfs_t* efs = (esp_espfs_t *)ctx;

    if (fd < 0 || fd >= efs->max_files) {
        return -1;
    }

    EspFsFile *fp = efs->files[fd];
    if (fp == NULL) {
        return -1;
    }

    espFsClose(fp);
    efs->files[fd] = NULL;
    return 0;
}


esp_err_t esp_vfs_espfs_register(const esp_vfs_espfs_conf_t* conf)
{
    assert(conf->base_path);
    assert(conf->fs);
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

    esp_espfs_t *efs = calloc(1, sizeof(esp_espfs_t));
    if (efs == NULL) {
        ESP_LOGE(TAG, "esp_espfs could not be molloc'd");
        return ESP_ERR_NO_MEM;
    }

    efs->fs = conf->fs;
    efs->max_files = conf->max_files;
    efs->files = calloc(conf->max_files, sizeof(EspFsFile*));
    if (efs->files == NULL) {
        ESP_LOGE(TAG, "allocating files failed");
        free(efs);
        return ESP_ERR_NO_MEM;
    }

    strlcat(efs->base_path, conf->base_path, ESP_VFS_PATH_MAX + 1);
    esp_err_t err = esp_vfs_register(conf->base_path, &vfs, efs);
    if (err != ESP_OK) {
        free(efs);
        return err;
    }

    _efs[index] = efs;
    return ESP_OK;
}