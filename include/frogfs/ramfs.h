/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/stat.h>
#include <sys/types.h>

/**
 * \brief A ramfs filesystem handle
 */
typedef struct ramfs_fs_t ramfs_fs_t;

/**
 * \brief Entry type
 */
typedef enum ramfs_entry_type_t {
    RAMFS_ENTRY_TYPE_DIR,
    RAMFS_ENTRY_TYPE_FILE,
} ramfs_entry_type_t;

/**
 * \brief A ramfs filesystem entry
 */
typedef struct ramfs_entry_t ramfs_entry_t;

/**
 * \brief Structure filled by the \a ramfs_stat function
 */
typedef struct ramfs_stat_t {
    ramfs_entry_type_t type; /**< entry type */
    size_t size; /**< file size */
} ramfs_stat_t;

#if defined(__DOXYGEN__) || !defined(RAMFS_PRIVATE_STRUCTS)
/**
 * \brief A ramfs directory handle
 */
typedef struct ramfs_dh_t {
    ramfs_entry_t *entry;
} ramfs_dh_t;

/**
 * \brief A ramfs file handle
 */
typedef struct ramfs_fh_t {
    ramfs_entry_t *entry;
} ramfs_fh_t;
#endif

/**
 * \brief       Initialize filesystem and return pointer
 * \return              \a ramfs_fs_t pointer or \a NULL on error
 */
ramfs_fs_t *ramfs_init(void);

/**
 * \brief       Tear down a filesystem
 * \param[in]   fs      \a ramfs_fs_t pointer
 */
void ramfs_deinit(ramfs_fs_t *fs);

/**
 * \brief       Get parent entry of path
 * \param[in]   fs      \a ramfs_fs_t pointer
 * \param[in]   path    path string
 * \return              \a ramfs_entry_t or \a NULL if parent not found; note
 *                      that the full path does not have to exist
 */
ramfs_entry_t *ramfs_get_parent(ramfs_fs_t *fs, const char *path);

/**
 * \brief       Get ramfs entry for path
 * \param[in]   fs      \a ramfs_fs_t pointer
 * \param[in]   path    path string
 * \return              \a ramfs_entry_t or \a NULL if path was not found
 */
ramfs_entry_t *ramfs_get_entry(ramfs_fs_t *fs, const char *path);

/**
 * \brief       Get path for ramfs entry
 * \param[in]   entry   \a ramfs_entry_t pointer
 * \return              path string or \a NULL if entry is NULL
 */
char *ramfs_get_path(const ramfs_entry_t *entry);

/**
 * \brief       Return entry name component
 * \param[in]   entry   \a ramfs_entry_t pointer
 * \return              the entry name string
 */
const char *ramfs_get_name(const ramfs_entry_t *entry);

/**
 * \brief       Return if entry is a directory
 * \param[in]   entry   \a ramfs_entry_t pointer
 * \return              1 if directory, 0 otherwise
 */
int ramfs_is_dir(const ramfs_entry_t *entry);

/**
 * \brief       Return if entry is a file
 * \param[in]   entry   \a ramfs_entry_t pointer
 * \return              1 if file, 0 otherwise
 */
int ramfs_is_file(const ramfs_entry_t *entry);

/**
 * \brief       Get information about a ramfs entry
 * \param[in]   fs      \a ramfs_fs_t pointer
 * \param[in]   entry   \a ramfs_entry_t pointer
 * \param[out]  st      \a ramfs_stat_t structure
 */
void ramfs_stat(const ramfs_entry_t *entry, ramfs_stat_t *st);

/**
 * \brief       Create an empty file and return a file handle
 * \param[in]   fs      \a ramfs_fs_t pointer
 * \param[in]   path    full path to file
 * \param[in]   flags   flags to pass to \a ramfs_open()
 * \return              created entry or \a NULL on error
 */
ramfs_entry_t *ramfs_create(ramfs_fs_t *fs, const char *path, int flags);

/**
 * \brief       Open a ramfs file object and return a file handle
 * \param[in]   entry   \a ramfs_entry_t pointer
 * \param[in]   flags   open flags
 * \return              opened file handle or \a NULL if entry is NULL
 */
ramfs_fh_t *ramfs_open(ramfs_entry_t *entry, int flags);

/**
 * \brief       Close an open file entry
 * \param[in]   fh      \a ramfs_fh_t pointer
 */
