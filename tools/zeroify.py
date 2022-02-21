#!/usr/bin/env python

import os
import subprocess
import sys


def main():
    if len(sys.argv) != 3 and len(sys.argv) != 1:
        print("Usage is ", sys.argv[0], " inputFile outputFileWithZero")
        return

    if len(sys.argv) == 1:
        input_str = sys.stdin.read()
        sys.stdout.write(input_str)
        sys.stdout.flush()
        b = bytearray(b'\0')
        sys.stdout.buffer.write(b)
        return

    with open(sys.argv[1], 'rb') as i:
        binary = i.read()

        with open(sys.argv[2], 'wb') as f:
            f.write(binary)
            b = bytearray(b'\0')
            f.write(b)

    return 

if __name__ == '__main__':
    main()
