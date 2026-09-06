"""Send a development command through the running Games container's WinHandler.

Requires an already running container, established through the launcher's UI.
Uses its existing 64-byte EXEC protocol. No private app access or APK changes.
Commands are staged separately from game files. Development use only.
"""
import argparse
import pathlib
import struct
import subprocess
import tempfile
import uuid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adb', required=True)
    parser.add_argument('--batch', type=pathlib.Path, required=True)
    args = parser.parse_args()
    # Unique names prevent one concurrent request from replacing another's command.
    name = 'ac2-' + uuid.uuid4().hex[:8] + '.bat'
    filename = b'cmd.exe'
    parameters = ('/c F:\\Download\\' + name).encode('ascii')
    packet = struct.pack('<BIII', 2, len(filename) + len(parameters) + 8, len(filename), len(parameters)) + filename + parameters
    if len(packet) > 64:
        raise ValueError('WinHandler packet exceeds 64 bytes')
    def adb(*command):
        subprocess.run([args.adb, *command], check=True)
    with tempfile.TemporaryDirectory(prefix='ac2-exec-') as temp:
        wire = pathlib.Path(temp, 'packet.bin')
        wire.write_bytes(packet.ljust(64, b'\0'))
        adb('push', str(args.batch), '/sdcard/Download/' + name)
        remote = '/data/local/tmp/' + name + '.bin'
        adb('push', str(wire), remote)
        try:
            adb('shell', 'toybox nc -u -w 2 -q 1 127.0.0.1 7946 < ' + remote)
        finally:
            adb('shell', 'rm ' + remote)
    print('Submitted F:\\Download\\' + name + '; submission is not proof of process completion.')


if __name__ == '__main__':
    main()
