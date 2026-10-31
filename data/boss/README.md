# Boss ability data

The dashboard's boss panel, phase label, add list and map links read one
generated file, `tools/observability/web/data/boss_abilities.json` (served at
`/data/boss_abilities.json`). Edit the CSV here, never the JSON:

```
python3 tools/boss_data.py build     # CSV + names.json + raids.csv + code_answers.csv -> JSON
python3 tools/boss_data.py check     # citations, uniqueness, answers, JSON freshness
python3 tools/boss_data.py check --db  # also: ids and flags against tw_world
python3 tools/boss_data.py coverage 309  # which abilities the bots have no answer for
```

The module never reads this table. The page looks a boss up by creature entry
and an ability by spell id, so a new raid is rows only: `raids.csv` (its key,
map id and name), `boss_abilities.csv` (its bosses, abilities, phases and adds)
and, when a code strategy already answers something, `code_answers.csv`.

## Files

| File | What |
|:--|:--|
| `boss_abilities.csv` | the source; one row per ability, phase rule, add or note |
| `raids.csv` | raid key, map id and display name; `check` fails when a CSV map has no row or a row has no boss |
| `code_answers.csv` | the answers that live in C++ (a trigger, action or strategy), with a repo path per claim |
| `names.json` | spell and creature names (plus known cast ms) used by `build` |
| `../../tools/boss_data.py` | `build` / `check` / `coverage`; validates every citation and every answer |
| `../../tools/boss_coverage.py` | the coverage rule shared with the page (`coverage_cases.json` keeps both sides equal) |
| `../../tools/observability/web/data/boss_abilities.json` | generated, committed, served |

`names.json` is committed so `check` works without a database. On a world-DB
machine, `python3 tools/boss_data.py build --db` refreshes it from
`spell_template` / `creature_template` (env `BOSS_DATA_DB_*`, or `--db-*`).
`castMs` stays as recorded there; `0` means unknown and the page uses the live
cast event's end instead.

`raids.csv` names the map ids the module's own enter triggers use
(`ai/playerbot/policy/raid/RaidFrameworkPolicy.h` for the raids and the Onyxia
trigger header for map 249); it needs no citation column.

## CSV columns

`map,boss,row,key,a,b,c,d,source`

| row | key | a | b | c | d |
|:--|:--|:--|:--|:--|:--|
| `ability` | spell id | first ms after pull, `0` = no timer | repeat min ms | repeat max ms (`b=c` constant, `0/0` one-shot) | flags |
| `phase` | phase name | `hp<N>`, `aura=<spell>`, `flag=<letter>`, `start` | order, 1 checked first | | |
| `add` | creature entry | `summon`/`linked`/`ward`/`totem`/`trigger` | kill priority, `0` none | | |
| `note` | | text shown under the panel | | | |

Ability flags, `;`-joined: `p=<phase>` (must be a phase row), `t=victim|random|self|aoe|ally`,
`aura` (leaves an aura on players), `self` (an aura on the boss), `mark` (marked
icon), `ground` (persistent area, checked against spell effect 27 with `--db`),
`cone` (frontal cone, checked against implicit target 24 with `--db`),
`tail` (rear cone or tail sweep; no DB column, it is the script's choice),
`int` (cast time/channel, interruptible), `summon=<entry>` (needs an add row),
`hp<N>` (one-shot below N percent), `ok=<reason>` (the whole ability needs no
answer) and `ok.<need>=<reason>` (that one need needs no answer).

Phase rules: first match in order wins; `hp<` rules latch; `start` always matches
and must be the last rule (it is the phase at pull, so a boss whose script has no
visible phase still gets a label). An `aura=` rule needs an ability row for that
spell.

## `ok=` reasons

The coverage rule (`/coverage` on the page, `boss_data.py coverage` in a
checkout) asks, for every ability, whether the bots have an answer for each of
its needs: `interrupt`, `kill`, `front`, `tail`, `avoid`, `mark`, `dispel`,
`purge` and `resist`. An ability whose flags derive no need at all shows as
`missing` with the need `classify`: the table has to say what the ability needs.

`ok=<reason>` says the ability needs nothing; `ok.<need>=<reason>` says one need
needs nothing (the need has to be one the row's own flags derive, so a typo
fails `check`). The reasons are closed:

| Reason | Meaning | `check --db` verifies |
|:--|:--|:--|
| `tank` | hits the tank or the current victim only and is healed through (plain strikes, sunders) | the row has `t=victim` (no DB) |
| `heal` | raid or random damage the healers take | no |
| `phase` | a transform or phase marker (the panel shows the phase) | the spell is the `aura=` of a phase rule of that boss, or the row has `hp<N` (no DB) |
| `script` | a script-internal spell with no player effect | no |
| `nokick` | only as `ok.interrupt=`: the core cannot interrupt it | `preventionType` 1, `interruptFlags` & 0x02 and `castingTimeIndex` > 1 would make it kickable and FAIL |
| `nodispel` | only as `ok.dispel=`: the aura cannot be dispelled | `spell_template.dispel` ≠ 0 FAILs |
| `nopurge` | only as `ok.purge=`: the boss aura cannot be purged or tranquilized | `spell_template.dispel` ≠ 0 FAILs |

A reason is justified by the row's own `source` citation (the line that defines
the spell), plus a `kb:` citation when the reason comes from the knowledge base.
Never add `ok` to make the count look better.

