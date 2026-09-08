"""Offline, exact-image suit input evidence. Does not enable or invoke actions.

Checks the action-name construction, callback registration, press handlers,
receiver construction/actor ownership, and shared native toggle path. A valid
report is static evidence, not a lifetime, scheduler, or callable ABI contract.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

TEXT_BASE = 0x37001000
RDATA_BASE = 0x3987F000
TEXT_SHA = 'e2cb3e908f599a2bb90c338a04ef5bec2a89b8a563e9f5f985374b153544989f'
RDATA_SHA = '376e0b44ad449a52aa98a5ac8fde81cef9819306be0cf41ef3b6074e3f3d17fe'


def exact(data, base, address, expected):
    offset = address - base
    if offset < 0 or offset + len(expected) > len(data) or data[offset:offset+len(expected)] != expected:
        raise ValueError(f'instruction/data mismatch at {address:08x}')


def relative_call(data, address, target):
    exact(data, TEXT_BASE, address, b'\xe8' + struct.pack('<i', target-address-5))


def inspect(text, rdata):
    """Structural checks, also usable with deliberately mutated test evidence."""
    rows = []
    for name, string, field, construction, registration, handler in (
        ('nanosuit_armor', 0x399C02B4, 0x28C, 0x38193D07, 0x380F6EA0, 0x380F2B30),
        ('nanosuit_stealth', 0x399C02A0, 0x290, 0x38193D17, 0x380F6EB7, 0x380F2BE0),
    ):
        exact(rdata, RDATA_BASE, string, name.encode('ascii') + b'\0')
        exact(text, TEXT_BASE, construction,
              b'\x68' + struct.pack('<I', string) + b'\x8d\x8e' + struct.pack('<I', field))
        relative_call(text, construction+11, 0x370A5BA0)
        # xor ecx,ecx; push ecx; mov eax,callback; push eax;
        # lea ecx/edx,[esi+actionField]; push ecx/edx; mov ecx,edi; call register
        reg = b'\x8d\x8e' if field == 0x28C else b'\x8d\x96'
        push = b'\x51' if field == 0x28C else b'\x52'
        exact(text, TEXT_BASE, registration,
              b'\x33\xc9\x51\xb8' + struct.pack('<I', handler) + b'\x50' + reg +
              struct.pack('<I', field) + push + b'\x8b\xcf')
        relative_call(text, registration+18, 0x382B2AC0)
        rows.append(dict(action=name, actionField=hex(field), callback=hex(handler)))

    # Four stack arguments are popped even though Ghidra infers only three.
    exact(text, TEXT_BASE, 0x380F2B30, bytes.fromhex('83ec20837c242c01'))
    exact(text, TEXT_BASE, 0x380F2BD7, bytes.fromhex('c21000'))
    exact(text, TEXT_BASE, 0x380F2BE0, bytes.fromhex('837c240c01'))
    exact(text, TEXT_BASE, 0x380F2C1E, bytes.fromhex('c21000'))
    exact(text, TEXT_BASE, 0x380F2B69, bytes.fromhex('6a018bce'))
    relative_call(text, 0x380F2B6D, 0x380F0E10)
    exact(text, TEXT_BASE, 0x380F2C12, bytes.fromhex('6a028bce'))
    relative_call(text, 0x380F2C16, 0x380F0E10)
    exact(text, TEXT_BASE, 0x380F0E10, bytes.fromhex('8b89d8020000'))
    relative_call(text, 0x380F0E57, 0x3816A090)
    relative_call(text, 0x380F0E66, 0x3816A090)
    exact(text, TEXT_BASE, 0x380F6797, bytes.fromhex('c74500b8639b39c706a8639b39'))
    exact(text, TEXT_BASE, 0x380F686E, bytes.fromhex('8b542414'))
    exact(text, TEXT_BASE, 0x380F6884, bytes.fromhex('8995d8020000'))
    for address in (0x380D94B6, 0x380D95C6):
        exact(text, TEXT_BASE, address, bytes.fromhex('568bc8'))
        relative_call(text, address+3, 0x380F6780)
    for address in (0x380D9529, 0x380D9658):
        exact(text, TEXT_BASE, address, bytes.fromhex('89be08110000'))
    actor_methods = {0x80: 0x380421B0, 0xF8: 0x380EFCD0,
                     0x1C4: 0x380EFC90, 0x1D0: 0x380D3340}
    for field, method in actor_methods.items():
        exact(rdata, RDATA_BASE, 0x399B5940+field, struct.pack('<I', method))
    exact(text, TEXT_BASE, 0x380EFC90, bytes.fromhex('8b81c00f0000c3'))
    return dict(actions=rows, actorInputField='0x1108', inputActorField='0x2d8',
                inputVtable='0x399b63b8', inputSecondaryVtable='0x399b63a8',
                callbackStackBytes=16, callbackActivationArgument=3,
                pressActivation=1, nativeToggle='0x380f0e10',
                nativeModeRequest='0x3816a090', runtimeActionVerified=False)


def audit(capture):
    text = (capture/'loaded-text.bin').read_bytes()
    rdata = (capture/'loaded-rdata.bin').read_bytes()
    for data, expected in ((text, TEXT_SHA), (rdata, RDATA_SHA)):
        if hashlib.sha256(data).hexdigest() != expected:
            raise ValueError('capture identity mismatch')
    result = inspect(text, rdata)
    result['fingerprints'] = []
    for address, size in ((0x38193D07, 32), (0x380F6EA0, 46),
                          (0x380F2B30, 170), (0x380F2BE0, 65),
                          (0x380F0E10, 94), (0x380F6780, 42),
                          (0x380F686E, 28), (0x380D94A5, 29),
                          (0x380D9529, 6), (0x380D9580, 225),
                          (0x380421B0, 43), (0x380EFCD0, 7), (0x380EFC90, 7)):
        data = text[address-TEXT_BASE:address-TEXT_BASE+size]
        if len(data) != size:
            raise ValueError('truncated fingerprint')
        result['fingerprints'].append(dict(address=hex(address), bytes=size,
                                           sha256=hashlib.sha256(data).hexdigest()))
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    args = parser.parse_args()
    print(json.dumps(audit(args.capture), indent=2))
