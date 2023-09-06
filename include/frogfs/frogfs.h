/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "frogfs/format.h"

#if defined(ESP_PLATFORM)
#include "spi_flash_mmap.h"
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>


/**
 * \brief Flag for \a frogfs_open to open any file as raw. Useful to pass
 *        compressed data over a transport such as HTTP.
 */
#define FROGFS_OPEN_RAW (1 << 0)

typedef struct frogfs_decomp_funcs_t frogfs_decomp_funcs_t;
typedef struct frogfs_decomp_t frogfs_decomp_t;

/**
 * \brief Configuration for the \a frogfs_init function
 */
typedef struct frogfs_config_t {
    const void *addr; /**< address of an frogfs filesystem in memory */
    const frogfs_decomp_t *decomp_funcs; /**< decompressor list */
#if defined(ESP_PLATFORM)
    const char *part_label; /**< name of a partition to use as an frogfs
            filesystem. \a addr should be \a NULL if used */
#endif
} frogfs_config_t;

/**
 * \brief Mapping structure of decompressor functions to compressor id
 */
typedef struct frogfs_decomp_t {
    frogfs_decomp_funcs_t *funcs; /**< decompressor struct */
    uint8_t id; /**< compressor id */
} frogfs_decomp_t;

/**
 * \brief Structure describing a frogfs filesystem
 */
typedef struct frogfs_fs_t {
#if defined(ESP_PLATFORM)
    spi_flash_mmap_handle_t mmap_handle;
#endif
    const frogfs_head_t *head; /**< fs header pointer */
    const frogfs_hash_t *hashes; /**< hash table pointer */
    const frogfs_decomp_t *decomp_funcs; /**< decompressor list */
} frogfs_fs_t;

/**
 * \brief Structure filled by the \a frogfs_stat function
 */
typedef struct frogfs_stat_t {
    frogfs_type_t type; /**< object type */
    frogfs_comp_t compression; /**< compression type */
    size_t size; /**< uncompressed file size */
    size_t size_compressed; /**< compressed file size */
} frogfs_stat_t;

/**
 * \brief Structure describing a frogfs file object
 */
typedef struct frogfs_f_t {
    const frogfs_fs_t *fs; /**< frogfs fs pointer */
    const frogfs_file_t *file; /**< file header pointer */
    uint8_t *data_start; /**< data start pointer */
    uint8_t *data_ptr; /**< current data pointer */
    unsigned int flags; /** open flags */
    const frogfs_decomp_funcs_t *decomp_funcs; /**< decompresor funcs */
    void *decomp_priv; /**< decompressor private data */
} frogfs_f_t;

#if defined(CONFIG_FROGFS_SUPPORT_DIR) || defined(__DOXYGEN__)
/**
 * \brief Structure describing a frogfs directory object
 */
typedef struct frogfs_d_t {
    const frogfs_fs_t *fs; /**< frogfs fs pointer */
    const frogfs_dir_t *dir; /**< frogfs object */
    const frogfs_sort_t *children; /**< sort table */
    uint16_t index; /**< current index */
} frogfs_d_t;
#endif

/**
 * \brief Structure of function pointers that describe a decompressor
 */
typedef struct frogfs_decomp_funcs_t {
    int (*open)(frogfs_f_t *f, unsigned int flags);
    void (*close)(frogfs_f_t *f);
    ssize_t (*read)(frogfs_f_t *f, void *buf, size_t len);
    ssize_t (*seek)(frogfs_f_t *f, long offset, int mode);
    size_t (*tell)(frogfs_f_t *f);
} frogfs_decomp_funcs_t;

/**
 * \brief Raw decompressor functions
 */
extern const frogfs_decomp_funcs_t frogfs_decomp_raw;

/**
 * \brief Deflate decompressor functions
 */
extern const frogfs_decomp_funcs_t frogfs_decomp_deflate;

/**
 * \brief Heatshrink decompressor functions
 */
extern const frogfs_decomp_funcs_t frogfs_decomp_heatshrink;

/**
 * \brief      Initialize and return a \a frogfs_fs_t instance
 * \param[in]  config frogfs configuration
 * \return     \a frogfs_fs_t pointer or \a NULL on error
 */
frogfs_fs_t *frogfs_init(frogfs_config_t *conf);

/**
 * \brief      Tear down a \a frogfs_fs_t instance
 * \param[in]  fs \a frogfs_fs_t pointer
 */
void frogfs_deinit(frogfs_fs_t *fs);

