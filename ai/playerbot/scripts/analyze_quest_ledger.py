#!/usr/bin/env python3
"""
Reads bot_events.csv and deaths.csv produced by the playerbot logging system
and outputs a ranked table of quests broken down by failure mode.

Usage:
    python3 analyze_quest_ledger.py
    python3 analyze_quest_ledger.py --min-attempts 10
    python3 analyze_quest_ledger.py --detail 788
    python3 analyze_quest_ledger.py --events /path/to/bot_events.csv --deaths /path/to/deaths.csv
    python3 analyze_quest_ledger.py --events server1.csv --deaths server1_deaths.csv \
        --events2 baseline.csv --deaths2 baseline_deaths.csv \
        --label1 tortoise --label2 baseline
"""

import argparse
import csv
import sys
from collections import defaultdict
from dataclasses import dataclass, field

EVENTS_DEFAULT = "/home/ubuntu/cmake-build-debug/src/logs/bot_events.csv"
DEATHS_DEFAULT = "/home/ubuntu/cmake-build-debug/src/logs/deaths.csv"

BLACKLIST_RATE   = 0.25
INVESTIGATE_RATE = 0.60
BLACKLIST_DEATHS = 10

# column indices in bot_events.csv
COL_TIMESTAMP = 0
COL_BOT       = 1
COL_EVENT     = 2
COL_POS       = 3
COL_RACE      = 4
COL_CLASS     = 5
COL_LEVEL     = 6
COL_INFO1     = 7
COL_INFO2     = 8

# appended column in deaths.csv
DEATH_COL_TRAVEL = 11


@dataclass
class BotStats:
    accepted: int = 0
    turned_in: int = 0
    dropped: int = 0
    deaths: int = 0
    max_level: int = 0
    unique_quests_accepted: set = field(default_factory=set)
    unique_quests_completed: set = field(default_factory=set)


@dataclass
class QuestStats:
    name: str = ""
    accepted: int = 0
    # kill objective progress tracking
    kill_progress_starts: int = 0   # bots that made at least 1 kill event
    kill_progress_full: int = 0     # bots that reached ratio 1.0 (all kills done)
    objectives_done: int = 0        # QuestUpdateCompleteAction count
    turned_in: int = 0
    dropped: int = 0
    deaths: int = 0
    # per-bot timeline for --detail mode
    bot_events: list = field(default_factory=list)
    # track which bots have started kill progress (to count unique)
    _kill_started_bots: set = field(default_factory=set)
    _kill_completed_bots: set = field(default_factory=set)


def parse_row(row: list[str]) -> tuple[str, ...]:
    """Strip whitespace and quotes from a CSV row."""
    return tuple(f.strip().strip('"') for f in row)


