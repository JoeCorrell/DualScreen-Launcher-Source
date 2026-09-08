"""Read-only CryPak research extractor; writes only to a new local directory.

Format research: AWS Lumberyard ZipDirCacheFactory.cpp and PakDecrypt's
published format analysis (https://pastebin.com/DdU9jMvw). These are leads;
OAEP validation, CDR structure, lengths and EVERY entry CRC must agree with
the supplied archive. No executable patching, signature bypass or repacking.
The public key is discovered in the user's verified executable, not guessed.
"""
import argparse
import ctypes
import hashlib
import json
import re
import struct
import zlib
from pathlib import Path, PurePosixPath

from Crypto.PublicKey import RSA
from inspect_inputs import EXPECTED_EXE_BYTES, EXPECTED_EXE_SHA256, sha256


def xor(a, b):
    return bytes(x ^ y for x, y in zip(a, b))


def mgf(seed, length):
    return b''.join(hashlib.sha256(seed + i.to_bytes(4, 'big')).digest()
                    for i in range((length + 31) // 32))[:length]


def unwrap(block, key):
    n = int.from_bytes(block, 'big')
    if n >= key.n:
        raise ValueError('RSA input exceeds modulus')
    encoded = pow(n, key.e, key.n).to_bytes(128, 'big')
    seed = xor(encoded[1:33], mgf(encoded[33:], 32))
    data = xor(encoded[33:], mgf(seed, 95))
    if encoded[0] or data[:32] != hashlib.sha256(b'').digest():
        raise ValueError('OAEP hash mismatch')
    suffix = data[32:]
    index = suffix.find(b'\x01')
    if index < 0 or any(suffix[:index]) or len(suffix[index+1:]) != 16:
        raise ValueError('OAEP padding or key length mismatch')
    return suffix[index+1:]


class Twofish:
    def __init__(self, path):
        self.dll = ctypes.CDLL(str(path.resolve()))
        self.dll.initialize.argtypes = []
        self.dll.initialize.restype = None
        self.dll.transform.argtypes = [ctypes.c_void_p, ctypes.c_size_t,
                                       ctypes.c_char_p, ctypes.c_char_p]
        self.dll.transform.restype = None
        self.dll.initialize()  # Library's built-in known-answer tests.

    def ctr(self, data, key, iv):
        if len(key) != 16 or len(iv) != 16:
            raise ValueError('Invalid Twofish parameters')
        output = ctypes.create_string_buffer(data, len(data))
        self.dll.transform(output, len(data), key, iv)
        return output.raw


def public_keys(executable):
    if executable.stat().st_size != EXPECTED_EXE_BYTES or sha256(executable) != EXPECTED_EXE_SHA256:
        raise ValueError('Executable differs from the acquired device identity')
    data = executable.read_bytes()
    # DER SEQUENCE with long-form length. RSA.import_key validates its contents.
    for match in re.finditer(b'\x30\x81', data):
        offset = match.start()
        if offset + 3 > len(data):
            continue
        size = data[offset+2] + 3
        try:
            key = RSA.import_key(data[offset:offset+size])
            if key.size_in_bits() == 1024 and not key.has_private():
                yield offset, key
        except (ValueError, IndexError, TypeError):
            pass


def extract(archive, executable, target, cipher):
    if target.exists():
        raise ValueError('Output already exists; choose a fresh extraction directory')
    raw = archive.read_bytes()
    end = raw.rfind(b'PK\x05\x06', max(0, len(raw)-65557))
    if end < 0 or end+22 > len(raw):
        raise ValueError('Incomplete archive: no valid EOCD')
    _, disk, first, count, total, size, offset, comment = struct.unpack_from('<4s4H2IH', raw, end)
    if disk or first or count != total or end+22+comment != len(raw) or offset+size != end:
        raise ValueError('Unsupported or incomplete archive layout')
    trailer = raw[end+22:]
    if len(trailer) != 2320 or struct.unpack_from('<IHH', trailer) != (8, 3, 1):
        raise ValueError('Unsupported CryPak extension header')
    if struct.unpack_from('<I', trailer, 8)[0] != 132 or struct.unpack_from('<I', trailer, 140)[0] != 2180:
        raise ValueError('Invalid signature/encryption header size')
    wrapped = [trailer[144+i*128:144+(i+1)*128] for i in range(17)]
    material = None
    for key_offset, key in public_keys(executable):
        try:
            material = [unwrap(block, key) for block in wrapped]
            break
        except ValueError:
            continue
    if material is None:
        raise ValueError('No executable public key validates every wrapped archive key')
    directory = cipher.ctr(raw[offset:end], material[1], material[0])
    records = []
    cursor = 0
    paths = set()
    total_bytes = 0
    for _ in range(total):
        if cursor+46 > len(directory) or directory[cursor:cursor+4] != b'PK\x01\x02':
            raise ValueError('Decrypted central directory is invalid')
        header = struct.unpack_from('<4s6H3I5H2I', directory, cursor)
        _, _, needed, flags, method, _, _, crc, packed, unpacked, names, extra, notes, disk, _, _, local = header
        finish = cursor+46+names+extra+notes
        if finish > len(directory) or needed > 20 or flags & 8 or disk:
            raise ValueError('Unsupported entry header')
        name = directory[cursor+46:cursor+46+names].decode('utf-8').replace('\\', '/')
        path = PurePosixPath(name)
        if not name or path.is_absolute() or '..' in path.parts or ':' in name or '\0' in name:
            raise ValueError('Unsafe archive pathname')
        if name.casefold() in paths:
            raise ValueError('Duplicate archive pathname')
        paths.add(name.casefold())
        total_bytes += unpacked
        if unpacked > 256*1024*1024 or total_bytes > 1024*1024*1024:
            raise ValueError('Extraction budget exceeded')
        start = local+30+names+extra
        if start+packed > offset:
            raise ValueError('Entry payload outside archive data')
        data = raw[start:start+packed]
        if method in (13, 14) and data:
            iv = struct.pack('<4I', ((packed << 12) ^ unpacked) & 0xffffffff,
                             int(not packed), ((packed << 12) ^ crc) & 0xffffffff,
                             packed ^ int(not unpacked))
            data = cipher.ctr(data, material[1+((~(crc >> 2)) & 15)], iv)
        if method in (8, 14) and (packed or unpacked):
            inflater = zlib.decompressobj(-15)
            data = inflater.decompress(data, unpacked+1)
            if not inflater.eof or inflater.unused_data or inflater.unconsumed_tail:
                raise ValueError(f'Invalid or oversized deflate stream: {name}; eof={inflater.eof}, trailing={len(inflater.unused_data)}, unconsumed={len(inflater.unconsumed_tail)}, expected={unpacked}, actual={len(data)}')
        elif method not in (0, 8, 13, 14):
            raise ValueError(f'Unsupported compression {method}')
        if len(data) != unpacked or zlib.crc32(data) != crc:
            raise ValueError(f'CRC/length validation failed: {name}')
        records.append((name, data, crc))
        cursor = finish
    if cursor != len(directory):
        raise ValueError('Unexpected trailing directory data')
    # Validate the entire archive before publishing any extracted file.
    target.mkdir(parents=True)
    for name, data, _ in records:
        destination = target.joinpath(*PurePosixPath(name).parts)
        if name.endswith('/'):
            destination.mkdir(parents=True, exist_ok=True)
        else:
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)
    report = dict(archive=str(archive), sha256=sha256(archive), publicKeyFileOffset=key_offset,
                  entries=len(records), bytes=total_bytes, allEntryCrcsValid=True,
                  signatureValidated=False,
                  files=[dict(name=n, bytes=len(d), crc=f'{c:08x}') for n,d,c in records])
    (target / '_extraction-evidence.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items() if k != 'files'}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--twofish', type=Path, required=True)
    args = parser.parse_args()
    extract(args.archive, args.exe, args.output, Twofish(args.twofish))
