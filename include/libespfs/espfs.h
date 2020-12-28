/** \file espfs.h
 *  Main header for libespfs.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <esp_err.h>

/** @brief Magic number used in the espfs file header
 */
#define ESPFS_MAGIC 0x73665345

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Compression encodings for espfs file header
 */
typedef enum {
    ESPFS_COMPRESS_NONE,
    ESPFS_COMPRESS_HEATSHRINK
} espfs_compression_type_t;

/** @brief Bitfield flags for espfs file header
 */
typedef enum {
    ESPFS_FLAG_LASTFILE = (1 << 0),
    ESPFS_FLAG_GZIP = (1 << 1),
} espfs_flags_t;

/** @brief File type for \a espfs_stat function
 */
typedef enum {
    ESPFS_TYPE_MISSING,
    ESPFS_TYPE_FILE,
    ESPFS_TYPE_DIR,
} espfs_stat_type_t;

/** @brief Opaque type for espfs filesystem
 */
typedef struct espfs_t espfs_t;

/** @brief Opaque type for espfs files
 */
typedef struct espfs_file_t espfs_file_t;

/** @brief Configuration for the \a espfs_init function
 */
typedef struct {
    const void* addr;      /**< address of an espfs filesystem in memory */
#if defined(CONFIG_ENABLE_FLASH_MMAP)
    const char* partition; /**< name of a partition to use as an espfs
                                filesystem. only available on ESP32 and newer
                                platforms. \a addr should be \a NULL if used */
#endif
} espfs_config_t;

/** @brief Structure filled by the \a espfs_stat function
 */
typedef struct {
    espfs_flags_t flags;    /**< file flags */
    espfs_stat_type_t type; /**< file type */
    size_t size;            /**< file size */
} espfs_stat_t;

/** @brief Configuration structure for the \a esp_vfs_espfs_register function
 */
typedef struct {
    const char* base_path; /**< vfs path to mount the filesystem */
    espfs_t *espfs;        /**< the espfs instance */
    size_t max_files;      /**< maximum open files */
} esp_vfs_espfs_conf_t;

/** @brief Initialize and return an \a espfs_t instance
 *  @param[in] conf configuration
 *  @return         espfs handle
 */
espfs_t* espfs_init(espfs_config_t* conf);

/** @brief Tear down an \a espfs_t instance
 *  @param[in] fs espfs handle
 */
void espfs_deinit(espfs_t* fs);

/** @brief Open a file from an espfs instance
 *  @param[in] fs       espfs handle
 *  @param[in] filename path
 *  @return             espfs_file handle or \a NULL if not found
 */
espfs_file_t* espfs_open(espfs_t* fs, const char *filename);

/** @brief Open a file from an espfs instance
 *  @param[in]  fs       espfs handle
 *  @param[in]  filename path
 *  @param[out] s        stat structure
 *  @return              \a true if sucessful
 */
bool espfs_stat(espfs_t *fs, const char *filename, espfs_stat_t *s);

/** @brief Return flags for an open file handle
 *  @param[in] fh espfs file handle
 *  @return       flags 
 */
espfs_flags_t espfs_flags(espfs_file_t *fh);

/** @brief Read data from an open file handle
 *  @param[in]  fh  espfs file handle
 *  @param[out] buf byte buffer
 *  @param[in]  len maximum bytes to read
 *  @return         actual number of bytes read, zero if end of file reached
 */
ssize_t espfs_read(espfs_file_t *fh, char *buf, size_t len);

/** @brief Seek to a position within an open file handle
 * 
 *  If the file is compressed, seeking within the file is not implemented. Only
 *  seeking to the start or end is supported in this case.  This may be
 *  implemented in the future.
 * 
 *  @param[in] fh     espfs file handle
 *  @param[in] offset position
 *  @param[in] mode   one of SEEK_SET, SEEK_CUR, SEEK_END
 *  @return           position in file or < 0 upon error
 */
ssize_t espfs_seek(espfs_file_t *fh, long offset, int mode);

/** @brief Test if open file handle is compressed
 * 
 *  @param[in] fh espfs file handle
 *  @return       \a true if compressed
 */
bool espfs_is_compressed(espfs_file_t *fh);

/** @brief Get raw memory access for an uncompressed open file handle
 * 
 *  @param[in]  fh  espfs file handle
 *  @param[out] buf pointer to pointer to buf
 *  @return         length of file or < 0 upon error
 */
ssize_t espfs_access(espfs_file_t *fh, void **buf);

/** @brief Get the file size for an open file handle
 * 
 *  @param[in] fh espfs file handle
 *  @return       length of file
 */
size_t espfs_filesize(espfs_file_t *fh);

/** @brief Close an open file handle
 * 
 *  @param[in] fh espfs file handle
 */
void espfs_close(espfs_file_t *fh);

/** @brief Mount an espfs handle under a vfs path
 * 
 *  @param[in] conf configuration
 *  @return         ESP_OK if successful, ESP_ERR_NO_MEM if too many VFSes are
 *                  registered
 */
esp_err_t esp_vfs_espfs_register(const esp_vfs_espfs_conf_t* conf);

extern const uint8_t espfsimage_bin[];
extern const size_t espfsimage_bin_len;

#ifdef __cplusplus
} /* extern "C" */
#endif
