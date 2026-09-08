"""Bounded, read-only loadout evidence. Never emits a runtime adapter or sends game input."""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for field in ('adb', 'serial', 'session', 'output'):
        parser.add_argument('--'+field, required=True)
    parser.add_argument('--first-id', type=int, required=True)
    args = parser.parse_args()
    next_id = args.first_id
    def word(data, offset=0): return struct.unpack_from('<I', data, offset)[0]
    def read(address, count, reason):
        nonlocal next_id
        request = next_id
        next_id += 1
        folder = Path(args.output)/('read'+str(request))
        subprocess.run([sys.executable, str(Path(__file__).with_name('read_live_inspector.py')),
                        '--adb', args.adb, '--serial', args.serial, '--session', args.session,
                        '--id', str(request), '--address', hex(address), '--bytes', str(count),
                        '--evidence', reason, '--output', str(folder)],
                       check=True, stdout=subprocess.PIPE, timeout=40)
        report = json.loads((folder/('read-'+str(request)+'.json')).read_text())
        if not report['stable']:
            raise RuntimeError(f'Unstable read {request}; stop, next unused ID {next_id}')
        data = (folder/('read-'+str(request)+'.bin')).read_bytes()
        print(json.dumps({'id': request, 'address': hex(address), 'size': count, 'reason': reason}), flush=True)
        return data
    def expect(data, table):
        if word(data) != table: raise ValueError('Unexpected vtable')
    root = word(read(0x3b9fde30, 4, 'Fresh verified CGame root'))
    game = read(root, 0x34, 'CGame identity and framework owner'); expect(game, 0x399bef80)
    framework = word(game, 0x30)
    fw = read(framework, 0x30, 'Framework identity/context'); expect(fw, 0x398ba5e0)
    context = word(fw, 0x28)
    cache = read(context+0x8b0, 8, 'Current actor cache/id: corroborate with authoritative channel')
    holder = word(read(context+0x24, 4, 'Authoritative client channel holder'))
    channel = word(read(holder+8, 4, 'Client channel pointer'))
    owner = word(read(channel+0x10, 4, 'Authoritative current player EntityId'))
    if owner != word(cache): raise ValueError('Actor cache disagreement')
    actor_address = word(cache, 4)
    actor = read(actor_address, 0x44, 'Current actor identity and inventory'); expect(actor, 0x399b5940)
    if word(actor, 8) != owner: raise ValueError('Actor owner mismatch')
    inv_address = word(actor, 0x40)
    inventory = read(inv_address, 0x98, 'Inventory native item vector and current selection'); expect(inventory, 0x3988c288)
    if word(inventory, 8) != owner: raise ValueError('Inventory owner mismatch')
    begin, end = word(inventory, 0x10), word(inventory, 0x14)
    if end < begin or (begin|end)&3 or end-begin > 32*4: raise ValueError('Invalid bounded item vector')
    ids = list(struct.unpack('<'+'I'*((end-begin)//4), read(begin, end-begin, 'All actual owned item IDs'))) if end>begin else []
    print('OWNED_IDS='+repr([hex(value) for value in ids])+' CURRENT='+hex(word(inventory, 0x8c)), flush=True)
    system = word(read(0x3b9f9c68, 4, 'Current item-system interface global'))
    if system != word(read(framework+0x4b4, 4, 'Framework item-system ownership'))+4: raise ValueError('Item-system disagreement')
    header = read(system, 0x194, 'Native item-system tree root'); expect(header, 0x3988cf40)
    nodes = {}
    for wanted in ids:
        node = word(header, 0x190)
        seen = set()
        while node:
            if node == system+0x18c or node in seen or len(seen)>=32: raise ValueError('Invalid item tree')
            seen.add(node)
            if node not in nodes: nodes[node] = read(node, 24, 'Bounded item lookup node for '+hex(wanted))
            entry = nodes[node]
            key = word(entry, 0x10)
            if key == wanted:
                address = word(entry, 0x14)
                item = read(address, 0x10c, 'Owned item identity and weak-owner fields for '+hex(wanted))
                print('ITEM='+json.dumps({'id': hex(wanted), 'address': hex(address), 'vtable': hex(word(item)),
                    'ownerId': hex(word(item, 0x100)), 'weakActor': hex(word(item, 0x104)), 'weakControl': hex(word(item, 0x108))}), flush=True)
                entity = read(word(item, 0xc), 32, 'Owned item IEntity/id/class'); expect(entity, 0x398eccc0)
                if word(entity, 0xc) != wanted: raise ValueError('Owned item IEntity mismatch')
                cls = read(word(entity, 0x1c), 12, 'Owned item class/name pointer'); expect(cls, 0x398f4638)
                name = read(word(cls, 8), 64, 'Owned item native class name').split(b'\0', 1)[0].decode('ascii')
                print('CLASS='+name, flush=True)
                if word(item, 0x108): read(word(item, 0x108), 12, 'Owned item weak-owner validity counts')
                break
            node = word(entry, 8 if wanted<key else 12)
    after = read(inv_address, 0x98, 'Final inventory identity/selection/vector check')
    if after != inventory: raise ValueError('Inventory changed during independently timed reads')
    print('NEXT_REQUEST_ID='+str(next_id), flush=True)


if __name__ == '__main__': main()
