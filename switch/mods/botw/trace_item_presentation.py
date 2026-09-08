"""Read-only presentation trace against the owner's extracted BOTW resources.

Requires oead and capstone; never attaches to or modifies the running game.
"""
import argparse
import struct
from pathlib import Path

import capstone
import oead


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--actor-pack', type=Path, required=True)
    parser.add_argument('--main', type=Path, required=True)
    args = parser.parse_args()
    data = args.actor_pack.read_bytes()
    archive = oead.Sarc(oead.yaz0.decompress(data) if data[:4] == b'Yaz0' else data)
    for name in ('PauseMenuPlayer_UseItem', 'PauseMenuPlayer_FaceUseItem'):
        resource = archive.get_file('Actor/AS/' + name + '.bas')
        text = oead.aamp.ParameterIO.from_binary(resource.data).to_text()
        print('\n###', name)
        # Preserve selector values, clips and event frame/type/value triplets.
        for line in text.splitlines():
            if any(key in line for key in ('Element', 'FileName:', 'TypeIndex:',
                                          'Frame:', 'Value:', 'Value0:', 'Value1:',
                                          'Value2:', 'Value3:', 'Value4:', 'Value5:',
                                          'Value6:', 'Value7:', 'Child')):
                print(line.strip())
    binary = args.main.read_bytes()
    names = ('EatFood', 'EatCookingDish', 'EatCookingSkewer', 'EatBizarre',
             'EatTooHard', 'Drink', 'UseFairy')
    targets = {binary.index(name.encode() + b'\0'): name for name in names}
    pages = {address & ~4095 for address in targets}
    dis = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)
    words = memoryview(binary[:min(targets) & ~3]).cast('I')
    for index, word in enumerate(words):
        if word & 0x9F000000 != 0x90000000:
            continue
        pc = index * 4
        immediate = ((word >> 5 & 0x7FFFF) << 2) | (word >> 29 & 3)
        if immediate & (1 << 20):
            immediate -= 1 << 21
        page = (pc & ~4095) + (immediate << 12)
        if page not in pages:
            continue
        register = word & 31
        for following in range(index + 1, min(index + 9, len(words))):
            add = words[following]
            if add & 0xFFC00000 != 0x91000000 or add >> 5 & 31 != register:
                continue
            address = page + (add >> 10 & 4095)
            if address not in targets:
                continue
            print('\n### Reference', targets[address], hex(pc))
            start = max(0, pc - 48)
            for instruction in dis.disasm(binary[start:following * 4 + 36], start):
                print(f'{instruction.address:08x}: {instruction.mnemonic} {instruction.op_str}')


if __name__ == '__main__':
    main()
