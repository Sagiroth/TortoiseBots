#!/usr/bin/env python3
"""Pool KPI report: one Markdown table of bot-pool health since a reset.

Reads the log/JSON files a snapshot dir already contains plus (optionally)
the character DB, and prints a Markdown KPI table, optionally side by side
with a stored baseline JSON.

Usage:
    python3 tools/pool_kpi_report.py --logs <snapshot-logs-dir> --reset "2026-10-02 12:54"
    python3 tools/pool_kpi_report.py --logs ... --bots-json api/bots.json \\
        --grinding-json api/grinding.json --activity-json api/activity.json \\
        --anomalies-json api/anomalies.json --anomaly-totals-json api/anomalies-totals.json \\
        --trades-log logs/trades.log \\
        --levelup-log logs/levelup.log --server-log logs/server_*.log \\
        --bot-logs-dir logs/bots --baseline tools/pool_kpi_baseline_2026-10-02.json
    python3 tools/pool_kpi_report.py --logs ... --save-baseline new.json

DB (bag fill) is opt-in so no stack name is hardcoded: pass e.g.
    --db-cmd "docker exec -i <container> sh -c 'mariadb -uroot -p\"$MYSQL_ROOT_PASSWORD\" -N -B'"
with SQL fed on stdin, plus --char-db/--world-db names. Without --db-cmd
the bag KPIs print as n/a.
"""

import argparse
import csv
import json
import re
import shlex
import statistics
import subprocess
import sys
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path

# bot_events.csv columns: ts, bot, event, pos, race, class, level, info1, info2
C_TS, C_BOT, C_EVENT, C_RACE, C_INFO1, C_INFO2 = 0, 1, 2, 4, 7, 8

FISH_SPELL_IDS = ("7620", "7731", "7732", "18248", "131474")
# Zone-migration KPIs (owner's overnight question: do pool bots move on to
# zones that fit their level?). The fit rule mirrors the module's own travel
# gates (GrindSpotPolicy.h / TravelMgr.cpp, validated ai_playerbot_zone_level
# cache seeded by data/sql/world/20260824090004_world.sql):
#   fit  <=>  bot_level - 2 <= zone_level <= bot_level + 5
# +5 is the RPG/quest-errand ceiling; the tighter autonomous grind ceiling
# (+3, +1 below 10) governs destination picks, not where a bot may stand.
# A zone_level <= 0 (unknown area, capital, instance) fails open -> n/a.
ZONE_BANDS = ("1-5", "6-10", "11-15", "16-20", "21+")
# Race (SharedDefines.h Races enum) -> expected start zone id (StartZoneBalance.h).
RACE_START_ZONE = {2: 14, 8: 14, 9: 14, 6: 215, 5: 85, 1: 12, 10: 12, 3: 1, 7: 1, 4: 141}
START_ZONE_IDS = frozenset(RACE_START_ZONE.values())
ZONE_LEVEL_FALLBACK = {
    # Module ai_playerbot_zone_level cache (SELECT id, level FROM <world>.ai_playerbot_zone_level):
    # only ids the report actually reads (snapshot zones + capitals). Refresh
    # from the migration when these drift.
    1: 7, 12: 6, 14: 8, 17: 16, 85: 7, 130: 14, 141: 7, 215: 6,
    148: 15, 40: 14, 38: 13, 33: 39, 10: 24, 11: 24, 267: 25, 331: 24,
    1497: 10, 1638: 10, 1657: 10, 1519: 10, 1537: 10,
}


def zone_band(level):
    level = int(level)
    if level <= 5:
        return "1-5"
    if level <= 10:
        return "6-10"
    if level <= 15:
        return "11-15"
    if level <= 20:
        return "16-20"
    return "21+"


def zone_fit(zone_level, bot_level):
    """None when the zone level is unknown (fail open); else fit bool."""
    zone_level = int(zone_level)
    if zone_level <= 0:
        return None
    bot_level = int(bot_level)
    return bot_level - 2 <= zone_level <= bot_level + 5


def load_zone_levels(db_cmd, world_db):
    """Validated zone levels from the world DB; {} when no --db-cmd."""
    if not db_cmd:
        return {}
    levels = {}
    for zid, lvl in run_db(db_cmd, "", world_db,
                            f"SELECT id, level FROM {world_db}.ai_playerbot_zone_level"):
        try:
            levels[int(zid)] = int(lvl)
        except ValueError:
            continue
    return levels


