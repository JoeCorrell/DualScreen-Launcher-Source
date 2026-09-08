"""Build the guarded late-landing candidate, retaining the dev.18 contact fix.

Input must be the user's exact stock executable; never edits it in place.
The game retains collision selection, transition requests, animation and damage.
Offline verification does not establish resolution of the live falling lock.
"""
import argparse
import json
from pathlib import Path
import struct

import pefile
import patch_landing_contacts as contacts
from landing_retry_code import RETRY_CODE

RETRY_OFFSET = 0x100
RETRY_VA = contacts.PATCH_VA + RETRY_OFFSET
UPDATE_CALL = 0x10853B3


def patch(original):
    data = bytearray(contacts.patch(original))
    pe = pefile.PE(data=bytes(data))
    section = pe.sections[-1]
    offset = pe.get_offset_from_rva(UPDATE_CALL - 0x400000)
    if data[offset:offset + 5] != contacts.rel_call(UPDATE_CALL, 0x1083410):
        raise ValueError('Unexpected airborne-update instruction')
    data[offset:offset + 5] = contacts.rel_call(UPDATE_CALL, RETRY_VA)
    size = RETRY_OFFSET + len(RETRY_CODE)
    raw_size = contacts.aligned(size, pe.OPTIONAL_HEADER.FileAlignment)
    if size > pe.OPTIONAL_HEADER.SectionAlignment or len(contacts.SEARCH_CODE) > RETRY_OFFSET:
        raise ValueError('Retry overlaps code or exceeds the existing added image section')
    data.extend(bytes(section.PointerToRawData + raw_size - len(data)))
    start = section.PointerToRawData + RETRY_OFFSET
    data[start:start + len(RETRY_CODE)] = RETRY_CODE
    struct.pack_into('<I', data, section.get_field_absolute_offset('Misc_VirtualSize'), size)
    struct.pack_into('<I', data, section.get_field_absolute_offset('SizeOfRawData'), raw_size)
    opt = pe.OPTIONAL_HEADER
    struct.pack_into('<I', data, opt.get_field_absolute_offset('SizeOfCode'),
                     opt.SizeOfCode + raw_size - section.SizeOfRawData)
    updated = pefile.PE(data=bytes(data))
    struct.pack_into('<I', data, opt.get_field_absolute_offset('CheckSum'), updated.generate_checksum())
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
    receipt = dict(input_sha256=contacts.sha(original), sha256=contacts.sha(result),
                   bytes=len(result), retained_contact_patch=True,
                   call_site=hex(UPDATE_CALL), retry_entry=hex(RETRY_VA),
                   live_resolution='unverified')
    args.output.with_suffix(args.output.suffix+'.json').write_text(json.dumps(receipt, indent=2)+'\n')
    print(json.dumps(receipt, indent=2))


if __name__ == '__main__':
    main()
