import sys

try:
    from heatshrink2 import compress
except:
    from subprocess import DEVNULL, call
    call([sys.executable, '-P', '-m', 'pip', 'install', 'heatshrink2'],
            stdout=DEVNULL)
    from heatshrink2 import compress

from argparse import ArgumentParser

def main():
    parser = ArgumentParser()
    parser.add_argument('-w', '--window', metavar='WINDOW', type=int,
            choices=range(4, 15), default=11, help='window sz2')
    parser.add_argument('-l', '--lookahead', metavar='LOOKAHEAD', type=int,
            choices=range(3, 14), default=4, help='lookahead sz2')
    args = parser.parse_args()

    data = sys.stdin.buffer.read()
    data = compress(data, args.window, args.lookahead)
    sys.stdout.buffer.write(data)

if __name__ == '__main__':
    main()
