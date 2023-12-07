/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>


#define RAMFS_PRIVATE_STRUCTS
typedef struct ramfs_fs_t ramfs_fs_t;
typedef struct ramfs_dir_t ramfs_dir_t;

/* format structures */
typedef struct ramfs_entry_t {
    ramfs_fs_t *fs;
    ramfs_dir_t *parent;
    const char *name;
    int type;
} ramfs_entry_t;

typedef struct ramfs_dir_t {
    ramfs_entry_t entry;
    ramfs_entry_t **children;
    size_t children_len;
} ramfs_dir_t;

typedef struct ramfs_file_t {
    ramfs_entry_t entry;
    uint8_t *data;
    size_t size;
} ramfs_file_t;

/* user handles */
typedef struct ramfs_fs_t {
    ramfs_dir_t root;
} ramfs_fs_t;

typedef struct ramfs_dh_t {
    ramfs_fs_t *fs;
    ramfs_dir_t *dir;
    size_t loc;
} ramfs_dh_t;

typedef struct ramfs_fh_t {
    ramfs_fs_t *fs;
    ramfs_file_t *file;
    int flags;
    size_t pos;
} ramfs_fh_t;

#include "frogfs/ramfs.h"


static ssize_t find_entry(ramfs_entry_t *dir, const char *name)
{
    int first = 0;
    int last = ((ramfs_dir_t *) dir)->children_len - 1;

    while (first <= last) {
        int middle = (first + last) / 2;
        int cmp = strcmp(((ramfs_dir_t *) dir)->children[middle]->name, name);
        if (cmp == 0) {
            return middle;
        } else if (cmp < 0) {
            first = middle + 1;
        } else {
            last = middle - 1;
        }
    }

    errno = ENOENT;
    return -(last + 2);
}

static int insert(ramfs_entry_t *parent, ramfs_entry_t *child, int i)
{
    if (!ramfs_is_dir(parent)) {
        errno = ENOTDIR;
        return -1;
    }
    ramfs_dir_t *dir = (ramfs_dir_t *) parent;

    size_t new_size = sizeof(*dir->children) * (dir->children_len + 1);
    ramfs_entry_t **new_children = realloc(dir->children, new_size);
    if (new_children == NULL) {
        return -1;
    }
    dir->children = new_children;

    memmove(&dir->children[i + 1], &dir->children[i],
            sizeof(*dir->children) * (dir->children_len - i));

    dir->children[i] = child;
    dir->children_len++;
    child->fs = parent->fs;
    child->parent = dir;
    return 0;
}

static ramfs_entry_t *remove_index(ramfs_entry_t *parent, size_t i)
{
    if (!ramfs_is_dir(parent)) {
        errno = ENOTDIR;
        return NULL;
    }
    ramfs_dir_t *dir = (ramfs_dir_t *) parent;

    if (i >= dir->children_len) {
        errno = ERANGE;
        return NULL;
    }
    ramfs_entry_t *child = dir->children[i];

    memmove(&dir->children[i], &dir->children[i + 1],
            sizeof(*dir->children) * (dir->children_len - i - 1));
    size_t new_size = sizeof(*dir->children) * (dir->children_len - 1);
    ramfs_entry_t **new_children = realloc(dir->children, new_size);
    if (new_size != 0 && new_children == NULL) {
        return NULL;
    }

    dir->children = new_children;
    dir->children_len--;
    child->parent = NULL;
    return child;
}

static ramfs_entry_t *remove(ramfs_entry_t *entry)
{
    ssize_t i = find_entry(&entry->parent->entry, entry->name);
    if (i < 0) {
        return NULL;
    }

    return remove_index(entry, i);
}

ramfs_fs_t *ramfs_init(void)
{
    ramfs_fs_t *fs = calloc(1, sizeof(*fs));

    if (fs == NULL) {
        return NULL;
    }

    fs->root.entry.fs = fs;
    return fs;
}

void ramfs_deinit(ramfs_fs_t *fs)
{
    assert(fs != NULL);

    ramfs_rmtree(&fs->root.entry);
    free(fs);
}