def parse_events(path: str, detail_id: str | None) -> tuple[dict[str, QuestStats], dict[str, BotStats]]:
    quests: dict[str, QuestStats] = defaultdict(QuestStats)
    bots: dict[str, BotStats] = defaultdict(BotStats)
    try:
        with open(path, newline="", encoding="utf-8", errors="replace") as f:
            for raw in csv.reader(f):
                if len(raw) <= COL_INFO2:
                    continue
                row = parse_row(raw)
                event = row[COL_EVENT]
                info1 = row[COL_INFO1]
                info2 = row[COL_INFO2]
                bot   = row[COL_BOT]
                ts    = row[COL_TIMESTAMP]
                level = row[COL_LEVEL]

                try:
                    lvl_int = int(float(level))
                    if lvl_int > bots[bot].max_level:
                        bots[bot].max_level = lvl_int
                except (ValueError, TypeError):
                    pass

                if event == "AcceptQuestAction":
                    qid, name = info2, info1
                    quests[qid].name = name
                    quests[qid].accepted += 1
                    bots[bot].accepted += 1
                    bots[bot].unique_quests_accepted.add(qid)
                    if detail_id and qid == detail_id:
                        quests[qid].bot_events.append((ts, bot, level, "ACCEPTED", ""))

                elif event == "QuestUpdateAddKillAction":
                    qid = info1
                    try:
                        ratio = float(info2)
                    except ValueError:
                        continue
                    if bot not in quests[qid]._kill_started_bots:
                        quests[qid]._kill_started_bots.add(bot)
                        quests[qid].kill_progress_starts += 1
                    if ratio >= 1.0 and bot not in quests[qid]._kill_completed_bots:
                        quests[qid]._kill_completed_bots.add(bot)
                        quests[qid].kill_progress_full += 1
                    if detail_id and qid == detail_id:
                        quests[qid].bot_events.append((ts, bot, level, "KILL_PROGRESS", f"{ratio:.0%}"))

                elif event == "QuestUpdateCompleteAction":
                    qid = info1
                    quests[qid].objectives_done += 1
                    if detail_id and qid == detail_id:
                        quests[qid].bot_events.append((ts, bot, level, "OBJECTIVES_DONE", ""))

                elif event == "TalkToQuestGiverAction":
                    qid, name = info2, info1
                    quests[qid].name = name
                    quests[qid].turned_in += 1
                    bots[bot].turned_in += 1
                    bots[bot].unique_quests_completed.add(qid)
                    if detail_id and qid == detail_id:
                        quests[qid].bot_events.append((ts, bot, level, "TURNED_IN", ""))

                elif event == "QuestDropped":
                    qid, name = info2, info1
                    if not quests[qid].name:
                        quests[qid].name = name
                    quests[qid].dropped += 1
                    bots[bot].dropped += 1
                    if detail_id and qid == detail_id:
                        quests[qid].bot_events.append((ts, bot, level, "DROPPED", ""))

                elif event in ("QuestTravelToGiver", "QuestTravelToObjective", "QuestTravelToTaker"):
                    qid = info2
                    if not quests[qid].name:
                        quests[qid].name = info1
                    if detail_id and qid == detail_id:
                        label = event.replace("QuestTravel", "TRAVEL_")
                        quests[qid].bot_events.append((ts, bot, level, label, ""))

    except FileNotFoundError:
        print(f"[warn] events file not found: {path}", file=sys.stderr)
    return quests, bots


def parse_deaths(path: str, quests: dict[str, QuestStats], bots: dict[str, BotStats]) -> None:
    seen: set[tuple[str, str, str]] = set()
    try:
        with open(path, newline="", encoding="utf-8", errors="replace") as f:
            for raw in csv.reader(f):
                if len(raw) < 2:
                    continue
                ts       = raw[0].strip()
                bot_name = raw[1].strip().strip('"')
                level    = raw[4].strip() if len(raw) > 4 else ""
                key = (ts, bot_name, level)
                if key in seen:
                    continue
                seen.add(key)
                bots[bot_name].deaths += 1
                if len(raw) <= DEATH_COL_TRAVEL:
                    continue
                travel_title = raw[DEATH_COL_TRAVEL].strip().strip('"')
                for qid, stats in quests.items():
                    if stats.name and stats.name in travel_title:
                        stats.deaths += 1
                        break
    except FileNotFoundError:
        print(f"[warn] deaths file not found: {path}", file=sys.stderr)


def print_general_stats(quests: dict[str, QuestStats], bots: dict[str, BotStats]) -> None:
    total_accepted       = sum(s.accepted  for s in quests.values())
    total_turned_in      = sum(s.turned_in for s in quests.values())
    total_dropped        = sum(s.dropped   for s in quests.values())
    total_deaths         = sum(b.deaths    for b in bots.values())
    unique_quests        = len(quests)
    unique_quests_done   = sum(1 for s in quests.values() if s.turned_in > 0)
    unique_bots          = len(bots)
    max_bot_level        = max((b.max_level for b in bots.values()), default=0)
    overall_rate         = total_turned_in / total_accepted if total_accepted else 0

    print("=" * 60)
    print("GENERAL STATS")
    print("=" * 60)
    print(f"  Bots active          : {unique_bots}")
    print(f"  Highest bot level    : {max_bot_level}")
    print(f"  Unique quests tried  : {unique_quests}")
    print(f"  Unique quests done   : {unique_quests_done}")
    print(f"  Total accepted       : {total_accepted}")
    print(f"  Total turned in      : {total_turned_in}")
    print(f"  Total dropped        : {total_dropped}")
    print(f"  Total deaths         : {total_deaths}")
    print(f"  Overall quest rate   : {overall_rate:.1%}")
    print()

    def top(bots_dict, key, label, fmt=str, n=5):
        ranked = sorted(bots_dict.items(), key=lambda x: key(x[1]), reverse=True)
        ranked = [(name, b) for name, b in ranked if key(b) > 0][:n]
        if not ranked:
            return
        print(f"  Top {label}:")
        for name, b in ranked:
            print(f"    {name:<20} {fmt(key(b))}")
        print()

    top(bots, lambda b: b.turned_in,
        "completers (quests turned in)",
        fmt=lambda v: f"{v} quests")

    top(bots, lambda b: b.accepted,
        "grinders (quests accepted)",
        fmt=lambda v: f"{v} accepts")

    top(bots, lambda b: len(b.unique_quests_completed),
        "variety (unique quests completed)",
        fmt=lambda v: f"{v} unique")

    top(bots, lambda b: b.dropped,
        "droppers (quests dropped)",
        fmt=lambda v: f"{v} drops")

    top(bots, lambda b: b.deaths,
        "casualties (deaths)",
        fmt=lambda v: f"{v} deaths")

    print("=" * 60)
    print()


