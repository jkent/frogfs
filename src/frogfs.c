/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This is a read-only filesystem that uses a sorted hash table to locate
 * objects in a monolithic binary. The binary is generated by the mkfrogfs
 * tool that comes with this source distribution.
 */

#include "log.h"
#include "frogfs/frogfs.h"
#include "frogfs/format.h"

#if defined(ESP_PLATFORM)
# include "esp_partition.h"
# include "spi_flash_mmap.h"
#endif

#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>


// Returns the current or next highest multiple of n.
static inline size_t align(size_t n, size_t m)
{
    return ((n + m - 1) / m) * m;
}

// String hashing function.
static uint32_t djb2_hash(const char *s)
{
    unsigned long hash = 5381;

    while (*s) {
        /* hash = hash * 33 ^ c */
        hash = ((hash << 5) + hash) ^ *s++;
    }

    return hash;
}

frogfs_fs_t *frogfs_init(frogfs_config_t *conf)
{
    frogfs_fs_t *fs = calloc(1, sizeof(frogfs_fs_t));
    if (fs == NULL) {
        LOGE("calloc failed");
        return NULL;
    }

    LOGV("%p", fs);

    fs->decomp_funcs = conf->decomp_funcs;

    fs->head = (const void *) conf->addr;
    if (fs->head == NULL) {
#if defined (ESP_PLATFORM)
        esp_partition_subtype_t subtype = conf->part_label ?
                ESP_PARTITION_SUBTYPE_ANY :
                ESP_PARTITION_SUBTYPE_DATA_ESPHTTPD;
        const esp_partition_t* partition = esp_partition_find_first(
                ESP_PARTITION_TYPE_DATA, subtype, conf->part_label);

        if (partition == NULL) {
            LOGE("unable to find frogfs partition");
            goto err_out;
        }

        if (esp_partition_mmap(partition, 0, partition->size,
                SPI_FLASH_MMAP_DATA, (const void **)&fs->head,
                &fs->mmap_handle) != ESP_OK) {
            LOGE("mmap failed");
            goto err_out;
        }
#else
        LOGE("flash mmap not enabled and addr is NULL");
        goto err_out;
#endif
    }

    if (fs->head->magic != FROGFS_MAGIC) {
        LOGE("frogfs magic not found");
        goto err_out;
    }

    if (fs->head->ver_major != FROGFS_VER_MAJOR) {
        LOGE("frogfs major version mismatch. filesystem is v%d.%d and this "
                "library is v%d.%d", fs->head->ver_major, fs->head->ver_minor,
                FROGFS_VER_MAJOR, FROGFS_VER_MINOR);
        goto err_out;
    }

    fs->hashes = (const void *) fs->head + align(fs->head->len,
            fs->head->align);

    return fs;

err_out:
    frogfs_deinit(fs);
    return NULL;
}

void frogfs_deinit(frogfs_fs_t *fs)
{
    LOGV("%p", fs);

#if defined(ESP_PLATFORM)
    if (fs->mmap_handle) {
        spi_flash_munmap(fs->mmap_handle);
    }
#endif
    free(fs);
}

const frogfs_obj_t *frogfs_obj_from_path(const frogfs_fs_t *fs,
        const char *path)
{
    assert(fs != NULL);
    assert(path != NULL);

    while (*path == '/') {
        path++;
    }
    LOGV("%s", path);

    uint32_t hash = djb2_hash(path);
    LOGV("hash %08"PRIx32, hash);

    int first = 0;
    int last = fs->head->num_objs - 1;
    int middle;
    const frogfs_hash_t *entry;

    while (first <= last) {
        middle = first + (last - first) / 2;
        entry = &fs->hashes[middle];
        if (entry->hash == hash) {
            break;
        } else if (entry->hash < hash) {
            first = middle + 1;
        } else {
            last = middle - 1;
        }
    }

    if (first > last) {
        LOGV("no match");
        return NULL;
    }

    /* be optimistic and test the first match */
    const frogfs_obj_t *obj = (const void *) fs->head + entry->offset;
    if (strcmp(path, frogfs_path_from_obj(obj)) == 0) {
        LOGV("object %d", middle);
        return obj;
    }

    /* hash collision, move entry to the first match */
    LOGV("hash collision");
    int skip = middle;
    while (middle > 0) {
        entry = fs->hashes + middle;
        if ((entry - 1)->hash != hash) {
            break;
        }
        middle--;
    }

    /* walk through canidates and look for a match */
    do {
        if (middle != skip) {
            obj = (const void *) fs->head + entry->offset;
            if (strcmp(path, frogfs_path_from_obj(obj)) == 0) {
                LOGV("object %d", middle);
                return obj;
            }
        }
        entry++;
        middle++;
    } while ((middle < fs->head->num_objs) && (entry->hash == hash));

    LOGW("unable to find object");
    return NULL;
}

const char *frogfs_path_from_obj(const frogfs_obj_t *obj)
{
    if (obj == NULL) {
        return NULL;
    }

    return (const char *) obj + obj->len;
}

