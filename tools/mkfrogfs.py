import json
import os
from argparse import ArgumentParser
from fnmatch import fnmatch
from shutil import which
from struct import Struct
from subprocess import PIPE, Popen
from sys import executable, stderr
from zlib import crc32

import yaml


# Header
FROGFS_MAGIC            = 0x474F5246 # FROG
FROGFS_VER_MAJOR        = 1
FROGFS_VER_MINOR        = 0
FROGFS_FLAG_DIRS        = (1 << 0)

# Object types
FROGFS_OBJ_TYPE_FILE    = 0
FROGFS_OBJ_TYPE_DIR     = 1

# FrogFS header
# magic, len, ver_major, ver_minor, bin_len, num_objs, align, flags
s_header = Struct('<IBBHIHBB')

# Hash table entry
# hash, offset
s_hash_entry = Struct('<II')

# Object header
# len, type, path_len
s_object = Struct('<BBH')

# Directory header
# len, type, path_len, child_count
s_dir = Struct('<BBHH')

# Sort table entry
# offset
s_sort_entry = Struct('<I')

# File header
# len, type, path_len, data_len, compression
s_file = Struct('<BBHIB')

# Compressed file header
# len, type, path_len, data_len, compression, options, res, expanded_len
s_file_comp = Struct('<BBHIBBHI')

# FrogFS footer
# crc32
s_footer = Struct('<I')


def pipe_script(script: str, args: dict, data: bytes | bytearray) -> bytes:
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

def collect_compressors() -> dict:
    '''Find compressors in frogfs' tools directory and in ${CWD}/tools'''
    compressors = {}
    for path in (g_tools_dir, 'tools'):
        if not os.path.exists(path):
            continue

        for file in os.listdir(path):
            if file.startswith('compress-'):
                name, _ = os.path.splitext(file[9:])
                filepath = os.path.join(path, file)
                compressors[name] = {
                    'path': filepath,
                    'id': int(pipe_script(filepath, {'id': None}, ''))
                }
    return compressors

def djb2_hash(s: str) -> int:
    '''A simple string hashing algorithm'''
    hash = 5381
    for c in s.encode('utf-8'):
        hash = ((hash << 5) + hash ^ c) & 0xFFFFFFFF
    return hash

### Stage 1 ###

def collect_objects() -> dict:
    '''Collects all the path items from root directory'''
    objects = {}
    for dir, _, files in os.walk(g_root_dir, followlinks=True):
        reldir = os.path.relpath(dir, g_root_dir).replace('\\', '/') \
                .lstrip('.').lstrip('/')
        absdir = os.path.abspath(dir)
        obj = {
            'path': reldir,
            'hash': djb2_hash(reldir),
            'type': 'dir',
        }
        objects[reldir] = obj

        for file in files:
            relfile = os.path.join(reldir, file).replace('\\', '/')
            absfile = os.path.join(absdir, file)
            obj = {
                'path': relfile,
                'hash': djb2_hash(relfile),
                'type': 'file',
                'root_size': os.path.getsize(absfile),
            }
            objects[relfile] = obj

    return dict(sorted(objects.items()))

def clean_removed(objects: dict) -> bool:
    '''Check if files have been removed from root directory'''
    removed = False
    for dir, _, files in os.walk(g_cache_dir, topdown=False, followlinks=True):
        reldir = os.path.relpath(dir, g_cache_dir).replace('\\', '/') \
                .lstrip('.').lstrip('/')
        for file in files:
            relfile = os.path.join(reldir, file).replace('\\', '/')
            if relfile not in objects.keys():
                os.unlink(os.path.join(g_cache_dir, relfile))
                removed = True
        if reldir not in objects.keys():
            os.rmdir(os.path.join(g_cache_dir, reldir))
            removed = True

    return removed

