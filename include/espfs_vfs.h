#pragma once

#include <esp_err.h>
#include <esp_vfs.h>
#include <stdint.h>

#include <espfs.h>


#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char* base_path;
    EspFs *fs;
    size_t max_files;
} esp_vfs_espfs_conf_t;

esp_err_t esp_vfs_espfs_register(const esp_vfs_espfs_conf_t* conf);

#ifdef __cplusplus
}
#endif
