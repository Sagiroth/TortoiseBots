#!/usr/bin/env python3
"""Boss ability data for the dashboard: the CSV source, the served JSON, the citation gate.

Usage:
    python3 tools/boss_data.py build [--csv PATH] [--json PATH] [--names PATH]
    python3 tools/boss_data.py check [--csv PATH] [--json PATH] [--names PATH]
                                     [--core-dir DIR] [--kb-dir DIR]
                                     [--db-host H] [--db-user U] [--db-pass P]
                                     [--db-name tw_world] [--db-defaults FILE]
    python3 tools/boss_data.py coverage [<raid>] [--boss ENTRY] [--all] [--json]
                                     [--sql-dir DIR] [--db] [--fail-on-missing]

The CSV (data/boss/boss_abilities.csv) is the source; `build` writes
tools/observability/web/data/boss_abilities.json, which the dashboard serves at
/data/boss_abilities.json. The JSON is committed and `check` fails when it is
stale. Nothing here runs at server runtime: the module never reads this data and
the page reads only the JSON.

Rows (columns map,boss,row,key,a,b,c,d,source):
    ability  key=spell id   a=first ms after pull (0 = no timer)
                            b..c=repeat window ms (b=c for a constant; 0/0 = one-shot)
                            d=flags, ';'-joined:
                              p=<phase>    only in that phase (must be a phase row)
                              t=victim|random|self|aoe|ally
                              aura         leaves an aura on players
                              self         leaves an aura on the boss
                              mark         draws the marked icon (Gaze, Mark of Arlokk)
                              ground       persistent area spell (drawn on the ground)
                              cone         frontal cone (the coverage rule needs `front`)
                              tail         rear cone / tail sweep (needs `tail`)
                              int          cast time / channel, interruptible
                              summon=<creature entry>  must have an add row
                              hp<N>        one-shot below N percent health
                              ok=<reason>  the whole ability needs no answer
                              ok.<need>=<reason>  that one need needs no answer
    phase    key=name       a=rule: hp<N | aura=<spell id> | flag=<letter> | start
                            b=order (1 = checked first); a `start` rule always
                            matches and must be the last one (the pull phase)
    add      key=creature   a=summon|linked|ward|totem|trigger   b=kill priority (0 none)
    note     a=text         a mechanic the dashboard cannot see

`source` is one or more `path:line[-line]` citations separated by ';'. Script
paths are relative to the core checkout (--core-dir, default $TORTOISE_CORE_DIR
or ~/tortoise-wow); a path starting `data/` is relative to this repository (its
SQL migrations are not in the core). A knowledge-base citation is `kb:<file>#<heading-slug>` under
--kb-dir (default $TURTLE_KB_DIR or ~/TortoiseBots-audit/turtle-kb).

`ok` reasons are a closed list (see data/boss/README.md): tank, heal, phase,
script, and the DB-verified nokick/nodispel/nopurge, which are only valid on
their own need. An ok.<need> must name a need the row's own flags derive.

data/boss/raids.csv maps every map slug to its map id (the JSON `maps[]`), and
data/boss/code_answers.csv lists the answers that live in C++ rather than in a
world table (the JSON `codeAnswers[]`).

Checks:
    1. Every ability row cites the line that defines its spell (id or constant)
       and every timer value; phase hp/aura/flag rules and add entries likewise.
       A value of N is also accepted as N//1000 on a line using IN_MILLISECONDS.
    2. Ability spell ids, summon= entries and add entries are unique per boss;
       every summon= entry has an add row; every p= phase exists; every map slug
       has a raids.csv row and every raids.csv row has a boss.
    3. Every code answer cites a file of this repo that registers its name and
       names its boss entry; its spell/add key is a row of that boss; a `*` row
       is partly. No two rows make the same claim.
    4. The committed JSON equals a fresh build.
    5. --db: every spell id exists in spell_template and every add entry in
       creature_template; a `ground` flag has effect 27; a `cone` flag has
       implicit target 24; ok.interrupt=nokick is not kickable, ok.dispel=nodispel
       has dispel 0, ok.purge=nopurge has dispel 0. Without --db it prints
       "db check skipped".

Citations need a core checkout; CI has none and prints "core source check
skipped" instead. The same is true for the KB. Names come from the committed
data/boss/names.json (spell and creature names, optional cast ms), refreshed
from the world DB with `--db` on `build`.
"""

import argparse
import csv
import json
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CSV = ROOT / "data" / "boss" / "boss_abilities.csv"
DEFAULT_JSON = ROOT / "tools" / "observability" / "web" / "data" / "boss_abilities.json"
DEFAULT_NAMES = ROOT / "data" / "boss" / "names.json"
DEFAULT_RAIDS = ROOT / "data" / "boss" / "raids.csv"
DEFAULT_CODE_ANSWERS = ROOT / "data" / "boss" / "code_answers.csv"
DEFAULT_CORE = Path(os.environ.get("TORTOISE_CORE_DIR", Path.home() / "tortoise-wow"))
DEFAULT_KB = Path(
    os.environ.get("TURTLE_KB_DIR", Path.home() / "TortoiseBots-audit" / "turtle-kb")
)