def load_state(file: str, objects: dict) -> bool:
    '''Loads previous state or initializes a empty one'''

    data = {'options': {}, 'paths': {}}
    try:
        with open(file, 'r') as f:
            data = json.load(f)
    except:
        pass

    for path, state in data['paths'].items():
        if path not in objects:
            continue
        object = objects[path]
        object['transforms'] = state.get('transforms', {})
        object['compressor'] = state.get('compressor')
        object['expanded_sz'] = state.get('expanded_sz')

    options = {
        'align': g_align,
        'use_dirs': g_use_dirs,
    }
    dirty = data['options'] != options

    for object in objects.values():
        if dirty or 'transforms' not in object:
            object['preprocess'] = True

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

def apply_rules(filters: dict, obj: dict) -> None:
    '''Applies preprocessing rules for a single object'''
    transforms = {}
    compressor = None

    for filter, actions in filters.items():
        if fnmatch(obj['path'], filter):
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
                        if obj['type'] == 'file':
                            transforms[action] = False if disable else args
                    else:
                        raise Exception(f'{action} is not a known transform')

                if compress != None:
                    if compressor != None:
                        continue
                    if obj['type'] == 'file':
                        compressor = [compress, args]

    if obj.setdefault('transforms', {}) != transforms or \
                obj['transforms'].keys() != transforms.keys():
        obj['transforms'] = transforms
        obj['preprocess'] = True
    
    if obj['type'] == 'file' and \
            obj.setdefault('compressor', None) != compressor:
        obj['compressor'] = compressor
        obj['preprocess'] = True

def preprocess(obj: dict) -> None:
    '''Run preprocessors for a given object'''
    if 'discard' in obj['transforms']:
        return

    path = obj['path']

    if obj['type'] == 'dir':
        if not os.path.exists(os.path.join(g_cache_dir, path)):
            os.mkdir(os.path.join(g_cache_dir, path))

    if obj['type'] == 'file':
        print(f'         - {obj["path"]}', file=stderr)

        with open(os.path.join(g_root_dir, path), 'rb') as f:
            data = f.read()

        for name, args in obj['transforms'].items():
            if name == 'cache':
                continue

            print(f'           - {name}... ', file=stderr, end='', flush=True)
            transform = g_transforms[name]
            data = pipe_script(transform['path'], args, data)
            print('done', file=stderr)

        if obj['compressor']:
            name, args = obj['compressor']
            print(f'           - compress {name}... ', file=stderr, end='',
                    flush=True)
            compressor = g_compressors[name]
            compressed = pipe_script(compressor['path'], args, data)

            if len(data) < len(compressed):
                print('skipped', file=stderr)
                obj['expanded_sz'] = None
            else:
                percent = 0
                size = os.path.getsize(os.path.join(g_root_dir, path))
                if size != 0:
                    percent = len(compressed) / size
                print(f'done ({percent * 100:.1f}%)', file=stderr)
                obj['expanded_sz'] = len(data)
                data = compressed

        with open(os.path.join(g_cache_dir, path), 'wb') as f:
            f.write(data)

def run_preprocessors(objects: dict) -> bool:
    '''Run preprocessors on all objects as needed'''
    dirty = False
    for obj in objects.values():
        path = obj['path']

        if not obj.get('preprocess') and \
                not os.path.exists(os.path.join(g_cache_dir, path)):
            obj['preprocess'] = True

        if not obj.get('preprocess') and \
                not obj['transforms'].get('cache', True):
            obj['preprocess'] = True

        # if a file, check that the file is not newer
        if not obj.get('preprocess') and obj['type'] == 'file':
            root_mtime = os.path.getmtime(os.path.join(g_root_dir, path))
            cache_mtime = os.path.getmtime(os.path.join(g_cache_dir, path))
            if root_mtime > cache_mtime:
                obj['preprocess'] = True

        if obj.get('preprocess'):
            dirty |= True
            preprocess(obj)

    return dirty

