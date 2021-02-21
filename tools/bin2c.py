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

    source = '#include <stddef.h>\n#include <stdint.h>\n\nconst __attribute__((aligned(4))) uint8_t %s[] = {\n' % varname

    data_len = len(data)
    n = 0
    while n < data_len:
        source += '  '
        for i in range(12):
            source += '0x%02X' % data[n]
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

    source += 'const size_t %s_len = %s;\n' % (varname, data_len)

    with open(args.OUTPUT, 'wb') as f:
        f.write(source.encode())

if __name__ == '__main__':
    main()