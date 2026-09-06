"""Execute the actual loader-emitted trace; prove it doesn't alter native call state.

Requires host g++, Unicorn and the supported locally extracted main image.
Generated harness/binaries are test artifacts, not game assets for distribution.
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
    constants = re.findall(r'constexpr u32 Plza\w+\s*=.*?;', source, re.S)
    encoders = [function(source, name) for name in
                re.findall(r'constexpr u32 (Encode\w+)\(', source)]
    header = '''#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
using u8=uint8_t; using u16=uint16_t; using u32=uint32_t;
using u64=uint64_t; using s64=int64_t;
#define LOG_ERROR(...) do {} while(false)
#define LOG_INFO(...) do {} while(false)
'''
    main = '''
int main(int argc, char** argv) {
    std::ifstream in(argv[1],std::ios::binary);
    std::vector<u8> image((std::istreambuf_iterator<char>(in)),{});
    image.resize(0x6205000,0);
    for(auto site : {PlzaFieldPreUpdateHookOffset,PlzaBagNativeDispatchOffset,0xA5A070U,0xA5C988U,0xA5C51CU,0x2BB90CU}) {
        for(unsigned word=0;word<(site==PlzaFieldPreUpdateHookOffset?1:4);++word) {
            image[site+word*4]^=1;
            assert(!InstallPlzaCompanionItemHook(image,0,0x6200000,0x6201000,0x4000));
            image[site+word*4]^=1;
        }
    }
    for(auto signature : PlzaItemPayload::Signatures) {
        for(unsigned word=0;word<4;++word) {
            image[signature.rva+word*4]^=1;
            assert(!InstallPlzaCompanionItemHook(image,0,0x6200000,0x6201000,0x4000));
            image[signature.rva+word*4]^=1;
        }
    }
    assert(!InstallPlzaCompanionItemHook(image,0,0x6200001,0x6201000,0x4000));
    assert(InstallPlzaCompanionItemHook(image,0,0x6200000,0x6201000,0x4000));
    assert(!InstallPlzaCompanionItemHook(image,0,0x6200000,0x6201000,0x4000));
    std::ofstream full(std::string(argv[2])+".full",std::ios::binary);
    full.write(reinterpret_cast<char*>(image.data()+0x6201000),0x4000);
    full.write(reinterpret_cast<char*>(image.data()+0x2bb00c),4);
    std::ofstream out(argv[2],std::ios::binary);
    out.write(reinterpret_cast<char*>(image.data()+0x6201000),4096);
    for(auto site : {PlzaBagNativeDispatchOffset,0xA5A070U,0xA5C988U,0xA5C51CU,0x2BB90CU,PlzaFieldPreUpdateHookOffset})
        out.write(reinterpret_cast<char*>(image.data()+site),4);
}
'''
    harness = work / 'plza-emitter.cpp'
    loader = Path(__file__).resolve().parents[2]/'src/core/loader'
    code_header = (loader/'plza_item_code.h').read_text().replace('#include "common/common_types.h"', '')
    protocol = (loader/'plza_item_protocol.h').read_text()
    harness.write_text(header + code_header + protocol + '\nusing namespace Loader;\n' + '\n'.join(constants + encoders + [
        function(source, 'ReadCodeWord'), function(source, 'InstallPlzaCompanionItemHook')]) + main)
    exe = work / 'plza-emitter.exe'
    env = os.environ.copy()
    env['PATH'] = str(Path(compiler).parent) + os.pathsep + env.get('PATH', '')
    subprocess.run([compiler, '-std=c++20', '-O0', str(harness), '-o', str(exe)],check=True,env=env)
    subprocess.run([str(exe),str(image_path),str(work/'plza-trace.bin')],check=True,env=env)


def test(work):
    from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE
    from unicorn import arm64_const as r
    blob = (work/'plza-trace.bin').read_bytes()
    for base in (0x100000000, 0x700002000):
        for busy in (False, True):
            uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
            mailbox, bridge, dispatch = base+0x6200000,base+0x6201000,base+0xc150e4
            player, stack = 0x200000,0x302000
            for address,size in [(mailbox,8192),(dispatch&~4095,4096),(player,4096),(stack-4096,8192)]:
                uc.mem_map(address,size)
            uc.mem_write(bridge,blob[:4096])
            uc.mem_write(dispatch,blob[4096:4100])
            def write(address,value,fmt='Q'): uc.mem_write(address,struct.pack('<'+fmt,value))
            def read(address,fmt='Q'): return struct.unpack('<'+fmt,uc.mem_read(address,struct.calcsize(fmt)))[0]
            xregs = [getattr(r,f'UC_ARM64_REG_X{i}') for i in range(31)]
            qregs = [getattr(r,f'UC_ARM64_REG_Q{i}') for i in range(32)]
            for i,reg in enumerate(xregs): uc.reg_write(reg,0x12340000+i*0x100)
            for i,reg in enumerate(qregs): uc.reg_write(reg,(i+1)<<96)
            uc.reg_write(r.UC_ARM64_REG_X0,player)
            uc.reg_write(r.UC_ARM64_REG_SP,stack)
            uc.reg_write(r.UC_ARM64_REG_NZCV,0xB0000000)
            before = [uc.reg_read(reg) for reg in xregs+qregs]
            write(mailbox+0x80,int(busy),'I')
            write(mailbox+0x88,5)
            write(mailbox,123)
            write(player+0x140,17|(4<<16),'I')
            for i in range(5): write(player+0x108+i*8,0x400000+i*0x1000)
            write(player+0x68,0x500000)
            game_before = bytes(uc.mem_read(player,4096))
            uc.emu_start(dispatch,dispatch+4,count=200)
            assert uc.reg_read(r.UC_ARM64_REG_PC)==dispatch+4
            assert [uc.reg_read(reg) for reg in xregs+qregs]==before
            assert uc.reg_read(r.UC_ARM64_REG_SP)==stack-0x80
            assert uc.reg_read(r.UC_ARM64_REG_NZCV)==0xB0000000
            assert bytes(uc.mem_read(player,4096))==game_before
            assert read(mailbox+0x88)==(5 if busy else 6)
            assert read(mailbox+0x80,'I')==int(busy)
            if not busy:
                assert read(mailbox+0x90)==player
                assert read(mailbox+0x98)==before[30]
                assert read(mailbox+0xA0)==123
                assert read(mailbox+0xA8,'I')==17|(4<<16)
                assert read(mailbox+0xD8)==0x500000
            print(f'PASS base={base:#x} busy={busy}: registers, flags, stack, game memory, publication')

        for number,(site,offset,stack_size) in enumerate([
                (0xA5A070,0x100,0x30),(0xA5C988,0x180,0x50),
                (0xA5C51C,0x200,0x70),(0x2BB90C,0x280,0x130),(0x31232C,0,0x60)],1):
            for mode in ('clear', 'busy', 'contended', 'null_actor'):
                if mode == 'null_actor' and site != 0xA5C51C: continue
                busy = mode == 'busy'
                uc=Uc(UC_ARCH_ARM64,UC_MODE_ARM)
                mailbox,bridge,entry=base+0x6200000,base+0x6201000,base+site
                stack=0x302000
                for address,size in [(mailbox,8192),(entry&~4095,4096),
                                     (0x200000,4096),(stack-4096,8192)]: uc.mem_map(address,size)
                uc.mem_write(bridge,blob[:4096])
                uc.mem_write(entry,blob[4096+number*4:4100+number*4])
                for i,reg in enumerate(xregs):uc.reg_write(reg,0x12340000+i*0x100)
                for i,reg in enumerate(qregs):uc.reg_write(reg,(i+1)<<96)
                # Native retained-ref parameters; bag x1/x2 are scalar item/count.
                for i in range(3):
                    uc.reg_write(xregs[i],0x200000+i*0x10)
                    uc.mem_write(0x200000+i*0x10,struct.pack('<Q',0x200100+i*0x10))
                    uc.mem_write(0x200100+i*0x10,struct.pack('<Q',0x400000+i*0x10))
                if mode == 'null_actor': uc.mem_write(0x200020,bytes(8))
                if mode == 'contended' and offset:
                    # Emulate a failed exclusive store without modifying game state.
                    def collide(machine, address, size, data):
                        if bytes(machine.mem_read(address,4)) == struct.pack('<I',0x880BFD2A):
                            machine.reg_write(r.UC_ARM64_REG_W11,1)
                            machine.reg_write(r.UC_ARM64_REG_PC,address+4)
                    uc.hook_add(UC_HOOK_CODE,collide)
                uc.reg_write(r.UC_ARM64_REG_SP,stack)
                uc.reg_write(r.UC_ARM64_REG_NZCV,0xB0000000)
                before=[uc.reg_read(reg) for reg in xregs+qregs]
                uc.mem_write(mailbox+offset,struct.pack('<I',int(busy)))
                game_before=bytes(uc.mem_read(0x200000,4096))
                uc.emu_start(entry,entry+4,count=200)
                assert uc.reg_read(r.UC_ARM64_REG_PC)==entry+4
                assert [uc.reg_read(reg) for reg in xregs+qregs]==before
                assert uc.reg_read(r.UC_ARM64_REG_NZCV)==0xB0000000
                assert uc.reg_read(r.UC_ARM64_REG_SP)==stack-stack_size
                assert bytes(uc.mem_read(0x200000,4096))==game_before
                if offset:
                    seq=struct.unpack('<Q',uc.mem_read(mailbox+offset+8,8))[0]
                    published = mode in ('clear', 'null_actor')
                    assert seq==int(published)
                    if published:
                        event=struct.unpack('<10Q',uc.mem_read(mailbox+offset+8,80))
                        assert list(event[1:4])==before[:3] and event[4]==before[30]
                        if site==0xA5C988:
                            assert event[6:10]==(0x200100,0x200110,0x400000,0x400010)
                        if site==0xA5C51C:
                            assert event[6]==(0 if mode=='null_actor' else 0x200120)
                            assert event[8]==(0 if mode=='null_actor' else 0x400020)
                else:
                    assert bytes(uc.mem_read(mailbox,4096))==struct.pack('<I',int(busy))+bytes(4092)
                if site==0xA5A070:
                    assert struct.unpack('<2Q',uc.mem_read(stack-0x30,16))==tuple(before[29:31])
                print(f'PASS base={base:#x} native={site:#x} mode={mode}: ABI and read-only')


if __name__ == '__main__':
    p=argparse.ArgumentParser()
    p.add_argument('--image',type=Path,required=True)
    p.add_argument('--work',type=Path,required=True)
    p.add_argument('--compiler',required=True)
    args=p.parse_args()
    args.work.mkdir(parents=True,exist_ok=True)
    build(Path(__file__).resolve().parents[3]/'mods/plza/native/hooks.cpp',args.image,args.work,args.compiler)
    test(args.work)
