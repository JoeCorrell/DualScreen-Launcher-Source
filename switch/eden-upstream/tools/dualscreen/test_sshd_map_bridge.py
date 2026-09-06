"""Exercise the production SSHD emitter's ABI and render-completion handshake.

Requires a host C++ compiler, Unicorn, and the supported uncompressed main image.
The native initializer is replaced by an ABI-clobbering stub, not emulated game code.
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
    constants = re.findall(r'constexpr u32 Sshd\w+\s*=.*?;', source, re.S)
    encoders = [function(source, name) for name in
                re.findall(r'constexpr u32 (Encode\w+)\(', source)]
    payload = (source_path.parent / 'sshd_companion_payload.h').read_text()
    payload = payload[payload.index('namespace Loader::'):]
    ui_code = (source_path.parent / 'sshd_map_ui_code.h').read_text()
    payload += ui_code[ui_code.index('namespace Loader::'):]
    header = '''#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
using u8=uint8_t; using s8=int8_t; using u16=uint16_t;
using u32=uint32_t; using u64=uint64_t; using s64=int64_t; using u32_le=uint32_t;
#define ASSERT_MSG(expr, ...) assert(expr)
#define LOG_ERROR(...) do {} while (false)
#define LOG_INFO(...) do {} while (false)
'''
    main = '''
int main(int argc, char** argv) {
    std::ifstream input(argv[1], std::ios::binary);
    std::vector<u8> image((std::istreambuf_iterator<char>(input)), {});
    image.resize(0x2d035000, 0);
    for (auto site : {SshdMapCaptureHookOffset, SshdMapCompleteHookOffset,
                     SshdMapInitializeOffset, SshdMapUiHookOffset, SshdMapCameraHookOffset}) {
        image[site] ^= 1;
        assert(!InstallSshdCompanionTelemetryPayload(image, 0, 0x2d033000, 0x2d034000, 0x1000));
        image[site] ^= 1;
    }
    assert(InstallSshdCompanionTelemetryPayload(image, 0, 0x2d033000, 0x2d034000, 0x1000));
    std::ofstream out(argv[2], std::ios::binary);
    for (auto [offset, size] : std::array<std::pair<u32,u32>,6>{{
        {0x2d034000, 0x1000}, {SshdTelemetryBridgeOffset,64},
        {SshdMapCaptureHookOffset,4}, {SshdMapCompleteHookOffset,4}, {SshdMapUiHookOffset,4}, {SshdMapCameraHookOffset,4}}}) {
        out.write(reinterpret_cast<char*>(image.data()+offset), size);
    }
}
'''
    harness = work / 'sshd-emitter.cpp'
    harness.write_text(header + payload + '\nusing namespace Loader;\n' + '\n'.join(
        constants + encoders + [function(source, 'ReadCodeWord'),
                               function(source, 'InstallSshdCompanionTelemetryPayload')]) + main)
    executable = work / 'sshd-emitter.exe'
    env = os.environ.copy()
    env['PATH'] = str(Path(compiler).parent) + os.pathsep + env.get('PATH', '')
    subprocess.run([compiler, '-std=c++20', '-O0', str(harness), '-o', str(executable)],
                   check=True, env=env)
    subprocess.run([str(executable), str(image_path), str(work / 'sshd-bridge.bin')],
                   check=True, env=env)


def test(work):
    from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE
    from unicorn import arm64_const as r
    blob = (work / 'sshd-bridge.bin').read_bytes()
    base, mailbox, bridge = 0x100000000, 0x12d033000, 0x12d034000
    stage, texture, map_main, stack = 0x200000, 0x220000, 0x300000, 0x601000
    map_global, capture = map_main + 0x10d38, map_main + 0x19e0
    xregs = [getattr(r, f'UC_ARM64_REG_X{i}') for i in range(31)]
    qregs = [getattr(r, f'UC_ARM64_REG_Q{i}') for i in range(32)]
    for name in ['idle', 'cold', 'cold-reused-owner', 'initialized', 'pending', 'completion', 'ack',
                 'ui-disabled', 'ui-recursion', 'ui-offscreen', 'ui-other-channel', 'ui-draw', 'ui-hidden', 'camera-original', 'camera-companion']:
        uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
        pages = set()
        def mapping(address, size=0x1000):
            for page in range(address & ~0xfff, (address+size+0xfff)&~0xfff, 0x1000):
                if page not in pages:
                    uc.mem_map(page, 0x1000)
                    pages.add(page)
        for address, size in [(bridge,0x1000), (mailbox,0x1000), (base+0x105c950,64), (base+0xc46c20,64),
                              (base+0xe2391c,8), (base+0xe280b4,8), (base+0xc448e0,8),
                              (base+0xc64000,0x1000), (base+0x12cc8,8), (base+0xef4b8c,8),
                              (base+0x4cbdb6d,1), (0x500000,0x1d000), (base+0x2c8ed70,8), (base+0x2c8efd0,8), (base+0x2c8f100,8), (base+0x4cc0960,8), (base+0x4cc3134,4),
                              (base+0xc853a4,8), (base+0xc5caa0,8), (base+0xc57418,8),
                              (base+0x10448e0,8), (base+0x16cac94,8),
                              (base+0x183c440,8), (base+0x2bf98d8,0x500),
                              (stage,0x3000), (texture,0x1000), (map_main,0x12000),
                              (stack-0x1000,0x2000)]:
            mapping(address,size)
        uc.mem_write(bridge,blob[:4096])
        uc.mem_write(base+0x105c950,blob[4096:4160])
        uc.mem_write(base+0xe2391c,blob[4160:4164])
        uc.mem_write(base+0xe280b4,blob[4164:4168])
        uc.mem_write(base+0x12cc8,blob[4168:4172])
        uc.mem_write(base+0xc46c20,blob[4172:4176])
        def write(address,value,fmt='Q'):
            uc.mem_write(address,struct.pack('<'+fmt,value))
        def read(address,fmt='I'):
            return struct.unpack('<'+fmt,uc.mem_read(address,struct.calcsize(fmt)))[0]
        write(base+0x2c8ed70,0x517000)
        write(base+0x2c8efd0,0x518000)
        write(base+0x4cc03a9,1,"B")
        write(base+0x2c8f100,0x516000)
        write(base+0x4cc0960,0x12345678)
        write(base+0x183c440,map_global)
        write(base+0x2bf9d90,stage)
        write(base+0x2bf9940,0x500000)
        uc.mem_write(base+0x2bf98d8,b'F300\0\0\0\0')
        write(map_main+0x1a8,0x510000)
        write(0x510000,0x514000)
        write(0x514018,0x515000)
        write(0x514070,0x511000)
        hints={b'N_backZoom_00':0x50a000,b'N_input_03':0x50b000,b'N_nameRotate_00':0x50c000}
        for pane in hints.values(): write(pane+0xeb,0x5,'B')
        write(map_main+0x10798,0x512000)
        write(map_main+0x1a58,0x513000)
        write(0x513000,0x51c000)
        write(0x51c0c8,0x519000)
        write(0x51a050,0x51b000)
        write(0x51b020,0xabcd0000,"I")
        write(stage+0x3bc,1,'B')
        write(stage+0x550,texture)
        write(stage+0x562,1,'B')
        write(texture+8,812,'H')
        write(texture+10,456,'H')
        write(texture+0xd,5,'B')
        write(texture+0x18,0x230000)
        write(map_global+0x44,1.,'f')
        if not name.startswith('cold'):
            write(capture+0xa0,812.,'f')
            write(capture+0xa8,456.,'f')
        if name == 'cold-reused-owner':
            write(mailbox+0xa0,map_main)
            write(mailbox+0xb0,stage)
            write(mailbox+0x94,500,'I')
        if name != 'idle':
            write(mailbox+0x50,7,'I')
        if name in ['pending','completion','ack']:
            write(mailbox+0x4c,2 if name=='ack' else 1,'I')
            write(mailbox+0x54,7,'I')
            write(mailbox+0x68,stage)
        if name=='ack':
            write(mailbox+0x58,7,'I')
            write(mailbox+0x5c,7,'I')
        if name.startswith('ui-'):
            write(mailbox+0x50,0,'I')
            write(mailbox+0x90,0 if name=='ui-disabled' else 1,'I')
            write(mailbox+0x9c,1 if name=='ui-recursion' else 0,'I')
            write(mailbox+0xc0,1,'I')
            write(map_main+0x10d27,0 if name=='ui-hidden' else 1,'B')
            write(base+0x4cbdb6d,1 if name=='ui-offscreen' else 0,'B')
            write(0x500144,123.5,'f')
            write(0x50014c,-432.25,'f')
            write(0x50013e,0x3456,'H')
            write(map_global+0x4e,73,'B')
            write(base+0x16cac94,1280,'I')
            write(base+0x16cac98,720,'I')
        before = [0x900000+i*0x10 for i in range(31)]
        before[8] = stage
        before[20] = stage
        if name.startswith('ui-'):
            before[0] = map_main
            before[20] = base+0x4cbdba0+(0x2dd0 if name=='ui-other-channel' else 0)
        for reg,value in zip(xregs,before): uc.reg_write(reg,value)
        vectors = [(0x123456789abcdef << 64) | i for i in range(32)]
        for reg,value in zip(qregs,vectors): uc.reg_write(reg,value)
        uc.reg_write(r.UC_ARM64_REG_SP,stack)
        uc.reg_write(r.UC_ARM64_REG_NZCV,0xa0000000)
        uc.reg_write(r.UC_ARM64_REG_CPACR_EL1,3<<20)
        calls=[]
        def on_code(emu,address,size,_):
            if address in [base+0xef4b8c,base+0x10448e0,base+0xc642f4,
                           base+0xc853a4,base+0xc5caa0,base+0xc57418,0x511000,0x515000,0x516000,0x517000,0x518000,0x519000]:
                assert emu.reg_read(r.UC_ARM64_REG_SP)%16==0
                result=texture if address==base+0xef4b8c else 0x51a000 if address==0x519000 else 0
                if address==0x511000:
                    assert emu.reg_read(r.UC_ARM64_REG_X0)==0x510000
                    assert emu.reg_read(r.UC_ARM64_REG_X2)==1
                    name_bytes=bytes(emu.mem_read(emu.reg_read(r.UC_ARM64_REG_X1),16)).split(b'\0')[0]
                    result=hints[name_bytes]
                if address==0x516000:
                    assert calls[-3:]==[base+0x10448e0,0x517000,0x518000]
                    assert emu.reg_read(r.UC_ARM64_REG_X0)==0x12345678
                    assert emu.reg_read(r.UC_ARM64_REG_X1)==0
                    assert emu.reg_read(r.UC_ARM64_REG_X3)==15
                    assert bytes(emu.mem_read(emu.reg_read(r.UC_ARM64_REG_X2),16))==bytes(16)
                if address==base+0xc642f4:
                    assert calls[-1]==0x516000
                    assert read(mailbox+0x9c)==1
                    assert read(map_global+0x4e,'B')==255
                    assert emu.reg_read(r.UC_ARM64_REG_X0)==map_main
                    for pane in hints.values(): assert read(pane+0xeb,'B')==0x4
                if address==base+0xc853a4:
                    assert emu.reg_read(r.UC_ARM64_REG_X0)==map_main+0x3790
                if address in [base+0xc57418,base+0xc5caa0]:
                    assert emu.reg_read(r.UC_ARM64_REG_X0)==map_main+0x2790
                if address==base+0xc57418:
                    assert emu.reg_read(r.UC_ARM64_REG_S0)==read(map_global+0x64)
                if address==0x517000:
                    assert emu.reg_read(r.UC_ARM64_REG_X0)==base+0x4cc0924
                    assert emu.reg_read(r.UC_ARM64_REG_X1)==0
                    assert [emu.reg_read(xregs[i]) for i in range(2,5)]==[1,1,1]
                    assert emu.reg_read(r.UC_ARM64_REG_X5)==(0 if base+0xc642f4 in calls else 1)
                if address==0x518000:
                    assert emu.reg_read(r.UC_ARM64_REG_X0)==0x12345678
                    assert emu.reg_read(r.UC_ARM64_REG_X1)==base+0x4cc0924
                calls.append(address)
                ret=emu.reg_read(r.UC_ARM64_REG_LR)
                for reg in xregs[:19]: emu.reg_write(reg,0xdead)
                for reg in qregs: emu.reg_write(reg,0xbeef)
                emu.reg_write(r.UC_ARM64_REG_X0,result)
                emu.reg_write(r.UC_ARM64_REG_NZCV,0)
                emu.reg_write(r.UC_ARM64_REG_PC,ret)
                return
            if address != base+0xc448e0: return
            assert emu.reg_read(r.UC_ARM64_REG_X0)==map_main
            assert read(mailbox+0x9c)==2
            assert emu.reg_read(r.UC_ARM64_REG_SP)%16==0
            calls.append(address)
            ret=emu.reg_read(r.UC_ARM64_REG_LR)
            for reg in xregs[:19]: emu.reg_write(reg,0xdead)
            for reg in qregs: emu.reg_write(reg,0xbeef)
            emu.reg_write(r.UC_ARM64_REG_NZCV,0)
            write(capture+0xa0,812.,'f')
            write(capture+0xa8,456.,'f')
            emu.reg_write(r.UC_ARM64_REG_PC,ret)
        uc.hook_add(UC_HOOK_CODE,on_code)
        hook=base+(0x12cc8 if name.startswith('ui-') else 0xe280b4 if name=='completion' else 0xe2391c)
        end=hook+4
        if name.startswith('camera-'):
            hook=base+0xc46c20
            write(mailbox+0x9c,2 if name=='camera-companion' else 0,'I')
            end=base+(0xc46c4c if name=='camera-companion' else 0xc46c24)
        uc.emu_start(hook,end,count=5000)
        assert uc.reg_read(r.UC_ARM64_REG_PC)==end, name
        assert uc.reg_read(r.UC_ARM64_REG_SP)==stack, name
        assert uc.reg_read(r.UC_ARM64_REG_NZCV)==0xa0000000, name
        for i,(reg,value) in enumerate(zip(xregs,before)):
            expected = (base+0x1642000 if i==9 and name=='camera-original' else value) if name.startswith('camera-') else hook+4 if i==30 else stack+0x60 if i==29 and name!='completion' and not name.startswith('ui-') else value
            assert uc.reg_read(reg)==expected,(name,'x',i,hex(uc.reg_read(reg)),hex(expected))
        for i,(reg,value) in enumerate(zip(qregs,vectors)):
            assert uc.reg_read(reg)==value,(name,'q',i)
        expected_state={'idle':0,'cold':1,'cold-reused-owner':1,'initialized':1,'pending':1,'completion':2,'ack':0}.get(name,0)
        assert read(mailbox+0x4c)==expected_state,(name,read(mailbox+0x4c))
        assert len(calls)==(17 if name=='ui-hidden' else 13 if name=='ui-draw' else 1 if name.startswith('cold') else 0),(name,calls)
        for pane in hints.values(): assert read(pane+0xeb,'B')==0x5
        assert read(mailbox+0xcc)==(1 if name.startswith('cold') else 0)
        if name=='cold-reused-owner':
            assert read(mailbox+0xa0,'Q')==0
            assert read(mailbox+0x94)==0
        if name=='ui-hidden':
            assert read(map_global+0x30,'f')==123.5
            assert read(map_global+0x38,'f')==-432.25
            assert read(map_main+0x3790+0xa3e,'H')==0x3456
            assert read(mailbox+0x94)==1
            assert read(map_main+0x10d27,'B')==0
        if name=='ui-draw':
            assert read(mailbox+0x94)==1
            assert read(mailbox+0x98)==7
            assert read(mailbox+0x9c)==0
            assert read(map_global+0x4e,'B')==73
            assert calls==[0x511000]*3+[0x519000,base+0xef4b8c,base+0x10448e0,0x517000,0x518000,0x516000,base+0xc642f4,0x517000,0x518000,base+0x10448e0]
        if name in ['ui-hidden','ui-draw']:
            assert read(0x51b000,'Q')==0x230000
            assert read(0x51b010)==(456<<16)|812
            assert read(0x51b020)==0xabcd0005
            assert read(0x513078,'f')==812.
            assert read(0x51307c,'f')==456., (read(0x51307c,'f'),read(map_main+0x1a90,'f'))
            assert read(map_main+0x1a78)==0x230000
        assert read(base+0x4cc03a9,'B')==1
        assert read(base+0x4cc03aa,'B')==0
        if name=='completion':
            assert read(stage+0x562,'B')==0
            assert read(mailbox+0x58)==7
            assert read(mailbox+0x70,'Q')==texture
        print('PASS',name)


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--image',type=Path,required=True)
    parser.add_argument('--work',type=Path,required=True)
    parser.add_argument('--compiler',required=True)
    args=parser.parse_args()
    args.work.mkdir(parents=True,exist_ok=True)
    build(Path(__file__).resolve().parents[2]/'src/core/loader/nso.cpp',
          args.image.resolve(),args.work.resolve(),args.compiler)
    test(args.work)
