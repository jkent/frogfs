#!/usr/bin/env python

import sys


if __name__ == '__main__':
    data = sys.stdin.buffer.read()
    sys.stdout.buffer.write(data + b'\0')
