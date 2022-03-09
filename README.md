# About FrogFS

FrogFS (Fast Read-Only General-purpose File System) is a read-only filesystem
designed for embedded use, including, but not limited to
[ESP-IDF](https://github.com/espressif/esp-idf) and
[ESP8266_RTOS_SDK](https://github.com/espressif/ESP8266_RTOS_SDK). There is
also [a cross-platform example](https://github.com/jkent/esphttpd-example)
available. It originally was written to be used with libesphttpd and Clockwise
HTTPd, but has been separated to allow other uses. There is a simple test
Linux program in `tools/test` directory to read files from a FrogFS image.


## How it works

Under the hood, it uses a sorted hash table and does a binary search to locate
file and directory entries. The FrogFS binary can be embedded in your
application or it can be accessed via memory mapped I/O such as the SPI flash
memory space on the ESP8266 or ESP32. A set of python tools are provided to
compress and/or obfusicate the data stored, and then create a final FrogFS
image binary. Current compression methods include heatshrink and gzip -- the
later without decompression support.


## Getting Started

To use this library with IDF, make a `components` directory in your project's
root directory and within that directory run:

```sh
git clone --recursive https://github.com/jkent/frogfs
```

Using the CMake build system, it is straightforward to generate or embed espfs
images in your binary.


### Embedding a FrogFS image

```cmake
target_add_frogfs(target path [NAME name] [CONFIG yaml])
```

Symbols will be named **name** or if not specified, the last path part of
**path**.

As an example for ESP-IDF, in your project's toplevel CMakeLists.txt:

```cmake
cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(my_project)

include(components/frogfs/cmake/functions.cmake)
target_add_frogfs(my_project.elf html NAME frogfs CONFIG frogfs.yaml)
```

Where **html** is the path and name of the directory containing the root to
use, **frogfs** is the name of the symbol to use, and **frogfs.yaml** is the
configuration overrides. In C this results in these two symbols being defined:

```C
extern const uint8_t frogfs_bin[];
extern const size_t frogfs_bin_len;
```

### Making a FrogFS binary and flashing it

There is also the option to create a binary without linking it with your
application. A CMake function is provided to to output a binary with a new
target `generate_${name}`. If **name** is not specified, the last path part of
**path** is used:

```cmake
declare_frogfs_bin(path [NAME name] [CONFIG yaml])
```

Again, for ESP-IDF, in your project main's CMakeLists.txt:

```cmake
set(name frogfs)
declare_frogfs_bin(../html NAME ${name} CONFIG ../frogfs.yaml)
idf_component_get_property(main_args esptool_py FLASH_ARGS)
idf_component_get_property(sub_args esptool_py FLASH_SUB_ARGS)
esptool_py_flash_target(${name}-flash "${main_args}" "${sub_args}" ALWAYS_PLAINTEXT)
esptool_py_flash_to_partition(${name}-flash frogfs ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/${COMPONENT_LIB}.dir/frogfs_${name}.bin)
add_dependencies(${name}-flash generate_${name})
```

In this case, **../html** is the directory to build from, **${name}** is the
name of the binary name (without the .bin) and **frogfs.bin** and
**frogfs.yaml** is the configuration overrides.


### Configuration

In the root of `frogfs` There is a `frogfs_defaults.yaml` file that has sane
defaults for HTTP usage. The yaml file defines filters for the various actions
-- preprocessors and compressors -- to run before building the FrogFS image.
The **config** file settings can enable or disable rules with wildcard matches
or absolute matches. For example:

```yaml
filters:
    "index.html": no-html-minifier
    "*.html": gzip
    "*.txt": [zeroify, uncompressed]
```

All preprocessors can be prefixed with `no-` to skip the preprocessor for that
glob pattern. There are currently only two compressors, `gzip` and
`heatshrink`. Gzip is somewhat special in that FrogFS does not know how to
decompress it. Its expected to be decompressed by a client browser. Basically,
don't store your templates gzip compressed. There are also a few other special
actions. They are `skip-preprocessing`, `uncompressed`, `cache`, `discard` and
`zeroify`. Skip-preprocessing does what it sounds like, no preprocessors will
run if it is specified. Uncompressed disables any compression option, cache is
just a flag that is used to tell the browser that it should cache the file
instead of re-downloading it the next visit. Discard means leave the specified
files out of the FrogFS image. And finally zeroify null terminates data.

You can define your own preprocessors. Look at the config_default.yaml within
frogfs for an example. Preprocessors must take data on stdin and produce data
on stdout. Installation of preprocessors can be done with the **install**
key or using the **npm** key for node.js.


### Usage

When using ESP_IDF, there are two ways you can use FrogFS. There is a low
level interface and there is a VFS interface. The VFS interface is the
recommended way to use FrogFS, since it allows you to use `stdio` operations
on it. There is nothing preventing you from using both at the same time,
however.


#### Shared initialization

```C
extern const uint8_t frogfs_bin[];

frogfs_config_t frogfs_config = {
    .addr = frogfs_bin,
};
frogfs_fs_t *fs = frogfs_init(&frogfs_config);
assert(fs != NULL);
```

```C
frogfs_deinit(fs);
```

You can also mount a filesystem from a flash partition. Instead of specifying
**addr**, you would specify a **part_label** string.


#### VFS interface

```C
esp_vfs_frogfs_conf_t vfs_frogfs_conf = {
    .base_path = "/frogfs",
    .fs = fs,
    .max_files = 5,
};
esp_vfs_frogfs_register(&vfs_frogfs_conf);
```

You can now use the system (open, read, close) or file stream (fopen, fread,
fclose) functions to access files, with the FrogFS filesystem mounted to
**/frogfs**.


#### Raw interface

```C
const char *frogfs_get_path(frogfs_fs_t *fs, uint16_t index);
bool frogfs_stat(frogfs_fs_t *fs, const char *path, frogfs_stat_t *s);
frogfs_file_t *frogfs_fopen(frogfs_fs_t *fs, const char *path);
void frogfs_fclose(frogfs_file_t *f);
void frogfs_fstat(frogfs_file_t *f, const char *path, frogfs_stat_t *s);
ssize_t frogfs_fread(frogfs_file_t *f, void *buf, size_t len);
ssize_t frogfs_fseek(frogfs_file_t *f, long offset, int mode);
size_t frogfs_ftell(frogfs_file_t *f);
ssize_t frogfs_faccess(frogfs_file_t *f, void **buf);
```