CSV_COLUMNS = ["map", "boss", "row", "key", "a", "b", "c", "d", "source"]
RAID_COLUMNS = ["key", "map_id", "name"]
CODE_COLUMNS = ["map", "boss", "key", "need", "level", "name", "text", "source"]
ROW_KINDS = {"ability", "phase", "add", "note"}
TARGETS = {"victim", "random", "self", "aoe", "ally"}
ADD_ROLES = {"summon", "linked", "ward", "totem", "trigger"}
NEEDS = (
    "interrupt",
    "kill",
    "front",
    "tail",
    "avoid",
    "mark",
    "dispel",
    "purge",
    "resist",
    "classify",
)
OK_REASONS = ("tank", "heal", "phase", "script", "nokick", "nodispel", "nopurge")
# The DB-verified reasons are only valid on their own need.
OK_REASON_NEED = {"nokick": "interrupt", "nodispel": "dispel", "nopurge": "purge"}
# The flags a `flag=` code answer may scope to (§2.3).
ANSWER_FLAGS = ("ground", "aura", "self", "cone", "tail", "mark", "int")
FLAG_LETTERS = {
    "n": "UNIT_FLAG_NOT_SELECTABLE",
    "i": "UNIT_FLAG_IMMUNE_TO_PLAYER",
    "v": "VISIBILITY_OFF",  # the page sets v on a boss the reports left out; SetVisibility(VISIBILITY_OFF) is what drops it
    "h": None,  # flying/hovering state, no script literal to check
    "d": None,  # stand state dead (feign)
    "e": None,  # evading
}
PHASE_NAME_RE = re.compile(r"^[a-z0-9_]+$")
MAP_RE = re.compile(r"^[a-z0-9_]+$")
CITATION_RE = re.compile(r"^(?P<path>[^:]+):(?P<start>\d+)(?:-(?P<end>\d+))?$")
CONST_RE = re.compile(
    r"#define\s+([A-Z][A-Z0-9_]{2,})\s+(\d+)\b"
    r"|([A-Z][A-Z0-9_]{2,})\s*(?:=\s*|\{\s*)(\d+)\b"
)


class DataError(Exception):
    """A row that cannot be built; the message names the row and field."""


# --------------------------------------------------------------------------
# CSV


def read_rows(path):
    """Return CSV rows as dicts; every structural problem raises DataError."""
    with open(path, newline="", encoding="utf-8") as f:
        reader = csv.reader(f)
        try:
            header = next(reader)
        except StopIteration:
            raise DataError(f"{path}: empty file")
        if header != CSV_COLUMNS:
            raise DataError(
                f"{path}: header must be {','.join(CSV_COLUMNS)}, got {','.join(header)}"
            )
        rows = []
        for lineno, fields in enumerate(reader, start=2):
            if not fields or all(not f.strip() for f in fields):
                continue
            if len(fields) != len(CSV_COLUMNS):
                raise DataError(
                    f"{path}:{lineno}: {len(fields)} fields, expected {len(CSV_COLUMNS)}"
                )
            row = {k: v.strip() for k, v in zip(CSV_COLUMNS, fields)}
            row["_line"] = lineno
            rows.append(row)
    return rows


def _row_id(row):
    return f"line {row['_line']} ({row['row']} {row['key'] or row['boss']})"

def read_raids(path):
    """Return the raid rows as dicts {key, mapId, name}; every problem raises DataError."""
    try:
        handle = open(path, newline="", encoding="utf-8")
    except OSError:
        raise DataError(f"raids file not found: {path}")
    with handle:
        reader = csv.reader(handle)
        try:
            header = next(reader)
        except StopIteration:
            raise DataError(f"{path}: empty file")
        if header != RAID_COLUMNS:
            raise DataError(
                f"{path}: header must be {','.join(RAID_COLUMNS)}, got {','.join(header)}"
            )
        raids = []
        seen_keys = set()
        seen_ids = set()
        for lineno, fields in enumerate(reader, start=2):
            if not fields or all(not f.strip() for f in fields):
                continue
            if len(fields) != len(RAID_COLUMNS):
                raise DataError(
                    f"{path}:{lineno}: {len(fields)} fields, expected {len(RAID_COLUMNS)}"
                )
            key, map_id, name = (field.strip() for field in fields)
            if not MAP_RE.match(key):
                raise DataError(f"{path}:{lineno}: bad raid key {key!r}")
            if not re.fullmatch(r"\d+", map_id):
                raise DataError(f"{path}:{lineno}: map_id must be an integer, got {map_id!r}")
            if not name:
                raise DataError(f"{path}:{lineno}: raid {key!r} has no name")
            if key in seen_keys:
                raise DataError(f"{path}:{lineno}: duplicate raid key {key!r}")
            if int(map_id) in seen_ids:
                raise DataError(f"{path}:{lineno}: duplicate map id {map_id}")
            seen_keys.add(key)
            seen_ids.add(int(map_id))
            raids.append({"key": key, "mapId": int(map_id), "name": name})
    return raids


def read_code_answers(path):
    """Return the code-answer rows; every structural problem raises DataError."""
    try:
        handle = open(path, newline="", encoding="utf-8")
    except OSError:
        raise DataError(f"code answers file not found: {path}")
    with handle:
        reader = csv.reader(handle)
        try:
            header = next(reader)
        except StopIteration:
            raise DataError(f"{path}: empty file")
        if header != CODE_COLUMNS:
            raise DataError(
                f"{path}: header must be {','.join(CODE_COLUMNS)}, got {','.join(header)}"
            )
        answers = []
        seen = set()
        for lineno, fields in enumerate(reader, start=2):
            if not fields or all(not f.strip() for f in fields):
                continue
            if len(fields) != len(CODE_COLUMNS):
                raise DataError(
                    f"{path}:{lineno}: {len(fields)} fields, expected {len(CODE_COLUMNS)}"
                )
            where = f"{path}:{lineno}"
            answer = _code_answer({k: v.strip() for k, v in zip(CODE_COLUMNS, fields)}, where)
            duplicate = (
                answer["map"],
                answer["boss"],
                answer["key"],
                answer["need"],
                answer["name"],
            )
            if duplicate in seen:
                raise DataError(
                    f"{where}: duplicate code answer {answer['key']} {answer['need']} "
                    f"for {answer['map']} {answer['name']}"
                )
            seen.add(duplicate)
            answers.append(answer)
    return answers