ramfs_entry_t *ramfs_get_parent(ramfs_fs_t *fs, const char *path)
{
    ramfs_dir_t *dir = &fs->root;

    while (*path == '/') {
        path++;
    }

    const char *end;
    while ((end = strchr(path, '/')) != NULL) {
        char *key = strndup(path, end - path);
        if (key == NULL) {
            return NULL;
        }
        ssize_t i = find_entry(&dir->entry, key);
        free(key);
        if (i < 0) {
            return NULL;
        }
        if (!ramfs_is_dir(dir->children[i])) {
            errno = ENOTDIR;
            return NULL;
        }
        dir = (ramfs_dir_t *) dir->children[i];
        path = end + 1;
        while (*path == '/') {
            path++;
        }
    }

    return (ramfs_entry_t *) dir;
}

ramfs_entry_t *ramfs_get_entry(ramfs_fs_t *fs, const char *path)
{
    assert(fs != NULL);
    assert(path != NULL);

    while (*path == '/') {
        path++;
    }

    ramfs_dir_t *parent = (ramfs_dir_t *) ramfs_get_parent(fs, path);
    if (parent == NULL) {
        return NULL;
    }

    size_t len = strlen(path);
    const char *key = path + len;
    while (key > path && *(key - 1) != '/') {
        key--;
    }

    if (len - (key - path) == 0) {
        errno = EINVAL;
        return NULL;
    }

    ssize_t i = find_entry(&parent->entry, key);
    if (i < 0) {
        return NULL;
    }

    return parent->children[i];
}

const char *ramfs_get_name(const ramfs_entry_t *entry)
{
    return entry->name;
}

char *ramfs_get_path(const ramfs_entry_t *entry)
{
    assert(entry != NULL);

    size_t len = 0;
    const ramfs_entry_t *node = entry;

    while (node != NULL) {
        len += strlen((char *) node->name) + 1;
        node = &node->parent->entry;
    }

    char *path = malloc(len + 1);
    path[len] = '\0';

    node = entry;
    while (node != NULL) {
        int name_len = strlen((char *) node->name);
        len -= name_len;
        memcpy(path + len, (char *) node->name, name_len);
        path[--len] = '/';
        node = &node->parent->entry;
    }

    return path;
}

int ramfs_is_dir(const ramfs_entry_t *entry)
{
    return entry->type == RAMFS_ENTRY_TYPE_DIR;
}

int ramfs_is_file(const ramfs_entry_t *entry)
{
    return entry->type == RAMFS_ENTRY_TYPE_FILE;
}

void ramfs_stat(const ramfs_entry_t *entry, ramfs_stat_t *st)
{
    assert(entry != NULL);

    memset(st, 0, sizeof(*st));
    st->type = entry->type;
    if (entry->type == RAMFS_ENTRY_TYPE_FILE) {
        ramfs_file_t *file = (ramfs_file_t *) entry;
        st->size = file->size;
    }
}

ramfs_entry_t *ramfs_create(ramfs_fs_t *fs, const char *path, int flags)
{
    ramfs_file_t *file;

    while (*path == '/') {
        path++;
    }

    ramfs_dir_t *parent = (ramfs_dir_t *) ramfs_get_parent(fs, path);
    if (parent == NULL) {
        return NULL;
    }

    size_t len = strlen(path);
    const char *name = path + len;
    while (name > path && *(name - 1) != '/') {
        name--;
    }
    if (len - (path - name) == 0) {
        errno = EINVAL;
        return NULL;
    }

    ssize_t i = find_entry(&parent->entry, name);
    if (i >= 0) {
        errno = EEXIST;
        return NULL;
    }
    i = -i - 1;

    if (strlen(name) == 0 || strchr(name, '/')) {
        errno = EINVAL;
        return NULL;
    }

    file = calloc(1, sizeof(*file));
    if (file == NULL) {
        return NULL;
    }

    file->entry.name = strdup(name);
    if (file->entry.name == NULL) {
        free(file);
        return NULL;
    }
    file->entry.fs = fs;
    file->entry.parent = parent;
    file->entry.type = RAMFS_ENTRY_TYPE_FILE;

    if (insert(&parent->entry, &file->entry, i) < 0) {
        free((void *) file->entry.name);
        free(file);
        return NULL;
    }

    return &file->entry;
}

