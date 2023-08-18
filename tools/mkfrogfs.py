#!/usr/bin/env python

import json
import os
from argparse import ArgumentParser
from struct import Struct
from subprocess import PIPE, Popen
from sys import executable, stderr
from zlib import crc32

frogfs_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
frogfs_tools_dir = os.path.join(frogfs_dir, 'tools')

os.environ['FROGFS_DIR'] = frogfs_dir
os.environ['NODE_PREFIX'] = os.environ['BUILD_DIR']
os.environ['NODE_PATH'] = os.path.join(os.environ['BUILD_DIR'], 'node_modules')

frogfs_head_t = Struct('<IBBHIHB')
# magic, len, ver_major, ver_minor, bin_len, num_objs, align
FROGFS_MAGIC        = 0x474F5246 # FROG
FROGFS_VER_MAJOR    = 1
FROGFS_VER_MINOR    = 0

frogfs_hash_t = Struct('<II')
# hash, offset

frogfs_sort_t = Struct('<I')
# offset

frogfs_obj_t = Struct('<BBHH')
# len, type, index, path_len
FROGFS_TYPE_FILE    = 0
FROGFS_TYPE_DIR     = 1

frogfs_file_t = Struct('<BBHHBBI')
# len, type, index, path_len, compression, reserved, data_len
FROGFS_FILE_COMP_NONE       = 0
FROGFS_FILE_COMP_DEFLATE    = 1
FROGFS_FILE_COMP_HEATSHRINK = 2

frogfs_file_comp_t = Struct('<BBHHBBII')
# len, type, index, path_len, compression, options, data_len, uncompressed_len

frogfs_fs_foot_t = Struct('<I')
# crc32

def load_preprocessors():
    global pp_dict

    pp_dict = {}
    for path in {frogfs_tools_dir, 'tools'}:
        if not os.path.exists(path):
            continue
        for file in sorted(os.listdir(path)):
            if file.startswith('compress-'):
                filepath = os.path.join(path, file)
                name, _ = os.path.splitext(file.removeprefix('compress-'))
                pp_dict[name] = {'file': filepath, 'type': 'compressor'}
            elif file.startswith('transform-'):
                filepath = os.path.join(path, file)
                name, _ = os.path.splitext(file.removeprefix('transform-'))
                pp_dict[name] = {'file': filepath, 'type': 'transformer'}

    return pp_dict

def load_state():
    global state

    state = {}
    if os.path.exists(root + '.json'):
        with open(root + '.json', 'r') as f:
            state = json.load(f)

    state = {path: entry
             for (path, entry)
             in state.items()
             if not entry['rules'].get('discard', False)}

    if skip_directories:
        state = {path: entry
                 for (path, entry)
                 in state.items()
                 if not os.path.isdir(os.path.join(root, path))}

def djb2_hash(s):
    hash = 5381
    for c in s.encode('utf-8'):
        hash = ((hash << 5) + hash ^ c) & 0xFFFFFFFF
    return hash

def make_dir_obj(path, hash, index):
    encoded_path = path.encode('utf-8') + b'\0'
    path_len = len(encoded_path)

    obj = frogfs_obj_t.pack(frogfs_obj_t.size, FROGFS_TYPE_DIR, index,
            path_len)

    print(f'       - D {path:<60s} {hash:08X}', file=stderr)

    return obj + encoded_path

