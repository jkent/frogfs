#!/usr/bin/env python

import bisect
import gzip
import os
import subprocess
import sys
from argparse import ArgumentParser
from collections import OrderedDict
from fnmatch import fnmatch
from struct import Struct
from zlib import crc32

import heatshrink2
from hiyapyco import odyldo

script_dir = os.path.dirname(os.path.realpath(__file__))

espfs_fs_header_t = Struct('<IBBHII')
# magic, len, version_major, version_minor, num_objects, file_len
ESPFS_MAGIC = 0x2B534645 # EFS+
ESPFS_VERSION_MAJOR = 0
ESPFS_VERSION_MINOR = 1

espfs_hashtable_t = Struct('<II')
# hash, offset

espfs_object_header_t = Struct('<BBH')
# type, len, path_len
ESPFS_TYPE_FILE = 0
ESPFS_TYPE_DIR  = 1

espfs_file_header_t = Struct('<IIHBB')
# data_len, file_len, flags, compression, reserved
ESPFS_FLAG_GZIP = (1 << 1)
ESPFS_COMPRESSION_NONE       = 0
ESPFS_COMPRESSION_HEATSHRINK = 1

espfs_heatshrink_header_t = Struct('<BBH')
# window_sz2, lookahead_sz2

espfs_crc32_footer_t = Struct('<I')
# crc32

def load_config(root):
    global config

    defaults_file = os.path.join(script_dir, '..', 'espfs_defaults.yaml')
    with open(defaults_file) as f:
        config = list(odyldo.safe_load_all(f))[0]

    user = OrderedDict()
    user_file = os.path.join(root, 'espfs.yaml')
    if os.path.exists(user_file):
        with open(user_file) as f:
            user_list = list(odyldo.yaml.safe_load_all(f))
        if user_list:
            user = user_list[0]

    def section_merge(s_name):
        s = user.get(s_name, OrderedDict())
        if s is None:
            if s_name in config:
                del config[s_name]
        else:
            for ss_name, ss in s.items():
                if ss is None:
                    if ss_name in config[s_name]:
                        del config[s_name][ss_name]
                else:
                    config[s_name][ss_name] = ss

    for s_name in ('preprocessors', 'compressors', 'paths'):
        section_merge(s_name)
        for ss_name, ss in config.get(s_name, OrderedDict()).items():
            if isinstance(ss, str):
                config[s_name][ss_name] = [ss]
            elif isinstance(ss, dict):
                for sss_name, sss in ss.items():
                    if isinstance(sss, str):
                        ss[sss_name] = [sss]

    class pattern_sort:
        def __init__(self, path, *args):
            self.pattern, _ = path

        def __lt__(self, other):
            if self.pattern == '*':
                return False
            if other.pattern == '*':
                return True
            if self.pattern.startswith('*') and \
                    not other.pattern.startswith('*'):
                return False
            if not self.pattern.startswith('*') and \
                    other.pattern.startswith('*'):
                return True
            return self.pattern < other.pattern

    config['paths'] = OrderedDict(sorted(config['paths'].items(),
            key = pattern_sort))

def hash_path(path):
    hash = 5381
    for c in path.encode('utf8'):
        hash = ((hash << 5) + hash + c) & 0xFFFFFFFF
    return hash

def make_pathlist(root):
    pathlist = []
    for dir, _, files in os.walk(root):
        reldir = os.path.relpath(dir, root).replace('\\', '/').lstrip('.') \
                .lstrip('/')
        absdir = os.path.abspath(dir)
        if reldir:
            entry = (hash_path(reldir), reldir, absdir, ESPFS_TYPE_DIR, {})
            bisect.insort(pathlist, entry)
        for file in files:
            relfile = os.path.join(reldir, file).replace('\\', '/') \
                    .lstrip('/')
            absfile = os.path.join(absdir, file)
            entry = (hash_path(relfile), relfile, absfile, ESPFS_TYPE_FILE, {})
            bisect.insort(pathlist, entry)
    return pathlist

