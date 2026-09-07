#!/usr/bin/env python3
"""
M1 decision-trail contract checker.

Validates BotActionLog decision-trail grammar emitted by Engine::LogAction
(TICK / TRIGGER_REASON / PUSH / ACTION) and PlayerbotAI::CastSpell
(CAST_START / CAST_OK / CAST_FAIL / CAST_GATE), then reports the four M1
failure signatures:

  1. unknown actions      -- A:<name> - UNKNOWN (unregistered/ambiguous)
  2. suppression          -- USELESS / IMPOSSIBLE / MULT-zero storms
  3. failed preparation   -- CAST_FAIL phase=PREPARE* or CAST_GATE reasons
  4. stale-target retries -- same action FAILED 3+ times with no OK between

Usage:
    python3 check_decision_trail.py <botlog> [--strict] [--max-stale 3]
    python3 check_decision_trail.py --self-test   # runs built-in fixtures

Exit codes: 0 = log parses, findings printed; 1 = usage/IO error;
2 = malformed trail line (emitter contract broken) or --strict finding.

A log label alone never closes a gameplay gate; this tool only proves the
trail is machine-readable and surfaces candidate defects for triage.
"""

import re
import sys
from collections import Counter, defaultdict

LINE_RE = re.compile(r"^\[(?P<ts>[^\]]+)\] \[(?P<tag>[^\]]+)\] (?P<body>.*)$")

TICK_RE = re.compile(r"^--- AI Tick --- state=(?P<state>\S+) strats=(?P<strats>.*)$")
TRIGGER_RE = re.compile(r"^T:(?P<trigger>.+?) src=(?P<src>.*)$")
PUSH_RE = re.compile(r"^PUSH:(?P<action>.+?) - (?P<rel>-?[\d.]+) \((?P<kind>\w+)\) src=(?P<src>.*)$")
ACTION_RE = re.compile(
    r"^A:(?P<action>.+?) - (?P<outcome>UNKNOWN|PREREQ|OK|FAILED|IMPOSSIBLE|USELESS)"
    r"(?: src=(?P<src>.*?))?"
    r"(?: base=(?P<base>-?[\d.]+))?"
    r"(?: eff=(?P<eff>-?[\d.]+))?$"
)
MULT_RE = re.compile(
    r"^A:(?P<action>.+) - MULT (?P<mult>.+) x(?P<factor>-?[\d.]+) "
    r"\((?P<before>-?[\d.]+)->(?P<after>-?[\d.]+)\)$"
)
USELESS_RE = re.compile(r"^Multiplier (?P<mult>.+) made action (?P<action>.+?) useless$")
CAST_START_RE = re.compile(
    r"^spell=(?P<spell>.+)\((?P<id>\d+)\) targetGuid=(?P<guid>\S+) castTime=(?P<ms>\d+)ms$"
)
CAST_RESULT_RE = re.compile(
    r"^spell=(?P<spell>.+)\((?P<id>\d+)\) result=(?P<result>\d+) phase=(?P<phase>\S+)$"
)
CAST_GATE_RE = re.compile(
    r"^spell=(?P<id>\d+) targetGuid=(?P<guid>\S+) reason=(?P<reason>\S+)$"
)

# Tags the emitter may write that carry no decision content; accepted as-is.
PASS_THROUGH_TAGS = {
    "STATE", "AURA_DUMP", "AURA_APPLY", "AURA_REMOVE", "AURA_ATTEMPT",
    "DMG_DEALT", "DMG_TAKEN", "ENGINE", "NO_ACTION", "TICK",
}

OUTCOMES = ("UNKNOWN", "PREREQ", "OK", "FAILED", "IMPOSSIBLE", "USELESS")


