#!/usr/bin/env python3
"""Print label/text pairs from a little- or big-endian Nintendo MSBT file."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from generate_botw_item_text import decode_msbt_text


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("msbt", type=Path)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    messages = decode_msbt_text(args.msbt.read_bytes())
    if args.json:
        print(json.dumps(messages, ensure_ascii=False, indent=2))
    else:
        for label, value in messages.items():
            escaped_value = value.replace("\n", "\\n")
            print(f"{label}\t{escaped_value}")


if __name__ == "__main__":
    main()
