"""Compile the production emitter and exercise its ARM64 mailbox protocol.

Requires g++, Unicorn, and the supported uncompressed main image. Native calls
are ABI-clobbering test stubs; this does not prove game-side action completion.
"""
import argparse
import os
from pathlib import Path
import re
import struct
import subprocess

from test_botw_carry_grab import function


def build(source_path, image_path, work, compiler):
    source = source_path.read_text()
    constants = re.findall(r'constexpr u32 Xcx\w+\s*=.*?;', source, re.S)
    encoders = [function(source, name) for name in
                re.findall(r'constexpr u32 (Encode\w+)\(', source)]
    payload = (source_path.parent / 'xcx_companion_payload.h').read_text()
    payload = payload[payload.index('namespace XcxCompanionPayload'):]
    header = '''#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
using u8=uint8_t; using u16=uint16_t; using u32=uint32_t; using u64=uint64_t;
using s64=int64_t;
#define LOG_ERROR(...) do {} while (false)
#define LOG_INFO(...) do {} while (false)
'''
    main = '''
int main(int argc, char** argv) {
    std::ifstream input(argv[1], std::ios::binary);
    std::vector<u8> image((std::istreambuf_iterator<char>(input)), {});
    assert(image.size() > 0x1d00000);
    image.resize(0x47c1000);
    for (auto site : {XcxPartyManagerUpdateHookOffset, XcxQueueCharacterSwitchOffset,
                     XcxMapJumpOffset, XcxIsEventSceneOffset, XcxCollectBattleActorsOffset,
                     XcxInstallProbeOffset, XcxProbeInstalledOffset,
                     XcxTrackMissionOffset, XcxQuestPhaseOffset,
                     0x405D28U, 0x13BF80U, 0x405F5CU, 0x132EE8U, 0x132CB8U}) {
        image[site] ^= 1;
        assert(!InstallXcxCompanionActionHook(image, 0, 0x47bf000, 0x47c0000, 0x1000));
        image[site] ^= 1;
    }
    assert(!InstallXcxCompanionActionHook(image, 0, 0x47bf001, 0x47c0000, 0x1000));
    assert(InstallXcxCompanionActionHook(image, 0, 0x47bf000, 0x47c0000, 0x1000));
    std::ofstream out(argv[2], std::ios::binary);
    out.write(reinterpret_cast<char*>(image.data()+0x47c0000), 0x1000);
}
'''
    harness = work / 'xcx-emitter.cpp'
    harness.write_text(header + payload + '\n'.join(constants + encoders + [
        function(source, 'ReadCodeWord'), function(source, 'InstallXcxCompanionActionHook')]) + main)
    executable = work / 'xcx-emitter.exe'
    env = os.environ.copy()
    env['PATH'] = str(Path(compiler).parent) + os.pathsep + env.get('PATH', '')
    subprocess.run([compiler, '-std=c++20', '-O0', str(harness), '-o', str(executable)],
                   check=True, env=env)
    subprocess.run([str(executable), str(image_path), str(work / 'xcx-bridge.bin')],
                   check=True, env=env)


