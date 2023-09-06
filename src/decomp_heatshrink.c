/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "log.h"
#include "frogfs/frogfs.h"
#include "frogfs/format.h"

#include "heatshrink_decoder.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>


#define BUFFER_LEN 16
#define PRIV(f) ((decomp_priv_t *)(f->decomp_priv))

typedef struct {
    heatshrink_decoder *hsd;
    size_t file_pos;
} decomp_priv_t;

static int open_heatshrink(frogfs_f_t *f, unsigned int flags)
{
    const frogfs_file_comp_t *file = (const frogfs_file_comp_t *) f->file;

    decomp_priv_t *data = malloc(sizeof(decomp_priv_t));
    if (data == NULL) {
        LOGE("malloc failed");
        return -1;
    }
    memset(data, 0, sizeof(*data));

    uint8_t window = file->options & 0xf;
    uint8_t lookahead = file->options >> 4;

    data->hsd = heatshrink_decoder_alloc(BUFFER_LEN, window, lookahead);
    if (data->hsd == NULL) {
        LOGE("error allocating heatshrink decoder");
        free(data);
        return -1;
    }

    f->decomp_priv = data;
    return 0;
}

static void close_heatshrink(frogfs_f_t *f)
{
    heatshrink_decoder_free(PRIV(f)->hsd);
    free(PRIV(f));
    f->decomp_priv = NULL;
}

static ssize_t read_heatshrink(frogfs_f_t *f, void *buf, size_t len)
{
    const frogfs_file_comp_t *file = (const frogfs_file_comp_t *) f->file;
    size_t rlen, decoded = 0;

    while (decoded < len) {
        /* feed data into the decoder */
        size_t remain = file->data_len - (f->data_ptr - f->data_start);
        if (remain > 0) {
            HSD_sink_res res = heatshrink_decoder_sink(PRIV(f)->hsd,
                    f->data_ptr, (remain > BUFFER_LEN) ? BUFFER_LEN : remain,
                    &rlen);
            if (res < 0) {
                LOGE("heatshrink_decoder_sink");
                return -1;
            }
            f->data_ptr += rlen;
        }

        /* poll decoder for data */
        HSD_poll_res res = heatshrink_decoder_poll(PRIV(f)->hsd,
                (uint8_t *) buf, len - decoded, &rlen);
        if (res < 0) {
            LOGE("heatshrink_decoder_poll");
            return -1;
        }
        PRIV(f)->file_pos += rlen;
        buf += rlen;
        decoded += rlen;

        /* end of input data */
        if (remain == 0) {
            if (PRIV(f)->file_pos == file->uncompressed_len) {
                HSD_finish_res res = heatshrink_decoder_finish(PRIV(f)->hsd);
                if (res < 0) {
                    LOGE("heatshink_decoder_finish");
                    return -1;
                }
                LOGV("heatshrink_decoder_finish");
            }
            return decoded;
        }
    }

    return len;
}

static ssize_t seek_heatshrink(frogfs_f_t *f, long offset, int mode)
{
    const frogfs_file_comp_t *file = (const frogfs_file_comp_t *) f->file;
    ssize_t new_pos = PRIV(f)->file_pos;

    if (mode == SEEK_SET) {
        if (offset < 0) {
            return -1;
        }
        if (offset > file->uncompressed_len) {
            offset = file->uncompressed_len;
        }
        new_pos = offset;
    } else if (mode == SEEK_CUR) {
        if (new_pos + offset < 0) {
            new_pos = 0;
        } else if (new_pos > file->uncompressed_len) {
            new_pos = file->uncompressed_len;
        } else {
            new_pos += offset;
        }
    } else if (mode == SEEK_END) {
        if (offset > 0) {
            return -1;
        }
        if (offset < -(ssize_t) file->uncompressed_len) {
            offset = 0;
        }
        new_pos = file->uncompressed_len + offset;
    } else {
        return -1;
    }

    if (PRIV(f)->file_pos > new_pos) {
        f->data_ptr = f->data_start;
        PRIV(f)->file_pos = 0;
        heatshrink_decoder_reset(PRIV(f)->hsd);
    }

    while (PRIV(f)->file_pos < new_pos) {
        uint8_t buf[BUFFER_LEN];
        size_t len = new_pos - PRIV(f)->file_pos < BUFFER_LEN ?
                new_pos - PRIV(f)->file_pos : BUFFER_LEN;

        ssize_t res = frogfs_read(f, buf, len);
        if (res < 0) {
            LOGE("frogfs_fread");
            return -1;
        }
    }

    return PRIV(f)->file_pos;
}

static size_t tell_heatshrink(frogfs_f_t *f)
{
    return PRIV(f)->file_pos;
}

const frogfs_decomp_funcs_t frogfs_decomp_heatshrink = {
    .open = open_heatshrink,
    .close = close_heatshrink,
    .read = read_heatshrink,
    .seek = seek_heatshrink,
    .tell = tell_heatshrink,
};
