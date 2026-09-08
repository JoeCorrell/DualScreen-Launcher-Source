"""Inventory owner-supplied Crysis 3 files without executing or modifying them.

Reports PE identity/imports, archive metadata and candidate native strings. A
string is a research lead, never a verified hook or an offset contract.
Generated reports and all original game data belong outside source control.
"""
import argparse
import hashlib
import json
import re
import struct
import zipfile
from pathlib import Path

import pefile

# Identity read from the user's device, not from a partially transferred file.
EXPECTED_EXE_BYTES = 86636544
EXPECTED_EXE_SHA256 = 'f332e2fb466ec8ce44336a4af06cd498bb5a787d7352548c76f2e4ec008f5e8e'


def sha256(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def inspect_pe(path, output):
    if path.stat().st_size != EXPECTED_EXE_BYTES or sha256(path) != EXPECTED_EXE_SHA256:
        raise ValueError('Executable is incomplete or differs from the recorded device build')
    data = path.read_bytes()
    pe = pefile.PE(data=data)
    if any(s.PointerToRawData + s.SizeOfRawData > len(data) for s in pe.sections):
        raise ValueError('PE section extends beyond acquired file')
    sections = [dict(name=s.Name.rstrip(b'\0').decode('ascii'),
                     rva=s.VirtualAddress, virtualBytes=s.Misc_VirtualSize,
                     fileOffset=s.PointerToRawData, fileBytes=s.SizeOfRawData,
                     flags=s.Characteristics) for s in pe.sections]
    imports = {d.dll.decode('ascii'): [dict(name=i.name.decode('ascii') if i.name else None,
                ordinal=i.ordinal, address=i.address) for i in d.imports]
               for d in getattr(pe, 'DIRECTORY_ENTRY_IMPORT', [])}
    keywords = re.compile(r'player|nanosuit|cloak|armor|weapon|attachment|ammo|grenade|'
                          r'objective|mission|inventory|radar|map|hud|cry.?pak|decrypt|'
                          r'health|energy|module|statistics|suit|scriptbind|gEnv', re.I)
    strings = []
    for match in re.finditer(rb'[\x20-\x7e]{5,512}', data):
        value = match.group().decode('ascii')
        if not keywords.search(value):
            continue
        offset = match.start()
        section = next((s for s in pe.sections if s.PointerToRawData <= offset <
                        s.PointerToRawData + s.SizeOfRawData), None)
        if section is not None:
            va = pe.OPTIONAL_HEADER.ImageBase + section.VirtualAddress + offset - section.PointerToRawData
            strings.append(dict(va=f'{va:08x}', offset=offset, text=value))
    report = dict(file=str(path), sha256=sha256(path), bytes=len(data),
                  machine=pe.FILE_HEADER.Machine, timestamp=pe.FILE_HEADER.TimeDateStamp,
                  imageBase=pe.OPTIONAL_HEADER.ImageBase, imageBytes=pe.OPTIONAL_HEADER.SizeOfImage,
                  entryRva=pe.OPTIONAL_HEADER.AddressOfEntryPoint, sections=sections,
                  imports=imports, candidateStrings=len(strings))
    (output / 'executable.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    (output / 'candidate-strings.json').write_text(json.dumps(strings, indent=2), encoding='utf-8')
    return report


def inspect_pak(path):
    report = dict(file=str(path), bytes=path.stat().st_size, sha256=sha256(path))
    with path.open('rb') as stream:
        report['prefix'] = stream.read(32).hex()
        stream.seek(max(0, path.stat().st_size - 65557))
        tail = stream.read()
        index = tail.rfind(b'PK\x05\x06')
        if index >= 0 and index + 22 <= len(tail):
            fields = struct.unpack_from('<4s4H2IH', tail, index)
            report['endOfCentralDirectory'] = dict(disk=fields[1], directoryDisk=fields[2],
                diskEntries=fields[3], entries=fields[4], directoryBytes=fields[5],
                directoryOffset=fields[6], commentBytes=fields[7],
                trailingBytes=len(tail)-index-22)
    try:
        with zipfile.ZipFile(path) as archive:
            report['entries'] = [dict(name=i.filename, bytes=i.file_size,
                packedBytes=i.compress_size, method=i.compress_type, flags=i.flag_bits,
                crc=f'{i.CRC:08x}') for i in archive.infolist()]
    except zipfile.BadZipFile as error:
        report['zipError'] = str(error)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    executable = args.root / 'Bin32/Crysis3.exe'
    if executable.is_file() and executable.stat().st_size:
        try:
            report = inspect_pe(executable, args.output)
            print(json.dumps({k: report[k] for k in ('sha256','bytes','machine','imageBase','candidateStrings')}))
        except ValueError as error:
            # Replace any earlier incomplete report with explicit rejection.
            for name in ('executable.json', 'candidate-strings.json'):
                (args.output / name).write_text(json.dumps({'rejected': str(error)}), encoding='utf-8')
            print(str(error))
    archives = [inspect_pak(p) for p in sorted(args.root.rglob('*.pak')) if p.stat().st_size]
    (args.output / 'archives.json').write_text(json.dumps(archives, indent=2), encoding='utf-8')
    for item in archives:
        print(json.dumps({k: v for k, v in item.items() if k != 'entries'}))


if __name__ == '__main__':
    main()
