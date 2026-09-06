"""Bounded, read-only AC II Scimitar v25 inspection; never edits a game archive.

Layout verified against the user's DataPC.forge. Asset outputs are private
research material, not payload inputs. Type/ownership meaning is not inferred
from archive presence. Compression uses an explicitly supplied local LZO DLL.
"""
from __future__ import annotations
import argparse
import ctypes
import dataclasses
import json
from pathlib import Path
import struct

MAGIC = bytes.fromhex('33aafb5799fa0410')
MAX_ENTRIES = 250_000
MAX_EXPANDED = 256 * 1024 * 1024


class FormatError(ValueError):
    pass


@dataclasses.dataclass(frozen=True)
class Entry:
    id: int
    offset: int
    size: int
    name: str


def resource_index(index: bytes, data: bytes) -> list[dict]:
    """Index v25's decoded resource streams without interpreting object fields.

    Some resources carry an extended relocation header. Its size is preserved;
    a resource name or archive presence is never treated as an unlock flag.
    """
    if len(index) < 6:
        raise FormatError('Truncated resource index')
    count, = struct.unpack_from('<H', index)
    if not count or len(index) != 2 + 10 * count + 4 or index[-4:] != bytes(4):
        raise FormatError('Invalid resource index extent')
    result = []
    cursor = 0
    seen = set()
    for number in range(count):
        id_, size, flags = struct.unpack_from('<IIH', index, 2 + 10 * number)
        if id_ in seen or size < 13 or cursor > len(data) - size:
            raise FormatError('Duplicate or truncated resource')
        seen.add(id_)
        type_, body_size, name_size = struct.unpack_from('<III', data, cursor)
        if name_size > 1024 or body_size > size - 13 - name_size:
            raise FormatError('Invalid resource header bounds')
        name = data[cursor + 12:cursor + 12 + name_size].decode('utf-8', errors='strict')
        marker = data[cursor + 12 + name_size]
        extra = size - body_size - 13 - name_size
        if (marker == 0 and extra != 0) or marker not in (0, 1):
            raise FormatError('Unverified resource header variant')
        result.append(dict(id=id_, type=f'{type_:08x}', name=name, offset=cursor,
            size=size, flags=flags, extended_header_bytes=extra))
        cursor += size
    if cursor != len(data):
        raise FormatError('Unindexed resource bytes')
    return result