def _code_answer(row, where):
    """One code-answer row, parsed; the fields follow §2.3 of the coverage spec."""
    if row["map"] != "*" and not MAP_RE.match(row["map"]):
        raise DataError(f"{where}: bad map {row['map']!r}; expected a raid key or *")
    if row["boss"] == "*":
        boss = 0
    elif re.fullmatch(r"\d+", row["boss"]):
        boss = int(row["boss"])
    else:
        raise DataError(f"{where}: boss must be a creature entry or *, got {row['boss']!r}")
    key = row["key"]
    spell = add = 0
    flag = ""
    if re.fullmatch(r"\d+", key):
        spell = int(key)
    elif key.startswith("add=") and re.fullmatch(r"\d+", key[4:]):
        add = int(key[4:])
    elif key.startswith("flag=") and key[5:]:
        flag = key[5:]
    else:
        raise DataError(
            f"{where}: key must be a spell id, add=<entry> or flag=<flag>, got {key!r}"
        )
    need = row["need"]
    if need not in NEEDS or need == "classify":
        raise DataError(
            f"{where}: need must be one of {', '.join(n for n in NEEDS if n != 'classify')}, "
            f"got {need!r}"
        )
    if row["level"] not in ("full", "partly"):
        raise DataError(f"{where}: level must be full or partly, got {row['level']!r}")
    for column in ("name", "text", "source"):
        if not row[column]:
            raise DataError(f"{where}: {column} is empty")
    source = [part.strip() for part in row["source"].split(";")]
    if not all(source):
        raise DataError(f"{where}: empty citation in {row['source']!r}")
    return {
        "map": row["map"],
        "boss": boss,
        "key": key,
        "spell": spell,
        "add": add,
        "flag": flag,
        "need": need,
        "level": row["level"],
        "name": row["name"],
        "text": row["text"],
        "source": source,
    }


def check_code_answers(answers, map_doc, bosses, root):
    """Yield the errors of §2.3 for the parsed code-answer rows."""
    table_maps = {raid["key"] for raid in map_doc}
    cache = {}
    for answer in answers:
        where = "code answer %s %s %s %s" % (
            answer["map"],
            answer["boss"] or "*",
            answer["key"],
            answer["need"],
        )
        if answer["map"] == "*" and answer["level"] != "partly":
            yield f"{where}: a * row must be partly, generic code is never a full answer"
        if answer["map"] != "*" and answer["map"] not in table_maps:
            yield f"{where}: map {answer['map']!r} is no raid of the boss table"
        if answer["map"] == "*" and answer["boss"]:
            yield f"{where}: a * map takes no boss entry"
        if answer["map"] != "*" and not answer["boss"] and not answer["flag"]:
            yield f"{where}: a * boss needs map * or a flag= key"
        if answer["flag"] and answer["flag"] not in ANSWER_FLAGS:
            yield f"{where}: unknown flag {answer['flag']!r}; expected one of {', '.join(ANSWER_FLAGS)}"

        boss = bosses.get(answer["boss"]) if answer["boss"] else None
        if answer["boss"] and boss is None:
            yield f"{where}: boss {answer['boss']} is not in the boss table"
        if answer["spell"]:
            if boss is None:
                yield f"{where}: a spell key needs a boss entry of the boss table"
            elif answer["spell"] not in {ability["sid"] for ability in boss["abilities"]}:
                yield f"{where}: spell {answer['spell']} is no ability of boss {answer['boss']}"
        if answer["add"]:
            if boss is None:
                yield f"{where}: an add key needs a boss entry of the boss table"
            elif not any(add["entry"] == answer["add"] for add in boss["adds"]):
                yield f"{where}: entry {answer['add']} is no add row of boss {answer['boss']}"

        lines = []
        missing = False
        for path in answer["source"]:
            resolved = Path(root) / path
            if not resolved.is_file():
                yield f"{where}: cited source {path} does not exist"
                missing = True
                continue
            lines.extend(read_lines(resolved, cache) or [])
        if missing:
            continue
        text = "\n".join(lines)
        if f'"{answer["name"]}"' not in text:
            yield f'{where}: the cited files do not register "{answer["name"]}"'
        if answer["boss"]:
            consts = constant_values(lines)
            if not _has_id(list(enumerate(lines, start=1)), answer["boss"], consts):
                yield f"{where}: the cited files do not name creature {answer['boss']}"


def _int_field(row, name, *, minimum=0):
    text = row[name]
    if not re.fullmatch(r"-?\d+", text):
        raise DataError(f"{_row_id(row)}: {name} must be an integer, got {text!r}")
    value = int(text)
    if value < minimum:
        raise DataError(f"{_row_id(row)}: {name} must be >= {minimum}, got {value}")
    return value


def parse_flags(text):
    flags = {
        "phase": "",
        "target": "",
        "aura": False,
        "self": False,
        "mark": False,
        "ground": False,
        "cone": False,
        "tail": False,
        "int": False,
        "summon": 0,
        "hp": 0,
        "ok": "",
        "okNeeds": {},
    }
    seen = set()
    if not text:
        return flags
    for token in text.split(";"):
        token = token.strip()
        if not token:
            raise DataError(f"empty flag in {text!r}")
        if token in seen:
            raise DataError(f"duplicate flag {token!r} in {text!r}")
        seen.add(token)
        if token.startswith("p="):
            name = token[2:]
            if not PHASE_NAME_RE.match(name):
                raise DataError(f"bad phase name {name!r} in {text!r}")
            flags["phase"] = name
        elif token.startswith("t="):
            target = token[2:]
            if target not in TARGETS:
                raise DataError(f"bad target {target!r} in {text!r}")
            flags["target"] = target
        elif token in ("aura", "self", "mark", "ground", "cone", "tail", "int"):
            flags[token] = True
        elif token.startswith("summon="):
            value = token[7:]
            if not re.fullmatch(r"\d+", value):
                raise DataError(f"bad summon entry {value!r} in {text!r}")
            flags["summon"] = int(value)
        elif token.startswith("ok="):
            flags["ok"] = token[3:]
        elif token.startswith("ok."):
            need, sep, reason = token[3:].partition("=")
            if not sep:
                raise DataError(f"ok.{need} needs =<reason> in {text!r}")
            if need in flags["okNeeds"]:
                raise DataError(f"duplicate ok.{need} in {text!r}")
            flags["okNeeds"][need] = reason
        elif token.startswith("hp"):
            value = token[2:]
            if not re.fullmatch(r"\d+", value) or not 1 <= int(value) <= 99:
                raise DataError(f"bad hp flag {token!r} in {text!r}")
            flags["hp"] = int(value)
        else:
            raise DataError(f"unknown flag {token!r} in {text!r}")
    _check_ok(flags, text)
    return flags


