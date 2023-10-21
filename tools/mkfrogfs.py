import json
import os
import zlib
from argparse import ArgumentParser
from fnmatch import fnmatch
from sys import stderr
from zlib import crc32

import format
import heatshrink2
import yaml

from frogfs import align, djb2_hash, pad, pipe_script


def collect_transforms() -> dict:
    '''Find transforms in frogfs' tools directory and in ${CWD}/tools'''
    xforms = {}
    for dir in (default_tools_dir, 'tools'):
        if not os.path.exists(dir):
            continue

        for file in os.listdir(dir):
            if file.startswith('transform-'):
                name, _ = os.path.splitext(file[10:]) # strip off 'transform-'
                path = os.path.join(dir, file)
                xforms[name] = {'path': path}
    return xforms

def collect_entries() -> dict:
    '''Collects all the path items from root directory'''
    entries = {}
    for dir, _, files in os.walk(root_dir, followlinks=True):
        reldir = os.path.relpath(dir, root_dir).replace('\\', '/')
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

### Stage 1 ###

def clean_removed() -> None:
    '''Check if files have been removed from root directory'''
    global dirty

    removed = False
    for dir, _, files in os.walk(cache_dir, topdown=False, followlinks=True):
        reldir = os.path.relpath(dir, cache_dir).replace('\\', '/')
        if reldir == '.' or reldir.startswith('./'):
            reldir = reldir[2:]
        for file in files:
            relfile = os.path.join(reldir, file).replace('\\', '/')
            if relfile not in entries.keys():
                os.unlink(os.path.join(cache_dir, relfile))
                removed = True
        if reldir not in entries.keys():
            os.rmdir(os.path.join(cache_dir, reldir))
            removed = True

    dirty |= removed

def load_state() -> None:
    '''Loads previous state or initializes a empty one'''
    global dirty

    paths = {}
    try:
        with open(state_file, 'r') as f:
            paths = json.load(f)
    except:
        dirty |= True

    for path, state in paths.items():
        if path not in entries:
            continue
        ent = entries[path]
        ent['xforms'] = state.get('xforms', {})
        ent['comp'] = state.get('comp')
        ent['real_sz'] = state.get('real_sz')

    for ent in entries.values():
        if 'xforms' not in ent:
            ent['preprocess'] = True
            dirty |= True

def load_config() -> None:
    '''Loads and normalizes configuration yaml file'''
    global config

    with open(config_file, 'r') as f:
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

def apply_rules() -> None:
    '''Applies preprocessing rules for entries'''
    for entry in entries.values():
        xforms = {}
        comp = None

        for filter, actions in config['filters'].items():
            if not fnmatch(entry['path'], filter):
                continue

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
                    if xforms.get(action, None) != None:
                        continue
                    if action == 'cache':
                        if disable:
                            xforms[action] = False
                    elif action == 'discard':
                        xforms[action] = not disable
                    elif action in transforms:
                        if entry['type'] == 'file':
                            xforms[action] = False if disable else args
                    else:
                        raise Exception(f'{action} is not a known transform')

                if compress != None:
                    if comp != None:
                        continue
                    if compress not in ('deflate', 'heatshrink'):
                        raise Exception(f'{compress} is not a valid compressor')
                    if entry['type'] == 'file':
                        comp = [compress, args]

        if entry.setdefault('xforms', {}) != xforms or \
                    entry['xforms'].keys() != xforms.keys():
            entry['xforms'] = xforms
            entry['preprocess'] = True

        if entry['type'] == 'file' and \
                entry.setdefault('comp', None) != comp:
            entry['comp'] = comp
            entry['preprocess'] = True