class Forge25:
    def __init__(self, path: Path):
        self.path = path
        self.length = path.stat().st_size

    def read(self, offset: int, size: int) -> bytes:
        if offset < 0 or size < 0 or size > MAX_EXPANDED or offset > self.length - size:
            raise FormatError('Archive range outside file or inspection limit')
        with self.path.open('rb') as stream:
            stream.seek(offset)
            data = stream.read(size)
        if len(data) != size:
            raise FormatError('Truncated archive range')
        return data

    def entries(self) -> list[Entry]:
        header = self.read(0, 21)
        if header[:9] != b'scimitar\0' or struct.unpack_from('<I', header, 9)[0] != 25:
            raise FormatError('Not a Scimitar version 25 archive')
        table = self.read(struct.unpack_from('<Q', header, 13)[0], 40)
        total, = struct.unpack_from('<I', table)
        if not 0 < total <= MAX_ENTRIES:
            raise FormatError('Invalid entry count')
        current, = struct.unpack_from('<q', table, 32)
        seen_sets: set[int] = set()
        seen_ids: set[int] = set()
        entries: list[Entry] = []
        while current != -1:
            if current in seen_sets or len(seen_sets) >= 1024:
                raise FormatError('Cyclic or excessive file sets')
            seen_sets.add(current)
            file_set = self.read(current, 48)
            count, = struct.unpack_from('<I', file_set)
            index, current = struct.unpack_from('<Qq', file_set, 8)
            names, = struct.unpack_from('<Q', file_set, 32)
            if count > total - len(entries):
                raise FormatError('File set exceeds declared entry count')
            index_data = self.read(index, count * 16)
            name_data = self.read(names, count * 188)
            for i in range(count):
                offset, id_, size = struct.unpack_from('<QII', index_data, i * 16)
                metadata = name_data[i * 188:(i + 1) * 188]
                if struct.unpack_from('<I', metadata)[0] != size or id_ in seen_ids:
                    raise FormatError('Inconsistent size or duplicate archive ID')
                if offset > self.length - 444 - size:
                    raise FormatError('Truncated FILEDATA entry')
                name = metadata[44:172].split(b'\0', 1)[0].decode('utf-8', errors='strict')
                entries.append(Entry(id_, offset, size, name))
                seen_ids.add(id_)
        if len(entries) != total:
            raise FormatError('Incomplete file set chain')
        return entries

    def unpack(self, entry: Entry, lzo_path: Path | None = None) -> list[bytes]:
        header = self.read(entry.offset, 444)
        if header[:8] != b'FILEDATA' or struct.unpack_from('<I', header, 395)[0] != entry.size:
            raise FormatError('Invalid FILEDATA header')
        data = self.read(entry.offset + 444, entry.size)
        if not data.startswith(MAGIC):
            return [data]
        cursor = 0
        result: list[bytes] = []
        total = 0
        lzo = None
        while cursor < len(data):
            if cursor == len(data) - 4 and data[cursor:] == b'\0\0\0\0':
                break  # Version-25 FILEDATA terminal word, after both streams.
            if data[cursor:cursor + 8] != MAGIC or cursor + 17 > len(data):
                raise FormatError(f'Invalid compressed stream header at {cursor:#x}/{len(data):#x}: {data[cursor:cursor+16].hex()}')
            if data[cursor + 10] != 2 or data[cursor + 14] != 128:
                raise FormatError('Unverified compression encoding')
            count, = struct.unpack_from('<H', data, cursor + 15)
            cursor += 17
            if not count or cursor + count * 4 > len(data):
                raise FormatError('Invalid compressed block count')
            sizes = list(struct.iter_unpack('<HH', data[cursor:cursor + count * 4]))
            cursor += count * 4
            chunks: list[bytes] = []
            for expanded, compressed in sizes:
                if not expanded or not compressed or cursor + 4 + compressed > len(data):
                    raise FormatError('Invalid compressed block range')
                total += expanded
                if total > MAX_EXPANDED:
                    raise FormatError('Expansion limit exceeded')
                # Stored checksums are retained in the source archive; their algorithm
                # is not yet verified. This inspector makes no CRC-integrity claim.
                cursor += 4
                source = data[cursor:cursor + compressed]
                cursor += compressed
                if expanded == compressed:
                    chunks.append(source)
                    continue
                if lzo_path is None:
                    raise FormatError('Compressed entry needs --lzo-dll')
                if lzo is None:
                    lzo = ctypes.CDLL(str(lzo_path.resolve()))
                    lzo.lzo2a_decompress_safe.argtypes = [ctypes.c_void_p, ctypes.c_size_t,
                        ctypes.c_void_p, ctypes.POINTER(ctypes.c_size_t), ctypes.c_void_p]
                    lzo.lzo2a_decompress_safe.restype = ctypes.c_int
                dest = ctypes.create_string_buffer(expanded)
                length = ctypes.c_size_t(expanded)
                code = lzo.lzo2a_decompress_safe(source, compressed, dest, ctypes.byref(length), None)
                if code != 0 or length.value != expanded:
                    raise FormatError(f'LZO decode failed ({code}, {length.value}/{expanded})')
                chunks.append(dest.raw)
            result.append(b''.join(chunks))
        return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    parser.add_argument('--index', required=True, type=Path)
    parser.add_argument('--entry', action='append', default=[], help='Exact archive entry name')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--lzo-dll', type=Path)
    args = parser.parse_args()
    archive = Forge25(args.archive)
    entries = archive.entries()
    args.index.write_text(json.dumps([dataclasses.asdict(e) for e in entries], indent=2), encoding='utf-8')
    if args.entry and args.output is None:
        parser.error('--entry requires --output')
    for name in args.entry:
        matches = [e for e in entries if e.name == name]
        if len(matches) != 1:
            raise FormatError(f'Entry must resolve uniquely: {name}')
        entry = matches[0]
        chunks = archive.unpack(entry, args.lzo_dll)
        args.output.mkdir(parents=True, exist_ok=True)
        for index, chunk in enumerate(chunks):
            target = args.output / f'{entry.id:08x}-{index}.bin'
            with target.open('xb') as stream:
                stream.write(chunk)
        print(f'{name}: {len(chunks)} streams, {sum(map(len, chunks))} decoded bytes')


if __name__ == '__main__':
    main()
