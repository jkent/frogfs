import json
import os
from argparse import ArgumentParser
from fnmatch import fnmatch
from shutil import rmtree, which
from struct import Struct
from subprocess import PIPE, Popen
from sys import executable, stderr
from time import time
from types import SimpleNamespace
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

# File compression
FROGFS_COMP_NONE        = 0
FROGFS_COMP_DEFLATE     = 1
FROGFS_COMP_HEATSHRINK  = 2

def init():
    '''Performs initialization tasks
    
      * Global variable g
      * Paths
      * Environment variables
      * Command line arguments
      * Finds transforms
      * Finds compressors
      * Struct formats
    '''
    global g

    g = SimpleNamespace()

    g.frogfs_dir = os.path.join(os.path.dirname(__file__), '..')
    g.frogfs_dir = os.path.abspath(g.frogfs_dir)
    os.environ['FROGFS_DIR'] = g.frogfs_dir

    g.build_dir = os.environ.get('BUILD_DIR', os.getcwd())
    os.environ['NODE_PREFIX'] = g.build_dir
    os.environ['NODE_PATH'] = os.path.join(g.build_dir, 'node_modules')

    g.config_file = os.path.join(g.frogfs_dir, 'default_config.yaml')
    g.tools_dir = os.path.join(g.frogfs_dir, 'tools')

    parser = ArgumentParser()
    parser.add_argument('--align', metavar='ALIGN',
                        help='data alignment, in bytes',
                        default=4)
    parser.add_argument('--config', metavar='CONFIG',
                        help='YAML configuration file', default=g.config_file)
    parser.add_argument('--dirs',
                        help='include directory entries',
                        action='store_true', default=False)
    parser.add_argument('root', metavar='ROOT', help='root directory')
    parser.add_argument('output', metavar='OUTPUT', help='output binary file')
    args = parser.parse_args()
 
    g.align = args.align
    g.config_file = args.config
    g.use_dirs = args.dirs
    g.root_dir = args.root
    g.output_file = args.output

    g.options = {
        'align': g.align,
        'use_dirs': g.use_dirs,
    }

    output_name, _ = os.path.splitext(os.path.basename(g.output_file))
    g.cache_dir = os.path.join(g.build_dir, output_name + '-cache')
    g.state_file = os.path.join(g.build_dir, output_name + '-state.json')

def collect_transforms():
    '''Find transforms in frogfs' tools directory and in ${CWD}/tools'''
    g.transforms = {}
    for path in (g.tools_dir, 'tools'):
        if not os.path.exists(path):
            continue

        for file in os.listdir(path):
            if file.startswith('transform-'):
                name, _ = os.path.splitext(file[10:])
                filepath = os.path.join(path, file)
                g.transforms[name] = {'path': filepath}

def collect_compressors():
    '''Find compressors in frogfs' tools directory and in ${CWD}/tools'''
    g.compressors = {}
    for path in (g.tools_dir, 'tools'):
        if not os.path.exists(path):
            continue

        for file in os.listdir(path):
            if file.startswith('compress-'):
                name, _ = os.path.splitext(file[9:])
                filepath = os.path.join(path, file)
                g.compressors[name] = {'path': filepath, 'id': None}

def init_structs():
    '''Initialize struct formats'''
    # Setup structs
    g.s = SimpleNamespace()
    
    # FrogFS header
    # magic, len, ver_major, ver_minor, bin_len, num_objs, align, flags
    g.s.header = Struct('<IBBHIHBB')

    # Hash table entry
    # hash, offset
    g.s.hash_entry = Struct('<II')

    # Object header
    # len, type, path_len
    g.s.object = Struct('<BBH')

    # Directory header
    # len, type, path_len, child_count
    g.s.dir = Struct('<BBHH')

    # Sort table entry
    # offset
    g.s.sort_entry = Struct('<I')

    # File header
    # len, type, path_len, data_len, compression
    g.s.file = Struct('<BBHIB')

    # Compressed file header
    # len, type, path_len, data_len, compression, options, res, expanded_len
    g.s.file_comp = Struct('<BBHIBBHI')

    # FrogFS footer
    # crc32
    g.s.footer = Struct('<I')