def derived_needs(flags):
    """The needs a row's own flags derive, in the coverage rule's order."""
    needs = []
    if flags["int"]:
        needs.append("interrupt")
    if flags["summon"]:
        needs.append("kill")
    if flags["cone"]:
        needs.append("front")
    if flags["tail"]:
        needs.append("tail")
    if flags["ground"]:
        needs.append("avoid")
    if flags["mark"]:
        needs.append("mark")
    if flags["aura"]:
        needs.append("dispel")
    if flags["self"]:
        needs.append("purge")
    return needs


def spells_with_ok(rows, need, reason):
    """Spell ids whose row marks one need `ok.<need>=<reason>`."""
    return {
        int(row["key"])
        for row in rows
        if row["row"] == "ability"
        and parse_flags(row["d"])["okNeeds"].get(need) == reason
    }


def _check_ok(flags, text):
    """The `ok=`/`ok.<need>=` rules that need only this row's flags."""
    ok = flags["ok"]
    ok_needs = flags["okNeeds"]
    if ok and ok_needs:
        raise DataError(f"ok={ok} cannot be combined with ok.<need> in {text!r}")
    if ok:
        if ok not in OK_REASONS:
            raise DataError(
                f"unknown ok reason {ok!r} in {text!r}; expected one of {', '.join(OK_REASONS)}"
            )
        if ok in OK_REASON_NEED:
            raise DataError(f"ok={ok} is only valid as ok.{OK_REASON_NEED[ok]}={ok} in {text!r}")
    for need, reason in ok_needs.items():
        if reason not in OK_REASONS:
            raise DataError(
                f"unknown ok reason {reason!r} in {text!r}; expected one of {', '.join(OK_REASONS)}"
            )
        if need not in derived_needs(flags):
            raise DataError(f"ok.{need} on a row whose flags do not derive {need} in {text!r}")
        owner = OK_REASON_NEED.get(reason)
        if owner and owner != need:
            raise DataError(
                f"ok.{need}={reason} is only valid as ok.{owner}={reason} in {text!r}"
            )
    reasons = [ok, *ok_needs.values()]
    if "tank" in reasons and flags["target"] not in ("victim", "random"):
        raise DataError(f"ok reason tank needs a t=victim or t=random row in {text!r}")


def parse_phase_rule(text):
    if text == "start":
        return {"kind": "start"}
    match = re.fullmatch(r"hp<(\d+)", text)
    if match:
        hp = int(match.group(1))
        if not 1 <= hp <= 99:
            raise DataError(f"phase hp rule must be 1..99, got {hp}")
        return {"kind": "hp", "hp": hp}
    if text.startswith("aura="):
        value = text[5:]
        if not re.fullmatch(r"\d+", value):
            raise DataError(f"bad aura rule {text!r}")
        return {"kind": "aura", "aura": int(value)}
    if text.startswith("flag="):
        letter = text[5:]
        if letter not in FLAG_LETTERS:
            raise DataError(f"bad flag rule {text!r}; expected one of {sorted(FLAG_LETTERS)}")
        return {"kind": "flag", "flag": letter}
    raise DataError(f"bad phase rule {text!r}")


def read_names(path):
    try:
        with open(path, encoding="utf-8") as f:
            names = json.load(f)
    except FileNotFoundError:
        raise DataError(f"names file not found: {path}")
    spells = names.get("spells")
    creatures = names.get("creatures")
    if not isinstance(spells, dict) or not isinstance(creatures, dict):
        raise DataError(f"{path}: expected objects 'spells' and 'creatures'")
    return names


# --------------------------------------------------------------------------
# Citations


def read_lines(path, cache):
    """Lines of a citation file, cached; None when the file does not exist."""
    key = str(path)
    if key not in cache:
        try:
            cache[key] = Path(path).read_text(encoding="utf-8", errors="replace").splitlines()
        except OSError:
            cache[key] = None
    return cache[key]


def constant_values(lines):
    """Map constant name -> integer for `#define X 1`, `X = 1`, `X{ 1 }`."""
    values = {}
    for line in lines or ():
        for m in CONST_RE.finditer(line):
            name = m.group(1) or m.group(3)
            value = m.group(2) or m.group(4)
            if name and value:
                values[name] = int(value)
    return values


def parse_source(row):
    """Return a list of (kind, path, start, end) citations; 'kind' is 'core' or 'kb'."""
    text = row["source"]
    if not text:
        raise DataError(f"{_row_id(row)}: empty source")
    citations = []
    for part in text.split(";"):
        part = part.strip()
        if not part:
            raise DataError(f"{_row_id(row)}: empty citation in {text!r}")
        if part.startswith("kb:"):
            rest = part[3:]
            if "#" not in rest:
                raise DataError(f"{_row_id(row)}: kb citation needs #heading: {part!r}")
            name, anchor = rest.split("#", 1)
            if not name or not anchor:
                raise DataError(f"{_row_id(row)}: bad kb citation {part!r}")
            citations.append(("kb", name, anchor, 0))
            continue
        match = CITATION_RE.match(part)
        if not match:
            raise DataError(f"{_row_id(row)}: bad citation {part!r}; expected path:line[-line]")
        start = int(match.group("start"))
        end = int(match.group("end") or start)
        if end < start:
            raise DataError(f"{_row_id(row)}: citation range {part!r} is reversed")
        citations.append(("core", match.group("path"), start, end))
    return citations


def heading_slugs(text):
    slugs = set()
    for line in text.splitlines():
        match = re.match(r"^#{1,6}\s+(.*)$", line)
        if not match:
            continue
        slug = match.group(1).strip().lower()
        slug = re.sub(r"[^\w\s-]", "", slug)
        slugs.add(re.sub(r"\s+", "-", slug))
    return slugs


def _has_number(lines, number):
    token = re.compile(rf"(?<!\d){number}(?!\d)")
    for _, text in lines:
        if token.search(text):
            return True
        if number % 1000 == 0 and "IN_MILLISECONDS" in text:
            if re.search(rf"(?<!\d){number // 1000}(?!\d)", text):
                return True
    return False


