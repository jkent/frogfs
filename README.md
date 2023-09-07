# About FrogFS

FrogFS (Fast Read-Only General-purpose File System) is a read-only filesystem
designed for embedded use. It can be easily used with a CMake project &mdash;
including [ESP-IDF](https://github.com/espressif/esp-idf). It has built-in
filters to save space; preprocessed HTML (`examples/files/test.html`) is
reduced by 64.5% with the applied transforms and filters.

Transforms include:
  * babel-convert
  * babel-minify
  * html-minifier
  * terminate
  * uglify-js
  * uglifycss

Compression options include:
  * none
  * [zlib deflate](https://www.zlib.net/) (best compression)
  * [heatshrink](https://github.com/atomicobject/heatshrink) (best speed)

For an HTTP server, deflate compressed files can even be passed through
untouched! This saves both processing time and bandwidth.

With the ESP-IDF [VFS Interface](#vfs-interface), you can use FrogFS as a
base file system &mdash; you can overlay a different file system such as FAT
or SPIFFS to create a hybrid file system. To explain better, when opening a
file for reading it will search the overlay file system, and then fall back
to FrogFS. The overlay is an optional feature of course; and IDF's VFS lets
you mount filesystems to any prefix path of your choosing.

Included is a standalone binary with an embedded filesystem to test and verify
functionality and an ESP-IDF example using Espressif's http server, with the
filesystem mounted using VFS.

# Getting started with ESP-IDF

To use this component with ESP-IDF, within your projects directory run

    idf.py add-dependency jkent/frogfs

## Embedding a FrogFS image

Embed FrogFS within your project binary with the folowing CMake function:

    target_add_frogfs(<target> <path> [NAME name] [CONFIG yaml])

If **NAME** is not specified, it will default to the basename of **path**. If
**CONFIG** is not specified, `default_config.yaml` will be used.

As an example for ESP-IDF, in your project's toplevel CMakeLists.txt:

```cmake
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(my_project)
target_add_frogfs(my_project.elf files NAME frogfs CONFIG frogfs.yaml)
```

Where **target** `my_project.elf` must match your project's name and
**path** `files` is either an absolute path or a path relative to your
project's root. It should contain the files to embed in the resulting frogfs
binary. In C, this results in these two global symbols being available to your
application:

```C
extern const uint8_t frogfs_bin[];
extern const size_t frogfs_bin_len;
```

## Making a FrogFS binary and flashing it

You have the option of creating a binary without linking it with your
application. A CMake function is provided to output a binary with target
`generate_${name}`.

    declare_frogfs_bin(path [NAME name] [CONFIG yaml])

If **NAME** is not specifed the default is the basename of **path**.

Here's an example of what you can add to your toplevel CMakeLists.txt, with a
directory named files:

```cmake
declare_frogfs_bin(files NAME frogfs)

idf_component_get_property(main_args esptool_py FLASH_ARGS)
idf_component_get_property(sub_args esptool_py FLASH_SUB_ARGS)
esptool_py_flash_target(frogfs-flash "${main_args}" "${sub_args}" ALWAYS_PLAINTEXT)
esptool_py_flash_to_partition(frogfs-flash storage ${BUILD_DIR}/CMakeFiles/frogfs.dir/frogfs.bin)
add_dependencies(frogfs-flash generate_${name}_bin)
```

In this case, **files** is the source directory to build the file system from, **frogfs** is the target prefix and binary filename (without the .bin) and
**storage** is the name of the partition where the binary is flashed. You can
invoke the flash process by running `idf.py frogfs-flash`.

## Configuration

In the root of `frogfs` there is a `default_config.yaml` file that has sane
defaults for HTTP usage. The yaml file is a bunch of filters that are applied
top down to apply various actions. First, all transforms are applied, then a
compressor is applied as a last optional step. Transforms and compressors can
take optional command line arguments. Included transforms and compressors are
found in the `frogs/tools` directory, and you can optionally supplement
and/or override existing transforms and compressors.

Once a transform or compression option is enabled or disabled, it cannot be
overriden later. Transforms are applied in descending order. You can prefix a
transforms or the `compress` keyword with `no` to disable it. There are a
couple of special keywords; `discard` which prevents inclusion and `no cache`,
which causes preprocessing to run every time on matching objects.

## Usage

Two interfaces are available: the [bare API](#bare-api) or when using IDF
there is the [VFS interface](#vfs-interface) which builds on top of the bare
API. You should use the VFS interface in IDF projects, as it uses the portable
and familiar `stdio` C functions with it. There is nothing preventing you from
using both at the same time, however.

### Shared initialization

Configuration requries defining a `frogfs_config_t` structure and passing it
to `frogfs_init`. Two different ways to specify the filesystem:

  1. a memory address using the `addr` variable:

```C
frogfs_config_t frogfs_config = {
    .addr = frogfs_bin,
};
```

 2. a partition name using the `part_label` string:

```C
frogfs_config_t frogfs_config = {
    .part_label = "storage",
};
```

Then it is just a matter of passing the `frogfs_config` to `frogfs_init`
function and checking its return variable:

```C
frogfs_fs_t *fs = frogfs_init(&frogfs_config);
assert(fs != NULL);
```

When done, and all file handles are closed, you can call `frogfs_deinit`:

```C
frogfs_deinit(fs);
```

### VFS interface

The VFS interface has a similar method of initialization; you define a
`frogfs_vfs_conf_t` structure:

  * **base_path** - path to mount FrogFS
  * **overlay_path** - an optional path to search before FrogFS
  * **fs** - a `frogfs_fs_t` instance
  * **max_files** - max number of files that can be open at a time

```C
frogfs_vfs_conf_t frogfs_vfs_conf = {
    .base_path = "/frogfs",
    .fs = fs,
    .max_files = 5,
};
frogfs_vfs_register(&frogfs_vfs_conf);
```

### Bare API

#### Filesystem functions:

  * frogfs_fs_t *[frogfs_init](https://frogfs.readthedocs.io/en/next/api-reference/frogfs.html#c.frogfs_init)(frogfs_config_t *conf)
  * void [frogfs_deinit](https://frogfs.readthedocs.io/en/next/api-reference/frogfs.html#c.frogfs_deinit)(frogfs_fs_t *fs)

#### Object functions:

  * const frogfs_obj_t *[frogfs_obj_from_path](https://frogfs.readthedocs.io/en/next/api-reference/frogfs.html#c.frogfs_obj_from_path)(const frogfs_fs_t *fs, const char *path)
  * const char *[frogfs_path_from_obj](https://frogfs.readthedocs.io/en/next/api-reference/frogfs.html#c.frogfs_path_from_obj)(const frogfs_obj_t *obj)
  * void [frogfs_stat](https://frogfs.readthedocs.io/en/next/api-reference/frogfs.html#c.frogfs_stat)(const frogfs_fs_t *fs, const frogfs_obj_t *obj, frogfs_stat_t *st)
  * frogfs_f_t *[frogfs_open](https://frogfs.readthedocs.io/en/next/api-reference/frogfs.html#c.frogfs_open)(const frogfs_fs_t *fs, const frogfs_obj_t *obj, unsigned int flags)
  * void [frogfs_close](https://frogfs.readthedocs.io/en/next/api-reference/frogfs.html#c.frogfs_close)(frogfs_f_t *f)
  * size_t [frogfs_read](https://frogfs.readthedocs.io/en/next/api-reference/frogfs.html#c.frogfs_read)(frogfs_f_t *f, void *buf, size_t len)
  * ssize_t [frogfs_seek](https://frogfs.readthedocs.io/en/next/api-reference/frogfs.html#c.frogfs_seek)(frogfs_f_t *f, long offset, int mode)
  * size_t [frogfs_tell](https://frogfs.readthedocs.io/en/next/api-reference/frogfs.html#c.frogfs_tell)(frogfs_f_t *f)
  * size_t [frogfs_access](https://frogfs.readthedocs.io/en/next/api-reference/frogfs.html#c.frogfs_access)(frogfs_f_t *f, void **buf)

#### Directory Functions:

  * frogfs_d_t *[frogfs_opendir](https://frogfs.readthedocs.io/en/next/api-reference/frogfs.html#c.frogfs_opendir)(frogfs_fs_t *fs, const frogfs_obj_t *obj)
  * void [frogfs_closedir](https://frogfs.readthedocs.io/en/next/api-reference/frogfs.html#c.frogfs_closedir)(frogfs_d_t *d)
  * const frogfs_obj_t *[frogfs_readdir](https://frogfs.readthedocs.io/en/next/api-reference/frogfs.html#c.frogfs_readdir)(frogfs_d_t *d)
  * void [frogfs_rewinddir](https://frogfs.readthedocs.io/en/next/api-reference/frogfs.html#c.frogfs_rewinddir)(frogfs_d_t *d)
  * void [frogfs_seekdir](https://frogfs.readthedocs.io/en/next/api-reference/frogfs.html#c.frogfs_seekdir)(frogfs_d_t *d, uint16_t loc)
  * uint16_t [frogfs_telldir](https://frogfs.readthedocs.io/en/next/api-reference/frogfs.html#c.frogfs_telldir)(frogfs_d_t *d)

# How it works

Under the hood there is a hash table consisting of djb2 path hashes which
allows for fast lookups using a binary search algorithm. Directory objects have
a sort table of child objects. Directory support is optional for both the
library and the FrogFS binary. It is not required for both to match for
compatibility, however.

FrogFS binaries can be either embedded in your application, or accessed using
memory mapped I/O. It is not possible (at this time) to use FrogFS without the
file system binary existing in data address space.

Creation of a FrogFS filesystem is a multi-step process handled by a single
tool:

  * `tools/mkfrogfs.py`
    - Recursively enumerate all objects in a given directory
    - Apply transforms to objects as configured in yaml
    - Compress files to files as configured in yaml
    - Save json file containing the state including file timestamps
    - Hash path using djb2 hash
    - Save FrogFS binary: header, hashtable, objects, and footer

You can add your own transforms by creating a `tools` directory in your
projects root directory, with a filename starting with `transform-` and ending
with `.js` or `.py`. Transform tools take data on stdin and produce output on
stdout. Similarly you can add your own compressors by creating a `compress-` 
file.

Both transform and compress tools can accept arguments. Look at
`default_config.yaml` for an examples.

# History and Acknowledgements

FrogFS was split off of Chris Morgan (chmorgan)'s
[libesphttpd](https://github.com/chmorgan/libesphttpd/) project (MPL 2.0),
which is a fork of Jeroen Domburg (Sprite_tm)'s
[libesphttpd](https://github.com/spritetm/libesphttpd/) (BEER-WARE). This project
would never have existed without them.

Thank you to all the contributors to this project.
