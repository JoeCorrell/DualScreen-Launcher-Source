"""Package the diagnostic DLL for the launcher's private development opt-in.

This never publishes a release, marks a mod ready, or writes to a device.
"""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

RELEASE = "https://github.com/JoeCorrell/DualScreen-Launcher-Payloads/releases/download/launcher-payloads-v1"
VERSION = "0.0.2-probe"

def encode(value):
    return (json.dumps(value, indent=2) + "\n").encode("utf-8")

def package(dll, output):
    import pefile
    binary = dll.read_bytes()
    pe = pefile.PE(data=binary)
    if pe.FILE_HEADER.Machine != 0x14c or pe.OPTIONAL_HEADER.Magic != 0x10b:
        raise ValueError("Probe must be PE32/i386")
    if [symbol.name for symbol in pe.DIRECTORY_ENTRY_EXPORT.symbols] != [b"DirectInput8Create"]:
        raise ValueError("Unexpected proxy exports")
    resources = {"dinput8.dll": binary,
                 "DishonoredSecondScreen.ini": b"[Probe]\r\nEnabled=0\r\n"}
    manifest = {"version": VERSION, "files": [{"source": name,
        "target": "Binaries/Win32/" + name, "sha256": hashlib.sha256(data).hexdigest()}
        for name, data in resources.items()]}
    output.mkdir(parents=True, exist_ok=True)
    name = f"dishonored-{VERSION}.zip"
    archive = output / name
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as target:
        for path, data in [("manifest.json", encode(manifest))] + [("payload/" + name, data) for name, data in resources.items()]:
            entry = zipfile.ZipInfo(path, date_time=(2026, 9, 5, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = 0o100644 << 16
            target.writestr(entry, data)
    descriptor = {"schema": 1, "launcherApi": 1, "gameId": "dishonored",
        "status": "development", "version": VERSION, "name": name,
        "url": f"{RELEASE}/{name}", "bytes": archive.stat().st_size,
        "sha256": hashlib.sha256(archive.read_bytes()).hexdigest()}
    (output / "dishonored-payload.json").write_bytes(encode(descriptor))
    print(f"Development-only package: {archive} ({descriptor['bytes']} bytes)")
    print("Requires the existing app-private development-package opt-in; not release-ready.")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dll", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    package(args.dll, args.output)
