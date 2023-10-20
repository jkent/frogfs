import json
import os
import zlib
from argparse import ArgumentParser
from fnmatch import fnmatch
from shutil import which
from struct import Struct
from subprocess import PIPE, Popen
from sys import executable, stderr
from typing import Union
from zlib import crc32

import heatshrink2
import yaml

# Header
FROGFS_MAGIC            = 0x474F5246 # FROG
FROGFS_VER_MAJOR        = 1
FROGFS_VER_MINOR        = 0

# Object types
FROGFS_OBJ_TYPE_FILE    = 0
FROGFS_OBJ_TYPE_DIR     = 1

# FrogFS header
# magic, bin_sz, num_ent, ver_majr, ver_minor
s_head = Struct('<IBBHI')

# Hash table entry
# hash, offs
s_hash = Struct('<II')

# Offset
# offs
s_offs = Struct("<I")

# Entry header
# parent, child_count, seg_sz, opts
s_ent = Struct('<IHBB')

# Directory header
# parent, child_count, seg_sz, opts
s_dir = Struct('<IHBB')

# File header
# parent, child_count, seg_sz, opts, data_offs, data_sz
s_file = Struct('<IHBBII')

# Compressed file header
# parent, child_count, seg_sz, opts, data_offs, data_sz, real_sz
s_comp = Struct('<IHBBIII')

# FrogFS footer
# crc32
s_foot = Struct('<I')


def pipe_script(script: str, args: dict, data: Union[bytes, bytearray]) -> bytes:
    '''Run a script with arguments and data to stdin, returning stdout data'''
    _, ext = os.path.splitext(script)
    if ext == '.js':
        if which('node') == None:
            raise Exception('node was not found, please install it')
        command = ['node']
    elif ext == '.py':
        command = [executable]
    else:
        raise Exception(f'unhndled file extension for {script}')

    command.append(script)
    for arg, value in args.items():
        arg = str(arg)
        command.append(('--' if len(arg) > 1 else '-') + arg)
        if value is not None:
            command.append(str(value))

    process = Popen(command, stdin=PIPE, stdout=PIPE)
    data, _ = process.communicate(input=data)
    code = process.returncode
    if code != 0:
        raise Exception(f'error {code} running process')

    return data

def collect_transforms() -> dict:
    '''Find transforms in frogfs' tools directory and in ${CWD}/tools'''
    transforms = {}
    for path in (g_tools_dir, 'tools'):
        if not os.path.exists(path):
            continue

        for file in os.listdir(path):
            if file.startswith('transform-'):
                name, _ = os.path.splitext(file[10:])
                filepath = os.path.join(path, file)
                transforms[name] = {'path': filepath}
    return transforms

def djb2_hash(s: str) -> int:
    '''A simple string hashing algorithm'''
    hash = 5381
    for c in s.encode('utf-8'):
        hash = ((hash << 5) + hash ^ c) & 0xFFFFFFFF
    return hash

### Stage 1 ###

def collect_entries() -> dict:
    '''Collects all the path items from root directory'''
    entries = {}
    for dir, _, files in os.walk(g_root_dir, followlinks=True):
        reldir = os.path.relpath(dir, g_root_dir).replace('\\', '/')
        if reldir == '.' or reldir.startswith('./'):
            reldir = reldir[2:]
        entry = {
            'path': reldir,
            'seg': os.path.basename(reldir),
            'type': 'dir',
        }
        entries[reldir] = entry

        for file in files:
            relfile = os.path.join(reldir, file).replace('\\', '/')
            entry = {
                'path': relfile,
                'seg': file,
                'type': 'file',
            }
            entries[relfile] = entry

    return dict(sorted(entries.items()))

def clean_removed(entries: dict) -> bool:
    '''Check if files have been removed from root directory'''
    removed = False
    for dir, _, files in os.walk(g_cache_dir, topdown=False, followlinks=True):
        reldir = os.path.relpath(dir, g_cache_dir).replace('\\', '/')
        if reldir == '.' or reldir.startswith('./'):
            reldir = reldir[2:]
        for file in files:
            relfile = os.path.join(reldir, file).replace('\\', '/')
            if relfile not in entries.keys():
                os.unlink(os.path.join(g_cache_dir, relfile))
                removed = True
        if reldir not in entries.keys():
            os.rmdir(os.path.join(g_cache_dir, reldir))
            removed = True

    return removed

