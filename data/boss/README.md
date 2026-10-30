# Boss ability data

The dashboard's boss panel, phase label, add list and map links read one
generated file, `tools/observability/web/data/boss_abilities.json` (served at
`/data/boss_abilities.json`). Edit the CSV here, never the JSON:

```
python3 tools/boss_data.py build     # CSV + names.json -> JSON
python3 tools/boss_data.py check     # citations, uniqueness, JSON freshness
python3 tools/boss_data.py check --db  # also: ids exist in tw_world
```

The module never reads this table. The page looks a boss up by creature entry
and an ability by spell id, so a new raid is rows only.

## Files

| File | What |
|:--|:--|
| `boss_abilities.csv` | the source; one row per ability, phase rule, add or note |
| `names.json` | spell and creature names (plus known cast ms) used by `build` |
| `../../tools/boss_data.py` | `build` / `check`; validates every citation |
| `../../tools/observability/web/data/boss_abilities.json` | generated, committed, served |

`names.json` is committed so `check` works without a database. On a world-DB
machine, `python3 tools/boss_data.py build --db` refreshes it from
`spell_template` / `creature_template` (env `BOSS_DATA_DB_*`, or `--db-*`).
`castMs` stays as recorded there; `0` means unknown and the page uses the live
cast event's end instead.

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
`int` (cast time/channel, interruptible), `summon=<entry>` (needs an add row),
`hp<N>` (one-shot below N percent).

Phase rules: first match in order wins; `hp<` rules latch; `start` always matches
and must be the last rule (it is the phase at pull, so a boss whose script has no
visible phase still gets a label). An `aura=` rule needs an ability row for that
spell.

## Citations

`source` is one or more `path:line[-line]` citations separated by `;`; paths are
relative to the core checkout (`--core-dir`, default `$TORTOISE_CORE_DIR` or
`~/tortoise-wow`). A timer or spell id must appear inside one of the cited
ranges; a constant name (`SPELL_X`) counts when its definition in the same file
holds the id. A value `N` is also accepted as `N//1000` on a line using
`IN_MILLISECONDS`. Knowledge-base notes use `kb:<file>#<heading-slug>` under
`--kb-dir` (default `$TURTLE_KB_DIR` or `~/TortoiseBots-audit/turtle-kb`).

CI has no core checkout and no KB: `check` prints `core source check skipped` /
`kb check skipped` and still validates structure, uniqueness and JSON freshness.
`--db` is the lead's check on the world DB.
