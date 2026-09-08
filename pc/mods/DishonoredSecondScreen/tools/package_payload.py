"""Prepare the independent full-mod package locally; never upload or install it.

This recipe has not been executed during the user's build hold. Output remains
development-only until native/device acceptance is complete. The diagnostic
probe has a separate recipe and cannot be substituted for this payload.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import zipfile

from prepare_assets import IMAGES, FONTS, MAPS

RELEASE = "https://github.com/JoeCorrell/DualScreen-Launcher-Payloads/releases/download/launcher-payloads-v1"
MAX_BYTES = 256 * 1024 * 1024


def encoded(value):
    return (json.dumps(value, indent=2) + "\n").encode("utf-8")


def digest(data):
    return hashlib.sha256(data).hexdigest()


def catalog(path, macro):
    names = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.startswith("//"):
            continue
        match = re.fullmatch(macro + r"\(([A-Za-z0-9_]+)\)", line)
        if not match or match[1] in names:
            raise ValueError("Invalid original-resource catalog")
        names.append(match[1])
    return names


def original_assets(directory):
    manifest_file = directory / "assets.json"
    if manifest_file.stat().st_size > 512 * 1024:
        raise ValueError("Resource manifest is too large")
    manifest_bytes = manifest_file.read_bytes()
    manifest = json.loads(manifest_bytes)
    expected = {name + ".png": export + ".tga" for name, export in IMAGES.items()}
    expected.update(FONTS)
    expected.update({name + ".png": export + ".tga" for name, (_, _, export) in MAPS.items()})
    source = Path(__file__).resolve().parents[1] / "src"
    for name in catalog(source / "ItemAssets.inc", "DIS_ITEM_ASSET"):
        expected["items/" + name + "_Large.png"] = name + "_Large.tga"
    for name in catalog(source / "PortraitAssets.inc", "DIS_PORTRAIT_ASSET"):
        expected["portraits/" + name + ".png"] = name + ".tga"
    entries = manifest["files"]
    if len(entries) != len(expected) or len({row["file"] for row in entries}) != len(entries):
        raise ValueError("Resource manifest is incomplete or contains duplicates")
    root = directory.resolve()
    resources = {"assets.json": manifest_bytes}
    for row in entries:
        name = row["file"]
        if name not in expected or row["export"] != expected[name]:
            raise ValueError("Stale or unexpected original asset: " + name)
        if not re.fullmatch(r"[0-9a-f]{64}", row["sourceSha256"]):
            raise ValueError("Missing original resource provenance")
        if PurePosixPath(name).is_absolute() or "\\" in name or ".." in PurePosixPath(name).parts:
            raise ValueError("Invalid resource path")
        path = (directory / name).resolve()
        if not path.is_relative_to(root) or not path.is_file() or not 0 < path.stat().st_size <= MAX_BYTES:
            raise ValueError("Resource is outside the original staging directory")
        data = path.read_bytes()
        if digest(data) != row["sha256"]:
            raise ValueError("Resource hash mismatch: " + name)
        resources[name] = data
    return resources


def package(dll, assets, output, version, wemu_transport=False):
    import pefile
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,95}", version):
        raise ValueError("Invalid payload version")
    if not 0 < dll.stat().st_size <= 32 * 1024 * 1024:
        raise ValueError("Unexpected payload binary size")
    binary = dll.read_bytes()
    pe = pefile.PE(data=binary)
    if pe.FILE_HEADER.Machine != 0x14c or pe.OPTIONAL_HEADER.Magic != 0x10b:
        raise ValueError("The full payload must be PE32/i386")
    if [entry.name for entry in pe.DIRECTORY_ENTRY_EXPORT.symbols] != [b"DirectInput8Create"]:
        raise ValueError("Unexpected proxy exports")
    if b"DishonoredSecondScreen.FullPayload.i386.v1\0" not in binary:
        raise ValueError("The diagnostic probe cannot be packaged as the full mod")
    for entry in pe.DIRECTORY_ENTRY_IMPORT:
        if entry.dll.lower().startswith((b"libgcc", b"libstdc", b"libwinpthread")):
            raise ValueError("Unexpected native runtime dependency")
    configuration = b"[Mod]\r\nEnabled=1\r\n[Panel]\r\nTallDesktopFallback=1\r\n"
    if wemu_transport:
        configuration += b"WemuSkyrimTransport=1\r\n"
    files = {"dinput8.dll": binary, "DishonoredSecondScreen.ini": configuration}
    files.update({"DishonoredSecondScreen/assets/" + name: data for name, data in original_assets(assets).items()})
    manifest = {"version": version, "files": [{"source": name, "target": "Binaries/Win32/" + name,
        "sha256": digest(data)} for name, data in sorted(files.items())]}
    contents = {"manifest.json": encoded(manifest)}
    contents.update({"payload/" + name: data for name, data in files.items()})
    if sum(len(data) for data in contents.values()) > MAX_BYTES or len(contents) > 4097:
        raise ValueError("Package exceeds the launcher's extraction limits")
    output.mkdir(parents=True, exist_ok=True)
    name = "dishonored-" + version + ".zip"
    archive, temporary = output / name, output / (name + ".pending")
    with zipfile.ZipFile(temporary, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as target:
        for path, data in sorted(contents.items()):
            entry = zipfile.ZipInfo(path, date_time=(2026, 9, 6, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = 0o100644 << 16
            target.writestr(entry, data)
    with zipfile.ZipFile(temporary) as check:
        if check.testzip() is not None or set(check.namelist()) != set(contents):
            raise ValueError("Package integrity check failed")
        for path, data in contents.items():
            if digest(check.read(path)) != digest(data):
                raise ValueError("Package content verification failed")
    if not 0 < temporary.stat().st_size <= MAX_BYTES:
        raise ValueError("Package exceeds the launcher's download limit")
    temporary.replace(archive)
    descriptor = {"schema": 1, "launcherApi": 1, "gameId": "dishonored", "status": "development",
        "version": version, "name": name, "url": RELEASE + "/" + name,
        "bytes": archive.stat().st_size, "sha256": digest(archive.read_bytes())}
    descriptor_file = output / "dishonored-payload.json.pending"
    descriptor_file.write_bytes(encoded(descriptor))
    descriptor_file.replace(output / "dishonored-payload.json")
    print("Verified local development package: " + str(archive))
    print("No publishing, installation or APK changes were performed.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dll", type=Path)
    parser.add_argument("assets", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--version", required=True)
    parser.add_argument("--wemu-transport", action="store_true",
                        help="Use stock WEMU's existing Skyrim private-panel transport identity")
    args = parser.parse_args()
    package(args.dll, args.assets, args.output, args.version, args.wemu_transport)