def filter_objects(objects: dict) -> dict:
    '''Filter out directory paths if use_dirs is False'''
    if g_use_dirs:
        return objects
    return dict(filter(lambda obj: obj[1]['type'] != 'dir', objects.items()))

def save_state(file: str, objects: dict) -> None:
    '''Save current state'''
    options = {
        'align': g_align,
        'use_dirs': g_use_dirs,
    }
    data = {'options': options, 'paths': {}}

    for obj in objects.values():
        state = {
            'type': obj['type'],
        }
        if obj['transforms'] != {}:
            state['transforms'] = obj['transforms']

        if obj['type'] == 'file':
            if obj['compressor'] is not None:
                state['compressor'] = obj['compressor']
            if obj['expanded_sz'] is not None:
                state['expanded_sz'] = obj['expanded_sz']
        data['paths'][obj['path']] = state

    with open(file, 'w') as f:
        json.dump(data, f, indent=2)

### Stage 2 ###

def generate_dir_header(obj, paths) -> None:
    '''Generate header and data for a directory object'''
    if obj['path'] == '':
        depth = 0
    else:
        depth = 1 + obj['path'].count('/')

    encoded_path = obj['path'].encode('utf-8') + b'\0'
    path_len = len(encoded_path)

    children = []
    for path in paths:
        count = path.count('/')
        if count != depth:
            continue
        if path and count == 0 and depth == 0:
            children.append(path)
        elif path.startswith(obj['path'] + '/'):
            _, segment = path.rsplit('/', 1)
            children.append(segment)
    obj['children'] = children

    child_count = len(children)
    obj['size'] = s_sort_entry.size * child_count
    obj['header'] = s_dir.pack(s_dir.size, FROGFS_OBJ_TYPE_DIR, path_len,
            child_count) + encoded_path
    
def generate_file_header(obj) -> None:
    '''Generate header and load data for a file object'''
    encoded_path = obj['path'].encode('utf-8') + b'\0'
    path_len = len(encoded_path)

    obj['size'] = os.path.getsize(os.path.join(g_cache_dir, obj['path']))
    if obj.get('expanded_sz') is not None:
        method = obj['compressor'][0]
        compression = g_compressors[method]['id']
        if method == 'deflate':
            options = obj['compressor'][1].get('level', 9)
        elif method == 'heatshrink':
            window = obj['compressor'][1].get('window', 11)
            lookahead = obj['compressor'][1].get('lookahead', 4)
            options = lookahead << 4 | window

        obj['header'] = s_file_comp.pack(s_file_comp.size,
                FROGFS_OBJ_TYPE_FILE, path_len, obj['size'], compression,
                options, 0, obj['expanded_sz']) + encoded_path
    else:
        obj['header'] = s_file.pack(s_file.size,
                FROGFS_OBJ_TYPE_FILE, path_len, obj['size'], 0) + encoded_path