def preprocess(entry: dict) -> None:
    '''Run preprocessors for a given entry'''
    if 'discard' in entry['xforms']:
        return

    path = entry['path']

    if entry['type'] == 'file':
        print(f'         - {entry["path"]}', file=stderr)

        with open(os.path.join(root_dir, path), 'rb') as f:
            data = f.read()

        for name, args in entry['xforms'].items():
            if name == 'cache':
                continue

            print(f'           - {name}... ', file=stderr, end='', flush=True)
            transform = transforms[name]
            data = pipe_script(transform['path'], args, data)
            print('done', file=stderr)

        if entry['comp']:
            name, args = entry['comp']
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
                size = os.path.getsize(os.path.join(root_dir, path))
                if size != 0:
                    percent = len(compressed) / size
                print(f'done ({percent * 100:.1f}%)', file=stderr)
                entry['real_sz'] = len(data)
                data = compressed

        with open(os.path.join(cache_dir, path), 'wb') as f:
            f.write(data)

    elif entry['type'] == 'dir':
        if not os.path.exists(os.path.join(cache_dir, path)):
            os.mkdir(os.path.join(cache_dir, path))

def run_preprocessors() -> None:
    '''Run preprocessors on all entries as needed'''
    global dirty

    for entry in entries.values():
        path = entry['path']

        if not entry.get('preprocess') and \
                not os.path.exists(os.path.join(cache_dir, path)):
            entry['preprocess'] = True

        if not entry.get('preprocess') and \
                not entry['xforms'].get('cache', True):
            entry['preprocess'] = True

        # if a file, check that the file is not newer
        if not entry.get('preprocess') and entry['type'] == 'file':
            root_mtime = os.path.getmtime(os.path.join(root_dir, path))
            cache_mtime = os.path.getmtime(os.path.join(cache_dir, path))
            if root_mtime > cache_mtime:
                entry['preprocess'] = True

        if entry.get('preprocess'):
            dirty |= True
            preprocess(entry)

def check_output() -> None:
    '''Checks if the output file exists or is older than the state file'''
    global dirty

    dirty |= not os.path.exists(output_file)
    if not dirty:
        dirty |= os.path.getmtime(state_file) > os.path.getmtime(output_file)

    if not dirty:
        print("         - Nothing to do!", file=stderr)
        exit(0)

### Stage 2 ###

def save_state() -> None:
    '''Save current state'''
    paths = {}

    for entry in entries.values():
        state = {
            'type': entry['type'],
        }
        if entry['xforms'] != {}:
            state['xforms'] = entry['xforms']

        if entry['type'] == 'file':
            if entry['comp'] is not None:
                state['comp'] = entry['comp']
            if entry.get('real_sz') is not None:
                state['real_sz'] = entry['real_sz']
        paths[entry['path']] = state

    with open(state_file, 'w') as f:
        json.dump(paths, f, indent=4)

def generate_file_header(ent) -> None:
    '''Generate header and load data for a file entry'''
    seg = ent['seg'].encode('utf-8')

    data_sz = os.path.getsize(os.path.join(cache_dir, ent['path']))
    if ent.get('real_sz') is not None:
        method, args = ent['comp']
        if method == 'deflate':
            comp = 1
            opts = args.get('level', 9)
        elif method == 'heatshrink':
            comp = 2
            window = args.get('window', 11)
            lookahead = args.get('lookahead', 4)
            opts = lookahead << 4 | window

        header = bytearray(format.comp.size + len(seg))
        format.comp.pack_into(header, 0, 0, 0xFF00 | comp, len(seg), opts, 0,
                data_sz, ent['real_sz'])
        header[format.comp.size:] = seg
    else:
        header = bytearray(format.file.size + len(seg))
        format.file.pack_into(header, 0, 0, 0xFF00, len(seg), 0, 0, data_sz)
        header[format.file.size:] = seg

    ent['header'] = header
    ent['data_sz'] = data_sz

def generate_dir_header(ent: dict) -> None:
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

    header = bytearray(format.dir.size + (4 * child_count) + len(seg))
    format.dir.pack_into(header, 0, 0, child_count, len(seg), 0)
    header[format.dir.size + (4 * child_count):] = seg
    ent['header'] = header
    ent['data_sz'] = 0

def generate_entry_headers() -> None:
    '''Iterate entries and call their respective generate header function'''
    for entry in entries.values():
        if entry['type'] == 'file':
            generate_file_header(entry)
        elif entry['type'] == 'dir':
            generate_dir_header(entry)