def make_dir_object(hash, path):
    print('%08x %-34s dir' % (hash, path))
    path = path.encode('utf8') + b'\0'
    path = path.ljust((len(path) + 3) // 4 * 4, b'\0')
    header = espfs_object_header_t.pack(ESPFS_TYPE_DIR,
            espfs_object_header_t.size, len(path))
    return header + path

def make_file_object(hash, path, data, actions={}):
    global config

    flags = 0
    compression = ESPFS_COMPRESSION_NONE
    initial_len = len(data)

    if 'skip' not in actions:
        for action in actions:
            if action in ('heatshrink'):
                compression = ESPFS_COMPRESSION_HEATSHRINK
            elif action == 'gzip':
                flags |= ESPFS_FLAG_GZIP
                level = config['preprocessors']['gzip']['level']
                level = min(max(level, 0), 9)
                data = gzip.compress(data, level)
            elif action in config['preprocessors']:
                command = config['preprocessors'][action]['command']
                process = subprocess.Popen(command, stdin = subprocess.PIPE,
                        stdout = subprocess.PIPE, shell = True)
                data = process.communicate(input = data)[0]
            else:
                print('Unknown action: %s' % (action), file = sys.stderr)
                sys.exit(1)

    file_data = data
    file_len = len(data)

    if compression == ESPFS_COMPRESSION_HEATSHRINK:
        window_sz2 = config['compressors']['heatshrink']['window_sz2']
        lookahead_sz2 = config['compressors']['heatshrink']['lookahead_sz2']
        data = espfs_heatshrink_header_t.pack(window_sz2, lookahead_sz2,
                0) + heatshrink2.compress(data, window_sz2 = window_sz2,
                lookahead_sz2 = lookahead_sz2)

    data_len = len(data)

    if compression and data_len >= file_len:
        compression = ESPFS_COMPRESSION_NONE
        data = file_data
        data_len = len(data)

    if initial_len < 1024:
        initial_len_str = '%d B' % (initial_len)
        data_len_str = '%d B' % (data_len)
    elif initial_len < 1024 * 1024:
        initial_len_str = '%.1f KiB' % (initial_len / 1024)
        data_len_str = '%.1f KiB' % (data_len / 1024)
    else:
        initial_len_str = '%.1f MiB' % (initial_len / 1024 / 1024)
        data_len_str = '%.1f MiB' % (data_len / 1024 / 1024)

    percent = 100.0
    if initial_len > 0:
        percent = data_len / initial_len * 100.0

    stats = '%-9s -> %-9s (%.1f%%)' % (initial_len_str, data_len_str, percent)
    print('%08x %-34s file %s' % (hash, path, stats))

    path = path.encode('utf8') + b'\0'
    path = path.ljust((len(path) + 3) // 4 * 4, b'\0')
    data = data.ljust((data_len + 3) // 4 * 4, b'\0')
    header = espfs_object_header_t.pack(ESPFS_TYPE_FILE,
            espfs_object_header_t.size + espfs_file_header_t.size,
            len(path)) + espfs_file_header_t.pack(data_len, file_len, flags,
            compression, 0)

    return header + path + data

def main():
    global config

    parser = ArgumentParser()
    parser.add_argument('ROOT')
    parser.add_argument('IMAGE')
    args = parser.parse_args()

    load_config(args.ROOT)

    pathlist = make_pathlist(args.ROOT)
    npmset = set()
    for entry in pathlist[:]:
        _, path, _, type, attributes = entry
        attributes['actions'] = OrderedDict()
        for pattern, actions in config['paths'].items():
            if fnmatch(path, pattern):
                if 'discard' in actions:
                    pathlist.remove(entry)
                    break
                for action in actions:
                    if action in ('discard', 'gzip', 'heatshrink'):
                        pass
                    elif action not in config['preprocessors']:
                        print('unknown action %s for %s' % (action, pattern),
                                file = sys.sterr)
                        sys.exit(1)
                    else:
                        for npm in config['preprocessors'][action].get('npm',
                                ()):
                            npmset.add(npm)

                    attributes['actions'][action] = None

    for npm in npmset:
        if not os.path.exists(os.path.join('node_modules', npm)):
            subprocess.check_call('npm install %s' % (npm), shell = True)

    num_objects = len(pathlist)
    offset = espfs_fs_header_t.size + (espfs_hashtable_t.size * num_objects)
    hashtable = b''
    objects = b''

    for hash, path, abspath, type, attributes in sorted(pathlist):
        if type == ESPFS_TYPE_DIR:
            object = make_dir_object(hash, path)
        elif type == ESPFS_TYPE_FILE:
            with open(abspath, 'rb') as f:
                data = f.read()
            object = make_file_object(hash, path, data, attributes['actions'])
        else:
            print('unknown object type %d' % (type), file = sys.stderr)
            sys.exit(1)
        hashtable += espfs_hashtable_t.pack(hash, offset)
        objects += object
        offset += len(object)

    binary_len = offset + espfs_crc32_footer_t.size
    header = espfs_fs_header_t.pack(ESPFS_MAGIC, espfs_fs_header_t.size,
            ESPFS_VERSION_MAJOR, ESPFS_VERSION_MINOR, num_objects, binary_len)
    binary = header + hashtable + objects
    binary += espfs_crc32_footer_t.pack(crc32(binary) & 0xFFFFFFFF)

    with open(args.IMAGE, 'wb') as f:
        f.write(binary)

if __name__ == '__main__':
    main()
