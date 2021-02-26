#!/usr/bin/env python
import bisect
import os
from argparse import ArgumentParser

discard = ('espfs.paths',)

def make_pathlist(root):
    pathlist = []
    for dir, _, files in os.walk(root, followlinks=True):
        reldir = os.path.relpath(dir, root).replace('\\', '/').lstrip('.') \
                .lstrip('/')
        if reldir and os.path.exists(dir):
            bisect.insort(pathlist, reldir)
        for file in files:
            if os.path.exists(os.path.join(dir, file)):
                relfile = os.path.join(reldir, file).replace('\\', '/') \
                        .lstrip('/')
                bisect.insort(pathlist, relfile)

    for path in discard:
        if path in pathlist:
            pathlist.remove(path)

    return pathlist

def main():
    parser = ArgumentParser()
    parser.add_argument('ROOT')
    args = parser.parse_args()

    filelist = os.path.join(args.ROOT, 'espfs.paths')

    oldpathstr = ''
    oldpathtime = 0
    if os.path.exists(filelist):
        with open(filelist) as f:
            oldpathstr = f.read()
        oldpathtime = os.path.getmtime(filelist)

    pathlist = make_pathlist(args.ROOT)
    pathstr = '\n'.join(pathlist)

    update = False
    if (oldpathstr != pathstr):
        update = True
    else:
        for path in pathlist:
            if os.path.getmtime(os.path.join(args.ROOT, path)) > oldpathtime:
                update = True
                break

    if update:
        with open(filelist, 'w') as f:
            f.write(pathstr)

if __name__ == '__main__':
    main()