ramfs_fh_t *ramfs_open(ramfs_fs_t *fs, ramfs_entry_t *entry,
        unsigned int flags)
{
    assert(entry != NULL);

    if (entry->type != RAMFS_ENTRY_TYPE_FILE) {
        return NULL;
    }

    ramfs_file_t *file = (ramfs_file_t *) entry;

    if (flags & O_TRUNC) {
        free(file->data);
        file->data = NULL;
        file->size = 0;
    }

    ramfs_fh_t *fh = calloc(1, sizeof(*fh));
    if (fh == NULL) {
        return NULL;
    }

    if (flags & O_APPEND) {
        fh->pos = file->size;
    }

    fh->fs = fs;
    fh->file = file;
    fh->flags = flags;
    return fh;
}

void ramfs_close(ramfs_fh_t *fh)
{
    free(fh);
}

ssize_t ramfs_read(ramfs_fh_t *fh, char *buf, size_t len)
{
    assert(fh != NULL);
    assert(buf != NULL);

    if (fh->pos >= fh->file->size) {
        return 0;
    }

    if (len > fh->file->size - fh->pos) {
        len = fh->file->size - fh->pos;
    }

    memcpy(buf, fh->file->data + fh->pos, len);
    fh->pos += len;
    return len;
}

ssize_t ramfs_write(ramfs_fh_t *fh, const char *buf, size_t len)
{
    assert(fh != NULL);
    assert(buf != NULL);

    if (!(fh->flags & O_WRONLY || fh->flags & O_RDWR)) {
        errno = EBADF;
        return -1;
    }

    if (fh->pos + len > fh->file->size) {
        size_t new_size = fh->pos + len;
        uint8_t *p = realloc(fh->file->data, new_size);
        if (new_size != 0 && p == NULL) {
            return -1;
        }
        fh->file->data = p;
        if (fh->pos > fh->file->size) {
            memset(fh->file->data + fh->file->size, 0,
                    fh->pos - fh->file->size);
        }
        fh->file->size = fh->pos + len;
    }

    memcpy(fh->file->data + fh->pos, buf, len);
    fh->pos += len;
    return len;
}

ssize_t ramfs_seek(ramfs_fh_t *fh, off_t offset, int whence)
{
    assert(fh != NULL);

    ssize_t pos = fh->pos;

    if (whence == SEEK_CUR) {
        pos += offset;
    } else if (whence == SEEK_SET) {
        pos = offset;
    } else if (whence == SEEK_END) {
        pos = fh->file->size + offset;
    }

    if (pos < 0) {
        pos = 0;
    }

    fh->pos = pos;
    return pos;
}

size_t ramfs_tell(const ramfs_fh_t *fh)
{
    assert(fh != NULL);

    return fh->pos;
}

size_t ramfs_access(const ramfs_fh_t *fh, const void **buf)
{
    assert(fh != NULL);

    *buf = fh->file->data;
    return fh->file->size;
}

int ramfs_unlink(ramfs_entry_t *entry)
{
    assert(entry != NULL);

    if (entry->type != RAMFS_ENTRY_TYPE_FILE) {
        errno = ENFILE;
        return -1;
    }

    if (remove(entry) == NULL) {
        return -1;
    }

    free((void *) entry->name);
    free(entry);
    return 0;
}

int ramfs_rename(ramfs_fs_t *fs, const char *src, const char *dst)
{
    assert(src != NULL);
    assert(dst != NULL);

    if (strcmp(src, dst) == 0) {
        return 0;
    }

    while (*src == '/') {
        src++;
    }
    ramfs_dir_t *src_parent = (ramfs_dir_t *) ramfs_get_parent(fs, src);
    if (src_parent == NULL) {
        errno = ENOENT;
        return -1;
    }

    while (*dst == '/') {
        dst++;
    }
    ramfs_dir_t *dst_parent = (ramfs_dir_t *) ramfs_get_parent(fs, dst);
    if (dst_parent == NULL) {
        errno = ENOENT;
        return -1;
    }

    size_t len = strlen(src);
    const char *name = src + len;
    while (name > src && *(name - 1) != '/') {
        name--;
    }
    ssize_t src_index = find_entry(&src_parent->entry, name);
    if (src_index < 0) {
        return -1;
    }

    len = strlen(dst);
    name = dst + len;
    while (name > dst && *(name - 1) != '/') {
        name--;
    }
    ssize_t dst_index = find_entry(&dst_parent->entry, name);
    if (dst_index >= 0) {
        errno = EEXIST;
        return -1;
    }

    name = strdup(name);
    if (name == NULL) {
        return -1;
    }

    ramfs_entry_t *entry = remove(src_parent->children[src_index]);
    if (entry == NULL) {
        return -1;
    }
    dst_index = find_entry(&dst_parent->entry, name);
    dst_index = -dst_index - 1;
    free((void *) entry->name);
    entry->name = name;
    if (insert(&dst_parent->entry, entry, dst_index) < 0) {
        return -1;
    }

    return 0;
}

