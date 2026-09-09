#!/usr/bin/env python3
"""
tools/import_travel_nodes.py

Offline navigation and fishing location data import and validation tool
for Tortoise WoW 1.18.1 (TortoiseBots).

This tool reads pre-computed travel node, link, path, and fishing location datasets
(such as those from donor SQL dumps), validates them against Tortoise WoW map/DBC
conventions and schema invariants (ai_playerbot_travelnode, ai_playerbot_travelnode_link,
ai_playerbot_travelnode_path, ai_playerbot_named_location), and generates clean,
optimized SQL migrations or applies them directly to a target MySQL/MariaDB world database.

Usage:
    python3 tools/import_travel_nodes.py --help
    python3 tools/import_travel_nodes.py --validate-only
    python3 tools/import_travel_nodes.py --output-sql data/sql/world/ai_playerbot_travel_data.sql
    python3 tools/import_travel_nodes.py --apply --db-host 127.0.0.1 --db-port 3306 --db-name mangos0 --db-user root --db-password root
"""

import argparse
import os
import re
import sys
from typing import Dict, List, Set, Tuple

DEFAULT_TRAVEL_NODES_PATHS = [
    "playerbots-references/shyalya-tortoise-wow/modules/mod-playerbots/sql/world/classic/ai_playerbot_travel_nodes.sql",
    "../playerbots-references/shyalya-tortoise-wow/modules/mod-playerbots/sql/world/classic/ai_playerbot_travel_nodes.sql",
    "/mnt/pny-ssd/Tortoise WoW Projects/playerbots-references/shyalya-tortoise-wow/modules/mod-playerbots/sql/world/classic/ai_playerbot_travel_nodes.sql",
]

DEFAULT_NAMED_LOCATION_PATHS = [
    "playerbots-references/shyalya-tortoise-wow/modules/mod-playerbots/sql/world/classic/ai_playerbot_named_location.sql",
    "../playerbots-references/shyalya-tortoise-wow/modules/mod-playerbots/sql/world/classic/ai_playerbot_named_location.sql",
    "/mnt/pny-ssd/Tortoise WoW Projects/playerbots-references/shyalya-tortoise-wow/modules/mod-playerbots/sql/world/classic/ai_playerbot_named_location.sql",
]

VALID_MAP_IDS = {
    0: "Eastern Kingdoms",
    1: "Kalimdor",
    30: "Alterac Valley",
    33: "Shadowfang Keep",
    34: "Stormwind Stockade",
    36: "Deadmines",
    43: "Wailing Caverns",
    47: "Razorfen Kraul",
    48: "Blackfathom Deeps",
    70: "Uldaman",
    90: "Gnomeregan",
    109: "Sunken Temple",
    129: "Razorfen Downs",
    189: "Scarlet Monastery",
    209: "Zul'Farrak",
    229: "Blackrock Spire",
    230: "Blackrock Depths",
    249: "Onyxia's Lair",
    289: "Scholomance",
    309: "Zul'Gurub",
    329: "Stratholme",
    349: "Maraudon",
    369: "Deeprun Tram",
    389: "Ragefire Chasm",
    409: "Molten Core",
    429: "Dire Maul",
    449: "Alliance PVP Barracks",
    450: "Horde PVP Barracks",
    469: "Blackwing Lair",
    489: "Warsong Gulch",
    509: "Ruins of Ahn'Qiraj",
    529: "Arathi Basin",
    531: "Temple of Ahn'Qiraj",
    533: "Naxxramas",
}


def find_file(configured: str, defaults: List[str]) -> str:
    if configured and os.path.isfile(configured):
        return configured
    for candidate in defaults:
        if os.path.isfile(candidate):
            return candidate
    return ""


def sanitize_table_name(query: str) -> str:
    """Removes schema qualifiers like `classicmangos`. or `world`."""
    return re.sub(r"`?[a-zA-Z0-9_]+`?\.`?([a-zA-Z0-9_]+)`?", r"`\1`", query)


