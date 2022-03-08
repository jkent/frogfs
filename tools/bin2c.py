#!/usr/bin/env python

from argparse import ArgumentParser
import os

def main():
    parser = ArgumentParser()
    parser.add_argument('src_bin', metavar='SRC', help='source binary data')
    parser.add_argument('dst_out', metavar='DST', help='destination c source')
    args = parser.parse_args()

    with open(args.src_bin, 'rb') as f:
        in_data = f.read()

    transtab = str.maketrans('-.', '__')
    varname = os.path.basename(args.src_bin).translate(transtab)

    out_data = ''

    data_len = len(in_data)
    n = 0
    while n < data_len:
        out_data += '  '
        for i in range(12):
            out_data += '0x%02X' % in_data[n]
            n += 1
            if n == data_len:
                break
            elif i == 11:
                out_data += ','
            else:
                out_data += ', '

        out_data += '\n'
        if n >= data_len:
            break

    source_code = \
f'''#include <stddef.h>
#include <stdint.h>

const size_t {varname}_len = {data_len};
const __attribute__((aligned(4))) uint8_t {varname}[] = {{
{out_data}}};
'''

    with open(args.dst_out, 'w') as f:
        f.write(source_code)

if __name__ == '__main__':
    main()