"""Request one bounded, read-only diagnostic range from the running inspector."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import time


def make_request(request_id, address, size):
    if (type(request_id) is not int or type(address) is not int or type(size) is not int or
            not 1 <= request_id <= 128 or not 1 <= size <= 4096 or
            not 0 < address <= 0x100000000 - size):
        raise ValueError("Invalid/bounds-exceeding request")
    return struct.pack("<5I", 0x51523343, 1, request_id, address, size)


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("Duplicate response field: " + key)
        result[key] = value
    return result


def validate_report(data, request_id, address, size):
    report = json.loads(data, object_pairs_hook=unique_object)
    if not isinstance(report, dict):
        raise ValueError("Response must be an object")
    for key, expected in (("id", request_id), ("address", address), ("requestedBytes", size)):
        if type(report.get(key)) is not int or report[key] != expected:
            raise ValueError("Response/request mismatch: " + key)
    if (type(report.get("stable")) is not bool or report.get("atomicSnapshot") is not False or
            type(report.get("readError")) is not int or not 0 <= report["readError"] <= 0xffffffff or
            type(report.get("elapsedMs")) is not int or not 0 <= report["elapsedMs"] <= 930000):
        raise ValueError("Invalid response metadata")
    if report["stable"]:
        if (report["readError"] or not isinstance(report.get("sha256"), str) or
                not re.fullmatch(r"[0-9a-f]{64}", report["sha256"])):
            raise ValueError("Invalid stable response")
    elif report.get("sha256") != "":
        raise ValueError("Unstable response must not claim byte evidence")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", required=True)
    parser.add_argument("--serial", required=True)
    parser.add_argument("--session", required=True)
    parser.add_argument("--id", type=int, required=True)
    parser.add_argument("--address", type=lambda x: int(x, 0), required=True)
    parser.add_argument("--bytes", type=int, required=True)
    parser.add_argument("--evidence", required=True, help="Why this address/range is in scope")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if (not re.fullmatch(r"C3R[0-9]{6}", args.session) or args.output.exists() or not args.evidence.strip()):
        parser.error("Invalid session, empty evidence reason, or existing local output")
    try:
        request = make_request(args.id, args.address, args.bytes)
    except ValueError as error:
        parser.error(str(error))
    args.output.mkdir(parents=True)
    (args.output / "request.bin").write_bytes(request)
    (args.output / "reason.txt").write_text(args.evidence, encoding="utf-8")
    base = "/sdcard/" + args.session + "/capture/"
    stem = "read-" + str(args.id)

    def adb(*command, check=True):
        return subprocess.run([args.adb, "-s", args.serial, *command], check=check,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=20)

    # ADB push may create the remote capture directory. Do not race the helper's
    # exclusive output-directory check by writing a request before it is ready.
    for _ in range(30):
        ready = adb("shell", "cat " + base + "inspector.json", check=False)
        if ready.returncode == 0:
            metadata = json.loads(ready.stdout)
            if (metadata.get("readOnly") is not True or metadata.get("maximumReadBytes") != 4096 or
                    metadata.get("maximumRequests") != 128):
                raise ValueError("Inspector readiness metadata rejected")
            break
        finished = adb("shell", "cat /sdcard/" + args.session + "/capture-exit.txt", check=False)
        if finished.returncode == 0:
            raise RuntimeError("Inspector exited before readiness: " + finished.stdout.decode().strip())
        time.sleep(0.5)
    else:
        raise TimeoutError("Inspector not ready; no remote request directory created")
    adb("push", str(args.output / "request.bin"), base + "request-next.bin")
    adb("shell", "mv " + base + "request-next.bin " + base + "request.bin")
    for _ in range(20):
        result = adb("shell", "cat " + base + stem + ".json", check=False)
        if result.returncode == 0:
            report = validate_report(result.stdout, args.id, args.address, args.bytes)
            (args.output / (stem + ".json")).write_bytes(result.stdout)
            if report.get("stable") is True:
                target = args.output / (stem + ".bin")
                adb("pull", base + stem + ".bin", str(target))
                data = target.read_bytes()
                if len(data) != args.bytes or hashlib.sha256(data).hexdigest() != report.get("sha256"):
                    raise ValueError("Returned evidence size/hash mismatch")
            print(json.dumps(report, indent=2))
            return
        time.sleep(0.5)
    raise TimeoutError("No complete response within bounded wait; do not reuse this request ID")


if __name__ == "__main__":
    main()
