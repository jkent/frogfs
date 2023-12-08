/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "frogfs.h"

#include "esp_err.h"


#define F_REOPEN_RAW 1000

/**
 * \brief       Configuration structure for the \a frogfs_vfs_register function
 */
typedef struct frogfs_vfs_conf_t {
    const char *base_path; /**< vfs path to mount the filesystem */
    frogfs_fs_t *fs; /**< the frogfs instance */
    size_t max_files; /**< maximum open files */
} frogfs_vfs_conf_t;

/**
 * \brief      Mount an frogfs fs handle under a vfs path
 * \param[in]  conf vfs configuration
 * \return     ESP_OK if successful, ESP_ERR_NO_MEM if too many VFSes are
 *             registered
 */
esp_err_t frogfs_vfs_register(const frogfs_vfs_conf_t *conf);

#ifdef __cplusplus
} /* extern "C" */
#endif
