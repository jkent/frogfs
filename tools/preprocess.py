import json
import os
from argparse import ArgumentParser
from fnmatch import fnmatch
from shutil import rmtree
from subprocess import PIPE, Popen
from sys import executable, stderr
from time import time

import yaml

frogfs_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
default_config_path = os.path.join(frogfs_dir, 'default_config.yaml')
frogfs_tools_dir = os.path.join(frogfs_dir, 'tools')

os.environ['FROGFS_DIR'] = frogfs_dir
os.environ['NODE_PREFIX'] = os.environ['BUILD_DIR']
os.environ['NODE_PATH'] = os.path.join(os.environ['BUILD_DIR'], 'node_modules')

def load_config(path):
    global config

    with open(path, 'r') as f:
        doc = yaml.safe_load(f)

    config = {'filters': {}}
    for path, filters in doc['filters'].items():
        config['filters'][path] = {}
        for filter in filters:
            if isinstance(filter, str):
                filter_name = filter
                config['filters'][path][filter_name] = {}
            elif isinstance(filter, dict):
                for filter_name, args in filter.items():
                    if not isinstance(args, dict):
                        args = {}
                    config['filters'][path][filter_name] = args

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

def load_paths():
    global path_list

    path_list = []
    for dir, _, files in os.walk(root, followlinks=True):
        reldir = os.path.relpath(dir, root).replace('\\', '/').lstrip('.') \
                .lstrip('/')
        absdir = os.path.abspath(dir)
        if os.path.exists(absdir):
            path_list.append(reldir)

        for file in files:
            relfile = os.path.join(reldir, file).replace('\\', '/').lstrip('/')
            absfile = os.path.join(absdir, file)
            if os.path.exists(absfile):
                path_list.append(relfile)

    path_list.sort()

def load_state():
    global state

    state = {}
    if os.path.exists(output + '.json'):
        with open(output + '.json', 'r') as f:
            state = json.load(f)

def save_state():
    with open(output + '.json', 'w') as f:
        json.dump(new_state, f, indent=4)

def get_rules(path):
    is_dir = os.path.isdir(os.path.join(root, path))

    rules = {}
    for filter, actions in config['filters'].items():
        if fnmatch(path, filter):
            for action, args in actions.items():
                action = action.split()
                disable = False
                if action[0] == 'no':
                    action = action[1:]
                    disable = True
                name = action[0]

                if not disable and rules.get(name) != None:
                    continue

                if name == 'cache':
                    if disable:
                        rules[name] = False
                elif name == 'discard':
                    rules[name] = not disable
                elif not is_dir:
                    if name == 'compress':
                        if disable:
                            rules[name] = False
                            continue
                        pp = action[1]
                        if pp not in pp_dict:
                            raise Exception(f'{pp} is unknown')
                        if pp_dict[pp]['type'] != 'compressor':
                            raise Exception('f{pp} is not a known compressor')
                        rules[name] = {pp: args}
                    else:
                        pp = action[0]
                        if pp not in pp_dict:
                            raise Exception(f'{pp} is unknown')
                        if pp_dict[pp]['type'] != 'transformer':
                            raise Exception('f{pp} is not a known transformer')
                        rules[pp] = False if disable else args

    return rules

def preprocess(path, rules):
    entry = {
        'mtime': time(),
        'rules': rules.copy(),
    }
    if 'discard' in rules:
        new_state[path] = entry
        return

    if os.path.isdir(os.path.join(root, path)):
        if os.path.exists(os.path.join(output, path)):
            rmtree(os.path.join(output, path))
        os.mkdir(os.path.join(output, path))
    else:
        print(f'       - {path}', file=stderr)

        with open(os.path.join(root, path), 'rb') as f:
            data = f.read()

        for action, args in rules.items():
            if action in ('cache', 'compress'):
                continue
            else:
                print(f'         - {action}', file=stderr)
                script = pp_dict[action]['file']
                data = pipe_script(script, args, data)

        with open(os.path.join(output, path), 'wb') as f:
            f.write(data)

    entry['mtime'] = os.path.getmtime(os.path.join(output, path))
    new_state[path] = entry

def pipe_script(script, args, data):
    _, ext = os.path.splitext(script)
    if ext == '.js':
        command = ['node']
    elif ext == '.py':
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
    global root, output
    global new_state

    parser = ArgumentParser()
    parser.add_argument('--config', metavar='CONFIG',
                        help='YAML configuration file',
                        default=default_config_path)
    parser.add_argument('root', metavar='ROOT', help='root directory')
    parser.add_argument('output', metavar='OUTPUT', help='output directory')
    args = parser.parse_args()

    root = args.root
    output = args.output

    load_config(args.config)
    load_preprocessors()
    load_paths()
    load_state()

    removed = False
    if os.path.isdir(output):
        for dir, _, files in os.walk(output, topdown=False,
                                        followlinks=True):
            reldir = os.path.relpath(dir, output).replace('\\', '/') \
                    .lstrip('.').lstrip('/')
            for file in files:
                relfile = os.path.join(reldir, file).replace('\\', '/') \
                        .lstrip('/')
                if relfile not in path_list:
                    os.unlink(os.path.join(output, relfile))
                    removed = True
            if reldir not in path_list:
                os.rmdir(os.path.join(output, reldir))
                removed = True

    new_state = {}
    for path in path_list:
        rules = get_rules(path)

        if path not in state or not state[path]['rules'].get('cache', True):
            preprocess(path, rules)
            continue

        if not os.path.exists(os.path.join(output, path)):
            preprocess(path, rules)
            continue

        if os.path.getmtime(os.path.join(root, path)) > state[path]['mtime']:
            if not os.path.isdir(os.path.join(root, path)):
                preprocess(path, rules)
                continue

        if state[path]['rules'] != rules:
            preprocess(path, rules)
            continue

        if state[path]['rules'].keys() != rules.keys():
            preprocess(path, rules)
            continue

        new_state[path] = state[path]

    if state != new_state or removed:
        save_state()

if __name__ == '__main__':
    main()
