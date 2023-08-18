import sys
from argparse import ArgumentParser
from zlib import compress

def main():
    parser = ArgumentParser()
    parser.add_argument('-l', '--level', metavar='LEVEL', type=int, default=9,
            help='compression level')
    args = parser.parse_args()

    data = sys.stdin.buffer.read()
    data = compress(data, args.level)
    sys.stdout.buffer.write(data)

if __name__ == '__main__':
    main()
