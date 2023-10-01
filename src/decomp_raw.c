/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "frogfs/frogfs.h"
#include "frogfs/format.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>


static ssize_t read_raw(frogfs_f_t *f, void *buf, size_t len)
{
    size_t remaining = f->data_sz - (f->data_ptr - f->data_start);

    if (len > remaining) {
        len = remaining;
    }

    if (buf) {
        memcpy(buf, f->data_ptr, len);
    }
    f->data_ptr += len;

    return len;
}

static ssize_t seek_raw(frogfs_f_t *f, long offset, int mode)
{
    ssize_t new_pos = f->data_ptr - f->data_start;

    if (mode == SEEK_SET) {
        if (offset < 0) {
            return -1;
        }
        if (offset > f->data_sz) {
            offset = f->data_sz;
        }
        new_pos = offset;
    } else if (mode == SEEK_CUR) {
        if (new_pos + offset < 0) {
            new_pos = 0;
        } else if (new_pos > f->data_sz) {
            new_pos = f->data_sz;
        } else {
            new_pos += offset;
        }
    } else if (mode == SEEK_END) {
        if (offset > 0) {
            return -1;
        }
        if (offset < -(ssize_t) f->data_sz) {
            offset = 0;
        }
        new_pos = f->data_sz + offset;
    } else {
        return -1;
    }

    f->data_ptr = f->data_start + new_pos;
    return new_pos;
}

static size_t tell_raw(frogfs_f_t *f)
{
    return f->data_ptr - f->data_start;
}

const frogfs_decomp_funcs_t frogfs_decomp_raw = {
    .read = read_raw,
    .seek = seek_raw,
    .tell = tell_raw,
};
