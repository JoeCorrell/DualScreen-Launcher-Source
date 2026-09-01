#!/usr/bin/env python3







from __future__ import annotations

import argparse
import struct
from pathlib import Path

from Crypto.Cipher import AES


MEDIA_UNIT = 0x200
NCA_HEADER_SIZE = 0xC00
NCA_MAGIC = b"NCA3"


def read_keys(path: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if "=" in line:
            name, value = line.split("=", 1)
            result[name.strip().lower()] = value.strip()
    return result


def multiply_tweak(tweak: bytes) -> bytes:
    value = bytearray(tweak)
    carry = 0
    for index in range(16):
        next_carry = value[index] >> 7
        value[index] = ((value[index] << 1) & 0xFF) | carry
        carry = next_carry
    if carry:
        value[0] ^= 0x87
    return bytes(value)


def decrypt_xts(data: bytes, key: bytes, first_sector: int = 0) -> bytes:
    if len(key) != 32 or len(data) % MEDIA_UNIT:
        raise ValueError("AES-XTS input must use a 32-byte key and complete media sectors")
    data_cipher = AES.new(key[:16], AES.MODE_ECB)
    tweak_cipher = AES.new(key[16:], AES.MODE_ECB)
    output = bytearray(len(data))
    for sector_offset in range(0, len(data), MEDIA_UNIT):
        sector = first_sector + sector_offset // MEDIA_UNIT
        tweak = tweak_cipher.encrypt(sector.to_bytes(16, "big"))
        for block_offset in range(sector_offset, sector_offset + MEDIA_UNIT, 16):
            block = bytes(a ^ b for a, b in zip(data[block_offset : block_offset + 16], tweak))
            plain = data_cipher.decrypt(block)
            output[block_offset : block_offset + 16] = bytes(
                a ^ b for a, b in zip(plain, tweak)
            )
            tweak = multiply_tweak(tweak)
    return bytes(output)


def decrypt_ctr(data: bytes, key: bytes, section_ctr: bytes, absolute_offset: int) -> bytes:
    if absolute_offset % 16:
        raise ValueError("AES-CTR source offset must be 16-byte aligned")
    prefix = bytes(reversed(section_ctr))
    initial_value = int.from_bytes(prefix + (absolute_offset // 16).to_bytes(8, "big"), "big")
    return AES.new(key, AES.MODE_CTR, nonce=b"", initial_value=initial_value).decrypt(data)


def content_key(header: bytes, production: dict[str, str], titles: dict[str, str]) -> bytes:
    rights_id = header[0x230:0x240].hex()
    crypto_type = max(header[0x206], header[0x220])
    generation = max(0, crypto_type - 1)
    if rights_id == "0" * 32:
        key_area_names = ("application", "ocean", "system")
        key_area_index = header[0x207]
        if key_area_index >= len(key_area_names):
            raise RuntimeError("The NCA key-area index is invalid")
        key_name = f"key_area_key_{key_area_names[key_area_index]}_{generation:02x}"
        if key_name not in production:
            raise RuntimeError(f"Required key-area material is unavailable: {key_name}")
        key_area_key = bytes.fromhex(production[key_name])
        encrypted_key_area = header[0x300:0x340]
        if len(key_area_key) != 16 or len(encrypted_key_area) != 0x40:
            raise RuntimeError("NCA key-area material has an invalid length")
        decrypted_key_area = AES.new(key_area_key, AES.MODE_ECB).decrypt(encrypted_key_area)
        return decrypted_key_area[0x20:0x30]
    title_kek_name = f"titlekek_{generation:02x}"
    if rights_id not in titles or title_kek_name not in production:
        raise RuntimeError("Required title key material is unavailable")
    encrypted = bytes.fromhex(titles[rights_id])
    title_kek = bytes.fromhex(production[title_kek_name])
    if len(encrypted) != 16 or len(title_kek) != 16:
        raise RuntimeError("Title key material has an invalid length")
    return AES.new(title_kek, AES.MODE_ECB).decrypt(encrypted)


def parse_pfs0(data: bytes) -> list[tuple[str, int, int]]:
    if data[:4] != b"PFS0":
        raise RuntimeError("ExeFS PFS0 header was not found")
    file_count, string_size = struct.unpack_from("<II", data, 4)
    entries_offset = 0x10
    strings_offset = entries_offset + file_count * 0x18
    if strings_offset + string_size > len(data):
        raise RuntimeError("ExeFS PFS0 metadata is truncated")
    files: list[tuple[str, int, int]] = []
    for index in range(file_count):
        offset, size, name_offset = struct.unpack_from("<QQI", data, entries_offset + index * 0x18)
        name_start = strings_offset + name_offset
        name_end = data.find(b"\0", name_start, strings_offset + string_size)
        if name_end < 0:
            raise RuntimeError("ExeFS filename is unterminated")
        files.append((data[name_start:name_end].decode("utf-8"), offset, size))
    return files


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--nca", type=Path, required=True)
    parser.add_argument("--exefs-section", type=Path)
    parser.add_argument("--prod-keys", type=Path, required=True)
    parser.add_argument("--title-keys", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    encrypted = args.nca.read_bytes()
    if len(encrypted) < NCA_HEADER_SIZE:
        raise RuntimeError("NCA prefix does not contain a complete header")
    production = read_keys(args.prod_keys)
    titles = read_keys(args.title_keys)
    header_key = bytes.fromhex(production["header_key"])
    header = decrypt_xts(encrypted[:NCA_HEADER_SIZE], header_key)
    if header[0x200:0x204] != NCA_MAGIC:
        raise RuntimeError("NCA header decryption failed")
    key = content_key(header, production, titles)

    sections: list[tuple[int, int, int]] = []
    for index in range(4):
        start_media, end_media = struct.unpack_from("<II", header, 0x240 + index * 0x10)
        if start_media:
            sections.append((index, start_media * MEDIA_UNIT, end_media * MEDIA_UNIT))
    print(f"Program NCA sections: {len(sections)}")

    exefs_section: tuple[int, int, int] | None = None
    for index, start, end in sections:
        fs_header = header[0x400 + index * 0x200 : 0x600 + index * 0x200]
        partition_type, fs_type, crypt_type = fs_header[2], fs_header[3], fs_header[4]
        print(
            f"Section {index}: offset=0x{start:x} size=0x{end - start:x} "
            f"partition={partition_type} fs={fs_type} crypt={crypt_type}"
        )
        if partition_type == 1 and fs_type == 2 and crypt_type == 3:
            exefs_section = (index, start, end)

    if exefs_section is None:
        raise RuntimeError("Program NCA has no ExeFS PFS0 section")
    index, section_start, section_end = exefs_section
    section_encrypted = (
        args.exefs_section.read_bytes()
        if args.exefs_section is not None
        else encrypted[section_start:section_end]
    )
    if args.exefs_section is None and section_end > len(encrypted):
        raise RuntimeError(
            f"NCA prefix is too short for ExeFS: need 0x{section_end:x}, have 0x{len(encrypted):x}"
        )
    if len(section_encrypted) != section_end - section_start:
        raise RuntimeError("ExeFS section range has an unexpected size")
    fs_header = header[0x400 + index * 0x200 : 0x600 + index * 0x200]
    crypt_type = fs_header[4]
    if crypt_type != 3:
        raise RuntimeError(f"Unsupported ExeFS crypto type {crypt_type}")
    section_ctr = fs_header[0x140:0x148]
    section_plain = decrypt_ctr(
        section_encrypted, key, section_ctr, section_start
    )
    pfs0_offset = struct.unpack_from("<Q", fs_header, 0x40)[0]
    pfs0_size = struct.unpack_from("<Q", fs_header, 0x48)[0]
    pfs0 = section_plain[pfs0_offset : pfs0_offset + pfs0_size]
    files = parse_pfs0(pfs0)
    metadata_size = 0x10 + len(files) * 0x18 + struct.unpack_from("<I", pfs0, 8)[0]
    data_offset = (metadata_size + 0xF) & ~0xF
    for name, offset, size in files:
        print(f"ExeFS {name}: size=0x{size:x}")
        if args.output is not None:
            args.output.mkdir(parents=True, exist_ok=True)
            (args.output / name).write_bytes(pfs0[data_offset + offset : data_offset + offset + size])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
