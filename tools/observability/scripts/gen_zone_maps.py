#!/usr/bin/env python3
"""Regenerate the dashboard's zone art as *fully revealed* maps.

The dashboard draws the client's WorldMap texture: 12 base tiles (a 4x3 grid of
256px textures, `Interface\\WorldMap\\<Zone>\\<Zone><1..12>.blp`) that render the
map in its unexplored style, plus one `WorldMapOverlay.dbc` texture per sub-area
(`Interface\\WorldMap\\<Zone>\\<Overlay><n>.blp`) which paints in the revealed
detail. Shipping only the base tiles is why every map on the dashboard looks
fogged. This script composites the overlays at their DBC offsets and writes the
result back to `web/maps/*.webp`.

Output size: the existing file's size is kept. The daemon's world->map
percentages are relative to that canvas, so the art geometry must not move.
Zones whose composited map would be identical to the base (no overlays -- every
instance and both continents), plus the hand-drawn Maps of Mystery zones that
have no client folder, are left untouched.

Requires: python3 -m pip install mpyq Pillow
Usage:
  python3 gen_zone_maps.py --data "/path/to/WoW 1.12/Data" \
      [--overlay-dbc /path/to/WorldMapOverlay.dbc] [--maps ../web/maps] [--dry-run]

`--overlay-dbc` defaults to the newest WorldMapOverlay.dbc readable from the
client MPQs. Some Turtle clients keep it in an encrypted patch that mpyq cannot
open; pass the copy the server tooling extracted (e.g. the one in
tortoise-docker-penqle/data/dbc/) so the offsets match the shipped art.
"""

import argparse
import io
import json
import math
import os
import re
import struct
import sys

try:
    import mpyq
    from PIL import Image
except ImportError as exc:  # pragma: no cover - tooling guard
    sys.exit(f"missing dependency: {exc}. Run: python3 -m pip install mpyq Pillow")

# Highest patch priority first, so a later patch's file wins.
ARCHIVE_ORDER = [
    "patch-9.MPQ", "patch-8.MPQ", "patch-7.mpq", "patch-6.mpq", "patch-5.mpq",
    "patch-4.mpq", "patch-3.mpq", "patch-2.MPQ", "patch.MPQ", "interface.MPQ",
]
TILE = 256
GRID_W = 4  # the client lays the 12 detail tiles out 4 wide (WorldMapFrame.xml)

# Client folder typos / renames vs the dashboard's zone file names.
FOLDER_ALIASES = {"darnassus": "Darnassis"}

WEBP_QUALITY = 90


class Client:
    def __init__(self, data_dir):
        self.archives = []
        self.folders = {}  # folder -> {lowercase filename: archive}
        for name in ARCHIVE_ORDER:
            path = os.path.join(data_dir, name)
            if not os.path.exists(path):
                continue
            try:
                archive = mpyq.MPQArchive(path)
            except Exception as exc:
                print(f"  skip {name}: {exc}")
                continue
            self.archives.append((name, archive))
            for entry in archive.files:
                entry = entry.decode("utf8", "replace") if isinstance(entry, bytes) else entry
                m = re.match(r"Interface\\WorldMap\\([^\\]+)\\([^\\]+)$", entry)
                if m and m.group(2).lower().endswith(".blp"):
                    self.folders.setdefault(m.group(1), {})[m.group(2).lower()] = archive

    def read(self, folder, name):
        archive = self.folders.get(folder, {}).get(name.lower())
        if archive is None:
            return None
        data = archive.read_file(f"Interface\\WorldMap\\{folder}\\{name}")
        return Image.open(io.BytesIO(data)).convert("RGBA")

    def folder_for(self, stem):
        if stem in FOLDER_ALIASES:
            return FOLDER_ALIASES[stem]
        for folder in self.folders:
            if folder.lower() == stem.lower():
                return folder
        for folder in self.folders:
            if folder.lower().replace(" ", "") == stem.lower().replace(" ", ""):
                return folder
        return None

    def newest_dbc(self, entry):
        for _, archive in self.archives:
            try:
                return archive.read_file(entry)
            except Exception:
                continue
        return None


def parse_overlays(raw):
    """WorldMapOverlay.dbc -> {world_map_area_id: [(name, w, h, ox, oy), ...]}."""
    records, fields, rec_size, _ = struct.unpack("<IIII", raw[4:20])
    base = 20
    strings = base + records * rec_size
    string_block = raw[strings:]

    def string_at(offset):
        end = string_block.index(b"\0", offset)
        return string_block[offset:end].decode("utf8", "replace")

    by_area = {}
    for i in range(records):
        rec = struct.unpack(f"<{fields}I", raw[base + i * rec_size:base + i * rec_size + rec_size])
        by_area.setdefault(rec[1], []).append((string_at(rec[8]), rec[9], rec[10], rec[11], rec[12]))
    return by_area