def _has_id(lines, ident, consts):
    token = re.compile(rf"(?<!\d){ident}(?!\d)")
    for _, text in lines:
        if token.search(text):
            return True
        for name, value in consts.items():
            if value == ident and re.search(rf"\b{re.escape(name)}\b", text):
                return True
    return False


def check_citations(row, citations, core, kb, cache):
    """Yield error messages for one row's citations."""
    resolved = []  # (os path, [(lineno, text)])
    for kind, path, start, end in citations:
        if kind == "kb":
            if not kb.is_dir():
                continue
            kb_path = kb / path
            lines = read_lines(kb_path, cache)
            if lines is None:
                yield f"{_row_id(row)}: missing knowledge-base file {kb_path}"
                continue
            if start not in heading_slugs("\n".join(lines)):
                yield f"{_row_id(row)}: {kb_path} has no heading #{start}"
            continue
        core_path = ROOT / path if path.startswith("data/") else core / path
        lines = read_lines(core_path, cache)
        if lines is None:
            continue
        if end > len(lines):
            yield f"{_row_id(row)}: {path}:{start}-{end} is past end of file ({len(lines)})"
            continue
        resolved.append((core_path, list(enumerate(lines[start - 1 : end], start=start))))

    if not resolved:
        return

    flat = [line for _, lines in resolved for line in lines]
    consts = {}
    for core_path, _ in resolved:
        consts.update(constant_values(read_lines(core_path, cache)))

    kind = row["row"]
    if kind == "ability":
        sid = int(row["key"])
        if not _has_id(flat, sid, consts):
            yield f"{_row_id(row)}: cited lines do not define spell {sid} (id or constant name)"
        for field in ("a", "b", "c"):
            value = int(row[field])
            if value and not _has_number(flat, value):
                yield f"{_row_id(row)}: cited lines do not contain {field}={value}"
    elif kind == "phase":
        rule = parse_phase_rule(row["a"])
        if rule["kind"] == "hp" and not _has_number(flat, rule["hp"]):
            yield f"{_row_id(row)}: cited lines do not contain hp={rule['hp']}"
        elif rule["kind"] == "aura" and not _has_id(flat, rule["aura"], consts):
            yield f"{_row_id(row)}: cited lines do not define aura spell {rule['aura']}"
        elif rule["kind"] == "flag":
            name = FLAG_LETTERS[rule["flag"]]
            if name and not any(name in text for _, text in flat):
                yield f"{_row_id(row)}: cited lines do not mention {name}"
    elif kind == "add":
        entry = int(row["key"])
        if not _has_id(flat, entry, consts):
            yield f"{_row_id(row)}: cited lines do not define creature {entry} (id or constant name)"


# --------------------------------------------------------------------------
# Model


