#!/usr/bin/env python3
"""Dump Tortoise talent trees per class with positions, rank spells, names, prereqs.

Usage: python3 tools/talents/dump_trees.py --dbc <dbc-dir> --class <id> [--tree <page>]
Prints one line per talent: page row col talentId maxRank [depId:needRank] [reqSpell] rankSpellIds... name
"""
import argparse
import re
import struct
import sys
from pathlib import Path


def load_dbc(path):
    data = Path(path).read_bytes()
    magic, records, fields, recsize, strsize = struct.unpack('<4sIIII', data[:20])
    return [struct.unpack('<' + 'i' * fields, data[20 + i * recsize:20 + (i + 1) * recsize])
            for i in range(records)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dbc', required=True)
    ap.add_argument('--sql', required=True, help='tw_world_spell_template.sql for names')
    ap.add_argument('--cls', type=int, required=True)
    ap.add_argument('--tree', type=int, default=None)
    args = ap.parse_args()

    dbc = Path(args.dbc)
    talents = load_dbc(dbc / 'Talent.dbc')
    tabs = load_dbc(dbc / 'TalentTab.dbc')
    tab_page = {t[0]: (1 if t[0] == 41 else t[13]) for t in tabs}
    tab_class = {t[0]: t[12] & 0xFFFFFFFF for t in tabs}
    sql = Path(args.sql).read_text(encoding='utf-8', errors='replace')

    def name(sid):
        m = re.search(r'\(' + str(sid) + r',(?:[^()\'"]|\'[^\']*\')*?\'([^\']{1,80})\'\s*,\s*\d+\s*,\s*\'', sql)
        # fallback: first quoted string after id that looks like a name (fields 123+)
        return m.group(1) if m else '?'

    mask = 1 << (args.cls - 1)
    ct = [t for t in talents if tab_class.get(t[1], 0) & mask]
    ct.sort(key=lambda t: (tab_page.get(t[1], 99), t[2], t[3]))
    for t in ct:
        page = tab_page.get(t[1], 99)
        if args.tree is not None and page != args.tree:
            continue
        ranks = [t[4 + r] for r in range(5) if t[4 + r]]
        dep = f' dep={t[13]}:need{t[16] + 1}' if t[13] else ''
        req = f' reqSpell={t[20]}' if t[20] else ''
        nm = name(ranks[0]) if ranks else '?'
        print(f'p{page} r{t[2]} c{t[3]} id={t[0]} max={len(ranks)}{dep}{req} spells={ranks} :: {nm}')


if __name__ == '__main__':
    sys.exit(main())
