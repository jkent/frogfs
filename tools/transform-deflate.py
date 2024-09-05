import sys
# Try to use zopfli first, else fallback to zlib
try:
    import zopfli.zlib as zlib
except:
    import zlib

if __name__ == '__main__':
    while True:
        chunk = sys.stdin.buffer.read()
        if not chunk:
            break
        compressed = zlib.compress(data)
        sys.stdout.buffer.write(compressed)
