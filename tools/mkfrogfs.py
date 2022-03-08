#!/usr/bin/env python

import configparser
import csv
import gzip
import os
import sys
from argparse import ArgumentParser
from struct import Struct
from zlib import crc32

import heatshrink2
from hiyapyco import odyldo
from sortedcontainers import SortedDict


frogfs_fs_header_t = Struct('<IBBHIHH')
# magic, len, version_major, version_minor, binary_len, num_objects, reserved
FROGFS_MAGIC = 0x676F7246
FROGFS_VERSION_MAJOR = 0
FROGFS_VERSION_MINOR = 0

frogfs_hashtable_entry_t = Struct('<II')
# hash, offset

frogfs_sorttable_entry_t = Struct('<I')
# offset

frogfs_object_header_t = Struct('<BBHHH')
# type, len, index, path_len, reserved
FROGFS_TYPE_FILE = 0
FROGFS_TYPE_DIR  = 1

frogfs_file_header_t = Struct('<IIHBB')
# data_len, file_len, flags, compression, reserved
FROGFS_FLAG_GZIP  = (1 << 0)
FROGFS_FLAG_CACHE = (1 << 1)
FROGFS_COMPRESSION_NONE       = 0
FROGFS_COMPRESSION_HEATSHRINK = 1

frogfs_heatshrink_header_t = Struct('<BBH')
# window_sz2, lookahead_sz2

frogfs_crc32_footer_t = Struct('<I')
# crc32

def hash_path(path):
    hash = 5381
    for c in path.encode('utf8'):
        hash = ((hash << 8) + hash + c) & 0xFFFFFFFF
    return hash

def load_state(path):
    state = SortedDict()
    state_file = os.path.join(args.src_dir, '.state')
    with open(state_file, newline='') as f:
        index = 0
        reader = csv.reader(f, quoting=csv.QUOTE_NONNUMERIC)
        for data in reader:
            path, type, _, flags, _, compressor = data
            hash = hash_path(path)
            flags = () if not flags else tuple(flags.split(','))
            if 'discard' in flags:
                continue
            state[(hash, path)] = {
                'index': index,
                'type': type,
                'flags': flags,
                'compressor': compressor,
            }
            index += 1

    return state

def make_dir_object(item):
    print(f'{item[0][0]:08x} {item[0][1]:<34s} dir')

    path = item[0][1].encode('utf8') + b'\0'
    path = path.ljust((len(path) + 3) // 4 * 4, b'\0')
    header = frogfs_object_header_t.pack(FROGFS_TYPE_DIR,
            frogfs_object_header_t.size, item[1]['index'], len(path), 0)
    return header + path

def make_file_object(item, data):
    global config

    flags = 0
    compression = FROGFS_COMPRESSION_NONE
    initial_data_len = len(data)
    inital_data = data

    if 'cache' in item[1]['flags']:
        flags |= FROGFS_FLAG_CACHE

    if item[1]['compressor'] == 'gzip':
        flags |= FROGFS_FLAG_GZIP
        level = int(config['gzip']['level'])
        level = min(max(level, 0), 9)
        data = gzip.compress(data, level)
    elif item[1]['compressor'] == 'heatshrink':
        compression = FROGFS_COMPRESSION_HEATSHRINK
        window_sz2 = int(config['heatshrink']['window_sz2'])
        lookahead_sz2 = int(config['heatshrink']['lookahead_sz2'])
        data = frogfs_heatshrink_header_t.pack(window_sz2, lookahead_sz2, 0) + \
                heatshrink2.compress(data, window_sz2=window_sz2,
                lookahead_sz2=lookahead_sz2)

    data_len = len(data)

    if data_len >= initial_data_len:
        flags &= ~FROGFS_FLAG_GZIP
        compression = FROGFS_COMPRESSION_NONE
        data = inital_data
        data_len = initial_data_len

    if initial_data_len < 1024:
        initial_data_len_str = f'{initial_data_len:d} B'
        data_len_str = f'{data_len:d} B'
    elif initial_data_len < 1024 * 1024:
        initial_data_len_str = f'{initial_data_len / 1024:.1f} KiB'
        data_len_str = f'{data_len / 1024:.1f} KiB'
    else:
        initial_data_len_str = f'{initial_data_len / 1024 / 1024:.1f} MiB'
        data_len_str = f'{data_len / 1024 / 1024:.1f} MiB'

    percent = 100.0
    if initial_data_len > 0:
        percent *= data_len / initial_data_len

    stats = f'{initial_data_len_str:<9s} -> {data_len_str:<9s} ({percent:.1f}%)'
    print(f'{item[0][0]:08x} {item[0][1]:<34s} file {stats}')

    path = item[0][1].encode('utf8') + b'\0'
    path = path.ljust((len(path) + 3) // 4 * 4, b'\0')
    header = frogfs_object_header_t.pack(FROGFS_TYPE_FILE,
            frogfs_object_header_t.size + frogfs_file_header_t.size,
            item[1]['index'], len(path), 0) + frogfs_file_header_t.pack(
            data_len, initial_data_len, flags, compression, 0)

    return header + path + data

def main():
    global args, config

    parser = ArgumentParser()
    parser.add_argument('src_dir', metavar='SRC', help='source directory')
    parser.add_argument('dst_bin', metavar='DST', help='destination binary')
    parser.add_argument('--config', help='user configuration')
    args = parser.parse_args()

    config = configparser.ConfigParser()
    config.read(os.path.join(args.src_dir, '.config'))

    state = load_state(args.src_dir)

    num_objects = len(state)
    offset = frogfs_fs_header_t.size + \
            (frogfs_hashtable_entry_t.size * num_objects) + \
            (frogfs_sorttable_entry_t.size * num_objects)
    hashtable = b''
    sorttable = bytearray(frogfs_sorttable_entry_t.size * num_objects)
    objects = b''

    for item in state.items():
        abspath = os.path.join(args.src_dir, item[0][1])
        if item[1]['type'] == 'dir':
            object = make_dir_object(item)
        elif item[1]['type'] == 'file':
            if not os.path.exists(abspath):
                continue
            with open(abspath, 'rb') as f:
                data = f.read()
            object = make_file_object(item, data)
        else:
            print(f'unknown object type {type}', file=sys.stderr)
            sys.exit(1)
        hashtable += frogfs_hashtable_entry_t.pack(item[0][0], offset)
        frogfs_sorttable_entry_t.pack_into(sorttable,
                frogfs_sorttable_entry_t.size * item[1]['index'], offset)
        objects += object
        offset += len(object)

    binary_len = offset + frogfs_crc32_footer_t.size
    header = frogfs_fs_header_t.pack(FROGFS_MAGIC, frogfs_fs_header_t.size,
            FROGFS_VERSION_MAJOR, FROGFS_VERSION_MINOR, binary_len, num_objects,
            0)
    binary = header + hashtable + sorttable + objects
    binary += frogfs_crc32_footer_t.pack(crc32(binary) & 0xFFFFFFFF)

    with open(args.dst_bin, 'wb') as f:
        f.write(binary)

if __name__ == '__main__':
    main()