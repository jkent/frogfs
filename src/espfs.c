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
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <esp_log.h>
#if defined(CONFIG_ENABLE_FLASH_MMAP)
# include <esp_partition.h>
# include <esp_spi_flash.h>
#endif

#include "espfs_priv.h"
#include "libespfs/espfs.h"


#if CONFIG_ESPFS_USE_HEATSHRINK
#include "heatshrink_decoder.h"
#endif

espfs_t* espfs_init(espfs_config_t* conf)
{
#if defined(CONFIG_ENABLE_FLASH_MMAP)
	spi_flash_mmap_handle_t mmapHandle = 0;
#endif
	const void* addr = conf->addr;

	if (!addr) {
#if defined(CONFIG_ENABLE_FLASH_MMAP)
		esp_partition_subtype_t subtype = conf->partition ?
				ESP_PARTITION_SUBTYPE_ANY : ESP_PARTITION_SUBTYPE_DATA_ESPHTTPD;
		const esp_partition_t* partition = esp_partition_find_first(
				ESP_PARTITION_TYPE_DATA, subtype, conf->partition);
		if (!partition) {
			return NULL;
		}

		esp_err_t err = esp_partition_mmap(partition, 0, partition->size,
				SPI_FLASH_MMAP_DATA, &addr, &mmapHandle);
		if (err) {
			return NULL;
		}
#else
		return NULL;
#endif
	}

	espfs_t* fs = NULL;

	const espfs_header_t *h = (const espfs_header_t *)addr;
	if (h->magic != ESPFS_MAGIC) {
		ESP_LOGE(__func__, "Magic not found at %p", h);
		goto err;
	}

	fs = malloc(sizeof(espfs_t));
	if (!fs) {
		ESP_LOGE(__func__, "Unable to allocate espfs");
		goto err;
	}

	uint32_t entry_length = sizeof(*h) + h->filenameLen + h->fileLenComp;
	if (entry_length & 3) {
		entry_length += 4 - (entry_length & 3);
	}
	fs->length = entry_length;
	fs->numFiles = 0;

	do {
		fs->numFiles++;
		h = (void*)h + entry_length;
		if (h->magic != ESPFS_MAGIC) {
			ESP_LOGE(__func__, "Magic not found while walking espfs");
			goto err;
		}
		entry_length = sizeof(*h) + h->filenameLen + h->fileLenComp;
		if (entry_length & 3) {
			entry_length += 4 - (entry_length & 3);
		}
		fs->length += entry_length;
	} while (!(h->flags & ESPFS_FLAG_LASTFILE));

	fs->addr = addr;
#if defined(CONFIG_ENABLE_FLASH_MMAP)
	fs->mmapHandle = mmapHandle;
#endif
	return fs;

err:
	if (fs) {
		free(fs);
	}
#if defined(CONFIG_ENABLE_FLASH_MMAP)
	if (mmapHandle) {
		spi_flash_munmap(mmapHandle);
	}
#endif
	return NULL;
}

void espfs_deinit(espfs_t* fs)
{
#if defined(CONFIG_ENABLE_FLASH_MMAP)
	if (fs->mmapHandle) {
		spi_flash_munmap(fs->mmapHandle);
	}
#endif
	free(fs);
}

// Returns flags of opened file.
espfs_flags_t espfs_flags(espfs_file_t *fh)
{
	if (fh == NULL) {
		ESP_LOGE(__func__, "Invalid file handle");
		return -1;
	}

	return fh->header->flags;
}