def load_levelup_zones(path, reset=None):
    """bot -> [(ts, level, zone)] from levelup.log (zone rides each row).

    reset=None keeps every row: start-zone inference needs pre-window history
    (pool bots are already L2+ when the window opens).
    """
    pat = re.compile(r"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}) Character (\S+?):\d+ "
                     r"\[c\d+ r(\d+)\] reaches level\s+(\d+), zone (\d+)")
    traj = defaultdict(list)
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = pat.match(line)
            if not m:
                continue
            t = parse_naive_utc(m.group(1))
            if reset is not None and t < reset:
                continue
            traj[m.group(2)].append((t, int(m.group(4)), int(m.group(5)), int(m.group(3))))
    for v in traj.values():
        v.sort()
    return traj


def pool_scope_note(db_cmd, char_db, bots):
    """One-line pool-scope note for the zone rows; scope itself stays the snapshot.

    Pool = characters on managed pool accounts (tortoise_bots_pool_account),
    minus hired-out companions (tortoise_bots_hire ledger) - the same scope
    RandomBotService loads. The snap1540_char copy is a *different* pool cycle
    (zero name overlap with the night2 snapshot), so a guid/name intersection
    would silently empty the report; the zone rows therefore always cover the
    snapshot bots and only report the DB overlap for context.
    """
    if not db_cmd or not bots:
        return "all snapshot bots (no pool check: need --db-cmd + --bots-json)"
    try:
        pool_names = {r[0] for r in run_db(
            db_cmd, char_db, "",
            f"SELECT name FROM {char_db}.characters WHERE account IN "
            f"(SELECT account_id FROM {char_db}.tortoise_bots_pool_account)")}
        snap = {b["name"] for b in bots}
        return (f"all {len(snap)} snapshot bots "
                f"({len(snap & pool_names)} also on managed pool accounts in {char_db})")
    except RuntimeError as e:
        return f"all snapshot bots (pool lookup failed: {str(e)[:120]})"


def load_zone_transitions(path, reset):
    """(moved_bots, leave_rows): zone changes from LeaveOutgrownZone rows.

    One row is one leave-request fire: info1 = zone name left, info2 =
    outgrown/capital. Pairs need ids, which bot_events lacks, so the top-pairs
    detail comes from the levelup trajectory instead (see below).
    """
    moved, leave_rows = set(), 0
    with open(path, newline="", encoding="utf-8", errors="replace") as f:
        for row in csv.reader(f):
            if len(row) <= C_INFO2 or row[C_EVENT].strip() != "LeaveOutgrownZone":
                continue
            try:
                if parse_ts(row[C_TS]) < reset:
                    continue
            except ValueError:
                continue
            leave_rows += 1
            moved.add(row[C_BOT])
    return moved, leave_rows



def parse_ts(s):
    """'2026-10-02 12:54:30+00' or naive '2026-10-02 12:54:30' -> aware UTC."""
    s = s.strip()
    if s.endswith("+00"):
        s = s[:-3] + "+0000"
    try:
        dt = datetime.strptime(s, "%Y-%m-%d %H:%M:%S%z")
    except ValueError:
        dt = datetime.strptime(s[:19], "%Y-%m-%d %H:%M:%S").replace(tzinfo=timezone.utc)
    return dt.astimezone(timezone.utc)


def parse_naive_utc(s):
    s = s.strip()
    f = "%Y-%m-%d %H:%M:%S" if len(s) >= 19 else "%Y-%m-%d %H:%M"
    return datetime.strptime(s[:19] if len(s) >= 19 else s, f).replace(tzinfo=timezone.utc)


def fmt(v, nd=1):
    if v is None:
        return "n/a"
    if isinstance(v, float):
        return f"{v:.{nd}f}"
    return str(v)


def load_bot_events(path, reset):
    """Returns (Counter[event], vendor_gaps, max_ts, n_rows)."""
    from collections import Counter
    counts = Counter()
    last_vendor = {}
    gaps = []
    max_ts = None
    n = 0
    with open(path, newline="", encoding="utf-8", errors="replace") as f:
        for row in csv.reader(f):
            if len(row) <= C_INFO2:
                continue
            try:
                t = parse_ts(row[C_TS])
            except ValueError:
                continue
            if t < reset:
                continue
            n += 1
            if max_ts is None or t > max_ts:
                max_ts = t
            ev = row[C_EVENT].strip()
            counts[ev] += 1
            if ev == "TravelTarget" and row[C_INFO1].strip() == "Vendor":
                b = row[C_BOT]
                if b in last_vendor:
                    gaps.append((t - last_vendor[b]).total_seconds())
                last_vendor[b] = t
    return counts, gaps, max_ts, n


