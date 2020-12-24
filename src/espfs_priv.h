#pragma once

#include <stddef.h>
#include "libespfs/format.h"

struct EspFs {
	const void *memAddr;
#if defined(CONFIG_IDF_TARGET_ESP32) || \
    defined(CONFIG_IDF_TARGET_ESP32S2) || \
	defined(CONFIG_IDF_TARGET_ESP32S3)
	spi_flash_mmap_handle_t mmapHandle;
#endif
	size_t length;
	size_t numFiles;
};

struct EspFsFile {
	EspFsHeader *header;
	char decompressor;
	int32_t posDecomp;
	char *posStart;
	char *posComp;
	void *decompData;
};
