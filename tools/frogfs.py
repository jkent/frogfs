from sys import executable, stderr
from importlib import import_module
from subprocess import DEVNULL, call


def needs(module):
    try:
        import_module(module)
    except:
        print("installing... ", end='', file=stderr, flush=True)
        status = call([executable, '-m', 'pip', '-qq', 'install', module],
                stdout=DEVNULL)
        if status != 0:
            print('could not install ' + module, file=stderr)
            exit(status)