def parse_travel_nodes_file(path: str):
    print(f"Reading travel nodes dataset: {path}")
    nodes: Dict[int, Tuple[str, int, float, float, float, int]] = {}
    links: Dict[Tuple[int, int], Tuple] = {}
    paths_count = 0
    raw_path_statements: List[str] = []

    insert_regex = re.compile(
        r"INSERT INTO\s+`?(?:[a-zA-Z0-9_]+\.)?([a-zA-Z0-9_]+)`?\s*(\([^)]+\))?\s*VALUES\s*(.+);",
        re.IGNORECASE | re.DOTALL,
    )

    with open(path, "r", encoding="utf-8", errors="replace") as f:
        current_sql = ""
        for line in f:
            stripped = line.strip()
            if not stripped or stripped.startswith("--") or stripped.startswith("/*"):
                continue

            current_sql += line
            if stripped.endswith(";"):
                stmt = current_sql.strip()
                current_sql = ""

                match = insert_regex.match(stmt)
                if not match:
                    continue

                table_name = match.group(1).lower()
                values_part = match.group(3)

                # Process by table
                if table_name == "ai_playerbot_travelnode":
                    # (id, name, map_id, x, y, z, linked)
                    rows = re.findall(
                        r"\(\s*(\d+)\s*,\s*'((?:[^'\\]|\\.)*)'\s*,\s*(\d+)\s*,\s*([-\d\.]+)\s*,\s*([-\d\.]+)\s*,\s*([-\d\.]+)\s*,\s*(\d+)\s*\)",
                        values_part,
                    )
                    for r in rows:
                        nid, name, mid, x, y, z, linked = (
                            int(r[0]),
                            r[1],
                            int(r[2]),
                            float(r[3]),
                            float(r[4]),
                            float(r[5]),
                            int(r[6]),
                        )
                        nodes[nid] = (name, mid, x, y, z, linked)

                elif table_name == "ai_playerbot_travelnode_link":
                    # (node_id, to_node_id, type, object, distance, swim_distance, extra_cost, calculated, max_creature_0, max_creature_1, max_creature_2)
                    rows = re.findall(
                        r"\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*([-\d\.]+)\s*,\s*([-\d\.]+)\s*,\s*([-\d\.]+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)",
                        values_part,
                    )
                    for r in rows:
                        link_key = (int(r[0]), int(r[1]))
                        links[link_key] = tuple(
                            int(r[i]) if i in (0, 1, 2, 3, 7, 8, 9, 10) else float(r[i])
                            for i in range(len(r))
                        )

                elif table_name == "ai_playerbot_travelnode_path":
                    # Count path points and sanitize prefix
                    sanitized_stmt = sanitize_table_name(stmt)
                    raw_path_statements.append(sanitized_stmt)
                    # Approximate points count from number of '(' occurrences
                    paths_count += values_part.count("(")

    print(f"  Loaded {len(nodes):,} nodes")
    print(f"  Loaded {len(links):,} links")
    print(f"  Loaded ~{paths_count:,} path spline points across {len(raw_path_statements):,} insert blocks")
    return nodes, links, paths_count, raw_path_statements


def parse_named_locations_file(path: str):
    print(f"Reading named locations dataset: {path}")
    locations = []
    insert_regex = re.compile(
        r"INSERT INTO\s+`?(?:[a-zA-Z0-9_]+\.)?ai_playerbot_named_location`?\s*(\([^)]+\))?\s*VALUES\s*(.+);",
        re.IGNORECASE | re.DOTALL,
    )

    raw_statements: List[str] = []
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        current_sql = ""
        for line in f:
            stripped = line.strip()
            if not stripped or stripped.startswith("--") or stripped.startswith("/*"):
                continue

            current_sql += line
            if stripped.endswith(";"):
                stmt = current_sql.strip()
                current_sql = ""

                match = insert_regex.match(stmt)
                if not match:
                    continue

                sanitized_stmt = sanitize_table_name(stmt)
                raw_statements.append(sanitized_stmt)

                # name, map_id, position_x, position_y, position_z, orientation, description
                rows = re.findall(
                    r"\(\s*'((?:[^'\\]|\\.)*)'\s*,\s*(\d+)\s*,\s*([-\d\.]+)\s*,\s*([-\d\.]+)\s*,\s*([-\d\.]+)\s*,\s*([-\d\.]+)\s*,\s*'((?:[^'\\]|\\.)*)'\s*\)",
                    match.group(2),
                )
                for r in rows:
                    locations.append((r[0], int(r[1]), float(r[2]), float(r[3]), float(r[4]), float(r[5]), r[6]))

    print(f"  Loaded {len(locations):,} named locations ({len(raw_statements):,} insert blocks)")
    return locations, raw_statements


