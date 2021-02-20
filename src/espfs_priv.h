/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(CONFIG_IDF_TARGET_ESP32) || \
    defined(CONFIG_IDF_TARGET_ESP32S2) || \
	defined(CONFIG_IDF_TARGET_ESP32S3)
# include <esp_spi_flash.h>
#endif


/**
 * \brief Magic number used in the espfs file header
 */
#define ESPFS_MAGIC 0x73665345

typedef struct espfs_fs_t {
	const void *addr;
#if defined(CONFIG_IDF_TARGET_ESP32) || \
    defined(CONFIG_IDF_TARGET_ESP32S2) || \
	defined(CONFIG_IDF_TARGET_ESP32S3)
	spi_flash_mmap_handle_t mmap_handle;
#endif
	size_t length;
	size_t num_files;
} espfs_fs_t;

/**
 * Format notes from Sprite_tm:
 *
 * The idea 'borrows' from cpio: it's basically a concatenation of
 * {header, filename, file} data.  Header, filename and file data is 32-bit
 * aligned. The last file is indicated by data-less header with the
 * FLAG_LASTFILE flag set.
*/

typedef struct __attribute__((packed)) espfs_header_t {
	int32_t magic;
	int8_t flags;
	int8_t compression;
	int16_t filename_len;
	int32_t raw_len;
	int32_t file_len;
} espfs_header_t;

typedef struct espfs_file_t {
	struct espfs_header_t *header;
	char decompressor;
	int32_t file_pos;
	char *raw_start;
	char *raw_pos;
	void *user;
} espfs_file_t;
