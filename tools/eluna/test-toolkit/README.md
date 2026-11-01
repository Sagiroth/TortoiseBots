# Eluna test toolkit (TEST SERVERS ONLY)

**These Lua scripts are for test servers only. They are not shipped, not part of
the module build, and must never be installed on a live realm.** They give a GM
account commands that despawn creatures, teleport whole groups and remove
instance binds, and they print run telemetry to the server log.

They need a core built with Eluna (`-DBUILD_ELUNA=ON`, tortoise-wow branch
`feat/eluna`, Eluna pinned to `1b06f28` on `cmangos-spell-update`, backend
`ELUNA_VMANGOS`; see the core's `docs/ELUNA.md`). Every Eluna function used is
listed with its source file in `~/TortoiseBots-audit/ELUNA-TOOLKIT-2026-10-22.md`.

## Files

| File | State | What |
|---|---|---|
| `lua_scripts/tt/tt_lib.ext` | every state | config (`TT.CFG`), JSON, guid helpers, floor lookup, `TTEV` writer |
| `lua_scripts/tt/tt_commands.lua` | world state | the `.tt` GM commands |
| `lua_scripts/tt/tt_telemetry.lua` | dungeon map states | `TTEV` lines: kills, deaths, combat, casts, creature poll |
| `lua_scripts/tt/tt_proto.lua` | map 822 / 43 states | encounter watcher prototypes (Hailar's Ritual, Kresh) |
| `check.sh`, `.luacheckrc` | | syntax check (`luac -p`, luacheck); nothing is executed |

## Install on a test box

1. Copy `lua_scripts/tt/` into the folder `Eluna.ScriptPath` names (default
   `./lua_scripts` relative to mangosd's working directory; in the container, mount
   it or `docker cp`). `.ext` files load before `.lua` files, so `tt_lib.ext` is
   ready when the others `require("tt_lib")`.
2. `.reload eluna` (rank >= `Eluna.ReloadSecurityLevel`, default 3) or restart.
3. Optional: `Eluna.UseUnsafeMethods = 1` in mangosd.conf enables `.tt q inst`
   (saved instance data) and `.tt q link` (creature_linking). Off, they answer
   `unsafe_methods_off`; everything else works.
4. Optional: `Eluna.OnlyOnMaps = "<dungeon map ids>"` keeps Lua states off the
   continents entirely; `.tt` still works (it runs in the world state).

## Commands

Only accounts with GM rank >= 3 (`TT.CFG.min_gm_rank`); others get
`TTJSON {"ok":false,"err":"not_allowed",...}`. Each reply is ONE system-chat line
`TTJSON {json}` to the caller, also echoed to stdout. Add `#<id>` anywhere in a
command and the reply carries `"id":"<id>"`. Guids are hex strings
`0xF130EEEEEECCCCCC` (high, entry, counter; counter = DB guid for static spawns).

| Command | Does |
|---|---|
| `.tt help` | the list |
| `.tt q floor [x y [z]]` | floor under the caller (or at x y within 40 yd), searched down from the caller's z like `.gps` FloorZ; `top` = surface from the sky (null indoors); `multi` true on two-level spots |
| `.tt q units [r] [x y z] [all] [dead] [e=<entry>] [au=<id,...>]` | creatures within r (2D, default 40, max 100) of the point: hp, combat, victim, current cast `cs` and channel `ch` with targets, DB guid, rank, home; `au=` looks those aura ids up: caster guid and `chan` (the caster channels that spell at this unit) |
| `.tt q unit <0xGUID> [au=...]` | one unit by guid; refuses a guid whose entry does not match |
| `.tt q gos [r] [x y z] [e=<entry>]` | game objects: `st` GO state (0 open/active, 1 closed/ready, 2 alt), loot state, spawned |
| `.tt q group` | the caller's group: map, position, hp, combat, height above the floor under each |
| `.tt q inst` | map/instance, players; `saved` = last saved instance data string (unsafe methods) |
| `.tt q link <0xGUID>` or `db=<n>` | creature_linking master and members, live state of each (unsafe methods) |
| `.tt quiet <r> [x y z] [w=<s>] [dz=<yd>] [skip=<e,e>]` | the owner's quiet rule: no hostile alive within r (2D) now and after w s (default 20, max 60), and no hostile within 80 yd that moved or arrived; two replies, `read` 1 then 2 (`final`, `quiet`). Units more than `dz` yd above/below the point (default 8; `dz=0` = no limit, the old 2D rule) and units whose entry is in `skip=` (e.g. `skip=3849`, Shadowfang's Deathstalker Adamant) never count |
| `.tt clearto <map> <x> <y> <z> <r> [kill] [boss] [dz=<yd>] [skip=<e,e>] [all=1] [of=<name>]` | despawn (default) or kill hostile creatures within r of the point; bosses (rank 3/world boss) skipped unless `boss`, pets/guardians always skipped, max 60. Same `dz` window (default 8) and `skip=` list as `quiet`. **Friendly NPCs are never touched unless `all=1`**: friendliness is judged from a non-GM member of the caller's group (or of `of=<name>`'s group, which a commander needs), so without such a member on the map it refuses `no_reference`. Skipped units come back in `skipped[]` with `why` = `friendly`/`dz`/`skip`/`boss`/`owned`/`cap` |
| `.tt place here` / `.tt place <x> <y> <z> [spread=<yd>] [force] [me]` | move every group member on the caller's map onto the floor at the point in one burst (ring of `spread` yd, default 3), facing the centre; refuses members in combat (unless `force`), a floor that does not match z, a caller under the floor; second reply `read` 2 after 4 s with where each landed (`placed` true when all are within 2 yd of z and 5 yd of the spot) |
| `.tt reset <mapId>` | `UnbindInstance(mapId)` for the caller and online group members; refused while any of them is inside; group binds are NOT reachable from Eluna |

