"""Bounded symbol/disassembly inspection of the user's extracted base NSO."""
import argparse
import json
import re
from pathlib import Path
import capstone

p = argparse.ArgumentParser()
p.add_argument('pattern')
p.add_argument('--disassemble', action='store_true')
a = p.parse_args()
root = Path('.work/smo-audit')
symbols = json.loads((root / 'base-symbols.json').read_text())['symbols']
matches = {s['rva']: s for s in symbols if s['type'] == 2 and s['size'] and re.search(a.pattern, s['name'])}
image = (root / 'base-main.bin').read_bytes() if a.disassemble else b''
md = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_LITTLE_ENDIAN)
for address, s in sorted(matches.items()):
    print(f"{address:#x} {s['size']:#x} {s['name']}")
    if a.disassemble:
        if s['size'] > 4096:
            print('  omitted: function exceeds bounded disassembly size'); continue
        for i in md.disasm(image[address:address+s['size']], address):
            print(f'  {i.address:x} {i.mnemonic} {i.op_str}')