def load_state(file: str, entries: dict) -> bool:
    '''Loads previous state or initializes a empty one'''

    paths = {}
    try:
        with open(file, 'r') as f:
            paths = json.load(f)
    except:
        pass

    for path, state in paths.items():
        if path not in entries:
            continue
        ent = entries[path]
        ent['transforms'] = state.get('transforms', {})
        ent['compressor'] = state.get('compressor')
        ent['real_sz'] = state.get('real_sz')

    dirty = False
    for ent in entries.values():
        if 'transforms' not in ent:
            ent['preprocess'] = True
            dirty = True

    return dirty

def load_config(file: str) -> dict:
    '''Loads and normalizes configuration yaml file'''
    with open(file, 'r') as f:
        doc = yaml.safe_load(f)

    config = {'filters': {}}
    for path, filters in doc['filters'].items():
        config['filters'][path] = {}
        if not filters:
            continue
        for filter in filters:
            if isinstance(filter, str):
                filter_name = filter
                config['filters'][path][filter_name] = {}
            elif isinstance(filter, dict):
                for filter_name, args in filter.items():
                    if not isinstance(args, dict):
                        args = {}
                    config['filters'][path][filter_name] = args
    return config

def apply_rules(filters: dict, entry: dict) -> None:
    '''Applies preprocessing rules for a single entry'''
    transforms = {}
    compressor = None

    for filter, actions in filters.items():
        if fnmatch(entry['path'], filter):
            for action, args in actions.items():
                action = action.split()
                disable = False
                compress = None
                if action[0] == 'no':
                    disable = True
                    action = action[1:]
                if action[0] == 'compress':
                    compress = False if disable else action[1]
                    action = None
                else:
                    action = action[0]

                if action != None:
                    if transforms.get(action, None) != None:
                        continue
                    if action == 'cache':
                        if disable:
                            transforms[action] = False
                    elif action == 'discard':
                        transforms[action] = not disable
                    elif action in g_transforms:
                        if entry['type'] == 'file':
                            transforms[action] = False if disable else args
                    else:
                        raise Exception(f'{action} is not a known transform')

                if compress != None:
                    if compressor != None:
                        continue
                    if compress not in ('deflate', 'heatshrink'):
                        raise Exception(f'{compress} is not a valid compressor')
                    if entry['type'] == 'file':
                        compressor = [compress, args]

    if entry.setdefault('transforms', {}) != transforms or \
                entry['transforms'].keys() != transforms.keys():
        entry['transforms'] = transforms
        entry['preprocess'] = True

    if entry['type'] == 'file' and \
            entry.setdefault('compressor', None) != compressor:
        entry['compressor'] = compressor
        entry['preprocess'] = True

def preprocess(entry: dict) -> None:
    '''Run preprocessors for a given entry'''
    if 'discard' in entry['transforms']:
        return

    path = entry['path']

    if entry['type'] == 'dir':
        if not os.path.exists(os.path.join(g_cache_dir, path)):
            os.mkdir(os.path.join(g_cache_dir, path))

    if entry['type'] == 'file':
        print(f'         - {entry["path"]}', file=stderr)

        with open(os.path.join(g_root_dir, path), 'rb') as f:
            data = f.read()

        for name, args in entry['transforms'].items():
            if name == 'cache':
                continue

            print(f'           - {name}... ', file=stderr, end='', flush=True)
            transform = g_transforms[name]
            data = pipe_script(transform['path'], args, data)
            print('done', file=stderr)

        if entry['compressor']:
            name, args = entry['compressor']
            print(f'           - compress {name}... ', file=stderr, end='',
                    flush=True)

            if name == 'deflate':
                level = args.get('level', 9)
                compressed = zlib.compress(data, level)
            elif name == 'heatshrink':
                window = args.get('window', 11)
                lookahead = args.get('lookahead', 4)
                compressed = heatshrink2.compress(data, window, lookahead)

            if len(data) < len(compressed):
                print('skipped', file=stderr)
                entry['real_sz'] = None
            else:
                percent = 0
                size = os.path.getsize(os.path.join(g_root_dir, path))
                if size != 0:
                    percent = len(compressed) / size
                print(f'done ({percent * 100:.1f}%)', file=stderr)
                entry['real_sz'] = len(data)
                data = compressed

        with open(os.path.join(g_cache_dir, path), 'wb') as f:
            f.write(data)

