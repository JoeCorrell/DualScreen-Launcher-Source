#!/usr/bin/env python3
"""Inspect Skyward Sword's shipped navi_table.dat progression tables."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("table", type=Path)
    args = parser.parse_args()
    data = args.table.read_bytes()
    if len(data) < 0x1C or data[:4] != b"V001":
        raise ValueError("expected a V001 navi table")
    summary_count, hint_count, objective_count, analysis_count = struct.unpack_from(">4H", data, 4)
    summary_offset, hint_offset, objective_offset, analysis_offset = struct.unpack_from(">4I", data, 12)

    print(
        f"summary={summary_count}@0x{summary_offset:x} hint={hint_count}@0x{hint_offset:x} "
        f"objective={objective_count}@0x{objective_offset:x} "
        f"analysis={analysis_count}@0x{analysis_offset:x}"
    )
    print("[summary]")
    for index in range(summary_count):
        storyflag, message = struct.unpack_from(">2H", data, summary_offset + index * 4)
        print(f"{index}\tstoryflag={storyflag}\tKEN0_{message:02d}")
    print("[objective]")
    for index in range(objective_count):
        storyflag, message = struct.unpack_from(">2H", data, objective_offset + index * 4)
        print(f"{index}\tstoryflag={storyflag}\tKEN2_{message:02d}")


if __name__ == "__main__":
    main()
