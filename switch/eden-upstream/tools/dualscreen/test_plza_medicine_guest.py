"""Run the compiled production medicine bridge under AArch64 emulation.

Native A5C988 recovery dispatch/calculations execute from the user's game image.
Other game services are explicit ABI-checking test doubles, not a claim of live
gameplay validation. All reference ownership and one-shot consumption is checked.
"""
import argparse
import struct
from pathlib import Path
from collections import Counter
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE
from unicorn import arm64_const as r

p = argparse.ArgumentParser()
p.add_argument('--work', type=Path, required=True)
p.add_argument('--image', type=Path, required=True)
a = p.parse_args()
blob = (a.work / 'plza-trace.bin.full').read_bytes()
game = a.image.read_bytes()
CASES = ('potion', 'super_potion', 'percentage', 'revive', 'full_hp', 'fainted',
         'revive_alive', 'status_cure', 'wrong_status', 'condition_cure', 'full_restore',
         'empty_stack', 'last_item', 'party_changed', 'actor_mismatch', 'no_actor',
         'no_binding', 'disabled_command', 'battle_locked', 'pokemon_locked',
         'native_consume_failed', 'unsupported', 'delayed_effect', 'missing_effect',
         'scene_after_consume', 'root_after_consume', 'unready_after_consume',
         'db_unready', 'allocator_unready', 'bag_changed')

