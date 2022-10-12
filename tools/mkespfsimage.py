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

espfs_fs_header_t = Struct('<IBBHIHH')
# magic, len, version_major, version_minor, binary_len, num_objects, reserved
ESPFS_MAGIC = 0x2B534645 # EFS+
ESPFS_VERSION_MAJOR = 1
ESPFS_VERSION_MINOR = 0

espfs_hashtable_entry_t = Struct('<II')
# hash, offset

espfs_sorttable_entry_t = Struct('<I')
# offset

espfs_object_header_t = Struct('<BBHHH')
# type, len, index, path_len, reserved
ESPFS_TYPE_FILE = 0
ESPFS_TYPE_DIR  = 1

espfs_file_header_t = Struct('<IIHBB')
# data_len, file_len, flags, compression, reserved
ESPFS_FLAG_GZIP  = (1 << 0)
ESPFS_FLAG_CACHE = (1 << 1)
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
                    if s_name == 'filters':
                        if isinstance(config[s_name][ss_name], str):
                            config[s_name][ss_name] = [config[s_name][ss_name]]
                        if isinstance(ss, str):
                            ss = [ss]
                        config[s_name][ss_name] += ss
                    else:
                        config[s_name][ss_name] = ss

    for s_name in ('preprocessors', 'compressors', 'filters'):
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

    config['filters'] = OrderedDict(sorted(config['filters'].items(),
            key = pattern_sort))

def hash_path(path):
    hash = 5381
    for c in path.encode('utf8'):
        hash = ((hash << 5) + hash + c) & 0xFFFFFFFF
    return hash

def make_pathlist(root):
    pathlist = []
    for dir, _, files in os.walk(root, followlinks=True):
        reldir = os.path.relpath(dir, root).replace('\\', '/').lstrip('.') \
                .lstrip('/')
        absdir = os.path.abspath(dir)
        if reldir and os.path.exists(absdir):
            hash = hash_path(reldir)
            entry = (reldir, {'path': absdir, 'type': ESPFS_TYPE_DIR,
                    'hash': hash})
            bisect.insort(pathlist, entry)
        for file in files:
            relfile = os.path.join(reldir, file).replace('\\', '/') \
                    .lstrip('/')
            absfile = os.path.join(absdir, file)
            if os.path.exists(absfile):
                hash = hash_path(relfile)
                entry = (relfile, {'path': absfile, 'type': ESPFS_TYPE_FILE,
                        'hash': hash})
                bisect.insort(pathlist, entry)
    return pathlist