// Open a file and return a pointer to the file desc struct.
espfs_file_t *espfs_open(espfs_t* fs, const char *filename)
{
	if (!fs) {
		ESP_LOGE(__func__, "fs is null");
		return NULL;
	}

	char *p = (char *)fs->addr;
	char namebuf[256];
	espfs_header_t *h;
	espfs_file_t *r;

	// Strip first initial slash
	// We should not strip any next slashes otherwise there is potential
	// security risk when mapped authentication handler will not invoke
	// (ex. ///security.html)
	if(filename[0]=='/') filename++;
	ESP_LOGD(__func__, "looking for file '%s'.", filename);

	// Go find that file!
	while(1) {
		h = (espfs_header_t*)p;
		if (h->magic != ESPFS_MAGIC) {
			ESP_LOGE(__func__, "Magic mismatch. EspFS image broken.");
			return NULL;
		}
		if (h->flags & ESPFS_FLAG_LASTFILE) {
			ESP_LOGD(__func__, "End of image");
			return NULL;
		}
		// Grab the name of the file.
		p += sizeof(espfs_header_t);
		memcpy((uint32_t*)&namebuf, (uintptr_t*)p, sizeof(namebuf));
		if (strcmp(namebuf, filename) == 0) {
			ESP_LOGD(__func__, "Found file '%s'. Namelen=%x fileLenComp=%x, compr=%d flags=%d",
					namebuf, (unsigned int)h->filenameLen, (unsigned int)h->fileLenComp, h->compression, h->flags);
			// Yay, this is the file we need!
			p += h->filenameLen; //Skip to content.
			r = (espfs_file_t *)malloc(sizeof(espfs_file_t)); // Alloc file desc mem
			ESP_LOGV(__func__, "Alloc %p", r);
			if (r == NULL) return NULL;
			r->header = h;
			r->decompressor = h->compression;
			r->posComp = p;
			r->posStart = p;
			r->posDecomp = 0;
			if (h->compression == ESPFS_COMPRESS_NONE) {
				r->decompData = NULL;
#if CONFIG_ESPFS_USE_HEATSHRINK
			} else if (h->compression == ESPFS_COMPRESS_HEATSHRINK) {
				// File is compressed with Heatshrink.
				char parm;
				heatshrink_decoder *dec;
				// Decoder params are stored in 1st byte.
				memcpy(&parm, r->posComp, 1);
				r->posComp++;
				ESP_LOGD(__func__, "Heatshrink compressed file; decode parms = %x", parm);
				dec = heatshrink_decoder_alloc(16, (parm >> 4) & 0xf, parm & 0xf);
				r->decompData=dec;
#endif
			} else {
				ESP_LOGE(__func__, "Invalid compression: %d", h->compression);
				free(r);
				return NULL;
			}
			return r;
		}
		// We don't need this file. Skip name and file
		p += h->filenameLen + h->fileLenComp;
		if ((uintptr_t)p & 3) {
		    p += 4 - ((uintptr_t)p & 3); // align to next 32bit val
		}
	}
}

bool espfs_stat(espfs_t* fs, const char *filename, espfs_stat_t *s)
{
	if (!fs) {
		ESP_LOGE(__func__, "fs is null");
		return 0;
	}

	char *p = (char *)fs->addr;
	espfs_header_t *h;

	if(filename[0]=='/') filename++;
	ESP_LOGD(__func__, "looking for file '%s'.", filename);

	memset(s, 0, sizeof(espfs_stat_t));

	if(strlen(filename) == 0) {
		s->type = ESPFS_TYPE_DIR;
		return 1;
	}

	size_t filenameLen = strlen(filename);
	size_t dirNameLen;
	char dirName[256];

	strncpy(dirName, filename, sizeof(dirName));
	dirName[sizeof(dirName) - 1] = '\0';
	if (filename[filenameLen - 1] != '/') {
		strncat(dirName, "/", sizeof(dirName) - 1);
	}
	dirNameLen = strlen(dirName);

	while(1) {
		h = (espfs_header_t*)p;
		if (h->magic != ESPFS_MAGIC) {
			ESP_LOGE(__func__, "Magic mismatch. EspFS image broken.");
			return 0;
		}
		if (h->flags & ESPFS_FLAG_LASTFILE) {
			ESP_LOGD(__func__, "End of image");
			break;
		}
		p += sizeof(espfs_header_t);
		if (strcmp((const char *)p, filename) == 0) {
			s->type = ESPFS_TYPE_FILE;
			s->size = h->fileLenDecomp;
			s->flags = h->flags;
			return 1;
		}
		if (strncmp((const char *)p, dirName, dirNameLen) == 0) {
			s->type = ESPFS_TYPE_DIR;
		}
		p += h->filenameLen + h->fileLenComp;
		if ((uintptr_t)p & 3) {
		    p += 4 - ((uintptr_t)p & 3); // align to next 32bit val
		}
	}

	return s->type != ESPFS_TYPE_MISSING;
}

