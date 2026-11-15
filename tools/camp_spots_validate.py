#!/usr/bin/env python3
"""Validate TortoiseBots camp spots against world DB rules.

Checks every row in `ai_playerbot_camp_spot` (or parsed from a migration SQL
file via `--sql`) against:
  - no innkeeper (`npc_flags & 0x80`) within 250 yards (2D)
  - no non-friendly creature spawn (`npc_flags == 0`) within 30 yards (3D)
  - no friendly NPC (`npc_flags != 0`) within 15 yards (3D)
  - valid `map` (0 or 1), valid `zone_id` in `area_template`, non-empty `name`/`note`
  - no two camp spots closer than 50 yards to each other

Water, indoors, and terrain walkability cannot be checked from SQL; the bot
checks those at runtime.
"""

import argparse
import math
import re
import subprocess
import sys
from collections import defaultdict


INNKEEPER_MIN_DIST = 250.0
CREATURE_MIN_DIST = 30.0
FRIENDLY_NPC_MIN_DIST = 15.0
SPOT_MIN_SPACING = 50.0
GRID_CELL = 64.0


def run_db(db_cmd, sql):
    proc = subprocess.run(
        db_cmd,
        input=sql,
        text=True,
        shell=True,
        capture_output=True,
    )
    if proc.returncode != 0:
        raise RuntimeError(f"DB command failed ({proc.returncode}): {proc.stderr.strip()}")
    rows = []
    for line in proc.stdout.splitlines():
        if not line.strip():
            continue
        rows.append(line.split("\t"))
    return rows


def table_ref(world_db, table):
    return f"{world_db}.{table}" if world_db else table


def parse_sql_spots(sql_path):
    with open(sql_path, encoding="utf-8") as f:
        text = f.read()

    # Strip SQL line comments
    lines = [ln for ln in text.splitlines() if not ln.lstrip().startswith("--")]
    cleaned = "\n".join(lines)

    # Match tuples in INSERT INTO ... ai_playerbot_camp_spot ... VALUES (...), (...);
    m = re.search(
        r"INSERT\s+INTO\s+`?ai_playerbot_camp_spot`?\s*\(([^)]+)\)\s*VALUES\s*(.+?);",
        cleaned,
        re.IGNORECASE | re.DOTALL,
    )
    if not m:
        raise ValueError(f"No INSERT INTO ai_playerbot_camp_spot found in {sql_path}")

    cols = [c.strip().strip("`") for c in m.group(1).split(",")]
    values_blob = m.group(2)

    # Parse SQL value tuples respecting single-quoted strings with '' escapes
    tuples = []
    i = 0
    n = len(values_blob)
    while i < n:
        while i < n and values_blob[i] != "(":
            i += 1
        if i >= n:
            break
        i += 1  # skip '('
        fields = []
        while i < n:
            while i < n and values_blob[i] in " \t\r\n":
                i += 1
            if i < n and values_blob[i] == "'":
                i += 1
                buf = []
                while i < n:
                    if values_blob[i] == "'":
                        if i + 1 < n and values_blob[i + 1] == "'":
                            buf.append("'")
                            i += 2
                            continue
                        i += 1
                        break
                    if values_blob[i] == "\\" and i + 1 < n:
                        buf.append(values_blob[i + 1])
                        i += 2
                        continue
                    buf.append(values_blob[i])
                    i += 1
                fields.append("".join(buf))
            else:
                start = i
                while i < n and values_blob[i] not in ",)":
                    i += 1
                fields.append(values_blob[start:i].strip())
            while i < n and values_blob[i] in " \t\r\n":
                i += 1
            if i < n and values_blob[i] == ",":
                i += 1
                continue
            if i < n and values_blob[i] == ")":
                i += 1
                break
        if len(fields) != len(cols):
            raise ValueError(f"Column/value count mismatch: expected {len(cols)}, got {len(fields)} in {fields}")
        row = dict(zip(cols, fields))
        tuples.append({
            "id": len(tuples) + 1,
            "map": int(row["map"]),
            "x": float(row["x"]),
            "y": float(row["y"]),
            "z": float(row["z"]),
            "orientation": float(row.get("orientation", 0.0)),
            "zone_id": int(row.get("zone_id", 0)),
            "name": row.get("name", ""),
            "note": row.get("note", ""),
            "source": int(row.get("source", 0)),
        })
    return tuples


def load_db_spots(db_cmd, world_db):
    tbl = table_ref(world_db, "ai_playerbot_camp_spot")
    rows = run_db(
        db_cmd,
        f"SELECT id, map, x, y, z, orientation, zone_id, name, note, source FROM {tbl} ORDER BY id;",
    )
    spots = []
    for r in rows:
        spots.append({
            "id": int(r[0]),
            "map": int(r[1]),
            "x": float(r[2]),
            "y": float(r[3]),
            "z": float(r[4]),
            "orientation": float(r[5]),
            "zone_id": int(r[6]),
            "name": r[7],
            "note": r[8],
            "source": int(r[9]),
        })
    return spots