### Tests

`sh check.sh` runs `luac -p`, luacheck and `tests/mock_window_test.lua` (plain Lua with fake
players and creatures: dz window, skip filter, the friendly rule and its refusals). It does not
need a server; a live check is still the final proof.

### Limits worth knowing

- The floor is searched from the CALLER's z (Eluna's `Map:GetHeight(x, y)` has no z
  and searches from the sky: the roof, or nothing indoors). Stand at the point first
  (`.go xyz X Y Z+2 <map>`), or within 40 yd and about its height. If the caller
  stands exactly 0.05 above the floor the lookup cannot tell "found" from "not
  found" and answers `no_floor`: move up 1 yd and ask again.
- `place` uses NearTeleport, not `.bot summon`: no follow restore. Put the bots on
  `.bot action stay` yourself (before or right after; see the test plan, T6).
- Hostility is "hostile to the caller" (the range searcher's own check; Eluna has no
  `IsHostileTo`). A GM of either faction sees dungeon mobs as hostile; check on a
  faction-mixed spot.
- No aura listing: auras are looked up by id (`au=`).
- Live encounter state of C++ instance scripts is not readable from Lua.

## Telemetry (`TTEV`)

`tt_telemetry.lua` prints, in dungeon instances only:

`enter`, `leave`, `kill` (player killed creature), `pdeath` (creature killed
player), `pcombat`/`pleave`, `pcast` (player cast, at Spell::cast), and from a 1 s
poll of creatures within 45 yd of up to 5 players: `c_combat` (on/off, victim),
`c_cast` (spell id; target is always null, a spell in flight is not read), `c_chan` (start/stop, target), `c_death`. Rate cap 50
lines/s per instance (a `dropped` line counts the rest). Casts shorter than the
poll period can be missed by `c_cast`; channels are caught.

Every line: `TTEV {"ev":...,"ts":<unix s>,"ms":<core ms>,"map":..,"inst":..,...}`.

Read them:

```sh
docker logs -t --since 30m <mangosd-container> 2>&1 | grep ' TTEV ' \
  | sed 's/^\([^ ]*\) TTEV /{"dt":"\1","e":/; s/$/}/' > ttev.jsonl     # docker time + event
docker logs --since 30m <mangosd-container> 2>&1 | grep -o 'TTEV {.*}' | cut -c6- | jq -c 'select(.ev=="c_chan")'
```

(`TT.CFG.file_sink = "tt_events.jsonl"` also appends them to a file in mangosd's
working directory.)

## No creature events, on purpose

Eluna gives a creature an `ElunaCreatureAI` when its entry has ANY Lua creature
binding and it has no C++ script (Eluna `LuaEngine.cpp` `Eluna::GetAI`, core
`ScriptMgr.cpp` `GetCreatureAI`). That replaces its EventAI, so a toolkit that
registered creature events would change the dungeon it tests. The toolkit uses
player and map hooks plus bounded polls by guid instead.

## Test plan

See `~/TortoiseBots-audit/ELUNA-TOOLKIT-2026-10-22.md`, section "Test plan".