def compose(client, folder, overlays):
    """Base 4x3 tile grid with every overlay alpha-composited at its DBC offset."""
    grid = Image.new("RGBA", (GRID_W * TILE, 3 * TILE), (0, 0, 0, 255))
    for i in range(GRID_W * 3):
        row, col = divmod(i, GRID_W)
        tile = client.read(folder, f"{folder}{i + 1}.blp")
        if tile is not None:
            grid.alpha_composite(tile, (col * TILE, row * TILE))

    painted = 0
    for name, width, height, offset_x, offset_y in overlays:
        cols = math.ceil(width / TILE)
        rows = math.ceil(height / TILE)
        for row in range(rows):
            for col in range(cols):
                index = row * cols + col + 1
                tile = client.read(folder, f"{name}{index}.blp")
                if tile is None:
                    continue
                grid.alpha_composite(tile, (offset_x + col * TILE, offset_y + row * TILE))
                painted += 1
    return grid, painted


def save_like(grid, path, dry_run):
    """Write the composite at the existing file's size (pad black if needed)."""
    with Image.open(path) as old:
        width, height = old.size
    out = Image.new("RGB", (width, height), (0, 0, 0))
    out.paste(grid.convert("RGB"), (0, 0))
    if dry_run:
        return width, height, grid.tobytes()
    out.save(path, "WEBP", quality=WEBP_QUALITY, method=6)
    return width, height, None


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--data", default=os.environ.get("WOW_DATA"), help="WoW client Data directory")
    parser.add_argument("--overlay-dbc", default=os.environ.get("WORLDMAP_OVERLAY_DBC"),
                        help="WorldMapOverlay.dbc to use (default: newest readable client copy)")
    parser.add_argument("--data-dir", default=os.path.join(here, "..", "web", "data"),
                        help="directory holding zone_maps.json and zones.json")
    parser.add_argument("--maps", default=os.path.join(here, "..", "web", "maps"),
                        help="directory holding the *.webp artwork")
    parser.add_argument("--dry-run", action="store_true", help="report work without writing files")
    args = parser.parse_args()

    if not args.data:
        parser.error("--data (or WOW_DATA) is required")

    dbc_path = args.overlay_dbc
    if dbc_path:
        with open(dbc_path, "rb") as handle:
            dbc_raw = handle.read()
        print(f"overlays: {dbc_path}")
    else:
        print(f"client:   {args.data}")
    client = Client(args.data)
    if dbc_path is None:
        dbc_raw = client.newest_dbc("DBFilesClient\\WorldMapOverlay.dbc")
        if dbc_raw is None:
            sys.exit("no readable WorldMapOverlay.dbc in the client; pass --overlay-dbc")
        print("overlays: newest readable WorldMapOverlay.dbc from the client MPQs")
    overlays_by_area = parse_overlays(dbc_raw)

    with open(os.path.join(args.data_dir, "zone_maps.json")) as handle:
        zone_maps = json.load(handle)
    with open(os.path.join(args.data_dir, "zones.json")) as handle:
        zones = json.load(handle)

    written = skipped = failed = 0
    for area_id, zone in sorted(zone_maps.items(), key=lambda kv: kv[1]["file"]):
        path = os.path.join(args.maps, zone["file"])
        if not os.path.exists(path):
            print(f"  {zone['file']}: no existing artwork, skipped")
            skipped += 1
            continue
        bounds = zones.get(f"{zone.get('map', 0)}_{area_id}")
        overlays = overlays_by_area.get(bounds["wma_id"], []) if bounds else []
        if not overlays:
            skipped += 1
            continue
        folder = client.folder_for(zone["file"][:-5])
        if folder is None:
            print(f"  {zone['file']}: no client WorldMap folder, skipped")
            failed += 1
            continue
        grid, painted = compose(client, folder, overlays)
        if not painted:
            print(f"  {zone['file']}: overlay textures missing, skipped")
            failed += 1
            continue
        width, height, _ = save_like(grid, path, args.dry_run)
        print(f"  {zone['file']}: {painted}/{len(overlays)} overlays -> {width}x{height}")
        written += 1

    print(f"\n{written} written, {skipped} skipped (no overlays), {failed} without a client folder")


if __name__ == "__main__":
    main()