for base in (0x100000000, 0x700002000):
    for case in CASES:
        uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
        mailbox, bridge, entry = base + 0x6200000, base + 0x6201000, base + 0x2bb00c
        stack = 0x403000
        uc.mem_map(base, 0x6300000)
        uc.mem_map(0x200000, 0x100000)
        uc.mem_map(stack - 0x10000, 0x11000)
        uc.mem_write(bridge, blob[:0x4000]); uc.mem_write(entry, blob[0x4000:])
        # Real recovery routine plus its percentage calculation helper.
        uc.mem_write(base + 0xa5c988, game[0xa5c988:0xa5cb18])
        uc.mem_write(base + 0xa5b650, game[0xa5b650:0xa5b66c])
        uc.mem_write(base + 0xc2be3c, game[0xc2be3c:0xc2bfe8])
        def put(addr, val, fmt='Q'): uc.mem_write(addr, struct.pack('<' + fmt, val))
        def get(addr, fmt='Q'): return struct.unpack('<' + fmt, uc.mem_read(addr, struct.calcsize(fmt)))[0]
        def ref(obj, control, tag):
            put(obj + 0x38, control); put(control, tag); put(control + 0x10, obj + 0x28)
            put(control + 0x1a, 2, 'H'); put(control + 0x1c, 3, 'I')
        presenter, model, root, bag, pokemon = 0x200000, 0x201000, 0x202000, 0x210000, 0x220000
        wrapper, actor, adapter = 0x230000, 0x231000, 0x232000
        db, service, inventory, records, allocator = [0x240000 + i * 0x1000 for i in range(5)]
        first, second, owner, registry = 0x250000, 0x251000, 0x252000, 0x253000
        ref(root, 0x260000, 123); ref(pokemon, 0x260040, 456)
        put(pokemon, base + 0x3e2a438); put(pokemon + 0x50, 0x261000); put(0x261070, 0x262000)
        put(actor, base + 0x3be0d28); put(adapter, base + 0x3da1970); put(adapter + 0x30, actor)
        put(adapter + 8, allocator)
        put(allocator, 0x270000); put(0x270010, base + 0x123400)
        put(first, 0x270100); put(second, 0x270100); put(0x270118, base + 0x123404)
        put(base + 0x3be0d28 + 0x60, base + 0x123408)
        for off, addr in ((0x3ec3320, db), (0x3ec2c38, service), (0x3ec32b0, inventory), (0x3ec68d8, records)):
            put(base + off, addr)
        put(base + 0x3ec2228, base + 0x3c00000)
        for off in (0x610b8c0, 0x610dc00, 0x610d940, 0x610a980, 0x610dec0, 0x610e100): put(base + off, 1, 'I')
        for obj, off in ((db, 0x80), (service, 0x140), (inventory, 0x140), (records, 0x280)): put(obj + off, 1, 'I')
        put(base + 0x610b810, root); put(presenter + 0x130, model); put(presenter + 0x138, 1, 'I')
        put(model + 8, allocator); put(model + 0x50, 0x280000); put(model + 0x58, 0x280004); put(0x280000, 3, 'I')
        put(mailbox, 10); put(mailbox + 0x10, 1, 'I'); put(mailbox + 0x14, 2, 'I')
        put(mailbox + 0x18, 17, 'I'); put(mailbox + 0x1c, 0, 'i'); put(mailbox + 0x20, 0, 'I')
        put(mailbox + 0x2c, 0x5a414954, 'I'); put(mailbox + 0x30, root); put(mailbox + 0x38, 123)
        put(mailbox + 0x40, bag); put(mailbox + 0x58, 1, 'I')
        d = mailbox + 0x400
        put(d, pokemon); put(d + 8, 456)
        quantity = 0 if case == 'empty_stack' else 1 if case == 'last_item' else 44
        put(bag + 0x44 + 17 * 16, quantity, 'I')
        hp = 0 if case in ('revive', 'fainted') else 100 if case in ('full_hp', 'status_cure', 'wrong_status', 'condition_cure') else 63
        old_hp = hp
        status = 1 if case in ('status_cure', 'wrong_status', 'full_restore') else 0
        condition = int(case == 'condition_cure')
        fixed = 0 if case in ('percentage', 'revive', 'revive_alive', 'status_cure', 'wrong_status', 'condition_cure') else 60 if case == 'super_potion' else 20
        percent = 50 if case == 'percentage' else 0
        revive = 50 if case in ('revive', 'revive_alive') else 0
        has_status = case in ('status_cure', 'wrong_status', 'full_restore')
        has_condition = case == 'condition_cure'
        if case == 'party_changed': put(d + 8, 789)
        if case == 'disabled_command': put(owner + 0x1d1, 1, 'B')
        if case == 'db_unready': put(base + 0x610dec0, 0, 'I')
        if case == 'allocator_unready': put(model + 8, 0)
        if case == 'bag_changed': put(mailbox + 0x40, bag + 8)
        # Real A5C988 calls these native virtual recovery operations.
        for off, fn in ((0x28, 0xc2be3c), (0x30, 0x123414), (0x38, 0x123418)):
            put(base + 0x3da1970 + off, base + fn)
        for off, fn in ((0x78, 0x18d80), (0x80, 0xdb9c)): put(base + 0x3e2a438 + off, base + fn)
        ledger = Counter(); calls = []; consumes = 0; dispatched = 0; pending_hp = None
        command_values = {}
        def acquire(output, obj):
            assert stack - 0x10000 < output < stack
            put(output, obj)
            if obj: ledger[obj] += 1
        def release(obj):
            if obj:
                ledger[obj] -= 1
                assert ledger[obj] >= 0, (case, hex(obj), calls[-8:])
        def native(machine, address, size, data):
            global hp, status, condition, consumes, dispatched, pending_hp
            fn = address - base
            helpers = {0x2bb680, 0x43b10, 0x4de870, 0x556ca0, 0x407900, 0x3668, 0x3678, 0x975a0,
                       0xdb9c, 0x18d80, 0x5d30, 0x941600, 0xc31f10, 0xc31f84, 0xa5b588,
                       0xa5b5c8, 0xa5b548, 0xa5b608, 0x941b80, 0xa5cb18, 0xc31ff0, 0xc32194,
                       0xa5b7e0, 0x18e44, 0xa5bd0c, 0x2d2c78, 0x8eae80, 0x5f8d2c, 0x5f8e90,
                       0x9beab0, 0x5534e0, 0x52d688, 0xb63b0, 0xdb220, 0x72af0, 0xa5c670,
                       0x2bcac8, 0x99e6b4, 0xa5a070, 0xbcea78, 0xa5d060, 0x986638,
                       0x120500, 0x696f10, 0x9bb3f0, 0x123404, 0x123408, 0x123410, 0x123414, 0x123418,
                       0x52d5dc, 0xd88b8, 0x52d53c, 0x52d49c,
                       0x421c, 0xfb600, 0x5f441c, 0xc2bfe8, 0x1e344, 0x6a740}
            if fn == 0xa5c988:
                assert get(machine.reg_read(r.UC_ARM64_REG_X0)) == adapter
                assert get(machine.reg_read(r.UC_ARM64_REG_X1)) == pokemon
                assert machine.reg_read(r.UC_ARM64_REG_W2) == 17
                dispatched += 1
                return  # EXECUTE game's real medicine dispatcher/calculation
            if fn not in helpers: return
            x = [machine.reg_read(getattr(r, f'UC_ARM64_REG_X{i}')) for i in range(9)]
            calls.append(fn); value = 0
            if fn == 0x2bb680: acquire(x[8], root)
            elif fn == 0x43b10: assert x[0] == root; value = 6
            elif fn == 0x4de870: assert x[0:2] == [root, 0]; acquire(x[8], pokemon)
            elif fn in (0x556ca0, 0x407900, 0xb63b0, 0x696f10, 0x99e6b4, 0x52d53c, 0x52d49c): release(get(x[0]))
            elif fn == 0x52d5dc: assert x[0] == base + 0x610b780; acquire(x[8], service)
            elif fn == 0xd88b8: assert x[0] == service and get(x[1] + 8) == 0x12345678; acquire(x[8], pokemon)
            elif fn in (0x3668, 0x3678, 0x975a0, 0xdb9c, 0x18d80, 0x5d30, 0xc31ff0):
                assert x[0] == pokemon
                value = {0x3668:160, 0x3678:0, 0x975a0:70, 0xdb9c:hp, 0x18d80:100, 0x5d30:status, 0xc31ff0:condition}[fn]
            elif fn in (0x941600, 0xa5b588, 0xa5b5c8, 0xa5b548, 0x941b80, 0xa5cb18, 0xa5b7e0, 0xc32194):
                assert x[:2] == [db, 17]
                value = {0x941600:case != 'unsupported', 0xa5b588:fixed, 0xa5b5c8:percent,
                         0xa5b548:revive, 0x941b80:has_status, 0xa5cb18:has_condition,
                         0xa5b7e0:0, 0xc32194:case != 'wrong_status'}[fn]
            elif fn == 0xa5b608: assert get(x[0]) == pokemon; value = 50
            elif fn == 0xc31f10: assert x[:2] == [root, 0]; value = case == 'pokemon_locked'
            elif fn == 0xc31f84: assert x[0] == root; value = case == 'battle_locked'
            elif fn == 0x18e44: assert x[:2] == [pokemon, 2]
            elif fn == 0xa5bd0c: assert x[:2] == [root, 0]; acquire(x[8], wrapper)
            elif fn == 0x2d2c78: assert x[0] == wrapper; acquire(x[8], 0 if case == 'no_actor' else actor)
            elif fn == 0x8eae80: assert x[0] == actor; put(x[8], 0x12345678)
            elif fn == 0x5f8d2c:
                assert x[0] == service and get(x[1]) == base + 0x3c00010 and get(x[1] + 8) == 0x12345678
                put(x[8], 0x9876)
            elif fn == 0x5f8e90: assert get(x[0]) == 0x9876
            elif fn == 0x9beab0: assert get(x[0]) == 0x9876; acquire(x[8], registry)
            elif fn == 0x5534e0: assert x[0] == registry; acquire(x[8], pokemon)
            elif fn == 0x52d688:
                assert get(x[1]) == pokemon
                # Real 52D688 moves the retained source, it does not copy it.
                put(x[1], 0); put(x[0], pokemon)
                if case == 'actor_mismatch':
                    release(pokemon); acquire(x[0], pokemon + 8)
            elif fn == 0xdb220:
                assert x[0] == actor + 0x118
                for i, v in enumerate((0xdddd, 1, 2, owner)): put(x[8] + i * 8, v)
            elif fn == 0x72af0:
                assert get(x[0]) == 0xdddd
                acquire(x[8], first); acquire(x[8] + 8, 0 if case == 'no_binding' else second); put(x[8] + 16, owner)
            elif fn == 0xa5c670: assert x[0] == allocator and get(x[1]) == actor; acquire(x[8], adapter)
            elif fn == 0x2bcac8: acquire(x[8], bag)
            elif fn == 0xa5a070:
                assert x[:3] == [inventory, 17, 1]
                value = case != 'native_consume_failed'
                if value:
                    consumes += 1; put(bag + 0x44 + 17 * 16, quantity - 1, 'I')
            elif fn == 0xbcea78: assert x[0] == root
            elif fn == 0xa5d060: assert x[:4] == [17, 160, 0, 70]
            elif fn == 0x986638: assert x[0] in (10, 11) and x[1] == 1
            elif fn in (0x120500, 0x123404, 0x123408): release(x[0])
            elif fn == 0x9bb3f0: acquire(x[0], get(x[1]))
            elif fn == 0x421c:
                assert x[0] in (base + 0x3da19e0, base + 0x3da19f8, base + 0x3da1a10)
                for i, part in enumerate((x[0], x[0], 0, 0)): put(x[8] + i * 8, part)
            elif fn == 0xfb600: pass
            elif fn == 0x5f441c:
                assert x[1] == allocator and get(x[2]) == base + 0x3da19f8
                command_values[x[0]] = x[3]
            elif fn == 0xc2bfe8:
                assert x[1] == allocator and get(x[2]) == base + 0x3da1a10 and x[0] in command_values
            elif fn == 0x1e344: del command_values[x[0]]
            elif fn == 0x6a740:
                assert x[0] == owner + 0x130 and x[2] == 0
                assert get(x[1] + 0x10) == base + 0x3da19e0
                assert get(x[1] + 0xb0, 'B') == 1
                amount = command_values[x[1]]
                if case == 'delayed_effect': pending_hp = min(100, hp + amount)
                elif case not in ('missing_effect', 'scene_after_consume'): hp = min(100, hp + amount)
            elif fn == 0x123414: assert x[0] == adapter; status = 0
            elif fn == 0x123418: assert x[0] == adapter; condition = 0
            machine.reg_write(r.UC_ARM64_REG_X0, int(value))
            machine.reg_write(r.UC_ARM64_REG_PC, machine.reg_read(r.UC_ARM64_REG_X30))
        uc.hook_add(UC_HOOK_CODE, native)
        def run():
            regs = [getattr(r, f'UC_ARM64_REG_X{i}') for i in range(31)] + [getattr(r, f'UC_ARM64_REG_Q{i}') for i in range(32)]
            for i, reg in enumerate(regs): uc.reg_write(reg, 0x12340000 + i * 0x100)
            uc.reg_write(r.UC_ARM64_REG_X0, presenter); uc.reg_write(r.UC_ARM64_REG_SP, stack)
            uc.reg_write(r.UC_ARM64_REG_FPCR, 0x400000)
            uc.reg_write(r.UC_ARM64_REG_NZCV, 0xb0000000)
            before = [uc.reg_read(reg) for reg in regs]
            uc.emu_start(entry, entry + 4, count=100000)
            assert uc.reg_read(r.UC_ARM64_REG_PC) == entry + 4, (case, hex(uc.reg_read(r.UC_ARM64_REG_PC)))
            assert uc.reg_read(r.UC_ARM64_REG_SP) == stack - 0x30
            assert uc.reg_read(r.UC_ARM64_REG_FPCR) == 0x400000
            assert uc.reg_read(r.UC_ARM64_REG_NZCV) == 0xb0000000
            assert [uc.reg_read(reg) for reg in regs] == before
            assert not +ledger, (case, ledger)
        run()
        successes = ('potion', 'super_potion', 'percentage', 'revive', 'status_cure', 'condition_cure', 'full_restore', 'last_item', 'delayed_effect')
        if case in successes or case in ('missing_effect', 'scene_after_consume', 'root_after_consume', 'unready_after_consume'):
            assert consumes == 1 and dispatched == 1 and get(mailbox + 0x10, 'I') == 2, (case, calls)
            if case == 'delayed_effect':
                run(); assert get(mailbox + 0x10, 'I') == 2
                hp = pending_hp
            if case == 'scene_after_consume': put(d + 8, 999)
            if case == 'root_after_consume': put(base + 0x610b810, 0)
            if case == 'unready_after_consume': put(base + 0x610d940, 0, 'I')
            if case == 'missing_effect': put(mailbox + 0x54, 121, 'I')
            run()
            assert get(mailbox + 8) == 10
            assert get(mailbox + 0x28, 'I') == (0 if case in successes else 23), (case, get(mailbox + 0x28, 'I'))
            if case in successes:
                expected_hp = 50 if case == 'revive' else 100 if case in ('super_potion', 'percentage', 'status_cure', 'condition_cure') else 83
                assert hp == expected_hp and status == 0 and condition == 0, (case, hp, status, condition)
            run(); assert consumes == dispatched == 1
        else:
            assert consumes == dispatched == 0 and get(mailbox + 0x10, 'I') == 4, (case, calls)
            assert hp == old_hp and get(bag + 0x44 + 17 * 16, 'I') == quantity
        print(f'PASS {base:#x} {case}: native dispatcher, target/ABI, lifetime, single consumption')
