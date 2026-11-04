#!/usr/bin/env python3
"""Select static hire-gear sets (issue #490, 29 specs x tiers 1..7).

Reads: data/bisclassic.json.gz, data/wowisclassic.json, data/turtle_sheets.json,
  data/item_tiers.csv (+ data/enchants_by_tier.json when a second worker
  produces it; enchant_spell stays 0 until then).
Writes: data/hire_gear.csv (review), ../../..//data/sql/world/<ts>_world.sql
  (DELETE + INSERT, idempotent), ../../../docs/reference/hire-gear.md.

Per slot: highest-ranked bisclassic candidate whose ladder tier <= target
and which is not rep/PvP/legendary/unobtainable; tiers 6-7 prefer the Turtle
sheet list item when its tier fits; else keep the previous tier's item; else
best by the spec's stat weights (server ai_playerbot_weightscales idea, see
WEIGHTS below) with flags bit0 set.

DB: env HIRE_GEAR_DB_* or CLI flags (read-only SELECTs for item stats,
weight scales, race/class validity).
"""
import argparse
import csv
import gzip
import json
import os
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
DATA = HERE / "data"


def load_snapshot(name, fetcher):
    """Site snapshots are not vendored: fail clearly before writing anything."""
    path = DATA / name
    if not path.exists():
        sys.exit(f"{path} not found: run {fetcher} first "
                 "(site snapshots are generated locally, not vendored)")
    return json.loads(path.read_text())

# (class id, hire spec key) in HireSpecOptions order; druid bear/cat distinct.
SPECS = [(1, "protection"), (1, "arms"), (1, "fury"),
         (2, "protection"), (2, "holy"), (2, "retribution"),
         (3, "beastmastery"), (3, "marksmanship"), (3, "survival"),
         (4, "assassination"), (4, "combat"), (4, "subtlety"),
         (5, "holy"), (5, "discipline"), (5, "shadow"),
         (7, "restoration"), (7, "elemental"), (7, "enhancement"), (7, "tank"),
         (8, "frost"), (8, "fire"), (8, "arcane"),
         (9, "affliction"), (9, "demonology"), (9, "destruction"),
         (11, "bear"), (11, "restoration"), (11, "balance"), (11, "cat")]

# hire spec -> (bisclassic class, spec, spec name) per Appendix D.
SOURCE = {"protection": (1, 2), "arms": (1, 1), "fury": (1, 1),
          "holy": (2, 1), "retribution": (2, 3),
          "beastmastery": (3, 1), "marksmanship": (3, 1), "survival": (3, 1),
          "assassination": (4, 2), "combat": (4, 1), "subtlety": (4, 2),
          "discipline": (5, 1), "shadow": (5, 2),
          "restoration": (7, 1), "elemental": (7, 1), "enhancement": (7, 2),
          "tank": (7, 2),
          "frost": (8, 1), "fire": (8, 1), "arcane": (8, 1),
          "affliction": (9, 1), "demonology": (9, 1), "destruction": (9, 1),
          "bear": (11, 1), "balance": (11, 6), "cat": (11, 4)}
# priest holy uses class-5 spec-1; paladin holy class-2 spec-1; warrior
# protection uses the Mitigation list (spec 3), not Threat (spec 2).
SOURCE_BY_CLASS = {(2, "holy"): (2, 1), (5, "holy"): (5, 1),
                   (5, "discipline"): (5, 1), (1, "protection"): (1, 3),
                   (11, "restoration"): (11, 5), (7, "restoration"): (7, 1),
                   (7, "tank"): (7, 2)}

# hire spec -> weight-scale name (ai_playerbot_weightscales). shamantank is
# the new shaman-tank scale (issue #1125); the DB row ships with its weight
# migration. The shaman tank shares the Enhancement melee BiS list (no
# tank list exists); the tank pins from the Turtle
# knowledge base (github.com/stevemcqueenz/turtle-knowledge-base-, KB) (HAND_OVERRIDES) carry the shields,
# weapons, relics and helm.
SCALES = {"protection": "prot", "arms": "arms", "fury": "fury",
          "holy": "holy", "retribution": "retrib",
          "beastmastery": "beast", "marksmanship": "marks", "survival": "surv",
          "assassination": "assas", "combat": "combat", "subtlety": "subtle",
          "discipline": "disc", "shadow": "shadow",
          "restoration": "resto", "elemental": "elem", "enhancement": "enhance",
          "tank": "shamantank",
          "frost": "frost", "fire": "fire", "arcane": "arcane",
          "affliction": "afflic", "demonology": "demo", "destruction": "destro",
          "bear": "feraltank", "balance": "balance", "cat": "feraldps"}

# hire (class id, spec) -> ordered Turtle tab names, primary first, later
# tabs are fallback for slots the primary leaves empty. Tabs deliberately
# NOT pooled per class: pooling mixed e.g. resto weapons onto bear/cat and
# 2H Fury rows onto arms/protection.
TURTLE_TABS = {(1, "protection"): ("FProt Threat BIS", "FProt Mit BIS"),
               (1, "arms"): ("Arms BIS",),
               (1, "fury"): ("Fury BIS", "Fury Sword BIS", "Fury Fist BIS"),
               (2, "protection"): ("Prot Threat BIS", "Prot Mit BIS"),
               (2, "holy"): ("Holy BIS",),
               (2, "retribution"): ("AP Ret BIS",),
               (3, "beastmastery"): ("BM BIS",),
               (3, "marksmanship"): ("Marks BIS",),
               (3, "survival"): ("Surv Axe BIS", "Surv BIS"),
               (4, "assassination"): ("Dagger BIS",),
               (4, "combat"): ("Combat BIS", "Sword BIS"),
               (4, "subtlety"): ("Sub BIS",),
               (5, "holy"): ("Holy BIS", "Holy Throughput BIS",
                             "Holy Sustain BIS"),
               (5, "discipline"): ("Holy BIS", "Holy Sustain BIS",
                                   "Holy Throughput BIS"),
               (5, "shadow"): ("Shadow BIS",),
               (7, "restoration"): ("Resto BIS",),
               (7, "elemental"): ("Elemental Lightning BIS", "Elemental BIS"),
               (7, "enhancement"): ("Enhance BIS",),
               # Shaman tank has no Turtle tank tab: the Enhance tab fills the
               # melee slots, HAND_OVERRIDES pin the tank pieces.
               (7, "tank"): ("Enhance BIS",),
               (8, "frost"): ("Frost BIS",),
               (8, "fire"): ("Fire BIS",),
               (8, "arcane"): ("Arcane BIS",),
               (9, "affliction"): ("Affliction BIS",),
               (9, "demonology"): ("Demo BIS",),
               (9, "destruction"): ("SM Ruin BIS",),
               (11, "bear"): ("Bear Threat BIS", "Bear Mit BIS"),
               (11, "restoration"): ("Resto Rejuv Tree BIS",
                                     "Resto NGRegrowth BIS"),
               (11, "balance"): ("Boomkin BIS",),
               (11, "cat"): ("Cat BIS",)}

# weapon plan per spec: (mh kinds, oh kind). Kinds: 1H, 2H, DW (mh+oh pair),
# SHIELD, none (caster MH+OH from rankings, hunter keeps ranged). Shaman tank
# is 1H + shield (KB guide: 2H tanking is not viable; the shield is ~50% of
# the armor and 10% bonus threat).
WEAPONS = {"protection": ("1H", "SHIELD"), "arms": ("2H", None),
            "fury": ("DW", "DW"), "retribution": ("2H", None),
            "enhancement": ("2H", None), "tank": ("1H", "SHIELD")}

# equip slot -> Turtle sheet slot label.
TURTLE_SLOT = {0: "Head", 1: "Neck", 2: "Shoulders", 14: "Back", 4: "Chest",
               8: "Wrist", 9: "Hands", 5: "Waist", 6: "Legs", 7: "Feet",
               10: "Ring 1", 11: "Ring 2", 13: "Trinket 1", 12: "Trinket 2",
               15: "Main Hand", 16: "Offhand", 17: "Ranged"}

# Racials (+3) per Appendix D: race id -> weapon subclass names.
RACE_SKILL = {1: {"sword", "mace"}, 2: {"axe"}, 3: {"gun"},
              8: {"bow", "thrown"}, 10: {"bow"}, 9: {"dagger", "mace"}}
SUBCLASS_NAME = {0: "axe", 1: "axe", 2: "bow", 3: "gun", 4: "mace",
                 5: "mace", 6: "polearm", 7: "sword", 8: "sword",
                 10: "staff", 13: "fist", 15: "dagger", 16: "thrown",
                 17: "spear", 18: "crossbow", 19: "wand"}