def load_deaths(path, reset):
    n = 0
    max_ts = None
    with open(path, newline="", encoding="utf-8", errors="replace") as f:
        for row in csv.reader(f):
            if len(row) < 2:
                continue
            try:
                t = parse_ts(row[0])
            except ValueError:
                continue
            if t < reset:
                continue
            n += 1
            if max_ts is None or t > max_ts:
                max_ts = t
    return n, max_ts


def load_levelup(path, reset):
    """bot -> {level: first reach ts}. Row: '... Character NAME:guid [cX rY] reaches level  N, ...'"""
    pat = re.compile(r"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}) Character (\S+?):\d+ "
                     r"\[c\d+ r\d+\] reaches level\s+(\d+)")
    first = defaultdict(dict)
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = pat.match(line)
            if not m:
                continue
            t = parse_naive_utc(m.group(1))
            if t < reset:
                continue
            bot, lvl = m.group(2), int(m.group(3))
            if lvl not in first[bot]:
                first[bot][lvl] = t
    return first


def load_botperf(path, reset):
    pat = re.compile(r"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}).*BOTPERF passUs=(\d+)")
    vals = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = pat.search(line)
            if not m:
                continue
            if parse_naive_utc(m.group(1)) < reset:
                continue
            vals.append(int(m.group(2)) / 1000.0)
    return vals


def load_trades(path, reset):
    pat = re.compile(r"^(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})")
    listings = buys = 0
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = pat.match(line)
            if not m or parse_naive_utc(m.group(1)) < reset:
                continue
            if "[AuctionHouse]" not in line:
                continue
            if "listing Item" in line:
                listings += 1
            elif re.search(r"\b(buy|bid)\b", line, re.IGNORECASE):
                buys += 1
    return listings, buys


def count_fishing_casts(bot_logs_dir, reset):
    """CAST_START lines for fishing spells at/after reset; per-bot log ts are naive UTC."""
    n_files = casts = 0
    pat = re.compile(r"\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})[^]]*\] \[CAST_START\] spell=([^(]+)\((\d+)\)")
    for p in Path(bot_logs_dir).glob("*.log"):
        n_files += 1
        try:
            with open(p, encoding="utf-8", errors="replace") as f:
                for line in f:
                    if "CAST_START" not in line:
                        continue
                    m = pat.search(line)
                    if not m:
                        continue
                    if m.group(2).strip().lower().startswith("fish") or m.group(3) in FISH_SPELL_IDS:
                        if parse_naive_utc(m.group(1)) >= reset:
                            casts += 1
        except OSError:
            continue
    return casts, n_files


def run_db(db_cmd, char_db, world_db, sql):
    """Run SQL via the configured command (SQL on stdin), return TSV rows."""
    proc = subprocess.run(shlex.split(db_cmd), input=sql,
                          capture_output=True, text=True, timeout=300)
    if proc.returncode != 0:
        raise RuntimeError(f"db command failed: {proc.stderr.strip()[:300]}")
    return [line.split("\t") for line in proc.stdout.splitlines() if line.strip()]


