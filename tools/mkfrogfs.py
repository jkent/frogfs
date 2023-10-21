import json
import os
import zlib
from argparse import ArgumentParser
from fnmatch import fnmatch
from shutil import rmtree
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
        ent = {
            'path': reldir,
            'seg': os.path.basename(reldir),
            'type': 'dir',
        }
        entries[reldir] = ent

        for file in files:
            relfile = os.path.join(reldir, file).replace('\\', '/')
            ent = {
                'path': relfile,
                'seg': file,
                'type': 'file',
            }
            entries[relfile] = ent

    return dict(sorted(entries.items()))

def load_config() -> dict:
    '''Loads and normalizes configuration yaml file'''
    with open(config_file, 'r') as f:
        doc = yaml.safe_load(f)

    config = {'filters': []}
    for filter in doc['filters']:
        for pattern, actions in filter.items():
            normalized_actions = []
            for action in actions:
                if isinstance(action, dict):
                    action = tuple(next(iter(action.items())))
                if isinstance(action, str):
                    action = (action, {})
                normalized_actions.append(action)
            config['filters'].append((pattern, normalized_actions))

    return config

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
        ent['comp'] = state.get('comp')
        ent['discard'] = state.get('discard')
        ent['real_sz'] = state.get('real_sz')
        ent['skip'] = True
        ent['xforms'] = state.get('xforms', {})

def apply_rules() -> None:
    '''Applies preprocessing rules for entries'''
    global dirty

    for ent in tuple(entries.values()):
        path = ent['path']
        xforms = {}
        comp = None

        for filter, actions in config['filters']:
            if not fnmatch(path, filter):
                continue
            for action, args in actions:
                parts = action.split()
                enable = True

                if parts[0] == 'no':
                    enable = False
                    parts = parts[1:]

                verb = parts[0]
                if verb == 'discard' and enable:
                    discards[path] = ent
                    continue

                if verb == 'cache':
                    ent['cache'] = enable
                    continue

                if verb == 'compress':
                    if ent['type'] == 'dir':
                        continue
                    if not enable:
                        comp = None
                        continue
                    if parts[1] in ('deflate', 'heatshrink'):
                        comp = [parts[1], args]
                        continue
                    raise Exception(f'{parts[1]} is not a valid compress type')


                if verb in transforms:
                    xforms[verb] = args if enable else False
                    continue

                raise Exception(f'{verb} is not a known transform')

        # if never seen before, preprocess
        if ent.get('xforms') is None:
            ent['xforms'] = {}
            ent['skip'] = False

        # if transforms changed, apply and preprocess
        if ent['xforms'] != xforms or ent['xforms'].keys() != xforms.keys():
            ent['xforms'] = xforms
            ent['skip'] = False

        # if is file and compression changed, apply and preprocess
        if ent['type'] == 'file' and ent.setdefault('comp', None) != comp:
            ent['comp'] = comp
            ent['skip'] = False

def preprocess(ent: dict) -> None:
    '''Run preprocessors for a given entry'''
    global dirty

    path = ent['path']

    for discard in discards.values():
        if path == discard['path']:
            return
        if discard['type'] != 'dir':
            continue
        if path.startswith(discard['path'] + '/'):
            return

    if ent['type'] == 'file':
        print(f'         - {ent["path"]}', file=stderr)

        with open(os.path.join(root_dir, path), 'rb') as f:
            data = f.read()

        for name, args in ent['xforms'].items():
            print(f'           - {name}... ', file=stderr, end='', flush=True)
            transform = transforms[name]
            data = pipe_script(transform['path'], args, data)
            print('done', file=stderr)

        if ent['comp']:
            name, args = ent['comp']
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
                ent['real_sz'] = None
            else:
                percent = 0
                size = os.path.getsize(os.path.join(root_dir, path))
                if size != 0:
                    percent = len(compressed) / size
                print(f'done ({percent * 100:.1f}%)', file=stderr)
                ent['real_sz'] = len(data)
                data = compressed

        with open(os.path.join(cache_dir, path), 'wb') as f:
            f.write(data)

    elif ent['type'] == 'dir':
        if not os.path.exists(os.path.join(cache_dir, path)):
            os.mkdir(os.path.join(cache_dir, path))

    dirty |= True