def failure_mode(s: QuestStats) -> str:
    """Classify the primary failure mode."""
    if s.accepted == 0:
        return "NO_DATA"
    rate = s.turned_in / s.accepted
    if rate >= INVESTIGATE_RATE:
        return "OK"
    # Did bots even start killing?
    if s.kill_progress_starts == 0:
        return "NOT_STARTED"      # accepted, never killed anything for the quest
    if s.kill_progress_full == 0:
        return "STUCK_MID"        # some kills but nobody finished objectives
    # Some bots finished objectives but not many turned in
    return "NO_TURNIN"            # objectives done but turn-in failing


def verdict(s: QuestStats) -> str:
    rate = s.turned_in / s.accepted if s.accepted else 0
    if rate < BLACKLIST_RATE or s.deaths >= BLACKLIST_DEATHS:
        return "BLACKLIST"
    if rate < INVESTIGATE_RATE:
        return "INVESTIGATE"
    return "OK"


def print_summary(rows: list, min_attempts: int) -> None:
    total_quests = len(rows)
    rows = [(qid, s) for qid, s in rows if s.accepted >= min_attempts]
    hidden = total_quests - len(rows)
    if not rows:
        print(f"No quests with >= {min_attempts} attempts found.")
        return

    rows.sort(key=lambda x: (x[1].turned_in / x[1].accepted if x[1].accepted else 0, -x[1].deaths))

    name_w = max((len(s.name) for _, s in rows), default=22)
    name_w = max(name_w, 22)

    header = (
        f"{'Quest ID':>8} | {'Quest Name':<{name_w}} | "
        f"{'Accept':>6} | {'Kill%':>5} | {'ObjDone':>7} | {'TurnIn':>6} | "
        f"{'Dropped':>7} | {'Rate':>6} | {'Deaths':>6} | {'Failure Mode':<12} | Verdict"
    )
    sep = "-" * len(header)
    print(header)
    print(sep)

    for qid, s in rows:
        rate = s.turned_in / s.accepted if s.accepted else 0
        kill_pct = f"{s.kill_progress_starts / s.accepted:.0%}" if s.accepted else "  -"
        v = verdict(s)
        fm = failure_mode(s)
        print(
            f"{qid:>8} | {s.name:<{name_w}} | "
            f"{s.accepted:>6} | {kill_pct:>5} | {s.objectives_done:>7} | {s.turned_in:>6} | "
            f"{s.dropped:>7} | {rate:>5.1%} | {s.deaths:>6} | {fm:<12} | {v}"
        )

    print(sep)
    counts = {fm: sum(1 for _, s in rows if failure_mode(s) == fm) for fm in ("NOT_STARTED", "STUCK_MID", "NO_TURNIN", "OK")}
    print(f"{len(rows)} quests  |  NOT_STARTED: {counts['NOT_STARTED']}  |  STUCK_MID: {counts['STUCK_MID']}  |  NO_TURNIN: {counts['NO_TURNIN']}  |  OK: {counts['OK']}")
    print()
    print("Failure modes:")
    print("  NOT_STARTED  — accepted but zero kill progress: route/objective targeting issue or instant drop")
    print("  STUCK_MID    — some kills but objectives never finished: combat failure or mid-quest drop")
    print("  NO_TURNIN    — objectives completed but quest not turned in: travel/NPC targeting issue")
    print()
    print("BLACKLIST candidates → AcceptAllQuestsAction::ProcessQuest")
    print("  src/modules/PlayerBots/playerbot/strategy/actions/AcceptQuestAction.cpp")
    if hidden:
        print()
        print(f"  ({hidden} quest(s) with < {min_attempts} accepts hidden — rerun with --min-attempts 1 to see all)")