ramfs_dh_t *ramfs_opendir(ramfs_fs_t *fs, const ramfs_entry_t *entry)
{
    assert(entry != NULL);

    if (ramfs_is_file(entry)) {
        errno = ENOTDIR;
        return NULL;
    }

    ramfs_dh_t *dh = calloc(1, sizeof(*dh));
    dh->fs = fs;
    dh->dir = (ramfs_dir_t *) entry;
    return dh;
}

void ramfs_closedir(ramfs_dh_t *dh)
{
    assert(dh != NULL);

    free(dh);
}

const ramfs_entry_t *ramfs_readdir(ramfs_dh_t *dh)
{
    assert(dh != NULL);

    if (dh->loc < dh->dir->children_len) {
        return dh->dir->children[dh->loc++];
    }

    return NULL;
}

void ramfs_rewinddir(ramfs_dh_t *dh)
{
    assert(dh != NULL);

    dh->loc = 0;
}

void ramfs_seekdir(ramfs_dh_t *dh, size_t loc)
{
    assert(dh != NULL);

    if (loc < dh->dir->children_len) {
        dh->loc = loc;
    } else {
        dh->loc = dh->dir->children_len;
    }
}

size_t ramfs_telldir(ramfs_dh_t *dh)
{
    assert(dh != NULL);

    return dh->loc;
}

ramfs_entry_t *ramfs_mkdir(ramfs_fs_t *fs, const char *path)
{
    assert(fs != NULL);
    assert(path != NULL);

    while (*path == '/') {
        path++;
    }

    ramfs_dir_t *parent = (ramfs_dir_t *) ramfs_get_parent(fs, path);
    if (parent == NULL) {
        return NULL;
    }

    size_t len = strlen(path);
    const char *name = path + len;
    while (name > path && *(name - 1) != '/') {
        name--;
    }
    if (len - (path - name) == 0) {
        name--;
        errno = EINVAL;
        return NULL;
    }

    ssize_t i = find_entry(&parent->entry, name);
    if (i >= 0) {
        errno = EEXIST;
        return NULL;
    }
    i = -i - 1;

    if (strlen(name) == 0 || strchr(name, '/')) {
        errno = EINVAL;
        return NULL;
    }

    ramfs_dir_t *dir = calloc(1, sizeof(*dir));
    if (dir == NULL) {
        return NULL;
    }

    dir->entry.name = strdup(name);
    if (dir->entry.name == NULL) {
        free(dir);
        return NULL;
    }
    dir->entry.fs = fs;
    dir->entry.parent = parent;
    dir->entry.type = RAMFS_ENTRY_TYPE_DIR;

    if (insert(&parent->entry, &dir->entry, i) < 0) {
        free((void *) dir->entry.name);
        free(dir);
        return NULL;
    }

    return &dir->entry;
}

int ramfs_rmdir(ramfs_entry_t *entry)
{
    assert(entry != NULL);

    if (!ramfs_is_dir(entry)) {
        errno = ENOTDIR;
        return -1;
    }

    if (((ramfs_dir_t *) entry)->children_len > 0) {
        errno = ENOTEMPTY;
        return -1;
    }

    if (remove(entry) == NULL) {
        return -1;
    }

    free((void *) entry->name);
    free(entry);
    return 0;
}

void ramfs_rmtree(ramfs_entry_t *entry)
{
    assert(entry != NULL);

    if (ramfs_is_file(entry)) {
        ramfs_unlink(entry);
        return;
    }

    ramfs_dir_t *dir = (ramfs_dir_t *) entry;

    for (int i = 0; i < dir->children_len; i++) {
        if (ramfs_is_dir(dir->children[i])) {
            ramfs_rmtree(dir->children[i]);
        } else {
            free(((ramfs_file_t *) dir->children[i])->data);
            free((void *) dir->children[i]->name);
            free(dir->children[i]);
        }
    }
    free((void *) entry->name);
    free(entry);
}
