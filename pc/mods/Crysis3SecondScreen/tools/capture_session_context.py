"""Bounded research reads using documented chains; never authorizes telemetry.

Uses the already-running inspector and its exact source/header gate. Stops on
unstable reads or an unexpected vtable. Each range retains its own evidence.
"""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import sys


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--adb', required=True)
    p.add_argument('--serial', required=True)
    p.add_argument('--session', required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--first-id', type=int, required=True)
    args = p.parse_args()
    next_id = args.first_id

    def u32(data, offset=0):
        return struct.unpack_from('<I', data, offset)[0]

    def read(address, size, reason):
        nonlocal next_id
        request_id = next_id
        next_id += 1
        destination = args.output / ('read' + str(request_id))
        subprocess.run([sys.executable, str(Path(__file__).with_name('read_live_inspector.py')),
                        '--adb', args.adb, '--serial', args.serial,
                        '--session', args.session, '--id', str(request_id),
                        '--address', hex(address), '--bytes', str(size),
                        '--evidence', reason, '--output', str(destination)],
                       check=True, stdout=subprocess.PIPE, timeout=40)
        data = (destination / ('read-' + str(request_id) + '.bin')).read_bytes()
        print(json.dumps({'id': request_id, 'address': hex(address), 'bytes': size,
                          'reason': reason}), flush=True)
        return data

    def table(data, expected):
        if u32(data) != expected:
            raise ValueError('Unexpected vtable: ' + hex(u32(data)))

    root = u32(read(0x3b9fde30, 4, 'Fresh exact-profile CGame root global'))
    game = read(root, 0x200, 'CGame header, framework/objective/HUD ownership slots')
    table(game, 0x399bef80)
    framework = u32(game, 0x30)
    fw = read(framework, 0x4b8, 'Fresh framework, native phase and level/item-system slots')
    table(fw, 0x398ba5e0)
    context = u32(fw, 0x28)
    ctx = read(context, 0x8b8, 'Fresh client context authoritative/cache paths and phase')
    holder = read(u32(ctx, 0x24), 12, 'Client-channel holder from fresh context+24')
    channel = read(u32(holder, 8), 20, 'Authoritative channel entity ID at +10')
    env = read(u32(ctx, 0x2c), 0xaa, 'Client context environment and alternate-client predicate owner')
    branch = read(u32(env, 0x3c), 12, 'Alternate-client predicate receiver')
    table(branch, 0x3991ca90)
    refresh = read(0x3b806517, 1, 'Exact-profile client cache refresh flag')
    if refresh[0] or branch[8] or u32(channel, 0x10) != u32(ctx, 0x8b0):
        raise ValueError('Non-normal or refreshing client actor path')
    actor_address = u32(ctx, 0x8b4)
    actor = read(actor_address, 0x1000, 'Fresh normal-client actor header, entity, inventory, suit owner')
    table(actor, 0x399b5940)
    if u32(actor, 8) != u32(channel, 0x10):
        raise ValueError('Client actor identity mismatch')
    # +4 is IGameObject, not IEntity. Fresh reads50/55 and native pose callsites
    # establish the entity interface at +0xc; never apply entity slots to +4.
    entity = read(u32(actor, 0xc), 0x40, 'Fresh actor-owned IEntity header at +0xc')
    table(entity, 0x398eccc0)
    if u32(entity, 0xc) != u32(actor, 8):
        raise ValueError('Actor/entity identity mismatch')
    read(u32(entity), 0x128, 'Observed IEntity vtable through world angles slot +124')
    inventory = read(u32(actor, 0x40), 0x114, 'Fresh actor inventory owner/current item/ammunition/vector headers')
    table(inventory, 0x3988c288)
    if u32(inventory, 8) != u32(actor, 8):
        raise ValueError('Inventory owner mismatch')
    suit_address = u32(actor, 0xfc0)
    if suit_address:
        suit = read(suit_address, 0x200, 'Fresh suit ownership, mode, energy channels and maxima')
        if u32(suit, 0x18) != actor_address:
            raise ValueError('Suit owner mismatch')
    hud_address = u32(game, 0x1e8)
    if hud_address:
        hud = read(hud_address, 0x74, 'CGame+1e8-owned HUD cutscene predicate receiver')
        table(hud, 0x399ef860)
    timer_address = u32(read(0x3b8063dc, 4, 'Exact-profile native timer global'))
    timer = read(timer_address, 0x6c, 'Fresh timer enabled byte and virtual identity')
    table(timer, 0x39952878)
    read(u32(timer), 0x1c, 'Native timer predicate vtable slot +18')
    system = read(u32(fw, 0x4ac), 0x20, 'Authoritative current-level system from framework+4ac')
    table(system, 0x398868a0)
    level = read(u32(system, 0x1c), 0x20, 'Current level and embedded level-info identities')
    table(level, 0x398862c4)
    if u32(level, 4) != 0x398862d0:
        raise ValueError('Unexpected embedded level-info vtable')
    read(u32(level, 8), 128, 'Bounded current level name string')
    read(u32(level, 12), 192, 'Bounded current level filesystem path string')
    objectives = read(u32(game, 0x104), 0x28, 'Fresh objective-system vector header')
    table(objectives, 0x399f32c0)
    start, end = u32(objectives, 0x1c), u32(objectives, 0x20)
    if start and end >= start and (end-start) % 40 == 0 and end-start <= 4096:
        if end != start:
            read(start, end-start, 'Fresh bounded forty-byte objective records')
    item_interface = u32(read(0x3b9f9c68, 4, 'Exact-profile item-system interface global'))
    if item_interface != u32(fw, 0x4b4) + 4:
        raise ValueError('Framework/global item-system disagreement')
    item_system = read(item_interface, 0x194, 'Fresh item-system interface and current-item tree root')
    table(item_system, 0x3988cf40)
    wanted, node = u32(inventory, 0x8c), u32(item_system, 0x190)
    sentinel, seen = item_interface+0x18c, set()
    for _ in range(20):
        if not wanted or not node or node == sentinel:
            break
        if node in seen:
            raise ValueError('Item tree cycle')
        seen.add(node)
        entry = read(node, 24, 'Fresh item tree node for current EntityId ' + hex(wanted))
        key = u32(entry, 0x10)
        if key == wanted:
            item = read(u32(entry, 0x14), 0x2e0, 'Matched current item: verify identity/owner/IWeapon before adopting fields')
            read(u32(item), 0x20, 'Actual current-item primary vtable')
            read(u32(item, 0x1bc), 0x84, 'Candidate embedded IWeapon interface vtable; diagnostic only')
            break
        node = u32(entry, 8 if wanted < key else 12)
    read(0x3b9fde30, 4, 'Final root stability check; not an atomic snapshot')
    print('NEXT_REQUEST_ID=' + str(next_id), flush=True)


if __name__ == '__main__':
    main()
