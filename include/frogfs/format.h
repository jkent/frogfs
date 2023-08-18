/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#include <stdint.h>


/**
 * \brief Magic number used in the frogfs file header
 */
#define FROGFS_MAGIC 0x474F5246 /** FROG */

/**
 * \brief Major version this source distrobution supports
 */
#define FROGFS_VER_MAJOR 1

/**
 * \brief Minor version this source distrobution supports
 */
#define FROGFS_VER_MINOR 0

/**
 * \brief Object type ids
 */
typedef enum frogfs_type_t {
    FROGFS_OBJ_TYPE_FILE,
    FROGFS_OBJ_TYPE_DIR
} frogfs_type_t;

/**
 * \brief Known compression ids
 */
typedef enum frogfs_comp_t {
    FROGFS_COMP_NONE,
    FROGFS_COMP_DEFLATE,
    FROGFS_COMP_HEATSHRINK,
} frogfs_comp_t;

/**
 * \brief Filesystem header
 */
typedef struct __attribute__((packed)) frogfs_head_t {
    uint32_t magic; /**< filesystem magic */
    uint8_t len; /**< header length */
    uint8_t ver_major; /**< major version */
    uint16_t ver_minor; /**< minor version */
    uint32_t bin_len; /**< binary length */
    uint16_t num_objs; /**< object count */
    uint8_t align; /**< object alignment */
} frogfs_head_t;

/**
 * \brief Hash table entry
 */
typedef struct __attribute__((packed)) frogfs_hash_t {
    uint32_t hash; /**< path hash */
    uint32_t offset; /**< object offset */
} frogfs_hash_t;

/**
 * \brief Sort table entry
 */
typedef struct __attribute__((packed)) frogfs_sort_t {
    uint32_t offset; /**< object offset */
} frogfs_sort_t;

/**
 * \brief Object header
 */
typedef struct __attribute__((packed)) frogfs_obj_t {
    uint8_t len; /**< header length */
    uint8_t type; /**< object type */
    uint16_t index; /**< sorted index */
    uint16_t path_len; /**< path length (including null) */
} frogfs_obj_t;

/**
 * \brief File object header
 */
typedef struct __attribute__((packed)) frogfs_file_t {
    uint8_t len; /**< header length */
    uint8_t type; /**< object type */
    uint16_t index; /**< sorted index */
    uint16_t path_len; /**< path length (including null) */
    uint8_t compression; /**< compression type */
    uint8_t reserved; /**< reserved field */
    uint32_t data_len; /**< data length */
} frogfs_file_t;

/**
 * \brief Compressed file object header
 */
typedef struct __attribute__((packed)) frogfs_file_comp_t {
    uint8_t len; /**< header length */
    uint8_t type; /**< object type */
    uint16_t index; /**< sorted index */
    uint16_t path_len; /**< path length (including null) */
    uint8_t compression; /**< compression type */
    uint8_t options; /**< compression options */
    uint32_t data_len; /**< data length (raw) */
    uint32_t uncompressed_len; /**< data length (uncompressed) */
} frogfs_file_comp_t;

/**
 * \brief Filesystem footer
 */
typedef struct __attribute__((packed)) frogfs_foot_t {
    uint32_t crc32; /**< crc32 of entire file without this field */
} frogfs_foot_t;
