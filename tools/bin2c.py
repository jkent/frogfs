#!/usr/bin/env python

from argparse import ArgumentParser
import os

def main():
    parser = ArgumentParser()
    parser.add_argument('--use-array', action='store_true', help='use slower but portable array method')
    parser.add_argument('binary', metavar='BINARY', help='source binary data')
    parser.add_argument('output', metavar='OUTPUT', help='destination C source')
    args = parser.parse_args()

    use_array = args.use_array
    bin = args.binary
    output = args.output

    bin_len = os.path.getsize(bin)
    var = os.path.basename(bin).translate(str.maketrans('-.', '__'))

    if use_array:
        with open(bin, 'rb') as in_f:
            with open(output, 'w') as out_f:
                out_f.write('#include <stddef.h>\n')
                out_f.write('#include <stdint.h>\n')
                out_f.write('\n')
                out_f.write(f'const size_t {var}_len = {bin_len};\n')
                out_f.write(f'const __attribute__((aligned(4))) uint8_t {var}[] = {{\n')
                while True:
                    data = in_f.read(12)
                    if not data:
                        break
                    data = [f'0x{byte:02X}' for byte in data]
                    s = ', '.join(data)
                    out_f.write(f'    {s},\n')
                out_f.write('};\n')
    else:
        with open(output, 'w') as f:
            f.write('asm (\n')
            f.write('    ".section .rodata\\n"\n')
            f.write('    ".balign 4\\n"\n')
            f.write(f'    ".global {var}_len\\n"\n')
            f.write(f'    "{var}_len:\\n"\n')
            f.write(f'    ".int {bin_len}\\n"\n')
            f.write(f'    ".global {var}\\n"\n')
            f.write(f'    "{var}:\\n"\n')
            f.write(f'    ".incbin \\"{bin}\\"\\n"\n')
            f.write('    ".balign 4\\n"\n')
            f.write('    ".section .text\\n"\n')
            f.write(');\n')

if __name__ == '__main__':
    main()