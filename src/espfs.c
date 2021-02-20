/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This is a simple read-only implementation file system. It uses a block of
 * data coming from the mkespfsimg tool, and can use that block to do
 * abstracted operations on the files that are in there. It's written for use
 * with libesphttpd, but doesn't need to be used as such.
 */

/**
 * These routines can also be tested by comping them in with the espfstest
 * tool. This simplifies debugging, but needs some slightly different headers.
 * The #ifdef takes care of that.
 */

#include "espfs_priv.h"
#include "log.h"
#include "libespfs/espfs.h"

#if CONFIG_ESPFS_USE_HEATSHRINK
# include "heatshrink_decoder.h"
#endif

#if defined(CONFIG_ENABLE_FLASH_MMAP)
# include <esp_partition.h>
# include <esp_spi_flash.h>
#endif

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>


espfs_fs_t *espfs_init(espfs_config_t *conf)
{
#if defined(CONFIG_ENABLE_FLASH_MMAP)
	spi_flash_mmap_handle_t mmap_handle = 0;
#endif
	const void *addr = conf->addr;

	if (!addr) {
#if defined(CONFIG_ENABLE_FLASH_MMAP)
		esp_partition_subtype_t subtype = conf->partition ?
				ESP_PARTITION_SUBTYPE_ANY : ESP_PARTITION_SUBTYPE_DATA_ESPHTTPD;
		const esp_partition_t *partition = esp_partition_find_first(
				ESP_PARTITION_TYPE_DATA, subtype, conf->partition);
		if (!partition) {
			return NULL;
		}

		esp_err_t err = esp_partition_mmap(partition, 0, partition->size,
				SPI_FLASH_MMAP_DATA, &addr, &mmap_handle);
		if (err) {
			return NULL;
		}
#else
		return NULL;
#endif
	}

	espfs_fs_t *fs = NULL;

	const espfs_header_t *h = (const espfs_header_t *) addr;
	if (h->magic != ESPFS_MAGIC) {
		ESPFS_LOGE(__func__, "Magic not found at %p", h);
		goto err;
	}

	fs = malloc(sizeof(espfs_fs_t));
	if (!fs) {
		ESPFS_LOGE(__func__, "Unable to allocate espfs");
		goto err;
	}

	uint32_t entry_length = sizeof(*h) + h->filename_len + h->raw_len;
	if (entry_length & 3) {
		entry_length += 4 - (entry_length & 3);
	}
	fs->length = entry_length;
	fs->num_files = 0;

	do {
		fs->num_files++;
		h = (void *) h + entry_length;
		if (h->magic != ESPFS_MAGIC) {
			ESPFS_LOGE(__func__, "Magic not found while walking espfs");
			goto err;
		}
		entry_length = sizeof(*h) + h->filename_len + h->raw_len;
		if (entry_length & 3) {
			entry_length += 4 - (entry_length & 3);
		}
		fs->length += entry_length;
	} while (!(h->flags & ESPFS_FLAG_LASTFILE));

	fs->addr = addr;
#if defined(CONFIG_ENABLE_FLASH_MMAP)
	fs->mmap_handle = mmap_handle;
#endif
	return fs;

err:
	if (fs) {
		free(fs);
	}
#if defined(CONFIG_ENABLE_FLASH_MMAP)
	if (mmap_handle) {
		spi_flash_munmap(mmap_handle);
	}
#endif
	return NULL;
}

void espfs_deinit(espfs_fs_t *fs)
{
#if defined(CONFIG_ENABLE_FLASH_MMAP)
	if (fs->mmap_handle) {
		spi_flash_munmap(fs->mmap_handle);
	}
#endif
	free(fs);
}

// Returns flags of opened file.
espfs_flags_t espfs_flags(espfs_file_t *fh)
{
	if (fh == NULL) {
		ESPFS_LOGE(__func__, "Invalid file handle");
		return -1;
	}

	return fh->header->flags;
}

