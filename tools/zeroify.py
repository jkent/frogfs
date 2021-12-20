#!/usr/bin/env python

import os
import subprocess
import sys


def main():
    if len(sys.argv) < 3:
        print("Usage is ", sys.argv[0], " inputFile outputFileWithZero")
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
