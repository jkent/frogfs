#pragma once

#include <esp_err.h>
#include <esp_vfs.h>
#include <stdint.h>


typedef struct {
    const char* base_path;
    size_t max_files;
} esp_vfs_espfs_conf_t;

esp_err_t esp_vfs_espfs_register(const esp_vfs_espfs_conf_t * conf);