// Open a file and return a pointer to the file desc struct.
espfs_file_t *espfs_open(espfs_fs_t *fs, const char *filename)
{
	if (!fs) {
		ESPFS_LOGE(__func__, "fs is null");
		return NULL;
	}

	char *p = (char *) fs->addr;
	char namebuf[256];
	espfs_header_t *h;
	espfs_file_t *r;

	// Strip leading slashes
	while (*filename == '/') {
		filename++;
	}
	ESPFS_LOGD(__func__, "looking for file '%s'.", filename);

	// Go find that file!
	while(1) {
		h = (espfs_header_t *) p;
		if (h->magic != ESPFS_MAGIC) {
			ESPFS_LOGE(__func__, "Magic mismatch. espfs image broken.");
			return NULL;
		}
		if (h->flags & ESPFS_FLAG_LASTFILE) {
			ESPFS_LOGD(__func__, "End of image");
			return NULL;
		}
		// Grab the name of the file.
		p += sizeof(espfs_header_t);
		memcpy((uint32_t *) &namebuf, (uintptr_t *) p, sizeof(namebuf));
		if (strcmp(namebuf, filename) == 0) {
			ESPFS_LOGD(__func__, "Found file '%s'. Namelen=%x raw_len=%x, compr=%d flags=%d",
					namebuf, (unsigned int)h->filename_len, (unsigned int)h->raw_len, h->compression, h->flags);
			// Yay, this is the file we need!
			p += h->filename_len; //Skip to content.
			r = (espfs_file_t *) malloc(sizeof(espfs_file_t)); // Alloc file desc mem
			ESPFS_LOGV(__func__, "Alloc %p", r);
			if (r == NULL) return NULL;
			r->header = h;
			r->decompressor = h->compression;
			r->raw_pos = p;
			r->raw_start = p;
			r->file_pos = 0;
			if (h->compression == ESPFS_COMPRESS_NONE) {
				r->user = NULL;
#if CONFIG_ESPFS_USE_HEATSHRINK
			} else if (h->compression == ESPFS_COMPRESS_HEATSHRINK) {
				// File is compressed with Heatshrink.
				char parm;
				heatshrink_decoder *dec;
				// Decoder params are stored in 1st byte.
				memcpy(&parm, r->raw_pos, 1);
				r->raw_pos++;
				ESPFS_LOGD(__func__, "Heatshrink compressed file; decode parms = %x", parm);
				dec = heatshrink_decoder_alloc(16, (parm >> 4) & 0xf, parm & 0xf);
				r->user=dec;
#endif
			} else {
				ESPFS_LOGE(__func__, "Invalid compression: %d", h->compression);
				free(r);
				return NULL;
			}
			return r;
		}
		// We don't need this file. Skip name and file
		p += h->filename_len + h->raw_len;
		if ((uintptr_t)p & 3) {
		    p += 4 - ((uintptr_t)p & 3); // align to next 32bit val
		}
	}
}