def check_lines(lines):
    """Returns (malformed, stats, findings). malformed is a list of (lineno, line)."""
    malformed = []
    findings = []
    outcomes = Counter()
    unknowns = Counter()
    mults = Counter()
    useless_mults = Counter()
    cast_fails = Counter()
    cast_gates = Counter()
    coord_rejected = 0
    # stale retry: consecutive FAILED per action with no OK between
    failed_streak = defaultdict(int)

    for n, raw in enumerate(lines, 1):
        line = raw.rstrip("\n")
        if not line.strip():
            continue
        m = LINE_RE.match(line)
        if not m:
            malformed.append((n, line))
            continue
        tag, body = m.group("tag"), m.group("body")
        if tag == "TICK":
            if not TICK_RE.match(body):
                malformed.append((n, line))
        elif tag == "TRIGGER_REASON":
            if not TRIGGER_RE.match(body):
                malformed.append((n, line))
        elif tag == "PUSH":
            if not PUSH_RE.match(body):
                malformed.append((n, line))
        elif tag == "ACTION":
            am = ACTION_RE.match(body)
            mm = MULT_RE.match(body) if not am else None
            um = USELESS_RE.match(body) if (not am and not mm) else None
            if am:
                out = am.group("outcome")
                outcomes[out] += 1
                if out == "UNKNOWN":
                    unknowns[am.group("action")] += 1
                    findings.append((n, "unknown-action", am.group("action")))
                if out == "FAILED":
                    failed_streak[am.group("action")] += 1
                    if failed_streak[am.group("action")] == 3:
                        findings.append((n, "stale-retry?", am.group("action")))
                elif out == "OK":
                    failed_streak[am.group("action")] = 0
            elif mm:
                mults[(mm.group("action"), mm.group("mult"))] += 1
            elif um:
                useless_mults[(um.group("action"), um.group("mult"))] += 1
                findings.append((n, "multiplier-zero", "%s by %s" % (um.group("action"), um.group("mult"))))
            else:
                malformed.append((n, line))
        elif tag in ("CAST_OK", "CAST_FAIL"):
            cm = CAST_RESULT_RE.match(body)
            if not cm:
                malformed.append((n, line))
            elif tag == "CAST_FAIL":
                cast_fails[(cm.group("id"), cm.group("phase"))] += 1
                findings.append((n, "cast-fail", "%s phase=%s" % (cm.group("id"), cm.group("phase"))))
                if cm.group("phase") == "PREPARE-coord":
                    coord_rejected += 1
        elif tag == "CAST_START":
            if not CAST_START_RE.match(body):
                malformed.append((n, line))
        elif tag == "CAST_GATE":
            gm = CAST_GATE_RE.match(body)
            if not gm:
                malformed.append((n, line))
            else:
                cast_gates[gm.group("reason")] += 1
                findings.append((n, "cast-gate", "%s reason=%s" % (gm.group("id"), gm.group("reason"))))
        elif tag in PASS_THROUGH_TAGS:
            pass
        else:
            malformed.append((n, line))

    stats = {
        "outcomes": dict(outcomes),
        "unknowns": dict(unknowns),
        "multiplier_factors": len(mults),
        "multiplier_zeroes": dict(useless_mults),
        "cast_fails": dict(("%s/%s" % k, v) for k, v in cast_fails.items()),
        "cast_gates": dict(cast_gates),
        "coord_rejected": coord_rejected,
    }
    return malformed, stats, findings