def print_detail(quest_id: str, quests: dict[str, QuestStats]) -> None:
    s = quests.get(quest_id)
    if not s:
        print(f"No data found for quest ID {quest_id}")
        return

    rate = s.turned_in / s.accepted if s.accepted else 0
    print(f"Quest {quest_id}: {s.name}")
    print(f"  Accepted={s.accepted}  Kill%={s.kill_progress_starts/s.accepted:.0%}  "
          f"KillFull={s.kill_progress_full}  ObjDone={s.objectives_done}  "
          f"TurnedIn={s.turned_in}  Dropped={s.dropped}  Rate={rate:.1%}  Deaths={s.deaths}")
    print(f"  Failure mode: {failure_mode(s)}")
    print()

    if not s.bot_events:
        print("  No detailed events (requires rebuild with QuestTravelTo* logging enabled)")
        return

    # Group by bot
    by_bot: dict[str, list] = defaultdict(list)
    for evt in s.bot_events:
        by_bot[evt[1]].append(evt)

    for bot, evts in sorted(by_bot.items()):
        evts.sort()
        outcome = "?"
        labels = [e[3] for e in evts]
        if "TURNED_IN" in labels:
            outcome = "✓ COMPLETE"
        elif "DROPPED" in labels:
            outcome = "✗ DROPPED"
        elif "KILL_PROGRESS" in labels:
            last_ratio = max((e[4] for e in evts if e[3] == "KILL_PROGRESS"), default="")
            outcome = f"~ IN_PROGRESS ({last_ratio})"
        elif "ACCEPTED" in labels:
            outcome = "? NO_PROGRESS"
        print(f"  {bot:<16} [{outcome}]")
        for ts, _, lvl, label, extra in evts:
            print(f"    {ts}  lv{lvl:<5}  {label}  {extra}")
        print()


