#!/usr/bin/env python3
"""Bounded read-only LA base 1.0.0 debugger probe; requests resume on exit.

Use only after saving and explicitly agreeing to a brief debugger pause.
Forward the emulator's debugger port with ADB first. No registers, inventory,
flags, breakpoints or save bytes are written. Results are diagnostic candidates,
not a gameplay mutation API or a finished companion snapshot protocol.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import socket
import struct
import time
from pathlib import Path

TITLE = "0x01006bb00c6f0000"
BUILD = "ae16f71e002af8cb059a9a74c4d90f34ba984892000000000000000000000000"
IMAGE_SHA256 = "0ff3778476613fe7f1ff922ddef1e35f98e8536f575c52600e93bd78fde1d586"
# Separately traced in the base executable. Each 32-byte window is unique in
# base-main.bin AND differs from 1.1.0; shared prologues cannot distinguish builds.
SIGNATURES = (0x7E13E8, 0xDFA948, 0xDF8358)


class Remote:
    def __init__(self, port):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=3)
        self.sock.settimeout(3)
        self.buffer = b""
        self.deadline = time.monotonic() + 12
        self.read_bytes = 0

    def send(self, command):
        body = command.encode("ascii")
        self.sock.sendall(b"$" + body + b"#" + f"{sum(body) & 255:02x}".encode())

    def command(self, command):
        if time.monotonic() >= self.deadline:
            raise TimeoutError("Read-only probe pause budget exhausted")
        self.send(command)
        while True:
            start = self.buffer.find(b"$")
            end = self.buffer.find(b"#", max(0, start))
            if start >= 0 and end >= 0 and len(self.buffer) >= end + 3:
                packet = self.buffer[start + 1:end]
                check = int(self.buffer[end + 1:end + 3], 16)
                self.buffer = self.buffer[end + 3:]
                if check != sum(packet) & 255:
                    raise ValueError("Debugger checksum mismatch")
                self.sock.sendall(b"+")
                result, escaped = bytearray(), False
                for byte in packet:
                    if escaped:
                        result.append(byte ^ 32)
                        escaped = False
                    elif byte == 125:
                        escaped = True
                    else:
                        result.append(byte)
                if escaped:
                    raise ValueError("Truncated debugger escape")
                return result.decode("ascii")
            if len(self.buffer) > 1024 * 1024:
                raise ValueError("Oversized debugger packet")
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError("Debugger disconnected")
            self.buffer += chunk

    def read(self, address, size):
        if not 0 < address < (1 << 39) or not 0 < size <= 4096 or address + size > (1 << 39):
            raise ValueError("Invalid guest read range")
        self.read_bytes += size
        if self.read_bytes > 16384:
            raise ValueError("Probe byte budget exhausted")
        reply = self.command(f"m{address:x},{size:x}")
        if len(reply) != size * 2 or not re.fullmatch("[0-9a-fA-F]+", reply):
            raise ValueError(f"Guest memory unavailable at {address:#x}")
        return bytes.fromhex(reply)

    def monitor(self, name):
        return bytes.fromhex(self.command("qRcmd," + name.encode().hex())).decode()

    def close(self):
        try:
            # Resume even when validation, pointer reads or the pause budget fail.
            self.send("c")
        finally:
            self.sock.close()


def snapshot(remote, image):
    remote.command("qSupported")
    if TITLE not in remote.monitor("info").lower():
        raise ValueError("Debugger is not attached to Link's Awakening")
    mappings = remote.monitor("mappings")
    candidates = []
    for line in mappings.splitlines():
        match = re.match(r"\s*(0x[0-9a-f]+) - (0x[0-9a-f]+) r-x Code ", line)
        if match:
            start, end = (int(value, 16) for value in match.groups())
            if end - start >= 0x1433830 - 1:
                candidates.append(start)
    if not 1 <= len(candidates) <= 8:
        raise ValueError("Unexpected executable mappings")
    matches = [base for base in candidates if all(
        remote.read(base + rva, 32) == image[rva:rva + 32] for rva in SIGNATURES)]
    if len(matches) != 1:
        raise ValueError("Cannot uniquely match supported main executable")
    base = matches[0]
    # One bounded aggregate read of native save-manager fields. The inventory
    # cell is manager+0x248; flag buffer/count are manager+0x268/+0x270.
    manager = remote.read(base + 0x1CC1120, 0x2B0)
    pointer = struct.unpack_from("<Q", manager, 0x248)[0]
    flags_pointer = struct.unpack_from("<Q", manager, 0x268)[0]
    flag_count = struct.unpack_from("<i", manager, 0x270)[0]
    result = {"build": BUILD, "base": hex(base), "inventoryPointer": hex(pointer),
              "flagsPointer": hex(flags_pointer), "flagByteCount": flag_count,
              "managerBytes": manager.hex(),
              "dungeonIndex": manager[0x2A8], "runtimeValidated": False}
    if pointer:
        inventory = remote.read(pointer, 0x140)
        result["inventoryBytes"] = inventory.hex()
        result["rupeesCandidate"] = struct.unpack_from("<i", inventory, 0x80)[0]
        result["healthQuartersCandidate"] = struct.unpack_from("<h", inventory, 0x84)[0]
        result["equipmentSlotBytes"] = list(inventory[0xA2:0xA4])
        result["equipmentOwnershipBits"] = hex(struct.unpack_from("<I", inventory, 0xA8)[0])
    if flags_pointer and 0 < flag_count <= 4096:
        result["flagBytes"] = remote.read(flags_pointer, flag_count).hex()
    elif flags_pointer or flag_count:
        raise ValueError("Invalid native flag buffer/count")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=6543)
    parser.add_argument("--nso", type=Path, required=True)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    nso = args.nso.read_bytes()
    if nso[:4] != b"NSO0" or nso[0x40:0x60].hex() != BUILD:
        raise ValueError("Unsupported local executable build")
    image = args.image.read_bytes()
    if hashlib.sha256(image).hexdigest() != IMAGE_SHA256:
        raise ValueError("Invalid decompressed memory image")
    remote = Remote(args.port)
    try:
        result = snapshot(remote, image)
    finally:
        remote.close()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"Captured bounded read-only state to {args.output}; resume requested")


if __name__ == "__main__":
    main()
