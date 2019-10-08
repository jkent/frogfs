/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
This is a simple read-only implementation of a file system. It uses a block of data coming from the
mkespfsimg tool, and can use that block to do abstracted operations on the files that are in there.
It's written for use with httpd, but doesn't need to be used as such.
*/

//These routines can also be tested by comping them in with the espfstest tool. This
//simplifies debugging, but needs some slightly different headers. The #ifdef takes
//care of that.

#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_partition.h"
#include "esp_spi_flash.h"
#include "sdkconfig.h"

#include "espfsformat.h"
#include "espfs.h"
#include "espfs_priv.h"


#if CONFIG_ESPFS_USE_HEATSHRINK
#include "heatshrink_config_custom.h"
#include "heatshrink_decoder.h"
#endif

const static char* TAG = "espfs";


EspFs* espFsInit(EspFsConfig* conf)
{
	spi_flash_mmap_handle_t mmapHandle = 0;
	const void* memAddr = conf->memAddr;

	if (!memAddr) {
		esp_partition_subtype_t subtype = conf->partLabel ?
				ESP_PARTITION_SUBTYPE_ANY : ESP_PARTITION_SUBTYPE_DATA_ESPHTTPD;
		const esp_partition_t* partition = esp_partition_find_first(
				ESP_PARTITION_TYPE_DATA, subtype, conf->partLabel);
		if (!partition) {
			return NULL;
		}

		esp_err_t err = esp_partition_mmap(partition, 0, partition->size,
				SPI_FLASH_MMAP_DATA, &memAddr, &mmapHandle);
		if (err) {
			return NULL;
		}
	}

	const EspFsHeader *h = memAddr;
	if (h->magic != ESPFS_MAGIC) {
		ESP_LOGE(TAG, "Magic not found at %p", h);
		if (mmapHandle) {
			spi_flash_munmap(mmapHandle);
		}
		return NULL;
	}

	EspFs* fs = malloc(sizeof(EspFs));
	if (!fs) {
		ESP_LOGE(TAG, "Unable to allocate EspFs");
		if (mmapHandle) {
			spi_flash_munmap(mmapHandle);
		}
		return NULL;
	}

	uint32_t entry_length = sizeof(*h) + h->nameLen + h->fileLenComp;
	if (entry_length & 3) {
		entry_length += 4 - (entry_length & 3);
	}
	fs->length = entry_length;
	fs->numFiles = 0;

	do {
		fs->numFiles++;
		h = (void*)h + entry_length;
		if (h->magic != ESPFS_MAGIC) {
			ESP_LOGE(TAG, "Magic not found while walking EspFs");
			free(fs);
			if (mmapHandle) {
				spi_flash_munmap(mmapHandle);
			}
			return NULL;
		}
		entry_length = sizeof(*h) + h->nameLen + h->fileLenComp;
		if (entry_length & 3) {
			entry_length += 4 - (entry_length & 3);
		}
		fs->length += entry_length;
	} while (!(h->flags & FLAG_LASTFILE));

	fs->memAddr = memAddr;
	fs->mmapHandle = mmapHandle;
	return fs;
}

void espFsDeinit(EspFs* fs)
{
	if (fs->mmapHandle) {
		spi_flash_munmap(fs->mmapHandle);
	}
	free(fs);
}

// Returns flags of opened file.
int espFsFlags(EspFsFile *fh)
{
	if (fh == NULL) {
		ESP_LOGE(TAG, "Invalid file handle");
		return -1;
	}

	return fh->header->flags;
}