def self_test():
    fixture = """\
[2026-09-07 12:00:00.000] [TICK] --- AI Tick --- state=combat strats=battle|dps|
[2026-09-07 12:00:00.010] [TRIGGER_REASON] T:enemy too close for spell src=combat
[2026-09-07 12:00:00.011] [PUSH] PUSH:fireball - 45.000000 (trigger) src=combat
[2026-09-07 12:00:00.012] [ACTION] A:fireball - MULT conserve mana x0.500 (45.000->22.500)
[2026-09-07 12:00:00.013] [ACTION] A:fireball - OK src=combat base=45.000 eff=22.500
[2026-09-07 12:00:00.014] [CAST_START] spell=Fireball(133) targetGuid=0x1234 castTime=3000ms
[2026-09-07 12:00:00.015] [CAST_OK] spell=Fireball(133) result=0 phase=PREPARE
[2026-09-07 12:00:01.000] [TICK] --- AI Tick --- state=combat strats=battle|dps|
[2026-09-07 12:00:01.010] [ACTION] A:frobnicate - UNKNOWN src=combat base=10.000
[2026-09-07 12:00:01.011] [ACTION] A:heal - FAILED src=combat base=80.000 eff=80.000
[2026-09-07 12:00:01.012] [ACTION] A:heal - FAILED src=combat base=80.000 eff=80.000
[2026-09-07 12:00:01.013] [ACTION] A:heal - FAILED src=combat base=80.000 eff=80.000
[2026-09-07 12:00:01.014] [ACTION] Multiplier threat made action heal useless
[2026-09-07 12:00:01.015] [CAST_FAIL] spell=Heal(2050) result=1 phase=PREPARE
[2026-09-07 12:00:01.016] [CAST_GATE] spell=133 targetGuid=0x1234 reason=moving-no-master
[2026-09-07 12:00:01.017] [CAST_FAIL] spell=Blizzard(10) result=2 phase=PREPARE-coord
[2026-09-07 12:00:01.018] [STATE] reason=tick hp=100% mp=50% combat=1 alive=1 level=60 pos=0.0,0.0,0.0,0.0 map=0 zone=0 target=Foo targetGuid=0x1234
""".splitlines()
    malformed, stats, findings = check_lines(fixture)
    assert not malformed, malformed
    assert stats["outcomes"] == {"OK": 1, "UNKNOWN": 1, "FAILED": 3}, stats["outcomes"]
    assert stats["unknowns"] == {"frobnicate": 1}, stats["unknowns"]
    assert stats["multiplier_factors"] == 1, stats
    assert stats["cast_gates"] == {"moving-no-master": 1}, stats["cast_gates"]
    assert stats["coord_rejected"] == 1, stats
    kinds = {k for _, k, _ in findings}
    assert {"unknown-action", "stale-retry?", "multiplier-zero", "cast-fail", "cast-gate"} <= kinds, kinds
    bad = ["no brackets here", "[2026-09-07] [ACTION] A:heal - SORTA src=x"]
    malformed2, _, _ = check_lines(bad)
    assert len(malformed2) == 2, malformed2

    # R1 regression check: event sources with spaces and all outcome states
    spaced_fixture = [
        "[2026-09-07 12:00:00.010] [TRIGGER_REASON] T:enemy too close for spell src=enemy too close for spell",
        "[2026-09-07 12:00:00.011] [PUSH] PUSH:frost nova - 70.000000 (trigger) src=enemy too close for spell",
        "[2026-09-07 12:00:00.013] [ACTION] A:fireball - OK src=enemy too close for spell base=45.000 eff=45.000",
        "[2026-09-07 12:00:00.014] [ACTION] A:greater heal - PREREQ src=party member low health base=80.000 eff=80.000",
        "[2026-09-07 12:00:00.015] [ACTION] A:curse of agony - IMPOSSIBLE src=target immune base=20.000 eff=0.000",
        "[2026-09-07 12:00:00.016] [ACTION] A:shoot - USELESS src=out of ammo base=15.000 eff=0.000",
        "[2026-09-07 12:00:00.017] [ACTION] A:frostbolt - UNKNOWN src=some strange trigger base=12.340",
    ]
    malformed3, stats3, findings3 = check_lines(spaced_fixture)
    assert not malformed3, malformed3
    assert stats3["outcomes"] == {"OK": 1, "PREREQ": 1, "IMPOSSIBLE": 1, "USELESS": 1, "UNKNOWN": 1}, stats3["outcomes"]

    # Direct field capture checks
    tm = TRIGGER_RE.match("T:enemy too close for spell src=enemy too close for spell")
    assert tm and tm.group("trigger") == "enemy too close for spell" and tm.group("src") == "enemy too close for spell"
    pm = PUSH_RE.match("PUSH:frost nova - 70.000000 (trigger) src=enemy too close for spell")
    assert pm and pm.group("action") == "frost nova" and pm.group("src") == "enemy too close for spell"
    am = ACTION_RE.match("A:fireball - OK src=enemy too close for spell base=45.000 eff=45.000")
    assert am and am.group("action") == "fireball" and am.group("src") == "enemy too close for spell"
    assert float(am.group("base")) == 45.0 and float(am.group("eff")) == 45.0

    print("self-test: all assertions passed (grammar + 4 signatures + malformed detection + R1 spaced sources)")


def main(argv):
    if "--self-test" in argv:
        self_test()
        return 0
    args = [a for a in argv if not a.startswith("--")]
    strict = "--strict" in argv
    if not args:
        print(__doc__.splitlines()[1])
        print("usage: check_decision_trail.py <botlog> [--strict] | --self-test")
        return 1
    try:
        with open(args[0], "r", encoding="utf-8", errors="replace") as f:
            lines = f.readlines()
    except OSError as e:
        print("cannot read %s: %s" % (args[0], e))
        return 1
    malformed, stats, findings = check_lines(lines)
    print("lines=%d outcomes=%s" % (len(lines), stats["outcomes"]))
    print("unknowns=%s multiplier_factors=%d multiplier_zeroes=%s" % (
        stats["unknowns"], stats["multiplier_factors"], stats["multiplier_zeroes"]))
    print("cast_fails=%s cast_gates=%s coord_rejected=%d" % (
        stats["cast_fails"], stats["cast_gates"], stats["coord_rejected"]))
    for n, kind, detail in findings[:50]:
        print("line %d [%s] %s" % (n, kind, detail))
    if len(findings) > 50:
        print("... %d more findings" % (len(findings) - 50))
    if malformed:
        print("MALFORMED (%d):" % len(malformed))
        for n, line in malformed[:20]:
            print("line %d: %s" % (n, line[:160]))
        return 2
    if strict and findings:
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