def load_bag_fill(db_cmd, char_db, world_db, guids):
    """Returns dict(median_fill_pct, ge90, full, bagless, n). Capacity 16 + equipped bags."""
    guid_list = ",".join(str(g) for g in guids)
    inv = run_db(db_cmd, char_db, world_db,
                 f"SELECT guid, bag, slot, item FROM {char_db}.character_inventory "
                 f"WHERE guid IN ({guid_list})")
    item_guids = {r[3] for r in inv}
    chunks = ",".join(item_guids) if item_guids else "0"
    inst = run_db(db_cmd, char_db, world_db,
                  f"SELECT guid, itemEntry FROM {char_db}.item_instance WHERE guid IN ({chunks})")
    entry_of = {r[0]: r[1] for r in inst}
    entries = {e for e in entry_of.values()}
    echunks = ",".join(entries) if entries else "0"
    tmpl = run_db(db_cmd, char_db, world_db,
                  f"SELECT entry, container_slots FROM {world_db}.item_template "
                  f"WHERE entry IN ({echunks})")
    slots_of = {r[0]: int(r[1]) for r in tmpl}

    bags = {}   # bot guid -> list of bag item guids in slots 19-22
    used = defaultdict(int)
    for guid, bag, slot, item in inv:
        slot = int(slot)
        if bag == "0" and slot in (19, 20, 21, 22):
            bags.setdefault(guid, []).append(item)
        elif bag == "0" and 23 <= slot <= 38:
            used[guid] += 1  # backpack slots only; 39+ are bank/equip-adjacent
    bag_item_guids = {i for lst in bags.values() for i in lst}
    for guid, bag, slot, item in inv:
        if bag != "0" and bag in bag_item_guids:
            used[guid] += 1
    fills, bagless = [], 0
    for g in guids:
        g = str(g)
        cap = 16 + sum(slots_of.get(entry_of.get(i, ""), 0) for i in bags.get(g, []))
        if not bags.get(g):
            bagless += 1
        fills.append(100.0 * used.get(g, 0) / cap if cap else 0.0)
    fills.sort()
    return {"median_fill_pct": round(statistics.median(fills), 1) if fills else None,
            "ge90": sum(1 for x in fills if x >= 90.0),
            "full": sum(1 for x in fills if x >= 100.0),
            "bagless": bagless, "n": len(fills)}


def trail_spread(bot):
    tr = bot.get("trail") or []
    if not tr:
        return None
    x0, y0 = bot["x"], bot["y"]
    return max(((p["x"] - x0) ** 2 + (p["y"] - y0) ** 2) ** 0.5 for p in tr)