def djb2_hash(s):
    '''A string hashing algorithm'''
    hash = 5381
    for c in s.encode('utf-8'):
        hash = ((hash << 5) + hash ^ c) & 0xFFFFFFFF
    return hash

def align(n):
    '''Return n rounded up to the next alignment'''
    return ((n + g.align - 1) // g.align) * g.align

def pad(data):
    '''Return data padded with zeros to next alignment'''
    return data.ljust(align(len(data)), b'\0')

def load_config():
    '''Loads and normalizes configuration yaml file'''
    with open(g.config_file, 'r') as f:
        doc = yaml.safe_load(f)

    g.config = {'filters': {}}
    for path, filters in doc['filters'].items():
        g.config['filters'][path] = {}
        for filter in filters:
            if isinstance(filter, str):
                filter_name = filter
                g.config['filters'][path][filter_name] = {}
            elif isinstance(filter, dict):
                for filter_name, args in filter.items():
                    if not isinstance(args, dict):
                        args = {}
                    g.config['filters'][path][filter_name] = args

def apply_rules(path, object):
    '''Applies preprocessing rules for a single object'''
    transforms = {}
    compressor = None

    for filter, actions in g.config['filters'].items():
        if fnmatch(path, filter):
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
                    elif action in g.transforms:
                        if object['type'] == 'file':
                            transforms[action] = False if disable else args
                    else:
                        raise Exception(f'{action} is not a known transform')

                if compress != None:
                    if compressor != None:
                        continue
                    if object['type'] == 'file':
                        compressor = [compress, args]

    object['transforms'] = transforms
    object['compressor'] = compressor

def load_state():
    '''Initializes a empty state or loads an existing one'''
    g.state = {'options': g.options, 'paths': {}}
    if not os.path.exists(g.state_file):
        g.dirty = True
        return

    with open(g.state_file, 'r') as f:
        g.state = json.load(f)

def collect_paths():
    '''Collects all the path items from root directory'''
    paths = {}
    for dir, _, files in os.walk(g.root_dir, followlinks=True):
        reldir = os.path.relpath(dir, g.root_dir).replace('\\', '/') \
                .lstrip('.').lstrip('/')
        absdir = os.path.abspath(dir)
        paths[reldir] = {
            'path': reldir,
            'hash': djb2_hash(reldir),
            'type': 'dir',
        }
        for file in files:
            relfile = os.path.join(reldir, file).replace('\\', '/')
            absfile = os.path.join(absdir, file)
            paths[relfile] = {
                'path': relfile,
                'hash': djb2_hash(relfile),
                'type': 'file',
                'len': os.path.getsize(absfile),
                'compression': g.state['paths'].get(relfile, {}) \
                        .get('compressor'),
                'mtime': g.state['paths'].get(relfile, {}).get('mtime', 0),
                'uncompressed_len': g.state['paths'].get(relfile, {}) \
                        .get('uncompressed_len')
            }
    g.paths = {k: paths[k] for k in sorted(paths.keys())}

    for path, object in g.paths.items():
        apply_rules(path, object)

def check_removed():
    '''Check if files have been removed from root directory and mark dirty'''
    g.dirty = False
    if not os.path.isdir(g.cache_dir):
        return

    for dir, _, files in os.walk(g.cache_dir, topdown=False, followlinks=True):
        reldir = os.path.relpath(dir, g.cache_dir).replace('\\', '/') \
                .lstrip('.').lstrip('/')
        for file in files:
            relfile = os.path.join(reldir, file).replace('\\', '/')
            if relfile not in g.paths:
                os.unlink(os.path.join(g.cache_dir, relfile))
                g.dirty = True
        if reldir not in g.paths:
            os.rmdir(os.path.join(g.cache_dir, reldir))
            g.dirty = True

def pipe_script(script, args, data):
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

def preprocess(path, object):
    '''Run preprocessors for a given object'''
    if 'discard' in object['transforms']:
        return

    if object['type'] == 'dir':
        if os.path.exists(os.path.join(g.cache_dir, path)):
            rmtree(os.path.join(g.cache_dir, path))
        os.mkdir(os.path.join(g.cache_dir, path))
    elif object['type'] == 'file':
        print(f'       - {path}', file=stderr)

        with open(os.path.join(g.root_dir, path), 'rb') as f:
            data = f.read()

        for name, args in object['transforms'].items():
            if name == 'cache':
                continue

            print(f'         - {name}... ', file=stderr, end='', flush=True)
            script = g.transforms[name]['path']
            data = pipe_script(script, args, data)
            print('done', file=stderr)

        if object['compressor']:
            name, args = object['compressor']
            print(f'         - compress {name}... ', file=stderr, end='',
                    flush=True)

            script = g.compressors[name]['path']
            if g.compressors[name]['id'] == None:
                id = pipe_script(script, {'id': None}, '')
                g.compressors[name]['id'] = int(id)
            compressed = pipe_script(script, args, data)

            if len(data) < len(compressed):
                print('skipped', file=stderr)
            else:
                percent = 0
                if object['len'] != 0:
                    percent = len(compressed) / object['len']
                print(f'done ({percent * 100:.1f}%)', file=stderr)
                object['uncompressed_len'] = len(data)
                data = compressed
                object['compressed'] = True

        with open(os.path.join(g.cache_dir, path), 'wb') as f:
            f.write(data)

        object['mtime'] = os.path.getmtime(os.path.join(g.cache_dir, path))

def run_preprocessors():
    '''Run preprocessors on all objects as needed'''
    for path, object in g.paths.items():
        skip = True

        if skip and g.options != g.state['options']:
            skip = False

        if skip and path not in g.state['paths']:
            skip = False
        
        if skip and not g.state['paths'][path]['transforms'].get('cache', True):
            skip = False

        # if a file, check that the file is not newer
        if skip and g.paths[path]['type'] == 'file':
            mtime = os.path.getmtime(os.path.join(g.root_dir, path))
            if mtime > g.state['paths'][path]['mtime']:
                skip = False

        # check that we have the same transforms, in the same order
        if skip and g.state['paths'][path]['transforms'].keys() != \
                object['transforms'].keys():
            skip = False

        # check that we have the same arguments to the transforms
        if skip and g.state['paths'][path]['transforms'] != \
                    object['transforms']:
            skip = False
        
        # check that we have the same compressor
        if skip and g.paths[path]['type'] == 'file':
            if g.state['paths'][path]['compressor'] != object['compressor']:
                skip = False

        if not skip:
            preprocess(path, object)

def filter_paths():
    '''Remove directory paths if use_dirs is False'''
    g.paths = g.paths if g.use_dirs else \
            dict(filter(lambda o: o[1]['type'] != 'dir', g.paths.items()))

def update_state():
    '''If changes were detected, remove output file and save current state'''
    state = {'options': g.options, 'paths': {}}
    for path, object in g.paths.items():
        state['paths'][path] = {
            'type': object['type'],
            'transforms': object['transforms'],
        }

        if object['type'] == 'file':
            state['paths'][path]['mtime'] = object['mtime']
            state['paths'][path]['compressor'] = object['compressor']
            state['paths'][path]['uncompressed_len'] = \
                    object['uncompressed_len']

    if state != g.state or g.dirty:
        g.state = state
        with open(g.state_file, 'w') as f:
            json.dump(g.state, f, indent=4)
    else:
        exit(0)

def generate_dir_object(path, object):
    '''Generate header and data for a directory object'''
    if path == '':
        depth = 0
    else:
        depth = 1 + path.count('/')

    encoded_path = path.encode('utf-8') + b'\0'
    path_len = len(encoded_path)

    object['children'] = []
    for path2 in g.paths.keys():
        count = path2.count('/')
        if count != depth:
            continue
        if path2 and count == 0 and depth == 0:
            object['children'].append(path2)
        elif path2.startswith(path + '/'):
            _, segment = path2.rsplit('/', 1)
            object['children'].append(segment)

    child_count = len(object['children'])
    object['size'] = g.s.sort_entry.size * child_count
    object['header'] = g.s.dir.pack(g.s.dir.size, FROGFS_OBJ_TYPE_DIR,
            path_len, child_count) + encoded_path
    
def generate_file_object(path, object):
    '''Generate header and load data for a file object'''
    encoded_path = path.encode('utf-8') + b'\0'
    path_len = len(encoded_path)

    object['size'] = os.path.getsize(os.path.join(g.cache_dir, path))
    if object.get('compressed', False):
        method = object['compressor'][0]
        if method == 'deflate':
            compression = FROGFS_COMP_DEFLATE
            options = object['compressor'][1].get('level', 9)
        elif method == 'heatshrink':
            compression = FROGFS_COMP_HEATSHRINK
            window = object['compressor'][1].get('window', 11)
            lookahead = object['compressor'][1].get('lookahead', 4)
            options = lookahead << 4 | window

        object['header'] = g.s.file_comp.pack(g.s.file_comp.size,
                FROGFS_OBJ_TYPE_FILE, path_len, object['size'], compression,
                options, 0, object['uncompressed_len']) + encoded_path
    else:
        object['header'] = g.s.file.pack(g.s.file.size,
                FROGFS_OBJ_TYPE_FILE, path_len, object['size'],
                FROGFS_COMP_NONE) + encoded_path

def generate_objects():
    '''Generate header and data for all objects'''
    for path, object in g.paths.items():
        if object['type'] == 'dir':
            generate_dir_object(path, object)
        elif object['type'] == 'file':
            generate_file_object(path, object)
        else:
            raise Exception('unhandled object type!')

def generate_header():
    '''Generate FrogFS header and calculate object offsets'''
    num_objects = len(g.paths)

    binary_len = align(g.s.header.size) + \
            align(g.s.hash_entry.size * num_objects)
    for object in g.paths.values():
        object['offset'] = binary_len
        binary_len += align(len(object['header'])) + align(object['size'])
    binary_len += g.s.footer.size

    flags = 0
    if g.use_dirs:
        flags = FROGFS_FLAG_DIRS

    g.data = g.s.header.pack(FROGFS_MAGIC, g.s.header.size, FROGFS_VER_MAJOR,
            FROGFS_VER_MINOR, binary_len, num_objects, g.align, flags)

def generate_hashtable():
    '''Generate hashtable for objects'''
    objects = dict(sorted(map(lambda s: (g.paths[s]['hash'], g.paths[s]),
            g.paths)))

    data = b''
    for hash, object in objects.items():
        data += g.s.hash_entry.pack(hash, object['offset'])

    g.data += pad(data)

def update_sorttables():
    '''Build sorttables for directory objects'''
    if not g.use_dirs:
        return

    for path, object in g.paths.items():
        if object['type'] != 'dir':
            continue

        data = b''
        for segment in object['children']:
            if path:
                child = g.paths[path + '/' + segment]
            else:
                child = g.paths[segment]
            data += g.s.sort_entry.pack(child['offset'])
        object['data'] = data

def append_data():
    '''Append all object header and data items'''
    for path, object in g.paths.items():
        g.data += pad(object['header'])
        if object['type'] == 'file':
            with open(os.path.join(g.cache_dir, path), 'rb') as f:
                g.data += pad(f.read())
        elif object['type'] == 'dir':
            g.data += pad(object['data'])

def generate_footer():
    '''Generate and append FrogFS footer'''
    g.data += g.s.footer.pack(crc32(g.data) & 0xFFFFFFFF)

def write_output():
    '''Save the generated binary'''
    with open(g.output_file, 'wb') as f:
        f.write(g.data)

def main():
    init()
    collect_transforms()
    collect_compressors()
    init_structs()
    load_config()
    load_state()
    collect_paths()
    check_removed()
    run_preprocessors()
    filter_paths()
    update_state()
    generate_objects()
    generate_header()
    generate_hashtable()
    update_sorttables()
    append_data()
    generate_footer()
    write_output()

if __name__ == '__main__':
    main()