# Turtle custom races: 9 Goblin, 10 High Elf (playercreateinfo_item).
# Hand overrides (issue #490 PR3 gear): (hire class id, spec, tier, slot)
# -> item id. Applied after the ranked pick, before the no-downgrade guard,
# so progression guards still hold. Each entry states its evidence in the
# comment; keep this table small and prefer audit word-list fixes for
# false alarms.
HAND_OVERRIDES = {
    # retribution t1/t2 feet: BIS rank 1 (18322, mail caster boots) over
    # rank 3 (14616, mail melee boots); the rank-3 item fits the spec
    # (agi front-loads seal damage) and tier. (Rank 2 11822 is cloth with
    # int/sta + spell damage — worse than the BIS pick, never pin it.)
    (2, "retribution", 1, 7): 14616,
    (2, "retribution", 2, 7): 14616,
    # retribution t1/t2 second ring: BIS rank 2 (13001, caster ring, pair
    # filler behind 13098) -> rank 5 18500 (melee ring, same t1 pool).
    (2, "retribution", 1, 11): 18500,
    (2, "retribution", 2, 11): 18500,
    # protection t1/t2 hands: BIS rank 1 (14551, weapon-skill gloves, no
    # tank stat) -> rank 2 13963 (sta + weapon skill, same t1 pool).
    (1, "protection", 1, 9): 13963,
    (1, "protection", 2, 9): 13963,
    (2, "protection", 1, 9): 13963,
    (2, "protection", 2, 9): 13963,
    # protection t3/t4 hands: BIS rank 1 is still 14551; rank 2 16863
    # (sta/str + defense, t3 MC loot) fits warriors, but it is plate
    # (warrior-only), so paladins take rank 3 13162 (sta/str + crit,
    # same t1 pool, paladin-usable).
    (1, "protection", 3, 9): 16863,
    (1, "protection", 4, 9): 16863,
    (2, "protection", 3, 9): 13162,
    (2, "protection", 4, 9): 13162,
    # protection t1/t2 MH: BIS rank-1 three-way tie (11684 proc-only,
    # 12798 armor-shatter proc, 15806 agi/str stat stick); rank 2 10837
    # (str + hit, t1) is the only one with tank-threat stats AND proc —
    # but it is an axe-offhand paladins cannot use, so warriors take it
    # while paladins keep t1 BIS and take rank 5 19867 (sta + defense,
    # t2 ZG loot) at t2.
    (1, "protection", 1, 15): 10837,
    (1, "protection", 2, 15): 10837,
    (2, "protection", 2, 15): 19867,
    # restoration t2 shoulders: BIS rank 1 (20680, hit proc, no heal stats)
    # -> rank 4 19928 (int/spi + healing, t2 ZG loot).
    (7, "restoration", 2, 2): 19928,
    # holy t5 relic: BIS rank 1 (22400 Devotion-armor libram, a prot
    # effect) -> rank 2 22401 (Seal-cost libram, same t1 pool).
    (2, "holy", 5, 17): 22401,
    # protection t2-t4 relic: the warrior-list source has no relic slot, so
    # the weights fallback picks a level-34 libram; pin the t2 Faithful
    # libram (61443, Lord Blackwald II loot) instead. (No t1 libram with a
    # matching effect exists; the t1 fallback stays.)
    (2, "protection", 2, 17): 61443,
    (2, "protection", 3, 17): 61443,
    (2, "protection", 4, 17): 61443,
    # restoration t2-t4 relic: the weights fallback picks an elemental
    # totem; pin the Scholomance healer totem (23200, Lesser-Healing-Wave
    # effect, t1 loot via reference 900700). (No t1 totem with a matching
    # effect exists; the t1 fallback stays.)
    (7, "restoration", 2, 17): 23200,
    (7, "restoration", 3, 17): 23200,
    (7, "restoration", 4, 17): 23200,
    # bear t2-t4 relic: the weights fallback picks a maul-cost idol for a
    # feral set; pin the Moonfang idol (61293, armor effect, t2 loot).
    # (No t1 idol with a matching effect exists; the t1 fallback stays.)
    (11, "bear", 2, 17): 61293,
    (11, "bear", 3, 17): 61293,
    (11, "bear", 4, 17): 61293,
    # shaman tank (issue #1125): KB tank ladder pins (guide/classes/shaman/
    # tank.md; every id verified in world SQL 20260505222630_world.sql).
    # Only item_tiers.csv-TAGGED ids are pinned: the generator's tier_of()
    # gate rejects untagged ids, so the untagged KB picks (Vault's Defender
    # 40003, Sulfuron Aegis 58242, Earthen Guard 20688, Blackthorn's Blackjack
    # 84600, Blackstone Crown 83565, Stonebreaker 61204, Earthfury 47122)
    # need classifier tier tags first; the Enhance list + weight fallback covers those slots meanwhile.
    # shields: Aegis of the Blood God 19862 (t2, tagged) -> Blessed Qiraji
    # Bulwark 21269 (t5, tagged) -> Bulwark of Enduring Earth 55121 (t7,
    # tagged; t3/t4/t6 carry). Immovable Object 19321 skipped: pvp-flagged,
    # excluded() drops it.
    (7, "tank", 2, 16): 19862,
    (7, "tank", 5, 16): 21269,
    (7, "tank", 7, 16): 55121,
    # MH: Deathbringer 17068 (t3, tagged) -> Spineshatter 19335 (t4, tagged)
    # -> Remnants of an Old God 60003 (t5, tagged; carries t6/t7).
    (7, "tank", 3, 15): 17068,
    (7, "tank", 4, 15): 19335,
    (7, "tank", 5, 15): 60003,
    # relic: Rage 22395 (t1, tagged) -> Calming River 61698 (t2, tagged, KB
    # rank 1). Tides 58146 stays unenforced (KB AoE niche).
    (7, "tank", 1, 17): 22395,
    (7, "tank", 2, 17): 61698,
    # ring: Naglering 11669 (t1, tagged). Cryptstalker 23067 NOT pinned:
    # allow_class=4 (hunter-only) per world SQL, unwearable by shamans.
    (7, "tank", 1, 10): 11669,
    # trinkets: Hatereaver Cog 60559 (t1, tagged) + Araxxna's Husk 61816
    # (t2, tagged; carries t4). Onyxia Blood Talisman 18406 (t3, tagged,
    # carries t5/t6) + Sigil of Ancient Accord 58244 (t3, tagged, Tides-build
    # threat).
    (7, "tank", 1, 13): 60559,
    (7, "tank", 1, 12): 18537,
    (7, "tank", 2, 13): 61816,
    (7, "tank", 3, 13): 18406,
    (7, "tank", 3, 12): 58244,
    # warlock t2 first trinket: BIS rank 1 (19812 Rune of the Dawn) only
    # boosts spell damage against the Undead, which the tier's content
    # (Zul'Gurub and the level 60 dungeons) is not; the mage t2 kit's Eye of
    # the Beast (13968, all-school spell damage, t1 quest loot) fits the slot.
    (9, "affliction", 2, 13): 13968,
    (9, "demonology", 2, 13): 13968,
    (9, "destruction", 2, 13): 13968,
}

UNIQUENESS_FLAG = 0  # item_template Flags bit for Unique-equip (0x20000?)
UNIQUE_BIT = 0x20000


def connect(args):
    import pymysql
    kw = {"host": args.db_host, "user": args.db_user,
          "password": args.db_pass, "database": args.db_name}
    if args.db_defaults:
        kw["read_default_file"] = args.db_defaults
    return pymysql.connect(**kw)


def q(cur, sql, params=()):
    cur.execute(sql, params)
    return cur.fetchall()


