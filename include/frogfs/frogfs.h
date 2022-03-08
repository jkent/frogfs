/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef struct frogfs_fs_t frogfs_fs_t;
typedef struct frogfs_file_t frogfs_file_t;
typedef struct frogfs_config_t frogfs_config_t;
typedef struct frogfs_stat_t frogfs_stat_t;

/**
 * \brief Object type
 */
typedef enum frogfs_stat_type_t {
    FROGFS_TYPE_FILE,
    FROGFS_TYPE_DIR,
} frogfs_stat_type_t;

/**
 * \brief Object flags
 */
typedef enum frogfs_flags_t {
    FROGFS_FLAG_GZIP  = (1 << 0),
    FROGFS_FLAG_CACHE = (1 << 1),
} frogfs_flags_t;

/**
 * \brief Compression encodings
 */
typedef enum frogfs_compression_type_t {
    FROGFS_COMPRESSION_NONE,
    FROGFS_COMPRESSION_HEATSHRINK
} frogfs_compression_type_t;

/**
 * \brief Configuration for the \a frogfs_init function
 */
struct frogfs_config_t {
    const void *addr; /**< address of an frogfs filesystem in memory */
#if defined(ESP_PLATFORM)
    const char *part_label; /**< name of a partition to use as an frogfs
            filesystem. \a addr should be \a NULL if used */
#endif
};

/**
 * \brief Structure filled by \a frogfs_stat and \a frogfs_fstat functions
 */
struct frogfs_stat_t {
    uint16_t index; /**< file index */
    frogfs_stat_type_t type; /**< file type */
    frogfs_flags_t flags; /**< file flags */
    frogfs_compression_type_t compression; /**< compression type */
    size_t size; /**< file size */
};

/**
 * \brief Initialize and return an \a frogfs_fs_t instance
 *
 * \return frogfs fs pointer or \a NULL on error
 */
frogfs_fs_t *frogfs_init(
    frogfs_config_t *conf /** [in] configuration */
);

/**
 * \brief Tear down an \a frogfs_fs_t instance
 */
void frogfs_deinit(
    frogfs_fs_t *fs /** [in] frogfs fs pointer */
);

/**
 * \brief Get path for sorted frogfs object index
 *
 * \return path or NULL if the index is invalid
 */
const char *frogfs_get_path(
    frogfs_fs_t *fs, /** [in] frogfs fs pointer */
    uint16_t index /** [in] frogfs file index */
);

/**
 * \brief Get information about an frogfs object
 *
 * \return \a true if object found
 */
bool frogfs_stat(
    frogfs_fs_t *fs, /** [in] frogfs fs pointer */
    const char *path, /** [in] frogfs path */
    frogfs_stat_t *s /** [out] stat structure */
);

/**
 * \brief Open a file from an \a frogfs_fs_t instance
 *
 * \return frogfs_file_t or \a NULL if not found
 */
frogfs_file_t *frogfs_fopen(
    frogfs_fs_t *fs, /** [in] frogfs fs pointer */
    const char *path /** [in] frogfs path */
);

/**
 * \brief Close an open file object
 */
void frogfs_fclose(
    frogfs_file_t *f /* [in] frogfs file */
);

/**
 * \brief Get information about an open file object
 */
void frogfs_fstat(
    frogfs_file_t *f, /** [in] frogfs file */
    frogfs_stat_t *s /** [out] stat structure */
);

/**
 * \brief Read data from an open file object
 *
 * \return actual number of bytes read, zero if end of file reached
 */
ssize_t frogfs_fread(
    frogfs_file_t *f, /** [in] frogfs file */
    void *buf, /** [out] bytes read */
    size_t len /** [len] maximum bytes to read */
);

/**
 * \brief Seek to a position within an open file object
 *
 * \return position in file or < 0 upon error
 */
ssize_t frogfs_fseek(
    frogfs_file_t *f, /** [in] frogfs file */
    long offset, /** [in] position */
    int mode /** [in] mode */
);

/**
 * \brief Get the current position in an open file object
 *
 * \return position in file
 */
size_t frogfs_ftell(
    frogfs_file_t *f /** [in] frogfs file */
);

/**
 * \brief Get raw memory for an uncompressed open file object
 *
 * \return length of file or < 0 upon error
 */
ssize_t frogfs_faccess(
    frogfs_file_t *f, /* [in] frogfs file */
    void **buf /** [out] doube pointer to buf */
);


#ifdef __cplusplus
} /* extern "C" */
#endif
