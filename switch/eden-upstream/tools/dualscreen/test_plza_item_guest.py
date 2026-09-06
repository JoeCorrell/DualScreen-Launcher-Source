"""Execute the production field-selector trampoline and compiled guest in Unicorn.

The typed bag resolver, reference release and selection persistence execute the
user's actual game instructions. Only the inventory-list refresh is mocked.
No host translation of the action implementation. Tests include complete ABI
restoration after native helpers intentionally destroy caller-saved registers.
"""
import argparse
from pathlib import Path
import struct
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE
from unicorn import arm64_const as r

p = argparse.ArgumentParser()
p.add_argument('--work', type=Path, required=True)
p.add_argument('--image', type=Path, required=True)
a = p.parse_args()
blob = (a.work/'plza-trace.bin.full').read_bytes()
game = a.image.read_bytes()
for base in (0x100000000, 0x700002000):
    for case in ('success', 'already_selected', 'controller_change', 'scene_change',
                 'bag_change', 'empty_stack', 'wrong_pouch', 'missing_from_list',
                 'input_busy', 'suspended', 'invalid_operation', 'invalid_target',
                 'corrupt_vector', 'idle', 'no_persistence', 'unready', 'cancelled',
                 'party', 'party_unavailable', 'party_invalid', 'party_changed_hp'):
        uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
        mailbox, bridge, entry = base+0x6200000, base+0x6201000, base+0x2bb00c
        presenter, model, root, persistent, bag = 0x200000,0x201000,0x202000,0x203000,0x210000
        vector, stack = 0x204000,0x302000
        for address,size in ((mailbox,0x5000),(base+0x610a000,0x4000),
                             (entry&~4095,4096),(base+0x2bc000,4096),
                             (base+0xa88000,4096),(base+0x99e000,4096),
                             (base+0x291a000,4096),(base+0x3d59000,4096),
                             (0x200000,0x20000),(stack-4096,8192)):
            uc.mem_map(address,size)
        for page in (0x43000,0x4de000,0x556000,0x407000,0x3000,0x97000,0xd000,0x18000,0x5000):
            uc.mem_map(base+page,4096)
        for rva in (0x2bc000,0x99e000,0x291a000,0x2bb000):
            uc.mem_write(base+rva,game[rva:rva+4096])
        uc.mem_write(bridge,blob[:0x4000]); uc.mem_write(entry,blob[0x4000:])
        def put(address,v,fmt='Q'): uc.mem_write(address,struct.pack('<'+fmt,v))
        def get(address,fmt='Q'): return struct.unpack('<'+fmt,uc.mem_read(address,struct.calcsize(fmt)))[0]
        def ref(obj,control,tag):
            put(obj+0x38,control); put(control,tag); put(control+0x10,obj+0x28)
            put(control+0x18,2,'H'); put(control+0x1a,2,'H'); put(control+0x1c,3,'I')
        ref(root,0x205000,0x12345); ref(persistent,0x205040,0x23456)
        ref(bag,0x205080,0x34567)
        put(bag+0x28,base+0x3d59a50); put(base+0x3d59a50,base+0x291af34)
        put(base+0x610a710,0x206000); put(base+0x610a718,0x206008); put(0x206000,bag)
        for offset in (0x610a980,0x610b8c0,0x610d940,0x610dc00): put(base+offset,1,'I')
        put(base+0x610b810,root); put(base+0x610db60,persistent)
        put(presenter+0x130,model); put(presenter+0x138,1,'I')
        put(model+0x50,vector); put(model+0x58,vector+12)
        for i,item in enumerate((4,3,2)): put(vector+i*4,item,'I')
        put(persistent+0x40,4,'I')
        put(bag+0x40+3*16,1,'I'); put(bag+0x44+3*16,6,'I')
        put(mailbox,10); put(mailbox+0x10,1,'I'); put(mailbox+0x14,1,'I')
        put(mailbox+0x18,3,'I'); put(mailbox+0x1c,-1,'i'); put(mailbox+0x20,1,'I')
        put(mailbox+0x2c,0x5a414954,'I'); put(mailbox+0x30,root)
        put(mailbox+0x38,0x12345); put(mailbox+0x40,bag); put(mailbox+0x58,1,'I')
        if case=='already_selected': put(model+0x4c,1,'I'); put(persistent+0x40,3,'I')
        if case=='scene_change': put(mailbox+0x38,0x999)
        if case=='bag_change': put(mailbox+0x40,bag+16)
        if case=='empty_stack': put(bag+0x44+3*16,0,'I')
        if case=='wrong_pouch': put(bag+0x40+3*16,0,'I')
        if case=='missing_from_list': put(vector+4,5,'I')
        if case=='input_busy': put(presenter+0x13c,1,'B')
        if case=='suspended': put(presenter+0x110,1,'B')
        if case=='invalid_operation': put(mailbox+0x14,2,'I')
        if case=='invalid_target': put(mailbox+0x1c,0,'i')
        if case=='corrupt_vector': put(model+0x58,vector+20000)
        if case=='idle': put(mailbox+0x10,0,'I')
        if case=='cancelled': put(mailbox+0x10,4,'I')
        if case=='no_persistence': put(base+0x610db60,0)
        if case=='unready': put(base+0x610dc00,0,'I')
        party_case=case.startswith('party')
        if party_case:
            put(mailbox+0x10,0,'I'); put(mailbox+0x300,1,'I')
            pokemon=0x207000
            ref(pokemon,0x2050c0,0x45678)
            put(pokemon,base+0x3e2a438); put(pokemon+0x50,0x208000); put(0x208070,0x209000)
            if case=='party_invalid': put(pokemon,base+0x123456)
        calls=[]
        party_calls=[]
        hp=63
        def native(machine,address,size,data):
            if party_case and address-base in (0x2bb680,0x43b10,0x4de870,0x556ca0,0x407900,
                                               0x3668,0x3678,0x975a0,0xdb9c,0x18d80,0x5d30):
                rva=address-base; party_calls.append(rva)
                arg=machine.reg_read(r.UC_ARM64_REG_X0)
                output=machine.reg_read(r.UC_ARM64_REG_X8)
                value=0
                if rva in (0x2bb680,0x4de870):
                    assert stack-4096 < output < stack
                    if rva==0x4de870: assert arg==root and machine.reg_read(r.UC_ARM64_REG_W1)==0
                    put(output, (0 if case=='party_unavailable' else root) if rva==0x2bb680 else pokemon)
                elif rva==0x43b10: assert arg==root; value=1
                elif rva==0x556ca0: assert get(arg)==pokemon
                elif rva==0x407900: assert get(arg)==(0 if case=='party_unavailable' else root)
                else:
                    assert arg==pokemon
                    value={0x3668:160,0x3678:0,0x975a0:70,0xdb9c:hp,0x18d80:283,0x5d30:0}[rva]
                machine.reg_write(r.UC_ARM64_REG_X0,value)
                machine.reg_write(r.UC_ARM64_REG_PC,machine.reg_read(r.UC_ARM64_REG_X30)); return
            if address not in (base+0x2bcac8,base+0x99e6b4,base+0xa88620,base+0x2bb7dc): return
            calls.append(address-base)
            result=0
            if address==base+0x2bcac8:
                # The real function returns a retained reference through x8.
                output=machine.reg_read(r.UC_ARM64_REG_X8)
                assert stack-4096 < output < stack
                return # execute the real resolver, including ref acquisition
            elif address==base+0x99e6b4:
                assert get(machine.reg_read(r.UC_ARM64_REG_X0))==bag
                return # real release must balance the acquired strong reference
            else:
                assert machine.reg_read(r.UC_ARM64_REG_X0)==model
                if address==base+0x2bb7dc:
                    return # real persistent write / strong-reference lifetime
            lr=machine.reg_read(r.UC_ARM64_REG_X30)
            for i in range(19): machine.reg_write(getattr(r,f'UC_ARM64_REG_X{i}'),0xdead0000+i)
            for i in list(range(8))+list(range(16,32)): machine.reg_write(getattr(r,f'UC_ARM64_REG_Q{i}'),0xface<<96)
            machine.reg_write(r.UC_ARM64_REG_NZCV,0)
            machine.reg_write(r.UC_ARM64_REG_X0,result)
            machine.reg_write(r.UC_ARM64_REG_PC,lr)
        uc.hook_add(UC_HOOK_CODE,native)
        def run():
            regs=[getattr(r,f'UC_ARM64_REG_X{i}') for i in range(31)]+[getattr(r,f'UC_ARM64_REG_Q{i}') for i in range(32)]
            for i,reg in enumerate(regs): uc.reg_write(reg,0x12340000+i*0x100)
            uc.reg_write(r.UC_ARM64_REG_X0,presenter); uc.reg_write(r.UC_ARM64_REG_SP,stack)
            uc.reg_write(r.UC_ARM64_REG_NZCV,0xb0000000)
            uc.reg_write(r.UC_ARM64_REG_FPCR,0x400000)
            before=[uc.reg_read(reg) for reg in regs]
            uc.emu_start(entry,entry+4,count=100000)
            assert uc.reg_read(r.UC_ARM64_REG_PC)==entry+4
            assert uc.reg_read(r.UC_ARM64_REG_SP)==stack-0x30
            assert [uc.reg_read(reg) for reg in regs]==before
            assert uc.reg_read(r.UC_ARM64_REG_NZCV)==0xb0000000
            assert uc.reg_read(r.UC_ARM64_REG_FPCR)==0x400000
        original_bag=bytes(uc.mem_read(bag,0xc000))
        original_index=get(model+0x4c,'I')
        run()
        if case in ('success','already_selected','controller_change'):
            assert get(mailbox+0x10,'I')==2 and get(mailbox+8)==0
            assert calls==[0x2bcac8,0x99e6b4,0xa88620,0x2bb7dc]
            assert get(model+0x4c,'I')==1 and get(persistent+0x40,'I')==3
            if case=='controller_change': put(model+0x4c,2,'I'); put(persistent+0x40,2,'I')
            run()
            assert get(mailbox+8)==10
            assert get(mailbox+0x10,'I')==(4 if case=='controller_change' else 3)
            assert len(calls)==4 # no reapplication; subsequent controller wins
            run(); assert len(calls)==4
        elif party_case:
            assert calls==[]
            assert get(mailbox+0x304,'I')==1
            assert get(mailbox+0x30c,'I')==int(case not in ('party_invalid','party_unavailable'))
            if case not in ('party_invalid','party_unavailable'):
                assert get(mailbox+0x308,'I')==1
                assert get(mailbox+0x320)==pokemon
                assert get(mailbox+0x336,'H')==63 and get(mailbox+0x338,'H')==283
                if case=='party_changed_hp':
                    hp=83; put(mailbox+0x300,2,'I'); run()
                    assert get(mailbox+0x304,'I')==2 and get(mailbox+0x336,'H')==83
            before_calls=len(party_calls); run(); assert len(party_calls)==before_calls
        elif case in ('idle','cancelled'):
            assert calls==[] and get(mailbox+8)==0
        else:
            assert get(mailbox+0x10,'I')==4 and get(mailbox+8)==10
            assert get(model+0x4c,'I')==original_index and 0x2bb7dc not in calls
        assert bytes(uc.mem_read(bag,0xc000))==original_bag # equipping never consumes
        for control in (0x205000,0x205040,0x205080):
            assert get(control+0x1a,'H')==2 and get(control+0x18,'H')==2
        print(f'PASS base={base:#x} {case}: native selection, bounds, lifecycle, ABI')
