/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#include "frogfs/frogfs_format.h"

#if defined(ESP_PLATFORM)
#include "spi_flash_mmap.h"
#endif

#include <stdint.h>


typedef struct frogfs_fs_t frogfs_fs_t;
typedef struct frogfs_file_t frogfs_file_t;

struct frogfs_fs_t {
#if defined(ESP_PLATFORM)
    spi_flash_mmap_handle_t mmap_handle;
#endif
    const frogfs_fs_header_t *header;
    const frogfs_hashtable_entry_t *hashtable;
    const frogfs_sorttable_entry_t *sorttable;
};

struct frogfs_file_t {
    const frogfs_fs_t *fs;
    const frogfs_file_header_t *fh;
    uint8_t *raw_start;
    uint8_t *raw_ptr;
    uint32_t raw_len;
    uint32_t file_pos;
    void *user;
};
