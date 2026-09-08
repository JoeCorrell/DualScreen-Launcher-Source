"""Stage and explicitly invoke the read-only helper through existing WinHandler.

Wire contract: WinHandler.java exec(), RequestCodes.EXEC=2, 64-byte LE packet,
destination UDP 127.0.0.1:7946. Does not claim a port, replace the bridge, send
game inputs, restart Wine, or alter game files. The caller must verify that F:
maps to /sdcard in the live prefix before use. This is not a native game hook.
"""
import argparse
from pathlib import Path
import re
import struct
import subprocess


def exec_packet(filename, parameters):
    name, args = filename.encode("ascii"), parameters.encode("ascii")
    if not name or 13 + len(name) + len(args) > 64 or b"\0" in name + args:
        raise ValueError("Command does not fit the verified 64-byte wire contract")
    return (struct.pack("<BIII", 2, len(name) + len(args) + 8, len(name), len(args))
            + name + args).ljust(64, b"\0")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", required=True)
    parser.add_argument("--serial", required=True)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--session", required=True)
    parser.add_argument("--output", type=Path, required=True)
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--probe-root", action="store_true")
    modes.add_argument("--inspect", action="store_true")
    modes.add_argument("--performance", action="store_true")
    args = parser.parse_args()
    if not re.fullmatch(r"C3R[0-9]{6}", args.session):
        parser.error("Session must be C3R followed by six digits")
    if args.output.exists() or not args.exe.is_file():
        parser.error("Use a new local output directory and an existing capture executable")
    packet = exec_packet("wscript.exe", "//B //NoLogo F:\\" + args.session + "\\capture.vbs" +
                         (" p" if args.probe_root else " i" if args.inspect else " e" if args.performance else ""))
    args.output.mkdir(parents=True)
    packet_file = args.output / "launch-packet.bin"
    packet_file.write_bytes(packet)
    remote = "/sdcard/" + args.session

    def adb(*command):
        subprocess.run([args.adb, "-s", args.serial, *command], check=True, timeout=45)

    # mkdir without -p: refuse to overwrite evidence from an earlier attempt.
    adb("shell", "mkdir " + remote)
    adb("push", str(args.exe), remote + "/Crysis3RuntimeCapture.exe")
    adb("push", str(Path(__file__).with_name("CaptureRuntime.vbs")), remote + "/capture.vbs")
    adb("push", str(packet_file), remote + "/launch-packet.bin")
    # The device's nc can remain alive after its own UDP timeout when the
    # bridge has closed. Bound the remote process too, not just the adb client.
    adb("shell", "toybox timeout -k 1 5 toybox nc -u -w 2 -q 1 127.0.0.1 7946 < " + remote + "/launch-packet.bin")
    report = "inspector.json" if args.inspect else "root-probe.json" if args.probe_root else "performance.txt" if args.performance else "capture.json"
    print("Invoked once; inspect " + remote + "/capture-exit.txt and capture/" + report)


if __name__ == "__main__":
    main()
