#!/usr/bin/env python3
"""Check that a reader CSV has a contiguous seq column."""

import csv
import sys


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check_seq.py samples.csv", file=sys.stderr)
        return 2
    gaps = 0
    count = 0
    previous = None
    with open(sys.argv[1], newline="") as handle:
        for row in csv.DictReader(handle):
            seq = int(row["seq"])
            if previous is not None and seq != previous + 1:
                gaps += 1
            previous = seq
            count += 1
    print(f"samples={count} gaps={gaps}")
    if count == 0 or gaps != 0:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
