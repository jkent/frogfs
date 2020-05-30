#!/usr/bin/env python

from argparse import ArgumentParser
import os

def main():
    parser = ArgumentParser()
    parser.add_argument('INFILE')
    parser.add_argument('OUTPUT')
    args = parser.parse_args()

    with open(args.INFILE, 'rb') as f:
        data = f.read()

    transtab = str.maketrans('-.', '__')
    varname = os.path.basename(args.INFILE).translate(transtab)

    source = f'const __attribute__((aligned(4))) unsigned char {varname}[] = {{\n'

    data_len = len(data)
    n = 0
    while n < data_len:
        source += '  '
        for i in range(12):
            source += f'0x{data[n]:02X}'
            n += 1
            if n == data_len:
                break
            elif i == 11:
                source += ','
            else:
                source += ', '

        source += '\n'
        if n >= data_len:
            break
    source += '};\n'

    source += f'unsigned int {varname}_len = {data_len};\n'

    with open(args.OUTPUT, 'wb') as f:
        f.write(source.encode())

if __name__ == '__main__':
    main()