// Open a file and return a pointer to the file desc struct.
EspFsFile *espFsOpen(EspFs* fs, const char *fileName)
{
	if (!fs) {
		ESP_LOGE(TAG, "fs is null");
		return NULL;
	}

	char *p = (char *)fs->memAddr;
	char namebuf[256];
	EspFsHeader *h;
	EspFsFile *r;

	// Strip first initial slash
	// We should not strip any next slashes otherwise there is potential
	// security risk when mapped authentication handler will not invoke
	// (ex. ///security.html)
	if(fileName[0]=='/') fileName++;
	ESP_LOGD(TAG, "looking for file '%s'.", fileName);

	// Go find that file!
	while(1) {
		h = (EspFsHeader*)p;
		if (h->magic != ESPFS_MAGIC) {
			ESP_LOGE(TAG, "Magic mismatch. EspFS image broken.");
			return NULL;
		}
		if (h->flags & FLAG_LASTFILE) {
			ESP_LOGD(TAG, "End of image");
			return NULL;
		}
		// Grab the name of the file.
		p += sizeof(EspFsHeader);
		memcpy((uint32_t*)&namebuf, (uintptr_t*)p, sizeof(namebuf));
		if (strcmp(namebuf, fileName) == 0) {
			ESP_LOGD(TAG, "Found file '%s'. Namelen=%x fileLenComp=%x, compr=%d flags=%d",
					namebuf, (unsigned int)h->nameLen, (unsigned int)h->fileLenComp, h->compression, h->flags);
			// Yay, this is the file we need!
			p += h->nameLen; //Skip to content.
			r = (EspFsFile *)malloc(sizeof(EspFsFile)); // Alloc file desc mem
			ESP_LOGV(TAG, "Alloc %p", r);
			if (r == NULL) return NULL;
			r->header = h;
			r->decompressor = h->compression;
			r->posComp = p;
			r->posStart = p;
			r->posDecomp = 0;
			if (h->compression == COMPRESS_NONE) {
				r->decompData = NULL;
#if CONFIG_ESPFS_USE_HEATSHRINK
			} else if (h->compression == COMPRESS_HEATSHRINK) {
				// File is compressed with Heatshrink.
				char parm;
				heatshrink_decoder *dec;
				// Decoder params are stored in 1st byte.
				memcpy(&parm, r->posComp, 1);
				r->posComp++;
				ESP_LOGD(TAG, "Heatshrink compressed file; decode parms = %x", parm);
				dec = heatshrink_decoder_alloc(16, (parm >> 4) & 0xf, parm & 0xf);
				r->decompData=dec;
#endif
			} else {
				ESP_LOGE(TAG, "Invalid compression: %d", h->compression);
				free(r);
				return NULL;
			}
			return r;
		}
		// We don't need this file. Skip name and file
		p += h->nameLen + h->fileLenComp;
		if ((uintptr_t)p & 3) {
		    p += 4 - ((uintptr_t)p & 3); // align to next 32bit val
		}
	}
}

int espFsStat(EspFs* fs, const char *fileName, EspFsStat *s)
{
	if (!fs) {
		ESP_LOGE(TAG, "fs is null");
		return 0;
	}

	char *p = (char *)fs->memAddr;
	EspFsHeader *h;

	if(fileName[0]=='/') fileName++;
	ESP_LOGD(TAG, "looking for file '%s'.", fileName);

	size_t fileNameLen = strlen(fileName);
	size_t dirNameLen;
	char dirName[256];
	if (fileName[fileNameLen - 1] != '/') {
		strlcpy(dirName, fileName, sizeof(dirName) - 2);
	    strcat(dirName, "/");
		dirNameLen = fileNameLen + 1;
	} else {
		strlcpy(dirName, fileName, sizeof(dirName) - 1);
		dirNameLen = fileNameLen + 1;
	}

	s->type = ESPFS_TYPE_MISSING;
	s->size = 0;
	s->flags = 0;

	while(1) {
		h = (EspFsHeader*)p;
		if (h->magic != ESPFS_MAGIC) {
			ESP_LOGE(TAG, "Magic mismatch. EspFS image broken.");
			return 0;
		}
		if (h->flags & FLAG_LASTFILE) {
			ESP_LOGD(TAG, "End of image");
			return 0;
		}
		p += sizeof(EspFsHeader);
		if (strcmp((const char *)p, fileName) == 0) {
			s->type = ESPFS_TYPE_FILE;
			s->size = h->fileLenDecomp;
			s->flags = h->flags;
			return 1;
		}
		if (strncmp((const char *)p, dirName, dirNameLen)) {
			s->type = ESPFS_TYPE_DIR;
		}
		p += h->nameLen + h->fileLenComp;
		if ((uintptr_t)p & 3) {
		    p += 4 - ((uintptr_t)p & 3); // align to next 32bit val
		}
	}

	return s->type != ESPFS_TYPE_MISSING;
}

