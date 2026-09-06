"""Recheck native interoperability anchors against the exact device executable.

Passing this static audit does NOT authorize calling these functions. Their
owning thread, instance lifetime and transition behavior require live tests.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import capstone
import pefile

SHA256 = '23a73e5f2d035973ebe4afc056e5c56bad982d27142294fa44e7cd11732c8b92'
ANCHORS = [
    ('InventoryWeapons', 0x12da3f0, 0x12da930, 0x12d9f70),
    ('InventoryEquipCape', 0x12d9ac0, 0x12d9c40, 0x12d9510),
    ('MainSequenceCount', 0x1597580, 0x1598000, 0x15965c0),
    ('MAP_MARKER_PLAYER_POS', 0x12a2270, 0x12a2840, 0x4654d0),
]


def audit(path: Path) -> dict:
    raw = path.read_bytes()
    if hashlib.sha256(raw).hexdigest() != SHA256:
        raise ValueError('Executable is not the researched device build')
    pe = pefile.PE(data=raw, fast_load=True)
    if pe.FILE_HEADER.Machine != 0x14c or pe.OPTIONAL_HEADER.ImageBase != 0x400000:
        raise ValueError('Unexpected PE machine or preferred base')
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    decoder.detail = True
    result = []
    for command, start, end, callee in ANCHORS:
        offset = pe.get_offset_from_rva(start - 0x400000)
        body = raw[offset:offset + end - start]
        instructions = list(decoder.disasm(body, start))
        calls = {i.operands[0].imm for i in instructions if i.mnemonic == 'call' and
                 i.operands and i.operands[0].type == capstone.x86.X86_OP_IMM}
        if callee not in calls:
            raise ValueError(f'Expected native call edge missing: {command}')
        text = command.encode('utf-16-le') + b'\0\0'
        location = raw.find(text)
        if location < 0 or raw.find(text, location + 1) >= 0:
            raise ValueError(f'Native command does not resolve uniquely: {command}')
        address = 0x400000 + pe.get_rva_from_offset(location)
        if struct.pack('<I', address) not in body:
            raise ValueError(f'Command is not referenced by its native handler: {command}')
        result.append(dict(command=command, handler_va=f'{start:08x}', command_va=f'{address:08x}',
            native_callee_va=f'{callee:08x}', entry_bytes=body[:16].hex(), live_validated=False))
    return dict(executable_sha256=SHA256, image_base='00400000', anchors=result,
        status='static anchors checked; live calling contracts remain unverified')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    result = audit(args.executable)
    args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(f"Verified {len(result['anchors'])} native command/call anchors")