void ramfs_close(ramfs_fh_t *fh);

/**
 * \brief       Read data from an open file
 * \param[in]   fh      \a ramfs_fh_t handle
 * \param[out]  buf     buffer to read into
 * \param[in]   len     maximum number of bytes to read
 * \return              actual number of bytes read, zero if the end of file
 *                      reached, or < 0 on error
 */
ssize_t ramfs_read(ramfs_fh_t *fh, char *buf, size_t nbyte);

/**
 * \brief       Write data to an open file
 * \param[in]   fh      \a ramfs_fh_t handle
 * \param[in]   buf     buffer to write from
 * \param[in]   len     number of bytes to write
 * \return              number of bytes written, or < 0 on error
 */
ssize_t ramfs_write(ramfs_fh_t *fh, const char *buf, size_t nbyte);

/**
 * \brief       Seek to a position within a file
 * \param[in]   fh      \a ramfs_fh_t handle
 * \param[in]   offset  file position (relative or absolute)
 * \param[in]   mode    \a SEEK_SET, SEEK_CUR, or \a SEEK_END
 */
ssize_t ramfs_seek(ramfs_fh_t *fh, off_t offset, int mode);

/**
 * \brief       Get the current file position in a file
 * \param[in]   fh      \a ramfs_fh_t handle
 * \return              current position in file
 */
size_t ramfs_tell(const ramfs_fh_t *fh);

/**
 * \brief       Get raw memory for file
 * \param[in]   fh      \a ramfs_fh_t handle
 * \param[out]  buf     pointer pointer to buf
 * \return              length of raw data
 */
size_t ramfs_access(const ramfs_fh_t *fh, const void **buf);

/**
 * \brief       Free and delete a file on the filesystem
 * \param[in]   entry   \a ramfs_entry_t pointer
 * \return              0 on success, -1 on error
*/
int ramfs_unlink(ramfs_entry_t *entry);

/**
 * \brief       Rename a file
 * \param[in]   fs      \a ramfs_fs_t pointer
 * \param[in]   src     source file path
 * \param[in]   dst     destination file path
 * \return              0 on success, -1 on error
 */
int ramfs_rename(ramfs_fs_t *fs, const char *src, const char *dst);

/**
 * \brief       Open a directory
 * \param[in]   entry   \a ramfs_entry_t pointer
 * \return              directory handle or \a NULL on error
 */
ramfs_dh_t *ramfs_opendir(const ramfs_entry_t *entry);

/**
 * \brief       Close a directory
 * \param[in]   dh      \a ramfs_dh_t directory handle
 */
void ramfs_closedir(ramfs_dh_t *dh);

/**
 * \brief       Read the next entry from an open directory handle
 * \param[in]   dh      \a ramfs_dh_t directory handle
 * \return              \a ramfs_entry_t pointer or NULL on end
 */
const ramfs_entry_t *ramfs_readdir(ramfs_dh_t *dh);

/**
 * \brief       Rewind the directory handle to the first item
 * \param[in]   dh      \a ramfs_dh_t directory handle
 */
void ramfs_rewinddir(ramfs_dh_t *dh);

/**
 * \brief       Seek to a given directory location
 * \param[in]   dh      \a ramfs_dh_t directory handle
 * \param[in]   loc     entry index to seek to
 */
void ramfs_seekdir(ramfs_dh_t *dh, size_t loc);

/**
 * \brief       Return the current entry index
 * \param[in]   dh      \a ramfs_dh_t directory handle
 * \return              current directory index
 */
size_t ramfs_telldir(ramfs_dh_t *dh);

/**
 * \brief       Make a directory
 * \param[in]   fs      \a ramfs_fs_t pointer
 * \param[in]   path    directory path
 * \return              newly created entry handle
 */
ramfs_entry_t *ramfs_mkdir(ramfs_fs_t *fs, const char *name);

/**
 * \brief       Remove a directory. Directory must be empty
 * \param       entry   directory entry handle
 * \return              0 on success, -1 on failure
 */
int ramfs_rmdir(ramfs_entry_t *entry);

/**
 * \brief       Delete and free a directory tree
 * \param       entry   directory entry handle to remove
 */
void ramfs_rmtree(ramfs_entry_t *entry);

#ifdef __cplusplus
} /* extern "C" */
#endif
