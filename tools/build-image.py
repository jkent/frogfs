#!/usr/bin/env python3

import os
from pathlib import Path
import shutil
import subprocess
import sys

ESPFS_IMAGEROOTDIR = sys.argv[1]

CMAKE_CURRENT_BINARY_DIR = os.environ.get('CMAKE_CURRENT_BINARY_DIR')
CONFIG_ESPFS_PREPROCESS_FILES = os.environ.get('CONFIG_ESPFS_PREPROCESS_FILES')
CONFIG_ESPFS_CSS_MINIFY_UGLIFYCSS = os.environ.get('CONFIG_ESPFS_CSS_MINIFY_UGLIFYCSS')
CONFIG_ESPFS_HTML_MINIFY_HTMLMINIFIER = os.environ.get('CONFIG_ESPFS_HTML_MINIFY_HTMLMINIFIER')
CONFIG_ESPFS_JS_CONVERT_BABEL = os.environ.get('CONFIG_ESPFS_JS_CONVERT_BABEL')
CONFIG_ESPFS_JS_MINIFY_BABEL = os.environ.get('CONFIG_ESPFS_JS_MINIFY_BABEL')
CONFIG_ESPFS_JS_MINIFY_UGLIFYJS = os.environ.get('CONFIG_ESPFS_JS_MINIFY_UGLIFYJS')
CONFIG_ESPFS_UGLIFYCSS_PATH = os.environ.get('CONFIG_ESPFS_UGLIFYCSS_PATH}') or 'uglifycss'
CONFIG_ESPFS_HTMLMINIFIER_PATH = os.environ.get('CONFIG_ESPFS_HTMLMINIFIER_PATH') or 'html-minifier'
CONFIG_ESPFS_BABEL_PATH = os.environ.get('CONFIG_ESPFS_BABEL_PATH') or 'babel'
CONFIG_ESPFS_UGLIFYJS_PATH = os.environ.get('CONFIG_ESPFS_UGLIFYJS_PATH') or 'uglifyjs'

os.chdir(CMAKE_CURRENT_BINARY_DIR)
os.environ["PATH"] += os.pathsep + str(Path(CMAKE_CURRENT_BINARY_DIR).joinpath('bin'))

if CONFIG_ESPFS_PREPROCESS_FILES == 'y':
    dest = Path(CMAKE_CURRENT_BINARY_DIR).joinpath('espfs')
    shutil.rmtree(dest, ignore_errors=True)
    for root, _, files in os.walk(ESPFS_IMAGEROOTDIR):
        dest = Path(root).relative_to(ESPFS_IMAGEROOTDIR)
        if dest == Path('.'):
            dest = Path(CMAKE_CURRENT_BINARY_DIR).joinpath('espfs')
        else:
            dest = Path(CMAKE_CURRENT_BINARY_DIR).joinpath('espfs', dest)
        if not dest.exists():
            os.mkdir(dest)
        for filename in files:
            source = Path(root).joinpath(filename)
            destfile = Path(dest).joinpath(filename)
            ext = ''.join(source.suffixes)
            if ext == '.css' and CONFIG_ESPFS_CSS_MINIFY_UGLIFYCSS == 'y':
                with open(destfile, 'w') as f:
                    subprocess.check_call([CONFIG_ESPFS_UGLIFYCSS_PATH, source], stdout=f)
            elif ext in ['.html', '.htm'] and CONFIG_ESPFS_HTML_MINIFY_HTMLMINIFIER == 'y':
                with open(destfile, 'w') as f:
                    subprocess.check_call([CONFIG_ESPFS_HTMLMINIFIER_PATH,
                        '--collapse-whitespace', '--remove-comments',
                        '--use-short-doctype', '--minify-css true',
                        '--minify-js', 'true', source], stdout=f)
            elif ext == '.js' and (CONFIG_ESPFS_JS_CONVERT_BABEL == 'y' or \
                    CONFIG_ESPFS_JS_MINIFY_BABEL == 'y' or \
                    CONFIG_ESPFS_JS_MINIFY_UGLIFYJS == 'y'):
                with open(destfile, 'w') as f:
                    if CONFIG_ESPFS_JS_CONVERT_BABEL == 'y' and CONFIG_ESPFS_JS_MINIFY_BABEL == 'y':
                        subprocess.check_call([CONFIG_ESPFS_BABEL_PATH, '--presets',
                            '@babel/preset-env,minify', source], stdout=f)
                    elif CONFIG_ESPFS_JS_CONVERT_BABEL == 'y' and CONFIG_ESPFS_JS_MINIFY_UGLIFYJS == 'y':
                        babel = subprocess.check_call([CONFIG_ESPFS_BABEL_PATH, '--presets',
                            '@babel/preset-env', source], stdout=subprocess.PIPE)
                        subprocess.check_call([CONFIG_ESPFS_UGLIFYJS_PATH], stdin=babel.stdout, stdout=f)
                    elif CONFIG_ESPFS_JS_CONVERT_BABEL == 'y':
                        subprocess.check_call([CONFIG_ESPFS_BABEL_PATH, '--presets',
                            '@babel/preset-env', source], stdout=f)
                    elif CONFIG_ESPFS_JS_MINIFY_BABEL == 'y':
                        subprocess.check_call([CONFIG_ESPFS_BABEL_PATH, '--presets',
                            'minify', source], stdout=f)
                    elif CONFIG_ESPFS_JS_MINIFY_UGLIFYJS == 'y':
                        with open(source, 'r') as infile:
                            subprocess.check_call([CONFIG_ESPFS_UGLIFYJS_PATH], stdin=infile, stdout=f)
            else:
                shutil.copy2(source, dest)
    ESPFS_IMAGEROOTDIR = dest

os.chdir(ESPFS_IMAGEROOTDIR)

filelist = []
for root, _, files in os.walk(ESPFS_IMAGEROOTDIR):
    path = Path(root).relative_to(ESPFS_IMAGEROOTDIR)
    filelist.append(str(path))
    for filename in files:
        filelist.append(str(path.joinpath(filename)))

with open(Path(CMAKE_CURRENT_BINARY_DIR).joinpath('espfs_image.bin'), 'wb') as f:
    mkespfsimage = subprocess.Popen(['mkespfsimage'], stdin=subprocess.PIPE, stdout=f)
    mkespfsimage.communicate(('\n'.join(filelist) + '\n').encode('utf-8'))

os.chdir(CMAKE_CURRENT_BINARY_DIR)
os.makedirs('include', exist_ok=True)
subprocess.check_call(['xxd', '-i', 'espfs_image.bin', 'include/espfs_image_bin.h'])