def validate_dataset(nodes, links, locations):
    print("Validating dataset integrity...")
    errors = 0
    warnings = 0

    # 1. Validate nodes
    map_counts: Dict[int, int] = {}
    for nid, (name, mid, x, y, z, linked) in nodes.items():
        map_counts[mid] = map_counts.get(mid, 0) + 1
        if mid not in VALID_MAP_IDS:
            warnings += 1
            if warnings <= 5:
                print(f"  [WARN] Node {nid} '{name}' has unmapped map_id {mid}")
        if abs(x) > 25000.0 or abs(y) > 25000.0 or abs(z) > 10000.0:
            errors += 1
            print(f"  [ERROR] Node {nid} '{name}' has out-of-bounds coordinates ({x}, {y}, {z})")

    # 2. Validate links
    orphaned_links = 0
    for (from_id, to_id), link_data in links.items():
        if from_id not in nodes:
            orphaned_links += 1
            if orphaned_links <= 5:
                print(f"  [ERROR] Link from non-existent node {from_id} to {to_id}")
        if to_id not in nodes:
            orphaned_links += 1
            if orphaned_links <= 5:
                print(f"  [ERROR] Link to non-existent node {to_id} from {from_id}")
    errors += orphaned_links

    # 3. Validate locations
    fish_count = 0
    for name, mid, x, y, z, o, desc in locations:
        if name.startswith("FISH_LOCATION"):
            fish_count += 1
        if mid not in VALID_MAP_IDS:
            warnings += 1
        if abs(x) > 25000.0 or abs(y) > 25000.0:
            errors += 1
            if errors <= 10:
                print(f"  [ERROR] Location '{name}' has out-of-bounds coordinates ({x}, {y})")

    print("Validation Results:")
    print(f"  Total errors: {errors}")
    print(f"  Total warnings: {warnings}")
    print(f"  Fish locations found: {fish_count:,}")
    print("  Map distribution (top 5):")
    for mid, count in sorted(map_counts.items(), key=lambda item: item[1], reverse=True)[:5]:
        map_name = VALID_MAP_IDS.get(mid, f"Unknown Map {mid}")
        print(f"    - Map {mid} ({map_name}): {count:,} nodes")

    return errors == 0


def generate_consolidated_sql(nodes, links, path_stmts, location_stmts, output_file: str):
    print(f"Writing clean SQL migration: {output_file}")
    os.makedirs(os.path.dirname(os.path.abspath(output_file)), exist_ok=True)

    with open(output_file, "w", encoding="utf-8") as out:
        out.write("-- TortoiseBots Travel & Navigation Data Import\n")
        out.write("-- Automatically generated by tools/import_travel_nodes.py\n")
        out.write("SET FOREIGN_KEY_CHECKS = 0;\n\n")

        # ai_playerbot_travelnode
        out.write("-- 1. Travel Nodes\n")
        out.write("REPLACE INTO `ai_playerbot_travelnode` (`id`, `name`, `map_id`, `x`, `y`, `z`, `linked`) VALUES\n")
        node_lines = []
        for nid, (name, mid, x, y, z, linked) in nodes.items():
            safe_name = name.replace("\\", "\\\\").replace("'", "''")
            node_lines.append(f"({nid}, '{safe_name}', {mid}, {x:.4f}, {y:.4f}, {z:.4f}, {linked})")
        out.write(",\n".join(node_lines) + ";\n\n")

        # ai_playerbot_travelnode_link
        out.write("-- 2. Travel Node Links\n")
        out.write("REPLACE INTO `ai_playerbot_travelnode_link` (`node_id`, `to_node_id`, `type`, `object`, `distance`, `swim_distance`, `extra_cost`, `calculated`, `max_creature_0`, `max_creature_1`, `max_creature_2`) VALUES\n")
        link_lines = []
        for (from_id, to_id), d in links.items():
            link_lines.append(
                f"({d[0]}, {d[1]}, {d[2]}, {d[3]}, {d[4]:.2f}, {d[5]:.2f}, {d[6]:.2f}, {d[7]}, {d[8]}, {d[9]}, {d[10]})"
            )
        out.write(",\n".join(link_lines) + ";\n\n")

        # ai_playerbot_travelnode_path
        out.write("-- 3. Travel Node Path Splines\n")
        for stmt in path_stmts:
            clean_stmt = stmt.replace("INSERT INTO", "REPLACE INTO")
            out.write(clean_stmt + "\n")
        out.write("\n")

        # ai_playerbot_named_location
        out.write("-- 4. Named Locations & Fishing Points\n")
        for stmt in location_stmts:
            clean_stmt = stmt.replace("INSERT INTO", "REPLACE INTO")
            out.write(clean_stmt + "\n")
        out.write("\n")

        out.write("SET FOREIGN_KEY_CHECKS = 1;\n")

    print(f"Generated {os.path.getsize(output_file):,} bytes in {output_file}")