def make_bosses(rows, names, raids=None):
    """Validate rows and return (maps, bosses, abilities_by_boss, phase_names)."""
    if raids is None:
        raids = read_raids(DEFAULT_RAIDS)
    bosses = {}
    order = []
    maps = []
    ability_rows = {}
    phase_names = {}
    spell_name = {int(k): v for k, v in names["spells"].items()}
    creature_name = {int(k): v for k, v in names["creatures"].items()}

    for row in rows:
        kind = row["row"]
        if kind not in ROW_KINDS:
            raise DataError(f"{_row_id(row)}: unknown row kind {kind!r}")
        if not MAP_RE.match(row["map"]):
            raise DataError(f"{_row_id(row)}: bad map slug {row['map']!r}")
        if not re.fullmatch(r"\d+", row["boss"]):
            raise DataError(f"{_row_id(row)}: boss must be a creature entry, got {row['boss']!r}")
        entry = int(row["boss"])
        if entry not in creature_name or not creature_name[entry].get("name"):
            raise DataError(f"{_row_id(row)}: no creature name for boss {entry} in names.json")

        if row["map"] not in maps:
            maps.append(row["map"])
        if entry not in bosses:
            bosses[entry] = {
                "entry": entry,
                "name": creature_name[entry]["name"],
                "map": row["map"],
                "order": len(order) + 1,
                "abilities": [],
                "phases": [],
                "adds": [],
                "notes": [],
            }
            order.append(entry)
            ability_rows[entry] = {}
            phase_names[entry] = set()
        boss = bosses[entry]
        if boss["map"] != row["map"]:
            raise DataError(f"{_row_id(row)}: boss {entry} is already under map {boss['map']}")

        if kind == "ability":
            sid_text = row["key"]
            if not re.fullmatch(r"\d+", sid_text):
                raise DataError(f"{_row_id(row)}: ability key must be a spell id")
            sid = int(sid_text)
            if sid in ability_rows[entry]:
                raise DataError(f"{_row_id(row)}: duplicate ability {sid} for boss {entry}")
            if sid not in spell_name or not spell_name[sid].get("name"):
                raise DataError(f"{_row_id(row)}: no spell name for {sid} in names.json")
            first = _int_field(row, "a")
            rep_min = _int_field(row, "b")
            rep_max = _int_field(row, "c")
            if rep_min and not rep_max:
                raise DataError(f"{_row_id(row)}: b set but c empty")
            if rep_max and not rep_min:
                raise DataError(f"{_row_id(row)}: c set but b empty")
            if rep_min > rep_max:
                raise DataError(f"{_row_id(row)}: b={rep_min} > c={rep_max}")
            flags = parse_flags(row["d"])
            ability = {
                "sid": sid,
                "name": spell_name[sid]["name"],
                "castMs": int(spell_name[sid].get("castMs") or 0),
                "a": first,
                "b": rep_min,
                "c": rep_max,
                **flags,
            }
            boss["abilities"].append(ability)
            ability_rows[entry][sid] = ability

        elif kind == "phase":
            name = row["key"]
            if not PHASE_NAME_RE.match(name):
                raise DataError(f"{_row_id(row)}: bad phase name {name!r}")
            phase_order = _int_field(row, "b", minimum=1)
            rule = parse_phase_rule(row["a"])
            for other in boss["phases"]:
                if other["order"] == phase_order:
                    raise DataError(f"{_row_id(row)}: phase order {phase_order} already used")
                if other["name"] == name and other["rule"] == rule:
                    raise DataError(f"{_row_id(row)}: duplicate phase rule for {name!r}")
            boss["phases"].append({"name": name, "order": phase_order, "rule": rule})
            phase_names[entry].add(name)

        elif kind == "add":
            add_text = row["key"]
            if not re.fullmatch(r"\d+", add_text):
                raise DataError(f"{_row_id(row)}: add key must be a creature entry")
            add_entry = int(add_text)
            if any(a["entry"] == add_entry for a in boss["adds"]):
                raise DataError(f"{_row_id(row)}: duplicate add {add_entry} for boss {entry}")
            if add_entry not in creature_name or not creature_name[add_entry].get("name"):
                raise DataError(f"{_row_id(row)}: no creature name for add {add_entry}")
            role = row["a"]
            if role not in ADD_ROLES:
                raise DataError(f"{_row_id(row)}: add role must be one of {sorted(ADD_ROLES)}")
            priority = _int_field(row, "b") if row["b"] else 0
            if row["c"] or row["d"]:
                raise DataError(f"{_row_id(row)}: add rows use only a and b")
            boss["adds"].append(
                {
                    "entry": add_entry,
                    "name": creature_name[add_entry]["name"],
                    "role": role,
                    "priority": priority,
                }
            )

        else:  # note
            if not row["a"]:
                raise DataError(f"{_row_id(row)}: note text is empty")
            if row["b"] or row["c"] or row["d"]:
                raise DataError(f"{_row_id(row)}: note rows use only a and source")
            boss["notes"].append(row["a"])

    # Cross-row rules that need the whole boss table.
    for entry, boss in bosses.items():
        ability_ids = set(ability_rows[entry])
        phases = sorted(boss["phases"], key=lambda p: p["order"])
        for index, phase in enumerate(phases):
            if phase["rule"]["kind"] == "aura" and phase["rule"]["aura"] not in ability_ids:
                raise DataError(
                    f"boss {entry}: phase {phase['name']!r} aura spell {phase['rule']['aura']} "
                    "has no ability row"
                )
            if phase["rule"]["kind"] == "start" and index != len(phases) - 1:
                raise DataError(f"boss {entry}: a start rule must be the last phase rule")
        if sum(1 for p in phases if p["rule"]["kind"] == "start") > 1:
            raise DataError(f"boss {entry}: more than one start phase rule")
        for ability in boss["abilities"]:
            if ability["phase"] and ability["phase"] not in phase_names[entry]:
                raise DataError(
                    f"boss {entry}: ability {ability['sid']} names phase {ability['phase']!r} "
                    "with no phase row"
                )
            if ability["summon"]:
                if not any(a["entry"] == ability["summon"] for a in boss["adds"]):
                    raise DataError(
                        f"boss {entry}: ability {ability['sid']} summons {ability['summon']} "
                        "with no add row"
                    )
        seen_summons = set()
        for ability in boss["abilities"]:
            if ability["summon"]:
                if ability["summon"] in seen_summons:
                    raise DataError(
                        f"boss {entry}: summon {ability['summon']} used by two ability rows"
                    )
                seen_summons.add(ability["summon"])
        phase_auras = {
            phase["rule"]["aura"] for phase in phases if phase["rule"]["kind"] == "aura"
        }
        for ability in boss["abilities"]:
            reasons = [ability["ok"], *ability["okNeeds"].values()]
            if (
                "phase" in reasons
                and not ability["hp"]
                and ability["sid"] not in phase_auras
            ):
                raise DataError(
                    f"boss {entry}: ability {ability['sid']} says ok reason phase but it is "
                    "no aura= rule of the boss and carries no hp<N"
                )
        boss["phases"] = phases

    by_key = {raid["key"]: raid for raid in raids}
    for key in maps:
        if key not in by_key:
            raise DataError(f"map {key!r} has no row in raids.csv")
    for raid in raids:
        if raid["key"] not in maps:
            raise DataError(f"raids.csv row {raid['key']!r} has no boss in the CSV")
    map_doc = [
        {
            "key": m,
            "mapId": by_key[m]["mapId"],
            "name": by_key[m]["name"],
            "bosses": [e for e in order if bosses[e]["map"] == m],
        }
        for m in maps
    ]
    return map_doc, bosses


def build_document(rows, names, raids=None, code_answers=None, root=None):
    map_doc, bosses = make_bosses(rows, names, raids)
    if code_answers is None:
        code_answers = read_code_answers(DEFAULT_CODE_ANSWERS)
    errors = list(check_code_answers(code_answers, map_doc, bosses, root or ROOT))
    if errors:
        raise DataError("\n".join(errors))
    doc = {
        "version": 1,
        "note": "Generated by tools/boss_data.py build from data/boss/boss_abilities.csv; do not edit.",
        "doc": {
            "ability": "a = ms after pull before the first cast (0 = no timer); b..c = repeat window ms (0/0 = one-shot); castMs 0 = unknown",
            "phase": "rules are checked in order, first match wins; hp rules latch; start always matches and is last",
        },
        "maps": map_doc,
        "bosses": {str(e): bosses[e] for e in sorted(bosses, key=lambda e: bosses[e]["order"])},
        "codeAnswers": code_answers,
    }
    return doc


def dump_json(doc):
    return json.dumps(doc, indent=2, ensure_ascii=False) + "\n"


# --------------------------------------------------------------------------
# Database


def db_connect(args):
    import pymysql

    kw = {
        "host": args.db_host,
        "user": args.db_user,
        "password": args.db_pass,
        "database": args.db_name,
    }
    if args.db_defaults:
        kw["read_default_file"] = args.db_defaults
    return pymysql.connect(**kw)


def missing_rows(connect, spell_ids, creature_ids):
    """Entry ids absent from spell_template / creature_template."""
    missing_spells = []
    missing_creatures = []
    conn = connect()
    try:
        cur = conn.cursor()
        if spell_ids:
            cur.execute(
                "SELECT entry FROM spell_template WHERE entry IN (%s)"
                % ",".join(["%s"] * len(spell_ids)),
                tuple(sorted(spell_ids)),
            )
            found = {row[0] for row in cur.fetchall()}
            missing_spells = sorted(set(spell_ids) - found)
        if creature_ids:
            cur.execute(
                "SELECT entry FROM creature_template WHERE entry IN (%s)"
                % ",".join(["%s"] * len(creature_ids)),
                tuple(sorted(creature_ids)),
            )
            found = {row[0] for row in cur.fetchall()}
            missing_creatures = sorted(set(creature_ids) - found)
    finally:
        conn.close()
    return missing_spells, missing_creatures