def append_frogfs_header() -> None:
    '''Generate FrogFS header and calculate entry offsets'''
    global data

    num_ent = len(entries)

    bin_sz = align(format.head.size) + align(format.hash.size * num_ent)
    for entry in entries.values():
        entry['header_offs'] = bin_sz
        bin_sz += align(len(entry['header']))

    for entry in entries.values():
        entry['data_offs'] = bin_sz
        bin_sz += align(entry['data_sz'])

    bin_sz += format.foot.size

    data += format.head.pack(format.FROGFS_MAGIC, format.FROGFS_VER_MAJOR,
                             format.FROGFS_VER_MINOR, num_ent, bin_sz)

def apply_fixups() -> None:
    '''Insert offsets in dir and file headers'''
    for entry in entries.values():
        parent = entry['parent']
        if parent:
            format.offs.pack_into(entry['header'], 0, parent['header_offs'])
        if entry['type'] == 'file':
            format.offs.pack_into(entry['header'], 8, entry['data_offs'])
            continue
        for i, child in enumerate(entry['children']):
            offs = child['header_offs']
            format.offs.pack_into(entry['header'], format.dir.size + (i * 4),
                                  offs)

def append_hashtable() -> None:
    '''Generate hashtable for entries'''
    global data

    hashed_entries = {djb2_hash(k): v for k,v in entries.items()}
    hashed_entries = dict(sorted(hashed_entries.items(), key=lambda e: e))

    for hash, entry in hashed_entries.items():
        data += format.hash.pack(hash, entry['header_offs'])

def append_headers_and_files() -> None:
    global data

    # first append the entry headers
    for entry in entries.values():
        data += pad(entry['header'])

    # then append the file data
    for entry in entries.values():
        if entry['type'] == 'file':
            with open(os.path.join(cache_dir, entry['path']), 'rb') as f:
                data += pad(f.read())

def append_footer() -> None:
    '''Generate FrogFS footer'''
    global data

    data += format.foot.pack(crc32(data) & 0xFFFFFFFF)

def write_output() -> None:
    '''Write the data to the output file'''
    with open(output_file, 'wb') as f:
        f.write(data)

    print("         - Output file written", file=stderr)

if __name__ == '__main__':
    # Get and setup environment
    frogfs_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
    default_config_file = os.path.join(frogfs_dir, 'default_config.yaml')
    default_tools_dir = os.path.join(frogfs_dir, 'tools')
    os.environ['FROGFS_DIR'] = frogfs_dir
    cmakefiles_dir = os.environ.get('CMAKEFILES_DIR', os.getcwd())
    os.environ['NODE_PREFIX'] = cmakefiles_dir
    os.environ['NODE_PATH'] = os.path.join(cmakefiles_dir, 'node_modules') + \
                              os.pathsep + default_tools_dir

    # Get arguments
    argparse = ArgumentParser()
    argparse.add_argument('--config', metavar='CONFIG',
                        help='YAML configuration file',
                        default=default_config_file)
    argparse.add_argument('root', metavar='ROOT', help='input root directory')
    argparse.add_argument('output', metavar='OUTPUT', help='output binary file')
    args = argparse.parse_args()
    config_file = args.config
    root_dir = args.root
    output_file = args.output
    output_name, _ = os.path.splitext(os.path.basename(output_file))
    cache_dir = os.path.join(cmakefiles_dir, output_name + '-cache')
    state_file = os.path.join(cmakefiles_dir, output_name + '-cache-state.json')

    # Initial setup
    dirty = False
    data = b''
    transforms = collect_transforms()
    entries = collect_entries()

    # Stage 1
    print("       - Stage 1", file=stderr)
    clean_removed()
    load_state()
    load_config()
    apply_rules()
    run_preprocessors()
    check_output()

    # Stage 2
    print("       - Stage 2", file=stderr)
    save_state()
    generate_entry_headers()
    append_frogfs_header()
    append_hashtable()
    apply_fixups()
    append_headers_and_files()
    append_footer()
    write_output()