def print_compare(
    quests1: dict[str, QuestStats], bots1: dict[str, BotStats], label1: str,
    quests2: dict[str, QuestStats], bots2: dict[str, BotStats], label2: str,
    min_attempts: int,
) -> None:
    # General stats header
    def overall_rate(q):
        acc = sum(s.accepted for s in q.values())
        tin = sum(s.turned_in for s in q.values())
        return tin / acc if acc else 0

    print("=" * 80)
    print(f"COMPARISON: {label1}  vs  {label2}")
    print("=" * 80)
    hdr = f"  {'Metric':<30} {label1:>14} {label2:>14}"
    print(hdr)
    print("-" * len(hdr))

    def row(label, v1, v2, fmt=str):
        print(f"  {label:<30} {fmt(v1):>14} {fmt(v2):>14}")

    row("Bots active", len(bots1), len(bots2))
    row("Unique quests tried", len(quests1), len(quests2))
    row("Total accepted", sum(s.accepted for s in quests1.values()),
        sum(s.accepted for s in quests2.values()))
    row("Total turned in", sum(s.turned_in for s in quests1.values()),
        sum(s.turned_in for s in quests2.values()))
    row("Total dropped", sum(s.dropped for s in quests1.values()),
        sum(s.dropped for s in quests2.values()))
    row("Total deaths", sum(b.deaths for b in bots1.values()),
        sum(b.deaths for b in bots2.values()))
    row("Overall quest rate", overall_rate(quests1), overall_rate(quests2),
        fmt=lambda v: f"{v:.1%}")
    print()

    # Merge quest IDs from both servers
    all_ids = sorted(set(quests1) | set(quests2))
    rows = []
    for qid in all_ids:
        s1 = quests1.get(qid, QuestStats())
        s2 = quests2.get(qid, QuestStats())
        name = s1.name or s2.name or ""
        acc1, acc2 = s1.accepted, s2.accepted
        if acc1 < min_attempts and acc2 < min_attempts:
            continue
        rate1 = s1.turned_in / acc1 if acc1 else None
        rate2 = s2.turned_in / acc2 if acc2 else None
        delta = (rate2 - rate1) if (rate1 is not None and rate2 is not None) else None
        rows.append((qid, name, acc1, rate1, s1.deaths, acc2, rate2, s2.deaths, delta))

    if not rows:
        print(f"No quests with >= {min_attempts} attempts in either server.")
        return

    # Sort: biggest regression first (baseline worse than ours last, baseline better first)
    rows.sort(key=lambda r: (r[8] is None, r[8] if r[8] is not None else 0))

    name_w = max((len(r[1]) for r in rows), default=22)
    name_w = max(name_w, 22)

    L1 = label1[:10]
    L2 = label2[:10]

    header = (
        f"{'ID':>8} | {'Quest Name':<{name_w}} | "
        f"{L1+' Acc':>10} | {L1+' Rate':>10} | {L1+' D':>6} | "
        f"{L2+' Acc':>10} | {L2+' Rate':>10} | {L2+' D':>6} | "
        f"{'Delta':>7}"
    )
    sep = "-" * len(header)
    print(header)
    print(sep)

    def fmtrate(r):
        return f"{r:.1%}" if r is not None else "   n/a"

    def fmtdelta(d):
        if d is None:
            return "    n/a"
        sign = "+" if d >= 0 else ""
        return f"{sign}{d:.1%}"

    for qid, name, acc1, rate1, d1, acc2, rate2, d2, delta in rows:
        print(
            f"{qid:>8} | {name:<{name_w}} | "
            f"{acc1:>10} | {fmtrate(rate1):>10} | {d1:>6} | "
            f"{acc2:>10} | {fmtrate(rate2):>10} | {d2:>6} | "
            f"{fmtdelta(delta):>7}"
        )

    print(sep)
    label1_better = sum(1 for r in rows if r[8] is not None and r[8] < -0.1)
    label2_better = sum(1 for r in rows if r[8] is not None and r[8] > 0.1)
    only_s1 = sum(1 for r in rows if r[5] < min_attempts)
    only_s2 = sum(1 for r in rows if r[2] < min_attempts)
    print(f"{len(rows)} quests compared  |  {label1} better (delta < -10%): {label1_better}  |  "
          f"{label2} better (delta > +10%): {label2_better}  |  "
          f"only in {label1}: {only_s1}  |  only in {label2}: {only_s2}")
    print()
    print(f"Delta = {label2}_rate - {label1}_rate  (positive = {label2} does better)")


def main() -> None:
    parser = argparse.ArgumentParser(description="Analyse bot quest ledger CSVs")
    parser.add_argument("--events", default=EVENTS_DEFAULT)
    parser.add_argument("--deaths", default=DEATHS_DEFAULT)
    parser.add_argument("--events2", default=None,
                        help="events file for second server (enables compare mode)")
    parser.add_argument("--deaths2", default=None,
                        help="deaths file for second server (enables compare mode)")
    parser.add_argument("--label1", default="server1",
                        help="label for first server in compare mode (default: server1)")
    parser.add_argument("--label2", default="baseline",
                        help="label for second server in compare mode (default: baseline)")
    parser.add_argument("--min-attempts", type=int, default=5, metavar="N",
                        help="Ignore quests with fewer than N accept events (default 5)")
    parser.add_argument("--detail", metavar="QUEST_ID",
                        help="Show per-bot event timeline for a specific quest ID")
    args = parser.parse_args()

    quests, bots = parse_events(args.events, args.detail)
    parse_deaths(args.deaths, quests, bots)

    if args.events2:
        quests2, bots2 = parse_events(args.events2, None)
        if args.deaths2:
            parse_deaths(args.deaths2, quests2, bots2)
        print_compare(quests, bots, args.label1, quests2, bots2, args.label2, args.min_attempts)
    elif args.detail:
        print_detail(args.detail, quests)
    else:
        print_general_stats(quests, bots)
        print_summary(list(quests.items()), args.min_attempts)


if __name__ == "__main__":
    main()