def test(work):
    from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE
    from unicorn import arm64_const as r
    mailbox, bridge, resume = 0x47bf000, 0x47c0000, 0x2fae5c
    blob = (work / 'xcx-bridge.bin').read_bytes()
    cases = [
        ('idle', 0, 0, 0, {}, 0),
        ('avatar', 1, 1, 0, {'avatar_swap': 1}, 1),
        ('companion', 1, 1, 11, {}, 1),
        ('third slot', 1, 1, 12, {}, 1),
        ('travel', 1, 2, 22, {}, 1),
        ('unknown action', 1, 7, 1, {}, 2),
        ('equip ranged', 1, 6, 0xF1234506, {}, 1),
        ('equip melee', 1, 6, 0xF1234507, {'equip_context': 0x10b}, 1),
        ('equip armor', 1, 6, 0xF1234501, {'equip_context': 0x20b}, 1),
        ('equip fashion', 1, 6, 0xF1234501, {'equip_context': 0x70b}, 1),
        ('equip wrong category', 1, 6, 0xF1234501, {}, 3),
        ('equip invalid slot', 1, 6, 0xF1234506, {'equip_context': 0xc0b}, 3),
        ('equip invalid character', 1, 6, 0xF1234506, {'equip_context': 23}, 3),
        ('equip invalid context', 1, 6, 0xF1234506, {'equip_context': 0x1000b}, 3),
        ('equip missing instance', 1, 6, 0xF1234506, {'no_item': 1}, 6),
        ('equip incompatible', 1, 6, 0xF1234506, {'incompatible': 1}, 7),
        ('equip owned by character', 1, 6, 0xF1234506, {'equipped': 1}, 3),
        ('equip missing character', 1, 6, 0xF1234506, {'no_character': 1}, 3),
        ('equip missing game state', 1, 6, 0xF1234506, {'no_party': 1}, 6),
        ('equip in skell', 1, 6, 0xF1234506, {'actor_type': 4}, 9),
        ('equip in battle', 1, 6, 0xF1234506, {'battle': 1}, 8),
        ('equip paused', 1, 6, 0xF1234506, {'pause': 1}, 5),
        ('equip event', 1, 6, 0xF1234506, {'event': 1}, 4),
        ('swap companion slots', 1, 5, 0x0c0b0201, {}, 1),
        ('swap reverse', 1, 5, 0x0b0c0102, {}, 1),
        ('swap third slot', 1, 5, 0x0d0c0302, {'fourth': 1}, 1),
        ('swap cannot change leader', 1, 5, 0x0b000100, {}, 3),
        ('swap cannot target leader', 1, 5, 0x000b0001, {}, 3),
        ('swap cannot target fifth slot', 1, 5, 0x000b0401, {}, 3),
        ('swap cannot use fifth slot', 1, 5, 0x0b000104, {}, 3),
        ('swap same slot', 1, 5, 0x0b0b0101, {}, 3),
        ('swap empty target', 1, 5, 0x000b0301, {}, 3),
        ('swap empty source', 1, 5, 0x0b000103, {}, 3),
        ('swap stale source', 1, 5, 0x0c0d0201, {}, 3),
        ('swap stale target', 1, 5, 0x0d0b0201, {}, 3),
        ('swap missing leader', 1, 5, 0x0c0b0201, {'empty_leader': 1}, 3),
        ('swap in skell', 1, 5, 0x0c0b0201, {'actor_type': 4}, 9),
        ('swap in battle', 1, 5, 0x0c0b0201, {'battle': 1}, 8),
        ('swap during event', 1, 5, 0x0c0b0201, {'event': 1}, 4),
        ('swap paused', 1, 5, 0x0c0b0201, {'pause': 1}, 5),
        ('swap missing party', 1, 5, 0x0c0b0201, {'no_party': 1}, 6),
        ('swap no active actor', 1, 5, 0x0c0b0201, {'no_active': 1}, 6),
        ('track mission', 1, 4, 21, {}, 1),
        ('track highest mission', 1, 4, 2558, {}, 1),
        ('clear mission', 1, 4, 0, {'phase': 0}, 1),
        ('mission above range', 1, 4, 2559, {}, 3),
        ('mission negative', 1, 4, 0xffffffff, {}, 3),
        ('mission not accepted', 1, 4, 21, {'phase': 0}, 3),
        ('mission completed', 1, 4, 21, {'phase': 2}, 3),
        ('mission ended', 1, 4, 21, {'phase': 3}, 3),
        ('mission context missing', 1, 4, 21, {'no_quests': 1}, 6),
        ('navigation context missing', 1, 4, 21, {'no_navigation': 1}, 6),
        ('probe change', 1, 3, (2 << 16) | 14, {}, 1),
        ('probe highest type', 1, 3, (39 << 16) | 143, {}, 1),
        ('probe missing site', 1, 3, 2 << 16, {}, 3),
        ('probe bad site', 1, 3, (2 << 16) | 144, {}, 3),
        ('probe missing type', 1, 3, 14, {}, 3),
        ('probe bad type', 1, 3, (40 << 16) | 14, {}, 3),
        ('probe not deployed', 1, 3, (2 << 16) | 14, {'undeployed': 1}, 7),
        ('probe inventory or cost rejected', 1, 3, (2 << 16) | 14, {'reject': 1}, 7),
        ('invalid character', 1, 1, 24, {}, 3),
        ('negative character', 1, 1, 0xffffffff, {}, 3),
        ('zero travel', 1, 2, 0, {}, 3),
        ('event', 1, 1, 11, {'event': 1}, 4),
        ('pause', 1, 1, 11, {'pause': 1}, 5),
        ('combat', 1, 1, 11, {'battle': 1}, 8),
        ('inner', 1, 1, 11, {'actor_type': 2}, 1),
        ('skell', 1, 1, 11, {'actor_type': 4}, 9),
        ('absent member', 1, 1, 23, {}, 3),
        ('missing party state', 1, 1, 11, {'no_party': 1}, 6),
        ('missing active slot', 1, 1, 11, {'no_active': 1}, 6),
        ('empty leader', 1, 1, 11, {'empty_leader': 1}, 3),
    ]
    for name, state, action, argument, flags, expected in cases:
        uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
        uc.mem_map(0, 0x47c1000)
        uc.mem_map(0x8000000, 0x10000)
        uc.mem_map(0x9000000, 0x10000)
        uc.mem_write(bridge, blob)
        def write64(address, value):
            uc.mem_write(address, struct.pack('<Q', value))
        write64(0x1d54ec0, 0x9000000)
        write64(0x9000000, 0x9001000)
        uc.mem_write(0x90010b0, bytes([flags.get('pause', 0)]))
        write64(0x1d557a8, 0x9002000)
        write64(0x26d7228, 0x9003000)
        write64(0x9003630, 0x9004000)
        uc.mem_write(0x90040b4, bytes([flags.get('actor_type', 2)]))
        write64(0x1d55110, 0x9005000)
        uc.mem_write(0x9005015, bytes([0 if flags.get('no_active') else 1,
                                     0 if flags.get('actor_type') == 4 else 1]))
        write64(0x1dc1408, 0 if flags.get('no_party') else 0x9100000)
        write64(0x1df5978, 0 if flags.get('no_navigation') else 0x9100000)
        write64(0x1dfc440, 0 if flags.get('no_quests') else 0x9007000)
        uc.mem_map(0x9100000, 0x20000)
        roster = [(0 if flags.get('empty_leader') else 1, 0), (1, 11), (1, 12), (0, 0)]
        if flags.get('fourth'): roster[3] = (2, 13)
        if flags.get('avatar_swap'): roster[0], roster[1] = roster[1], roster[0]
        for i, (kind, character) in enumerate(roster):
            uc.mem_write(0x911b5dc + i * 8, bytes([kind, character, 0, 0, 0, 0, 0, 0]))
        equipment_context = flags.get('equip_context', 11) if action == 6 else 0
        equipment_slot = (equipment_context >> 8) & 255
        equipment_record = 0x91000f8 + (equipment_context & 255) * 0x7f8
        if action == 6 and (equipment_context & 255) <= 22 and not flags.get('no_character'):
            uc.mem_write(equipment_record + 0x78, bytes([2, equipment_context & 255, 30]))
        if flags.get('equipped'):
            other = 0x91000f8 + 12 * 0x7f8
            uc.mem_write(other + 0x78, bytes([2, 12, 30]))
            uc.mem_write(other + 0x80, struct.pack('<HHI', 51, 0, argument))
        uc.mem_write(0x9008000, struct.pack('<I', 51 | ((argument & 63) << 13)))
        uc.mem_write(mailbox, struct.pack('<IHHQQIIIIII',
                                        0, 0, 0, 42, 0, state, action, argument, 0, 0, equipment_context))
        registers = [getattr(r, f'UC_ARM64_REG_X{i}') for i in range(19)]
        initial = [0x9006000] + [0x12340000 + i for i in range(1, 19)]
        for reg, value in zip(registers, initial):
            uc.reg_write(reg, value)
        for i in range(8):
            uc.reg_write(getattr(r, f'UC_ARM64_REG_Q{i}'), (0x12345678 << 64) + i)
        uc.reg_write(r.UC_ARM64_REG_SP, 0x800f000)
        uc.reg_write(r.UC_ARM64_REG_LR, 0x12345678)
        calls = []
        def native_stub(machine, address, size, data):
            if address not in (0xcc394, 0x1f5750, 0x1367b4, 0x131358, 0x324ffc, 0x5650e4,
                               0xd009c, 0x56e58c, 0x405d28, 0x13bf80, 0x405f5c,
                               0x132cb8, 0x132ee8):
                return
            x0, x1 = [machine.reg_read(reg) for reg in registers[:2]]
            result = 0
            if address == 0xcc394:
                result = flags.get('event', 0)
            elif address == 0x1f5750:
                write64(x1 + 0x400, flags.get('battle', 0))
            elif address == 0x324ffc:
                assert x0 == argument & 0xffff, name
                result = 0 if flags.get('undeployed') else 1
            elif address == 0x1367b4:
                pairs = list(struct.iter_unpack('<II', machine.mem_read(x0 + 12, 32)))
                wanted = roster.copy()
                if action == 5:
                    source, target = argument & 255, (argument >> 8) & 255
                    wanted[source], wanted[target] = wanted[target], wanted[source]
                else:
                    target = next(i for i, pair in enumerate(roster) if pair[0] and pair[1] == argument)
                    wanted[0], wanted[target] = wanted[target], wanted[0]
                assert pairs == wanted, (name, pairs, wanted)
                calls.append((address, argument))
            elif address == 0xd009c:
                assert x0 == argument and argument != 0, name
                result = flags.get('phase', 1)
            elif address == 0x56e58c:
                calls.append((address, x0))
            elif address == 0x405d28:
                assert x0 == equipment_slot, name
                result = [6, 7, 1, 2, 4, 3, 5, 1, 2, 4, 3, 5][x0]
            elif address == 0x13bf80:
                assert x0 == 0x1f919a0, name
                assert struct.unpack('<I', machine.mem_read(x1, 4))[0] == argument, name
                result = 0 if flags.get('no_item') else 0x9008000
            elif address == 0x405f5c:
                assert (x0, x1) == (equipment_record, equipment_slot), name
                assert machine.reg_read(registers[2]) == 51, name
                assert machine.reg_read(registers[3]) == int(equipment_slot < 7), name
                assert machine.reg_read(registers[4]) == 0, name
                result = 0 if flags.get('incompatible') else 1
            elif address in (0x132cb8, 0x132ee8):
                assert (x0, x1) == (equipment_record, 0x9008000), name
                if address == 0x132cb8:
                    assert [machine.reg_read(registers[i]) for i in (2, 3)] == [1, 0], name
                else:
                    assert [machine.reg_read(registers[i]) for i in (2, 3, 4)] == [int(equipment_slot >= 7), 1, 0], name
                calls.append((address, argument))
            elif address == 0x5650e4:
                calls.append((address, x0, x1))
                out = machine.reg_read(registers[2])
                assert 0x8000000 <= out < 0x8010000, name
                machine.mem_write(out, struct.pack('<I', 3 if flags.get('reject') else 0))
                result = 0xffffffff if flags.get('reject') else 0
            else:
                calls.append((address, x0, x1))
                result = 0 if flags.get('reject') else 1
            for reg in registers:
                machine.reg_write(reg, 0xdeadbeef)
            for i in range(8):
                machine.reg_write(getattr(r, f'UC_ARM64_REG_Q{i}'), 0xdeadbeef)
            machine.reg_write(registers[0], result)
            machine.reg_write(r.UC_ARM64_REG_PC, machine.reg_read(r.UC_ARM64_REG_LR))
        uc.hook_add(UC_HOOK_CODE, native_stub)
        try:
            uc.emu_start(bridge, resume, count=2000)
        except Exception as error:
            raise AssertionError((name, hex(uc.reg_read(r.UC_ARM64_REG_PC)),
                                  [hex(uc.reg_read(reg)) for reg in registers])) from error
        assert uc.reg_read(r.UC_ARM64_REG_PC) == resume, name
        fields = struct.unpack('<IHHQQIIIIII', uc.mem_read(mailbox, 48))
        assert fields[:3] == (0x42435858, 1, 48), (name, fields)
        assert uc.reg_read(r.UC_ARM64_REG_SP) == 0x800efe0, name
        # x9/x10/x15 are scratch before saving, as in the displaced function.
        for i in (0, 1, 2, 3, 4, 5, 6, 7, 8, 11, 12, 13, 14, 16, 17, 18):
            assert uc.reg_read(registers[i]) == initial[i], (name, i)
        for i in range(8):
            assert uc.reg_read(getattr(r, f'UC_ARM64_REG_Q{i}')) == (0x12345678 << 64) + i
        if state:
            assert fields[3:6] == (0, 42, 3 if expected == 1 else 4), (name, fields)
            assert fields[8] == expected, (name, fields)
        if expected == 1:
            expected_calls = {1: [(0x1367b4, argument)],
                              2: [(0x131358, argument, 0)],
                              3: [(0x5650e4, argument & 0xffff, argument >> 16)],
                              4: [(0x56e58c, argument)],
                              5: [(0x1367b4, argument)],
                              6: [(0x132cb8 if equipment_slot < 2 else 0x132ee8, argument)]}
            assert calls == expected_calls[action], (name, calls)
        elif expected != 7:
            assert not calls, (name, calls)
    print(f'XCX action emitter: {len(cases)} ARM64 protocol cases passed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--compiler', required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    source_path = Path(__file__).resolve().parents[2] / 'src/core/loader/nso.cpp'
    build(source_path, args.image, args.work, args.compiler)
    test(args.work)
