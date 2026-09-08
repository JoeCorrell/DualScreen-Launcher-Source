"""Build an AC II landing-contact candidate from a user's exact executable.

Never edits the input or a running process. This fixes first-excluded-contact
masking; it does not claim that missing contacts or stalled timers are repaired.
Requires pefile. The original executable is not distributed with this tool.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

import pefile

ORIGINAL_SHA256 = '23a73e5f2d035973ebe4afc056e5c56bad982d27142294fa44e7cd11732c8b92'
PATCH_RVA = 0x216E000
PATCH_VA = 0x400000 + PATCH_RVA
CALL_SITES = (0x107A18B, 0x107A1C5)
# Assembled from landing_contact_search.S, including alignment NOPs.
SEARCH_CODE = bytes.fromhex(
    '5589e553565783ec088b018945ec8b41048945f08d4decff750cff7508'
    'e8de7805fe85c0743189c750e8820939ff83c40484c074202b7decc1ef06'
    '470fb745f225ff3f000029f8740e668945f2c1e706017decebbf89f8'
    '8d65f45f5e5b5dc20800909090')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def aligned(value, alignment):
    return (value + alignment - 1) // alignment * alignment


def rel_call(site, target):
    return b'\xe8' + struct.pack('<i', target - site - 5)


def patch(original):
    if sha(original) != ORIGINAL_SHA256:
        raise ValueError('Unsupported input SHA-256; no patch generated')
    pe = pefile.PE(data=original)
    opt = pe.OPTIONAL_HEADER
    if (pe.FILE_HEADER.Machine != 0x14C or opt.ImageBase != 0x400000
            or opt.SizeOfImage != PATCH_RVA):
        raise ValueError('Unexpected PE layout')
    header = pe.sections[-1].get_file_offset() + 40
    if header + 40 > opt.SizeOfHeaders or any(original[header:header + 40]):
        raise ValueError('No unused section-header slot')
    data = bytearray(original)
    for site in CALL_SITES:
        offset = pe.get_offset_from_rva(site - opt.ImageBase)
        if data[offset:offset + 5] != rel_call(site, 0x5C5900):
            raise ValueError('Unexpected contact-query instruction')
        data[offset:offset + 5] = rel_call(site, PATCH_VA)
    # Preserve every original section and the overlay at its original offset.
    raw = aligned(len(data), opt.FileAlignment)
    raw_size = aligned(len(SEARCH_CODE), opt.FileAlignment)
    data.extend(bytes(raw + raw_size - len(data)))
    data[raw:raw + len(SEARCH_CODE)] = SEARCH_CODE
    data[header:header + 40] = struct.pack(
        '<8sIIIIIIHHI', b'.ac2land', len(SEARCH_CODE), PATCH_RVA,
        raw_size, raw, 0, 0, 0, 0, 0x60000020)  # executable/read-only code
    struct.pack_into('<H', data, pe.FILE_HEADER.get_field_absolute_offset('NumberOfSections'),
                     pe.FILE_HEADER.NumberOfSections + 1)
    struct.pack_into('<I', data, opt.get_field_absolute_offset('SizeOfImage'),
                     aligned(PATCH_RVA + len(SEARCH_CODE), opt.SectionAlignment))
    struct.pack_into('<I', data, opt.get_field_absolute_offset('SizeOfCode'),
                     opt.SizeOfCode + raw_size)
    updated = pefile.PE(data=bytes(data))
    struct.pack_into('<I', data, opt.get_field_absolute_offset('CheckSum'),
                     updated.generate_checksum())
    return bytes(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    if args.input.resolve() == args.output.resolve() or args.output.exists():
        parser.error('Output must be a new file, separate from the input')
    original = args.input.read_bytes()
    result = patch(original)
    with args.output.open('xb') as out:
        out.write(result)
    receipt = {'status': 'offline candidate; not live-validated or installed',
               'original_sha256': sha(original), 'patched_sha256': sha(result),
               'patched_size': len(result), 'call_sites': [hex(x) for x in CALL_SITES],
               'behavior': 'Continue past excluded contacts using original slope query and filter',
               'rollback': 'Restore the unmodified input executable while the game is closed'}
    args.output.with_suffix(args.output.suffix + '.json').write_text(
        json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(receipt, indent=2))


if __name__ == '__main__':
    main()