void frogfs_stat(const frogfs_fs_t *fs, const frogfs_obj_t *obj,
        frogfs_stat_t *st)
{
    assert(fs != NULL);
    assert(obj != NULL);

    memset(st, 0, sizeof(*st));
    st->type = obj->type;
    if (obj->type == FROGFS_OBJ_TYPE_FILE) {
        const frogfs_file_t *file = (const void *) obj;
        st->compression = file->compression;
        if (file->compression != FROGFS_COMP_NONE) {
            const frogfs_file_comp_t *comp = (const void *) file;
            st->size = comp->uncompressed_len;
            st->size_compressed = comp->data_len;
        } else {
            st->size = file->data_len;
        }
    }
}

frogfs_f_t *frogfs_open(const frogfs_fs_t *fs, const frogfs_obj_t *obj,
        unsigned int flags)
{
    assert(fs != NULL);
    assert(obj != NULL);
    assert(obj->type == FROGFS_OBJ_TYPE_FILE);

    const frogfs_file_t *file = (const void *) obj;

    frogfs_f_t *f = calloc(1, sizeof(frogfs_f_t));
    if (f == NULL) {
        LOGE("calloc failed");
        goto err_out;
    }

    LOGV("%p", f);

    f->fs = fs;
    f->file = file;
    f->data_start = (void *) obj + align(obj->len + obj->path_len,
            f->fs->head->align);
    f->data_ptr = f->data_start;
    f->flags = flags;

    if (file->compression == 0 || flags & FROGFS_OPEN_RAW) {
        f->decomp_funcs = &frogfs_decomp_raw;
    }
#if defined(CONFIG_FROGFS_USE_DEFLATE)
    else if (file->compression == FROGFS_COMP_DEFLATE) {
        f->decomp_funcs = &frogfs_decomp_deflate;
    }
#endif
#if defined(CONFIG_FROGFS_USE_HEATSHRINK)
    else if (file->compression == FROGFS_COMP_HEATSHRINK) {
        f->decomp_funcs = &frogfs_decomp_heatshrink;
    }
#endif
    else if (fs->decomp_funcs) {
        const frogfs_decomp_t *entry = fs->decomp_funcs;
        while (entry->funcs) {
            if (file->compression == entry->id) {
                f->decomp_funcs = entry->funcs;
                break;
            }
            entry++;
        }
    }
    if (f->decomp_funcs == NULL) {
        LOGE("unknown compression type %d", file->compression)
        goto err_out;
    }

    if (f->decomp_funcs->open) {
        if (f->decomp_funcs->open(f, flags) < 0) {
            LOGE("decomp_funcs->fopen");
            goto err_out;
        }
    }

    return f;

err_out:
    frogfs_close(f);
    return NULL;
}

void frogfs_close(frogfs_f_t *f)
{
    if (f == NULL) {
        /* do nothing */
        return;
    }

    if (f->decomp_funcs && f->decomp_funcs->close) {
        f->decomp_funcs->close(f);
    }

    LOGV("%p", f);

    free(f);
}

ssize_t frogfs_read(frogfs_f_t *f, void *buf, size_t len)
{
    assert(f != NULL);

    if (f->decomp_funcs->read) {
        return f->decomp_funcs->read(f, buf, len);
    }

    return -1;
}

ssize_t frogfs_seek(frogfs_f_t *f, long offset, int mode)
{
    assert(f != NULL);

    if (f->decomp_funcs->seek) {
        return f->decomp_funcs->seek(f, offset, mode);
    }

    return -1;
}

size_t frogfs_tell(frogfs_f_t *f)
{
    assert(f != NULL);

    if (f->decomp_funcs->tell) {
        return f->decomp_funcs->tell(f);
    }

    return -1;
}

size_t frogfs_access(frogfs_f_t *f, const void **buf)
{
    assert(f != NULL);

    *buf = f->data_start;
    return f->file->data_len;
}

#if defined(CONFIG_FROGFS_SUPPORT_DIR)
frogfs_d_t *frogfs_opendir(frogfs_fs_t *fs, const frogfs_obj_t *obj)
{
    assert(fs != NULL);
    assert(obj != NULL);

    if (obj->type != FROGFS_OBJ_TYPE_DIR) {
        return NULL;
    }

    frogfs_d_t *d = calloc(1, sizeof(frogfs_d_t));
    if (d == NULL) {
        LOGE("calloc failed");
        return NULL;
    }

    d->fs = fs;
    d->dir = (const void *) obj;
    d->children = (const void *) obj + align(obj->len + obj->path_len,
            fs->head->align);

    return d;
}

void frogfs_closedir(frogfs_d_t *d)
{
    if (d == NULL) {
        return;
    }

    free(d);
}

const frogfs_obj_t *frogfs_readdir(frogfs_d_t *d)
{
    const frogfs_obj_t *obj = NULL;

    if (d->dir->child_count > d->index) {
        obj = (const void *) d->fs->head + d->children[d->index++].offset;
    }

    return obj;
}

void frogfs_rewinddir(frogfs_d_t *d)
{
    assert(d != NULL);

    d->index = 0;
}

void frogfs_seekdir(frogfs_d_t *d, uint16_t loc)
{
    assert(d != NULL);
    assert(loc < d->dir->child_count);

    d->index = loc;
}

uint16_t frogfs_telldir(frogfs_d_t *d)
{
    assert(d != NULL);

    return d->index;
}
#endif
