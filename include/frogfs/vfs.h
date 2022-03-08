/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "frogfs.h"

#include <esp_err.h>


typedef struct esp_vfs_frogfs_conf_t esp_vfs_frogfs_conf_t;

/**
 * \brief Configuration structure for the \a esp_vfs_frogfs_register function
 */
struct esp_vfs_frogfs_conf_t {
    const char *base_path; /**< vfs path to mount the filesystem */
    const char *overlay_path; /**< vfs overlay search path */
    frogfs_fs_t *fs; /**< the frogfs instance */
    size_t max_files; /**< maximum open files */
};

/**
 * \brief Mount an frogfs fs handle under a vfs path
 *
 * \return ESP_OK if successful, ESP_ERR_NO_MEM if too many VFSes are
 *         registered
 */
esp_err_t esp_vfs_frogfs_register(
    const esp_vfs_frogfs_conf_t *conf /** [in] vfs configuration */
);


#ifdef __cplusplus
} /* extern "C" */
#endif