bool espfs_stat(espfs_fs_t *fs, const char *filename, espfs_stat_t *s)
{
	if (!fs) {
		ESPFS_LOGE(__func__, "fs is null");
		return 0;
	}

	char *p = (char *) fs->addr;
	espfs_header_t *h;

	// Strip leading slashes
	while (*filename == '/') {
		filename++;
	}
	ESPFS_LOGD(__func__, "looking for file '%s'.", filename);

	memset(s, 0, sizeof(espfs_stat_t));

	if(strlen(filename) == 0) {
		s->type = ESPFS_TYPE_DIR;
		return 1;
	}

	size_t filename_len = strlen(filename);
	size_t dirname_len;
	char dirname[256];

	strncpy(dirname, filename, sizeof(dirname));
	dirname[sizeof(dirname) - 1] = '\0';
	if (filename[filename_len - 1] != '/') {
		strncat(dirname, "/", sizeof(dirname) - 1);
	}
	dirname_len = strlen(dirname);

	while (true) {
		h = (espfs_header_t *) p;
		if (h->magic != ESPFS_MAGIC) {
			ESPFS_LOGE(__func__, "Magic mismatch. espfs image broken.");
			return 0;
		}
		if (h->flags & ESPFS_FLAG_LASTFILE) {
			ESPFS_LOGD(__func__, "End of image");
			break;
		}
		p += sizeof(espfs_header_t);
		if (strcmp((const char *) p, filename) == 0) {
			s->type = ESPFS_TYPE_FILE;
			s->size = h->file_len;
			s->flags = h->flags;
			return 1;
		}
		if (strncmp((const char *) p, dirname, dirname_len) == 0) {
			s->type = ESPFS_TYPE_DIR;
		}
		p += h->filename_len + h->raw_len;
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
	if (fh == NULL) {
		return 0;
	}

	memcpy((char *) &flen, (char *) &fh->header->raw_len, 4);
	// Cache file length.
	// Do stuff depending on the way the file is compressed.
	if (fh->decompressor == ESPFS_COMPRESS_NONE) {
		int to_read = flen - (fh->raw_pos - fh->raw_start);
		if (len > to_read) {
			len = to_read;
		}
		ESPFS_LOGV(__func__, "Reading %d bytes from %p", len, fh->raw_pos);
		memcpy(buf, fh->raw_pos, len);
		fh->file_pos += len;
		fh->raw_pos += len;
		ESPFS_LOGV(__func__, "Done reading %d bytes, pos=%p", len, fh->raw_pos);
		return len;
#if CONFIG_ESPFS_USE_HEATSHRINK
	} else if (fh->decompressor==ESPFS_COMPRESS_HEATSHRINK) {
		memcpy((char *) &fdlen, (char *) &fh->header->file_len, 4);
		size_t decoded = 0;
		size_t elen, rlen;
		char ebuf[16];
		heatshrink_decoder *dec = (heatshrink_decoder *) fh->user;
		ESPFS_LOGV(__func__, "Alloc %p", dec);
		if (fh->file_pos == fdlen) {
			return 0;
		}

		// We must ensure that whole file is decompressed and written to output buffer.
		// This means even when there is no input data (elen==0) try to poll decoder until
		// file_pos equals decompressed file length

		while(decoded < len) {
			// Feed data into the decompressor
			// TODO: Check ret val of heatshrink fns for errors
			elen = flen - (fh->raw_pos - fh->raw_start);
			if (elen>0) {
				memcpy(ebuf, fh->raw_pos, 16);
				heatshrink_decoder_sink(dec, (uint8_t *) ebuf, (elen > 16) ? 16 : elen, &rlen);
				fh->raw_pos += rlen;
			}
			// Grab decompressed data and put into buff
			heatshrink_decoder_poll(dec, (uint8_t *) buf, len - decoded, &rlen);
			fh->file_pos += rlen;
			buf += rlen;
			decoded += rlen;

			ESPFS_LOGV(__func__, "Elen %d rlen %d d %d pd %d fdl %d", elen, rlen, decoded, fh->file_pos, fdlen);

			if (elen == 0) {
				if (fh->file_pos == fdlen) {
					ESPFS_LOGD(__func__, "Decoder finish");
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
			fh->raw_pos = fh->raw_start;
			fh->file_pos = 0;
		} else if (fh->decompressor == ESPFS_COMPRESS_NONE) {
			if (offset > fh->header->raw_len) {
				offset = fh->header->raw_len;
			}
			fh->raw_pos = fh->raw_start + offset;
			fh->file_pos = offset;
		} else {
			return -1;
		}
	} else if (mode == SEEK_CUR) {
		if (offset == 0) {
			return fh->file_pos;
		} else if (fh->decompressor == ESPFS_COMPRESS_NONE) {
			if (offset < 0) {
				if (fh->file_pos + offset < 0) {
					fh->raw_pos = fh->raw_start;
					fh->file_pos = 0;
				} else {
					fh->raw_pos = fh->raw_pos + offset;
					fh->file_pos = fh->file_pos + offset;
				}
			} else {
				if (fh->file_pos + offset > fh->header->raw_len) {
					fh->raw_pos = fh->raw_start + fh->header->raw_len;
					fh->file_pos = fh->header->raw_len;
				} else {
					fh->raw_pos = fh->raw_pos + offset;
					fh->file_pos = fh->file_pos + offset;
				}
			}
		} else {
			return -1;
		}
	} else if (mode == SEEK_END && fh->decompressor == ESPFS_COMPRESS_NONE) {
		if (offset == 0) {
			fh->raw_pos = fh->raw_start + fh->header->raw_len;
			fh->file_pos = fh->header->raw_len;
		} else if (offset < 0) {
			if (fh->header->raw_len + offset < 0) {
				fh->raw_pos = fh->raw_start;
				fh->file_pos = 0;
			} else {
				fh->raw_pos = fh->raw_start + fh->header->raw_len + offset;
				fh->file_pos = fh->header->raw_len + offset;
			}
		} else {
			return -1;
		}
	} else {
		return -1;
	}
	return fh->file_pos;
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
	*buf = fh->raw_start;
	return fh->header->raw_len;
}

size_t espfs_filesize(espfs_file_t *fh)
{
	return fh->header->file_len;
}

// Close the file.
void espfs_close(espfs_file_t *fh)
{
	if (fh == NULL) {
		return;
	}

#if CONFIG_ESPFS_USE_HEATSHRINK
	if (fh->decompressor == ESPFS_COMPRESS_HEATSHRINK) {
		heatshrink_decoder *dec = (heatshrink_decoder *) fh->user;
		ESPFS_LOGV(__func__, "Free %p", dec);
		heatshrink_decoder_free(dec);
	}
#endif

	ESPFS_LOGV(__func__, "Free %p", fh);
	free(fh);
}