// Read len bytes from the given file into buf. Returns the actual amount of bytes read.
ssize_t espfs_read(espfs_file_t *fh, char *buf, size_t len)
{
	size_t flen;
#if CONFIG_ESPFS_USE_HEATSHRINK
	size_t fdlen;
#endif
	if (fh == NULL) return 0;

	memcpy((char*)&flen, (char*)&fh->header->fileLenComp, 4);
	// Cache file length.
	// Do stuff depending on the way the file is compressed.
	if (fh->decompressor == ESPFS_COMPRESS_NONE) {
		int toRead;
		toRead = flen - (fh->posComp - fh->posStart);
		if (len>toRead) len = toRead;
		ESP_LOGV(__func__, "Reading %d bytes from %p", len, fh->posComp);
		memcpy(buf, fh->posComp, len);
		fh->posDecomp += len;
		fh->posComp += len;
		ESP_LOGV(__func__, "Done reading %d bytes, pos=%p", len, fh->posComp);
		return len;
#if CONFIG_ESPFS_USE_HEATSHRINK
	} else if (fh->decompressor==ESPFS_COMPRESS_HEATSHRINK) {
		memcpy((char*)&fdlen, (char*)&fh->header->fileLenDecomp, 4);
		size_t decoded = 0;
		size_t elen, rlen;
		char ebuf[16];
		heatshrink_decoder *dec = (heatshrink_decoder *)fh->decompData;
		ESP_LOGV(__func__, "Alloc %p", dec);
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
				memcpy(ebuf, fh->posComp, 16);
				heatshrink_decoder_sink(dec, (uint8_t *)ebuf, (elen > 16) ? 16 : elen, &rlen);
				fh->posComp += rlen;
			}
			// Grab decompressed data and put into buff
			heatshrink_decoder_poll(dec, (uint8_t *)buf, len - decoded, &rlen);
			fh->posDecomp += rlen;
			buf += rlen;
			decoded += rlen;

			ESP_LOGV(__func__, "Elen %d rlen %d d %d pd %d fdl %d", elen, rlen, decoded, fh->posDecomp, fdlen);

			if (elen == 0) {
				if (fh->posDecomp == fdlen) {
					ESP_LOGD(__func__, "Decoder finish");
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
ssize_t espfs_seek(espfs_file_t *fh, long offset, int mode)
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
		} else if (fh->decompressor == ESPFS_COMPRESS_NONE) {
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
		} else if (fh->decompressor == ESPFS_COMPRESS_NONE) {
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
	} else if (mode == SEEK_END && fh->decompressor == ESPFS_COMPRESS_NONE) {
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

bool espfs_is_compressed(espfs_file_t *fh)
{
	return (fh->header->compression != ESPFS_COMPRESS_NONE);
}

ssize_t espfs_access(espfs_file_t *fh, void **buf)
{
	if (espfs_is_compressed(fh)) {
		return -1;
	}
	*buf = fh->posStart;
	return fh->header->fileLenComp;
}

size_t espfs_filesize(espfs_file_t *fh)
{
	return fh->header->fileLenDecomp;
}

// Close the file.
void espfs_close(espfs_file_t *fh)
{
	if (fh == NULL) return;
#if CONFIG_ESPFS_USE_HEATSHRINK
	if (fh->decompressor == ESPFS_COMPRESS_HEATSHRINK) {
		heatshrink_decoder *dec = (heatshrink_decoder *)fh->decompData;
		ESP_LOGV(__func__, "Free %p", dec);
		heatshrink_decoder_free(dec);
	}
#endif

	ESP_LOGV(__func__, "Free %p", fh);
	free(fh);
}
