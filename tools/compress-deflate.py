import sys
from argparse import ArgumentParser

COMPRESSOR_ID = 1

def main():
    parser = ArgumentParser()
    parser.add_argument('--id', action='store_true',
            help='return compressor id')
    parser.add_argument('-l', '--level', metavar='LEVEL', type=int, default=9,
            help='compression level')
    args = parser.parse_args()

    if (args.id):
        print(COMPRESSOR_ID)
        sys.exit(0)

    from zlib import compress

    data = sys.stdin.buffer.read()
    data = compress(data, args.level)
    sys.stdout.buffer.write(data)

if __name__ == '__main__':
    main()