def align(n: int) -> int:
    '''Return n rounded up to the next alignment'''
    return ((n + g_align - 1) // g_align) * g_align

def pad(data: bytes | bytearray) -> bytes:
    '''Return data padded with zeros to next alignment'''
    return data.ljust(align(len(data)), b'\0')

def generate_frogfs_header(objects: dict) -> bytes:
    '''Generate FrogFS header and calculate object offsets'''
    num_objects = len(objects)

    binary_len = align(s_header.size) + align(s_hash_entry.size * num_objects)
    for obj in objects.values():
        obj['offset'] = binary_len
        binary_len += align(len(obj['header'])) + align(obj['size'])
    binary_len += s_footer.size

    flags = 0
    if g_use_dirs:
        flags = FROGFS_FLAG_DIRS

    return s_header.pack(FROGFS_MAGIC, s_header.size, FROGFS_VER_MAJOR,
            FROGFS_VER_MINOR, binary_len, num_objects, g_align, flags)

def generate_hashtable(objects: dict) -> bytes:
    '''Generate hashtable for objects'''
    objects = dict(sorted(objects.items(),
            key=lambda obj: (obj[1]['hash'], obj[0])))

    data = b''
    for obj in objects.values():
        data += s_hash_entry.pack(obj['hash'], obj['offset'])

    return data

def update_sorttables(objects: dict) -> None:
    '''Build sorttables for directory objects'''
    if not g_use_dirs:
        return

    for obj in objects.values():
        if obj['type'] != 'dir':
            continue

        data = b''
        for segment in obj['children']:
            if obj['path']:
                child = objects[obj['path'] + '/' + segment]
            else:
                child = objects[segment]
            data += s_sort_entry.pack(child['offset'])
        obj['data'] = data

def gather_data(objects):
    '''Gather all object data'''
    data = b''
    for obj in objects.values():
        data += pad(obj['header'])
        if obj['type'] == 'file':
            with open(os.path.join(g_cache_dir, obj['path']), 'rb') as f:
                data += pad(f.read())
        elif obj['type'] == 'dir':
            data += pad(obj['data'])
    return data

def generate_footer(data: bytes | bytearray) -> bytes:
    '''Generate FrogFS footer'''
    return s_footer.pack(crc32(data) & 0xFFFFFFFF)

if __name__ == '__main__':
    frogfs_dir = os.path.join(os.path.dirname(__file__), '..')
    frogfs_dir = os.path.abspath(frogfs_dir)
    g_tools_dir = os.path.join(frogfs_dir, 'tools')
    config_file = os.path.join(frogfs_dir, 'default_config.yaml')

    os.environ['FROGFS_DIR'] = frogfs_dir
    build_dir = os.environ.get('BUILD_DIR', os.getcwd())
    os.environ['NODE_PREFIX'] = build_dir
    os.environ['NODE_PATH'] = os.path.join(build_dir, 'node_modules') + \
            os.pathsep + g_tools_dir

    parser = ArgumentParser()
    parser.add_argument('--align', metavar='ALIGN',
                        help='data alignment, in bytes',
                        default=4)
    parser.add_argument('--config', metavar='CONFIG',
                        help='YAML configuration file', default=config_file)
    parser.add_argument('--dirs',
                        help='include directory entries',
                        action='store_true', default=False)
    parser.add_argument('root', metavar='ROOT', help='root directory')
    parser.add_argument('output', metavar='OUTPUT', help='output binary file')
    args = parser.parse_args()
 
    g_align = args.align
    config_file = args.config
    g_use_dirs = args.dirs
    g_root_dir = args.root
    output_file = args.output
    output_name, _ = os.path.splitext(os.path.basename(output_file))
    state_file = os.path.join(build_dir, output_name + '-state.json')
    g_cache_dir = os.path.join(build_dir, output_name + '-cache')
    g_transforms = collect_transforms()
    g_compressors = collect_compressors()

    print("       - Stage 1", file=stderr)
    objects = collect_objects()
    dirty = clean_removed(objects)
    dirty |= load_state(state_file, objects)
    config = load_config(config_file)
    for obj in objects.values():
        apply_rules(config['filters'], obj)
    dirty |= run_preprocessors(objects)
    objects = filter_objects(objects)
    dirty |= not os.path.exists(state_file) 
    dirty |= not os.path.exists(output_file)
    if not dirty:
        dirty |= os.path.getmtime(state_file) > os.path.getmtime(output_file)
    if not dirty:
        print("         - Nothing to do!", file=stderr)
        exit(0)
    save_state(state_file, objects)

    print("       - Stage 2", file=stderr)
    for obj in objects.values():
        if obj['type'] == 'dir':
            generate_dir_header(obj, objects.keys())
        if obj['type'] == 'file':
            generate_file_header(obj)
    
    data = generate_frogfs_header(objects)
    data += generate_hashtable(objects)
    update_sorttables(objects)
    data += gather_data(objects)
    data += generate_footer(data)

    with open(output_file, 'wb') as f:
        f.write(data)
    
    print("         - Output file written", file=stderr)