def run_preprocessors() -> None:
    '''Run preprocessors on all entries as needed'''
    global dirty

    for ent in entries.values():
        path = ent['path']

        # if file is not in cache, preprocess
        if ent['skip'] and not os.path.exists(os.path.join(cache_dir, path)):
            ent['skip'] = False

        # if file marked to be cached (default), preprocess
        if ent['skip'] and ent.get('cache') == False:
            ent['skip'] = False

        # if a file, check that the file is not newer
        if ent['skip'] and ent['type'] == 'file':
            root_mtime = os.path.getmtime(os.path.join(root_dir, path))
            cache_mtime = os.path.getmtime(os.path.join(cache_dir, path))
            if root_mtime > cache_mtime:
                ent['skip'] = False

        # if entry is not marked skip, preprocess
        if not ent['skip']:
            preprocess(ent)

        # mark dirty if something has been marked discard
        if path in discards.keys() and not ent.get('discard'):
            dirty |= True

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

    for ent in entries.values():
        path = ent['path']

        skip = False
        for discard in discards.values():
            if discard['type'] != 'dir':
                continue
            if path.startswith(discard['path'] + '/'):
                skip = True
                break
        if skip:
            continue

        state = {
            'type': ent['type'],
        }
        if path in discards.keys():
            state['discard'] = True
        else:
            if ent['xforms'] != {}:
                state['xforms'] = ent['xforms']
            if ent['type'] == 'file':
                if ent['comp'] is not None:
                    state['comp'] = ent['comp']
                if ent.get('real_sz') is not None:
                    state['real_sz'] = ent['real_sz']
        paths[path] = state

    for ent in tuple(entries.values()):
        path = ent['path']
        for discard in discards.values():
            if path == discard['path']:
                del entries[path]
            if discard['type'] == 'dir':
                if path.startswith(discard['path'] + '/'):
                    del entries[path]

    with open(state_file, 'w') as f:
        json.dump(paths, f, indent=4)

def generate_file_header(ent) -> None:
    '''Generate header and load data for a file entry'''
    seg = ent['seg'].encode('utf-8')

    data_sz = os.path.getsize(os.path.join(cache_dir, ent['path']))
    if ent.get('comp') and ent.get('real_sz') is not None:
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

def generate_dir_header(dirent: dict) -> None:
    '''Generate header and data for a directory entry'''
    if dirent['path'] == '':
        dirent['parent'] = None
        depth = 0
    else:
        depth = dirent['path'].count('/') + 1

    seg = dirent['seg'].encode('utf-8')

    children = []
    for path, ent in entries.items():
        count = path.count('/')
        if count != depth or not ent['path']:
            continue
        if (count == 0 and depth == 0) or path.startswith(dirent['path'] + '/'):
            children.append(ent)
            if ent['path']:
                ent['parent'] = dirent

    dirent['children'] = children
    child_count = len(children)

    header = bytearray(format.dir.size + (4 * child_count) + len(seg))
    format.dir.pack_into(header, 0, 0, child_count, len(seg), 0)
    header[format.dir.size + (4 * child_count):] = seg
    dirent['header'] = header
    dirent['data_sz'] = 0

def generate_entry_headers() -> None:
    '''Iterate entries and call their respective generate header funciton'''
    for ent in entries.values():
        if ent['type'] == 'file':
            generate_file_header(ent)
        elif ent['type'] == 'dir':
            generate_dir_header(ent)

def append_frogfs_header() -> None:
    '''Generate FrogFS header and calculate entry offsets'''
    global data

    num_ent = len(entries)

    bin_sz = align(format.head.size) + align(format.hash.size * num_ent)
    for ent in entries.values():
        ent['header_offs'] = bin_sz
        bin_sz += align(len(ent['header']))

    for ent in entries.values():
        ent['data_offs'] = bin_sz
        bin_sz += align(ent['data_sz'])

    bin_sz += format.foot.size

    data += format.head.pack(format.FROGFS_MAGIC, format.FROGFS_VER_MAJOR,
                             format.FROGFS_VER_MINOR, num_ent, bin_sz)

def apply_fixups() -> None:
    '''Insert offsets in dir and file headers'''
    for ent in entries.values():
        parent = ent['parent']
        if parent:
            format.offs.pack_into(ent['header'], 0, parent['header_offs'])
        if ent['type'] == 'file':
            format.offs.pack_into(ent['header'], 8, ent['data_offs'])
            continue
        for i, child in enumerate(ent['children']):
            offs = child['header_offs']
            format.offs.pack_into(ent['header'], format.dir.size + (i * 4),
                                  offs)

def append_hashtable() -> None:
    '''Generate hashtable for entries'''
    global data

    hashed_entries = {djb2_hash(k): v for k,v in entries.items()}
    hashed_entries = dict(sorted(hashed_entries.items(), key=lambda e: e))

    for hash, ent in hashed_entries.items():
        data += format.hash.pack(hash, ent['header_offs'])

def append_headers_and_files() -> None:
    global data

    # first append the entry headers
    for ent in entries.values():
        data += pad(ent['header'])

    # then append the file data
    for ent in entries.values():
        if ent['type'] == 'file':
            with open(os.path.join(cache_dir, ent['path']), 'rb') as f:
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
    transforms = collect_transforms()
    entries = collect_entries()
    config  = load_config()
    discards = {}
    dirty = False
    data = b''

    # Stage 1
    print("       - Stage 1", file=stderr)
    clean_removed()
    load_state()
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
