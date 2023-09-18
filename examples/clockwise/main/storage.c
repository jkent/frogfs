#include "esp_err.h"
#include "esp_spiffs.h"
#include "nvs_flash.h"

#include "frogfs/frogfs.h"
#include "frogfs/vfs.h"


extern const uint8_t frogfs_bin[];
frogfs_fs_t *frogfs;

esp_err_t storage_init(void)
{
    esp_err_t err;

    // Mount spiffs
    esp_vfs_spiffs_conf_t spiffs_config = {
        .base_path = "/spiffs",
        .format_if_mount_failed = true,
        .max_files = 5,
        .partition_label = "storage",
    };
    err = esp_vfs_spiffs_register(&spiffs_config);
    if (err != ESP_OK) {
        return err;
    }

    // Mount frogfs
    frogfs_config_t frogfs_config = {
        .addr = frogfs_bin,
    };
    frogfs = frogfs_init(&frogfs_config);
    assert(frogfs != NULL);

    frogfs_vfs_conf_t frogfs_vfs_conf = {
        .base_path = "/frogfs",
        .flat = true,
        .fs = frogfs,
        .max_files = 5,
        .overlay = "/spiffs",
    };
    err = frogfs_vfs_register(&frogfs_vfs_conf);
    if (err != ESP_OK) {
        return err;
    }

    // Initialize NVS
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
            err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    return err;
}
