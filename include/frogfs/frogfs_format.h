/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#include <stdint.h>


/**
 * \brief Magic number used in the frogfs file header
 */
#define FROGFS_MAGIC 0x676F7246 /** Frog */
#define FROGFS_VERSION_MAJOR 0
#define FROGFS_VERSION_MINOR 0

typedef struct frogfs_fs_header_t frogfs_fs_header_t;
typedef struct frogfs_hashtable_entry_t frogfs_hashtable_entry_t;
typedef struct frogfs_sorttable_entry_t frogfs_sorttable_entry_t;
typedef struct frogfs_object_header_t frogfs_object_header_t;
typedef struct frogfs_file_header_t frogfs_file_header_t;
typedef struct frogfs_heatshrink_header_t frogfs_heatshrink_header_t;
typedef struct frogfs_crc32_footer_t frogfs_crc32_footer_t;

struct frogfs_fs_header_t {
    uint32_t magic;
    uint8_t len;
    uint8_t version_major;
    uint16_t version_minor;
    uint32_t binary_len;
    uint16_t num_objects;
    uint16_t reserved;
} __attribute__((packed));

struct frogfs_hashtable_entry_t {
    uint32_t hash;
    uint32_t offset;
} __attribute__((packed));

struct frogfs_sorttable_entry_t {
    uint32_t offset;
} __attribute__((packed));

struct frogfs_object_header_t {
    uint8_t type;
    uint8_t len;
    uint16_t index;
    uint16_t path_len;
    uint16_t reserved;
} __attribute__((packed));

struct frogfs_file_header_t {
    frogfs_object_header_t object;
    uint32_t data_len;
    uint32_t file_len;
    uint16_t flags;
    uint8_t compression;
    uint8_t reserved;
} __attribute__((packed));

struct frogfs_heatshrink_header_t {
    uint8_t window_sz2;
    uint8_t lookahead_sz2;
    uint16_t reserved;
} __attribute__((packed));

struct frogfs_crc32_footer_t {
    uint32_t crc32;
} __attribute__((packed));
