# About libespfs

Libespfs is a read-only filesystem component designed for
[ESP-IDF](https://github.com/espressif/esp-idf) and
[ESP8266_RTOS_SDK](https://github.com/espressif/ESP8266_RTOS_SDK) that uses a
sorted hash table to locate file and directory entries. It works with a
monolithic binary that is generated using the mkespfsimage.py tool. It
currently supports [heatshrink](https://github.com/atomicobject/heatshrink)
for compression. It was originally written to use with Sprite_tm's esphttpd,
but has been separated to be used for other uses.

Libespfs can be used in other projects though, and works fine on Linux. There
is a test Linux program in `tools/test` to read files from an espfs image.


# Getting started

To use this component, make a components directory in your project's root
directory and within that directory run:

`git clone --recursive https://github.com/jkent/libespfs`


You can generate a filesystem using `tools/mkespfsiage.py`. The tool takes two
arguments, ROOT, the directory containing the files to generate from, and
IMAGE, the output file for the image. The script references an espfs.yaml file
in the image ROOT, with the default settings to not add it to the image. The
yaml file the various preprocessors and compressors to run while building the
image. Example:

```yaml
paths:
    '*.html': ['html-minifier', 'gzip']
    '*': heatshrink
```

You can add your own preprocessors as well. Look at the espfs_default.yaml
within the component as an example.


## Building an espfs image

An example for ESP-IDF, in your project's toplevel CMakeLists.txt:

```cmake
cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(myproject)

include(components/libespfs/cmake/include.cmake)
target_add_espfs(myproject.elf espfs html)
```

Where **espfs** is the prefix to the symbol name **espfs_bin** in your
resulting binary and **html** is the path relative to the project root
directory. In C you get two symbols:

```C
extern const uint8_t espfs_bin[];
extern const size_t espfs_bin_len;
```

You can also define your own target to build just the binary:

```cmake
define_target_espfs(my_espfs_target html espfs.bin)
```

In this case, **html** is the directory to build from, and **espfs.bin** is
the output path for the generated image.


# Usage

There are two different ways you can use libespfs. There is the low level
interface and there is a vfs interface for IDF. The vfs interface is the
normal way to use libespfs. But you can use both at the same time if you wish.


## Common initialization

Example initialization:

```C
extern const uint8_t espfs_bin[];

espfs_config_t espfs_config = {
    .addr = espfs_bin,
};
espfs_fs_t *fs = espfs_init(&espfs_config);
assert(fs != NULL);
```

```C
espfs_deinit(fs);
```

You can also mount a filesystem from a flash partition. Instead of specifying
**addr**, you'd specify a **part_label** string.


## VFS interface

```C
esp_vfs_espfs_conf_t vfs_espfs_conf = {
    .base_path = "/espfs",
    .fs = fs,
    .max_files = 5,
};
esp_vfs_espfs_register(&vfs_espfs_conf);
```

You can then use the system (open, read, close) or file stream (fopen, fread,
fclose) functions to access files.


## Raw interface

```C
bool espfs_stat(espfs_fs_t *fs, const char *path, espfs_stat_t *s);
espfs_file_t *espfs_fopen(espfs_fs_t *fs, const char *path);
void espfs_fclose(espfs_file_t *f);
void espfs_fstat(espfs_file_t *f, const char *path, espfs_stat_t *s);
ssize_t espfs_fread(espfs_file_t *f, void *buf, size_t len);
ssize_t espfs_fseek(espfs_file_t *f, long offset, int mode);
size_t espfs_ftell(espfs_file_t *f);
ssize_t espfs_faccess(espfs_file_t *f, void **buf);
```
