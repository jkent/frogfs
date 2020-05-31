#!/usr/bin/env python

from argparse import ArgumentParser
from fnmatch import fnmatch
import gzip
import heatshrink2
import os
from struct import Struct
import subprocess
import sys
import yaml

script_dir = os.path.dirname(os.path.realpath(__file__))
header_struct = Struct('<IBBHII')

FLAG_LASTFILE = 1 << 0
FLAG_GZIP = 1 << 1
COMPRESS_NONE = 0
COMPRESS_HEATSHRINK = 1
ESPFS_MAGIC = 0x73665345

def add_file(config, root, filename, actions):
    filepath = os.path.join(root, filename)
    with open(filepath, 'rb') as f:
        initial_data = f.read()

    processed_data = initial_data

    for action in actions:
        if action in ['gzip', 'heatshrink']:
            pass
        elif action in config['tools']:
            tool = config['tools'][action]
            command = tool['command']
            p = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, shell=True)
            processed_data = p.communicate(input=processed_data)[0]
        else:
            print(f'Unknown action: {action}', file=sys.stderr)
            sys.exit(1)

    flags = 0
    if 'gzip' in actions:
        flags |= FLAG_GZIP
        tool = config['tools']['gzip']
        level = min(max(tool.get('level', 9), 0), 9)
        processed_data = gzip.compress(processed_data, level)

    if 'heatshrink' in actions:
        compression = COMPRESS_HEATSHRINK
        tool = config['tools']['heatshrink']
        level = min(max(tool.get('level', 9), 0), 9) // 2
        window_sizes, lookahead_sizes = [5, 6, 8, 11, 13],  [3, 3, 4, 4, 4]
        window_sz2, lookahead_sz2 = window_sizes[level], lookahead_sizes[level]
        header = bytes([window_sz2 << 4 | lookahead_sz2])
        compressed_data = header + heatshrink2.compress(processed_data, window_sz2=window_sz2, lookahead_sz2=lookahead_sz2)
    else:
        compression = COMPRESS_NONE
        compressed_data = processed_data

    if len(compressed_data) >= len(processed_data):
        compression = COMPRESS_NONE
        compressed_data = processed_data

    initial_len, processed_len, compressed_len = len(initial_data), len(processed_data), len(compressed_data)

    if initial_len < 1024:
        initial = f'{initial_len} B'
        compressed = f'{compressed_len} B'
    elif initial_len < 1024 * 1024:
        initial = f'{initial_len / 1024:.1f} KiB'
        compressed = f'{compressed_len / 1024:.1f} KiB'
    elif initial_len < 1024 * 1024 * 1024:
        initial = f'{initial_len / 1024 / 1024:.1f} MiB'
        compressed = f'{compressed_len / 1024 / 1024:.1f} MiB'

    percent = 100.0
    if initial_len > 0:
        percent = compressed_len / initial_len * 100

    filename = filename.replace('\\', '/')
    print(f'{filename}: {initial} -> {compressed} ({percent:.1f}%)')
    filename = filename.encode('utf8')
    filename = filename.ljust((len(filename) + 4) // 4 * 4, b'\0')
    compressed_data = compressed_data.ljust((compressed_len + 3) // 4 * 4, b'\0')

    header = header_struct.pack(ESPFS_MAGIC, flags, compression, len(filename), compressed_len, processed_len)
    return header + filename + compressed_data

def add_footer():
    return header_struct.pack(ESPFS_MAGIC, FLAG_LASTFILE, COMPRESS_NONE, 0, 0, 0)

def main():
    parser = ArgumentParser()
    parser.add_argument('ROOT')
    parser.add_argument('IMAGE')
    args = parser.parse_args()

    with open(os.path.join(script_dir, '..', 'espfs.yaml')) as f:
        config = yaml.load(f.read(), Loader=yaml.SafeLoader)

    user_config = None
    user_config_file = os.path.join(os.getenv('PROJECT_DIR', '.'), 'espfs.yaml')
    if os.path.exists(user_config_file):
        with open(user_config_file) as f:
            user_config = yaml.load(f.read(), Loader=yaml.SafeLoader)

    if user_config:
        for k, v in user_config.items():
            if k == 'tools':
                if 'tools' not in config:
                    config['tools'] = {}
                for k2, v2 in v.items():
                    config['tools'][k2] = v2
            elif k == 'process':
                if 'process' not in config:
                    config['process'] = {}
                for k2, v2 in v.items():
                    config['process'][k2] = v2
            elif k == 'skip':
                if 'tools' not in config:
                    config['tools'] = {}
                config['skip'] = v

    file_ops = {}
    for subdir, _, files in os.walk(args.ROOT):
        for file in files:
            filename = os.path.relpath(os.path.join(subdir, file), args.ROOT)
            if filename not in file_ops:
                file_ops[filename] = []
            if 'process' in config:
                for pattern, actions in config['process'].items():
                    if fnmatch(filename, pattern):
                        file_ops[filename].extend(actions)

    if 'skip' in config:
        for pattern in config['skip']:
            for filename in file_ops.copy().keys():
                if fnmatch(filename, pattern):
                    file_ops[filename] = []

    all_tools = set()
    for filename, tools in file_ops.items():
        all_tools.update(tools)

    for tool in all_tools:
        if tool in ['gzip', 'heatshrink']:
            continue
        if 'npm' in config['tools'][tool]:
            npm = config['tools'][tool]['npm']
            npms = npm if type(npm) == list else [npm]
            for npm in npms:
                if not os.path.exists(os.path.join('node_modules', npm)):
                    subprocess.check_call(f'npm install {npm}', shell=True)

        elif 'setup' in config[tools][tool]:
            setup = config['tools'][tool]['setup']
            setups = setup if type(setup) == list else [setup]
            for setup in setups:
                subprocess.check_call(setup, shell=True)

    data = b''
    for filename, actions in file_ops.items():
        data += add_file(config, args.ROOT, filename, actions)
    data += add_footer()

    with open(args.IMAGE, 'wb') as f:
        f.write(data)

if __name__ == '__main__':
    main()