def run_preprocessors(entries: dict) -> bool:
    '''Run preprocessors on all entries as needed'''
    dirty = False
    for entry in entries.values():
        path = entry['path']

        if not entry.get('preprocess') and \
                not os.path.exists(os.path.join(g_cache_dir, path)):
            entry['preprocess'] = True

        if not entry.get('preprocess') and \
                not entry['transforms'].get('cache', True):
            entry['preprocess'] = True

        # if a file, check that the file is not newer
        if not entry.get('preprocess') and entry['type'] == 'file':
            root_mtime = os.path.getmtime(os.path.join(g_root_dir, path))
            cache_mtime = os.path.getmtime(os.path.join(g_cache_dir, path))
            if root_mtime > cache_mtime:
                entry['preprocess'] = True

        if entry.get('preprocess'):
            dirty |= True
            preprocess(entry)

    return dirty

def save_state(file: str, entries: dict) -> None:
    '''Save current state'''
    paths = {}

    for entry in entries.values():
        state = {
            'type': entry['type'],
        }
        if entry['transforms'] != {}:
            state['transforms'] = entry['transforms']

        if entry['type'] == 'file':
            if entry['compressor'] is not None:
                state['compressor'] = entry['compressor']
            if entry.get('real_sz') is not None:
                state['real_sz'] = entry['real_sz']
        paths[entry['path']] = state

    with open(file, 'w') as f:
        json.dump(paths, f, indent=4)

### Stage 2 ###

def generate_dir_header(ent: dict, entries: dict) -> None:
    '''Generate header and data for a directory entry'''
    if ent['path'] == '':
        ent['parent'] = None
        depth = 0
    else:
        depth = ent['path'].count('/') + 1

    seg = ent['seg'].encode('utf-8')

    children = []
    for path, entry in entries.items():
        count = path.count('/')
        if count != depth or not entry['path']:
            continue
        if (count == 0 and depth == 0) or path.startswith(ent['path'] + '/'):
            children.append(entry)
            if entry['path']:
                entry['parent'] = ent

    ent['children'] = children
    child_count = len(children)

    header = bytearray(s_dir.size + (4 * child_count) + len(seg))
    s_dir.pack_into(header, 0, 0, child_count, len(seg), 0)
    header[s_dir.size + (4 * child_count):] = seg
    ent['header'] = header
    ent['data_sz'] = 0

def generate_file_header(ent) -> None:
    '''Generate header and load data for a file entry'''
    seg = ent['seg'].encode('utf-8')

    data_sz = os.path.getsize(os.path.join(g_cache_dir, ent['path']))
    if ent.get('real_sz') is not None:
        method, args = ent['compressor']
        if method == 'deflate':
            comp = 1
            opts = args.get('level', 9)
        elif method == 'heatshrink':
            comp = 2
            window = args.get('window', 11)
            lookahead = args.get('lookahead', 4)
            opts = lookahead << 4 | window

        header = bytearray(s_comp.size + len(seg))
        s_comp.pack_into(header, 0, 0, 0xFF00 | comp, len(seg), opts, 0,
                data_sz, ent['real_sz'])
        header[s_comp.size:] = seg
    else:
        header = bytearray(s_file.size + len(seg))
        s_file.pack_into(header, 0, 0, 0xFF00, len(seg), 0, 0, data_sz)
        header[s_file.size:] = seg

    ent['header'] = header
    ent['data_sz'] = data_sz

