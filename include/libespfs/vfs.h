/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "espfs.h"

#include <esp_err.h>


/**
 * \brief Configuration structure for the \a esp_vfs_espfs_register function
 */
typedef struct {
    const char *base_path; /** vfs path to mount the filesystem */
    espfs_fs_t *fs; /** the espfs instance */
    size_t max_files; /** maximum open files */
} esp_vfs_espfs_conf_t;

/**
 * \brief Mount an espfs fs handle under a vfs path
 *
 * \return ESP_OK if successful, ESP_ERR_NO_MEM if too many VFSes are
 *         registered
 */
esp_err_t esp_vfs_espfs_register(
    const esp_vfs_espfs_conf_t *conf /** [in] vfs configuration */
);


#ifdef __cplusplus
} /* extern "C" */
#endif
