#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct espfs_t {
	const void *addr;
#if defined(CONFIG_IDF_TARGET_ESP32) || \
    defined(CONFIG_IDF_TARGET_ESP32S2) || \
	defined(CONFIG_IDF_TARGET_ESP32S3)
	spi_flash_mmap_handle_t mmapHandle;
#endif
	size_t length;
	size_t numFiles;
} espfs_t;

/*
Format notes from Sprite_tm:

The idea 'borrows' from cpio: it's basically a concatenation of 
{header, filename, file} data.  Header, filename and file data is 32-bit
aligned. The last file is indicated by data-less header with the FLAG_LASTFILE
flag set.
*/

typedef struct __attribute__((packed)) espfs_header_t {
	int32_t magic;
	int8_t flags;
	int8_t compression;
	int16_t filenameLen;
	int32_t fileLenComp;
	int32_t fileLenDecomp;
} espfs_header_t;

typedef struct espfs_file_t {
	struct espfs_header_t *header;
	char decompressor;
	int32_t posDecomp;
	char *posStart;
	char *posComp;
	void *decompData;
} espfs_file_t;