def make_file_obj(path, hash, index):
    encoded_path = path.encode('utf-8') + b'\0'
    path_len = len(encoded_path)
    compression = FROGFS_FILE_COMP_NONE

    with open(os.path.join(root, path), 'rb') as f:
        data = f.read()
    uncompressed_len = data_len = len(data)

    obj = frogfs_file_t.pack(frogfs_file_t.size, FROGFS_TYPE_FILE,
            index, path_len, compression, 0, data_len)
    uncompressed_obj = obj + encoded_path + data

    if 'compress' in state[path]['rules']:
        if 'deflate' in state[path]['rules']['compress']:
            compression = FROGFS_FILE_COMP_DEFLATE
            options = state[path]['rules']['compress']['deflate']
            level = options.setdefault('level', 9)

            data = pipe_script(pp_dict['deflate']['file'], options, data)
            data_len = len(data)
            obj = frogfs_file_comp_t.pack(frogfs_file_comp_t.size,
                    FROGFS_TYPE_FILE, index, path_len, compression, level,
                    data_len, uncompressed_len)
            compressed_obj = obj + encoded_path + data

        elif 'heatshrink' in state[path]['rules']['compress']:
            compression = FROGFS_FILE_COMP_HEATSHRINK
            options = state[path]['rules']['compress']['heatshrink']
            window = options.setdefault('window', 11)
            lookahead = options.setdefault('lookahead', 4)

            data = pipe_script(pp_dict['heatshrink']['file'], options, data)
            data_len = len(data)
            obj = frogfs_file_comp_t.pack(frogfs_file_comp_t.size,
                    FROGFS_TYPE_FILE, index, path_len, compression,
                    lookahead << 4 | window, data_len, uncompressed_len)
            compressed_obj = obj + encoded_path + data

        if len(compressed_obj) < len(uncompressed_obj):
            if compression == FROGFS_FILE_COMP_DEFLATE:
                comp = 'd '
            elif compression == FROGFS_FILE_COMP_HEATSHRINK:
                comp = 'hs'
            size = sz_str(uncompressed_len) + ' -> ' + sz_str(data_len)
            obj = compressed_obj

        else:
            compression = FROGFS_FILE_COMP_NONE

    if compression == FROGFS_FILE_COMP_NONE:
        comp = '  '
        size = '              ' + sz_str(uncompressed_len)
        obj = uncompressed_obj

    print(f'       - F {path:<32s} {comp} {size} {hash:08X}', file=stderr)

    return obj

def round_up(n, m):
    return ((n + m - 1) // m) * m

def sz_str(bytes):
    if bytes < 1024:
        s = f'{bytes:>4d}     B'
    elif bytes < 1024 * 1024:
        s = f'{bytes / 1024:>6.1f} KiB'
    elif bytes < 1024 * 1024 * 1024:
        s = f'{bytes / 1024 / 1024:>6.1f} MiB'
    elif bytes < 1024 * 1024 * 1024 * 1024:
        s = f'{bytes / 1024 / 1024 / 1024:>6.1f} GiB'
    return s

def pipe_script(script, args, data):
    _, extension = os.path.splitext(script)
    if extension == '.js':
        command = ['node']
    elif extension == '.py':
        command = [executable]
    else:
        raise Exception(f'unhandled file extension for {script}')

    command.append(script)
    for arg, value in args.items():
        command.append('--' + arg)
        if value is not None:
            command.append(str(value))

    process = Popen(command, stdin=PIPE, stdout=PIPE)
    data, _ = process.communicate(input=data)

    return data

def main():
    global skip_directories, root

    parser = ArgumentParser()
    parser.add_argument('--align', metavar='ALIGN',
                        help='data alignment, in bytes',
                        default=4)
    parser.add_argument('--skip-directories',
                        help='skip directory entries',
                        action='store_true', default=False)
    parser.add_argument('root', metavar='ROOT', help='root directory')
    parser.add_argument('output', metavar='OUTPUT', help='output binary')
    arguments = parser.parse_args()

    align = arguments.align
    skip_directories = arguments.skip_directories
    root = arguments.root
    output = arguments.output

    load_preprocessors()
    load_state()

    num_objs = len(state)
    head_len = round_up(frogfs_head_t.size, align)
    hash_len = round_up(frogfs_hash_t.size * num_objs, align)
    sort_len = round_up(frogfs_sort_t.size * num_objs, align)

    hashlist = []
    hashes = bytearray(hash_len)
    sorts = bytearray(sort_len)
    data = b''
    offset = head_len + hash_len + sort_len

    for index, path in enumerate(state.keys()):
        hash = djb2_hash(path)
        hashlist.append((hash, path, index))

    for position, (hash, path, index) in enumerate(sorted(hashlist)):
        if os.path.isdir(os.path.join(root, path)):
            obj = make_dir_obj(path, hash, index)
        else:
            obj = make_file_obj(path, hash, index)
        obj = obj.ljust(round_up(len(obj), align), b'\0')

        frogfs_hash_t.pack_into(hashes,
                frogfs_hash_t.size * position, hash, offset)
        frogfs_sort_t.pack_into(sorts, frogfs_sort_t.size * index,
                offset)

        data += obj
        offset += len(obj)

    bin_len = offset + frogfs_fs_foot_t.size
    head = frogfs_head_t.pack(FROGFS_MAGIC, head_len, FROGFS_VER_MAJOR,
            FROGFS_VER_MINOR, bin_len, num_objs, align)
    head = head.ljust(head_len, b'\0')

    bin = head + hashes + sorts + data
    foot = frogfs_fs_foot_t.pack(crc32(bin) & 0xFFFFFFFF)
    bin += foot

    with open(output, 'wb') as f:
        f.write(bin)

if __name__ == '__main__':
    main()