def align(n: int) -> int:
    '''Return n rounded up to the next alignment'''
    return ((n + 3) // 4) * 4

def pad(data: Union[bytes, bytearray]) -> bytes:
    '''Return data padded with zeros to next alignment'''
    return data.ljust(align(len(data)), b'\0')

def generate_frogfs_header(entries: dict) -> bytes:
    '''Generate FrogFS header and calculate entry offsets'''
    num_ent = len(entries)

    bin_sz = align(s_head.size) + align(s_hash.size * num_ent)
    for entry in entries.values():
        entry['header_offs'] = bin_sz
        bin_sz += align(len(entry['header']))

    for entry in entries.values():
        entry['data_offs'] = bin_sz
        bin_sz += align(entry['data_sz'])

    bin_sz += s_foot.size

    return s_head.pack(FROGFS_MAGIC, FROGFS_VER_MAJOR, FROGFS_VER_MINOR,
            num_ent, bin_sz)

def apply_fixups(entries: dict) -> None:
    for entry in entries.values():
        parent = entry['parent']
        if parent:
            s_offs.pack_into(entry['header'], 0, parent['header_offs'])
        if entry['type'] == 'file':
            s_offs.pack_into(entry['header'], 8, entry['data_offs'])
            continue
        for i, child in enumerate(entry['children']):
            offs = child['header_offs']
            s_offs.pack_into(entry['header'], s_dir.size + (i * 4), offs)

def generate_hashtable(entries: dict) -> bytes:
    '''Generate hashtable for entries'''
    entries = {djb2_hash(k): v for k,v in entries.items()}
    entries = dict(sorted(entries.items(), key=lambda e: e))

    data = b''
    for hash, entry in entries.items():
        data += s_hash.pack(hash, entry['header_offs'])

    return data

def gather_data(entries):
    '''Gather all entry data'''
    data = b''

    # first gather the headers
    for entry in entries.values():
        data += pad(entry['header'])

    # then gather the file data
    for entry in entries.values():
        if entry['type'] == 'file':
            with open(os.path.join(g_cache_dir, entry['path']), 'rb') as f:
                data += pad(f.read())

    return data

def generate_footer(data: Union[bytes, bytearray]) -> bytes:
    '''Generate FrogFS footer'''
    return s_foot.pack(crc32(data) & 0xFFFFFFFF)

if __name__ == '__main__':
    frogfs_dir = os.path.join(os.path.dirname(__file__), '..')
    frogfs_dir = os.path.abspath(frogfs_dir)
    g_tools_dir = os.path.join(frogfs_dir, 'tools')
    config_file = os.path.join(frogfs_dir, 'default_config.yaml')

    os.environ['FROGFS_DIR'] = frogfs_dir
    cmakefiles_dir = os.environ.get('CMAKEFILES_DIR', os.getcwd())
    os.environ['NODE_PREFIX'] = cmakefiles_dir
    os.environ['NODE_PATH'] = os.path.join(cmakefiles_dir, 'node_modules') + \
            os.pathsep + g_tools_dir

    parser = ArgumentParser()
    parser.add_argument('--config', metavar='CONFIG',
                        help='YAML configuration file', default=config_file)
    parser.add_argument('root', metavar='ROOT', help='root directory')
    parser.add_argument('output', metavar='OUTPUT', help='output binary file')
    args = parser.parse_args()

    config_file = args.config
    g_root_dir = args.root
    output_file = args.output
    output_name, _ = os.path.splitext(os.path.basename(output_file))
    g_cache_dir = os.path.join(cmakefiles_dir, output_name + '-cache')
    state_file = os.path.join(cmakefiles_dir, output_name + '-cache-state.json')
    g_transforms = collect_transforms()

    print("       - Stage 1", file=stderr)
    entries = collect_entries()
    dirty = clean_removed(entries)
    dirty |= load_state(state_file, entries)
    config = load_config(config_file)
    for entry in entries.values():
        apply_rules(config['filters'], entry)
    dirty |= run_preprocessors(entries)
    dirty |= not os.path.exists(state_file)
    dirty |= not os.path.exists(output_file)
    if not dirty:
        dirty |= os.path.getmtime(state_file) > os.path.getmtime(output_file)
    if not dirty:
        print("         - Nothing to do!", file=stderr)
        exit(0)
    save_state(state_file, entries)

    print("       - Stage 2", file=stderr)
    for entry in entries.values():
        if entry['type'] == 'dir':
            generate_dir_header(entry, entries)
        if entry['type'] == 'file':
            generate_file_header(entry)

    data = generate_frogfs_header(entries)
    data += generate_hashtable(entries)
    apply_fixups(entries)
    data += gather_data(entries)
    data += generate_footer(data)

    with open(output_file, 'wb') as f:
        f.write(data)

    print("         - Output file written", file=stderr)