def main():
    ap = argparse.ArgumentParser(description="Pool KPI report since a reset timestamp")
    ap.add_argument("--logs", required=True, help="dir with bot_events.csv + deaths.csv")
    ap.add_argument("--reset", required=True, help="'2026-10-02 12:54' (UTC)")
    ap.add_argument("--bots-json", default=None)
    ap.add_argument("--grinding-json", default=None)
    ap.add_argument("--activity-json", default=None)
    ap.add_argument("--anomalies-json", default=None)
    ap.add_argument("--anomaly-totals-json", default=None,
                    help="daemon GET /api/v1/anomalies/totals dump: cumulative per-session counts, preferred for ACTION_LOOP/UNREACHABLE rows")
    ap.add_argument("--trades-log", default=None)
    ap.add_argument("--levelup-log", default=None)
    ap.add_argument("--server-log", default=None)
    ap.add_argument("--bot-logs-dir", default=None, help="per-bot *.log dir for fishing casts")
    ap.add_argument("--db-cmd", default=None, help="shell command reading SQL on stdin, TSV (-N -B) on stdout, e.g. \"docker exec -i <c> mariadb -uroot -p\\\"$PW\\\" -N -B\"")
    ap.add_argument("--char-db", default="snap1540_char")
    ap.add_argument("--world-db", default="tw_world")
    ap.add_argument("--baseline", default=None, help="baseline JSON for side-by-side column")
    ap.add_argument("--save-baseline", default=None, help="write current KPIs as baseline JSON")
    ap.add_argument("--out", default=None, help="write Markdown here (default stdout)")
    args = ap.parse_args()

    reset = parse_ts(args.reset) if "+" in args.reset else parse_naive_utc(args.reset)
    logs = Path(args.logs)
    rows = []  # (kpi, value, note)

    ev, gaps, ev_end, ev_n = load_bot_events(logs / "bot_events.csv", reset)
    deaths, death_end = load_deaths(logs / "deaths.csv", reset)
    end = max(t for t in (ev_end, death_end) if t is not None)
    hours = (end - reset).total_seconds() / 3600
    minutes = (end - reset).total_seconds() / 60

    rows.append(("deaths/min (pool)", fmt(deaths / minutes, 2),
                 f"{deaths} deaths / {minutes:.0f} min (deaths.csv)"))
    rows.append(("BotDeath rows (bot_events)", fmt(ev.get("BotDeath", 0)),
                 "cross-check vs deaths.csv"))
    rows.append(("DeathClusterEscape rows", fmt(ev.get("DeathClusterEscape", 0)), "loop-breaker fires"))
    rows.append(("DeathSpotAvoided rows", fmt(ev.get("DeathSpotAvoided", 0)),
                 "0 = gate not live / no avoids"))

    bots = []
    if args.bots_json:
        bots = json.load(open(args.bots_json))
    grinding = json.load(open(args.grinding_json)) if args.grinding_json else None
    activity = json.load(open(args.activity_json)) if args.activity_json else None

    if grinding and "median_xp_hour" in grinding:
        rows.append(("median XP/h per bot", fmt(grinding["median_xp_hour"], 0),
                     "daemon grinding.json (pool XP model)"))
    elif bots:
        rows.append(("median XP/h per bot", fmt(statistics.median(b["xp_per_hour"] for b in bots), 0),
                     "median of bots.json xp_per_hour"))
    else:
        rows.append(("median XP/h per bot", "n/a", "need --grinding-json or --bots-json"))

    if activity:
        s = activity["summary"]
        kpm = s["counters"]["kills"] / (s["elapsed_sec"] / 3600) / s["bots_tracked"]
        rows.append(("kills/bot/h", fmt(kpm, 1),
                     f"{s['counters']['kills']} kills / {s['bots_tracked']} bots / "
                     f"{s['elapsed_sec'] / 3600:.2f} h (activity.json)"))
    else:
        rows.append(("kills/bot/h", "n/a", "need --activity-json"))

    if bots:
        ghosts = sum(1 for b in bots if b.get("activity", {}).get("ghost_seconds", 0) > 300)
        noxp = [b for b in bots if b["level"] <= 3 and (
            b.get("xp_gain_age_sec") is None or b["xp_gain_age_sec"] < 0
            or b["xp_gain_age_sec"] > 3600)]
        lvls = sum(1 for b in bots if b["level"] <= 3)
        static = [(b["name"], b["level"], b["state"]) for b in bots
                  if (lambda s: s is not None and s < 10)(trail_spread(b))]
        rows.append(("bots with >5 min total ghost", fmt(ghosts),
                     f"{ghosts}/{len(bots)} (bots.json ghost_seconds)"))
        rows.append(("lvl1-3, no XP last 60 min", f"{len(noxp)}/{lvls}",
                     "xp_gain_age_sec missing/neg/>3600s (incl. never-gained)"))
        rows.append(("static bots (trail <10 yd)", fmt(len(static)),
                     "snapshot proxy, not duration: " + ", ".join(
                         f"{n} L{l}/{s}" for n, l, s in static[:8])
                     + (" …" if len(static) > 8 else "")))
    else:
        for k in ("bots with >5 min total ghost", "lvl1-3, no XP last 60 min",
                  "static bots (trail <10 yd)"):
            rows.append((k, "n/a", "need --bots-json"))

    vendors = _vendor_pick_bots(logs / "bot_events.csv", reset)
    vendor_picks = len(gaps) + len(vendors)  # gaps = picks after each bot's first
    rows.append(("Vendor TravelTarget picks", fmt(vendor_picks),
                 f"{len(vendors)} bots; {ev.get('TravelMoveFailed', 0)} MoveFailed rows (all purposes)"))
    rows.append(("median vendor re-pick gap", fmt(statistics.median(gaps), 0) + " s" if gaps else "n/a",
                 f"{len(gaps)} consecutive-pick gaps"))

    if args.anomaly_totals_json:
        totals = json.load(open(args.anomaly_totals_json))
        by_type = totals.get("totals", {})
        by_action = totals.get("by_action", {})
        since = totals.get("since_str", "") or ""
        loops = by_action.get("ACTION_LOOP", {})
        top = ", ".join(f"{k} {v}" for k, v in
                        sorted(loops.items(), key=lambda kv: -kv[1])[:4]) or "none"
        rows.append(("ACTION_LOOP anomalies", fmt(by_type.get("ACTION_LOOP", 0)),
                     f"session totals{(' ' + since) if since else ''}: {top}"))
        rows.append(("UNREACHABLE_TARGET anomalies", fmt(by_type.get("UNREACHABLE_TARGET", 0)),
                     f"session totals{(' ' + since) if since else ''}"))
    elif args.anomalies_json:
        an = json.load(open(args.anomalies_json))
        loops = [a for a in an if a.get("type") == "ACTION_LOOP"]
        from collections import Counter
        c = Counter(a.get("last_action", "?") for a in loops)
        rows.append(("ACTION_LOOP anomalies", fmt(len(loops)),
                     "rolling 1000-row feed only (pass --anomaly-totals-json for whole-run counts): " +
                     (", ".join(f"{k} {v}" for k, v in c.most_common(4)) or "none")))
        rows.append(("UNREACHABLE_TARGET anomalies", fmt(sum(
            1 for a in an if a.get("type") == "UNREACHABLE_TARGET")),
                     "rolling 1000-row feed only (pass --anomaly-totals-json for whole-run counts)"))
    else:
        rows.append(("ACTION_LOOP anomalies", "n/a", "need --anomalies-json"))
        rows.append(("UNREACHABLE_TARGET anomalies", "n/a", "need --anomalies-json"))

    if args.server_log:
        perf = load_botperf(args.server_log, reset)
        if perf:
            n = len(perf)
            t = [statistics.mean(perf[:n // 3]), statistics.mean(perf[n // 3:2 * n // 3]),
                 statistics.mean(perf[2 * n // 3:])]
            rows.append(("BOTPERF AI pass", f"mean {fmt(statistics.mean(perf), 1)} ms, "
                         f"max {fmt(max(perf), 1)} ms",
                         f"n={n}; thirds {fmt(t[0], 1)}/{fmt(t[1], 1)}/{fmt(t[2], 1)} ms"))
        else:
            rows.append(("BOTPERF AI pass", "n/a", "no BOTPERF lines in window"))
    else:
        rows.append(("BOTPERF AI pass", "n/a", "need --server-log"))

    rows.append(("gathering node opens/h", fmt(ev.get("GatherLoot", 0) / hours, 1),
                 f"{ev.get('GatherLoot', 0)} GatherLoot rows / {hours:.2f} h"))
    if args.bot_logs_dir:
        casts, n_files = count_fishing_casts(args.bot_logs_dir, reset)
        rows.append(("fishing casts", fmt(casts), f"CAST_START over {n_files} per-bot logs"))
    else:
        rows.append(("fishing casts", "n/a", "need --bot-logs-dir"))

    if args.trades_log:
        listings, buys = load_trades(args.trades_log, reset)
        rows.append(("AH listings / buys", f"{listings} / {buys}", "trades.log in-window"))
    else:
        rows.append(("AH listings / buys", "n/a", "need --trades-log"))
    rows.append(("AhAction / AhMarket rows", f"{fmt(ev.get('AhAction', 0))} / "
                 f"{fmt(ev.get('AhMarket', 0))}", "bot_events sell/market path"))

    if args.levelup_log:
        first = load_levelup(args.levelup_log, reset)
        for lvl in (5, 6, 7, 10):
            v = sorted((d[lvl] - reset).total_seconds() / 60
                       for d in first.values() if lvl in d)
            rows.append((f"median min reset→L{lvl}",
                         fmt(statistics.median(v), 1) if v else "n/a", f"n={len(v)}"))
    else:
        rows.append(("median min reset→L5/6/7/10", "n/a", "need --levelup-log"))

    if args.levelup_log:
        traj = load_levelup_zones(args.levelup_log)  # full history: start-zone needs pre-window rows
        win_traj = {b: [(t, l, z) for t, l, z, _ in v if t >= reset]
                    for b, v in traj.items()}
        win_traj = {b: v for b, v in win_traj.items() if v}
        if args.db_cmd:
            try:
                zone_levels = load_zone_levels(args.db_cmd, args.world_db)
            except Exception as e:
                zone_levels, _zone_err = {}, str(e)[:120]
        else:
            zone_levels, _zone_err = {}, ""
        if not zone_levels:
            zone_levels = dict(ZONE_LEVEL_FALLBACK)
            zl_src = ("fallback table (pass --db-cmd for live ai_playerbot_zone_level)"
                      + (f"; DB error: {_zone_err}" if _zone_err else ""))
        else:
            zl_src = f"live ai_playerbot_zone_level ({len(zone_levels)} ids)"
        scope_note = pool_scope_note(args.db_cmd, args.char_db, bots)
        names = {b["name"] for b in bots} if bots else set(win_traj)
        cur = {b["name"]: (b["level"], b["zone"]) for b in bots} if bots else {}
        win_traj = {b: v for b, v in win_traj.items() if b in names}
        # Start zone: earliest recorded zone per bot. Cross-check on the pool:
        # 471/498 agree with the race-expected start zone (2026-10-02 night2 run),
        # so the "left start zone" share below is a 1-2 bot approximation only.
        start = {b: v[0][2] for b, v in traj.items() if b in names}
        # Highest zone (by level) reached per bot over the window: the window
        # trajectory plus the snapshot zone; capitals fail open and never win
        # (zone_level 10 beats every starter valley for a level-10+ bot).
        def _zl(z):
            return zone_levels.get(int(z), ZONE_LEVEL_FALLBACK.get(int(z), 0))
        top = {}
        for b in names:
            cands = []
            if b in start:
                cands.append((_zl(start[b]), start[b]))
            cands += [(_zl(z), z) for _, _, z in win_traj.get(b, [])]
            if b in cur:
                cands.append((_zl(cur[b][1]), cur[b][1]))
            known = [(zl, z) for zl, z in cands if zl > 0]
            if known:
                top[b] = max(known)[1]
        left = sum(1 for b, s in start.items()
                   if (top.get(b, s) != s) or
                   (len({z for _, _, z in win_traj.get(b, [])} | ({cur[b][1]} if b in cur else set())) > 1))
        stuck8 = ([b for b in names if b in start and b in cur
                   and cur[b][1] == start[b] and cur[b][0] > 8])
        rows.append(("zone: left start zone", f"{left}/{len(start)}" if start else "n/a",
                     f"{scope_note}; highest-zone or window-trail left earliest zone ({zl_src})"))
        rows.append(("zone: still in start zone above L8", fmt(len(stuck8)),
                     (("snapshot zone == earliest zone, level > 8: " +
                       ", ".join(sorted(stuck8)[:8]) + (" …" if len(stuck8) > 8 else ""))
                      if stuck8 else "none (snapshot zone == earliest zone, level > 8)")))
        if top:
            from collections import Counter
            tc = Counter(top.values())
            beyond = sorted(((z, c) for z, c in tc.items() if z not in START_ZONE_IDS),
                            key=lambda kv: -kv[1])
            rows.append(("zone: highest zone reached",
                         f"{len(tc)} distinct ({sum(c for _, c in beyond)} bots beyond start zones)",
                         "top: " + ", ".join(
                             f"{z}×{c}" for z, c in tc.most_common(6))
                         + ("; beyond start: " + ", ".join(f"{z}×{c}" for z, c in beyond[:6])
                            if beyond else "; none beyond start zones")))
        # Transitions over the window from the levelup trajectory (ids, not
        # names: bot_events zone rows carry names only). LeaveOutgrownZone fires
        # are the request-side cross-check (info1 = zone name left).
        moved_lv, pairs = set(), Counter()
        for b, v in win_traj.items():
            zs = [z for _, _, z in v]
            for a, bb in zip(zs, zs[1:]):
                if a != bb:
                    moved_lv.add(b)
                    pairs[(a, bb)] += 1
        moved_lo, lo_rows = load_zone_transitions(logs / "bot_events.csv", reset)
        moved_lo &= names
        rows.append(("zone: changed zone in window", fmt(len(moved_lv)),
                     f"{len(moved_lv)} bots via levelup trail; {len(moved_lo)} bots / {lo_rows} LeaveOutgrownZone fires (bot_events)"))
        if pairs:
            rows.append(("zone: top from→to pairs", f"{len(pairs)} pairs",
                         "; ".join(f"{a}→{bb}×{c}" for (a, bb), c in pairs.most_common(5))))
        # Fit share per level band from the bots.json snapshot (zone + level).
        if cur:
            band_fit = defaultdict(lambda: [0, 0])  # band -> [fit, n]
            for b, (lvl, z) in cur.items():
                f = zone_fit(_zl(z), lvl)
                if f is None:
                    continue
                cell = band_fit[zone_band(lvl)]
                cell[1] += 1
                cell[0] += 1 if f else 0
            for band in ZONE_BANDS:
                fit, n = band_fit.get(band, (0, 0))
                rows.append((f"zone: fit share L{band}",
                             f"{100.0 * fit / n:.0f}% ({fit}/{n})" if n else "n/a",
                             f"bot L-2 <= zone_level <= bot L+5 ({zl_src})"))
        # Left-start-zone by start zone (race stays in bot_events: col 4).
        seen_race = {}
        with open(logs / "bot_events.csv", newline="", encoding="utf-8", errors="replace") as f:
            for row in csv.reader(f):
                if len(row) <= C_INFO2 or row[C_BOT] not in names or row[C_BOT] in seen_race:
                    continue
                try:
                    if parse_ts(row[C_TS]) >= reset:
                        seen_race[row[C_BOT]] = int(row[C_RACE])
                except ValueError:
                    continue
        by_start = defaultdict(lambda: [0, 0])  # start zone -> [left, n]
        for b, s in start.items():
            if RACE_START_ZONE.get(seen_race.get(b, -1), s) != s:
                s = RACE_START_ZONE.get(seen_race.get(b, -1), s)
            cell = by_start[s]
            cell[1] += 1
            if top.get(b, s) != s or (b in cur and cur[b][1] != s):
                cell[0] += 1
        if by_start:
            rows.append(("zone: left start zone by start",
                         "; ".join(f"{s}:{l}/{n}" for s, (l, n) in sorted(by_start.items())),
                         "start zone id: left/n (race-expected when bot_events race disagrees)"))
    else:
        rows.append(("zone: left start zone", "n/a", "need --levelup-log"))

    if args.db_cmd and bots:
        try:
            bag = load_bag_fill(args.db_cmd, args.char_db, args.world_db,
                                [b["guid"] for b in bots])
            rows.append(("bag fill median / >=90%", f"{bag['median_fill_pct']}% / {bag['ge90']}",
                         f"n={bag['n']}, full={bag['full']}, bagless={bag['bagless']} (DB)"))
        except Exception as e:
            rows.append(("bag fill median / >=90%", "n/a", f"DB error: {e}"))
    else:
        rows.append(("bag fill median / >=90%", "n/a",
                     "need --db-cmd and --bots-json" if not args.db_cmd
                     else "need --bots-json for guid list"))

    rows.append(("TrainerAction / TrainerNoMoney",
                 f"{fmt(ev.get('TrainerAction', 0))} / {fmt(ev.get('TrainerNoMoney', 0))}",
                 "visits vs cannot-afford"))
    rows.append(("QuestCompleted rows", fmt(ev.get("QuestCompleted", 0)), "turn-in completions"))
    rescues = ev.get("UseHearthStoneAction", 0) + ev.get("LongStuckFallback", 0)
    rows.append(("unstuck rescues/h (hearth+fallback)", fmt(rescues / hours, 1),
                 f"{ev.get('UseHearthStoneAction', 0)} hearth + "
                 f"{ev.get('LongStuckFallback', 0)} LongStuckFallback / {hours:.2f} h; "
                 f"RepopAction {ev.get('RepopAction', 0)} extra context"))

    kpis = {k: {"value": v, "note": n} for k, v, n in rows}
    if args.save_baseline:
        json.dump({"meta": {"reset": args.reset, "end": end.isoformat(),
                            "window_hours": round(hours, 3), "source": str(logs)},
                   "kpis": kpis}, open(args.save_baseline, "w"), indent=1)
        print(f"saved baseline -> {args.save_baseline}", file=sys.stderr)

    base = {}
    if args.baseline:
        base = json.load(open(args.baseline)).get("kpis", {})

    L = ["| KPI | This run | Baseline | Δ / source |",
         "|---|---|---|---|"]
    for k, v, n in rows:
        b = base.get(k, {}).get("value", "—") if base else "—"
        d = "—"
        if base and k in base:
            d = "≈" if base[k]["value"] == v else "CHANGED"
        L.append(f"| {k} | {v} | {b} | {d}; {n} |")
    md = f"### Pool KPIs since reset {args.reset} UTC ({hours:.2f} h window)\n\n" + "\n".join(L) + "\n"
    if args.out:
        Path(args.out).write_text(md)
    else:
        print(md)


def _vendor_pick_bots(path, reset):
    """Bots with >=1 Vendor TravelTarget pick (first picks have no gap)."""
    seen = set()
    with open(path, newline="", encoding="utf-8", errors="replace") as f:
        for row in csv.reader(f):
            if len(row) <= C_INFO2:
                continue
            if row[C_EVENT].strip() != "TravelTarget" or row[C_INFO1].strip() != "Vendor":
                continue
            try:
                if parse_ts(row[C_TS]) >= reset:
                    seen.add(row[C_BOT])
            except ValueError:
                continue
    return seen


if __name__ == "__main__":
    main()
