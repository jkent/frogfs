/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "log.h"
#include "frogfs_priv.h"
#include "frogfs_format.h"
#include "frogfs/frogfs.h"

#include "miniz.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>


#define BUFFER_LEN 16

typedef struct {
    tinfl_decompressor inflator;
    uint8_t buf[TINFL_LZ_DICT_SIZE];
    size_t buf_pos;
    size_t buf_len;
    size_t out_pos;
} priv_data_t;

static int open_deflate(frogfs_fh_t *f, unsigned int flags)
{
    priv_data_t *priv = malloc(sizeof(priv_data_t));
    if (priv == NULL) {
        LOGE("malloc failed");
        return -1;
    }

    tinfl_init(&priv->inflator);
    priv->buf_pos = 0;
    priv->buf_len = 0;
    priv->out_pos = 0;
    f->decomp_priv = priv;
    return 0;
}

static void close_deflate(frogfs_fh_t *f)
{
    priv_data_t *priv = f->decomp_priv;
    free(priv);
    f->decomp_priv = NULL;
}

static ssize_t read_deflate(frogfs_fh_t *f, void *buf, size_t len)
{
    priv_data_t *priv = f->decomp_priv;
    tinfl_status status;
    size_t start_len = len;
    size_t in_bytes;
    size_t out_bytes;

    while (len) {
        size_t chunk = len < priv->buf_len - priv->buf_pos ? len :
                priv->buf_len - priv->buf_pos;
        memcpy(buf, priv->buf + priv->buf_pos, chunk);
        priv->buf_pos += chunk;
        priv->out_pos += chunk;
        buf += chunk;
        len -= chunk;

        if (priv->buf_len == priv->buf_pos) {
            priv->buf_len = 0;
            priv->buf_pos = 0;
        }

        in_bytes = f->data_sz - (f->data_ptr - f->data_start);
        out_bytes = sizeof(priv->buf) - priv->buf_len;
        status = tinfl_decompress(&priv->inflator, f->data_ptr, &in_bytes,
                priv->buf, &priv->buf[priv->buf_len], &out_bytes,
                TINFL_FLAG_PARSE_ZLIB_HEADER);
        f->data_ptr += in_bytes;
        priv->buf_len += out_bytes;

        if (status < TINFL_STATUS_DONE) {
            LOGE("tinfl_decompress");
            return -1;
        }

        if (priv->buf_len - priv->buf_pos == 0) {
            break;
        }
    }

    return start_len - len;
}

static ssize_t seek_deflate(frogfs_fh_t *f, long offset, int mode)
{
    priv_data_t *priv = f->decomp_priv;
    const frogfs_comp_t *comp = (const void *) f->file;
    ssize_t new_pos = priv->out_pos;

    if (mode == SEEK_SET) {
        if (offset < 0) {
            return -1;
        }
        if (offset > comp->real_sz) {
            offset = comp->real_sz;
        }
        new_pos = offset;
    } else if (mode == SEEK_CUR) {
        if (new_pos + offset < 0) {
            new_pos = 0;
        } else if (new_pos > comp->real_sz) {
            new_pos = comp->real_sz;
        } else {
            new_pos += offset;
        }
    } else if (mode == SEEK_END) {
        if (offset > 0) {
            return -1;
        }
        if (offset < -(ssize_t) comp->real_sz) {
            offset = 0;
        }
        new_pos = comp->real_sz + offset;
    } else {
        return -1;
    }

    if (new_pos < priv->out_pos) {
        f->data_ptr = f->data_start;
        tinfl_init(&priv->inflator);
        priv->buf_len = 0;
        priv->buf_pos = 0;
        priv->out_pos = 0;
    }

    while (new_pos > priv->out_pos) {
        uint8_t buf[BUFFER_LEN];
        size_t len = new_pos - priv->out_pos < BUFFER_LEN ?
                new_pos - priv->out_pos : BUFFER_LEN;

        ssize_t res = frogfs_read(f, buf, len);
        if (res < 0) {
            LOGE("frogfs_read");
            return -1;
        }
    }

    return priv->out_pos;
}

static size_t tell_deflate(frogfs_fh_t *f)
{
    priv_data_t *priv = f->decomp_priv;
    return priv->out_pos;
}

const frogfs_decomp_funcs_t frogfs_decomp_deflate = {
    .open = open_deflate,
    .close = close_deflate,
    .read = read_deflate,
    .seek = seek_deflate,
    .tell = tell_deflate,
};
