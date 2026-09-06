"""Synthetic guest memory fixtures; no game files or debugger needed."""
import struct
import unittest

from probe_smo_state import APPLICATION_INSTANCE, SIGNATURES, VTABLE_ADDRESS_POINTS, snapshot


class FakeRemote:
    base = 0x100000000

    def __init__(self):
        self.memory = {}
        self.reads = 0
        self.bytes = 0
        self.image = bytearray(max(SIGNATURES) + 32)
        for i, rva in enumerate(SIGNATURES):
            signature = bytes([i + 1]) * 32
            self.image[rva:rva + 32] = signature
            self.put(self.base + rva, signature)
        # Application -> framework -> task manager -> root -> game -> sequence -> scene.
        for address, value in ((self.base + APPLICATION_INSTANCE, 0x200000000),
                               (0x200000028, 0x200001000), (0x200001038, 0x200002000),
                               (0x200002128, 0x200003000), (0x200003200, 0x200004000),
                               (0x200004010, 0x200005000), (0x2000050B0, 0x200006000),
                               (0x2000062D0, 0x200007000), (0x2000060A0, 0x200008000),
                               (0x200008008, 0x200009000), (0x200009090, 0x200007000),
                               (0x200007020, 0x20000A000), (0x200006090, 0x20000B000),
                               (0x20000B098, 0x20000C000), (0x20000D000, 0x20000E000),
                               (0x20000E050, 0x20000F000)):
            self.pack(address, '<Q', value)
        for kind, address in (('gameSystem', 0x200004000), ('sequence', 0x200005000),
                              ('scene', 0x200006000), ('holder', 0x200007000),
                              ('player', 0x20000E000)):
            self.pack(address, '<Q', self.base + VTABLE_ADDRESS_POINTS[kind])
        self.pack(0x20000C000, '<Qii', 0x20000D000, 2, 1)  # capacity, active count
        self.pack(0x20000F008, '<fff', 1.0, 2.0, 3.0)
        save = 0x20000A000
        self.put(save + 0x5D8, bytes(0x460))
        self.pack(save + 0x5D8, '<i', 42)
        self.pack(save + 0x9F0, '<i', 0)
        self.pack(save + 0xA24, '<i', 1)
        for offset, address in ((0x860, 0x200010000), (0x718, 0x200011000),
                                (0x770, 0x200012000), (0xA00, 0x200013000),
                                (0x5E8, 0x200014000)):
            self.pack(save + offset, '<Q', address)
        self.pack(0x20001000C, '<i', 3)
        self.put(0x200011000, b'Mario'.ljust(96, b'\0'))
        self.put(0x200012000, b'MarioCap'.ljust(96, b'\0'))
        self.pack(0x200013000, '<i', 10)
        self.pack(0x200014000, '<i', 4)

    def put(self, address, data):
        self.memory.update({address + i: byte for i, byte in enumerate(data)})

    def pack(self, address, fmt, *values):
        self.put(address, struct.pack(fmt, *values))

    def command(self, command):
        assert command == 'qSupported'

    def monitor(self, name):
        assert name == 'info'
        return 'Program Id: 0x0100000000010000\n0x100000000 - 0x103ffffff RedStar.nss'

    def read(self, address, size):
        self.reads += 1
        self.bytes += size
        return bytes(self.memory[address + i] for i in range(size))


class SmoProbeTests(unittest.TestCase):
    def test_native_strings_need_only_byte_alignment(self):
        remote = FakeRemote()
        remote.pack(0x20000A718, '<Q', 0x200011003)
        remote.put(0x200011003, b'Mario'.ljust(96, b'\0'))
        self.assertEqual(snapshot(remote, remote.image)['outfit'], 'Mario')

    def test_player_virtual_base_address_point(self):
        self.assertEqual(VTABLE_ADDRESS_POINTS['player'], 0x1D779E8)

    def test_menu_independent_chain_and_capacity_order(self):
        remote = FakeRemote()
        data = snapshot(remote, remote.image)
        self.assertEqual(data['coins'], 42)
        self.assertEqual(data['regionalCoins'], 6)
        self.assertEqual(data['health'], 3)
        self.assertEqual(data['outfit'], 'Mario')
        self.assertEqual(data['hat'], 'MarioCap')
        self.assertEqual(data['playerPosition'], (1.0, 2.0, 3.0))
        self.assertLess(remote.bytes, 4096)
        self.assertLess(remote.reads, 50)

    def test_wrong_build_rejected_before_object_reads(self):
        remote = FakeRemote()
        remote.put(remote.base + SIGNATURES[0], bytes(32))
        with self.assertRaisesRegex(ValueError, 'supported base'):
            snapshot(remote, remote.image)
        self.assertEqual(remote.reads, 1)

    def test_title_scene_not_treated_as_gameplay(self):
        remote = FakeRemote()
        remote.pack(0x200006000, '<Q', remote.base + 0x1230)
        with self.assertRaisesRegex(ValueError, 'expected scene'):
            snapshot(remote, remote.image)

    def test_empty_player_list(self):
        remote = FakeRemote()
        remote.pack(0x20000C00C, '<i', 0)
        with self.assertRaisesRegex(ValueError, 'PlayerHolder'):
            snapshot(remote, remote.image)

    def test_inconsistent_scene_holder(self):
        remote = FakeRemote()
        remote.pack(0x200009090, '<Q', 0x200019000)
        with self.assertRaisesRegex(ValueError, 'disagree'):
            snapshot(remote, remote.image)

    def test_bad_world_rejected_before_indexing(self):
        remote = FakeRemote()
        remote.pack(0x20000A9F0, '<i', 17)
        with self.assertRaisesRegex(ValueError, 'world'):
            snapshot(remote, remote.image)


if __name__ == '__main__':
    unittest.main()
