#!/usr/bin/env python3

from __future__ import annotations

import argparse
import re
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=Path)
    parser.add_argument("--match", required=True)
    parser.add_argument("--minimum", type=int, default=4)
    args = parser.parse_args()

    pattern = re.compile(args.match, re.IGNORECASE)
    data = args.image.read_bytes()
    expression = rb"[\x20-\x7e]{" + str(max(1, args.minimum)).encode("ascii") + rb",}"
    for value in re.finditer(expression, data):
        text = value.group().decode("ascii")
        if pattern.search(text):
            print(f"0x{value.start():08X} {text}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
