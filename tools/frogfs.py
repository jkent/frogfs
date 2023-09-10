import sys
from importlib import import_module
from subprocess import DEVNULL, call


def needs(module):
    try:
        import_module(module)
    except:
        status = call([sys.executable, '-m', 'pip', '-qq', 'install', module],
                stdout=DEVNULL)
        if status != 0:
            print('could not install ' + module, file=sys.stderr)
            exit(status)

