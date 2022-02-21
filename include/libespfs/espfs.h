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

typedef struct espfs_fs_t espfs_fs_t;
typedef struct espfs_file_t espfs_file_t;
typedef struct espfs_config_t espfs_config_t;
typedef struct espfs_stat_t espfs_stat_t;

/**
 * \brief Object type
 */
typedef enum espfs_stat_type_t {
    ESPFS_TYPE_FILE,
    ESPFS_TYPE_DIR,
} espfs_stat_type_t;

/**
 * \brief Object flags
 */
typedef enum espfs_flags_t {
    ESPFS_FLAG_GZIP  = (1 << 0),
    ESPFS_FLAG_CACHE = (1 << 1),
} espfs_flags_t;

/**
 * \brief Compression encodings
 */
typedef enum espfs_compression_type_t {
    ESPFS_COMPRESSION_NONE,
    ESPFS_COMPRESSION_HEATSHRINK
} espfs_compression_type_t;

/**
 * \brief Configuration for the \a espfs_init function
 */
struct espfs_config_t {
    const void *addr; /**< address of an espfs filesystem in memory */
#if defined(ESP_PLATFORM)
    const char *part_label; /**< name of a partition to use as an espfs
            filesystem. \a addr should be \a NULL if used */
#endif
};

/**
 * \brief Structure filled by \a espfs_stat and \a espfs_fstat functions
 */
struct espfs_stat_t {
    uint16_t index; /**< file index */
    espfs_stat_type_t type; /**< file type */
    espfs_flags_t flags; /**< file flags */
    espfs_compression_type_t compression; /**< compression type */
    size_t size; /**< file size */
};

/**
 * \brief Initialize and return an \a espfs_fs_t instance
 *
 * \return espfs fs pointer or \a NULL on error
 */
espfs_fs_t *espfs_init(
    espfs_config_t *conf /** [in] configuration */
);

/**
 * \brief Tear down an \a espfs_fs_t instance
 */
void espfs_deinit(
    espfs_fs_t *fs /** [in] espfs fs pointer */
);

/**
 * \brief Get path for sorted espfs object index
 *
 * \return path or NULL if the index is invalid
 */
const char *espfs_get_path(
    espfs_fs_t *fs, /** [in] espfs fs pointer */
    uint16_t index /** [in] espfs file index */
);

/**
 * \brief Get information about an espfs object
 *
 * \return \a true if object found
 */
bool espfs_stat(
    espfs_fs_t *fs, /** [in] espfs fs pointer */
    const char *path, /** [in] espfs path */
    espfs_stat_t *s /** [out] stat structure */
);

/**
 * \brief Open a file from an \a espfs_fs_t instance
 *
 * \return espfs_file_t or \a NULL if not found
 */
espfs_file_t *espfs_fopen(
    espfs_fs_t *fs, /** [in] espfs fs pointer */
    const char *path /** [in] espfs path */
);

/**
 * \brief Close an open file object
 */
void espfs_fclose(
    espfs_file_t *f /* [in] espfs file */
);

/**
 * \brief Get information about an open file object
 */
void espfs_fstat(
    espfs_file_t *f, /** [in] espfs file */
    espfs_stat_t *s /** [out] stat structure */
);

/**
 * \brief Read data from an open file object
 *
 * \return actual number of bytes read, zero if end of file reached
 */
ssize_t espfs_fread(
    espfs_file_t *f, /** [in] espfs file */
    void *buf, /** [out] bytes read */
    size_t len /** [len] maximum bytes to read */
);

/**
 * \brief Seek to a position within an open file object
 *
 * \return position in file or < 0 upon error
 */
ssize_t espfs_fseek(
    espfs_file_t *f, /** [in] espfs file */
    long offset, /** [in] position */
    int mode /** [in] mode */
);

/**
 * \brief Get the current position in an open file object
 *
 * \return position in file
 */
size_t espfs_ftell(
    espfs_file_t *f /** [in] espfs file */
);

/**
 * \brief Get raw memory for an uncompressed open file object
 *
 * \return length of file or < 0 upon error
 */
ssize_t espfs_faccess(
    espfs_file_t *f, /* [in] espfs file */
    void **buf /** [out] doube pointer to buf */
);


#ifdef __cplusplus
} /* extern "C" */
#endif