def main():
    parser = argparse.ArgumentParser(
        description="Import and validate travel node and named location caches for TortoiseBots."
    )
    parser.add_argument(
        "--travel-nodes",
        help="Path to ai_playerbot_travel_nodes.sql",
        default="",
    )
    parser.add_argument(
        "--named-locations",
        help="Path to ai_playerbot_named_location.sql",
        default="",
    )
    parser.add_argument(
        "--output-sql",
        help="Path to write consolidated SQL migration",
        default="data/sql/world/20260909090000_travel_nodes.sql",
    )
    parser.add_argument(
        "--validate-only",
        action="store_true",
        help="Only validate dataset integrity without generating output",
    )
    parser.add_argument(
        "--apply",
        action="store_true",
        help="Directly apply output SQL to database using mysql CLI",
    )
    parser.add_argument("--db-host", default="127.0.0.1", help="Database host")
    parser.add_argument("--db-port", default="3306", help="Database port")
    parser.add_argument("--db-user", default="root", help="Database user")
    parser.add_argument("--db-password", default="root", help="Database password")
    parser.add_argument("--db-name", default="mangos0", help="World database name")

    args = parser.parse_args()

    travel_nodes_file = find_file(args.travel_nodes, DEFAULT_TRAVEL_NODES_PATHS)
    if not travel_nodes_file:
        print("ERROR: Could not locate ai_playerbot_travel_nodes.sql dataset.")
        print("Please supply --travel-nodes <path>")
        sys.exit(1)

    named_locations_file = find_file(args.named_locations, DEFAULT_NAMED_LOCATION_PATHS)
    if not named_locations_file:
        print("ERROR: Could not locate ai_playerbot_named_location.sql dataset.")
        print("Please supply --named-locations <path>")
        sys.exit(1)

    nodes, links, paths_count, path_stmts = parse_travel_nodes_file(travel_nodes_file)
    locations, loc_stmts = parse_named_locations_file(named_locations_file)

    valid = validate_dataset(nodes, links, locations)
    if not valid:
        print("WARNING: Dataset has errors. Proceeding with caution.")

    if args.validate_only:
        print("Validation complete (--validate-only set). Exiting 0.")
        sys.exit(0)

    generate_consolidated_sql(nodes, links, path_stmts, loc_stmts, args.output_sql)

    if args.apply:
        print(f"Applying SQL to database {args.db_name} at {args.db_host}:{args.db_port}...")
        cmd = f"mysql -h {args.db_host} -P {args.db_port} -u {args.db_user} -p{args.db_password} {args.db_name} < '{args.output_sql}'"
        rc = os.system(cmd)
        if rc != 0:
            print(f"ERROR: MySQL execution failed with return code {rc}")
            sys.exit(1)
        print("Successfully applied navigation data to database.")

    print("Done.")


if __name__ == "__main__":
    main()
