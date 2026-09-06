#!/usr/bin/env python3
"""Bounded, read-only Odyssey base capture. Always resume on debugger exit.

Static contracts come from the user's base NSO; no heap scanning, breakpoints,
function calls, registers, inventory, flags or save bytes are written.
"""
import argparse
import hashlib
import json
import math
import re
import struct
from pathlib import Path

from export_nso_symbols import verify_image
from probe_la_state import Remote  # Generic bounded GDB transport; never LA snapshot.

BUILD = '3ca12dfaaf9c82da064d1698df79cda100000000000000000000000000000000'
IMAGE_SHA256 = 'ff81888c00d1374aef1522d6ef36169f4788de0759760aad8a2a0ed0e24fbf04'
SIGNATURES = (0x5369FC, 0x4CC348, 0x515788)
APPLICATION_INSTANCE = 0x1E665C8
VTABLES = {'gameSystem': 0x1D99818, 'sequence': 0x1D96FE0,
           'scene': 0x1D8BB70, 'holder': 0x1D996B8, 'player': 0x1D779D0}
# PlayerActorHakoniwa has a virtual-base table prefix: its constructor at
# 0x41b290 installs vtable+0x18, unlike the +0x10 address points above.
VTABLE_ADDRESS_POINTS = {key: rva + (24 if key == 'player' else 16)
                         for key, rva in VTABLES.items()}