// Read len bytes from the given file into buff. Returns the actual amount of bytes read.
int espFsRead(EspFsFile *fh, char *buff, int len)
{
	int flen;
#if CONFIG_ESPFS_USE_HEATSHRINK
	int fdlen;
#endif
	if (fh == NULL) return 0;

	memcpy((char*)&flen, (char*)&fh->header->fileLenComp, 4);
	// Cache file length.
	// Do stuff depending on the way the file is compressed.
	if (fh->decompressor == COMPRESS_NONE) {
		int toRead;
		toRead = flen - (fh->posComp - fh->posStart);
		if (len>toRead) len = toRead;
		ESP_LOGV(TAG, "Reading %d bytes from %x", len, (unsigned int)fh->posComp);
		memcpy(buff, fh->posComp, len);
		fh->posDecomp += len;
		fh->posComp += len;
		ESP_LOGV(TAG, "Done reading %d bytes, pos=%x", len, (unsigned int)fh->posComp);
		return len;
#if CONFIG_ESPFS_USE_HEATSHRINK
	} else if (fh->decompressor==COMPRESS_HEATSHRINK) {
		memcpy((char*)&fdlen, (char*)&fh->header->fileLenDecomp, 4);
		int decoded = 0;
		size_t elen, rlen;
		char ebuff[16];
		heatshrink_decoder *dec = (heatshrink_decoder *)fh->decompData;
		ESP_LOGV(TAG, "Alloc %p", dec);
		if (fh->posDecomp == fdlen) {
			return 0;
		}

		// We must ensure that whole file is decompressed and written to output buffer.
		// This means even when there is no input data (elen==0) try to poll decoder until
		// posDecomp equals decompressed file length

		while(decoded < len) {
			// Feed data into the decompressor
			// ToDo: Check ret val of heatshrink fns for errors
			elen = flen - (fh->posComp - fh->posStart);
			if (elen>0) {
				memcpy(ebuff, fh->posComp, 16);
				heatshrink_decoder_sink(dec, (uint8_t *)ebuff, (elen > 16) ? 16 : elen, &rlen);
				fh->posComp += rlen;
			}
			// Grab decompressed data and put into buff
			heatshrink_decoder_poll(dec, (uint8_t *)buff, len - decoded, &rlen);
			fh->posDecomp += rlen;
			buff += rlen;
			decoded += rlen;

			ESP_LOGV(TAG, "Elen %d rlen %d d %d pd %d fdl %d", elen, rlen, decoded, fh->posDecomp, fdlen);

			if (elen == 0) {
				if (fh->posDecomp == fdlen) {
					ESP_LOGD(TAG, "Decoder finish");
					heatshrink_decoder_finish(dec);
				}
				return decoded;
			}
		}
		return len;
#endif
	}
	return 0;
}

// Seek in the file.
int espFsSeek(EspFsFile *fh, long offset, int mode)
{
	if (fh == NULL) {
		return -1;
	}

	if (mode == SEEK_SET) {
		if (offset < 0) {
			return -1;
		} else if (offset == 0) {
			fh->posComp = fh->posStart;
			fh->posDecomp = 0;
		} else if (fh->decompressor == COMPRESS_NONE) {
			if (offset > fh->header->fileLenComp) {
				offset = fh->header->fileLenComp;
			}
			fh->posComp = fh->posStart + offset;
			fh->posDecomp = offset;
		} else {
			return -1;
		}
	} else if (mode == SEEK_CUR) {
		if (offset == 0) {
			return fh->posDecomp;
		} else if (fh->decompressor == COMPRESS_NONE) {
			if (offset < 0) {
				if (fh->posDecomp + offset < 0) {
					fh->posComp = fh->posStart;
					fh->posDecomp = 0;
				} else {
					fh->posComp = fh->posComp + offset;
					fh->posDecomp = fh->posDecomp + offset;
				}
			} else {
				if (fh->posDecomp + offset > fh->header->fileLenComp) {
					fh->posComp = fh->posStart + fh->header->fileLenComp;
					fh->posDecomp = fh->header->fileLenComp;
				} else {
					fh->posComp = fh->posComp + offset;
					fh->posDecomp = fh->posDecomp + offset;
				}
			}
		} else {
			return -1;
		}
	} else if (mode == SEEK_END && fh->decompressor == COMPRESS_NONE) {
		if (offset == 0) {
			fh->posComp = fh->posStart + fh->header->fileLenComp;
			fh->posDecomp = fh->header->fileLenComp;
		} else if (offset < 0) {
			if (fh->header->fileLenComp + offset < 0) {
				fh->posComp = fh->posStart;
				fh->posDecomp = 0;
			} else {
				fh->posComp = fh->posStart + fh->header->fileLenComp + offset;
				fh->posDecomp = fh->header->fileLenComp + offset;
			}
		} else {
			return -1;
		}
	} else {
		return -1;
	}
	return fh->posDecomp;
}

bool espFsIsCompressed(EspFsFile *fh)
{
	return (fh->header->compression != COMPRESS_NONE);
}

int espFsAccess(EspFsFile *fh, void **buf)
{
	if (espFsIsCompressed(fh)) {
		return -1;
	}
	*buf = fh->posStart;
	return fh->header->fileLenComp;
}

int espFsFilesize(EspFsFile *fh)
{
	return fh->header->fileLenDecomp;
}

// Close the file.
void espFsClose(EspFsFile *fh)
{
	if (fh == NULL) return;
#if CONFIG_ESPFS_USE_HEATSHRINK
	if (fh->decompressor == COMPRESS_HEATSHRINK) {
		heatshrink_decoder *dec = (heatshrink_decoder *)fh->decompData;
		heatshrink_decoder_free(dec);
		ESP_LOGV(TAG, "Freed %p", dec);
	}
#endif

	ESP_LOGV(TAG, "Freed %p", fh);
	free(fh);
}