## `code_answers.csv`

The answers that live in C++ rather than in a world table:

`map,boss,key,need,level,name,text,source`

| Column | Values |
|:--|:--|
| `map` | raid key (`zulgurub`) or `*` (any raid) |
| `boss` | creature entry, or `*` (only when `map` is `*` or `key` is `flag=`) |
| `key` | a spell id; `add=<entry>`; or `flag=<flag>` (every ability under the scope with that flag: `ground`, `aura`, `self`, `cone`, `tail`, `mark`, `int`) |
| `need` | one of `interrupt`, `kill`, `front`, `tail`, `avoid`, `mark`, `dispel`, `purge`, `resist` |
| `level` | `full` or `partly`; a `*` row must be `partly` (generic code is never a full answer) |
| `name` | the registered trigger/action/strategy name, e.g. `dragon flank` |
| `text` | one short line on what the bots do (the page and the CLI show it) |
| `source` | one or more repo-relative paths, `;`-separated |

`check` verifies that every path exists, that the cited files register the name
quoted and name the boss entry (as an id or a constant), that a spell key is an
ability of that boss, that an `add=` key is an add row of that boss, and that no
two rows make the same claim. The paths are line-free, so moving code inside a
cited file does not break a row; a file the check can read but where the entry
appears for another reason still needs a human read of `text` against the code.

## `coverage`

`python3 tools/boss_data.py coverage [<raid key or map id>]` prints, raid by
raid, the abilities the bots have no answer for, plus the tactic and stand rows
that answer nothing. Rows come from replaying `data/sql/world` in file-name order
(`--sql-dir`), or from the world DB with `--db`; the abilities come from the CSV,
so an uncommitted row shows up at once. `--all` prints every line, `--json` the
machine form, `--fail-on-missing` exits 1 when anything is missing. `--db` refuses (exit 2) a table that hits the daemon's row cap, as a partial read would look complete. The page's
"Raid coverage" tab reads the same rule from the running world DB.

## Citations

`source` is one or more `path:line[-line]` citations separated by `;`; paths are
relative to the core checkout (`--core-dir`, default `$TORTOISE_CORE_DIR` or
`~/tortoise-wow`). A timer or spell id must appear inside one of the cited
ranges; a constant name (`SPELL_X`) counts when its definition in the same file
holds the id. A value `N` is also accepted as `N//1000` on a line using
`IN_MILLISECONDS`, and on a `creature_spells` row, whose four timer columns the
loader multiplies by `IN_MILLISECONDS` (`src/game/ObjectMgr.cpp`).
Knowledge-base notes use `kb:<file>#<heading-slug>` under `--kb-dir` (default
`$TURTLE_KB_DIR` or `~/TortoiseBots-audit/turtle-kb`).

CI has no core checkout and no KB: `check` prints `core source check skipped` /
`kb check skipped` and still validates structure, uniqueness, the code answers
and JSON freshness. `--db` is the lead's check on the world DB.