def load_world_data(db_cmd, world_db):
    c_tbl = table_ref(world_db, "creature")
    ct_tbl = table_ref(world_db, "creature_template")
    a_tbl = table_ref(world_db, "area_template")

    creature_rows = run_db(
        db_cmd,
        f"SELECT c.guid, c.map, c.position_x, c.position_y, c.position_z, ct.npc_flags, ct.name "
        f"FROM {c_tbl} c JOIN {ct_tbl} ct ON ct.entry = c.id WHERE c.map IN (0, 1);",
    )
    grid = defaultdict(list)
    innkeepers = defaultdict(list)
    for r in creature_rows:
        guid = int(r[0])
        m = int(r[1])
        x, y, z = float(r[2]), float(r[3]), float(r[4])
        flags = int(r[5])
        name = r[6]
        rec = (guid, x, y, z, flags, name)
        cx = int(math.floor(x / GRID_CELL))
        cy = int(math.floor(y / GRID_CELL))
        grid[(m, cx, cy)].append(rec)
        if flags & 0x80:
            innkeepers[m].append(rec)

    area_rows = run_db(db_cmd, f"SELECT entry, map_id, zone_id, name FROM {a_tbl} WHERE map_id IN (0, 1);")
    areas = {}
    for r in area_rows:
        areas[int(r[0])] = (int(r[1]), int(r[2]), r[3])

    return grid, innkeepers, areas


def validate(spots, grid, innkeepers, areas):
    violations = []
    for idx, s in enumerate(spots):
        sid = s["id"]
        m = s["map"]
        x, y, z = s["x"], s["y"], s["z"]
        zid = s["zone_id"]
        label = f"#{sid} '{s['name']}' (map={m}, zone={zid}, {x:.2f}, {y:.2f}, {z:.2f})"

        if m not in (0, 1):
            violations.append(f"{label}: invalid map {m} (expected 0 or 1)")
        if not s["name"].strip():
            violations.append(f"{label}: empty name")
        elif len(s["name"]) > 100:
            violations.append(f"{label}: name exceeds 100 chars ({len(s['name'])})")
        if not s["note"].strip():
            violations.append(f"{label}: empty note")
        elif len(s["note"]) > 255:
            violations.append(f"{label}: note exceeds 255 chars ({len(s['note'])})")
        if zid <= 0 or zid not in areas:
            violations.append(f"{label}: unknown zone_id {zid}")
        elif areas[zid][0] != m:
            violations.append(f"{label}: zone_id {zid} ({areas[zid][2]}) belongs to map {areas[zid][0]}, not {m}")

        # 1. Innkeeper within 250 yd (2D)
        for guid, ix, iy, iz, flags, iname in innkeepers.get(m, ()):
            d = math.hypot(x - ix, y - iy)
            if d < INNKEEPER_MIN_DIST:
                violations.append(
                    f"{label}: innkeeper '{iname}' (guid={guid}) is {d:.1f} yd away (< {INNKEEPER_MIN_DIST:.0f} yd)"
                )

        # 2. Creature spawns within 30 yd (npc_flags == 0) / friendly NPCs within 15 yd (npc_flags != 0)
        gx = int(math.floor(x / GRID_CELL))
        gy = int(math.floor(y / GRID_CELL))
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for guid, cx, cy, cz, flags, cname in grid.get((m, gx + dx, gy + dy), ()):
                    d3 = math.sqrt((x - cx) ** 2 + (y - cy) ** 2 + (z - cz) ** 2)
                    if flags == 0 and d3 < CREATURE_MIN_DIST:
                        violations.append(
                            f"{label}: creature '{cname}' (guid={guid}) is {d3:.1f} yd away (< {CREATURE_MIN_DIST:.0f} yd)"
                        )
                    elif flags != 0 and d3 < FRIENDLY_NPC_MIN_DIST:
                        violations.append(
                            f"{label}: friendly NPC '{cname}' (guid={guid}, flags={flags}) is {d3:.1f} yd away (< {FRIENDLY_NPC_MIN_DIST:.0f} yd)"
                        )

        # 3. Duplicate / too-close spots
        for j in range(idx + 1, len(spots)):
            o = spots[j]
            if o["map"] != m:
                continue
            d2 = math.hypot(x - o["x"], y - o["y"])
            if d2 < SPOT_MIN_SPACING:
                violations.append(
                    f"{label}: too close ({d2:.1f} yd < {SPOT_MIN_SPACING:.0f} yd) to #{o['id']} '{o['name']}'"
                )

    return violations


def main():
    ap = argparse.ArgumentParser(description="Validate ai_playerbot_camp_spot rows against world DB rules.")
    ap.add_argument("--db-cmd", required=True, help="Shell command reading SQL on stdin and writing TSV (-N -B) on stdout.")
    ap.add_argument("--world-db", default="", help="Optional world database name prefix (e.g. tw_world).")
    ap.add_argument("--sql", default=None, help="Optional migration SQL file to validate instead of querying ai_playerbot_camp_spot.")
    args = ap.parse_args()

    if args.sql:
        spots = parse_sql_spots(args.sql)
        source_desc = f"SQL file {args.sql}"
    else:
        spots = load_db_spots(args.db_cmd, args.world_db)
        source_desc = f"DB table {table_ref(args.world_db, 'ai_playerbot_camp_spot')}"

    grid, innkeepers, areas = load_world_data(args.db_cmd, args.world_db)
    violations = validate(spots, grid, innkeepers, areas)

    zones_covered = len({s["zone_id"] for s in spots})
    print(f"Camp spots validated: {len(spots)} spots across {zones_covered} zones (source: {source_desc})")
    print("Note: water, indoors, and terrain walkability cannot be checked from SQL; the bot checks those at runtime.")

    if violations:
        print(f"\nFAILED with {len(violations)} violation(s):")
        for v in violations:
            print(f"  - {v}")
        return 1

    print("PASSED: 0 violations.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
