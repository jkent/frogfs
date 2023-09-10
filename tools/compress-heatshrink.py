import sys

COMPRESSOR_ID = 2

from argparse import ArgumentParser

def main():
    parser = ArgumentParser()
    parser.add_argument('--id', action='store_true',
            help='return compressor id')
    parser.add_argument('-w', '--window', metavar='WINDOW', type=int,
            choices=range(4, 15), default=11, help='window sz2')
    parser.add_argument('-l', '--lookahead', metavar='LOOKAHEAD', type=int,
            choices=range(3, 14), default=4, help='lookahead sz2')
    args = parser.parse_args()

    if (args.id):
        print(COMPRESSOR_ID)
        sys.exit(0)

    from frogfs import needs
    needs('heatshrink2')
    from heatshrink2 import compress

    data = sys.stdin.buffer.read()
    data = compress(data, args.window, args.lookahead)
    sys.stdout.buffer.write(data)

if __name__ == '__main__':
    main()
