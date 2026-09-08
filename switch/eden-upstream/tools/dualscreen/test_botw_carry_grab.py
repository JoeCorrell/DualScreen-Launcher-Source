"""Compile the actual loader emitter and execute its CarryBox predicate in Unicorn.

No APK build or device access. Requires a host C++ compiler, capstone and unicorn.
The supported raw main image supplies all installer signature checks.
"""
import argparse
import os
from pathlib import Path
import re
import struct
import subprocess
import sys


def function(source, name):
    start = source.index(name + '(')
    start = source.rfind('\n', 0, start) + 1
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def build(source_path, image_path, work, compiler):
    source = source_path.read_text()
    constants = re.findall(r'constexpr (?:u32|std::string_view) Botw\w+\s*=.*?;', source, re.S)
    encoders = [function(source, name) for name in
                re.findall(r'constexpr u32 (Encode\w+)\(', source)]
    header = '''#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string_view>
#include <vector>
using u8=uint8_t; using u16=uint16_t; using u32=uint32_t; using u64=uint64_t;
using s64=int64_t; using u32_le=uint32_t;
#define ASSERT_MSG(expr, ...) assert(expr)
#define LOG_ERROR(...) do {} while (false)
#define LOG_INFO(...) do {} while (false)
'''
    main = '''
int main(int argc, char** argv) {
    std::ifstream input(argv[1], std::ios::binary);
    std::vector<u8> image((std::istreambuf_iterator<char>(input)), {});
    assert(image.size() > 0x1d00000);
    image.resize(0x1e9d000, 0);
    {
        auto mismatch = image;
        mismatch[BotwCarryGrabReadyHookOffset] ^= 1;
        const auto before = mismatch;
        assert(!InstallBotwCompanionEquipHook(mismatch, 0, 0x1e98000, 0x1e9c000, 0x1000));
        assert(mismatch == before); // signature rejection must not partly patch the game
    }
    assert(InstallBotwCompanionEquipHook(image, 0, 0x1e98000, 0x1e9c000, 0x1000));
    std::ofstream out(argv[2], std::ios::binary);
    out.write(reinterpret_cast<char*>(image.data()), image.size());
    const auto hook = BuildBotwCarryGrabReadyHook(0x1e9ce00);
    std::ofstream stub(argv[3], std::ios::binary);
    stub.write(reinterpret_cast<const char*>(hook.data()), hook.size()*4);
}
'''
    # Mechanical extraction: exercise the production C++ emitter, not a reimplementation.
    harness = work / 'carry_emitter.cpp'
    harness.write_text(header + '\n'.join(constants + encoders + [
        function(source, 'ReadCodeWord'), function(source, 'BuildBotwCarryGrabReadyHook'),
        function(source, 'InstallBotwCompanionEquipHook')]) + main)
    executable = work / 'carry_emitter.exe'
    env = os.environ.copy()
    env['PATH'] = str(Path(compiler).parent) + os.pathsep + env.get('PATH', '')
    subprocess.run([compiler, '-std=c++20', '-O0', str(harness), '-o', str(executable)],
                   check=True, env=env)
    subprocess.run([str(executable), str(image_path), str(work / 'patched-main.bin'),
                    str(work / 'carry-ready-stub.bin')], check=True, env=env)


def test(work):
    from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM
    from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_MEM_WRITE
    from unicorn import arm64_const as reg
    code = (work / 'carry-ready-stub.bin').read_bytes()
    base, stub, end = 0x100000000, 0x1e9ce00, 0x806d24
    print('\n'.join(f'{i.address-base:08x}: {i.mnemonic} {i.op_str}'
                    for i in Cs(CS_ARCH_ARM64, CS_MODE_ARM).disasm(code, base+stub)))
    cases = [dict(name='attached', result=1), dict(name='native pending', pending=1, result=1),
             dict(name='native pending bypasses unavailable player', pending=1, info=0, result=1),
             dict(name='no info', info=0), dict(name='no player', player=0),
             dict(name='no parent', parent=0), dict(name='wrong parent', parent=0x777),
             dict(name='no child', child=0), dict(name='wrong child', child=0x888),
             dict(name='not carrying', carrying=0), dict(name='not attached', attached=0)]
    cases += [dict(name=f'helper state {state}', state=state) for state in [-1, 0, 1, 3, 4]]
    registers = [getattr(reg, f'UC_ARM64_REG_X{i}') for i in range(31)]
    for case in cases:
        for flags in (0, 0xf0000000, 0xa0000000):
            uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
            info, player, carry, ai, stack = (0x200000, 0x210000, 0x220000, 0x230000, 0x240800)
            for address in [base+stub, base+end, base+0x1e13ed0, info, player, carry, ai, stack]:
                uc.mem_map(address & ~0xfff, 0x1000)
            uc.mem_write(base+stub, code)
            def write(address, value, fmt='Q'):
                uc.mem_write(address, struct.pack('<'+fmt, value))
            write(base+0x1e13ed0, case.get('info', info))
            write(info+0x60, case.get('player', player))
            write(carry+0x80, case.get('parent', player))
            write(player+0x88, case.get('child', carry))
            write(player+0xd11, case.get('carrying', 1), 'B')
            write(carry+0x92c, case.get('attached', 1), 'B')
            write(carry+0x970, case.get('state', 2), 'i')
            write(ai+0x68, case.get('pending', 0), 'B')
            for i, register in enumerate(registers):
                uc.reg_write(register, 0xabc00000+i)
            uc.reg_write(reg.UC_ARM64_REG_X19, ai)
            uc.reg_write(reg.UC_ARM64_REG_X21, carry)
            uc.reg_write(reg.UC_ARM64_REG_SP, stack)
            uc.reg_write(reg.UC_ARM64_REG_NZCV, flags)
            before = [uc.reg_read(r) for r in registers]
            writes = []
            uc.hook_add(UC_HOOK_MEM_WRITE, lambda u, a, address, size, v, data:
                        writes.append((address, size)))
            uc.emu_start(base+stub, base+end, count=100)
            assert uc.reg_read(reg.UC_ARM64_REG_PC) == base+end, case
            assert uc.reg_read(reg.UC_ARM64_REG_X8) == case.get('result', 0), case
            assert all(i == 8 or uc.reg_read(r) == before[i]
                       for i, r in enumerate(registers)), case
            assert uc.reg_read(reg.UC_ARM64_REG_SP) == stack, case
            assert uc.reg_read(reg.UC_ARM64_REG_NZCV) == flags, case
            assert all(stack-16 <= a and a+s <= stack for a, s in writes), case
    print(f'PASS: supported-image install and atomic signature rejection; {len(cases)*3} '
          'AArch64 paths; registers, SP, NZCV preserved; no game-state writes')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--compiler', required=True)
    parser.add_argument('--deps', type=Path)
    args = parser.parse_args()
    if args.deps:
        sys.path.insert(0, str(args.deps.resolve()))
    args.work.mkdir(parents=True, exist_ok=True)
    build(Path(__file__).resolve().parents[3] / 'mods/botw/native/hooks.cpp', args.image,
          args.work.resolve(), args.compiler)
    test(args.work)
