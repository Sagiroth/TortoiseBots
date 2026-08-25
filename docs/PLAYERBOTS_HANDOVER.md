# TortoiseBots PlayerBots audit handover

**Date:** 2026-08-25

**Target:** Tortoise WoW 1.18.1 / current Penqle Tortoise core

**Branch:** audit/playerbots-turtle-1.18.1
**Pull request:** [TortoiseBots PR #13](https://github.com/tortoise-wow-stack/TortoiseBots/pull/13)

> **Historical record.** PR #13 is merged. This file preserves the audit-phase
> handover and validation evidence; it is no longer the current resume point.
> Start with [STATUS.md](STATUS.md) and [PLAN.md](PLAN.md).

This document records what was inspected, what changed, what was actually
validated, and what remained outside the PlayerBots module boundary at the end
of the PR #13 audit. It is intentionally more operational than the full audit
in [PLAYERBOTS_AUDIT.md](PLAYERBOTS_AUDIT.md).

## Current conclusion

The module-side Turtle/1.18.1 cleanup and compatibility pass is complete for
the scope that can be addressed safely inside this repository. The target
branch contains no core source changes and no new PlayerBots-aware core hooks.
The module builds with PlayerBots enabled, and the core still builds with the
module absent or disabled.

The overall “ready to expand” goal is **not closed yet**. Two external/core
issues remain material:

1. The pinned core still contains legacy PlayerBots/LFT/chat/module coupling
   that belongs to a separate core cleanup (F-03).
2. The pinned core/data script registry does not register several custom SQL
   script names used by the world data (F-27).

Do not hide either issue by inventing module stubs or by claiming that missing
custom dungeon/NPC behavior is implemented. The full status and evidence are
in [PLAYERBOTS_AUDIT.md](PLAYERBOTS_AUDIT.md).

## Repositories and source-of-truth order

The target checkout is this repository. The audit was performed against:

- Target TortoiseBots commit before this handover:
  0702441ed0cec49bdf4d4cd0118b789795d7edb2.
- Target module closure implementation:
  d672048e86b9effc36210d3e6d076741fbeccc7f.
- Pinned local Tortoise core:
  9487c5150a6553c665fafc1f4568669b8b00f011 on playerbots-integration-gh.
- Local Turtle-patched client package: base WoW client build 5875,
  patched layer/package build 7272.
- Local runtime checkout: sibling tortoise-docker-penqle, used only for
  build/runtime validation.

For Turtle-specific spells, items, races, locations, scripts, and start rows,
the local Tortoise core plus its DBC/SQL data is authoritative. Vanilla or
CMaNGOS behavior is only a comparison/reference. The Turtle WoW wiki can add
context, but it must not override the local core/data validation.

Before continuing, read these in order:

1. [PLAN.md](PLAN.md)
2. [HOST_API.md](HOST_API.md)
3. [PLAYERBOTS_AUDIT.md](PLAYERBOTS_AUDIT.md)
4. [PROVENANCE.md](PROVENANCE.md)
5. The relevant Knowledge Base playerbots/ material, if behavior rather than
   compatibility is being implemented.

## What was done

### 1. Expansion and donor-content cleanup

The active module paths were searched for later-expansion, donor-only, absent,
or invalid Turtle IDs and branches. The audited residue was removed or made
data-driven. Examples include:

- removed the absent WotLK Battle Shout ID 47436;
- removed the absent totem exception 44452 and absent flag aura checks around
  34976;
- removed invalid Warrior talent checks for 30330 and the wrong-context
  30335 branch;
- removed invalid quest-item cases 52566, 39253, and 39645, retaining the
  locally valid 17117 -> 13016 mapping;
- removed the absent quest blacklist entry 50000, retaining the locally
  confirmed custom entry 50003;
- removed the audited TBC/WotLK consumable, poison, bandage, oil, stone, spell,
  formula, RTSC, SeeSpell, boss-aura, and test surfaces that did not belong in
  the 1.18.1 module.

The final source-surface validator is
[tools/verify_turtle_surface.sh](../tools/verify_turtle_surface.sh). It is a
guard against reintroducing the audited IDs and prohibited integration shapes;
it is not a substitute for checking new IDs against the core.

### 2. Native Tortoise/core compatibility

Donor shims were reduced to compatibility code that has a real local caller.
Where the core already exposes the behavior, the module now uses the native
API instead of an always-success or empty donor stub. The important changes
include:

- core race/team data through Player::TeamForRace and core player info;
- core DBC race names and core locale maps for quest, creature, and item names;
- native attack/stop, behind-target, combat-reach, evade, skill, item-count,
  taxi-eject, interaction, broadcast-text, auction, loot, and item-state paths;
- UseItemAction now uses the core spell CheckCast(true) path rather than an
  unconditional donor pre-check;
- interrupt decisions use actual spell state rather than an always-true donor
  CanBeInterrupted result;
- healing trigger damage uses native CalculateDamage over heal effects;
- world-position WMO area lookup falls back to the core terrain area;
- loot values use core ownership, conditions, round-robin, and active-roll
  state;
- sparse compatibility-store bounds are derived from the local core stores;
- the generic spell-cast error mapping remains only where the module needs to
  translate core errors into its own action result.

The purpose was to preserve mature behavior where it remains valid while
removing assumptions about the CMaNGOS host. No CMaNGOS host API was added to
the Tortoise core.

### 3. Turtle custom races, starts, mounts, and talents

The module no longer invents custom-race starts or faction/mount behavior:

- class/race availability comes from the core sObjectMgr.GetPlayerInfo data;
- custom starting locations and legal rows come from core playercreateinfo;
- release-spirit behavior uses core rows;
- Goblin (race 9) and High Elf (race 10) lifecycle checks used exact local core
  start data;
- custom-race mount selection uses collection_mount data only;
- collection mappings were checked against local data, including examples
  36550 -> 36650, 36551 -> 36651, 36666 -> 58031,
  92080 -> 57740, and 92082 -> 57723;
- class level-spell initialization was kept module-local and ports the intent
  of mature learnClassLevelSpells behavior through core quest, trainer, spell,
  talent, class, and race APIs;
- talent-spec allocation now totals points across all trees, rejects missing
  prerequisites safely, initializes dependency pointers, resets per-row
  maxRank, and falls back when a spell name is unavailable.

These changes make custom data discoverable and validateable. They do not claim
that every Turtle custom class/spec interaction, terrain tile, dungeon, or
client-visible behavior has been completed.

### 4. Pathing, cache, configuration, and safety cleanup

- The default avoid mobs strategy was removed.
- Path-area and area-cost operations are guarded; the module does not call
  unavailable generation APIs.
- Travel and fishing generation are explicitly disabled/fail-closed, with
  clear configuration errors instead of silently attempting donor generation.
- The local random reachable-point fallback uses core height data and static
  line-of-sight checks.
- Runtime travel/fishing DDL and generation paths were removed.
- SQL paths use the local lowercase world/characters schemas.
- Migration contracts now match the expected 32 scale columns,
  template_changed, and zone-level data; obsolete donor tables were removed.
- Owner input is escaped and bounded.
- RTSC/SeeSpell surfaces and dead SQL tables were removed.
- CMake source identity reports the module root/hash/dirty state.
- Optional LLM integration defaults to off and is inert unless explicitly
  enabled; it is not required for combat, movement, healing, threat,
  interrupts, or crowd control.

### 5. Optional-build boundary

No core file was changed in this audit branch. No new host hook was added.
PlayerBots remains optional, and the module-side code does not require the core
to know that a normal Player is bot-controlled.

The following legacy/core results were deliberately not “fixed” from this
repository because they are owned by the pinned core and require a separate
core decision/PR:

- src/game/LFT/LFTBotFill.cpp and related legacy LFT bot-fill paths;
- core .bot, .rndbot, .ahbot, and .perfmon registrations/stubs;
- core bot-slot and tracked src/modules/PlayerBots residue.

Keep BUILD_LEGACY_PLAYERBOTS=OFF for the intended clean build. Do not re-add
WorldSession::GetBot, WorldSession::SetBot, m_bot, sPlayerBotMgr, or
PlayerBotEntry coupling to normal core gameplay code.

## Validation actually performed

The following checks were run against the local pinned environment. “Passed”
means the command completed and its relevant output was inspected; it does not
mean every gameplay feature is complete.

| Check | Result |
| --- | --- |
| bash tools/verify_turtle_surface.sh | Passed on the final clean source tree. |
| git diff --check | Passed. |
| Cached PlayerBots-on build using docker exec tortoise-dev-builder and the /work/build-playerbots mangosd target | Passed and linked mangosd. |
| Cached PlayerBots-off build using the /work/build-off mangosd target | Passed. |
| Module-absent proof with BUILD_PLAYERBOTS=OFF, BUILD_LEGACY_PLAYERBOTS=OFF, and modules disabled | Passed; CMake reported modules: disabled (no modules found) and mangosd linked. |
| Fresh world/character schema migrations, applied twice | Passed; 32 scale columns and template_changed were present, with obsolete donor tables absent. |
| Normal runtime startup on the exact final module binary | Passed; world became ready at 2026-08-25T04:01:47.286072886Z, and a later clean restart became ready at 2026-08-25T04:14:40.631920103Z. |
| Custom Goblin headless lifecycle fixture | Passed login, AI/save, logout, relogin, and cleanup for race 9 at the local Blackstone start. |
| Custom High Elf headless lifecycle fixture | Passed login, AI/save, logout, relogin, and cleanup for race 10 at the local Thalassian start. |
| Fixture cleanup | Passed; temporary character and related rows were removed and the expected counts returned to zero. |
| Real Turtle client journey | Not passed/claimed; Wine/software rendering produced a black client, so no real-client .bot command result is being represented as evidence. |

The runtime fixtures were temporary and were removed. Configuration was restored
to normal values: TortoiseBots.AutoTest=0, PacketBridgeTest=0,
PendingAddRemoveTest=0, and the AutoTest character GUID was restored to 4.
No database reset, volume removal, or destructive Docker cleanup was used.

## Open issues and next decisions

These are the remaining items from the audit, in practical order.

### P0/P1: core-owned blockers

**F-03 — legacy core PlayerBots/LFT coupling remains open.** Decide whether to
remove, quarantine, or separately migrate the core-owned LFT/chat/module
surface. This cannot be solved by adding more module shims without violating
the optional-module architecture.

**F-27 — core/data script registry mismatch remains open.** Final startup logged
unregistered or missing custom script names, including:

~~~text
custom_dungeon_portal
spell_druid_wrath
go_airplane
go_curious_leaf
item_radio
item_temporal_bronze_disc
npc_alexandros_mograine
npc_breanna_darrowmont
npc_chieftain_icepaw
npc_chromie_dialogue
npc_distance_trigger
npc_frostshiv
npc_kitten
npc_lady_ripper
npc_nasuna
npc_surgeon_go
npc_teslinah
Script not found: 0.
~~~

The exact local startup log and source/data comparison are recorded in the
full audit. Do not manufacture empty module scripts merely to silence these
warnings. The correct owner must decide whether each SQL row is stale, whether
the core implementation/registration is missing, or whether the data should
be changed.

### P2: scope that still needs a deliberate follow-up

- **F-06:** terrain movement/death acceptance is incomplete. The Goblin map,
  MMAP, and VMAP tile exists; the exact High Elf VMap tile was absent locally.
- **F-07:** collection-based custom mount mapping is validated, but physical
  mount item-use behavior remains a core-owned acceptance case.
- **F-08:** SQL contains custom_dungeon_portal, but the pinned core reported a
  missing script. No custom dungeon encounter behavior is claimed.
- **F-09:** broad class/spec talent interactions still need targeted
  acceptance, even though the dangerous donor logic was corrected.
- **F-10:** broad donor configuration is accepted for now with random bots and
  LLM behavior off by default; split/trim it when the actual feature scope is
  chosen.
- **F-11/F-12:** native server command coverage exists, but real client
  incoming packet delivery and an optional client addon are separate work.
  There is no client addon in this server-MVP closure.
- **F-13:** guarded file globs remain a documented packaging trade-off.

If a future implementation genuinely needs a path-filter or another generic
host seam, first check whether it can be expressed as a core-neutral capability
and kept inside the small approved host boundary. Do not add a PlayerBots-
specific condition to unrelated gameplay code.

## Recommended continuation sequence

1. Review this handover together with PLAYERBOTS_AUDIT.md and PROVENANCE.md;
   preserve the evidence and do not rerun destructive setup.
2. Resolve F-03 and F-27 in a separately scoped core/data change. The current
   target branch has no core edits, and the sibling core checkout is reference
   material for this task.
3. Re-run the optional ON/OFF build matrix and the startup warning inspection
   against the resulting pinned core/data revision.
4. Add only the smallest deterministic runtime acceptance for the next feature:
   terrain movement/death, mount item use, dungeon portal/encounter, or talent
   interaction. Do not replay the full historical matrix for unrelated edits.
5. If client work starts, inspect the local patched client and document the
   actual opcode/UI behavior; do not infer it from a modern client or from a
   server-only test.
6. Update the audit status table and provenance entry with exact commits,
   commands, and observed logs before closing the next phase.

## Provenance used during the audit

The mature behavior references were consulted as references rather than copied
architectures. The relevant source commits recorded during the audit include:

- shyalya-tortoise-wow: 1f9497e0...;
- cmangos/playerbots: 076045...;
- mod-playerbots: 539711...;
- mangoszero/server: 1817ae...;
- cmangos/mangos-classic: 9b682be....

Use the exact full hashes and source-file mappings in
[PROVENANCE.md](PROVENANCE.md) before porting additional behavior. Preserve
upstream notices where substantial behavior is copied or reimplemented.

## Handover checklist

- [x] Read the active plan and host API docs.
- [x] Audit module code against local Turtle core/data rather than Vanilla IDs alone.
- [x] Remove audited later-expansion, absent, donor, test, and generation residue.
- [x] Replace available donor stubs with native core behavior where appropriate.
- [x] Validate PlayerBots-on, PlayerBots-off, and module-absent builds.
- [x] Validate fresh schema migrations and repeatability.
- [x] Run and remove temporary custom-race lifecycle fixtures.
- [x] Restore runtime test configuration and leave no temporary character fixtures.
- [x] Record unresolved core/data ownership issues instead of guessing.
- [ ] Resolve F-03 legacy core coupling.
- [ ] Resolve F-27 core/data script registration mismatch.
- [ ] Complete the explicitly scoped P2 runtime/client work when its owner and requirements are known.

The safest next change is therefore a small, evidence-backed follow-up after
the core/data blockers are assigned—not a new broad donor import.
