"""Extract the locally owned UEF menu for read-only ActionScript inspection.

UEF v8 carries an SWF-compatible tag stream. Change the signature only in an
isolated research copy; never write the modified stream back into the game.
"""
import argparse
from pathlib import Path
import struct


def swf(resource: bytes) -> bytes:
    if len(resource) < 13:
        raise ValueError('Truncated UEF resource')
    type_, body_size, name_size = struct.unpack_from('<III', resource)
    body = 13 + name_size
    if type_ != 0xaf43ca83 or name_size > 1024 or body_size != len(resource) - body or body_size < 21:
        raise ValueError('Unexpected UEF resource layout')
    offset = body + 13
    if resource[offset:offset + 4] != b'UEF\x08':
        raise ValueError('Only the verified uncompressed UEF v8 stream is supported')
    size, = struct.unpack_from('<I', resource, offset + 4)
    if size < 8 or offset + size > len(resource):
        raise ValueError('UEF extent outside resource')
    result = bytearray(resource[offset:offset + size])
    result[:3] = b'FWS'
    return bytes(result)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('resource', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    data = swf(args.resource.read_bytes())
    with args.output.open('xb') as output: output.write(data)
