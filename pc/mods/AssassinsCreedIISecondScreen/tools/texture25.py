"""Read-only DDS export for the verified v25 UI DXT3 texture layout.

Outputs stay with local research material. The game resource trailer is not
pixel data: using the final width*height bytes corrupts block alignment.
"""
import argparse
from pathlib import Path
import struct


def dds(resource: bytes) -> bytes:
    if len(resource) < 13:
        raise ValueError('Truncated texture resource')
    kind, body_size, name_size = struct.unpack_from('<III', resource)
    body = 13 + name_size
    if kind != 0xa2b7e917 or name_size > 1024 or body_size != len(resource) - body:
        raise ValueError('Unexpected texture resource layout')
    if resource[body - 1] != 0 or body_size < 85:
        raise ValueError('Unsupported extended texture header')
    width, height, depth, format_ = struct.unpack_from('<IIII', resource, body + 9)
    if not (4 <= width <= 4096 and 4 <= height <= 4096 and width % 4 == 0 and height % 4 == 0):
        raise ValueError('Invalid UI texture dimensions')
    if depth != 1 or format_ != 4:
        raise ValueError('Only verified 2D DXT3 UI textures are supported')
    size, = struct.unpack_from('<I', resource, body + 81)
    if size != width * height or body + 85 + size > len(resource):
        raise ValueError('Invalid top mip extent')
    header = bytearray(128)
    header[:4] = b'DDS '
    struct.pack_into('<7I', header, 4, 124, 0x81007, height, width, size, 0, 1)
    struct.pack_into('<II4s', header, 76, 32, 4, b'DXT3')
    struct.pack_into('<I', header, 108, 0x1000)
    return bytes(header) + resource[body + 85:body + 85 + size]


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('resource', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    with args.output.open('xb') as output:
        output.write(dds(args.resource.read_bytes()))
