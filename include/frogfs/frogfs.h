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

/**
 * \brief Maximum recursion depth for flat directory traversal. Uses
 *        sizeof(uint16_t) * n per open directory. Set to 1 to disable flat
 *        directory traversal.
 */
#define FROGFS_MAX_FLAT_DEPTH 8

/**
 * \brief Configuration for the \a frogfs_init function
 */
typedef struct frogfs_config_t {
    const void *addr; /**< address of an frogfs filesystem in memory */
#if defined(ESP_PLATFORM)
    const char *part_label; /**< name of a partition to use as an frogfs
            filesystem. \a addr should be \a NULL if used */
#endif
} frogfs_config_t;

/**
 * \brief Structure describing a frogfs filesystem
 */
typedef struct frogfs_fs_t {
#if defined(ESP_PLATFORM)
    spi_flash_mmap_handle_t mmap_handle;
#endif
    const frogfs_head_t *head; /**< fs header pointer */
    const frogfs_hash_t *hash; /**< hash table pointer */
    const frogfs_dir_t *root; /**< root directory entry */
    int num_entries; /**< total number of file system entries */
} frogfs_fs_t;

typedef enum frogfs_entry_type_t {
    FROGFS_ENTRY_TYPE_DIR,
    FROGFS_ENTRY_TYPE_FILE,
} frogfs_entry_type_t;

/**
 * \brief Structure filled by the \a frogfs_stat function
 */
typedef struct frogfs_stat_t {
    frogfs_entry_type_t type; /**< entry type */
    frogfs_comp_algo_t compression; /**< compression type */
    size_t data_sz; /**< compressed file size */
    size_t real_sz; /**< uncompressed file size */
} frogfs_stat_t;

typedef struct frogfs_decomp_funcs_t frogfs_decomp_funcs_t;

/**
 * \brief Structure describing a frogfs file entry
 */
typedef struct frogfs_f_t {
    const frogfs_fs_t *fs; /**< frogfs fs pointer */
    const frogfs_file_t *file; /**< file header pointer */
    const void *data_start; /**< data start pointer */
    const void *data_ptr; /**< current data pointer */
    size_t data_sz; /**< data size */
    size_t real_sz; /**< real (expanded) size */
    unsigned int flags; /** open flags */
    const frogfs_decomp_funcs_t *decomp_funcs; /**< decompresor funcs */
    void *decomp_priv; /**< decompressor private data */
} frogfs_f_t;

/**
 * \brief Structure describing a frogfs directory entry
 */
typedef struct frogfs_d_t {
    const frogfs_fs_t *fs; /**< frogfs fs pointer */
    const frogfs_dir_t *dir; /**< frogfs entry */
    uint16_t index; /**< current index */
    uint16_t pos[FROGFS_MAX_FLAT_DEPTH]; /**< current pos at depth */
    uint8_t depth; /**< current depth */
} frogfs_d_t;

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
 * \brief      Get frogfs entry for path
 * \param[in]  fs   \a frogfs_fs_t pointer
 * \param[in]  path path string
 * \return     \a frogfs_obj_t pointer or \a NULL if the path is not found
 */
const frogfs_entry_t *frogfs_get_entry(const frogfs_fs_t *fs,
        const char *path);

/**
 * \brief      Get path for frogfs entry
 * \param[in]  fs  \a frogfs_fs_t pointer
 * \param[in]  obj \a frogfs_obj_t pointer
 * \return     path string or \a NULL if entry is NULL
 */
char *frogfs_get_path(const frogfs_fs_t *fs, const frogfs_entry_t *entry);

/**
 * \brief      Get information about a frogfs entry
 * \param[in]  fs  \a frogfs_fs_t pointer
 * \param[in]  obj \a frogfs_obj_t pointer
 * \param[out] st  \a frogfs_stat_t structure
 */
void frogfs_stat(const frogfs_fs_t *fs, const frogfs_entry_t *entry,
        frogfs_stat_t *st);

/**
 * \brief      Open a frogfs entry as a file from a \a frogfs_fs_t instance
 * \param[in]  fs    \a frogfs_fs_t poitner
 * \param[in]  obj   \a frogfs_obj_t pointer
 * \param[in]  flags \a open flags
 * \return     \a frogfs_f_t or \a NULL if not found
 */
frogfs_f_t *frogfs_open(const frogfs_fs_t *fs, const frogfs_entry_t *entry,
        unsigned int flags);

/**
 * \brief Close an open file entry
 * \param[in]  f \a frogfs_f_t pointer
 */
void frogfs_close(frogfs_f_t *f);

/**
 * \brief      Read data from an open file entry
 * \param[in]  f   \a frogfs_f_t pointer
 * \param[out] buf write buffer
 * \param[in]  len maximum number of bytes to read
 * \return         actual number of bytes read, zero if end of file reached
 */
ssize_t frogfs_read(frogfs_f_t *f, void *buf, size_t len);

/**
 * \brief      Seek to a position within an open file entry
 * \param[in]  f      \a frogfs_f_t pointer
 * \param[in]  offset file position (relative or absolute)
 * \param[in]  mode   \a SEEK_SET, \a SEEK_CUR, or \a SEEK_END
 * \return     current position in file or < 0 upon error
 */
ssize_t frogfs_seek(frogfs_f_t *f, long offset, int mode);

/**
 * \brief      Get the current position in an open file entry
 * \param[in]  f \a frogfs_f_t pointer
 * \return     current position in file or < 0 upon error
 */
size_t frogfs_tell(frogfs_f_t *f);

/**
 * \brief      Get raw memory for raw file entry
 * \param[in]  f   \a frogfs_f_t pointer
 * \param[out] buf pointer pointer to buf
 * \return     length of raw data
 */
size_t frogfs_access(frogfs_f_t *f, const void **buf);

/**
 * \brief      Open a directory for reading child entrys
 * \param[in]  fs  \a frogfs_fs_t pointer
 * \param[in]  obj \a frogfs_obj_t pointer to root director
 * \return     \a frogfs_d_t pointer or \a NULL if invalid
 */
frogfs_d_t *frogfs_opendir(frogfs_fs_t *fs, const frogfs_entry_t *entry);

/**
 * \brief      Close a directory
 * \param[in]  d \a frogfs_d_t pointer
 */
void frogfs_closedir(frogfs_d_t *d);

/**
 * \brief      Get the next child entry in directory
 * \param[in]  d \a frogfs_d_t pointer
 * \return     \a frogfs_obj_t pointer or \a NULL if end has been reached
 */
const frogfs_entry_t *frogfs_readdir(frogfs_d_t *d);

/**
 * \brief      Rewind to the first entry in the directory
 * \param[in]  d \a frogfs_d_t pointer
 */
void frogfs_rewinddir(frogfs_d_t *d);

/**
 * \brief      Set dir entry index to a value returned by \a frogfs_telldir
 *             for the current \a frogfs_d_t pointer; any other values are
 *             undefined
 * \param[in]  d   \a frogfs_d_t pointer
 * \param[in]  loc entry index
 */
void frogfs_seekdir(frogfs_d_t *d, uint16_t loc);

/**
 * \brief      Return the current entry index for a directory
 * \param[in]  d   \a frogfs_d_t pointer
 * \return     entry index
 */
uint16_t frogfs_telldir(frogfs_d_t *d);

#ifdef __cplusplus
} /* extern "C" */
#endif
