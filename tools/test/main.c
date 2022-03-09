/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "frogfs/espfs.h"

int main(int argc, char *argv[])
{
    if (argc != 3) {
        fprintf(stderr, "Usage: %s IMAGE PATH\n", argv[0]);
        return EXIT_FAILURE;
    }

    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        fputs("open failed\n", stderr);
        return EXIT_FAILURE;
    }

    struct stat sb;
    if (fstat(fd, &sb) == -1) {
        fputs("fstat failed\n", stderr);
    }

    void *mmap_addr = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mmap_addr == MAP_FAILED) {
        fputs("mmap failed\n", stderr);
        close(fd);
        return EXIT_FAILURE;
    }

    espfs_config_t config = {
        .addr = mmap_addr,
    };

    espfs_fs_t *fs = espfs_init(&config);
    if (fs == NULL) {
        fputs("espfs_init failed\n", stderr);
        munmap(mmap_addr, sb.st_size);
        close(fd);
        return EXIT_FAILURE;

    }

    espfs_stat_t stat;
    if (espfs_stat(fs, argv[2], &stat)) {
        if (stat.type == ESPFS_TYPE_FILE) {
            fprintf(stderr, "Object '%s' is a file.\n", argv[2]);
            if (stat.compression == ESPFS_COMPRESSION_HEATSHRINK) {
                fprintf(stderr, "File is compressed with heatsrhink.\n");
            } else if (stat.compression != ESPFS_COMPRESSION_NONE) {
                fprintf(stderr, "File is compressed wth unknown.\n");
            }
            if (stat.flags & ESPFS_FLAG_GZIP) {
                fprintf(stderr, "File is gzip encapsulated.\n");
            }
            fprintf(stderr, "File is %d bytes.\n", stat.size);
            espfs_file_t *f = espfs_fopen(fs, argv[2]);
            if (f == NULL) {
                fprintf(stderr, "Error opening file.\n");
            } else {
                char buf[16];
                int bytes;
                fputs("File contents:\n", stderr);
                while ((bytes = espfs_fread(f, buf, sizeof(buf))) ==
                        sizeof(buf)) {
                    fwrite(buf, bytes, 1, stdout);
                }
                fflush(stdout);
                espfs_fclose(f);
            }
        } else if (stat.type == ESPFS_TYPE_DIR) {
            fprintf(stderr, "Object '%s' is a directory.\n", argv[2]);
        } else {
            fprintf(stderr, "Object '%s' is an unknown type.\n", argv[2]);
        }
    } else {
        fprintf(stderr, "Object '%s' does not exist.\n", argv[2]);
    }

    espfs_deinit(fs);
    munmap(mmap_addr, sb.st_size);
    close(fd);
    return EXIT_SUCCESS;
}
