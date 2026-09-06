"""Synthetic tests for the bounded base-version diagnostic; no game assets needed."""
import struct
import unittest

from probe_la_state import BUILD, SIGNATURES, TITLE, snapshot


class Image:
    def __getitem__(self, key):
        return bytes([SIGNATURES.index(key.start) + 1]) * (key.stop - key.start)


class RemoteFixture:
    base = 0x100000000
    inventory = 0x200000000
    flags = 0x200001000

    def __init__(self, *, initialized=True, bad_signature=False, flag_count=256):
        self.reads = []
        self.bad_signature = bad_signature
        manager = bytearray(0x2B0)
        if initialized:
            struct.pack_into('<Q', manager, 0x248, self.inventory)
            struct.pack_into('<Q', manager, 0x268, self.flags)
            struct.pack_into('<i', manager, 0x270, flag_count)
        manager[0x2A8] = 255
        inventory = bytearray(0x140)
        struct.pack_into('<i', inventory, 0x80, 17)
        struct.pack_into('<h', inventory, 0x84, 12)
        inventory[0xA2:0xA4] = bytes([9, 255])
        struct.pack_into('<I', inventory, 0xA8, 1 << 9)
        self.memory = {
            self.base + 0x1CC1120: bytes(manager),
            self.inventory: bytes(inventory),
            self.flags: bytes(256),
        }

    def command(self, command):
        assert command == 'qSupported'
        return 'PacketSize=4000'

    def monitor(self, command):
        if command == 'info':
            return TITLE
        assert command == 'mappings'
        return f'  {self.base:#x} - {self.base + 0x1433fff:#x} r-x Code ----'

    def read(self, address, size):
        self.reads.append((address, size))
        rva = address - self.base
        if rva in SIGNATURES:
            return bytes(size) if self.bad_signature else Image()[rva:rva + size]
        data = self.memory[address]
        assert len(data) == size
        return data


class ProbeTests(unittest.TestCase):
    def test_initialized_native_fields(self):
        remote = RemoteFixture()
        result = snapshot(remote, Image())
        self.assertEqual(result['build'], BUILD)
        self.assertEqual(result['healthQuartersCandidate'], 12)
        self.assertEqual(result['rupeesCandidate'], 17)
        self.assertEqual(result['equipmentSlotBytes'], [9, 255])
        self.assertEqual(result['equipmentOwnershipBits'], '0x200')
        self.assertEqual(result['dungeonIndex'], 255)
        self.assertFalse(result['runtimeValidated'])
        self.assertLess(sum(size for _, size in remote.reads), 16384)

    def test_wrong_executable_never_reads_game_objects(self):
        remote = RemoteFixture(bad_signature=True)
        with self.assertRaisesRegex(ValueError, 'Cannot uniquely match'):
            snapshot(remote, Image())
        self.assertTrue(all(address - remote.base in SIGNATURES
                            for address, _ in remote.reads))

    def test_uninitialized_objects_are_not_dereferenced(self):
        remote = RemoteFixture(initialized=False)
        result = snapshot(remote, Image())
        self.assertNotIn('inventoryBytes', result)
        self.assertNotIn('flagBytes', result)
        self.assertEqual(len(remote.reads), len(SIGNATURES) + 1)

    def test_bad_flag_count_fails_before_flag_read(self):
        for count in (-1, 0, 4097):
            with self.subTest(count=count):
                remote = RemoteFixture(flag_count=count)
                with self.assertRaisesRegex(ValueError, 'Invalid native flag'):
                    snapshot(remote, Image())
                self.assertNotIn(remote.flags, [a for a, _ in remote.reads])

    def test_wrong_title_does_not_read_any_memory(self):
        remote = RemoteFixture()
        remote.monitor = lambda _: 'another game'
        with self.assertRaisesRegex(ValueError, 'not attached'):
            snapshot(remote, Image())
        self.assertEqual(remote.reads, [])


if __name__ == '__main__':
    unittest.main()