def not_persistent_area(connect, spell_ids):
    """Spell ids flagged `ground` whose effects contain no PERSISTENT_AREA_AURA (27)."""
    if not spell_ids:
        return []
    conn = connect()
    try:
        cur = conn.cursor()
        cur.execute(
            "SELECT entry, effect1, effect2, effect3 FROM spell_template WHERE entry IN (%s)"
            % ",".join(["%s"] * len(spell_ids)),
            tuple(sorted(spell_ids)),
        )
        rows = {row[0]: row[1:] for row in cur.fetchall()}
    finally:
        conn.close()
    return sorted(sid for sid in spell_ids if 27 not in rows.get(sid, ()))


def spell_facts(connect, spell_ids):
    """preventionType, interruptFlags, castingTimeIndex and dispel per spell id."""
    if not spell_ids:
        return {}
    conn = connect()
    try:
        cur = conn.cursor()
        cur.execute(
            "SELECT entry, preventionType, interruptFlags, castingTimeIndex, dispel "
            "FROM spell_template WHERE entry IN (%s)"
            % ",".join(["%s"] * len(spell_ids)),
            tuple(sorted(spell_ids)),
        )
        rows = cur.fetchall()
    finally:
        conn.close()
    return {
        row[0]: {
            "preventionType": row[1],
            "interruptFlags": row[2],
            "castingTimeIndex": row[3],
            "dispel": row[4],
        }
        for row in rows
    }


def cone_spells_without_cone_target(connect, spell_ids):
    """Spell ids flagged `cone` whose effects contain no TARGET_UNIT_CONE_ENEMY (24)."""
    if not spell_ids:
        return []
    conn = connect()
    try:
        cur = conn.cursor()
        cur.execute(
            "SELECT entry, effectImplicitTargetA1, effectImplicitTargetB1, "
            "effectImplicitTargetA2, effectImplicitTargetB2, "
            "effectImplicitTargetA3, effectImplicitTargetB3 "
            "FROM spell_template WHERE entry IN (%s)"
            % ",".join(["%s"] * len(spell_ids)),
            tuple(sorted(spell_ids)),
        )
        rows = {row[0]: row[1:] for row in cur.fetchall()}
    finally:
        conn.close()
    return sorted(sid for sid in spell_ids if 24 not in rows.get(sid, ()))


def refresh_names(connect, rows, names):
    """Fill missing names (and spell cast ms) from the world DB."""
    spell_ids = {int(r["key"]) for r in rows if r["row"] == "ability"}
    creature_ids = {int(r["boss"]) for r in rows if r["row"]}
    creature_ids |= {int(r["key"]) for r in rows if r["row"] == "add"}
    creature_ids |= {
        int(t[7:]) for r in rows if r["row"] == "ability" for t in r["d"].split(";") if t.startswith("summon=")
    }
    conn = connect()
    try:
        cur = conn.cursor()
        if spell_ids:
            cur.execute(
                "SELECT entry, name FROM spell_template WHERE entry IN (%s)"
                % ",".join(["%s"] * len(spell_ids)),
                tuple(sorted(spell_ids)),
            )
            for entry, name in cur.fetchall():
                slot = names["spells"].setdefault(str(entry), {})
                slot["name"] = name
                slot.setdefault("castMs", 0)
        if creature_ids:
            cur.execute(
                "SELECT entry, name FROM creature_template WHERE entry IN (%s)"
                % ",".join(["%s"] * len(creature_ids)),
                tuple(sorted(creature_ids)),
            )
            for entry, name in cur.fetchall():
                names["creatures"].setdefault(str(entry), {})["name"] = name
    finally:
        conn.close()
    return names


# --------------------------------------------------------------------------
# Commands


def add_common(parser):
    parser.add_argument("--csv", default=str(DEFAULT_CSV))
    parser.add_argument("--json", default=str(DEFAULT_JSON))
    parser.add_argument("--names", default=str(DEFAULT_NAMES))
    parser.add_argument("--raids", default=str(DEFAULT_RAIDS))
    parser.add_argument("--code-answers", default=str(DEFAULT_CODE_ANSWERS))
    parser.add_argument("--repo-root", default=str(ROOT))
    parser.add_argument("--core-dir", default=str(DEFAULT_CORE))
    parser.add_argument("--kb-dir", default=str(DEFAULT_KB))


def add_db(parser):
    parser.add_argument("--db-host", default=os.environ.get("BOSS_DATA_DB_HOST", "localhost"))
    parser.add_argument("--db-user", default=os.environ.get("BOSS_DATA_DB_USER", "mangos"))
    parser.add_argument("--db-pass", default=os.environ.get("BOSS_DATA_DB_PASS", "mangos"))
    parser.add_argument("--db-name", default=os.environ.get("BOSS_DATA_DB_NAME", "tw_world"))
    parser.add_argument("--db-defaults", default=os.environ.get("BOSS_DATA_DB_DEFAULTS"))


def cmd_build(args):
    rows = read_rows(Path(args.csv))
    names = read_names(Path(args.names))
    if args.db:
        names = refresh_names(db_connect(args), rows, names)
        with open(args.names, "w", encoding="utf-8") as f:
            f.write(json.dumps(names, indent=2, sort_keys=True) + "\n")
    doc = build_document(
        rows,
        names,
        read_raids(Path(args.raids)),
        read_code_answers(Path(args.code_answers)),
        args.repo_root,
    )
    with open(args.json, "w", encoding="utf-8") as f:
        f.write(dump_json(doc))
    print(f"wrote {args.json}: {len(doc['bosses'])} bosses")
    return 0


