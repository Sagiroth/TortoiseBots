#!/usr/bin/env python3
"""Verify every queued trigger/action name resolves to a creator.

Collects TriggerNode("t") and NextAction("a") names across the strategy tree
and checks triggers against trigger-factory creators and actions against
action/node-factory creators (namespaces tracked separately: a same-named
trigger creator does NOT satisfy an action reference). Also verifies
ACTION_NODE_X primaries resolve as actions. Reports unresolvable names —
silent no-ops at runtime (cf. the unregistered CancelChannelAction found in
batch 5, and the CombatBoost non-combat miswiring in batch 7).

Known non-creator names (strategy switches, engine pseudo-actions, dynamic
say::/rti names) live in SKIP. Audited tech-debt files live in DEAD_FILES.

Usage: python3 tools/verify_action_trigger_wiring.py [--strict]
Exit 1 on any unclassified missing name (always); --strict also fails on SKIP hits.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

SKIP = {
    # Engine/strategy pseudo-targets, not AiObject creators:
    'melee', 'shoot', 'stay', 'follow', 'flee', 'move to loot', 'open loot',
    'tank assist', 'dps assist', 'dps aoe', 'attack rti target',
    # Dynamic names (say::X, rti ..., position helpers resolved elsewhere):
    'say::no ammo',
    # String-concat fragments ("greater " + name built at runtime in
    # PaladinStrategy/PaladinActions; the full names have creators):
    'greater',
}

# Audited unresolvable refs (live files, kept deliberately; re-audit before
# wiring anything new to these names):
KNOWN = {
    # Continuer of the "move random" node; "move random" itself is never
    # queued, so this is unreachable, not a runtime hole.
    'stay line': 'unreachable continuer',
    # PossibleAdsStrategy is never added to any engine; its single node
    # never evaluates. Delete the class if strategies are ever cleaned up.
    'possible ads': 'strategy never added',
}

# Known-dead strategy files (exact basenames) whose queued names are expected
# to miss: the vector-style Generic* trees plus the unregistered NEW
# Bear/Cat/Resto/Dps stacks (see PROGRESS.md). Misses there are tech debt, not
# live bugs; the audit value is in misses from legacy list-engine files.
# NOTE: TotemsShamanStrategy.cpp is vector-style (dead); the live per-spec
# *TotemsStrategy blocks inside Enhancement/Elemental/ShamanStrategy.cpp use
# registered "X totem" names and are unaffected.
DEAD_FILES = (
    'GenericDruidStrategy.cpp', 'GenericDruidNonCombatStrategy.cpp',
    'GenericHunterStrategy.cpp', 'GenericHunterNonCombatStrategy.cpp',
    'GenericMageStrategy.cpp', 'GenericMageNonCombatStrategy.cpp',
    'GenericPaladinStrategy.cpp', 'GenericPaladinNonCombatStrategy.cpp',
    'GenericPriestStrategy.cpp', 'GenericShamanStrategy.cpp',
    'GenericRogueNonCombatStrategy.cpp', 'GenericWarriorStrategy.cpp',
    'GenericWarriorNonCombatStrategy.cpp', 'GenericWarlockStrategy.cpp',
    'GenericWarlockNonCombatStrategy.cpp', 'BearDruidStrategy.cpp',
    'CatDruidStrategy.cpp', 'RestoDruidStrategy.cpp', 'DpsRogueStrategy.cpp',
    'RestoShamanStrategy.cpp', 'HealPriestStrategy.cpp',
    'TankPaladinStrategy.cpp', 'LevelingDruidStrategy.cpp',
    'MoltenCoreDungeonStrategies.cpp', 'BattlegroundStrategy.cpp',
    'TravelStrategy.cpp', 'RpgStrategy.cpp', 'MeleeCombatStrategy.cpp',
    'SayStrategy.cpp', 'TotemsShamanStrategy.cpp',
    'ShamanNonCombatStrategy.cpp', 'DpsPaladinStrategy.cpp',
    'OffhealRetPaladinStrategy.cpp', 'TankPaladinStrategy.cpp',
    'HealPaladinStrategy.cpp', 'TankWarriorStrategy.cpp',
    'PriestNonCombatStrategy.cpp',
)

def is_dead(ref):
    base = ref.rsplit('/', 1)[-1]
    return base in DEAD_FILES



TRIGGERS = re.compile(r'TriggerNode\(\s*"([^"]+)"')
ACTIONS = re.compile(r'NextAction\(\s*"([^"]+)"')
CREATORS = re.compile(r'creators\["([^"]+)"\]')
# ACTION_NODE_X(func, "action", "alt/continue"): the node factory entry for
# key K = &func wraps primary action A with fallback B. A must resolve as an
# action creator; B may resolve as action or node (union). Catches the
# batch-11 "chain heal" class: node existed, inner action creator deleted.
NODEMACRO = re.compile(r'ACTION_NODE_\w+\(\s*(\w+)\s*,\s*"([^"]+)"\s*,\s*"([^"]+)"')
KEYFUNC = re.compile(r'creators\["([^"]+)"\]\s*=\s*&(\w+)')


def base(name):
    # The engine splits "name::qualifier" (NamedObjectFactory::Create);
    # strip qualifiers before lookup.
    return name.split('::')[0].strip()


def main():
    strict = '--strict' in sys.argv
    queued = {}
    for path in sorted((ROOT / 'ai').rglob('*.cpp')) + sorted((ROOT / 'ai').rglob('*.h')):
        try:
            text = path.read_text(encoding='utf-8', errors='replace')
        except OSError:
            continue
        rel = str(path.relative_to(ROOT))
        for m in TRIGGERS.finditer(text):
            queued.setdefault(('trigger', base(m.group(1))), set()).add(rel)
        for m in ACTIONS.finditer(text):
            queued.setdefault(('action', base(m.group(1))), set()).add(rel)
    trig_creators = set()
    act_creators = set()
    for path in sorted((ROOT / 'ai').rglob('*.cpp')) + sorted((ROOT / 'ai').rglob('*.h')):
        try:
            text = path.read_text(encoding='utf-8', errors='replace')
        except OSError:
            continue
        # Attribute each creators[] entry to the enclosing factory class:
        # *Trigger* factories provide triggers, action/node factories (and
        # strategy-local ActionNode factories) provide actions. A name queued
        # as NextAction must resolve in the action bucket specifically; a
        # same-named trigger creator does NOT satisfy it (cf. batch-19
        # "pummel on enemy healer" action-creator deletion).
        current = None
        for line in text.splitlines():
            cm = re.match(r'\s*class\s+(\w+)', line)
            if cm:
                current = cm.group(1)
            for name in CREATORS.findall(line):
                if current and 'Trigger' in current:
                    trig_creators.add(name)
                else:
                    act_creators.add(name)
    creators = trig_creators | act_creators
    def unresolved(kind, name):
        if kind == 'trigger':
            return name not in trig_creators
        return name not in act_creators
    missing = {k: v for k, v in sorted(queued.items()) if unresolved(*k)}
    missing_real = {k: v for k, v in missing.items() if k[1] not in SKIP}
    skipped = {k: v for k, v in missing.items() if k[1] in SKIP}
    live = {k: v for k, v in missing_real.items()
            if not all(is_dead(f) for f in v) and k[1] not in KNOWN}
    dead = {k: v for k, v in missing_real.items() if k not in live and k[1] not in KNOWN}
    known = {k: v for k, v in missing_real.items() if k[1] in KNOWN}
    # Node inner-action check: every ActionNode factory entry key K = &func
    # with ACTION_NODE_X(func, "A", "B") needs A in the action bucket.
    # (B may be an action or another node.) Restrict to live files so dead
    # trees do not fail the gate.
    node_broken = {}
    for path in sorted((ROOT / 'ai').rglob('*.cpp')) + sorted((ROOT / 'ai').rglob('*.h')):
        rel = str(path.relative_to(ROOT))
        if is_dead(rel):
            continue
        try:
            text = path.read_text(encoding='utf-8', errors='replace')
        except OSError:
            continue
        funcs = {}
        for m in NODEMACRO.finditer(text):
            funcs[m.group(1)] = (base(m.group(2)), base(m.group(3)))
        for m in KEYFUNC.finditer(text):
            key, func = base(m.group(1)), m.group(2)
            if func in funcs:
                primary, _alt = funcs[func]
                if primary not in act_creators:
                    node_broken.setdefault((key, primary), set()).add(rel)
    print(f'queued={len(queued)} creators={len(creators)} live-missing={len(live)} '
          f'dead-tree={len(dead)} known={len(known)} skipped={len(skipped)} '
          f'node-broken={len(node_broken)}')
    for (kind, name), files in live.items():
        print(f'LIVE-MISSING {kind} "{name}" <- {sorted(files)[0]} (+{len(files) - 1} more)')
    for (kind, name), files in dead.items():
        print(f'dead-tree {kind} "{name}" ({len(files)} refs)')
    for (kind, name), files in known.items():
        print(f'known {kind} "{name}": {KNOWN[name]}')
    if skipped:
        for (kind, name), files in sorted(skipped.items()):
            print(f'skip {kind} "{name}" ({len(files)} refs)')
    if live or (strict and skipped):
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