def main():
    import os as _os
    ap = argparse.ArgumentParser()
    ap.add_argument("--db-host", default=os.environ.get("HIRE_GEAR_DB_HOST", "localhost"))
    ap.add_argument("--db-user", default=os.environ.get("HIRE_GEAR_DB_USER", "mangos"))
    ap.add_argument("--db-pass", default=os.environ.get("HIRE_GEAR_DB_PASS", "mangos"))
    ap.add_argument("--db-name", default=os.environ.get("HIRE_GEAR_DB_NAME", "tw_world"))
    ap.add_argument("--db-defaults", default=os.environ.get("HIRE_GEAR_DB_DEFAULTS"))
    ap.add_argument("--stamp", default=None)
    ap.add_argument("--no-raid-rep", action="store_true")
    ap.add_argument("--raid-rep-allowed",
                    default=_os.environ.get("HIRE_GEAR_RAID_REP_ALLOWED", "1"))
    args = ap.parse_args()

    bc = json.load(gzip.open(DATA / "bisclassic.json.gz", "rt"))
    ts = load_snapshot("turtle_sheets.json", "tools/hire_gear/sources/turtle_sheets/fetch.py")
    wic = load_snapshot("wowisclassic.json", "tools/hire_gear/sources/wowisclassic/scrape.py")
    tiers = {}
    with open(DATA / "item_tiers.csv") as f:
        for r in csv.DictReader(f):
            tiers[int(r["item_id"])] = r
    # wic gear order per page: MH, OH, ranged, head, neck, shoulder, back,
    # chest, wrist, hands, waist, legs, feet, ring1, ring2, trinket1, trinket2
    # (verified on hunter/shaman/paladin P5 pages).
    WIC_SLOTS = (15, 16, 17, 0, 1, 2, 14, 4, 8, 9, 5, 6, 7, 10, 11, 13, 12)
    # (hire class id, hire spec) -> (wic class, wic spec).
    WIC_SPEC = {(1, "protection"): ("warrior", "prot"),
                (1, "arms"): ("warrior", "fury"),
                (1, "fury"): ("warrior", "fury"),
                (2, "protection"): ("paladin", "protection"),
                (2, "holy"): ("paladin", "heal"),
                (2, "retribution"): ("paladin", "ret"),
                (3, "beastmastery"): ("hunter", "hunter"),
                (3, "marksmanship"): ("hunter", "hunter"),
                (3, "survival"): ("hunter", "hunter"),
                (4, "assassination"): ("rogue", "dagger"),
                (4, "combat"): ("rogue", "sword"),
                (4, "subtlety"): ("rogue", "dagger"),
                (5, "holy"): ("priest", "holy"),
                (5, "discipline"): ("priest", "holy"),
                (5, "shadow"): ("priest", "shadow"),
                (7, "restoration"): ("shaman", "resto"),
                (7, "elemental"): ("shaman", "elem"),
                (7, "enhancement"): ("shaman", "enhan"),
                (7, "tank"): ("shaman", "enhan"),
                (8, "frost"): ("mage", "mage"),
                (8, "fire"): ("mage", "mage"),
                (8, "arcane"): ("mage", "mage"),
                (9, "affliction"): ("warlock", "warlock"),
                (9, "demonology"): ("warlock", "warlock"),
                (9, "destruction"): ("warlock", "warlock"),
                (11, "bear"): ("druid", "tank"),
                (11, "restoration"): ("druid", "resto"),
                (11, "balance"): ("druid", "balance"),
                (11, "cat"): ("druid", "cat")}
    # wic phase per target tier (classic phase whose content matches).
    WIC_PHASE = {1: "pre-raid", 2: "4", 3: "1", 4: "3", 5: "5", 6: "6",
                 7: "6"}
    wic_item = {}  # (wic class, wic spec, wic phase, slot) -> item id
    for r in wic["rows"]:
        for i, g in enumerate(r["gear"]):
            if i < len(WIC_SLOTS):
                wic_item[(r["class"], r["spec"], r["phase"],
                          WIC_SLOTS[i])] = g["item_id"]
    ench_map = {}  # (spec_key, tier, slot) -> (spell, name)
    ef = DATA / "enchants_by_tier.json"
    if ef.exists():
        ench = json.loads(ef.read_text())
        for spec_key, tierspec in ench.get("specs", {}).items():
            for tstr, slots in tierspec.items():
                for sstr, leaf in slots.items():
                    if isinstance(leaf, dict) and leaf.get("spell"):
                        ench_map[(spec_key, int(tstr), int(sstr))] = (
                            leaf["spell"], leaf.get("name", ""))

    db = connect(args)
    cur = db.cursor()
    # item stats for weight fallback + proficiency + unique flag
    stats = {}
    for r in q(cur, "SELECT entry, name, class, subclass, inventory_type,"
                    " allowable_class, allowable_race, quality, flags,"
                    " required_level, stat_type1, stat_value1, stat_type2,"
                    " stat_value2, stat_type3, stat_value3, stat_type4,"
                    " stat_value4, stat_type5, stat_value5, stat_type6,"
                    " stat_value6, stat_type7, stat_value7, stat_type8,"
                    " stat_value8, stat_type9, stat_value9, stat_type10,"
                    " stat_value10, delay, dmg_min1, dmg_max1, armor"
                    " FROM item_template"):
        stats[r[0]] = r
    # item proc/equip spells for the weapon stat gate (spellid_1..5).
    item_spells = {}
    for r in q(cur, "SELECT entry, spellid_1, spellid_2, spellid_3,"
                    " spellid_4, spellid_5 FROM item_template"):
        sids = [int(x) for x in r[1:] if x]
        if sids:
            item_spells[r[0]] = sids
    scales = {}
    for sid, name, cls in q(cur, "SELECT id, name, class FROM ai_playerbot_weightscales"):
        scales[(cls, name)] = sid
    wvals = {}
    for sid, field, val in q(cur, "SELECT id, field, val FROM ai_playerbot_weightscale_data"):
        wvals.setdefault(sid, {})[field] = val
    races_by_class = {}
    for cls, races in q(cur, "SELECT class, GROUP_CONCAT(DISTINCT race)"
                             " FROM playercreateinfo_item GROUP BY class"):
        races_by_class[cls] = [int(x) for x in races.split(",")]
    vendor_items = {it for (it,) in
                    q(cur, "SELECT DISTINCT item FROM npc_vendor")}
    vendor_rep349 = {it for (it,) in q(
        cur, "SELECT entry FROM item_template WHERE"
             " required_reputation_faction<>0")}
    cond_vendor = {it for (it,) in q(
        cur, "SELECT DISTINCT item FROM npc_vendor WHERE condition_id<>0")}
    # stat_type -> weight field (ItemModType: 3 agi 4 str 5 int 6 spi 7 sta)
    MOD_FIELD = {3: "agi", 4: "str", 5: "int", 6: "spi", 7: "sta"}

    def weight(item, scale_id):
        s = stats.get(item)
        if not s or scale_id is None:
            return 0
        w = wvals.get(scale_id, {})
        total = 0
        for i in range(10):
            t, v = s[10 + i * 2], s[11 + i * 2]
            f = MOD_FIELD.get(t)
            if f and v:
                total += w.get(f, 0) * v
        # weapon dps + armor (server CalculateStatWeight idea, simplified)
        if s[2] == 2 and s[30]:
            dps = ((s[31] or 0) + (s[32] or 0)) / 2 / (s[30] / 1000.0)
            total += w.get("mledps", w.get("rgddps", 0)) * dps
        total += w.get("armor", 0) * (s[33] or 0)
        return total

    # weapon proficiencies from playercreateinfo_spell (verified per class):
    # 196 1H-axe, 197 2H-axe, 198 1H-mace, 199 2H-mace, 200 polearm,
    # 201 1H-sword, 202 2H-sword, 264 bow, 266 gun, 227 staff, 1180 dagger,
    # 2567 thrown, 5009 wand, 5011 crossbow, 15590 fist.
    # class -> set(item subclass ids usable as weapons).
    # hunter: no maces/wands (no 198/199/5009); rogue: no 2H/staff/polearm;
    # priest: dagger/1H-mace/staff/wand; mage/warlock: 1H-sword/dagger/staff/
    # wand; druid: dagger/fist/1H+2H-mace/staff/polearm (Turtle).
    # warrior/rogue ranged = thrown only (2567 verified, no bow/gun/crossbow).
    # Vanilla weapon table (trainer-learned; playercreateinfo_spell only
    # has STARTING skills): warriors use all melee + bows/guns/crossbows/
    # thrown (no wands); rogues add bows/guns/crossbows/thrown to swords/
    # daggers/fist/1H-maces; hunters use axes/swords/daggers/polearms/fist/
    # staves + all ranged, no maces, no wands.
    PROF = {1: {0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 13, 15, 16, 18},
            2: {4, 5, 7, 8},
            3: {0, 1, 2, 3, 6, 7, 8, 10, 13, 15, 16, 18},
            4: {2, 3, 4, 7, 13, 15, 16, 18},
            5: {4, 10, 15, 19},
            7: {0, 1, 4, 5, 10, 13, 15},
            8: {7, 10, 15, 19},
            9: {7, 10, 15, 19},
            11: {4, 5, 6, 10, 13, 15}}
    # slot 17 per class: relic-only (druid idol / paladin libram / shaman
    # totem, invtype 28) for shaman/druid/paladin; hunters use ranged.
    RELIC_ONLY = {7, 11, 2}
    # head, shoulders, chest, waist, legs, feet, wrist, hands, robe (not cloaks)
    MAIL_ARMOR_INV = {1, 3, 5, 6, 7, 8, 9, 10, 20}

    def usable(item, cls):
        s = stats.get(item)
        if not s:
            return False
        if s[5] != -1 and not (s[5] & (1 << (cls - 1))):
            return False
        # weapon proficiency: weapons (class 2) must be a usable subclass;
        # ranged guns/bows (any item class, e.g. quest guns) gate the same
        # way — warriors/rogues never get guns (Egan's Blaster).
        if s[2] == 2 and s[3] not in PROF.get(cls, set()):
            return False
        if s[4] in (15, 25, 26) and s[3] not in PROF.get(cls, set()):
            return False
        # 8 idol = druid, 9 totem = shaman.
        if s[4] == 28:
            want = {2: 7, 11: 8, 7: 9}.get(cls)
            if s[3] != want:
                return False
        return True

    # (bcls, bspec, phase) -> True when the phase pool holds no usable item
    # of the phase's own new-content tier.
    PHASE_NEW_TIER = {"P1": 3, "P2": 1, "P3": 4, "P4": 2, "P5": 5, "P6": 6}

    def _phase_usable(it, cls):
        t = tiers.get(it)
        if not t or t["tier"] == "":
            return False
        fl = t["flags"].split("+")
        if "pvp" in fl or "legendary" in fl or "unobtainable" in fl:
            return False
        if "rep" in fl and not (
                RAID_REP_ON and "raid-rep:t" in t.get("evidence", "")):
            return False
        return usable(it, cls)

    BIS_MISSING = set()
    for (cls, spec) in {(c, s) for c, s in
                        [SOURCE_BY_CLASS.get((c, s), SOURCE[s])
                         for c, s in SPECS]}:
        for ph, exp in PHASE_NEW_TIER.items():
            pool = [r["item_id"] for r in bc["rows"]
                    if r["class"] == cls and r["spec"] == spec
                    and r["phase"] == ph]
            # usable() needs a hire class; check per hire class below at pick
            # time — here require only tier+flags (class-agnostic).
            ok = False
            for it in pool:
                t = tiers.get(it)
                if not t or t["tier"] == "" or int(t["tier"]) != exp:
                    continue
                fl = t["flags"].split("+")
                if "pvp" in fl or "legendary" in fl or "unobtainable" in fl:
                    continue
                # rep counts as present (generator decides per item later).
                ok = True
                break
            if not ok:
                BIS_MISSING.add((cls, spec, ph))
    # Both factions merged (shaman is Horde-only, paladin Alliance-only).
    # Selection per target tier uses ONE representative classic phase — the
    # phase that introduced the tier's headline content — so raw ranks never
    # compare across phases and rank ties break toward the higher tier:
    #   t1 pre-raid -> P2 (DM), t2 ZG/AQ20 -> P4, t3 MC/Ony -> P1,
    #   t4 BWL -> P3, t5 AQ40 -> P5, t6 Naxx -> P6, t7 Turtle -> P6.
    TIER_PHASE = {1: "P2", 2: "P4", 3: "P1", 4: "P3", 5: "P5", 6: "P6",
                  7: "P6"}
    PHASES = ("P6", "P5", "P4", "P3", "P2", "P1", "pre-raid")
    ranked = {}
    for r in bc["rows"]:
        key = (r["class"], r["spec"], r["equip_slot"], r["phase"])
        ranked.setdefault(key, []).append((r["rank"], r["item_id"], r["offhand"]))
    for v in ranked.values():
        v.sort()
    # (BIS_MISSING computed above, before ranked.)

    # raid-faction rep gear allowed from its tier when RAID_REP_ALLOWED is ON
    # (owner decision): the rep flag alone no longer excludes; other rep,
    # PvP and legendary still exclude.
    RAID_REP_ON = not args.no_raid_rep and str(args.raid_rep_allowed) != "0"
    RAID_REP_TIER = {270: 2, 609: 2, 910: 5, 529: 6}

    def tier_of(item):
        t = tiers.get(item)
        if not t or t["tier"] == "":
            return None
        return int(t["tier"])

    def excluded(item):
        t = tiers.get(item)
        if not t or t["tier"] == "":
            return "unobtainable"
        fl = t["flags"].split("+")
        if "pvp" in fl or "legendary" in fl:
            return "pvp" if "pvp" in fl else "legendary"
        if "rep" in fl:
            # allowed only for raid-faction rep (classifier writes raid-rep:t
            # evidence); all other rep gear stays excluded.
            if RAID_REP_ON and "raid-rep:t" in t.get("evidence", ""):
                return None
            return "rep"
        return None

    # turtle preference per (tab-class, list, slot label, tab): item.
    # Per-spec tab mapping (TURTLE_TABS): first tab is primary, later tabs
    # fill only slots the primary leaves empty. Never pooled per class.
    turtle = {}
    for r in ts["rows"]:
        if r["slot"] == "Trinket Swap" or not r["item_id"]:
            continue
        turtle.setdefault((r["class"], r["list_name"], r["slot"], r["tab"]),
                          []).append(r["item_id"])

    CLASS_NAME = {1: "warrior", 2: "paladin", 3: "hunter", 4: "rogue",
                  5: "priest", 7: "shaman", 8: "mage", 9: "warlock",
                  11: "druid"}
    # weapon-composition gate: allowed inventory_type per (class, spec) for
    # slots 15/16. Dual-wield: 1H MH + 1H OH. 2H specs: inv-17 MH with an
    # empty OH. Shield specs: 1H MH + inv-14 OH. Casters/healers follow the
    # rankings (2H staff with empty OH, or 1H + holdable 23/shield 14).
    DUAL = {(1, "fury"), (4, "assassination"), (4, "combat"),
            (4, "subtlety")}
    TWOH = {(1, "arms"), (2, "retribution"), (7, "enhancement")}
    SHIELD = {(1, "protection"), (2, "protection"), (7, "tank")}

    def comp_ok(cls, spec, slot, item):
        s = stats.get(item)
        if not s:
            return False
        inv = s[4]
        key = (cls, spec)
        if slot == 15:
            if key in DUAL:
                return inv in (13, 21)
            if key in TWOH:
                return inv == 17
            if key in SHIELD:
                return inv in (13, 21)
            return inv in (13, 17, 21)
        if slot == 16:
            if key in DUAL:
                return inv in (13, 22)
            if key in TWOH:
                return False  # 2H specs never take an offhand
            if key in SHIELD:
                return inv == 14
            if key in {(2, "holy"), (5, "holy"), (5, "discipline"),
                       (7, "restoration"), (7, "elemental")}:
                return inv in (14, 23)
            return inv in (13, 14, 22, 23)
        return True

    # weapon stat gate: physical-melee weapons need a melee stat or proc;
    # caster/healer weapons need a caster stat or proc. Item stat types 3
    # agi 4 str 5 int 6 spi 7 sta; proc text from spell_template.
    PHYS_SPECS = {(1, "protection"), (1, "arms"), (1, "fury"),
                  (2, "retribution"), (2, "protection"),
                  (3, "beastmastery"), (3, "marksmanship"), (3, "survival"),
                  (4, "assassination"), (4, "combat"), (4, "subtlety"),
                  (7, "enhancement"), (7, "tank"), (11, "bear"), (11, "cat")}
    CAST_SPECS = {(2, "holy"), (5, "holy"), (5, "discipline"), (5, "shadow"),
                  (7, "restoration"), (7, "elemental"), (8, "frost"),
                  (8, "fire"), (8, "arcane"), (9, "affliction"),
                  (9, "demonology"), (9, "destruction"), (11, "restoration"),
                  (11, "balance")}
    MELEE_WORDS = ("attack power", "strength", "agility", "critical",
                   "hit chance", "feral", "armor penetration", "strike",
                   "slaying", "sword", "axe", "mace", "dagger", "polearm",
                   "fist")
    # tank specs (protection, bear) also accept stamina and defensive
    # procs (Sanctuary, defense, dodge, parry, block): a Quel'Serrar with
    # no str/agi is a tank weapon, not a stat stick.
    TANK_SPECS_W = {(1, "protection"), (2, "protection"), (7, "tank"), (11, "bear")}
    TANK_WORDS_W = ("stamina", "sanctuary", "defense", "dodge", "parry",
                    "block", "armor")
    CAST_WORDS = ("healing", "spell", "mana regen", "spell damage", "frost",
                  "fire", "shadow", "nature", "arcane")
    spell_names = {}
    for sid, nm in q(cur, "SELECT entry, name FROM spell_template"):
        spell_names[int(sid)] = (nm or "").lower()
    def weapon_stat_ok(cls, spec, item):
        s = stats.get(item)
        if not s or s[2] != 2:
            return True  # not a weapon: other slots unchecked
        key = (cls, spec)
        if key not in PHYS_SPECS and key not in CAST_SPECS:
            return True  # hunters: melee slot is a stat stick
        mods = {}
        for i in range(10):
            t, v = s[10 + i * 2], s[11 + i * 2]
            if v:
                mods[t] = mods.get(t, 0) + v
        melee_stat = bool(mods.get(3, 0) or mods.get(4, 0))
        caster_stat = bool(mods.get(5, 0) or mods.get(6, 0))
        tank_ok = key in TANK_SPECS_W and bool(mods.get(7, 0))
        for sid in item_spells.get(item, ()):
            nm = spell_names.get(sid, "")
            if any(w in nm for w in MELEE_WORDS):
                melee_stat = True
            if any(w in nm for w in CAST_WORDS):
                caster_stat = True
            if key in TANK_SPECS_W and any(w in nm for w in TANK_WORDS_W):
                tank_ok = True
        if key in PHYS_SPECS:
            if key in TANK_SPECS_W:
                return melee_stat or tank_ok
            return melee_stat
        return caster_stat
    # spell_template mask data for the enchant allowlist (same table the
    # server loads from; DB `spell`/`spellitemenchantment` are empty, the
    # template columns are the spell DBC content). Loaded once per run.
    ench_mask = {}  # spell -> (class, subclass mask, invtype mask)
    for e, c, sm, im in q(cur, "SELECT entry, equippedItemClass,"
                                " equippedItemSubClassMask,"
                                " equippedItemInventoryTypeMask"
                                " FROM spell_template"):
        ench_mask[int(e)] = (int(c), int(sm), int(im))

    def enchant_for(cls, spec, tier, slot, item):
        # empty slot (2H offhand): no enchant.
        if not item:
            return 0
        s = stats.get(item)
        if not s:
            return 0
        inv = s[4]
        # held-in-off-hand (inv 23): nothing takes an enchant.
        if slot == 16 and inv == 23:
            return 0
        # shields (inv 14): shield enchant for tank specs, else none —
        # never a weapon enchant.
        if slot == 16 and inv == 14:
            if (cls, spec) in {(1, "protection"), (2, "protection"),
                    (7, "tank")}:
                return 20017
            return 0
        hit = ench_map.get((f"{CLASS_NAME[cls]}_{spec}", tier, slot))
        if not hit:
            return 0
        spell = hit[0]
        # validate against the spell's own masks, mirroring the runtime
        # EnchantItemT check: subclass bit OR inventory-type bit must fit
        # the ACTUAL item (EnchantItemT skips the class column, so the
        # masks are what decides — e.g. Crusader's mask excludes shields).
        m = ench_mask.get(spell)
        if m is not None:
            eclass, smask, imask = m
            sub_ok = bool((1 << s[3]) & smask) if smask else False
            inv_ok = bool((1 << inv) & imask) if imask else False
            if eclass != -1 and eclass != s[2]:
                return 0
            if not (sub_ok or inv_ok):
                return 0
        # scopes only fit bow/gun/crossbow (subclass 2/3/18, same mask the
        # server's EnchantItemT checks); relics, thrown and wands get none.
        if slot == 17 and spell in (12460, 22779, 3974, 3975, 3976, 12459,
                                    36945):
            if s[3] not in (2, 3, 18):
                return 0
        return spell

    rows = []  # (class, spec, race, tier, slot, item, enchant, source, flags)
    NO_DOWNGRADE_REVERTS = []  # (class, spec, tier, slot, kept item, new rank)
    WIC_FILLS = []  # (class, spec, tier, slot, item)
    for cls, spec in SPECS:
        bcls, bspec = SOURCE_BY_CLASS.get((cls, spec), SOURCE[spec])
        scale_id = scales.get((cls, SCALES[spec]))
        tclass = CLASS_NAME[cls]
        prev = {}
        for tier in range(1, 8):
            picks = {}
            prev_tier = dict(prev)  # previous tier's picks; prev[] gets
            # overwritten by the current tier during the slot loop
            for slot in range(0, 18+1):
                if slot in (3, 18):  # shirt, tabard: never
                    continue
                item, source, flags = None, "bisclassic", 0
                # equip_slot 10 serves both ring slots, 12 both trinkets
                cand_slots = {11: (10, 11), 10: (10, 11),
                              13: (12, 13), 12: (12, 13)}.get(slot, (slot,))

                def allowed(it):
                    t = tier_of(it)
                    if t is None or t > tier or excluded(it) \
                            or not usable(it, cls):
                        return False
                    # the shaman tank wears mail for armor (the Enhancement
                    # lists it borrows rank leather agility pieces high).
                    s = stats.get(it)
                    if (cls, spec) == (7, "tank") and s[2] == 4 \
                            and s[3] in (1, 2) and s[4] in MAIL_ARMOR_INV:
                        return False
                    return True

                # generic slot vs inventory_type gate (orchestrator rule):
                # 15 accepts 13,17,21; 16 accepts 13,14,22,23 (17 never —
                # no Titan Grip); 17 accepts 15,25,26 + 28 per class below.
                # Quivers (class 11, e.g. Harpy Hide Quiver) are bags, never
                # equipped — excluded everywhere.
                def slot_ok(it):
                    s = stats.get(it)
                    if not s:
                        return False
                    if s[2] == 11:  # containers/ammo bags
                        return False
                    # incoherent DB rows (Egan's Blaster: gun modeled as
                    # subclass-0 axe): ranged items must carry a ranged
                    # subclass (bow 2 / gun 3 / thrown 16 / crossbow 18 /
                    # wand 19).
                    if s[4] in (15, 25, 26) and s[3] not in (2, 3, 16, 18, 19):
                        return False
                    inv = s[4]
                    if slot == 15:
                        return inv in (13, 17, 21)
                    if slot == 16:
                        return inv in (13, 14, 22, 23)
                    if slot == 17:
                        if cls in RELIC_ONLY:
                            # relic-only classes: librams/totems/idols only.
                            return inv == 28
                        return inv in (15, 25, 26)
                    return True

                def weapon_ok(it):
                    if not slot_ok(it):
                        return False
                    # arms: fury list, 2H only for MH
                    if (cls, spec) == (1, "arms") and slot == 15:
                        return stats.get(it, (0,) * 34)[4] == 17
                    if spec in WEAPONS and slot in (15, 16):
                        plan = WEAPONS[spec][0 if slot == 15 else 1]
                        if plan == "2H":
                            return (stats.get(it) or (0,) * 34)[4] == 17
                        if plan == "1H":
                            return (stats.get(it) or (0,) * 34)[4] in \
                                (13, 21, 22)
                        if plan == "SHIELD":
                            return (stats.get(it) or (0,) * 34)[4] == 14
                    return True

                # representative phase for this tier; rank ties break toward
                # the higher tier (new unlocks win: 18404 over 15411 at t3).
                # Fall back to latest-first walk if the phase has no data.
                # No-downgrade guard below compares ranks within the phase.
                # MISSING phase (no new-content items, e.g. warrior P5/P6):
                # skip bisclassic ranked walks for this spec+tier entirely.
                bis_missing = (bcls, bspec, TIER_PHASE[tier]) in BIS_MISSING
                pick_rank, pick_phase = None, TIER_PHASE[tier]
                cands = []
                if not bis_missing:
                    for cs in cand_slots:
                        cands += [(rk, it, oh) for rk, it, oh in
                                  ranked.get((bcls, bspec, cs,
                                              TIER_PHASE[tier]), [])]
                cands.sort(key=lambda x: (x[0], -(tier_of(x[1]) or 0), x[1]))
                # pair rule: the second slot of a pair (11 after 10, 13
                # after 12, 16 after 15) never takes the first slot's item.
                pair_first = {11: 10, 13: 12, 16: 15}.get(slot)
                pair_item = picks.get(pair_first) if pair_first else None
                for rk, it, oh in cands:
                    if not allowed(it) or not weapon_ok(it):
                        continue
                    if slot in (15, 16) and (
                            not comp_ok(cls, spec, slot, it)
                            or not weapon_stat_ok(cls, spec, it)):
                        continue
                    if pair_item is not None and it == pair_item:
                        continue
                    item = it
                    pick_rank, pick_phase = rk, TIER_PHASE[tier]
                    break
                if item is None and not bis_missing:
                    for ph in PHASES:
                        cands = []
                        for cs in cand_slots:
                            cands += [(rk, it, oh) for rk, it, oh in
                                      ranked.get((bcls, bspec, cs, ph), [])]
                        cands.sort(key=lambda x: (x[0], -(tier_of(x[1]) or 0),
                                                  x[1]))
                        for rk, it, oh in cands:
                            if not allowed(it) or not weapon_ok(it):
                                continue
                            if slot in (15, 16) and (
                                    not comp_ok(cls, spec, slot, it)
                                    or not weapon_stat_ok(cls, spec, it)):
                                continue
                            if pair_item is not None and it == pair_item:
                                continue
                            item = it
                            pick_rank, pick_phase = rk, ph
                            break
                        if item is not None:
                            break
                # tiers 6-7: turtle preference from this spec's own tab(s)
                # only: primary fills what it can, fallback tabs fill only
                # slots the primary left empty. Skipped when it fails the
                # weapon-composition or stat gate, and never when it
                # downgrades the bisclassic pick's source tier (Turtle
                # customs often carry t0/proc tiers).
                if tier >= 6 and slot in TURTLE_SLOT:
                    lst = "K40" if tier == 7 else "Naxx"
                    for tab in TURTLE_TABS.get((cls, spec), ()):
                        # tab names in the sheet carry the list prefix
                        # ("K40 Fury BIS"); match the exact full name.
                        for tid in turtle.get(
                                (tclass, lst, TURTLE_SLOT[slot],
                                 f"{lst} {tab}"), []):
                            t = tier_of(tid)
                            if t is None or t > tier or excluded(tid) \
                                    or not usable(tid, cls):
                                continue
                            if pair_item is not None and tid == pair_item:
                                continue
                            ts_ = stats.get(tid)
                            if not ts_ or ts_[2] == 11:
                                continue
                            # polearm-modelled-as-2H rows (Surv tab "Main
                            # Hand" pointing at a polearm): skip, the ranked
                            # pools carry axes/swords for the melee slot.
                            if cls == 3 and ts_[3] == 6:
                                continue
                            if slot in (15, 16, 17) and not slot_ok(tid):
                                continue
                            if slot in (15, 16) and (
                                    not comp_ok(cls, spec, slot, tid)
                                    or not weapon_stat_ok(cls, spec, tid)):
                                continue
                            if item is not None:
                                it0 = tier_of(item)
                                if it0 is not None and t < it0:
                                    continue
                            if tid != item:
                                source = "turtle-sheet"
                            item = tid
                            pick_rank, pick_phase = None, None
                            break
                        if item is not None and source == "turtle-sheet":
                            break

                # NO DOWNGRADES: a tier never replaces the previous tier's
                # item with one the current phase list ranks worse. If the
                # previous item is absent from the current list, keep it when
                # its source tier is higher than the new pick's.
                if item is not None and slot in prev and prev[slot] not in (
                        0, item) and pick_rank is not None \
                        and pick_phase is not None and source == "bisclassic":
                    phase_ranks = {}
                    for cs in cand_slots:
                        for rk, it, oh in ranked.get(
                                (bcls, bspec, cs, pick_phase), []):
                            if it not in phase_ranks or rk < phase_ranks[it]:
                                phase_ranks[it] = rk
                    if prev[slot] in phase_ranks and \
                            phase_ranks[prev[slot]] < pick_rank:
                        item = prev[slot]
                        source = "prev-tier"
                        NO_DOWNGRADE_REVERTS.append(
                            (cls, spec, tier, slot, item, pick_rank))
                    elif prev[slot] not in phase_ranks:
                        pt = tier_of(prev[slot])
                        nt = tier_of(item)
                        if pt is not None and nt is not None and pt > nt:
                            item = prev[slot]
                            source = "prev-tier"
                            NO_DOWNGRADE_REVERTS.append(
                                (cls, spec, tier, slot, item, pick_rank))
                # base never displaces: a t0 pick never replaces a prev item
                # of tier >= 1 (world-drop quirks like Diamond Flask over HoJ).
                if item is not None and slot in prev and prev[slot] not in (
                        0, item) and source == "bisclassic":
                    pt = tier_of(prev[slot])
                    nt = tier_of(item)
                    if pt is not None and pt >= 1 and nt == 0:
                        item = prev[slot]
                        source = "prev-tier"
                        NO_DOWNGRADE_REVERTS.append(
                            (cls, spec, tier, slot, item, pick_rank))
                # slots the bisclassic phase list leaves empty.
                if item is None:
                    wc, ws = WIC_SPEC.get((cls, spec), (None, None))
                    if wc is not None:
                        wid = wic_item.get((wc, ws, WIC_PHASE[tier], slot))
                        if wid is not None and wid != pair_item:
                            t = tier_of(wid)
                            if t is not None and t <= tier \
                                    and not excluded(wid) \
                                    and usable(wid, cls):
                                s = stats.get(wid)
                                if s and s[2] != 11 and (
                                        slot not in (15, 16, 17)
                                        or slot_ok(wid)):
                                    if slot not in (15, 16) or (
                                            comp_ok(cls, spec, slot, wid)
                                            and weapon_stat_ok(
                                                cls, spec, wid)):
                                        item, source = wid, "wowisclassic"
                                        WIC_FILLS.append(
                                            (cls, spec, tier, slot, wid))
                _ovkey = (cls, spec, tier, slot)
                if item is None and slot in prev and _ovkey not in HAND_OVERRIDES:
                    pv = prev[slot]
                    if slot not in (15, 16) or pv == 0 \
                            or (comp_ok(cls, spec, slot, pv)
                                and weapon_stat_ok(cls, spec, pv)):
                        item, source = pv, "prev-tier"
                if item is None:
                    # weight fallback over pool items of this slot
                    cands2 = []
                    for it, s in stats.items():
                        if s[2] == 11:  # bags/ammo, never equipped
                            continue
                        inv = s[4]
                        if slot in (15, 16, 17):
                            if not slot_ok(it):
                                continue
                        else:
                            ok = {0: inv == 1, 1: inv == 2, 2: inv == 3,
                                  14: inv == 16, 4: inv in (5, 20), 8: inv == 9,
                                  9: inv == 10, 5: inv == 6, 6: inv == 7,
                                  7: inv == 8, 10: inv == 11, 11: inv == 11,
                                  13: inv == 12, 12: inv == 12}.get(slot, False)
                            if not ok:
                                continue
                        if slot in (15, 16) and (
                                not comp_ok(cls, spec, slot, it)
                                or not weapon_stat_ok(cls, spec, it)):
                            continue
                        t = tier_of(it)
                        is_white = False
                        if t is None:
                            # vendor whites / unclassified base gear: sold by
                            # a plain vendor, no rep gate, no pvp/legendary.
                            # (excluded() says "unobtainable" for untiered
                            # items, so check flags directly; rep comes from
                            # the item row since the CSV has no entry.)
                            tt = tiers.get(it, {})
                            fl = tt.get("flags", "").split("+") \
                                if tt else []
                            if it in vendor_items and it not in cond_vendor \
                                    and it not in vendor_rep349 \
                                    and "pvp" not in fl \
                                    and "legendary" not in fl \
                                    and usable(it, cls):
                                t, is_white = 0, True
                            else:
                                continue
                        if t > tier or (excluded(it) and not is_white) \
                                or not usable(it, cls):
                            continue
                        if pair_item is not None and it == pair_item:
                            continue
                        cands2.append((weight(it, scale_id), it))
                    if cands2:
                        cands2.sort(reverse=True)
                        item, source, flags = cands2[0][1], "weights", 1
                # required OH must never stay empty for dual-wield/shield
                # specs: the gated prev-tier carry above reuses the legal
                # previous item (failsafe: same value, flags bit0).
                if item is None and slot == 16 and (cls, spec) in DUAL | SHIELD \
                        and slot in prev_tier and prev_tier[slot]:
                    item, source, flags = prev_tier[slot], "prev-tier", 1
                # hand overrides (HAND_OVERRIDES above): validated like any
                # ranked pick. Runs after the fallback chain so slots with
                # no ranked/wic rows (relics) can be pinned too; the row
                # below records source bisclassic with flags bit1 (hand).
                _ov = HAND_OVERRIDES.get((cls, spec, tier, slot))
                if _ov is not None and _ov != pair_item:
                    _ot = tier_of(_ov)
                    if _ot is not None and _ot <= tier and not excluded(_ov) \
                            and usable(_ov, cls):
                        _os = stats.get(_ov)
                        if _os is not None and _os[2] != 11 and (
                                slot not in (15, 16, 17) or slot_ok(_ov)):
                            if slot not in (15, 16) or (
                                    comp_ok(cls, spec, slot, _ov)
                                    and weapon_stat_ok(cls, spec, _ov)):
                                # re-stamp even when a fallback already
                                # picked the same item (source/flags must
                                # say hand-touched).
                                item, source, flags = _ov, "bisclassic", 1

                if item is None:
                    continue
                picks[slot] = item
                prev[slot] = item
                rows.append((cls, spec, 0, tier, slot, item,
                             enchant_for(cls, spec, tier, slot, item),
                             source, flags))
            # 2H => explicit offhand-0 row (the runtime clears the offhand
            # only on an explicit item_id-0 row; a missing row keeps
            # today's item equipped).
            mh = picks.get(15)
            if mh and (stats.get(mh) or (0,) * 34)[4] == 17:
                for i, r in enumerate(rows):
                    if r[0] == cls and r[1] == spec and r[2] == 0 and r[3] == tier and r[4] == 16:
                        rows[i] = (cls, spec, 0, tier, 16, 0, 0, "prev-tier", 0)
                        break
                else:
                    rows.append((cls, spec, 0, tier, 16, 0, 0, "prev-tier", 0))
                picks[16] = 0
                prev[16] = 0
            # unique-equip: rings/trinkets/weapons must not repeat the same
            # item in both slots (both slots share one ranked pool).
            for a, b in ((10, 11), (13, 12), (15, 16)):
                ia = picks.get(a)
                ib = picks.get(b)
                if ia and ib and ia == ib and ia != 0:
                    cand_slots = {11: (10, 11), 10: (10, 11),
                                  13: (12, 13), 12: (12, 13)}.get(b, (b,))
                    rep = None
                    for ph in PHASES:
                        cands = []
                        for cs in cand_slots:
                            cands += [(rk, it, oh) for rk, it, oh in
                                      ranked.get((bcls, bspec, cs, ph), [])]
                        cands.sort()
                        for rk, it, oh in cands:
                            t = tier_of(it)
                            if it == ia or t is None or t > tier \
                                    or excluded(it) or not usable(it, cls):
                                continue
                            s = stats.get(it)
                            if not s or s[2] == 11:
                                continue
                            if b in (15, 16, 17) and not slot_ok(it):
                                continue
                            if b in (15, 16) and (
                                    not comp_ok(cls, spec, b, it)
                                    or not weapon_stat_ok(cls, spec, it)):
                                continue
                            rep = it
                            break
                        if rep is not None:
                            break
                    # replacement obeys the same no-downgrade rules (rank in
                    # the current phase list, then tier, then t0 floor).
                    # NOTE: reads prev_tier (snapshot), NOT prev[] which the
                    # slot loop already overwrote with this tier's picks.
                    # never revert to the item slot `a` already holds
                    # (unique-equip), or the set keeps the duplicate.
                    if rep is not None and b in prev_tier \
                            and prev_tier[b] not in (0, rep, ia):
                        phase_ranks = {}
                        for cs in cand_slots:
                            for rk, it, oh in ranked.get(
                                    (bcls, bspec, cs,
                                     TIER_PHASE.get(tier, "P6")), []):
                                if it not in phase_ranks \
                                        or rk < phase_ranks[it]:
                                    phase_ranks[it] = rk
                        rep_rk = phase_ranks.get(rep, 999)
                        prev_rk = phase_ranks.get(prev_tier[b], 999)
                        pt, nt = tier_of(prev_tier[b]), tier_of(rep)
                        if prev_tier[b] in phase_ranks and prev_rk < rep_rk:
                            rep = prev_tier[b]
                        elif pt is not None and nt is not None and pt > nt:
                            rep = prev_tier[b]
                        elif pt is not None and pt >= 1 and nt == 0:
                            rep = prev_tier[b]
                        if rep == prev_tier[b]:
                            NO_DOWNGRADE_REVERTS.append(
                                (cls, spec, tier, b, rep, rep_rk))
                    for i, r in enumerate(rows):
                        if r[0] == cls and r[1] == spec and r[2] == 0 \
                                and r[3] == tier and r[4] == b:
                            rows[i] = (cls, spec, 0, tier, b, rep or 0,
                                       enchant_for(cls, spec, tier, b,
                                                   rep or 0),
                                       "bisclassic" if rep else "prev-tier",
                                       0 if rep else 1)
                    # refresh picks (and prev: the guard reads prev, and the
                    # replaced row is what the next tier must compare against)
                    for r in rows:
                        if r[0] == cls and r[1] == spec and r[2] == 0 \
                                and r[3] == tier:
                            picks[r[4]] = r[5]
                            prev[r[4]] = r[5]
            # racial rows: weapon slots only, bonus weapon in source top 3
            for slot in (15, 16, 17):
                base = picks.get(slot)
                if not base:
                    continue
                # Turtle-sheet weapons (t6/t7) have no ranking to take a
                # top 3 from: keep the sheet's pick for every race.
                base_src = next((r[7] for r in rows if r[0] == cls
                                 and r[1] == spec and r[2] == 0
                                 and r[3] == tier and r[4] == slot), "")
                if base_src == "turtle-sheet":
                    continue
                base_tier = tier_of(base) or 0
                for race in races_by_class.get(cls, []):
                    skills = RACE_SKILL.get(race, set())
                    if not skills:
                        continue
                    # top 3 of this tier's phase list (earlier phases only
                    # when that list is empty).
                    cands = []
                    for ph in (TIER_PHASE[tier],) + PHASES:
                        cands = sorted(
                            [(rk, it, oh) for rk, it, oh in
                             ranked.get((bcls, bspec, slot, ph), [])])
                        if cands:
                            break
                    top3 = [it for _, it, _ in cands[:3]]
                    pick = None
                    for it in top3:
                        s = stats.get(it)
                        if not s:
                            continue
                        sub = SUBCLASS_NAME.get(s[3], "") if s[2] == 2 else ""
                        if sub in skills:
                            t = tier_of(it)
                            if t is None or t > tier or t < base_tier \
                                    or excluded(it) or not usable(it, cls):
                                continue
                            if slot in (15, 16) and (
                                    not comp_ok(cls, spec, slot, it)
                                    or not weapon_stat_ok(cls, spec, it)):
                                continue
                            pick = it
                            break
                    if pick and pick != base:
                        # verify base has no bonus (else no override needed)
                        bs = stats.get(base)
                        bsub = SUBCLASS_NAME.get(bs[3], "") if bs and bs[2] == 2 else ""
                        if bsub in skills:
                            continue
                        rows.append((cls, spec, race, tier, slot, pick,
                                     enchant_for(cls, spec, tier, slot, pick),
                                     "bisclassic", 0))
                        # a racial 2H MH switch needs its own explicit
                        # offhand-0 row for that race (the race-0 row does
                        # not cover it: FindRow matches the hire's race).
                        if slot == 15 and (stats.get(pick) or (0,) * 34)[4] \
                                == 17:
                            rows.append((cls, spec, race, tier, 16, 0, 0,
                                         "prev-tier", 0))
    # ---- review CSV
    names = {it: s[1] for it, s in stats.items()}
    with open(DATA / "hire_gear.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["class", "spec", "race", "tier", "slot", "item_id",
                    "item_name", "enchant", "source", "flags"])
        for r in sorted(rows):
            w.writerow([r[0], r[1], r[2], r[3], r[4], r[5],
                        names.get(r[5], "" if r[5] == 0 else "?"),
                        r[6], r[7], r[8]])
    print(f"rows={len(rows)} -> {DATA / 'hire_gear.csv'}")
    # ---- world SQL
    import datetime
    stamp = args.stamp or datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%d%H%M%S")
    sqlp = HERE / ".." / ".." / "data" / "sql" / "world" / f"{stamp}_world.sql"
    sqlp.parent.mkdir(parents=True, exist_ok=True)
    with open(sqlp, "w") as f:
        f.write(f"-- {stamp}_world.sql: static hire-gear sets (issue #490 PR 1).\n"
                f"-- Generated by tools/hire_gear/generate.py; hand edits welcome\n"
                f"-- (set flags bit1 on edited rows). Idempotent.\n"
                f"DELETE FROM `tortoise_bots_hire_gear`;\n"
                f"INSERT INTO `tortoise_bots_hire_gear` (`class`, `spec`, `race`,"
                f" `tier`, `slot`, `item_id`, `enchant_spell`, `flags`, `source`)"
                f" VALUES\n")
        vals = [f"({c},'{s}',{ra},{t},{sl},{it},{en},{fl},'{so}')".replace(
            "'prev-tier'", "'prev-tier'").replace("'turtle-sheet'", "'turtle-sheet'")
            .replace("'bisclassic'", "'bisclassic'").replace("'weights'", "'weights'")
                for c, s, ra, t, sl, it, en, so, fl in sorted(rows)]
        f.write(",\n".join(vals) + ";\n")
    print(f"sql rows={len(rows)} -> {sqlp}")
    # ---- docs page (frontmatter per tools/verify_okf.py REQUIRED_FIELDS)
    docp = HERE / ".." / ".." / "docs" / "reference" / "hire-gear.md"
    docp.parent.mkdir(parents=True, exist_ok=True)
    SLOT_NAME = {0: "Head", 1: "Neck", 2: "Shoulders", 14: "Back",
                 4: "Chest", 8: "Wrist", 9: "Hands", 5: "Waist", 6: "Legs",
                 7: "Feet", 10: "Finger 1", 11: "Finger 2",
                 13: "Trinket 1", 12: "Trinket 2", 15: "Main hand",
                 16: "Off hand", 17: "Ranged/Relic"}
    ename = {}
    for (spec_key, tstr, sstr), (spell, nm) in ench_map.items():
        ename[str(spell)] = nm
    L = ["---",
         "id: reference-hire-gear",
         "title: Hire Gear Sets (Generated)",
         "category: reference",
         "summary: Static level-60 hire sets per class/spec/tier from the offline generator.",
         "tags: [hire, gear, bis, generated]",
         "---",
         "",
         "# Hire gear sets (generated)",
         "",
         "Static level-60 hire sets per class/spec/tier. `*` = generator fallback",
         "(needs hand review). Item links open the Turtle database.",
         "Enchant is shown under the item when the tier allowlist has one.",
         ""]
    for cls, spec in SPECS:
        L.append(f"## {CLASS_NAME[cls]} {spec}")
        L.append("")
        L.append("| slot | " + " | ".join(f"t{t}" for t in range(1, 8)) + " |")
        L.append("| --- | " + " | ".join("---" for _ in range(1, 8)) + " |")
        for slot in range(0, 18+1):
            if slot in (3, 18):
                continue
            cells = []
            for tier in range(1, 8):
                hit = [r for r in rows if r[0] == cls and r[1] == spec
                       and r[2] == 0 and r[3] == tier and r[4] == slot]
                if not hit:
                    cells.append("-")
                    continue
                _, _, _, _, _, it, en, so, fl = hit[0]
                if not it:
                    cells.append("(empty)" + (" *" if fl & 1 else ""))
                    continue
                nm = names.get(it, "?")
                cell = f"[{nm}](https://database.turtlecraft.gg/?item={it})"
                if fl & 1:
                    cell += " *"
                if en:
                    cell += f"<br>{ename.get(str(en), en)}"
                cells.append(cell)
            L.append(f"| {SLOT_NAME[slot]} | " + " | ".join(cells) + " |")
        L.append("")
    docp.write_text("\n".join(L))
    print(f"docs -> {docp}")
    # fallback summary
    import collections
    fb = collections.Counter((r[0], r[1], r[3]) for r in rows if r[8] & 1)
    print(f"fallbacks={sum(fb.values())}")
    print(f"wic-fills={len(WIC_FILLS)} reverts={len(NO_DOWNGRADE_REVERTS)}")
    for cls, spec, tier, slot, wid in sorted(WIC_FILLS)[:40]:
        print(f"  wic {cls} {spec} t{tier} s{slot} {wid}")
    for cls, spec, tier, slot, kept, rk in sorted(NO_DOWNGRADE_REVERTS)[:40]:
        print(f"  revert {cls} {spec} t{tier} s{slot} keep={kept} newrk={rk}")
    # asserted self-checks (fail the run, don't just report)
    by_set = collections.defaultdict(list)
    for r in rows:
        if r[2] == 0 and r[5] != 0:
            by_set[(r[0], r[1], r[3])].append(r[5])
    dupes = {k: v for k, v in by_set.items() if len(v) != len(set(v))}
    assert not dupes, f"duplicate ids in sets: {sorted(dupes.items())[:5]}"
    print(f"self-check: no duplicate ids in any set ({len(by_set)} sets)")
    def content_count(cls, spec, tier, want):
        n = 0
        for r in rows:
            if r[0] == cls and r[1] == spec and r[3] == tier and r[2] == 0 \
                    and r[5] != 0:
                t = tiers.get(r[5])
                if t and t["tier"] != "" and int(t["tier"]) == want:
                    n += 1
        return n
    for cls, spec in SPECS:
        assert content_count(cls, spec, 5, 5) >= 3, \
            f"{cls} {spec} t5 has <3 AQ40 items"
        assert content_count(cls, spec, 6, 6) >= 3, \
            f"{cls} {spec} t6 has <3 Naxx items"
        assert content_count(cls, spec, 3, 3) >= 3, \
            f"{cls} {spec} t3 has <3 MC/Ony items"
    print("self-check: t5>=3 AQ40, t6>=3 Naxx, t3>=3 MC/Ony for all specs")
    _fury = {(r[4], r[5]) for r in rows if r[0] == 1 and r[1] == "fury"
             and r[3] == 3 and r[2] == 0}
    assert (5, 19137) in _fury, "fury t3 waist != 19137"
    assert (1, 18404) in _fury, "fury t3 neck != 18404"
    print("self-check: fury t3 waist=19137 neck=18404")
    # weapon-composition + enchant self-checks (fail the run).
    full = {}  # (cls, spec, race, tier, slot) -> (item, enchant)
    for r in rows:
        full[(r[0], r[1], r[2], r[3], r[4])] = (r[5], r[6])
    bad = []
    for cls, spec in SPECS:
        for tier in range(1, 8):
            mh, oh = full.get((cls, spec, 0, tier, 15), (0, 0))[0], \
                full.get((cls, spec, 0, tier, 16), (0, 0))[0]
            key = (cls, spec)
            if mh:
                inv = (stats.get(mh) or (0,) * 34)[4]
                if inv == 17 and oh:
                    bad.append(f"{cls} {spec} t{tier}: 2H {mh} + OH {oh}")
                if key in TWOH and inv != 17:
                    bad.append(f"{cls} {spec} t{tier}: 2H spec MH {mh}")
                if key in DUAL and inv not in (13, 21):
                    bad.append(f"{cls} {spec} t{tier}: DW MH {mh}")
                if key in SHIELD and inv not in (13, 21):
                    bad.append(f"{cls} {spec} t{tier}: shield MH {mh}")
            if oh:
                oinv = (stats.get(oh) or (0,) * 34)[4]
                if key in TWOH:
                    bad.append(f"{cls} {spec} t{tier}: 2H spec OH {oh}")
                if key in DUAL and oinv not in (13, 22):
                    bad.append(f"{cls} {spec} t{tier}: DW OH {oh}")
                if key in SHIELD and oinv != 14:
                    bad.append(f"{cls} {spec} t{tier}: shield OH {oh}")
            else:
                if key in DUAL | SHIELD and mh:
                    bad.append(f"{cls} {spec} t{tier}: empty OH")
            if mh and not weapon_stat_ok(cls, spec, mh):
                bad.append(f"{cls} {spec} t{tier}: MH {mh} fails stat gate")
            if oh and not weapon_stat_ok(cls, spec, oh):
                bad.append(f"{cls} {spec} t{tier}: OH {oh} fails stat gate")
            for sl in (15, 16):
                it, en = full.get((cls, spec, 0, tier, sl), (0, 0))
                if not it or not en:
                    continue
                inv = (stats.get(it) or (0,) * 34)[4]
                if inv == 23:
                    bad.append(f"{cls} {spec} t{tier} s{sl}: enchant on "
                               f"held {it}")
                if inv == 14:
                    m = ench_mask.get(en)
                    if m is not None and m[0] == 2:
                        bad.append(f"{cls} {spec} t{tier} s{sl}: weapon "
                                   f"enchant {en} on shield {it}")
    assert not bad, f"weapon/enchant violations: {bad[:10]}"
    print(f"self-check: weapon composition + enchants clean "
          f"({len(SPECS) * 7} sets)")
    # runtime contract: every set whose MH is inv 17 (any race row) must
    # carry an explicit slot-16 row with item_id 0, or the runtime keeps
    # today's offhand equipped. Covers race-override 2H switches too.
    missing_zero = []
    seen_sets = {(r[0], r[1], r[2], r[3]) for r in rows}
    for cls, spec, race, tier in seen_sets:
        mh_races = full.get((cls, spec, race, tier, 15), (0, 0))[0]
        if race != 0 and not mh_races:
            mh_races = full.get((cls, spec, 0, tier, 15), (0, 0))[0]
        if mh_races and (stats.get(mh_races) or (0,) * 34)[4] == 17:
            # runtime falls back per slot to the race-0 row
            oh = full.get((cls, spec, race, tier, 16)) \
                or full.get((cls, spec, 0, tier, 16))
            if not oh or oh[0] != 0:
                missing_zero.append(
                    f"{cls} {spec} r{race} t{tier}: 2H {mh_races} w/o "
                    f"slot-16 zero row")
    assert not missing_zero, f"missing offhand-0 rows: {missing_zero[:10]}"
    print(f"self-check: explicit slot-16 zero rows for all 2H sets")


if __name__ == "__main__":
    sys.exit(main())