def make_dir_object(hash, path, attributes):
    index = attributes['index']

    print('%08x %-34s dir' % (hash, path))
    path = path.encode('utf8') + b'\0'
    path = path.ljust((len(path) + 3) // 4 * 4, b'\0')
    header = espfs_object_header_t.pack(ESPFS_TYPE_DIR,
            espfs_object_header_t.size, index, len(path), 0)
    return header + path

def make_file_object(hash, path, data, attributes):
    global config

    index = attributes['index']
    actions = attributes['actions']
    flags = 0
    compression = ESPFS_COMPRESSION_NONE
    initial_data = data
    initial_len = len(data)

    if 'cache' in actions:
        flags |= ESPFS_FLAG_CACHE

    for action in actions:
        if action in config['preprocessors']:
            command = config['preprocessors'][action]['command']
            process = subprocess.Popen(command, stdin = subprocess.PIPE,
                    stdout = subprocess.PIPE, shell = True)
            data = process.communicate(input = data)[0]

    file_data = data
    file_len = len(data)

    if file_len >= initial_len:
        data = initial_data
        file_len = initial_len

    if 'gzip' in actions:
        flags |= ESPFS_FLAG_GZIP
        level = config['compressors']['gzip']['level']
        level = min(max(level, 0), 9)
        data = gzip.compress(data, level)
    elif 'heatshrink' in actions:
        compression = ESPFS_COMPRESSION_HEATSHRINK
        window_sz2 = config['compressors']['heatshrink']['window_sz2']
        lookahead_sz2 = config['compressors']['heatshrink']['lookahead_sz2']
        data = espfs_heatshrink_header_t.pack(window_sz2, lookahead_sz2,
                0) + heatshrink2.compress(data, window_sz2 = window_sz2,
                lookahead_sz2 = lookahead_sz2)

    data_len = len(data)

    if data_len >= file_len:
        flags &= ~ESPFS_FLAG_GZIP
        compression = ESPFS_COMPRESSION_NONE
        data = file_data
        data_len = file_len

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

    if flags & ESPFS_FLAG_GZIP:
        file_len = data_len

    path = path.encode('utf8') + b'\0'
    path = path.ljust((len(path) + 3) // 4 * 4, b'\0')
    data = data.ljust((data_len + 3) // 4 * 4, b'\0')
    header = espfs_object_header_t.pack(ESPFS_TYPE_FILE,
            espfs_object_header_t.size + espfs_file_header_t.size,
            index, len(path), 0) + espfs_file_header_t.pack(data_len,
            file_len, flags, compression, 0)

    return header + path + data

def main():
    global config

    parser = ArgumentParser()
    parser.add_argument('ROOT')
    parser.add_argument('IMAGE')
    args = parser.parse_args()

    load_config(args.ROOT)

    pathlist = make_pathlist(args.ROOT)
    index = 0
    for entry in pathlist[:]:
        path, attributes = entry
        attributes['actions'] = OrderedDict()
        attributes['index'] = index
        index += 1
        for pattern, actions in config['filters'].items():
            if fnmatch(path, pattern):
                if 'discard' in actions:
                    index -= 1
                    pathlist.remove(entry)
                    break
                if 'skip' in actions:
                    break
                for action in actions[:]:
                    if action == 'cache':
                        attributes['actions'][action] = None
                    elif action in config['compressors']:
                        attributes['actions'][action] = None
                    elif action in config['preprocessors']:
                        attributes['actions'][action] = None
                    elif action.startswith('no-'):
                        pass
                    else:
                        print('unknown action %s for %s' % (action, pattern),
                        file = sys.stderr)
                        sys.exit(1)

        for pattern, actions in config['filters'].items():
            if fnmatch(path, pattern):
                for action in actions[:]:
                    if action.startswith('no-'):
                        if action == 'no-cache':
                            del attributes['actions']['cache']
                        elif action == 'no-compression':
                            for name in config['compressors']:
                                if name in attributes['actions']:
                                    del attributes['actions'][name]
                        elif action == 'no-preprocessing':
                            for name in config['preprocessors']:
                                if name in attributes['actions']:
                                    del attributes['actions'][name]
                        elif action[3:] in attributes['actions']:
                            del attributes['actions'][action[3:]]

    npmset = set()
    for _, attributes in pathlist:
        actions = attributes['actions']
        for action in actions:
            if action in config['preprocessors']:
                for npm in config['preprocessors'][action].get('npm', ()):
                    npmset.add(npm)

    for npm in npmset:
        if not os.path.exists(os.path.join('node_modules', npm)):
            subprocess.check_call('npm install %s' % (npm), shell = True)

    num_objects = len(pathlist)
    offset = espfs_fs_header_t.size + \
            (espfs_hashtable_entry_t.size * num_objects) + \
            (espfs_sorttable_entry_t.size * num_objects)
    hashtable = b''
    sorttable = bytearray(espfs_sorttable_entry_t.size * num_objects)
    objects = b''

    pathlist = sorted(pathlist, key = lambda e: (e[1]['hash'], e[0]))
    for path, attributes in pathlist:
        abspath = attributes['path']
        type = attributes['type']
        hash = attributes['hash']
        if type == ESPFS_TYPE_DIR:
            object = make_dir_object(hash, path, attributes)
        elif type == ESPFS_TYPE_FILE:
            with open(abspath, 'rb') as f:
                data = f.read()
            object = make_file_object(hash, path, data, attributes)
        else:
            print('unknown object type %d' % (type), file = sys.stderr)
            sys.exit(1)
        hashtable += espfs_hashtable_entry_t.pack(hash, offset)
        espfs_sorttable_entry_t.pack_into(sorttable,
                espfs_sorttable_entry_t.size * attributes['index'], offset)
        objects += object
        offset += len(object)

    binary_len = offset + espfs_crc32_footer_t.size
    header = espfs_fs_header_t.pack(ESPFS_MAGIC, espfs_fs_header_t.size,
            ESPFS_VERSION_MAJOR, ESPFS_VERSION_MINOR, binary_len,
            num_objects, 0)
    binary = header + hashtable + sorttable + objects
    binary += espfs_crc32_footer_t.pack(crc32(binary) & 0xFFFFFFFF)

    with open(args.IMAGE, 'wb') as f:
        f.write(binary)

if __name__ == '__main__':
    main()
