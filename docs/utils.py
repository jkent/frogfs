import os
import sys

def run_cmd_get_output(cmd):
    return os.popen(cmd).read().strip()

def call_with_python(cmd):
    # using sys.executable ensures that the scripts are called with the same Python interpreter
    if os.system('{} {}'.format(sys.executable, cmd)) != 0:
        raise RuntimeError('{} failed'.format(cmd))
