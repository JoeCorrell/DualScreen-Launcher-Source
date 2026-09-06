#!/usr/bin/env python3
"""Index the dynamic symbols of a locally extracted NSO memory image.

Generated indexes contain private game metadata; keep them outside release assets.
No guessed offsets, external symbol databases, or live memory scans are used.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path


def verify_image(data, nso):
    """Bind the memory image to this NSO, not merely to a supplied build label."""
    if len(nso) < 256 or nso[:4] != b'NSO0':
        raise ValueError('Invalid NSO')
    flags, = struct.unpack_from('<I', nso, 0xC)
    previous_end = 0
    for index in range(3):
        _, location, size = struct.unpack_from('<III', nso, 0x10 + index * 16)
        if location < previous_end or location + size > len(data):
            raise ValueError('Invalid or overlapping NSO memory segments')
        if not flags & (1 << (index + 3)):
            raise ValueError('NSO segment lacks a verified hash')
        expected = nso[0xA0 + index * 32:0xC0 + index * 32]
        actual = hashlib.sha256(memoryview(data)[location:location + size]).digest()
        if actual != expected:
            raise ValueError(f'Memory image does not match NSO segment {index}')
        previous_end = location + size
    if previous_end != len(data):
        raise ValueError('Unexpected trailing memory image data')


def symbols(data):
    def read(fmt, offset):
        if offset < 0 or offset + struct.calcsize(fmt) > len(data):
            raise ValueError('Dynamic metadata exceeds image bounds')
        return struct.unpack_from(fmt, data, offset)

    module, = read('<I', 4)
    if data[module:module + 4] != b'MOD0':
        raise ValueError('No MOD0 header')
    relative, = read('<i', module + 4)
    dynamic = module + relative
    tags = {}
    for index in range(4096):
        tag, value = read('<qQ', dynamic + index * 16)
        if tag == 0:
            break
        tags[tag] = value
    else:
        raise ValueError('Unterminated dynamic table')
    if not all(tag in tags for tag in (6, 5, 10)):
        raise ValueError('Missing dynamic symbol/string metadata')
    table, strings, string_size = tags[6], tags[5], tags[10]
    if tags.get(11, 24) != 24 or strings + string_size > len(data):
        raise ValueError('Invalid dynamic string/symbol table')
    if 4 in tags:
        _, count = read('<II', tags[4])
    else:
        if strings < table or (strings - table) % 24:
            raise ValueError('Cannot establish symbol count')
        count = (strings - table) // 24
    if not 0 < count <= 1_000_000:
        raise ValueError('Invalid symbol count')
    result = []
    for index in range(count):
        name, info, other, section, value, size = read('<IBBHQQ', table + index * 24)
        if not name:
            continue
        if name >= string_size:
            raise ValueError('Symbol name outside string table')
        end = data.find(b'\0', strings + name, strings + string_size)
        if end < 0:
            raise ValueError('Unterminated symbol name')
        result.append(dict(name=data[strings + name:end].decode('utf-8'),
                           rva=value, size=size, section=section, type=info & 15))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('--nso', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    data, nso = args.image.read_bytes(), args.nso.read_bytes()
    verify_image(data, nso)
    entries = symbols(data)
    document = dict(schema=1, buildId=nso[0x40:0x60].hex(),
                    nsoSha256=hashlib.sha256(nso).hexdigest(),
                    imageSha256=hashlib.sha256(data).hexdigest(), symbols=entries)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(document, separators=(',', ':')) + '\n', encoding='utf-8')
    print(f'Indexed {len(entries)} named symbols; build {document["buildId"]}')


if __name__ == '__main__':
    main()