def validate(args):
    """Return (errors, notes) for the CSV, committed JSON and DB."""
    errors = []
    notes = []
    csv_path = Path(args.csv)
    races = None
    try:
        rows = read_rows(csv_path)
    except DataError as exc:
        return [str(exc)], notes
    try:
        names = read_names(Path(args.names))
    except DataError as exc:
        return [str(exc)], notes
    try:
        raids = read_raids(Path(args.raids))
        code_answers = read_code_answers(Path(args.code_answers))
    except DataError as exc:
        return [str(exc)], notes
    try:
        doc = build_document(rows, names, raids, code_answers, args.repo_root)
    except DataError as exc:
        return [str(exc)], notes

    core = Path(args.core_dir)
    kb = Path(args.kb_dir)
    if not core.is_dir():
        notes.append(f"core source check skipped ({core} not found)")
    if not kb.is_dir():
        notes.append(f"kb check skipped ({kb} not found)")
    cache = {}
    for row in rows:
        try:
            citations = parse_source(row)
        except DataError as exc:
            errors.append(str(exc))
            continue
        if not core.is_dir():
            continue
        for message in check_citations(row, citations, core, kb, cache):
            errors.append(message)

    if not errors:
        try:
            committed = Path(args.json).read_text(encoding="utf-8")
        except FileNotFoundError:
            errors.append(f"committed JSON not found: {args.json}; run build")
        else:
            if committed != dump_json(doc):
                errors.append(
                    f"{args.json} differs from a fresh build of {args.csv}; run "
                    "tools/boss_data.py build"
                )

    if args.db:
        spell_ids = {int(r["key"]) for r in rows if r["row"] == "ability"}
        creature_ids = {int(r["key"]) for r in rows if r["row"] == "add"}
        ground_ids = {
            int(r["key"]) for r in rows if r["row"] == "ability" and "ground" in r["d"].split(";")
        }
        cone_ids = {
            int(r["key"]) for r in rows if r["row"] == "ability" and "cone" in r["d"].split(";")
        }
        nokick_ids = spells_with_ok(rows, "interrupt", "nokick")
        nodispel_ids = spells_with_ok(rows, "dispel", "nodispel")
        nopurge_ids = spells_with_ok(rows, "purge", "nopurge")
        try:
            connect = lambda: db_connect(args)
            missing_spells, missing_creatures = missing_rows(connect, spell_ids, creature_ids)
            bad_ground = not_persistent_area(connect, ground_ids)
            bad_cone = cone_spells_without_cone_target(connect, cone_ids)
            facts = spell_facts(connect, nokick_ids | nodispel_ids | nopurge_ids)
        except Exception as exc:  # connection/driver errors are check failures
            errors.append(f"db check failed: {exc}")
        else:
            for sid in missing_spells:
                errors.append(f"spell {sid} is not in spell_template")
            for entry in missing_creatures:
                errors.append(f"creature {entry} is not in creature_template")
            for sid in bad_ground:
                errors.append(f"spell {sid} is flagged ground but has no persistent-area effect (27)")
            for sid in bad_cone:
                errors.append(f"spell {sid} is flagged cone but has no cone target (24)")
            for sid in sorted(nokick_ids):
                info = facts.get(sid)
                if (
                    info
                    and info["preventionType"] == 1
                    and info["interruptFlags"] & 0x02
                    and info["castingTimeIndex"] > 1
                ):
                    errors.append(f"spell {sid} is kickable, so ok.interrupt=nokick is wrong")
            for sid in sorted(nodispel_ids):
                info = facts.get(sid)
                if info and info["dispel"]:
                    errors.append(f"spell {sid} can be dispelled, so ok.dispel=nodispel is wrong")
            for sid in sorted(nopurge_ids):
                info = facts.get(sid)
                if info and info["dispel"]:
                    errors.append(f"spell {sid} can be purged, so ok.purge=nopurge is wrong")
    else:
        notes.append("db check skipped")

    return errors, notes


def cmd_check(args):
    errors, notes = validate(args)
    for note in notes:
        print(note)
    if errors:
        for message in errors:
            print(f"FAIL {message}")
        print(f"boss data: {len(errors)} problem(s)")
        return 1
    rows = read_rows(Path(args.csv))
    bosses = {int(r["boss"]) for r in rows}
    abilities = sum(1 for r in rows if r["row"] == "ability")
    print(f"boss data ok: {len(bosses)} bosses, {abilities} abilities")
    return 0


def cmd_coverage_hook(args):
    """Hand the report to boss_coverage.py, imported here so the other commands run alone."""
    from boss_coverage import cmd_coverage

    return cmd_coverage(args)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)
    build = sub.add_parser("build", help="write the JSON served by the dashboard")
    add_common(build)
    add_db(build)
    build.add_argument("--db", action="store_true", help="refresh names from the world DB")
    build.set_defaults(func=cmd_build)
    check = sub.add_parser("check", help="validate citations, uniqueness, freshness and the DB")
    add_common(check)
    add_db(check)
    check.add_argument("--db", action="store_true", help="also check entries in the world DB")
    check.set_defaults(func=cmd_check)
    coverage = sub.add_parser(
        "coverage", help="show, boss by boss, which abilities the bots have no answer for"
    )
    coverage.add_argument("raid", nargs="?", default="", help="raid key or map id (default: all)")
    coverage.add_argument("--boss", type=int, default=0, help="only this creature entry")
    coverage.add_argument("--all", action="store_true", help="print every line, not only the gaps")
    coverage.add_argument("--json", action="store_true", help="print the machine form")
    coverage.add_argument("--csv", default=str(DEFAULT_CSV))
    coverage.add_argument("--names", default=str(DEFAULT_NAMES))
    coverage.add_argument("--raids", default=str(DEFAULT_RAIDS))
    coverage.add_argument("--code-answers", default=str(DEFAULT_CODE_ANSWERS))
    coverage.add_argument("--sql-dir", default=str(ROOT / "data" / "sql" / "world"))
    coverage.add_argument("--repo-root", default=str(ROOT))
    coverage.add_argument("--fail-on-missing", action="store_true",
                          help="exit 1 when any line is missing")
    add_db(coverage)
    coverage.add_argument("--db", action="store_true", help="read the rows from the world DB")
    coverage.set_defaults(func=cmd_coverage_hook)
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
