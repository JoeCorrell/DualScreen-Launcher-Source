"""Strict plain-text MSBT decoding for LA's UTF-8 resources (and UTF-16).

Control tags are omitted, not interpreted as UI commands. Runtime substitutions
must be resolved separately; this decoder must not manufacture objective text.
"""
import struct


def decode(data: bytes) -> dict[str, str]:
    if len(data) < 32 or data[:8] != b"MsgStdBn":
        raise ValueError("Invalid MSBT header")
    if data[8:10] not in (b"\xff\xfe", b"\xfe\xff") or data[12] not in (0, 1):
        raise ValueError("Unsupported MSBT encoding")
    endian = "<" if data[8:10] == b"\xff\xfe" else ">"
    width = 1 if data[12] == 0 else 2
    encoding = "utf-8" if width == 1 else ("utf-16le" if endian == "<" else "utf-16be")

    def read(buf, fmt, offset):
        length = struct.calcsize(endian + fmt)
        if offset < 0 or offset + length > len(buf):
            raise ValueError("Truncated MSBT field")
        return struct.unpack_from(endian + fmt, buf, offset)

    if read(data, "I", 18)[0] != len(data):
        raise ValueError("MSBT declared size mismatch")
    sections = {}
    cursor = 32
    for _ in range(read(data, "H", 14)[0]):
        tag = data[cursor:cursor + 4]
        size = read(data, "I", cursor + 4)[0]
        start = cursor + 16
        if start + size > len(data) or tag in sections:
            raise ValueError("Invalid MSBT section")
        sections[tag] = data[start:start + size]
        cursor = (start + size + 15) & ~15
    if not {b"LBL1", b"TXT2"} <= sections.keys():
        raise ValueError("MSBT lacks labels or text")
    labels, text = sections[b"LBL1"], sections[b"TXT2"]
    text_count = read(text, "I", 0)[0]
    offsets = [read(text, "I", 4 + 4 * i)[0] for i in range(text_count)]
    if any(p < 4 + 4 * text_count or p >= len(text) for p in offsets) or offsets != sorted(offsets):
        raise ValueError("Invalid MSBT text offsets")
    result = {}
    for group in range(read(labels, "I", 0)[0]):
        count, cursor = read(labels, "II", 4 + 8 * group)
        for _ in range(count):
            length = read(labels, "B", cursor)[0]
            start = cursor + 1
            index = read(labels, "I", start + length)[0]
            label = labels[start:start + length].decode("utf-8")
            cursor = start + length + 4
            if index >= text_count or label in result:
                raise ValueError("Invalid MSBT label index")
            end = offsets[index + 1] if index + 1 < text_count else len(text)
            raw = text[offsets[index]:end]
            clean, pos, terminated = bytearray(), 0, False
            while pos + width <= len(raw):
                unit = int.from_bytes(raw[pos:pos + width], "little" if endian == "<" else "big")
                if unit == 0:
                    terminated = True
                    break
                if unit == 14:
                    size = read(raw, "H", pos + width + 4)[0]
                    pos += width + 6 + size
                elif unit == 15:
                    pos += width + 4
                else:
                    clean.extend(raw[pos:pos + width])
                    pos += width
                if pos > len(raw):
                    raise ValueError("Truncated MSBT control tag")
            if not terminated:
                raise ValueError("Unterminated MSBT text")
            result[label] = clean.decode(encoding).replace("\r", "")
    return result
