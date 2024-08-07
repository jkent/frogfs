/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#include <stdlib.h>
#include <sys/types.h>

#include "frogfs_config.h"
#define FROGFS_PRIVATE_STRUCTS
#include "frogfs_format.h"


typedef struct frogfs_fs_t frogfs_fs_t;
typedef struct frogfs_decomp_funcs_t frogfs_decomp_funcs_t;

/**
 * \brief       Structure describing a frogfs file entry
 */
typedef struct frogfs_fh_t {
    const frogfs_fs_t *fs; /**< frogfs fs pointer */
    const frogfs_file_t *file; /**< file header pointer */
    const void *data_start; /**< data start pointer */
    const void *data_ptr; /**< current data pointer */
    size_t data_sz; /**< data size */
    size_t real_sz; /**< real (expanded) size */
    unsigned int flags; /** open flags */
    const frogfs_decomp_funcs_t *decomp_funcs; /**< decompresor funcs */
    void *decomp_priv; /**< decompressor private data */
} frogfs_fh_t;

/**
 * \brief       Structure describing a frogfs directory entry
 */
typedef struct frogfs_dh_t {
    const frogfs_fs_t *fs; /**< frogfs fs pointer */
    const frogfs_dir_t *dir; /**< frogfs entry */
    long index; /**< current index */
} frogfs_dh_t;

/**
 * \brief       Structure of function pointers that describe a decompressor
 */
typedef struct frogfs_decomp_funcs_t {
    int (*open)(frogfs_fh_t *f, unsigned int flags);
    void (*close)(frogfs_fh_t *f);
    ssize_t (*read)(frogfs_fh_t *f, void *buf, size_t len);
    ssize_t (*seek)(frogfs_fh_t *f, long offset, int mode);
    size_t (*tell)(frogfs_fh_t *f);
} frogfs_decomp_funcs_t;

/**
 * \brief       Raw decompressor functions
 */
extern const frogfs_decomp_funcs_t frogfs_decomp_raw;

/**
 * \brief       Heatshrink decompressor functions
 */
extern const frogfs_decomp_funcs_t frogfs_decomp_heatshrink;

/**
 * \brief       Miniz decompressor functions
 */
extern const frogfs_decomp_funcs_t frogfs_decomp_miniz;

/**
 * \brief       Zlib decompressor functions
 */
extern const frogfs_decomp_funcs_t frogfs_decomp_zlib;

#include "frogfs/frogfs.h"
