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


/**
 * \brief Compression encodings for espfs file header
 */
typedef enum {
    ESPFS_COMPRESS_NONE,
    ESPFS_COMPRESS_HEATSHRINK
} espfs_compression_type_t;

/**
 * \brief Bitfield flags for espfs file header
 */
typedef enum {
    ESPFS_FLAG_LASTFILE = (1 << 0),
    ESPFS_FLAG_GZIP = (1 << 1),
} espfs_flags_t;

/**
 * \brief File type for \a espfs_stat function
 */
typedef enum {
    ESPFS_TYPE_MISSING,
    ESPFS_TYPE_FILE,
    ESPFS_TYPE_DIR,
} espfs_stat_type_t;

/**
 * \brief Opaque type for espfs filesystem
 */
typedef struct espfs_fs_t espfs_fs_t;

/**
 * \brief Opaque type for espfs files
 */
typedef struct espfs_file_t espfs_file_t;

/**
 * \brief Configuration for the \a espfs_init function
 */
typedef struct {
    const void *addr; /**< address of an espfs filesystem in memory */
#if defined(CONFIG_ENABLE_FLASH_MMAP)
    const char *partition; /**< name of a partition to use as an espfs
            filesystem. \a addr should be \a NULL if used */
#endif
} espfs_config_t;

/**
 * \brief Structure filled by the \a espfs_stat function
 */
typedef struct {
    espfs_flags_t flags; /**< file flags */
    espfs_stat_type_t type; /**< file type */
    size_t size; /**< file size */
} espfs_stat_t;

/**
 * \brief Initialize and return an \a espfs_fs_t instance
 *
 * \return espfs fs handle
 */
espfs_fs_t *espfs_init(
    espfs_config_t *conf /**< [in] configuration */
);

/**
 * \brief Tear down an \a espfs_fs_t instance
 */
void espfs_deinit(
    espfs_fs_t *fs /**< [in] espfs fs handle */
);

/**
 * \brief Open a file from an espfs instance
 *
 * \return espfs_file handle or \a NULL if not found
 */
espfs_file_t *espfs_open(
    espfs_fs_t *fs, /**< [in] espfs fs handle */
    const char *filename /**< [in] espfs path */
);

/**
 * \brief Open a file from an espfs instance
 *
 * \return \a true if sucessful
 */
bool espfs_stat(
    espfs_fs_t *fs, /**< [in] espfs fs handle */
    const char *filename, /**< [in] espfs path */
    espfs_stat_t *s /**< [out] stat structure */
);

/**
 * \brief Return flags for an open file handle
 *
 * \return flags
 */
espfs_flags_t espfs_flags(
    espfs_file_t *fh /**< [in] espfs file handle */
);

/**
 * \brief Read data from an open file handle
 *
 * \return actual number of bytes read, zero if end of file reached
 */
ssize_t espfs_read(
    espfs_file_t *fh, /**< [in] espfs file handle */
    char *buf, /**< [out] bytes read */
    size_t len /**< [len] maximum bytes to read */
);

/**
 * \brief Seek to a position within an open file handle
 *
 * If the file is compressed, seeking within the file is not implemented. Only
 * seeking to the start or end is supported in this case.  This may be
 * implemented in the future.
 *
 * \return position in file or < 0 upon error
 */
ssize_t espfs_seek(
    espfs_file_t *fh, /**< [in] espfs file handle */
    long offset, /**< [in] position */
    int mode /**< [in] mode */
);

/**
 * \brief Test if open file handle is compressed
 *
 * \return \a true if compressed
 */
bool espfs_is_compressed(
    espfs_file_t *fh /**< [in] espfs file handle */
);

/**
 * \brief Get raw memory access for an uncompressed open file handle
 *
 * \return length of file or < 0 upon error
 */
ssize_t espfs_access(
    espfs_file_t *fh, /* [in] espfs file handle */
    void **buf /**< [out] doube pointer to buf */
);

/**
 * \brief Get the file size for an open file handle
 *
 * \return       length of file
 */
size_t espfs_filesize(
    espfs_file_t *fh /* [in] espfs file handle */
);

/**
 * \brief Close an open file handle
 */
void espfs_close(
    espfs_file_t *fh /* [in] espfs file handle */
);


#ifdef __cplusplus
} /* extern "C" */
#endif