def snapshot(remote, image, include_map=False):
    remote.command('qSupported')
    info = remote.monitor('info')
    if '0x0100000000010000' not in info.lower():
        raise ValueError('Not attached to Odyssey')
    matches = re.findall(r'(0x[0-9a-f]+) - (0x[0-9a-f]+) RedStar\.nss', info)
    if len(matches) != 1:
        raise ValueError('Cannot uniquely identify Odyssey main module')
    base = int(matches[0][0], 16)
    if not all(remote.read(base + rva, 32) == image[rva:rva + 32] for rva in SIGNATURES):
        raise ValueError('Live main does not match supported base build')

    def ptr(address, alignment=8):
        value, = struct.unpack('<Q', remote.read(address, 8))
        if value == 0 or value % alignment or value >= 1 << 39:
            raise ValueError(f'Unavailable/invalid pointer at {address:#x}: {value:#x}')
        return value

    def obj(address, kind):
        value = ptr(address)
        actual = ptr(value)
        if actual != base + VTABLE_ADDRESS_POINTS[kind]:
            raise ValueError(f'Not the expected {kind} object: vtable RVA={actual - base:#x}')
        return value

    def string(address):
        raw = remote.read(address, 96)
        end = raw.find(b'\0')
        if end < 0:
            raise ValueError('Unterminated native string')
        return raw[:end].decode('utf-8')

    application = ptr(base + APPLICATION_INSTANCE)
    framework = ptr(application + 0x28)
    task_manager = ptr(framework + 0x38)
    root_task = ptr(task_manager + 0x128)
    game = obj(root_task + 0x200, 'gameSystem')
    sequence = obj(game + 0x10, 'sequence')
    scene = obj(sequence + 0xB0, 'scene')
    holder = obj(scene + 0x2D0, 'holder')
    scene_objects = ptr(ptr(scene + 0xA0) + 8)
    if ptr(scene_objects + 0x12 * 8) != holder:
        raise ValueError('Scene and scene-object table disagree on GameDataHolder')
    save = ptr(holder + 0x20)
    scene_info = ptr(scene + 0x90)
    player_holder = ptr(scene_info + 0x98)
    # Constructor writes capacity at +8; registerPlayer increments active count +12.
    players, capacity, count = struct.unpack('<Qii', remote.read(player_holder, 16))
    if not (0 < count <= capacity <= 4) or players == 0 or players % 8:
        raise ValueError('PlayerHolder is unavailable or outside bounded capacity')
    player = obj(players, 'player')
    pose = ptr(player + 0x50)
    position = struct.unpack('<fff', remote.read(pose + 8, 12))
    if not all(math.isfinite(v) for v in position):
        raise ValueError('Non-finite player position')
    values = remote.read(save + 0x5D8, 0x460)
    coins = struct.unpack_from('<i', values, 0)[0]
    world = struct.unpack_from('<i', values, 0x9F0 - 0x5D8)[0]
    scenario = struct.unpack_from('<i', values, 0xA24 - 0x5D8)[0]
    if not 0 <= world < 17 or coins < 0 or scenario < 0:
        raise ValueError('Invalid world/coin/scenario snapshot')
    life = ptr(save + 0x860)
    health, = struct.unpack('<i', remote.read(life + 0xC, 4))
    outfit = string(ptr(save + 0x718, alignment=1))
    hat = string(ptr(save + 0x770, alignment=1))
    # Corresponds to GameDataFile::getCoinCollectNum, not all-time collection count.
    total, = struct.unpack('<i', remote.read(ptr(save + 0xA00) + 4 * world, 4))
    spent, = struct.unpack('<i', remote.read(ptr(save + 0x5E8) + 4 * world, 4))
    map_state = None
    if include_map:
        stage = string(ptr(scene + 0xe0, alignment=1))
        world_list = ptr(holder + 0x190)
        main_stage = string(ptr(ptr(ptr(world_list + 8) + 8 * world), alignment=1))
        maps = ptr(holder + 0x1e0)
        if ptr(maps + 0x10) != holder:
            raise ValueError('MapDataHolder owner mismatch')
        map_count, = struct.unpack('<i', remote.read(maps, 4))
        if not world < map_count <= 32:
            raise ValueError('Map count invalid')
        selected = ptr(ptr(maps + 8) + world * 8)
        map_scenario, = struct.unpack('<i', remote.read(ptr(save + 0x7d8) + world * 4, 4))
        child_count, = struct.unpack('<i', remote.read(selected + 0xd8, 4))
        if not 0 <= child_count <= 32:
            raise ValueError('Map variant count invalid')
        if child_count:
            children = ptr(selected + 0xe0)
            for i in range(child_count):
                child = ptr(children + 8 * i)
                variant, = struct.unpack('<i', remote.read(child + 0xd0, 4))
                if variant == map_scenario:
                    selected = child
                    break
        data = remote.read(selected, 0xe8)
        matrix = struct.unpack_from('<16f', data)
        variant, = struct.unpack_from('<i', data, 0xd0)
        projected = [sum(matrix[row * 4 + i] * position[i] for i in range(3)) +
                     matrix[row * 4 + 3] for row in range(2)]
        map_state = dict(stage=stage, mainStage=main_stage, worldScenario=map_scenario,
            variant=variant, texture=main_stage + (str(variant) if variant > 0 else ''),
            dimensions=struct.unpack_from('<II', data, 0xb8), matrix=matrix,
            projectedUV=[(projected[0] + 1) / 2, (1 - projected[1]) / 2])
    result = {'schema': 1, 'build': BUILD, 'base': hex(base),
            'pointers': {k: hex(v) for k, v in dict(application=application,
                gameSystem=game, sequence=sequence, scene=scene, holder=holder,
                save=save, player=player).items()},
            'playerPosition': position, 'worldId': world, 'scenario': scenario,
            'coins': coins, 'regionalCoins': total - spent, 'health': health,
            'outfit': outfit, 'hat': hat,
            'status': 'diagnostic snapshot, not a completed companion protocol'}
    if include_map:
        result['map'] = map_state
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=6543)
    parser.add_argument('--nso', type=Path, required=True)
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--map', action='store_true', help='Also capture bounded native map metadata')
    args = parser.parse_args()
    nso, image = args.nso.read_bytes(), args.image.read_bytes()
    verify_image(image, nso)
    if nso[0x40:0x60].hex() != BUILD or hashlib.sha256(image).hexdigest() != IMAGE_SHA256:
        raise ValueError('Unsupported local base image')
    for rva in SIGNATURES:
        if image.count(image[rva:rva + 32]) != 1:
            raise ValueError('Ambiguous base signature')
    remote = Remote(args.port)
    try:
        result = snapshot(remote, image, args.map)
    finally:
        remote.close()
        print('Continue sent; debugger disconnected')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
