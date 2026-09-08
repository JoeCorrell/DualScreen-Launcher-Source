"""Synthetic evidence exercises validation only; no fixture is a game dump."""
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location(
    "audit_capture", Path(__file__).resolve().parents[1] / "tools/audit_capture.py")
audit = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(audit)


class CaptureAuditTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.capture = self.root / "capture"
        self.capture.mkdir()
        self.source = self.root / "synthetic.exe"
        headers = bytearray(512)
        headers[:2] = b"MZ"
        struct.pack_into("<I", headers, 60, 128)
        headers[128:132] = b"PE\0\0"
        struct.pack_into("<HH", headers, 132, 0x14C, 3)
        struct.pack_into("<H", headers, 148, 224)
        struct.pack_into("<H", headers, 152, 0x10B)
        self.image_size = (audit.FAULT_RVA & ~4095) + 4096
        struct.pack_into("<II", headers, 208, self.image_size, 512)
        for index, (name, size, rva, raw_size, raw, flags) in enumerate((
                (b".text", 64, 4096, 64, 512, 0x60000020),
                (b".rdata", 32, 8192, 32, 576, 0x40000040),
                (b".data", 4096, audit.FAULT_RVA & ~4095, 0, 0, 0xC0000040))):
            offset = 376 + index * 40
            headers[offset:offset + len(name)] = name
            struct.pack_into("<IIII", headers, offset + 8, size, rva, raw_size, raw)
            struct.pack_into("<I", headers, offset + 36, flags)
        self.source.write_bytes(headers + bytes(range(64)) + bytes(32))
        self.source_hash = audit.digest(self.source.read_bytes())
        self.manifest = dict(schema=2, sourceSha256=self.source_hash, pid=42, imageBase=0x37000000,
                             durationMs=100, complete=True, memoryRegionsComplete=True,
                             atomicSnapshot=False, decryptionVerified=False, nativeHooksVerified=False,
                             sectionRva=4096, sectionVirtualBytes=64)

        def blob(name, data):
            (self.capture / name).write_bytes(data)
            return dict(complete=True, capturedBytes=len(data), sha256=audit.digest(data))

        self.manifest["headers"] = blob("loaded-headers.bin", headers)
        loaded_text = blob("loaded-text.bin", bytes([0x90]) * 64)
        self.manifest.update(capturedBytes=64, captureSha256=loaded_text["sha256"])
        self.manifest["rdata"] = dict(blob("loaded-rdata.bin", bytes(32)), rva=8192, virtualBytes=32)
        self.manifest["faultPage"] = dict(blob("loaded-fault-page.bin", bytes(4096)),
                                          rva=audit.FAULT_RVA & ~4095)
        self.regions = [dict(base=0x37000000, bytes=self.image_size, allocationBase=0x37000000,
                             state=0x1000, protect=0x40, allocationProtect=0x40, type=0x1000000)]
        self.save_regions()
        self.save_manifest()

    def save_manifest(self):
        (self.capture / "capture.json").write_text(json.dumps(self.manifest), encoding="utf-8")

    def save_regions(self):
        data = "\n".join(json.dumps(region) for region in self.regions).encode("utf-8")
        (self.capture / "memory-regions.jsonl").write_bytes(data)
        self.manifest["memoryRegionsSha256"] = audit.digest(data)

    def run_fixture(self):
        # Test-scoped fingerprint substitution; the production CLI has no bypass.
        with patch.object(audit, "SUPPORTED", frozenset({self.source_hash})):
            return audit.audit(self.capture, self.source)

    def test_intact_evidence_does_not_authorize_hooks(self):
        result = self.run_fixture()
        self.assertEqual(result["differingTextBytes"], 64)
        self.assertTrue(result["faultRegionExecutableAtInventory"])
        for key in ("atomicSnapshot", "decryptionVerified", "nativeHooksVerified"):
            self.assertIs(result[key], False)

    def test_unknown_source_rejected_without_test_fingerprint(self):
        with self.assertRaisesRegex(ValueError, "Unrecognized executable"):
            audit.audit(self.capture, self.source)

    def test_truncated_or_modified_blob_rejected(self):
        target = self.capture / "loaded-text.bin"
        for data in (bytes(63), bytes(64), bytes(65)):
            with self.subTest(size=len(data)):
                target.write_bytes(data)
                with self.assertRaises(ValueError):
                    self.run_fixture()

    def test_missing_region_hash_rejected(self):
        del self.manifest["memoryRegionsSha256"]
        self.save_manifest()
        with self.assertRaisesRegex(ValueError, "region hash"):
            self.run_fixture()

    def test_changed_region_data_rejected(self):
        with (self.capture / "memory-regions.jsonl").open("ab") as stream:
            stream.write(b" ")
        with self.assertRaisesRegex(ValueError, "region hash"):
            self.run_fixture()

    def test_gapped_overlapping_truncated_and_extra_regions_rejected(self):
        first = self.regions[0].copy()
        malformed = [
            [dict(first, base=first["base"] + 4096)],
            [dict(first, bytes=4096)],
            [dict(first, bytes=4096), first],
            [first, dict(first, base=first["base"] + self.image_size)],
            [dict(first, bytes=0x100000000)],
        ]
        for regions in malformed:
            with self.subTest(regions=regions):
                self.regions = regions
                self.save_regions()
                self.save_manifest()
                with self.assertRaises(ValueError):
                    self.run_fixture()

    def test_guard_uncommitted_and_nonexecute_are_not_executable(self):
        for state, protection in ((0x1000, 4), (0x2000, 0x40), (0x1000, 0x140)):
            with self.subTest(state=state, protection=protection):
                self.regions[0].update(state=state, protect=protection)
                self.save_regions()
                self.save_manifest()
                self.assertFalse(self.run_fixture()["faultRegionExecutableAtInventory"])

    def test_stale_claims_wrong_build_boolean_pid_and_location_rejected(self):
        baseline = self.manifest.copy()
        for key, value in (("complete", False), ("complete", 1), ("nativeHooksVerified", True),
                           ("decryptionVerified", True), ("atomicSnapshot", True), ("pid", True),
                           ("sourceSha256", "0" * 64), ("sectionRva", 123), ("imageBase", 0xFFFFFFFF)):
            with self.subTest(key=key, value=value):
                self.manifest = dict(baseline, **{key: value})
                self.save_manifest()
                with self.assertRaises(ValueError):
                    self.run_fixture()

    def test_self_consistent_wrong_loaded_layout_rejected(self):
        path = self.capture / "loaded-headers.bin"
        headers = bytearray(path.read_bytes())
        struct.pack_into("<I", headers, 376 + 12, 12288)
        path.write_bytes(headers)
        self.manifest["headers"]["sha256"] = audit.digest(headers)
        self.save_manifest()
        with self.assertRaisesRegex(ValueError, "layout mismatch"):
            self.run_fixture()

    def test_duplicate_json_keys_rejected(self):
        path = self.capture / "capture.json"
        path.write_text('{"schema":2,' + json.dumps(self.manifest)[1:], encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "Duplicate JSON"):
            self.run_fixture()

    def test_alternate_profile_cannot_be_self_declared(self):
        for profile in ("observed-20260907-5section", "unknown", "", True):
            with self.subTest(profile=profile):
                self.manifest["layoutProfile"] = profile
                self.manifest["moduleBytes"] = self.image_size
                self.save_manifest()
                with self.assertRaisesRegex(ValueError, "Unrecognized live layout"):
                    self.run_fixture()


if __name__ == "__main__":
    unittest.main()
