#!/usr/bin/env python3
"""Diff two action_counts.csv snapshots and print the top actions per class.

Each snapshot is a set of rows (utc_time,class,action,ok_count,fail_count)
with cumulative counters since server start. Diffing an early snapshot
against a later one gives the per-action activity in that window.

Usage:
    python3 tools/action_counts_report.py early.csv late.csv [--top 10]

Prints, per class id, the top actions by total executions in the window,
with the ok share so dead/failing actions stand out.
"""

import argparse
import csv
import sys
from collections import defaultdict

CLASS_NAMES = {
    1: "warrior",
    2: "paladin",
    3: "hunter",
    4: "rogue",
    5: "priest",
    7: "shaman",
    8: "mage",
    9: "warlock",
    11: "druid",
}


def load(path):
    counts = {}
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            try:
                key = (int(row["class"]), row["action"])
                counts[key] = (int(row["ok_count"]), int(row["fail_count"]))
            except (KeyError, ValueError):
                continue
    return counts


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("early", help="earlier action_counts.csv snapshot")
    parser.add_argument("late", help="later action_counts.csv snapshot")
    parser.add_argument("--top", type=int, default=10,
                        help="actions shown per class (default 10)")
    args = parser.parse_args()

    early = load(args.early)
    late = load(args.late)

    if not early and not late:
        print("no rows in either snapshot", file=sys.stderr)
        return 1

    delta = defaultdict(dict)
    for key, (late_ok, late_fail) in late.items():
        early_ok, early_fail = early.get(key, (0, 0))
        ok, fail = late_ok - early_ok, late_fail - early_fail
        if ok < 0 or fail < 0:
            print(f"warning: {key} went backwards (server restarted?); "
                  f"clamping to late values", file=sys.stderr)
            ok, fail = max(ok, 0), max(fail, 0)
        if ok or fail:
            delta[key[0]][key[1]] = (ok, fail)

    if not delta:
        print("no activity between snapshots")
        return 0

    for cls in sorted(delta):
        name = CLASS_NAMES.get(cls, f"class{cls}")
        print(f"== {name} ({cls}) ==")
        rows = sorted(delta[cls].items(), key=lambda kv: -(kv[1][0] + kv[1][1]))
        for action, (ok, fail) in rows[:args.top]:
            total = ok + fail
            share = f"{100.0 * ok / total:.0f}% ok" if total else "n/a"
            print(f"  {total:>8}  {action} (ok={ok} fail={fail}, {share})")
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