/**
 * \brief      Get frogfs object for path
 * \param[in]  fs   \a frogfs_fs_t pointer
 * \param[in]  path path string
 * \return     \a frogfs_obj_t pointer or \a NULL if the path is not found
 */
const frogfs_obj_t *frogfs_obj_from_path(const frogfs_fs_t *fs,
        const char *path);

/**
 * \brief      Get path for frogfs object
 * \param[in]  obj \a frogfs_obj_t pointer
 * \return     path string or \a NULL if object is NULL
 */
const char *frogfs_path_from_obj(const frogfs_obj_t *obj);

/**
 * \brief      Get information about a frogfs object
 * \param[in]  fs  \a frogfs_fs_t pointer
 * \param[in]  obj \a frogfs_obj_t pointer
 * \param[out] st  \a frogfs_stat_t structure
 */
void frogfs_stat(const frogfs_fs_t *fs, const frogfs_obj_t *obj,
        frogfs_stat_t *st);

/**
 * \brief      Open a frogfs object as a file from a \a frogfs_fs_t instance
 * \param[in]  fs    \a frogfs_fs_t poitner
 * \param[in]  obj   \a frogfs_obj_t pointer
 * \param[in]  flags \a open flags
 * \return     \a frogfs_f_t or \a NULL if not found
 */
frogfs_f_t *frogfs_open(const frogfs_fs_t *fs, const frogfs_obj_t *obj,
        unsigned int flags);

/**
 * \brief Close an open file object
 * \param[in]  f \a frogfs_f_t pointer
 */
void frogfs_close(frogfs_f_t *f);

/**
 * \brief      Read data from an open file object
 * \param[in]  f   \a frogfs_f_t pointer
 * \param[out] buf write buffer
 * \param[in]  len maximum number of bytes to read
 * \return         actual number of bytes read, zero if end of file reached
 */
ssize_t frogfs_read(frogfs_f_t *f, void *buf, size_t len);

/**
 * \brief      Seek to a position within an open file object
 * \param[in]  f      \a frogfs_f_t pointer
 * \param[in]  offset file position (relative or absolute)
 * \param[in]  mode   \a SEEK_SET, \a SEEK_CUR, or \a SEEK_END
 * \return     current position in file or < 0 upon error
 */
ssize_t frogfs_seek(frogfs_f_t *f, long offset, int mode);

/**
 * \brief      Get the current position in an open file object
 * \param[in]  f \a frogfs_f_t pointer
 * \return     current position in file or < 0 upon error
 */
size_t frogfs_tell(frogfs_f_t *f);

/**
 * \brief      Get raw memory for raw file object
 * \param[in]  f   \a frogfs_f_t pointer
 * \param[out] buf pointer pointer to buf
 * \return     length of raw data
 */
size_t frogfs_access(frogfs_f_t *f, const void **buf);

#if defined(CONFIG_FROGFS_SUPPORT_DIR) || defined(__DOXYGEN__)
/**
 * \brief      Open a directory for reading child objects
 * \param[in]  fs  \a frogfs_fs_t pointer
 * \param[in]  obj \a frogfs_obj_t pointer to root director
 * \return     \a frogfs_d_t pointer or \a NULL if invalid
 */
frogfs_d_t *frogfs_opendir(frogfs_fs_t *fs, const frogfs_obj_t *obj);

/**
 * \brief      Close a directory
 * \param[in]  d \a frogfs_d_t pointer
 */
void frogfs_closedir(frogfs_d_t *d);

/**
 * \brief      Get the next child object in directory
 * \param[in]  d \a frogfs_d_t pointer
 * \return     \a frogfs_obj_t pointer or \a NULL if end has been reached
 */
const frogfs_obj_t *frogfs_readdir(frogfs_d_t *d);

/**
 * \brief      Rewind to the first object in the directory
 * \param[in]  d \a frogfs_d_t pointer
 */
void frogfs_rewinddir(frogfs_d_t *d);

/**
 * \brief      Set dir object index to a value returned by \a frogfs_telldir
 *             for the current \a frogfs_d_t pointer; any other values are
 *             undefined
 * \param[in]  d   \a frogfs_d_t pointer
 * \param[in]  loc object index
 */
void frogfs_seekdir(frogfs_d_t *d, uint16_t loc);

/**
 * \brief      Return the current object index for a directory
 * \param[in]  d   \a frogfs_d_t pointer
 * \return     object index
 */
uint16_t frogfs_telldir(frogfs_d_t *d);
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif
