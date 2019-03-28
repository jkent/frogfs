#include <esp_err.h>
#include <esp_log.h>
#include <esp_vfs.h>
#include <espfs.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/fcntl.h>

#include "espfs_vfs.h"
#include "espfs_image.h"


static const char *TAG = "ESPFS";

typedef struct {
    bool initialized;
    size_t max_files;
    EspFsFile **files;
    char base_path[ESP_VFS_PATH_MAX+1];
} esp_espfs_t;
static esp_espfs_t efs = { 0 };


static esp_err_t esp_espfs_init(const esp_vfs_espfs_conf_t *conf)
{
    if (efs.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    EspFsInitResult result = espFsInit(image_espfs_start);
    if (result != ESPFS_INIT_RESULT_OK) {
        return ESP_FAIL;
    }

    efs.max_files = conf->max_files;
    efs.files = calloc(conf->max_files, sizeof(EspFsFile *));
    if (efs.files == NULL) {
        ESP_LOGE(TAG, "calloc failed");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}


static int vfs_espfs_open(const char *path, int flags, int mode)
{
    if (flags & (O_CREAT | O_WRONLY | O_RDWR)) {
        return -1;
    }

    int fd;
    for (fd = 0; fd < efs.max_files; fd++) {
        if (efs.files[fd] == NULL) {
            break;
        }
    }

    if (fd >= efs.max_files) {
        return -1;
    }

    efs.files[fd] = espFsOpen(path);
    if (efs.files[fd] == NULL) {
        return -1;
    }

    return fd;
}


static int vfs_espfs_fstat(int fd, struct stat * st)
{
    if (fd < 0 || fd >= efs.max_files) {
        return -1;
    }

    st->st_mode = S_IFCHR;
    return 0;
}


static ssize_t vfs_espfs_read(int fd, void *data, size_t size)
{
    if (fd < 0 || fd >= efs.max_files) {
        return -1;
    }

    EspFsFile *fp = efs.files[fd];
    if (fp == NULL) {
        return -1;
    }
    return espFsRead(fp, data, size);
}


static ssize_t vfs_espfs_write(int fd, const void *data, size_t size)
{
    return -1;
}


static off_t vfs_espfs_lseek(int fd, off_t size, int mode)
{
    if (fd < 0 || fd >= efs.max_files) {
        return -1;
    }

    EspFsFile *fp = efs.files[fd];
    if (fp == NULL) {
        return -1;
    } 

    return espFsSeek(fp, size, mode);
}


static int vfs_espfs_close(int fd)
{
    if (fd < 0 || fd >= efs.max_files) {
        return -1;
    }

    EspFsFile *fp = efs.files[fd];
    if (fp == NULL) {
        return -1;
    }

    espFsClose(fp);
    efs.files[fd] = NULL;
    return 0;
}


esp_err_t esp_vfs_espfs_register(const esp_vfs_espfs_conf_t * conf)
{
    assert(conf->base_path);
    const esp_vfs_t vfs = {
        .flags = ESP_VFS_FLAG_DEFAULT,
        .write = &vfs_espfs_write,
        .lseek = &vfs_espfs_lseek,
        .read = &vfs_espfs_read,
        .open = &vfs_espfs_open,
        .close = &vfs_espfs_close,
        .fstat = &vfs_espfs_fstat,
    };

    esp_err_t err = esp_espfs_init(conf);
    if (err != ESP_OK) {
        return err;
    }

    strlcat(efs.base_path, conf->base_path, ESP_VFS_PATH_MAX + 1);
    err = esp_vfs_register(conf->base_path, &vfs, NULL);
    if (err != ESP_OK) {
        return err;
    }

    return ESP_OK;
}
