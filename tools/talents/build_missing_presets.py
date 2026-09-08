#!/usr/bin/env python3
"""Author the five missing premade-spec presets and emit config lines.

Each spec: ordered acquisition list of (page, index, rank) cumulative steps.
Checkpoints at levels 10..60 step 5 (budgets 1,6,11,...,51 at rate 1).
Link strings built in sorted talent order per tree (matching TalentSpec sort),
dashes separating the class's three trees, trailing empty trees truncated
(exactly like existing config rows).

Usage: python3 tools/talents/build_missing_presets.py --dbc <dbc-dir>
Prints AiPlayerbot.PremadeSpecName/Prob/Link lines to stdout.
"""
import argparse
import struct
import sys
from pathlib import Path


def load_dbc(path):
    data = Path(path).read_bytes()
    magic, records, fields, recsize, strsize = struct.unpack('<4sIIII', data[:20])
    return [struct.unpack('<' + 'i' * fields, data[20 + i * recsize:20 + (i + 1) * recsize])
            for i in range(records)]


def class_trees(talents, tab_page, tab_class, cls):
    mask = 1 << (cls - 1)
    ct = [t for t in talents if tab_class.get(t[1], 0) & mask]
    ct.sort(key=lambda t: (tab_page.get(t[1], 99), t[2], t[3]))
    pages = sorted(set(tab_page.get(t[1], 99) for t in ct))
    per = {p: [t for t in ct if tab_page.get(t[1], 99) == p] for p in pages}
    return pages, per


def build_link(pages, per, alloc):
    """alloc: {(page, idx): rank}. Existing config style: all-zero parts are
    empty, trailing empties cut, trailing zeros in a part cut. Semantics are
    unchanged (TalentSpec::ReadTalents treats all three as rank 0)."""
    parts = []
    for p in pages:
        tl = per[p]
        parts.append(''.join(str(alloc.get((p, i), 0)) for i in range(len(tl))))
    parts = [s.rstrip('0') for s in parts]
    while parts and parts[-1] == '':
        parts.pop()
    return '-'.join(parts)


def emit(cls, specno, name, pages, per, steps):
    """steps: list of (page, idx, rank) cumulative additions in order.
    Yields (level, link) at budgets 1,6,...,51."""
    print(f'AiPlayerbot.PremadeSpecName.{cls}.{specno} = {name}')
    print(f'AiPlayerbot.PremadeSpecProb.{cls}.{specno} = 100')
    alloc = {}
    si = 0
    for level in range(10, 61, 5):
        budget = level - 9
        while sum(alloc.values()) < budget and si < len(steps):
            p, i, r = steps[si]
            alloc[(p, i)] = alloc.get((p, i), 0) + r
            si += 1
        if sum(alloc.values()) != budget:
            print(f'# WARNING {cls}.{specno}.{level}: spent {sum(alloc.values())} != budget {budget}',
                  file=sys.stderr)
        print(f'AiPlayerbot.PremadeSpecLink.{cls}.{specno}.{level} = {build_link(pages, per, alloc)}')
    if si < len(steps):
        print(f'# WARNING {cls}.{specno}: {len(steps) - si} steps unused', file=sys.stderr)


def expand(order):
    """order: list of (page, idx, total_rank) in acquisition sequence.
    Expands to unit steps."""
    steps = []
    for p, i, r in order:
        steps.extend([(p, i, 1)] * r)
    return steps


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dbc', required=True)
    args = ap.parse_args()
    dbc = Path(args.dbc)
    talents = load_dbc(dbc / 'Talent.dbc')
    tabs = load_dbc(dbc / 'TalentTab.dbc')
    tab_page = {t[0]: (1 if t[0] == 41 else t[13]) for t in tabs}
    tab_class = {t[0]: t[12] & 0xFFFFFFFF for t in tabs}

    # (cls, specno, name, [(page, idx, rank)...]) — idx = dump order within tree.
    specs = {
        # Warrior Arms (p0) + Fury dip (p1). 40 + 11.
        (1, 2, 'arms'): [
            (0, 1, 5), (0, 4, 5), (0, 2, 2), (0, 3, 2), (0, 6, 1),
            (0, 5, 3), (0, 7, 2), (0, 8, 3), (0, 9, 3), (0, 10, 2),
            (0, 11, 5), (0, 12, 1), (0, 17, 1), (0, 13, 3), (0, 15, 2),
            (1, 1, 5), (1, 3, 5), (1, 5, 1),
        ],
        # Hunter Survival (p2) + BM dip (p0). 43 + 8.
        (3, 2, 'survival'): [
            (2, 0, 3), (2, 1, 5), (2, 3, 3), (2, 4, 2), (2, 5, 3),
            (2, 7, 2), (2, 8, 5), (2, 9, 1), (2, 10, 1), (2, 11, 2),
            (2, 12, 3), (2, 14, 3), (2, 15, 3), (2, 16, 1), (2, 18, 5),
            (2, 19, 1), (0, 1, 5), (0, 4, 3),
        ],
        # Rogue Subtlety (p2) + Assassination dip (p0). 41 + 10.
        (4, 2, 'subtlety'): [
            (2, 0, 5), (2, 2, 3), (2, 3, 3), (2, 5, 3), (2, 6, 3),
            (2, 7, 3), (2, 9, 1), (2, 8, 1), (2, 10, 2), (2, 11, 2),
            (2, 13, 2), (2, 14, 1), (2, 15, 1), (2, 16, 2), (2, 17, 2),
            (2, 19, 1), (2, 1, 2), (2, 4, 2), (2, 18, 2),
            (0, 2, 5), (0, 3, 3), (0, 4, 2),
        ],
        # Priest Discipline (p0) + Holy dip (p1). 40 + 11.
        (5, 2, 'discipline'): [
            (0, 0, 2), (0, 1, 3), (0, 3, 5), (0, 4, 5), (0, 9, 3),
            (0, 8, 1), (0, 10, 3), (0, 6, 2), (0, 7, 2), (0, 11, 3),
            (0, 13, 3), (0, 14, 1), (0, 15, 1), (0, 16, 5), (0, 17, 1),
            (1, 0, 3), (1, 1, 2), (1, 2, 5), (1, 3, 1),
        ],
        # Shaman Elemental (p0) + Restoration dip (p2). 42 + 9.
        (7, 2, 'elemental'): [
            (0, 0, 5), (0, 1, 5), (0, 3, 3), (0, 4, 3), (0, 2, 1),
            (0, 5, 1), (0, 6, 3), (0, 7, 5), (0, 9, 2), (0, 11, 3),
            (0, 12, 2), (0, 13, 1), (0, 14, 2), (0, 15, 5), (0, 16, 1),
            (2, 0, 5), (2, 1, 4),
        ],
    }

    for (cls, specno, name), order in specs.items():
        pages, per = class_trees(talents, tab_page, tab_class, cls)
        emit(cls, specno, name, pages, per, expand(order))
        print()


if __name__ == '__main__':
    sys.exit(main())
