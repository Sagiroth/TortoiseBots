---
id: ref-provenance
title: Source Provenance & Behavior Harvesting Ledger
category: reference
summary: Append-oriented historical ledger tracking donor repositories, commit SHAs, ported translation units, and licensing attribution.
tags: [provenance, attribution, donor, lineage, history]
relates_to:
  - concept-donor-hierarchy
  - concept-architecture-invariants
---

# PROVENANCE — TortoiseBots behavior harvesting log

> **Append-oriented historical record.** This file is the source-lineage and
> validation ledger for imported/adapted behavior.

Record every substantial port/reimplementation here for attribution,
licensing, reasoning and local validation.

| Feature | Source project | Source commit | Source files | Ported / reimplemented | Reason | Local validation |
| --- | --- | --- | --- | --- | ---: | --- |
| SessionTransport Headless pattern (`SessionTransport::Headless`, `IsHeadless()`, `HasNetworkTransport()`, `InitHeadlessSession()` with `NullSessionAnticheat`) | `Shyalya/tortoise-wow` (vendored `cmangos/playerbots` via `r-o-sh/tortoise-wow:playerbots-integration-gh`) and `tortoise-wow` core's existing `NullSessionAnticheat` | `shyalya-tortoise-wow@1f9497e` (checkpoint `0af2567` 2026-05-10, vendored `cmangos/playerbots@c33dfac`) / `Anticheat/Anticheat.h:143` (`NullSessionAnticheat`) | `Anticheat/Anticheat.h:143-209` (`NullSessionAnticheat`, `NullAnticheatLib`), `shyalya` host hooks for bot sessions (80-file surface) | Reimplemented as generic transport enum, not copied — core now distinguishes `Network` vs `Headless` via `SessionTransport`, module interprets `IsHeadless() == true` as bot. `InitHeadlessSession()` directly assigns `NullSessionAnticheat` (existing core precedent) | Harvest the null-transport precedent without inheriting Shyalya's 80-file host surface; keep host integration ≤5 files | `rg` audits for `GetBot/m_bot` remain clean; `BUILD_PLAYERBOTS=ON/OFF` matrix builds; Docker runtime spike: headless session survives `World::Update` (not deleted as disconnected), `HandlePlayerLogin` succeeds after queued `AddSession` + deferred `LoginPlayer`, bot enters world and re-enters after logout |
| `IWorldUpdateListener` generic world-tick registry (`RegisterWorldUpdateListener`, `GetPendingWorldListenerFactories`, `RegisterPendingWorldListeners` in `World::Update`) | `HardcodedEvents`/`ZoneScriptMgr::Update` pattern in `tortoise-wow/tortoise-wow` + `DiscordBot::RegisterHandlers` precedent in same core | `World.cpp:2448` (`World::Update`), `HardcodedEvents.h`, `ZoneScriptMgr.cpp:117` (`Update`), `World.cpp:2343` (`DiscordBot::RegisterHandlers`) | `World.h:889`, `World.cpp:2448`, `HardcodedEvents.h`, `ZoneScriptMgr` | Reimplemented as generic `IWorldUpdateListener` with explicit registration and pending-factory static initializers (no weak symbols, no `sBotHost` global) — inspirited by `DiscordBot`'s service registration but made generic | Bot AI must run on the world thread once per tick; no existing `WorldScript::OnUpdate` exists in MaNGOS `ScriptMgr`, so a single generic call site in `World::Update` is the correct seam | `World::Update` now calls listeners after `UpdateSessions`; `BotHostAdapter` receives tick, `BotManager` drives lifecycle; `rg -i PlayerBot` in `src/game` only shows `InitHeadlessSession` bridge |
| `BUILD_PLAYERBOTS` optional-module CMake wiring | `cmangos/mangos-classic` (`cmake/options.cmake: BUILD_PLAYERBOTS OFF`) and `mangoszero/server` (`CMakeLists.txt: PLAYERBOTS OFF`) | `cmangos-mangos-classic@9b682be`, `mangoszero-server@1817ae1` | `CMakeLists.txt`, `src/CMakeLists.txt`, `src/game/CMakeLists.txt`, `src/mangosd/CMakeLists.txt` | Reimplemented as explicit `option(BUILD_PLAYERBOTS OFF)` with `add_subdirectory(modules/TortoiseBots)` only when `ON`, and `target_link_libraries(mangosd tortoise_bots)` via `CMP0079` + `whole-archive` on Linux — no `FetchContent` auto-download, no `ENABLE_PLAYERBOTS` scattered defines | Keep `BUILD_PLAYERBOTS=OFF` first-class and `src/modules/TortoiseBots` absent/present matrix clean; harvest the option pattern without the `FetchContent` auto-clone | Matrix: `absent+OFF` OK, `present+OFF` OK (module present but not built), `present+ON` OK (module linked); `rg` audits clean |
| Headless queued-session lifecycle (`WorldSession::Update`, `CharacterScreenIdleKick`, queued add/remove) | `Shyalya`'s `NullSessionAnticheat` + `WorldSession::Update` null-socket handling (`WorldSession.cpp:163,383,736` already tolerates `m_Socket==nullptr` but deletes headless via `return false`) | `WorldSession.cpp:306,334,378`, `Handlers/CharacterHandler.cpp:548`, `World.cpp:283`, `LockedQueue.h` | `WorldSession.cpp`, `Handlers/CharacterHandler.cpp`, `World.cpp`, `LockedQueue.h` | Reimplemented: explicit `SessionTransport`, a one-pass `m_headlessLoginPending` keepalive, deferred `LoginPlayer` after queued `AddSession`, and generic pending-session inspection/cancellation. `BotManager` retains `Removing` records until cleanup. | The queued path must survive the first `UpdateSessions` pass without requiring synchronous insertion; immediate removal must cancel the queue entry instead of orphaning it. | Queued runtime spike passed; `PendingAddRemoveTest PASSED` with no active/pending session, player, or record; graceful shutdown clears both online flags. |
| World-owned Headless lifecycle façade (`StartHeadlessSession` / `StopHeadlessSession` / `GetHeadlessSessionState`) | `tortoise-wow/tortoise-wow` PR #411 refactor plus TortoiseBots packet transport identity correction | `1e7994934b864558e257dd1f375fbbdbbcebe95e` plus rebased #416 `58bcb1cf8ea7110561120ed47c3c9203f9338c5b`; TortoiseBots `73ce12958b933cb4e74f5ccaddb21819e7ed3573` | `HeadlessSessionMgr.{h,cpp}`, `World.{h,cpp}`, `WorldSession.{h,cpp}`, `Handlers/CharacterHandler.cpp`, `PlayerLoginQueryHolder.h`, `host/BotPacketAdapter.cpp` | Moved Headless validation, shared login dispatch, callback identity, update, reclaim, removal, and shutdown into the World-owned manager; migrated TortoiseBots to the three-call interface; kept packet dispatch keyed to transport identity rather than socket presence | Keep one concrete `WorldSession`, character-GUID Headless ownership, normal Network auth, bot-neutral core ownership, and valid synthetic Network packet fixtures | Docker core-only and synchronized static module builds reached `[100%] Built target mangosd`; `tools/verify_tortoise_surface.sh` passed; runtime `PendingAddRemoveTest PASSED`, AutoTest save/logout/relogin/cleanup PASSED, and PacketBridgeTest command-surface PASSED, while its synthetic group invite/accept check FAILED; no real-client path was run |
| Out-of-world Headless expiry and owner dungeon-exit recovery | Current generic Headless lifecycle and local observed instance-portal failure | Local reimplementation | `HeadlessSessionMgr.{h,cpp}`, `BotManager.*`, `BotPlayerAdapter.*` | Independently reimplemented; no donor code copied | A non-teleporting Headless player outside the world must not retain `characters.online=1` or block Network reclaim; a Network owner leaving a dungeon requests the existing safe summon for owned bots left inside | Cached module-enabled and module-disabled builds passed; real-client portal/reclaim check pending |

| Foundational Engine/AiObjectContext/Strategy/Trigger/Action/Value/ReactionEngine (Tortoise 1.18.1 baseline) | `Shyalya/tortoise-wow` (`playerbots-integration-gh` @ 1f9497e, vendored `cmangos/playerbots@c33dfac`) | `Shyalya` baseline provides the full `playerbot/strategy/Engine.{h,cpp}`, `AiObjectContext.{h,cpp}`, `AiObject.{h,cpp}`, `Strategy.{h,cpp}`, `Trigger.{h,cpp}`, `Action.{h,cpp}`, `Value.{h,cpp}`, `ReactionEngine.{h,cpp}`, `Queue/Event/Multiplier` etc, already translated for `MANGOSBOT_ZERO` (Vanilla 1.12/1.18.1) and core `WorldLocation`/`Position`/`Map` APIs | `ai/playerbot/strategy/Engine.*`, `AiObjectContext.*`, `AiObject.*`, `Strategy.*`, `Trigger.*`, `Action.*`, `Value.*`, `ReactionEngine.*`, `Queue.*`, `Event.*`, `AiObject.*` (full `ai/playerbot` tree, 82 top-level + 14 strategy core + 113 generic) | Copied verbatim as the Tortoise/Vanilla translation reference for the foundational runtime; `cmangos-compat-shim.h` and `botpch.h` already handle core `SpellEntry`/`ItemPrototype`/`MapStorage` translation, Vanilla `MANGOSBOT_ZERO` guards exclude `deathknight`/`TBC`/`WotLK` paths | Shyalya's `playerbots-integration-gh` is the only proven `1.18.1` PlayerBots that already runs on core `WorldLocation` (`mapId/x/y/z/o`), `Transport`/`GenericTransport`, `GuidSet`/`AreaTableEntry` etc; using it as the baseline avoids reinventing `Penqle` API translation and keeps `Headless`/`IsHeadless()` as the only host seam | `ai/` now contains the full Shyalya `playerbot` tree (204 generic files after modern layer); `CMakeLists.txt` now builds the real `Engine`/`AiObjectContext`/`Strategy` stack instead of the stub `EngineStub`/`AiObjectContextStub`; native linkage uses explicit `MODULE_TORTOISEBOTS=static` and `MANGOSBOT_ZERO` |
| Modern generic Base behavior (204 files, `Follow`/`Combat`/`Dead`/`Ranged`/`Melee` etc) | `mod-playerbots` `src/Ai/Base/Strategy` @ 5397110cba48 (merge #2661) + `Shyalya` `strategy/generic` @ 1f9497e | `mod-playerbots: src/Ai/Base/Strategy/*.{h,cpp}` (91 files, modern `FollowMasterStrategy` now `getDefaultActions()` + `InitTriggers` vs Shyalya's `InitNonCombatTriggers`/`InitCombatTriggers` + `NextAction::array`) / `shyalya: strategy/generic/*` (113 files, includes `BlackwingLair`/`Karazhan` dungeon strategies) | `ai/playerbot/strategy/generic/*` (204 files after modern layer; donor SHAs are retained here instead of backup copies) | Forward-ported the modern `mod-playerbots` generic set on top of Shyalya's Tortoise-translated generic; Vanilla/Tortoise `MANGOSBOT_ZERO` guards kept and expansion-only paths excluded | Modern class/combat/follow behavior remains attributable to the pinned donor commits; obsolete `*.shyalya.bak` copies were removed once this provenance record was complete |
| Modern class factory + per-class Strategy subfolders (9 Vanilla classes) | `mod-playerbots` `src/Ai/Class/{Warrior,Mage,Priest,Druid,Hunter,Rogue,Paladin,Shaman,Warlock}/AiObjectContext.*` + `Strategy/*.{h,cpp}` @ 5397110 + `Shyalya` `strategy/{warrior,mage,priest,...}/` @ 1f9497e | `mod-playerbots: src/Ai/Class/Warrior/{WarriorAiObjectContext.*,Strategy/*.cpp}` etc / `shyalya: strategy/warrior/{WarriorAiObjectContext.*,Arms/Fury/Protection/Tank...Strategy}` etc | `ai/playerbot/strategy/{warrior,mage,priest,druid,hunter,rogue,paladin,shaman,warlock}/*` | Forward-ported the nine Vanilla class contexts and strategy families; `AiFactory` now instantiates each native class context rather than silently falling back to the generic context | Fresh runtime attached real Warrior, Mage, Priest, and Hunter contexts; Warrior/Mage/Priest completed packet group journeys and Hunter completed via the deterministic existing-action diagnostic path |

Notes (updated 2026-08-22 — large-batch forward-port):

- Foundational runtime is now substantially real (not a stub): `ai/playerbot` contains the full Shyalya `playerbot` tree (Tortoise `1.18.1` translation, `MANGOSBOT_ZERO`) plus the modern `mod-playerbots@5397110` generic and nine class contexts. The obsolete `*.shyalya.bak` and `.orig` migration copies were removed after donor SHAs and source paths were recorded above. `CMakeLists.txt` builds the real `Engine`/`AiObjectContext`/`Strategy`/`Trigger`/`Action`/`Value`/`ReactionEngine`/`AiFactory` stack; `deathknight`/`WotLK` remains excluded via `MANGOSBOT_ZERO`.
- Host seam remains generic and minimal: `SessionTransport`, `IsHeadless()`, `HasNetworkTransport()`, `IWorldUpdateListener` (≤5 host files, verified via `rg -n -i 'PlayerBot|BotService|Headless' src/game` only shows the generic Headless manager/session code). No `IsBot()`/`GetBot()`/`m_bot`/`sPlayerBotMgr` reintroduced. Same-account `1 Network + N Headless` lifecycle is core-owned behind Start/Stop/State; `BotManager` retains only records and AI.
- Upstream licenses (GPL-2.0 for MaNGOS/CMaNGOS/Shyalya, GPL-2.0 for `mod-playerbots`) are preserved; headers retain original copyright/license and this file records donor SHAs/source files before migration copies were deleted. No `AzerothCore` `PlayerbotMgr`/`BotSession` ownership model is reintroduced.
- The broad donor tree is now wired into the active CMake source set: real `PlayerbotAI`/`AiFactory`, generic behavior, all nine Vanilla class folders, Value/Trigger/Action families, Travel, grouping, loot, quests, dungeon/raid and PvP families are compiled as one coherent batch rather than left dead in-tree. `MANGOSBOT_ZERO` filters expansion-only folders. Fresh runtime probes now cover the packet bridge and Warrior/Mage/Priest/Hunter class attachment/group journeys.
| Follow (dead-zone 1.5y, MoveFollow behind M_PI, public native target/moving-state restart guard, CanFollow guards) | `cmangos/playerbots` + `mangoszero/server` | `cmangos-playerbots@076045e` / `mangoszero-server@1817ae1` | `cmangos: playerbot/strategy/actions/FollowActions.cpp:36-90`; `mangoszero: src/modules/Bots/playerbot/strategy/actions/MovementActions.cpp:440-560`; local `ServerFacade.cpp` adapter over core `MotionMaster::GetCurrent()` | The real PlayerbotAI path uses `FollowMasterStrategy`/`FollowAction`; the Tortoise adapter reads the public native targeted-generator target and moving state instead of pretending core's private angle/offset fields are available. `BotController` retains only an intent/diagnostic record and is never a gameplay fallback after AI attachment | Keep 1.5y jitter-free follow without a second movement owner or re-entrant generator replacement | Cached ON/static build; preserved AI-enabled runtime packet journey exercised `follow chat shortcut`, group invite/accept, and cleanup without a movement-state crash |
| Warrior vertical slice | Shyalya `playerbots-integration-gh` + modern `mod-playerbots` | `1f9497e` / `5397110` | Focused `Engine`/`Queue`/`Trigger`/`Action`/`Value` primitives, generic `FollowMasterStrategy`/assist/combat/non-combat/dead strategies, and Warrior Arms/Fury/Protection strategy files | Ported/adapted focused family; unrelated expansion systems remain excluded by the CMake source set | Make one owned Tortoise bot use strategy-driven follow and combat before widening the donor tree | Cached Docker build: `Built target tortoise_bots`, `Built target mangosd`; runtime: same-account Headless Sagiroth + Dudette, `dps assist`, successful Warrior Heroic Strike spell 78, encounter end/follow resume, clean removal; `PlayerbotAIStorage` logout use-after-free fixed and revalidated |
| Broad Vanilla/Tortoise source-set checkpoint | Shyalya `playerbots-integration-gh` + modern `mod-playerbots` | `1f9497e` / `5397110` | `CMakeLists.txt` globs the real `PlayerbotAI`, generic, nine Vanilla class, Value/Trigger/Action, Travel, grouping, loot, quest, dungeon/raid, BG/PvP and economy families; `MANGOSBOT_ZERO` excludes Death Knight and other expansion-only paths | Adapted core naming and data shapes in the module-local compatibility layer; native loot ownership, area names/flags, channel wrappers and const loot-list views replace unsafe CMaNGOS member assumptions | Compile and stabilize the broad family without adding core `GetBot`/`m_bot` ownership | The first broad Docker pass reached the module compilation stage and exposed a small remaining core API family in `PlayerbotAI.cpp`; the correct upstream `module-system` host snapshot must also carry the documented generic Headless/session seams before a broad runtime claim is made |
| Sub-10 hunter "pet dead" suppression in `PetIsDeadValue` | `mod-playerbots` (mature behavior donor) | local `playerbots-references/mod-playerbots` `src/Ai/Base/Value/StatsValues.cpp:45-60` | `ai/playerbot/strategy/values/StatsValues.cpp:28-34` | Ported the level<10-hunter/mounted early-false rule with Turtle API spellings (`GetLevel`/`GetClass`/`IsMounted`); Shyalya has no equivalent (different AI lineage); the level now comes from the shared `runtime/HunterPetPolicy.h` threshold | Pet-less lowbies with a stale `character_pet` row read "pet dead" and loop failing revive-pet casts (observed as `ACTION_LOOP` on a level-2 hunter) | Rule kept as a backstop. The stale row itself is now fixed at the source: a pool hunter below level 10 is never given a pet (`PlayerbotFactory::InitPet`) and gives one up at login (`BotManager::OnPlayerLogin`) |
| Post-rez hopeless-death relocation (`RelocateHopelessBot`) | Inspired by `mod-playerbots` revive relocation (`RandomPlayerbotMgr::Revive` → `RandomTeleportGrindForLevel`); implemented natively on our revive path | local `playerbots-references/mod-playerbots` `src/Bot/RandomPlayerbotMgr.cpp` (`Revive`, `RandomTeleportGrindForLevel`) | `runtime/BotManager.{h,cpp}` (`RelocateHopelessBot`, shared `PickLevelFittingPoint` picker), `ai/playerbot/strategy/actions/ReviveFromCorpseAction.cpp` (both rez paths), `AiPlayerbot.RelocateHopelessDeaths` (default on) | Native conditional rescue instead of donor unconditional teleport: only random masterless ungrouped bots with 2+ deaths in a zone 5+ above their level relocate, reusing the login-scatter ±5 validated picker; corpse runs, master rezzes, BG/group flows untouched | Lowbies GY-locked in over-leveled zones (observed: level 5s dying in Northwind 25-30) | Pending: module rebuild + staged rescue verification |
| Buff & debuff triggers gated on trained spells (`BuffTrigger`/`MyBuffTrigger`/`DebuffTrigger`, class triggers) | Native hardening; no donor equivalent (donor triggers have the same hole, harmless there because donor bots are max-level) | local `playerbots-references/mod-playerbots` `src/Ai/Base/Trigger/GenericTriggers.cpp:159-167` (reference behavior) | `ai/playerbot/strategy/triggers/GenericTriggers.cpp`, `strategy/{priest,hunter,warrior,shaman,warlock,mage}/*` | `IsActive` returns false when `ai->HasSpell(spell)` is false: an untrained spell can never land its aura, so missing-aura triggers otherwise stay active forever and fail cast every tick | Observed `ACTION_LOOP`s on level 1-2 bots for inner fire, lightning shield, aspect of hawk, shocks, curses, bloodrage | Built into module; live verification |
| Auto-learn quest spells default on | Upstream mod-playerbots default (`AutoLearnQuestSpells=1`); Shyalya handled via ad-hoc class exceptions | upstream mod-playerbots `aiplayerbot.conf.dist` | `ai/playerbot/PlayerbotAIConfig.cpp`, `ai/playerbot/aiplayerbot.conf.dist.in`, `tortoise-docker-penqle` | Auto-learn quest-reward spells and items (stances, forms, totems, demon summons, hunter pet skills) on levelup while keeping trainer spells manual (gold/grind) | Bots cannot execute scripted class quest chains; quest rewards are required for basic class kit | Built into module; live verification |

## Native module-system checkpoint — 2026-08-24

Feature: Native module packaging, module-local runtime support, and broad Vanilla/Tortoise source selection

Source repository: local `tortoise-wow` checkout plus local TortoiseBots checkout

Source commit: core `73f32c063e6c4481a0415690896025178ca8076f` on branch `playerbots-integration-gh`; TortoiseBots `3484208` (`Finish native PlayerBots integration and playtest bridge`). This checkpoint is superseded by core `9487c5150a6553c665fafc1f4568669b8b00f011`; the core commits `133c6d19` and `9487c515` keep static-module include paths target-local and remove the stale `src/game/PlayerBots` common path.

Source files: `TortoiseBots.cmake`, `src/TortoiseBotsModule.cpp`, `host/*`, `runtime/*`, `ai/playerbot/*`, `strategy/{generic,druid,hunter,mage,paladin,priest,rogue,shaman,warlock,warrior}/*`, `strategy/{actions,triggers,values}/*`

Copied / ported / independently reimplemented: behavior was ported/adapted from local `shyalya-tortoise-wow@1f9497e0f42bfc1055841bb6ebdc7caa3515de0b`, `cmangos-playerbots@076045efa835da9aab7caa943bca752aebe1baad`, and `mod-playerbots@5397110cba484a9b7209bc9f632652e9d4bd6a70`; host lifecycle and BotManager ownership were independently reimplemented around generic `SessionTransport`/Headless APIs.

Reason: use existing combat/class/travel behavior without compiling donor manager, random-manager, login-manager, or second-session ownership into the target core.

Local validation: static `modules` target and `mangosd` link passed with `BUILD_PLAYERBOTS=ON`, `BUILD_LEGACY_PLAYERBOTS=OFF`, `MODULE_TORTOISEBOTS=static`; the complementary `BUILD_PLAYERBOTS=OFF`, `MODULES=disabled` `mangosd` build passed; the local runtime reached `TortoiseBots: native module loaded (AI enabled)` and `World server is up and running` after applying the three pending non-destructive world migrations required by the preserved database. Generated flags prove TortoiseBots definitions/includes/PCH are on `mod_tortoisebots_static`, not the combined `modules` target.

Explicit gaps at that earlier checkpoint: `AutoMaintenanceOnLevelupAction`, advanced `FishingAction`, `InventoryAction`/`TellEmblemsAction`, `NonCombatActions`, guardian-oriented `PetsAction`, `TellPvpStatsAction`, extended trade reporting, donor `TradeValues.cpp`, and expansion-only LFG/glyph/Karazhan/vehicle/Arena registrations remained excluded where they required WotLK/AzerothCore APIs or unsupported host data. Native loot, quest, inventory operations, travel, trade, class combat, pet-taming, and lockpicking paths are now compiled; no empty gameplay stubs were added.

Architecture note: core integration is generic Headless transport/session lifecycle plus ScriptMgr hooks. `MODULE_TORTOISEBOTS=static` selects the native module and does not pull the legacy vendored CMaNGOS tree; `BUILD_LEGACY_PLAYERBOTS` is a separate explicit escape hatch.

## Native runtime and Vanilla/Tortoise behavior checkpoint — 2026-08-24

Feature: Pet taming/control, lockpicking, bounded random-bot lifecycle, AH/economy pricing, named-location travel lookup, cache-safe startup, and native module SQL packaging

Source repository: local `TortoiseBots` checkout; host/runtime seam in the local `tortoise-wow` checkout

Source commit: TortoiseBots native stabilization work after `6a78b5c`; behavior references `shyalya-tortoise-wow@1f9497e0f42bfc1055841bb6ebdc7caa3515de0b`, `cmangos-playerbots@076045efa835da9aab7c943bca752aebe1baad`, and `mod-playerbots@5397110cba484a9b7209bc9f632652e9d4bd6a70`; required core seam `9487c5150a6553c665fafc1f4568669b8b00f011` on `playerbots-integration-gh`

Source files: `TameAction.*`, `UnlockItemAction.*`, `UnlockTradedItemAction.*`, `ChatActionContext.h`, `WorldPacketActionContext.h`, `runtime/RandomBotService.*`, `runtime/PlayerbotRuntimeFacade.cpp`, `ai/playerbot/RandomItemMgr.cpp`, `data/sql/{world,char}/*`, `TortoiseBots.cmake`, `host/BotHostAdapter.cpp`, `conf/tortoise_bots.conf.dist`

Copied / ported / independently reimplemented:

- Tame-beast behavior was independently reimplemented around the host's real `SPELL_EFFECT_TAMECREATURE` path; rename and abandon use the native `Pet`/`Player` APIs. The old WotLK pet-stable construction was not retained.
- Lockpicking was ported to `ItemPrototype`, `LockEntry`, `ITEM_DYNFLAG_UNLOCKED`, the native Pick Lock spell, and the native trade-slot path. The old AzerothCore `ItemTemplate`/extended trade wrappers were not retained.
- Random bots use a module-local, startup-loaded pool of pre-existing characters on the configured random-account prefix. `World` owns Headless/Network session lifetime; `BotManager` owns module records and AI adapters. Account/character creation and donor login managers remain intentionally outside the module.
- Random-bot buy/sell multipliers are now cached per character with the Existing Vanilla ranges, and named-location lookup uses the native `ai_playerbot_named_location` table instead of a compatibility no-op.
- Empty optional item/equipment caches are accepted without synchronous world-thread cache generation. Populated existing caches still load normally.
- Schema-only native migrations cover the tables queried by the active Vanilla/Tortoise AI initializer and per-bot state. Existing datasets remain deployable separately.

Reason: complete coherent Vanilla/Tortoise families without reintroducing donor manager/session ownership or making optional AI startup depend on a large synchronous cache write.

Local validation: ON/static `mangosd` build passed after the cache, config, SQL-install, installed-module-path, economy, recovery, packet, command, and class-context changes; OFF/disabled `mangosd` build passed and the ON/static configuration was restored. Docker runtime with AI enabled loaded the module, attached real Warrior/Mage/Priest/Hunter contexts, passed packet-bridge group invite/accept and cleanup journeys, and retained the earlier save/logout/relog spike evidence. The preserved DB was not reset; only additive missing schema migrations and disposable `TBPLAY` class fixtures were added. Random pool startup correctly reported zero candidates because no `RNDBOT*` accounts exist in the fixture.

Known scope gates: the physical tree and positive CMake graph contain no DK,
glyph, vehicle, Arena, Karazhan, or expansion-only donor families. Native
core LFG/meeting-stone behavior remains available; the module retains only
applicable group-role helpers and does not recreate the donor automatic queue.
The donor `PetsAction` guardian-control wrapper and post-Vanilla fishing
wrapper remain excluded because native pet, fishing, travel, loot, and
profession paths provide the applicable Vanilla/Tortoise behavior.
Account/character auto-creation remains the intentional random-bot product
gap; existing random characters, bounded login/logout, native TravelMgr
relocation, AI strategy rotation/recovery, gear refresh, and AH/economy pricing
are supported. Core `BattleGroundMgr` remains authoritative for Vanilla and
Tortoise battleground entries; the existing value compatibility view reads the
same native `battlemaster_entry` table once at AI startup and does not own
battleground state.

## Packet/config/static-isolation and playtest gate — 2026-08-24

Feature: generic packet/event bridge, safe multi-value configuration, isolated native static-module settings, existing command surface, random-bot debt cleanup, and fresh class journeys.

Source repository: TortoiseBots `phase4-follow@3484208`; required upstream core `playerbots-integration-gh@9487c5150a6553c665fafc1f4568669b8b00f011` (parent `133c6d19bf5898c1e4f5129b2890b1db89b17a07`).

Source files: `host/BotPacketAdapter.*`, `runtime/BotManager.*`, `runtime/PlayerbotAIAdapter.cpp`, `runtime/PlayerbotRuntimeFacade.cpp`, `runtime/RandomBotService.*`, `commands/BotCommands.cpp`, `ai/playerbot/PlayerbotAIConfig.*`, `ai/playerbot/strategy/ValueMacros.h`, `ai/playerbot/AiFactory.cpp`, `TortoiseBots.cmake`, `conf/tortoise_bots.conf.dist`, and core `modules/CMakeLists.txt`/`Config`.

Copied / ported / independently reimplemented: the packet calls preserve the existing `PlayerbotAI` handlers but the mapping and ownership are independently implemented through `PlayerbotAIStorage` and `BotManager`; Core `Config::GetValues` is a generic core API over ACE configuration enumeration; static isolation is a generic per-static-module OBJECT-target mechanism.

Historical runtime evidence: fresh AI-enabled Docker runs emitted bot outgoing `SMSG_GROUP_INVITE`, master outgoing `SMSG_PARTY_COMMAND_RESULT`, and a synthetic master-incoming diagnostic, with existing group invite/accept success and cleanup. The same journey instantiated real `WarriorAiObjectContext`, `MageAiObjectContext`, `PriestAiObjectContext`, and `HunterAiObjectContext`; the Hunter no-network diagnostic used the existing `accept invitation` action directly after packet delivery because activity scheduling is intentionally not treated as human-client evidence. `Loading WorldBuffs` proves the real multi-value config reader. This evidence is retained for provenance, not final incoming-hook acceptance.

This checkpoint is historical and is superseded by the final correctness pass
below: the direct incoming diagnostic and Hunter direct-action diagnostic are
not final acceptance evidence.

Migration cleanup: removed `MinimalPlayerbotAI*`, `VerticalSlice*`, `cmangos-compat-shim.h.orig`, and all tracked `*.shyalya.bak` copies after retaining donor provenance above. The obsolete `BotController` has also been removed; `PlayerbotAI` is the sole gameplay update owner.

Intentional gaps only: random account/character auto-creation and the
post-Vanilla fishing wrapper remain outside the Vanilla/Tortoise product
surface. A real human-client journey was not claimed from the automated
server-side packet fixture; the preserved runtime is left AI-enabled and
ready for manual client playtesting.

## Final pre-playtest correctness pass — 2026-08-24

Feature: movement-mode coherence, strict packet-trigger acceptance, human
master reconnect rebinding, controller cleanup, canonical random timing key,
and concrete PlayerbotAI value null-safety.

Source repository: TortoiseBots `phase4-follow@e0da302058cb1e5021c92272bf22983b2b5ad073`; required
upstream core `playerbots-integration-gh@9487c5150a6553c665fafc1f4568669b8b00f011` (parent `133c6d19bf5898c1e4f5129b2890b1db89b17a07`, with `73f32c063e6c4481a0415690896025178ca8076f` as the original seam commit).

Source files: `ai/playerbot/PlayerbotAI.{h,cpp}`,
`runtime/{BotManager,PlayerbotAIAdapter}.*`,
`host/BotPacketAdapter.*`, `commands/BotCommands.cpp`,
`ai/playerbot/PlayerbotAIConfig.cpp`,
`ai/playerbot/strategy/actions/CheckMountStateAction.cpp`,
`ai/playerbot/strategy/values/PossibleAttackTargetsValue.cpp`,
`ai/playerbot/AiFactory.cpp`, and `ai/playerbot/strategy/actions/AcceptInvitationAction.h`.

Copied / ported / independently reimplemented: movement transition
centralization and reconnect rebinding are independent module work around the
existing existing `FollowMasterStrategy`/`ChatShortcutActions` semantics; the
strict fixture is an independent runtime assertion over the existing packet
bridge; the null-safety changes are local defensive corrections.

Local validation: cached `BUILD_PLAYERBOTS=ON`, static native `mangosd` passed;
cached `BUILD_PLAYERBOTS=OFF`, `BUILD_LEGACY_PLAYERBOTS=OFF`, and
`MODULES=disabled` `mangosd` passed. The final AI-enabled Docker binary reached
world-ready, and the strict packet fixture passed automatic existing group
invite/accept plus cleanup without a direct accept-action fallback. The
fixture intentionally does not synthesize `CanPacketReceive`; a real client
incoming event remains the manual playtest gate. No client login was automated
in this pass at the user's request.

Remaining intentional gaps are unchanged: random account/character creation
and the post-Vanilla fishing wrapper. The next action is manual playtesting of
the real Network client path, including incoming packet delivery and
reconnect/reclaim behavior.

Core reproducibility: check out `playerbots-integration-gh` at
`9487c5150a6553c665fafc1f4568669b8b00f011` (parent
`133c6d19bf5898c1e4f5129b2890b1db89b17a07`). The final core commits remove
`MODULES_PUBLIC_INCLUDES` from the combined static archive target and remove
the stale `src/game/PlayerBots` common path, so unrelated static modules do not
inherit a selected module's include directories; the selected OBJECT target
still receives its own module settings. The configured
`Shyalya/tortoise-wow` fork rejected publication with HTTP 403, so this exact
local commit must be applied from a writable core fork/PR before reproducing
elsewhere.

## Narrow post-review cleanup pass — 2026-08-24

Feature: existing `.bot stay` anchors, existing-AI-authoritative reconnect,
durable random-bot master bind/clear, and corrected packet-fixture wording.

Source files: `commands/BotCommands.cpp`, `runtime/BotManager.{h,cpp}`,
`runtime/PlayerbotAIAdapter.{h,cpp}`, `ai/playerbot/PlayerbotAI.{h,cpp}`,
`ai/playerbot/strategy/actions/AcceptInvitationAction.h`,
`ai/playerbot/strategy/actions/LeaveGroupAction.cpp`,
`ai/playerbot/strategy/actions/BattleGroundJoinAction.cpp`,
`ai/playerbot/strategy/actions/BattleGroundTactics.cpp`, and the current
status/host-boundary documentation.

Copied / ported / independently reimplemented: native commands now reuse the
existing existing `StayChatShortcutAction`/`FollowChatShortcutAction` actions;
reconnect treats the existing existing strategy set as authoritative and only
applies the default when no movement strategy exists. `BindBotMaster` and
`ClearBotMaster` are small module-local lifecycle operations; they do not add
core fields or replace Headless sessions.

Local validation: cached ON/static `mangosd` passed after the coherent edit
batch; the earlier AI-enabled image reached world-ready and supplied the
pending add/remove and packet-bridge evidence. The final native image also
built, linked, installed, and reached world-ready with the preserved database,
but used the Docker wrapper's AI-off default, so it is not claimed as a fresh
AI fixture run after the final synthetic-headless-master authorization. No
database or Docker volume was reset. The real-client journey remains
intentionally unperformed in this pass.

## Final merge-hardening pass — 2026-08-24

Feature: current-scope ownership cleanup, controller removal, explicit native
source selection, optional-data startup safety, disposable-fixture guards,
packet lifetime safety, and diff hygiene.

Source repository: TortoiseBots `phase4-follow@7fd7a35`.
Required core remains `playerbots-integration-gh@9487c5150a6553c665fafc1f4568669b8b00f011`.

Source files: `runtime/BotManager.*`, `runtime/PlayerbotAIAdapter.*`,
`runtime/PlayerbotRuntimeFacade.cpp`, `commands/BotCommands.cpp`,
`host/{BotHostAdapter,BotSessionAdapter}.cpp`, existing ownership-transition
actions, `host/BotPacketAdapter.cpp`, asynchronous packet delivery in
`ai/playerbot/PlayerbotAI.cpp`, the active compatibility shims,
`TortoiseBots.cmake`, `ai/playerbot/{TravelNode,RandomItemMgr}.cpp`, and
current host/provenance documentation.

Local validation: `git diff --check` passes; no active `BotController` or
legacy core ownership symbols remain; the final native image built, linked,
and installed successfully and its preserved Docker stack reached world-ready
with module SQL migrations applied. The AI-enabled runtime reached Headless AI
attachment and automatic invite acceptance, but the pre-catch packet run then
terminated on a malformed party packet before cleanup. The follow-up rebuild
containing the packet safety catch was intentionally stopped at the user's
request, so a fresh post-catch AI runtime pass remains unclaimed. No database
or Docker volume was reset.

## Vanilla/Tortoise source cleanup — 2026-08-24

Feature: subtractive product cleanup from the multi-expansion donor tree.

Source repository: TortoiseBots `cleanup/vanilla-tortoise` working branch.

Source commit: `322120ef1fe9b848cfe520ad58f6ff698fa801e9` (the cleanup commit;
this follow-up metadata commit records its exact SHA).

Source files: `TortoiseBots.cmake`, `ai/playerbot/*`, the nine class strategy
folders, `runtime/*`, `conf/*`, `data/sql/*`, `README.md`, and current module
documentation.

Copied / ported / independently reimplemented: no new gameplay was imported.
The cleanup removed physical Death Knight, donor manager/login/command-server,
test, glyph, rune-forging, vehicle, Arena, Karazhan, automatic donor-LFG,
advanced-fishing, and duplicate food/inventory families. It also removed
later-expansion registrations and class actions while retaining core-backed
Tortoise custom spells (Druid Eclipse/Tree of Life/Mangle, Shaman Bloodlust/
Earth Shield/Water Shield, Warrior Intervene), all nine Vanilla classes,
Vanilla raids, WSG/AB/AV tactics, native transport/taxi travel, and native
core LFG/meeting-stone/group-role concepts. The former
`RandomPlayerbotMgr` name was replaced by the narrow `RandomBotFacade`; native
`BotManager` and `RandomBotService` remain the ownership boundaries.

Reason: make the physical tree, active registrations, configuration, and
compatibility surface read as one Vanilla/Tortoise product rather than a donor
tree hidden behind subtractive CMake filters.

Local validation: the persistent sibling builder's cached `BUILD_PLAYERBOTS=ON`
and complementary `BUILD_PLAYERBOTS=OFF` `mangosd` targets both compiled and
linked successfully. The ON wrapper's optional install step reports only that
`realmd` is not built; the `mangosd` artifact is produced. The existing Docker
stack was restarted from that artifact without an image rebuild or data reset;
the native module loaded and the world server reached ready. No manual
gameplay test was run, as requested.

## Surgical dead-code follow-up — 2026-08-24

Feature: residual Vanilla/Tortoise cleanup after the main donor-tree removal.

Source repository: TortoiseBots `cleanup/vanilla-tortoise`.

Source commit: `3213a058931ec7b88c87322cb11f02df4ffed8e1` (follow-up cleanup
commit; this metadata commit records its exact SHA).

Copied / ported / independently reimplemented: no new gameplay was imported.
This pass removed the unreachable legacy movement body, the dead spell-click
path, fake gem/socket qualifier and weight compatibility, the WotLK DK quest
special case, unused DK talent enum slots, and adjacent no-op branches.

Reason: remove concrete dead remnants identified during review without
starting another broad cleanup audit.

Local validation: targeted static checks and one cached persistent
`BUILD_PLAYERBOTS=ON` native `mangosd` build; no runtime restart or gameplay
test was performed.

## Tortoise audit closure pass — 2026-08-24/25

Feature: close the module-owned Tortoise WoW 1.18.1 audit findings without
reintroducing core ownership coupling: effective configured SQL packaging, additive
schema repair, fail-closed startup caches, owner-input SQL safety, collection
mount lookup, later-expansion residue removal, and repeatable surface checks.

Source repository: TortoiseBots `audit/playerbots-tortoise-1.18.1`

Source commit: `7e08fc810060e77839d4f38c813cc7eba9b05737` (final verified
implementation snapshot; the later provenance/docs update is documentation-only;
core-backed gossip/taxi/loot adapters, custom-start re-enable, talent validation
fixes, dead-shim cleanup, and native command fixture implementation; implementation `b76b5f4bf236b4d1bf370f0997e88cf30fd33695`, `fix: bound
engine action logging`), on top of `b863c6eedd3514a525f40e243bb8a61b2244fbe8` (`fix:
remove unreachable engine test logging`), `89a5e645e1485bd2e35b4944e88fdadfc6c95d05`
(`fix: remove remaining expansion-only item branches`), `887a6673675d06d716acc713aaeed8dca05d7e9f`
(`build: report native module source identity`), `9605a73c9bc16f0bf4fb4e84bba974a70f68c735`
(`fix: disable fish cache rebuild on startup`), `a6ea16605fde1b77e396ca588e0b34ddb1978bd5`
(`fix: align movement and channel shims with Tortoise core`),
`7fa875a6c6bc51534b4a5a3f2f373f3dd7446208` (`fix: quarantine optional LLM
and stale tooling paths`), `2afd2d1` (`fix: match effective core SQL paths
and migration history`), and the preceding
`9db49df` and `3a96923` remediation commits.

Required target core: local `tortoise-wow`
`playerbots-integration-gh@9487c5150a6553c665fafc1f4568669b8b00f011`.

Source files: `TortoiseBots.cmake`, `README.md`,
`ai/playerbot/{PlayerbotAI,PlayerbotAIConfig,PlayerbotDbStore,PlayerbotFactory,RandomItemMgr,TravelMgr,TravelNode}.{cpp,h}`,
  the edited strategy/action/value/context files, `data/sql/{world,char}/*`,
`ai/playerbot/{ServerFacade.cpp,cmangos-compat-shim.h}`, the follow/movement
and range-trigger files, `ai/playerbot/aiplayerbot.conf.dist.in`,
`runtime/BotManager.cpp`, `conf/tortoise_bots.conf.dist`,
`tools/{analyze_quest_ledger.py,verify_tortoise_surface.sh}`, and
`docs/PLAYERBOTS_AUDIT.md`.

Copied / ported / independently reimplemented:

- No new upstream gameplay was copied in this pass.
- Existing existing behavior was kept where the local core data validates it;
  the factory collection-mount selection is an independent module adapter over
  the core `collection_mount` table and `MountManager` contract.
- Removed RTSC/SeeSpell/BossAura and later-ID branches are subtractive cleanup,
  not replacements with expansion behavior.
- Movement inspection now uses the local core's public targeted-generator
  target/current-motion contract; current follow/chase guards no longer depend
  on fabricated zero offsets or private donor fields. The chat-channel proxy
  delegates to the core's loaded `ObjectMgr` channel map.
- The dead `InstanceTemplate`, synthetic session-state, formation-slot,
  client-loot-type, group-roll, and donor `TransportAnimation` scaffolding was
  removed. Empty-path elevator generation now logs and skips explicitly because
  the pinned core has no transport-animation loader. Custom-start travel and
  death handling now use the core's Goblin/High Elf start rows after the actual
  runtime terrain/MMAP tiles were verified present. The exact High Elf VMap
  tile is absent and remains an explicit acceptance concern.
- Talent validation now sums all three trees, rejects missing prerequisites
  without dereferencing absent records, and initializes each DBC row's rank
  metadata independently.
- SQL changes are module-owned schema and additive compatibility migrations;
  the final `20260824090003_*` cleanup explicitly drops only obsolete,
  module-owned donor cache tables; no character state or database reset is
  involved.

Reason: the module must be a trustworthy Tortoise 1.18.1 foundation. Core
`LFTBotFill`, legacy `src/modules/PlayerBots`, and the remaining compatibility
fallback matrix are intentionally recorded as separate core/product follow-up,
not hidden inside this module PR.

Local validation:

- `tools/verify_tortoise_surface.sh` passed.
- `git diff --check` passed before commit.
- Cached `bash ../tortoise-docker-penqle/dev/build-playerbots` completed the
  static native module and `mangosd` link successfully. Its best-effort install
  phase reported only the sibling builder's absent `realmd` artifact.
- Cached `bash ../tortoise-docker-penqle/dev/build-off` completed the
  `BUILD_PLAYERBOTS=OFF`, `MODULES=disabled` `mangosd` build successfully.
- A disposable `git archive` of the tracked target core, with no
  `modules/TortoiseBots` checkout, configured as `modules: disabled (no
  modules found)` and built/linked `mangosd` successfully with both PlayerBots
  options off. The temporary archive/build directories were removed.
- The final incremental ON rebuild after the cache fail-closed and data-derived
  eligibility edits also linked successfully; the same optional `realmd`
  install warning remained.
- The final cached OFF rebuild after those edits completed `mangosd`
  successfully with `BUILD_PLAYERBOTS=OFF`, `BUILD_LEGACY_PLAYERBOTS=OFF`, and
  `MODULES=disabled`.
- A disposable MariaDB 11.4 container applied both configured `world` and
  `character` migration pairs twice; the final schemas had 32 scale columns,
  `template_changed`, and zero obsolete donor tables.
- The preserved Docker stack processed the configured lowercase module paths,
  applied both `20260824090003_*` cleanup migrations, reached AI-enabled
  world-ready, passed `PendingAddRemoveTest`, the six-step `AutoTest`, and the
  packet group invite/accept plus cleanup journey. No volume reset was used.
- The final ON rebuild after making the optional LLM generator inert by
  default, the complementary OFF rebuild, and a preserved-data server restart
  all passed. Startup no longer attempts to load the optional LLM prompt file;
  the module still reached AI-enabled world-ready with the empty-cache and
  direct-travel safeguards.
- The subsequent full cached ON rebuild after the movement/channel shim fix,
  the complementary OFF build, and a preserved-data restart also passed. The
  focused packet fixture then exercised `follow chat shortcut`, native group
  invite/accept, and cleanup on the updated binary without a movement-state
  crash. Startup retained the expected core warning that custom dungeon rows
  reference the missing `custom_dungeon_portal` script; no teleport behavior
  was invented in the module.
- The final fish-generation hardening rebuild passed both ON/OFF gates and a
  preserved-data restart. The current startup logged `No persisted fish
  locations; generation is disabled, using direct fishing fallback.` and did
  not log fish-grid generation or cache-save activity.
- The final expansion-residue pass removed the local-core-absent Mage mana-gem
  IDs `22044`/`33312`, Druid reagent IDs `22147`/`22148`, and post-60 lifetime
  formulas. The cached ON/OFF builds and preserved startup remained clean;
  local SQL confirmed `33312` is a non-mana item and the other three IDs are
  absent from the target item data.
- The unreachable `Engine::testMode` branch and its `test.log` writes were
  removed. The final ON/OFF builds and timestamp-scoped preserved restart
  reached world-ready with no test-file path or test-mode log activity.
- `Engine::LogAction` now uses bounded `vsnprintf` formatting, preventing
  long owner-controlled action names from overrunning its fixed log buffer.
- The corrected final packet fixture run passed the native command surface:
  `list`, `stats`, and owned-bot `follow` were dispatched through a synthetic
  `ChatHandler`, followed by group invite/accept and cleanup. This is runtime
  command-path evidence, not a real-client incoming-packet claim.
- The compatibility shim's ScriptDevAI-shaped gossip callback now delegates to
  core `sScriptMgr` creature-gossip registry; it no longer unconditionally
  returns false and discards core gossip behavior.
- The compatibility shim's taxi view now reads the live core
  `Player::GetTaxi().GetTaxiPath()` route for in-flight position reasoning, and
  loot status checks pass the native loot target into the core's ownership and
  condition evaluator. No focused taxi/loot gameplay journey is claimed from
  this compile/core-trace change.
- A forced CMake configure prints the supported builder's bind-mounted module
  root `/work/core/modules/TortoiseBots`, commit, and clean/dirty source state;
  Git's scoped safe-directory option avoids changing global configuration, and
  a dirty checkout is reported explicitly rather than being mistaken for an
  exact clean snapshot. This makes stale or locally modified module selection
  observable. The final implementation snapshot is `7e08fc8`.
- The real Tortoise client was launched under Wine through normal and
  software-forced rendering paths; both rendered black with no observable
  login UI in this environment, so no real-client `.bot` command journey is
  claimed.
- The final timestamp-scoped preserved-data restart of the updated binary
  reached native AI module load and world-ready. It showed the expected direct
  travel/fishing and empty-cache safeguards, emitted no `ai_playerbot_*` table
  DDL/DML, and retained the known core `custom_dungeon_portal` script warning
  for the audited custom-content gap. Taxi/loot interactions were
  not replayed as a ceremony; their module changes were compile- and
  core-API-traced.
- The updated runtime restart at `2026-08-25T01:41:31.245096072Z` reached
  native AI module load and world-ready with no talent-spec validation errors,
  no `ai_playerbot_*` table DDL/DML, and the expected missing-core
  `custom_dungeon_portal` warning. The current runtime data checks found the
  Goblin start `maps/0013245.map` + `mmaps/0013245.mmtile` and High Elf start
  `maps/0002536.map` + `mmaps/0002536.mmtile`; `vmaps/000_25_36.vmtile` is not
  present.
- The updated disposable packet fixture at `2026-08-25T01:48:13.297552013Z`
  passed native `list`/`stats`/`follow`, existing group invite/accept, and
  cleanup. Its temporary `PacketBridgeTest` enablement was restored to `0`,
  and the normal restart at `2026-08-25T01:48:57.408768993Z` reached
  world-ready.

## Final traced Tortoise compatibility closure — 2026-08-25

Feature: close the remaining module-owned Tortoise 1.18.1 compatibility
mismatches found by tracing active call sites against the pinned core: sparse
store bounds, core-defined custom races, path-filter fail-closed behavior,
native combat/interaction/auction/quest/skill semantics, later-ID cleanup,
localized names, factory class-spell initialization, native text-emote fallback,
loot status/roll state, and collection-mount caching.

Source repository: TortoiseBots `audit/playerbots-tortoise-1.18.1`

Source commit: `d672048e86b9effc36210d3e6d076741fbeccc7f` (final source snapshot;
the initial traced implementation is `0f97403df42ee98b5085040a9a066ddc64608623`,
followed by `f594fc1` removing the unreachable fish-cache generator and
`d672048` closing the remaining active emote, loot, locale, spell-error, and
collection-mount fallbacks).

Reference repositories and commits:

- Local core: `tortoise-wow@9487c5150a6553c665fafc1f4568669b8b00f011`
  (`playerbots-integration-gh`), used as the API/data authority; no core file
  was modified by this commit.
- Local CMaNGOS Classic PlayerBots host reference:
  `cmangos-mangos-classic@9b682be617ac61c127c23aa60d7b4ffbc0ce37e6`,
  specifically the `Player::learnClassLevelSpells` behavior used as intent for
  the module-local factory learner. The host implementation was not copied
  into the core.

Source files: `ai/cmangos-compat-shim.h`,
`ai/playerbot/{ChatHelper,PlayerbotAIConfig,PlayerbotFactory,TravelMgr,TravelNode,WorldPosition}.{cpp,h}`,
`ai/playerbot/strategy/{Value.cpp,values,actions,triggers,druid,rogue,warrior}/*`,
`runtime/PlayerbotRuntimeFacade.cpp`, and
`tools/verify_tortoise_surface.sh`.

Copied / ported / independently reimplemented:

- Store upper bounds, DBC race-name loading, locale-map formatting, path
  fail-closed guards, native wrapper substitutions, and local heal prediction
  are independently reimplemented against the pinned core APIs.
- Class trainer/quest spell initialization is a module-local port of the
  existing CMaNGOS behavior, narrowed to the core's `Quest`, `TrainerSpell`,
  `SpellMgr`, talent, and class/race contracts. It does not add a
  `PlayerBots`-specific core hook.
- Absent expansion IDs and invalid Tortoise branches are subtractive cleanup,
  validated against local DBC/SQL; no expansion behavior was introduced.

Reason: preserve Existing Vanilla behavior while ensuring that Tortoise custom
IDs, Goblin/High Elf data, localized content, and native core semantics are
not silently hidden behind donor-era constants or no-op compatibility methods.

Local validation: `tools/verify_tortoise_surface.sh`, `git diff --check`, and the
cached persistent `BUILD_PLAYERBOTS=ON`, `BUILD_LEGACY_PLAYERBOTS=OFF`, static
`mangosd` build/link passed at this source commit. The build compiled the
module and linked the final `mangosd`; no core, Docker, reference checkout, or
database reset was performed. Runtime restart evidence for this exact commit
is recorded in the audit after installation: the preserved stack restarted at
`2026-08-25T04:01:18.842808942Z` and reached world-ready at
`2026-08-25T04:01:47.286072886Z`, with
native module load, TalentSpecs load, direct travel/fishing fallback, and no
module-table DDL/DML. The restart also confirmed the pinned core's
unregistered-content script warnings; no module replacement was invented.
Disposable custom-start fixtures then passed native AutoTest: Goblin guid 7
passed from `04:10:16.382Z` through cleanup at `04:10:49.840Z`, and High Elf
guid 8 passed from `04:12:11.086Z` through cleanup at `04:12:44.559Z`. The
fixture rows/state were removed and `AutoTest` restored to `0`; the normal
restart at `04:14:19.562258645Z` reached world-ready at `04:14:40.631920103Z`.
No unrun gameplay acceptance is claimed for terrain movement/death, physical
collection-mount use, taxi, loot, or real-client packet delivery.

## Surface verifier fail-closed correction — 2026-08-25

Feature: make `tools/verify_tortoise_surface.sh` fail closed when its required
ripgrep dependency is unavailable, so the surface/audit gate cannot report a
false success after `rg` returns command-not-found.

Source repository: TortoiseBots `audit/playerbots-tortoise-1.18.1`

Source commit: `9e9567c996d1cbf5c2c3f5949453499589600d4e` (implementation
commit; the subsequent audit/provenance edit is documentation-only).

Source files: `tools/verify_tortoise_surface.sh`.

Copied / ported / independently reimplemented: independently implemented as a
single `command -v rg` prerequisite check before repository setup and all
`rg`-based checks. No compatibility stub, replacement search implementation,
or core change was added.

Reason: with `set -e` and `if rg ...; then` conditions, a missing `rg` can be
treated as an ordinary false condition and allow the final success message to
be printed. The verifier must fail closed so its audit/provenance evidence is
meaningful.

Local validation:

- Missing-ripgrep negative test:
  `env -i PATH=/tmp/tortoisewow-no-ripgrep /bin/bash tools/verify_tortoise_surface.sh`
  exited `1` and printed
  `TortoiseBots surface check failed: ripgrep (rg) is required to verify the
  Tortoise module surface`; it did not print `Tortoise WoW 1.18.1 module surface:
  OK`.
- Full verifier with ripgrep available:
  `bash tools/verify_tortoise_surface.sh` exited `0` and printed
  `Tortoise WoW 1.18.1 module surface: OK` on the current source tree.
- `git diff --check` exited `0` for the implementation correction.
- No C++ build, Docker image rebuild, database migration, gameplay fixture, or
  runtime test was run for this shell/docs-only change.

The earlier closure entries that record the verifier as passed are retained as
historical normal-environment evidence. They did not test the missing-rg path;
the explicit negative and positive results above are the authoritative
validation for this correction. F-03 and F-27 remain open core/data follow-ups;
this change does not add scripts, stubs, or module-side replacements for them.

## F-03/F-27 core integration closure — 2026-08-25

Feature: remove the remaining legacy PlayerBots-specific core product surface
and reconcile the locally provable Tortoise ScriptName mismatches.

Source repository: local `tortoise-wow` core plus its local Tortoise SQL;
TortoiseBots module checkout for the optional-module build.

Source commit: core
`7353989c94399f80572a2f8ec2eb73c63a6c79f8` on
`cleanup/f03-f27-code-freeze`; TortoiseBots code checkpoint
`07cf7976c546fac27083c7b46e73299c25b095f3` on the same-named branch, followed
by the final documentation commit.

Source files: core `CMakeLists.txt`, `src/game/{CMakeLists.txt,Chat,Handlers,
LFT,Objects,ScriptMgr.h,SessionTransport.h,SharedDefines.h,Spells,World*,
vmap}`, `src/mangosd/{CMakeLists.txt,Master.cpp,mangosd.conf.dist.in}`,
`src/scripts/{CMakeLists.txt,miscellaneous/random_scripts_1.cpp,
spells/spell_druid.cpp}`, `tools/vmap_assembler/CMakeLists.txt`, and
`sql/database_updates/world/20260825090000_world.sql`; module
`ai/playerbot/PlayerbotHelpMgr.cpp` and
`ai/playerbot/strategy/actions/DebugAction.cpp`.

Copied / ported / independently reimplemented:

- No upstream behavior was copied.
- F-03 is subtractive core cleanup plus replacement of account-name checks with
  the existing generic `Script_IsMachineDriven` capability. No new PlayerBots
  host seam was introduced.
- F-27 `npc_teslinah` is a registration of the existing local callback; it is
  not a new implementation. The `script_name='0'` migration is an independent
  data correction for an invalid placeholder. The remaining unregistered
  Tortoise names were deliberately not implemented because their behavior is not
  established by the pinned core/history.

Reason: keep Tortoise core generic and optional, make TortoiseBots the only
supported PlayerBots implementation, and avoid converting missing Tortoise
content into fake success paths.

Local validation:

- Cached native ON/static `mangosd` build passed with
  `BUILD_LEGACY_PLAYERBOTS=OFF`.
- Cached native module build passed with `BUILD_PLAYERBOTS=OFF`,
  `BUILD_LEGACY_PLAYERBOTS=OFF`, and `MODULE_TORTOISEBOTS=static`.
- Cached module-disabled build passed with `BUILD_PLAYERBOTS=OFF`,
  `BUILD_LEGACY_PLAYERBOTS=OFF`, and `MODULES=disabled`.
- Preserved Docker runtime applied migration
  `20260825090000_world` (hash
  `9DD6905D83E17F6D0BD08CABC5618BBD2A5AD513`), loaded the native module, and
  reached `World server is up and running`.
- Runtime query found zero literal `script_name='0'` rows across all seven
  ScriptMgr registry tables and retained two `npc_teslinah` rows.
- Startup no longer reports `0` or `npc_teslinah`; the remaining 17 warnings
  are recorded as unverified content gaps in `PLAYERBOTS_AUDIT.md`.

## Penqle #411/#416 compatibility and stack checkpoint — 2026-08-26

Feature: align the complete optional TortoiseBots stack with the Penqle
Headless/session surface from #411 and the generic character/LFT/BG surface
from #416, without adding PlayerBots concepts to core.

Source repositories and commits: tortoise-wow/tortoise-wow #411
`c37e28b632dee3c73896240c1b399fdbb7c35ef8`; generic core PR #416
`5261e5317c3115aa8a0b61d8eb9d85a79766be95`; TortoiseBots stack heads #37
`c2893d3cb3b561988cbc163009d58b91194ac5f9` through #42
`2abd4f7eea50c740597681b32b5af9bdab58e427`.

Source files: module-local `ai/playerbot/*`, `runtime/*`, `host/*`,
`commands/*`, and feature configuration/documentation. Core #416 adds only
bot-neutral public lifecycle/snapshot interfaces and a generic
`Player::GetHomeBindLocation()` getter over existing character state.

Copied / ported / independently reimplemented: donor API calls were replaced
with the target core's public APIs (native distances, loot/mail, gossip,
trainer, transport/spline, battleground, and packet handlers). Missing
behavior was fail-closed or routed through existing module facades; no private
BG/LFT/AH state, fake queue fallback, raw character SQL, or PlayerBots-aware
core coupling was added. The BG service now consumes #416's copy-only demand
snapshot and queues only for observed human demand.

Reason: preserve module ownership and core optionality while making the broad
Vanilla/Tortoise donor behavior compile against the actual target core API shape.

Local validation: `git diff --check`, Tortoise surface audit, parent-diff audits,
GitHub `CLEAN/MERGEABLE` audit for #37–#42, and cached integrated module target
passed. Full native ON/static `mangosd` linked with `BUILD_PLAYERBOTS=OFF`,
`BUILD_LEGACY_PLAYERBOTS=OFF`, `MODULES=static`,
`MODULE_TORTOISEBOTS=static`; complementary `MODULE_TORTOISEBOTS=OFF`
mangosd also linked. The installed ON binary accepted `--version`. No live
server/gameplay smoke test was run; this remains pre-merge evidence until the
actual core #411/#416 commits are merged and rebuilt.

## Cleaned merge candidate — 2026-08-29

Feature: remove the accidental PR #43 dependency and migrate the complete
module stack to the manager-owned Headless lifecycle.

Source repositories and commits:

- tortoise-wow/tortoise-wow PR #411 candidate `f62cd95b63439ebdc7915053016ffa2753f98121`
  (rebased on upstream `main` `05912a49f7cd8f12afff04b3c37e6f852f981268`).
- tortoise-wow/tortoise-wow PR #416 candidate
  `5368fda0d884b4ff91960772ebf8e3f65f991850`, based on the cleaned #411
  candidate.
- TortoiseBots tested code `c9643eb14eed09c21aea087950ffef5460f239b0`;
  subsequent changes in this branch are documentation-only.

The final module branch was reconstructed from the PR #42 tip. PR #43's
pullback/summon implementation is not present. The module now calls only the
generic core `StartHeadlessSession` / `StopHeadlessSession` /
`GetHeadlessSessionState` façade; it does not construct, dispatch, promote,
log out, or delete Headless `WorldSession` objects.

Local validation:

- `git diff --check` passed for the cleaned core and module candidates.
- `tools/verify_tortoise_surface.sh` passed.
- Docker native static build passed with `BUILD_PLAYERBOTS=OFF`,
  `MODULES=static`, and `MODULE_TORTOISEBOTS=static`, reaching
  `[100%] Built target mangosd`.
- Complementary module-disabled build passed with `BUILD_PLAYERBOTS=OFF`,
  `MODULES=disabled`, and `MODULE_TORTOISEBOTS=disabled`, reaching
  `[100%] Built target mangosd`.
- The enabled image loaded `TortoiseBots (AI enabled)` and reached
  `World server is up and running!` without fatal startup errors.
- The temporary runtime stack was stopped without resetting or removing
  database volumes.

No real-client login, reconnect/reclaim, stale-callback adversarial probe, or
live LFT/BG/AH gameplay acceptance is claimed by this checkpoint.

## Corrected core review follow-up — 2026-08-29

Core review fixes were applied after the previous candidate:

- removed the stale `src/game/PlayerBots` include from module build glue;
- restored full GPL headers on the new generic core headers;
- preserved the normal Network character-list count during extracted
  character creation and removed the unintended account-limit behavior;
- used `SessionTransport::Headless` for the transient character-creation
  helper;
- reattached the Player to the replacement Network session before destroying
  the manager-owned Headless session.

Corrected candidates:

```text
Core #411:   8037fc8cc4c8c5734aafb9dc43858205bcf9051f
Core #416:   e63161c2da7f13ab25687ea389026aa2e3c97647
Module:      b9c7784accb8c719e8d7aadd2f6a9e0bda8d07a2
```

The corrected pair passed the native static and module-disabled Docker builds.
The corrected enabled image loaded TortoiseBots and reached
`World server is up and running!` without fatal startup errors. No database
volume reset was performed.

Real-client login/reconnect/reclaim and live LFT/BG/AH gameplay remain
unclaimed manual acceptance gates.

## Interrupt action shell — 2026-09-05

Feature: player-facing `.bot action interrupt` with server-side executor
selection and addon control.

Source repositories and commits:

- `mod-playerbots@5397110cba484a9b7209bc9f632652e9d4bd6a70`, representative
  class interrupt strategy/action registrations under `src/Ai/Class/*` and
  `src/Ai/Base/Actions`/`Triggers`.
- `Shyalya/tortoise-wow@1f9497e0f42bfc1055841bb6ebdc7caa3515de0b` and
  `cmangos/playerbots@076045efa835da9aab7c943bca752aebe1baad`, used as
  behavior comparisons only.

Source files: `commands/BotCommandContext.{h,cpp}`,
`commands/BotCommands.cpp`, and the companion manager's
`Constants.lua`, `UI.lua`, `README.md`, and `tests/regression.lua`.

Copied / ported / independently reimplemented: no donor code was copied.
The module adds a thin capability probe over already compiled Vanilla/Tortoise
class and pet actions (`counterspell`, `silence`, `spell lock`, `kick`,
`pummel`, `shield bash`, `bash`, `hammer of justice`, `repentance`, `earth
shock`, and `death coil`). It validates the target's active cast and the
actual spell's interrupt metadata, chooses one owned executor, and delegates
casting/reach movement to existing PlayerbotAI actions. The addon sends one
`.bot action interrupt` intent and consumes the existing structured ACK/ERR
transport.

Reason: make interrupt a portable executor command without adding a second
combat engine, a class-to-spell policy table, or any core bot coupling.

Local validation: addon regression checks passed; cached native static
`mangosd` module build passed after the new command/context code; the native
runtime action path remains a real-client/gameplay acceptance gate.

## Per-bot CC mark assignment — 2026-09-05

Feature: generalized `cc <raid-mark>` assignment with a bot-scoped addon picker
and Party-tab visibility for the current assignment.

Source repositories and commits:

- `mod-playerbots@5397110cba484a9b7209bc9f632652e9d4bd6a70`, `RtiAction`,
  `RtiCcValue`, `RtiCcTargetValue`, `CcTargetValue`, and CC trigger behavior.

Source files: `commands/BotCommands.cpp`, `README.md`, `docs/HOST_API.md`,
`docs/PLAYER_CONTROL.md`, and the manager's `Constants.lua`, `Roster.lua`,
`Comms.lua`, `UI.lua`, `README.md`, `TortoiseBotsManager.toc`, and
`tests/regression.lua`.

Copied / ported / independently reimplemented: the module keeps the donor's
per-AI mark preference and mature CC target/trigger path, but independently
validates the eight Vanilla raid-mark names, persists the selected bot's
`rti cc` value through the existing `PlayerbotDbStore`, and emits a separate
`TBM:CC_ASSIGN_*` snapshot. CC executor discovery now queries explicit
capability metadata on the mature action graph, covering the registered Mage,
Warlock, Priest, Druid, Rogue, Hunter, and Paladin CC actions instead of a
parallel class table. The addon uses normal target selection, a compact
raid-icon picker, and server-owned assignment display; it does not add a core
bot-aware `Group::SetTargetIcon` seam.

Reason: support Circle-to-Warlock / Moon-to-Mage style assignments while
preserving the global core raid-icon slots and the Actions-vs-Roster boundary.

Validation: `lua5.1 tests/regression.lua .`,
`tools/verify_tortoise_surface.sh`, and the cached Docker builder's native
`mangosd` target passed. Runtime logout/relogin persistence and real-client CC
reapplication remain manual acceptance gates.

## Explicit combat engagement and pull completion — 2026-09-05

Feature: reliable party attack engagement for ranged/healing bots and explicit
pull-versus-pullback completion semantics.

Source repositories and commits:

- `cmangos/playerbots@076045efa835da9aab7c943bca752aebe1baad`,
  `AttackAction`, `PullAction`, `PullStrategy`, pull triggers, and return-position
  actions, used as the behavioral baseline.
- `mod-playerbots@5397110cba484a9b7209bc9f632652e9d4bd6a70`,
  modern explicit/prioritized target handling, used as a behavior comparison.

Source files: `ai/playerbot/strategy/actions/AttackAction.cpp`,
`PositionAction.cpp`, `PullActions.cpp`, `generic/PullStrategy.{h,cpp}`,
`triggers/{GenericTriggers,PullTriggers}.cpp`,
`values/InvalidTargetValue.cpp`, and `commands/BotCommands.cpp`.

Copied / ported / independently reimplemented: the mature class rotations,
pull action selection, and movement actions remain in place. The local fix
independently invalidates cached target values when a player commits an attack,
allows that explicit target through the pre-threat validity window, runs a
normal first AI decision, and records when the selected melee/ranged pull
action actually succeeds. Pullback then returns to the requester position
captured at command time and retains that anchor until arrival; ordinary pull
hands control to normal combat after the pull action succeeds.

Reason: melee auto-attack created threat immediately and masked stale target
state, while ranged bots discarded the command target before their first spell
and the invalid-target action could starve healer actions. The donor pull state
also reused `RequestPull` after success, rearming its start phase, and could
erase the return anchor on timeout before the tank arrived.

Local validation: `tools/verify_tortoise_surface.sh`, `git diff --check`, and the
Docker release build of the native `mangosd` target passed. Focused real-client
attack/pullback acceptance remains a manual gate.

The follow-up explicit-engagement fix also adapts the donor prioritized-target
intent in the shared Tortoise target values: a valid command-only `explicit
attack target` remains ahead of DPS/AoE/tank assist selection until the command
target is no longer valid. Ordinary autonomous `attack target` state remains
outside that priority path. No donor target-value or command layer was copied
wholesale.

## Tactical action prerequisite handoff — 2026-09-06

Feature: align legacy Pullback with the Actions pull path and preserve mature
reach-then-cast behavior for explicit Interrupt and CC requests.

Source repositories and commits:

- `mod-playerbots@5397110cba484a9b7209bc9f632652e9d4bd6a70`, used to confirm
  that interrupt/CC spell actions own their reach prerequisites and that
  command targeting should not invent a second movement policy.
- `Shyalya/tortoise-wow@1f9497e0f42bfc1055841bb6ebdc7caa3515de0b`, used only as
  the Tortoise/Vanilla behavior comparison for the existing PullStrategy and
  class action graph.

Source files: `commands/BotCommands.cpp`, `commands/BotCommandContext.*`, and
`ai/playerbot/strategy/Engine.*`.

Copied / ported / independently reimplemented: no donor code was copied. The
module now shares its existing stay/follow relaxation and first normal tick
between legacy and structured Pullback. When a direct tactical cast is out of
range, a thin module-owned queue entry lets the active mature Engine execute
the action's existing prerequisite/continuation chain; no class-to-range table
or command-side movement controller was added.

Reason: legacy Pullback could leave a tank held in stay/follow and wait for a
later tick, while direct Interrupt/CC execution bypassed prerequisites and
could lose the reach-then-cast action after the command returned.

Local validation: `git diff --check`, `tools/verify_tortoise_surface.sh`, and
`tools/verify_penqle_host_contract.sh` passed after the coherent edit batch.
No Docker/server/client gameplay run was performed; Pull/Pullback completion,
interrupt timing, pet behavior, and CC reapplication remain manual gates.

## Golden-party healer target reach — 2026-09-06

Feature: keep injured or dispellable party members discoverable until the
mature heal/cure action can run its existing reach prerequisite.

Source repository and commit:

- `mod-playerbots@5397110cba484a9b7209bc9f632652e9d4bd6a70`,
  `Ai/Base/Value/PartyMemberToHeal.cpp` and
  `Ai/Base/Value/PartyMemberToDispel.cpp`, used for the target-selection
  distance intent.

Copied / ported / independently reimplemented: the Tortoise values retain
their existing group, map, life-state, pet, and Vanilla spell checks. The heal
value now keeps candidates within twice the configured heal range so its
registered `reach party member to heal` prerequisite can close the gap; the
dispel value leaves range enforcement to the cure action while it queues that
same reach path. Incoming-damage prediction is clamped at zero so a lethal
preheal estimate cannot wrap unsigned health and suppress an emergency heal.
The unregistered forward-ported Priest strategy files also no longer carry the
expansion-only `divine hymn`/`hymn of hope` nodes; their group-heal fallbacks use
the registered Vanilla actions instead.

Reason: filtering at cast range made out-of-range party healing/dispelling
unreachable even though the mature action graph already owns movement and the
actual cast-range legality check. No command-side healer or new target-selection
system was added.

Local validation: `git diff --check`, `tools/verify_tortoise_surface.sh`, and
`tools/verify_penqle_host_contract.sh` passed. No Docker/server/client gameplay
run was performed; the Golden Party healing, mana, dispel, and recovery checks
remain manual acceptance gates.

## Owned-party movement state — 2026-09-06

Feature: keep owner-controlled Follow/Stay/Come transitions coherent across
the PlayerbotAI reaction, combat, and non-combat engines, and fail a summon
cleanly when the core rejects its teleport.

Source repositories and commits:

- TortoiseBots' existing `PlayerbotAI::SetMovementStrategy` implementation,
  introduced in `f6a6c6b683a955d7747a0b4b91291631eb15a509`, is the local owner of
  cross-engine movement state.
- `mod-playerbots@5397110cba484a9b7209bc9f632652e9d4bd6a70`,
  `Ai/Base/Actions/ChatShortcutActions.cpp`, was consulted for the mature
  shortcut/anchor intent only.

Copied / ported / independently reimplemented: Follow and Stay shortcuts now
delegate their existing strategy changes through the local central setter,
while retaining formation, return-anchor, and position bookkeeping. Come/Hold
uses the same setter. The tactical relaxation helper clears a reaction-level
Stay copy introduced by that transition. Follow refreshes cached master/follow
targets after an ownership rebind. `PlayerConvenience` now removes a pending
summon immediately when `TeleportTo` rejects the request rather than treating
the unchanged position as arrival.

Reason: the public shortcuts previously changed only combat/non-combat state,
leaving a reaction-level Follow/Stay strategy or stale master-derived target to
fight the next command. The summon path could also enter its arrival phase
after a rejected teleport. No new movement controller or core bot seam was
added.

Local validation: `git diff --check`, `tools/verify_tortoise_surface.sh`, and
`tools/verify_penqle_host_contract.sh` passed for the movement change. No
Docker/server/client gameplay run was performed; Follow/Stay/Come/Summon and
repeated Golden Party transitions remain manual acceptance gates.

## Headless teleport acknowledgement — 2026-09-06

Feature: complete near/far Player teleports for module-owned Headless bots
before their normal AI update resumes.

Source repositories and commits:

- `mod-playerbots@5397110cba484a9b7209bc9f632652e9d4bd6a70`,
  `Bot/PlayerbotMgr.cpp`, whose session loop calls
  `PlayerbotAI::HandleTeleportAck()` for teleporting bots.
- Tortoise `tortoise-wow` core's public `ObjectAccessor::FindPlayerNotInWorld`
  and `WorldSession` teleport-ack methods, used without a new core seam.

Copied / ported / independently reimplemented: `BotManager::UpdateBots` now
performs the donor manager's acknowledgement at the module world-thread
boundary, using the public not-in-world lookup because a far-teleporting Player
is intentionally absent from `FindPlayer`. The module also keeps AI paused for
the core's one-tick pending-far-teleport marker and waits for the near/far ACK
semaphore before acknowledging. Normal AI updates remain behind the existing
usability gate and are skipped during the transition.

Reason: Headless sessions have no client to send `MSG_MOVE_TELEPORT_ACK` or
worldport ACK packets. Without this module-owned replacement, following an
instance area trigger or another far movement could leave the Player in
teleport limbo and stop all subsequent movement/combat decisions.

Local validation: `git diff --check`, `tools/verify_tortoise_surface.sh`, and
`tools/verify_penqle_host_contract.sh` passed. No Docker/server/client gameplay
run was performed; instance entry and cross-map movement remain manual gates.

## Party resurrection reach — 2026-09-06

Feature: keep a dead owned-party member selectable until the mature resurrection
action can reach the corpse and cast a Vanilla resurrection spell.

Source repository and commit:

- `mod-playerbots@5397110cba484a9b7209bc9f632652e9d4bd6a70`,
  `Ai/Base/Value/PartyMemberToResurrect.cpp` and
  `Ai/Base/Actions/ReachTargetActions.*`, used for the separation between
  corpse selection and cast-range movement.

Copied / ported / independently reimplemented: Tortoise keeps its existing
  group, map, corpse-state, resurrection-request, and duplicate-cast checks.
  The value no longer rejects a corpse solely for being outside spell range;
  the registered `reach party member to resurrect` prerequisite now owns that
  movement, and `ResurrectPartyMemberAction` requests it explicitly.

Reason: an out-of-range corpse disappeared before the existing cast action could
  queue movement, and the old prerequisite targeted the living-heal value rather
  than the corpse value. No new resurrection controller or expansion-only spell
  behavior was introduced.

Local validation: `git diff --check`, `tools/verify_tortoise_surface.sh`, and
`tools/verify_penqle_host_contract.sh` passed. No Docker/server/client gameplay
run was performed; death, resurrection, and post-revive regroup remain manual
acceptance gates.

## Golden-party class compatibility audit — 2026-09-06

Feature: keep owned characters' saved talent builds intact and close a small
set of Vanilla/Tortoise strategy wiring defects found during the pre-playtest
class audit.

Source repositories and data:

- Tortoise 1.18.1 `Spell.dbc`, `Talent.dbc`, `TalentTab.dbc`, and
  `tw_world_spell_template.sql` were the authority for spell/talent IDs and
  availability.
- `mod-playerbots@5397110cba484a9b7209bc9f632652e9d4bd6a70` was consulted for
  baseline Combat Rogue/Mage/Priest intent, including party-targeted Prayer of
  Healing; no later-expansion class subsystem was copied.
- Shyalya's Tortoise PlayerBots fork was used only as a comparison point. Its
  Cold Snap ID check and unconditional Defensive Tactics stance swap were not
  treated as compatibility evidence.

Copied / ported / independently reimplemented: module-owned bots now skip the
automatic `auto talents` action used during attachment and level-up, while an
explicit `talents ...` command remains available. The shipped alt-bot strategy
overrides are empty like the current donor defaults, so role/spec strategies
remain authoritative. Prayer of Healing now uses the existing AOE-heal target
and reach action and is wired into Holy's AOE strategy. The unverified
Tortoise-specific Defensive Tactics Berserker swap was removed because its
high-health trigger fought the normal Defensive Stance trigger (and was not
safe without the learned talent and shield). Cold Snap uses the learned named
spell and only fires when a known Frost cooldown is waiting; the low-level
Mana Gem helper now fails closed instead of reading an uninitialized ID.
Untalented low-level Rogues use the Combat fallback so the baseline Sinister
Strike rotation is available.

The follow-up talent-command path compares the learned talent topology before
and after a command. Only a changed topology updates the cached spec and calls
the existing `ResetStrategies()` once; query/list, invalid, and no-op commands
do not rebuild engines. Because the store persists complete `co`/`nc`/`dead`/
`react` strategy snapshots rather than deltas, a changed topology deletes those
strategy rows for every preset before the reset. Independent `value` rows are
retained, and loading a value-only preset leaves the newly rebuilt defaults in
place; future strategy saves create a fresh snapshot for the new build.

Reason: attachment-time talent mutation could rewrite an existing human build;
the inherited strategy config could replace Protection/Holy defaults with DPS
siblings; and several small Vanilla paths either used the wrong spell ID,
selected a self target for a group heal, or performed a stance swap without
the required Tortoise talent/equipment.

Local validation: Tortoise premade links for classes 1/4/5/8 were checked
against the current talent DBC (all generated links passed structural checks),
then `git diff --check`, `tools/verify_tortoise_surface.sh`, and
`tools/verify_penqle_host_contract.sh --core ../tortoise-wow` were run for the
implementation. No Docker/server/client gameplay run was performed; talent
preservation, class rotations, CC/AOE interaction, and dungeon healing remain
manual acceptance gates.

The Prayer of Healing trigger intentionally keeps the existing caster-centred
`AoeHealValue` activation count. The spell itself is target-centred, so a party
cluster 30–40 yards from the Priest can conservatively defer the group-heal
trigger; ordinary single-target healing/reach remains available and brings the
Priest into range. This is a manual edge-case check, not a reason to change the
generic value for every healer class.

`IsOwnedBot()` uses the module record plus `!BotManager::IsRandomBot()`. A
configured free-alt/always-online character is still an owned character and
therefore remains protected from automatic talent mutation; only the random
population identity opts into autonomous talent behavior.

## Combat: Target deduplication and AoE density clustering hardening (Issue #91) — 2026-09-08

Feature: Deduplicate hostile units in `AttackersValue` and harden `AoeCountValue::FindMaxDensity()` against false AoE clustering.

Source repository: `playerbots-references/shyalya-tortoise-wow` (API oracle for Tortoise runtime) and `playerbots-references/mod-playerbots` (behavior donor for target uniqueness and non-threat filtering).

Source files:
- `AttackersValue.cpp`: `AttackersValue::Calculate()`
- `AoeValues.cpp`: `AoeCountValue::FindMaxDensity()`, `AoePositionValue::Calculate()`

Copied / ported / independently reimplemented:
- In `AttackersValue::Calculate()`:
  - Resolved target duplication when `sPlayerbotAIConfig.shareTargets` is active: bot-specific targets (`current target`, `old target`, `attack target`, `pull target`) are collected into a `std::set<ObjectGuid>` before merging to prevent duplicate insertions for the same mob.
  - The merged list is validated and filtered through a distinctness filter (`std::set<ObjectGuid> seen`), enforcing the invariant that `attackers` contains strictly distinct hostile units.
  - The `getOne` qualifier (`attackers::1`) is now evaluated early and respected in the `shareTargets` path.
  - The fallback local target calculation path also guarantees distinctness via `seen.insert(target->getObjectGuid())`.
- In `AoeValues.cpp`:
  - `AoeCountValue::FindMaxDensity()` deduplicates incoming unit GUIDs into `std::set<ObjectGuid> uniqueUnits` before distance loops.
  - Cluster groups are stored in `std::map<ObjectGuid, std::set<ObjectGuid>> groups`, guaranteeing that duplicate creature GUIDs can never inflate cluster density.
  - `AoePositionValue::Calculate()` initializes bounding-box coordinates with a `first` guard, preventing reads of uninitialized stack variables when the first unit in a group is null.

Reason: Fix engine bug where single-mob pulls duplicated targets across shared party member lists, inflating density up to 3–4 and causing bots of all classes to prematurely cast expensive AoE abilities (Rain of Fire, Cleave, Thunder Clap, Blizzard).

Local validation:
- `tools/verify_tortoise_surface.sh`: OK
- `tools/verify_penqle_host_contract.sh --core ../tortoise-wow`: OK
- Standalone regression test suites (`tools/test_attackers_aoe_density.py` and `tools/test_attackers_aoe_density.cpp`): 6/6 tests PASS, proving:
  1. 1 mob with duplicate target references yields count = 1, density = 1, AoE inactive.
  2. 2 mobs with duplicate references yield count = 2 != 3+, AoE inactive.
  3. Real clustered 3 mobs yield count = 3, AoE eligible (>= 3).
  4. 3 spread mobs (> 2 * aoeRadius) yield density = 1, AoE inactive.
  5. Defensive FindMaxDensity with raw duplicate inputs returns count = 1.
  6. `attackers::1` qualifier returns exactly 1 distinct attacker.

## Class: Warlock Combat AI Overhaul (Issue #92) — 2026-09-08

Feature: Overhaul Warlock combat rotations, priorities, health/mana sustain, pet combat behavior, and AoE channel safeguards.

Source repository:
- `playerbots-references/mod-playerbots`: Behavior donor for `PetAttackTrigger`, `PetAttackAction`, and `RainOfFireChannelCheckTrigger`.
- `playerbots-references/shyalya-tortoise-wow`: Oracle for Tortoise 1.18.1 engine integration and pet spell autocast semantics.

Source files:
- `ai/playerbot/strategy/warlock/WarlockStrategy.cpp`
- `ai/playerbot/strategy/warlock/AfflictionWarlockStrategy.cpp`
- `ai/playerbot/strategy/warlock/DestructionWarlockStrategy.cpp`
- `ai/playerbot/strategy/warlock/DemonologyWarlockStrategy.cpp`
- `ai/playerbot/strategy/warlock/WarlockActions.h`
- `ai/playerbot/strategy/warlock/WarlockTriggers.h`
- `ai/playerbot/strategy/warlock/WarlockTriggers.cpp`
- `ai/playerbot/strategy/warlock/WarlockAiObjectContext.cpp`
- `ai/playerbot/strategy/actions/GenericActions.h`
- `ai/playerbot/strategy/actions/GenericActions.cpp`
- `ai/playerbot/strategy/actions/ActionContext.h`
- `ai/playerbot/strategy/actions/ChatActionContext.h`
- `ai/playerbot/strategy/triggers/GenericTriggers.h`
- `ai/playerbot/strategy/triggers/GenericTriggers.cpp`
- `ai/playerbot/strategy/triggers/TriggerContext.h`

Copied / ported / independently reimplemented:
- Life Tap Rebalancing & Safety Floor:
  - Rebalanced `life tap` priority in `WarlockStrategy::InitCombatTriggers` from `ACTION_HIGH + 3` (23.0f) to `ACTION_NORMAL` (10.0f). Prevents Life Tap from locking out primary combat DoTs (`curse of agony` at 12.0f, `corruption` at 11.0f) and AoE spells.
  - Guarded `LifeTapTrigger::IsActive()` and `CastLifeTapAction::isUseful()` with a configurable safety threshold (`health > sPlayerbotAIConfig.lowHealth`), breaking the death spiral while respecting server-configured bot health thresholds.
- Pet Combat Integration:
  - Implemented `PetAttackTrigger` and `PetAttackAction` using `CMSG_PET_ACTION` / `ACT_COMMAND` + `COMMAND_ATTACK`, registered in `GenericTriggers` and `GenericActions` for all pet classes.
  - Extracted side-effect-free shared engagement validation into `AttackAction::CanPetAttack(ai, pet, target)` to ensure both `AttackAction` and the new pet-command path strictly respect `WaitForAttackStrategy::ShouldWait`, combat `stay` range limits, passive stance, breakable/unbreakable CC, and damage immunity. Passive-to-defensive stance normalization is preserved strictly in execution paths (`AttackAction::Attack` and `PetAttackAction::Execute`).
  - Fixed dormant pet triggers in `WarlockPetStrategy::InitCombatTriggers` (`pet attack` at `ACTION_HIGH + 2`, `has aggro` -> `torment` at `ACTION_HIGH`).
  - Added non-combat `blood pact` party buff trigger for Imp.
  - Implemented `CastTormentAction`, `CastBloodPactAction`, and `CastFireboltAction` derived from `CastPetSpellAction`.
  - Fixed copy-paste bug across all three spec strategies (`AfflictionWarlockPetStrategy`, `DestructionWarlockPetStrategy`, `DemonologyWarlockPetStrategy`) where `InitCombatTriggers` inadvertently called `InitNonCombatTriggers` instead of `WarlockPetStrategy::InitCombatTriggers`.
- Affliction Sustain & Leveling Rotation:
  - Added `Drain Life` sustain triggers to `AfflictionWarlockStrategy`: `low health` (< 40%) -> `drain life` (`ACTION_HIGH`), `medium health` (< 70%) -> `drain life` (`ACTION_NORMAL + 2`), with `isUseful()` safety guard (< `almostFullHealth`).
  - Added early leveling `immolate` trigger (`ACTION_NORMAL`) to `AfflictionWarlockStrategy`.
  - Adjusted `DrainSoulTrigger::IsActive()` target health threshold from <= 15% to <= 25%, ensuring group kills register channel ticks in time to reap soul shards.
- AoE Rotation & Channel Interruption:
  - Implemented `RainOfFireChannelCheckTrigger`: detects active channeled Rain of Fire and activates if clustered enemies drop below 2 (`aoe count < 2`), triggering `cancel channel` (`ACTION_HIGH + 3`) to immediately save mana.
  - Lowered multi-dotting priorities in AoE strategies (`corruption on attacker`, `siphon life on attacker`, `curse of agony on attacker`) to `ACTION_HIGH - 1` (19.0f), allowing `rain of fire` (`ACTION_HIGH`, 20.0f) to reliably cast against 3+ grouped mobs.


## Class port Batch 1-3 (2026-09-08, uncommitted)
Feature: Talent prerequisite correctness + Arcane Power safety + Warrior Master Strike + Priest shields/Chastise
Source repository:
- `playerbots-references/mod-playerbots` @ b949b50 (mature behavior donor)
- `playerbots-references/shyalya-tortoise-wow` @ 83a61bc (Tortoise runtime reference)
- `tortoise-wow` @ 9f778a73 (effective spell/template/script source)
Source files:
- `ai/playerbot/Talentspec.cpp`, `ai/playerbot/aiplayerbot.conf.dist.in`, `tools/talents/validate_presets.py`
- `ai/playerbot/strategy/mage/MageTriggers.h/.cpp`, `ai/playerbot/strategy/mage/MageActions.h`
- `ai/playerbot/strategy/warrior/WarriorActions.h`, `WarriorTriggers.h`, `WarriorAiObjectContext.cpp`, `ArmsWarriorStrategy.cpp`, `FuryWarriorStrategy.cpp`
- `ai/playerbot/strategy/priest/PriestActions.h`, `PriestStrategy.cpp`
- Core evidence: `src/game/Objects/Player.cpp` LearnTalent, `src/scripts/spells/spell_mage.cpp` (arcane power/rupture/icicles), `spell_warrior.cpp` (master strike), `spell_priest.cpp` (chastise/enlighten), `sql/base/tw_world_spell_template.sql`, `data/dbc/Talent.dbc`
Copied / ported / independently reimplemented:
- Talent DependsOnRank zero-based fix + DependsOnSpell talent check (independent fix from core semantics; validator mirrors ReadTalents).
- Arcane Power 70% mana gate (independent Tortoise safety; donor Wrath behavior unsafe, not ported).
- Master Strike action/trigger (independent Tortoise implementation; no donor counterpart).
- Weakened Soul 6788 guard (ported intent from mod-playerbots PriestActions.cpp).
- Hostile Chastise CC wiring (ported intent from Shyalya PriestStrategy CC).
Reason: P1 shared correctness + first class packets per CLASS_BEHAVIOR_PORT_PLAN.md.
Local validation: validate_presets.py 242 links 0 failures; git diff --check; verify_tortoise_surface.sh OK; verify_penqle_host_contract.sh OK. No docker build (user-owned review).

## Class port Batches 5-7 (2026-09-08, uncommitted)
Feature: shared cancel-channel registration + Mage Icicles/evocation checks + Hunter KC/Carve/Lacerate + Rogue generator/Surprise/Noxious/boost fix
Source repository:
- `playerbots-references/mod-playerbots` @ b949b50 (donor intent; WotLK names rejected)
- `playerbots-references/shyalya-tortoise-wow` @ 83a61bc (Tortoise runtime parity)
- `tortoise-wow` @ 9f778a73 (spell_hunter.cpp, spell_rogue.cpp, spell_mage.cpp, Unit.cpp aura states, SpellMgr exclusivity, spell_template)
Source files:
- `ai/playerbot/strategy/actions/ActionContext.h` (cancel-channel creator)
- `ai/playerbot/strategy/mage/MageActions.h`, `MageTriggers.h/.cpp`, `MageAiObjectContext.cpp`, `FrostMageStrategy.cpp`, `MageStrategy.cpp`
- `ai/playerbot/strategy/hunter/HunterActions.h`, `HunterTriggers.h`, `HunterAiObjectContext.cpp`, `BeastMasteryHunterStrategy.cpp`, `SurvivalHunterStrategy.cpp`
- `ai/playerbot/strategy/rogue/RogueActions.h`, `RogueTriggers.h/.cpp`, `RogueAiObjectContext.cpp`, `CombatRogueStrategy.cpp`, `AssassinationRogueStrategy.cpp`
Copied / ported / independently reimplemented:
- CancelChannelAction registration (re-enables existing #92 RoF fix + druid/hunter/mage trees; class already vendored, zero creators found).
- Icicles/evocation channel checks (adapted from #92 RainOfFireChannelCheckTrigger pattern).
- Kill Command crit window via core CanCastSpell (casterAuraState 6), not DBC guessing; donor buff-model rejected.
- Carve below Multi-Shot (shared 10s category); Lacerate manual-only (Serpent churn avoidance).
- Surprise Attack reactive gate (mirrors local RiposteCastTrigger); Noxious Assault Combo-gated strike.
- CombatBoost adrenaline/blade flurry moved to combat triggers (was non-combat dead wiring).
Reason: Mage/Hunter/Rogue packets per CLASS_BEHAVIOR_PORT_PLAN.md.
Local validation: git diff --check; validate_presets.py 242 links 0 failures; verify_tortoise_surface.sh OK; verify_penqle_host_contract.sh OK. No docker build (user-owned review).

## Class port Batches 8-11 (2026-09-08, uncommitted)
Feature: Warlock DH/PO + Paladin HS/Bulwark/Exorcism + Druid Berserk/Swiftmend + Shaman 5 talents/Bloodlust
Source repository:
- `playerbots-references/mod-playerbots` @ b949b50 (donor intent; WotLK names rejected)
- `playerbots-references/shyalya-tortoise-wow` @ 83a61bc (Tortoise runtime parity)
- `tortoise-wow` @ 9f778a73 (spell_warlock.cpp, spell_paladin.cpp, spell_druid.cpp, spell_shaman.cpp, spell_template)
Source files: warlock/, paladin/, druid/, shaman/ strategy dirs (actions/triggers/contexts/spec strategies listed in PROGRESS.md Batches 8-11).
Copied / ported / independently reimplemented:
- Dark Harvest 2-DoT gate + inverted cancel (independent; CD refund mechanic).
- Power Overwhelming explicit pet targeting (independent; core fallback analysis).
- Holy Strike/Bulwark actions (independent; verified template rows); Exorcism creature-type gate (vanilla-correct).
- Druid Berserk boost + Swiftmend HoT-gated pair (independent); NEW-stack rejection, Savage Bite rejection, Tree deferral (evidence-based).
- Shaman EQ/LS/Spirit Link/AS-pair/Bloodlust wiring (independent); totem churn claims rechecked and rebutted with source.
Reason: Warlock/Paladin/Druid/Shaman packets per CLASS_BEHAVIOR_PORT_PLAN.md.
Local validation: git diff --check; validate_presets.py 242 links 0 failures; verify_tortoise_surface.sh OK; verify_penqle_host_contract.sh OK. No docker build (user-owned review).

## Class port Batch 12 (2026-09-08, uncommitted)
Feature: five missing talent presets + generator + stance creator registration
Source repository:
- `tortoise-docker-penqle/data/dbc/Talent.dbc` + `TalentTab.dbc` (tree topology)
- `tortoise-wow/sql/base/tw_world_spell_template.sql` (talent spell names)
- `tortoise-wow` core (LearnTalent zero-based DependsOnRank semantics)
Source files: `tools/talents/dump_trees.py`, `tools/talents/build_missing_presets.py`, `ai/playerbot/aiplayerbot.conf.dist.in` (+5 specs), `ai/playerbot/strategy/warrior/WarriorStrategy.cpp`.
Copied / ported / independently reimplemented:
- Preset generator (independent; explicit acquisition orders, 297/297 links validate). Placements decoded from DBC, not skill-tab inference.
- Stance creator registration (independent correction of census misread; nodes were live, creators commented).
Reason: TALENT_BUILDS completion (27/27 specs) + Warrior tank/interrupt correctness.
Local validation: validate_presets.py 297 links 0 failures; git diff --check; verify_tortoise_surface.sh OK; verify_penqle_host_contract.sh OK. No docker build (user-owned review).

## Class port Batches 12-13 (2026-09-08, uncommitted)
Feature: 5 missing presets + stance creators + 90-row family coverage
Source repository: Talent/TalentTab DBC + spell_template (names/topology).
Source files: `tools/talents/dump_trees.py`, `tools/talents/build_missing_presets.py`, `aiplayerbot.conf.dist.in` (+55 links), `strategy/warrior/WarriorStrategy.cpp`, `docs/class-port/*`.
Copied / ported / independently reimplemented: generator + builds (independent); stance creators (correction of census misread, nodes pre-existing live).
Reason: TALENT_BUILDS 27/27 + coverage completion.
Local validation: 297/297 links 0 failures; TSV column audit (90x16); diff --check; surface + host OK. No docker build (user-owned review).

## Class port Batch 14 (2026-09-08, uncommitted)
Feature: deferred Rogue four (Envenom/SoD/MfD/Smoke) + Ascendance
Source repository: `tortoise-wow` spell_template + Talent.dbc + spell_rogue.cpp.
Source files: rogue/ actions/triggers/context/Assassination/Subtlety strategies; priest/ actions/triggers/context/Holy boost.
Copied / ported / independently reimplemented: finisher/support slot decisions (independent from decoded mechanics); repaired two edit-placement breaks with diff verification.
Reason: close deferred Tortoise-talent gaps per census.
Local validation: git diff --check; validate_presets.py 297/0; verify_tortoise_surface.sh OK. No docker build (user-owned review).

## Class port Batch 15 (2026-09-08, uncommitted)
Feature: wiring audit gate + Ret/ready-check/master-target fixes + Elemental Mastery + naaru removal
Source repository: `tortoise-wow` (Engine::Init dual-path evidence); `playerbots-references/mod-playerbots` (bare-AoE donor semantics, deliberately not ported).
Source files: `tools/verify_action_trigger_wiring.py`; RetributionPaladinStrategy.cpp; WorldPacketActionContext.h; GenericTriggers.h/.cpp + TriggerContext.h; RacialsStrategy.cpp; shaman Elemental files.
Copied / ported / independently reimplemented: audit tool (independent); typo/registration fixes (independent); MasterTargetActiveTrigger (independent, from MasterTargetValue semantics).
Reason: reachability gate for all 28 profiles (a queued name without creator is a silent no-op).
Local validation: wiring gate exit 0 (live-missing=0); diff --check; presets 297/0; surface + host OK. No docker build (user-owned review).

## Class port Batch 16 (2026-09-08, uncommitted)
Feature: Tree of Life wiring + Conflagrate verification
Source repository: `tortoise-wow` spell_druid.cpp:570-579 + spell_warlock.cpp:475-510 + 45705 template row.
Source files: `strategy/druid/RestorationDruidStrategy.cpp` (tree maintain); Conflagrate paths unchanged (verified, not modified).
Copied / ported / independently reimplemented: Tree maintain (independent; restriction audit first).
Reason: close Tree design gap; verify Destruction policy.
Local validation: wiring gate 0; diff --check; presets 297/0; surface + host OK. No docker build (user-owned review).

## Class port Batch 17 (2026-09-08, uncommitted)
Feature: Wolf aspect manual action + coverage integrity
Source files: hunter/ actions+context; docs/class-port/SPELL_COVERAGE.tsv.
Reason: last unresolved family row; oscillation analysis withheld automation.
Local validation: wiring gate 0; diff --check; presets 297/0; surface + host OK. No docker build (user-owned review).

## Class port Batch 18 (2026-09-08, uncommitted)
Feature: Hunter pet attack parity + Wolf manual action
Source files: `strategy/hunter/HunterStrategy.cpp` (pet attack mirror of WarlockPetStrategy); HunterActions.h + HunterAiObjectContext.cpp (Wolf).
Reason: close Hunter pet-control gap with owned #92 machinery; Wolf without oscillation risk.
Local validation: wiring gate 0; diff --check; presets 297/0; surface + host OK. No docker build (user-owned review).

## Class port Batch 19 (2026-09-08, uncommitted)
Feature: Daybreak fallback consumers + documented non-gates
Source files: `strategy/paladin/HolyPaladinStrategy.cpp` (FoL/HS fallbacks).
Reason: consume the Daybreak window when HL is unsuitable; Bloodlust-gate and Kick-reserve withheld for lack of evidence (documented).
Local validation: diff --check; presets 297/0; wiring 0; surface + host OK. No docker build (user-owned review).

## Class port Batches 19-20 (2026-09-08, uncommitted)
Feature: Daybreak fallbacks + full-diff review repairs + namespace-aware wiring gate
Source files: HolyPaladinStrategy.cpp; reviewer-found repairs across rogue/warrior/shaman/paladin/packet/generic/druid/warlock files; tools/verify_action_trigger_wiring.py.
Reason: consume Daybreak window robustly; eliminate silent no-ops module-wide.
Local validation: wiring gate 0/0; diff --check; presets 297/0; surface + host OK. No docker build (user-owned review).

## Class port Batch 21 (2026-09-08, uncommitted)
Feature: review-pass repairs + 7 gate-found fixes + Viper manual action
Source files: rogue/warrior/shaman/paladin/packet/generic/druid/warlock/hunter strategy files; tools/verify_action_trigger_wiring.py (namespace buckets + node check).
Reason: eliminate silent no-ops; keep manual paths for oscillation-constrained aspects.
Local validation: wiring gate 0/0; diff --check; presets 297/0; surface + host OK. No docker build (user-owned review).

## Class port Batch 22 (2026-09-08, uncommitted)
Feature: full working-diff self-review + repairs
Reason: edit-tool range edits silently dropped creator lines; systematic review is the backstop without compilation.
Local validation: raw git diff per file vs HEAD; wiring gate 0/0; diff --check; presets 297/0; surface + host OK. No docker build (user-owned review).

## Class port Batch 23 (2026-09-08, uncommitted)
Feature: preset/dispatch integration audit (read-only)
Reason: prove new presets resolve end-to-end without code changes.
Local validation: code-read evidence (config loader, factory roll, AiFactory tabs, update maps); full battery green. No docker build (user-owned review).
- Engine Bounded Failure Backoff + Transition Invalidation (issue #84):
  - Ported failure-cache intent from `playerbots-references/shyalya-tortoise-wow` @ `83a61bc3edb66983256f64ffa89a8c8b61146571` (`modules/mod-playerbots/src/playerbot/strategy/Engine.cpp` failure key/backoff/TTL/eviction, background-only gating, position-change clearing). Did NOT port its core-coupled transition generations (`GetMapWorkGeneration`/`GetTransitionGeneration` absent from Penqle core `9f778a73`); transitions tracked module-side and drained with local queue drain (no `Reset()`/`Init()` interplay, strategies/triggers untouched).
  - Transition signal is a generation counter on `PlayerbotAI` bumped by `HandleTeleportAck` (the single choke point `BotManager::UpdateBots` drives; ack ticks skip AI updates). Engines consume it at tick start before triggers, so even short same-map hops drain. `TransitionTracker` (map id + 3D 100yd jump detector + away/arrival edges + `NoteAway` on mid-walk breaks) backstops transfers the ack path never sees; walking zone lines never trips.
  - New header-only policy unit `ai/playerbot/strategy/ActionFailureBackoff.h` (pure std, no core types) wired into `Engine::{FailureKey,AllowBackgroundRetry,IsFailureBackedOff,RecordFailure,ClearActionFailures,RefreshFailureContext,DrainQueue}`; backoff gate sits before prerequisites/possibility so backed-off actions (including `!isPossible`) cost no work; explicit `ExecuteAction` path clears backoff (acts without delay by design). Gating additionally exempts owned bots (`IsOwnedBot`) per the issue's unowned-only scope.
  - Config `AiPlayerbot.FailedActionRetry{Base,Max,CacheTtl,CacheMaxEntries}` with Shyalya defaults (250/2000/30000/64); zero base/max disables.
  - Regression: `tools/test_engine_failure_backoff.cpp` (g++-compiled, 55 checks: growth/cap/saturation, success-clear, TTL prune, stalest-first eviction, disable, key separation, transition state machine incl. skipped-tick short teleport + 3D jump + NoteAway) + `tools/test_engine_walk_gating.py` (9 checks: gate ordering, drain paths, explicit-command clearing, ack bump, generation consumption, away marking).

## Economy: Native Auction Read Model and Personal Settlement (Issue #87) — 2026-09-09

Feature: Native auction read model with unit-price accuracy, bounded multi-faction cache, market-informed pricing, personal auction cancellation, and mail settlement safety.

Source repository:
- `playerbots-references/shyalya-tortoise-wow` @ `83a61bc3edb66983256f64ffa89a8c8b61146571`: Reference for auction price mirroring intent and `ItemUsageValue` market appraisal queries (`GetAHMedianBuyoutPricePerItem`, `GetAHListingLowestBuyoutPricePerItem`).
- `tortoise-wow` @ `bot-helpers` (`9f778a73`): Canonical core auction structures (`sAuctionHouseStore`, `AuctionHouseObject::GetAuctions()`, `sAuctionMgr.GetAItem()`, `WorldSession::HandleAuctionRemoveItem()`) and mail settlement APIs (`WorldSession::HandleMailTakeMoney`, `WorldSession::HandleMailTakeItem`).

Source files:
- `ai/playerbot/RandomBotFacade.h`
- `runtime/PlayerbotRuntimeFacade.cpp`
- `runtime/RandomBotService.cpp`
- `runtime/AhMarketService.cpp`
- `ai/playerbot/PlayerbotAIConfig.h`
- `ai/playerbot/PlayerbotAIConfig.cpp`
- `ai/playerbot/aiplayerbot.conf.dist.in`
- `ai/playerbot/strategy/values/ItemUsageValue.h`
- `ai/playerbot/strategy/values/ItemUsageValue.cpp`
- `ai/playerbot/strategy/actions/AhAction.h`
- `ai/playerbot/strategy/actions/AhAction.cpp`
- `ai/playerbot/strategy/actions/ChatActionContext.h`
- `ai/playerbot/strategy/triggers/ChatTriggerContext.h`
- `ai/playerbot/strategy/generic/ChatCommandHandlerStrategy.cpp`
- `ai/playerbot/strategy/actions/CheckMailAction.cpp`
- `ai/playerbot/strategy/actions/MailAction.cpp`

Copied / ported / independently reimplemented:
- Read model snapshotting & caching:
  - Reimplemented `RandomBotFacade::LoadAuctionPrices()` to iterate core `sAuctionHouseStore`, deduplicate visited `AuctionHouseObject` instances across faction IDs, resolve item counts via `sAuctionMgr.GetAItem()`, and populate an in-memory mirror bounded to 64 lowest unit-price entries per item template.
  - Added periodic world-thread refresh (`RefreshAuctionPrices`) throttled by `AiPlayerbot.AuctionPriceRefreshInterval` (default 60s, configurable 5-3600s), wired into `RandomBotService::Update`.
  - Added faction-scoped queries (`GetAhPrices(itemId, houseFaction)` and `GetAhPrices(itemId, Player* bot)`) respecting two-sided auction house rules (`AiPlayerbot.TwoSidedAuctionHouses`) or faction boundaries (Alliance sees Alliance+Neutral, Horde sees Horde+Neutral).
- Unit-price precision and appraisal:
  - `GetAHMedianBuyoutPricePerItem`, `GetAHListingLowestBuyoutPricePerItem`, and `DesiredPricePerItem` calculate unit buyout as `(float)buyout / (float)count`. If unit price is in `(0, 1)`, a positive sentinel of 1 copper is preserved so low-value stack listings are not rounded to 0 and mistaken for unlisted items.
  - Market posting in `AhMarketService` and `AhAction` queries `DesiredPricePerItem(bot, proto, count, undercutPercent)` before falling back to vendor price multiplier.
- Lifecycle actions & safety:
  - Implemented `AhCancelAction` (`ah cancel <id|item-name|all>`), dispatching canonical `WorldSession::HandleAuctionRemoveItem` packets with proper deposit forfeiture / item return semantics. Registered in trigger/action contexts and chat command handler.
  - Guarded `AhBidAction` against same-account bidding (`auction->ownerAccount == bot->GetSession()->GetAccountId()`).
  - Fixed mail settlement in `CheckMailAction.cpp`: excluded auction mails (`MAIL_STATIONERY_AUCTION` and non-normal message types) from unsolicited deletion, preventing loss of pending auction proceeds or returned items.
  - Fixed mail claiming in `MailAction.cpp` (`TakeMailProcessor`): money and items are claimed sequentially, and `RemoveMail` is called only after both money and items have been completely collected, eliminating gold/item destruction.

Reason: Fix Issue #87. Previously, `LoadAuctionPrices` was a clear-only stub causing all appraisal sites to return 0 and fall back to vendor sell prices. Bots could not cancel auctions, same-account bidding was unguarded, and mail handling contained bugs that could delete auction mails or destroy mail contents.

Local validation:
- Standalone C++ verification harness `tools/test_auction_read_model.cpp` (59 checks, 100% pass): tested empty mirror fallback, single-lot and multi-stack unit pricing, sub-copper preservation, 64-entry bounding, Alliance/Horde/Neutral scoping, two-sided config override, same-account bid rejection, cancel lifecycle, outbid refund conservation, buyout transfer conservation, deposit rules, hardcore dead-bot auction handling, and zero gold/item leakage.
- `python3 tools/verify_action_trigger_wiring.py` (exit code 0, live-missing=0).
- `bash tools/verify_tortoise_surface.sh` (exit code 0).
- `bash tools/verify_penqle_host_contract.sh --core ../tortoise-wow` (exit code 0).
- Docker native static builder `./dev/build-playerbots` passed (`[100%] Built target mangosd`).

## Issue #88: Economy Synthetic AH Supply and Buyer Engine with Work Budgets — 2026-09-09

Feature: Time/operation-budgeted synthetic auction house generation and buyer engine, isolated synthetic inventory, item overrides and bans (`ahbot_items`), spend caps, same-account/outbid-self exclusions, and live telemetry/commands.

Source references:
- `playerbots-references/shyalya-tortoise-wow` @ `83a61bc3edb66983256f64ffa89a8c8b61146571`: Reference for ahbot generation sources, loot templates, and `ahbot_items` schema concept.
- `tortoise-wow` @ `bot-helpers` (`9f778a73`): Core auction house lifecycle, `sAuctionHouseStore`, `AuctionHouseObject`, `sAuctionMgr.GenerateAuctionID()`, and headless session auction packet handling.

Source files:
- `data/sql/char/20260906090000_char.sql`
- `ai/playerbot/PlayerbotAIConfig.h`
- `ai/playerbot/PlayerbotAIConfig.cpp`
- `ai/playerbot/aiplayerbot.conf.dist.in`
- `runtime/AhMarketService.h`
- `runtime/AhMarketService.cpp`
- `commands/BotCommands.cpp`
- `tools/test_synthetic_ah.cpp`

Copied / ported / independently reimplemented:
- Work budgets and phased state machine:
  - Implemented modular execution cycle (`Idle` -> `Gather` -> `Overrides` -> `Post` -> `Buy` -> `Expire` -> `Idle`) running within strict per-slice limits: `ahMarketBudgetUs` (default 2000 us) and `ahMarketMaxOperations` (default 32 ops).
  - Telemetry tracking slice durations, total listed/bought/expired counts, and budget overruns.
- Synthetic supply generation and pricing:
  - Generation sources cover creature loot templates (ranks 0..4), gathering professions (skinning, fishing, disenchanting, mining/herbs/chests), vendor inventories, and crafting recipes (`SPELL_EFFECT_CREATE_ITEM`).
  - Strict quality caps (`ahMarketMaxQuality`, default 4 / Epic), level caps (`ahMarketMaxLevel`, default 60), dynamic realm level clamping, and bound-item exclusion.
  - Value-based pricing bands with configurable variance (`ahMarketVariance`) and bid margins (`ahMarketBidMin`, `ahMarketBidMax`).
- Synthetic inventory isolation:
  - Synthetic items and auctions are tagged with `SYNTHETIC_OWNER_GUID = 0` and `SYNTHETIC_OWNER_ACCOUNT = 0`.
  - Synthetic items are tracked in `m_syntheticAuctions` and `m_syntheticItemGuids`.
  - Assertable property `AssertSyntheticIsolation(player, itemGuidLow)`: synthetic item LowGuids are never placed in real player or bot bags.
  - Unsold synthetic auctions expire cleanly by deleting from `item_instance` and memory without sending mail.
  - Purchases of synthetic items transfer items to buyers while money is sunk without paying non-existent sellers.
- Buyer engine and exclusions:
  - Evaluates auctions against fair market value and willingness threshold (`ahMarketBuyValue`, default 80%).
  - Enforces per-bot spend caps (`ahMarketMaxSpendPerBot`).
  - Excludes bidding on own auctions (`owner == buyer`), same-account listings (`ownerAccount == buyerAccount`), and outbidding self (`bidder == buyer`).
  - Buyer bots teleport to matching faction auctioneers and place bids/buyouts through canonical `HandleAuctionPlaceBid`.
- Overrides, blacklisting and live commands:
  - `ahbot_items` DB table (`20260906090000_char.sql`) provides per-item price overrides and blacklisting (`add_chance = 0` or `value = 0`).
  - In-game `.bot ah` / `.ahbot` commands: `status`, `reload`, `rebuild [all]`, `item <id> [value [chance [min [max]]]]`, and `item <id> reset` with administrator security checks.

Reason: Complete Issue #88. Complements the read model and personal settlement of PR #113 (Issue #87) by providing synthetic supply and demand for low-population realms without lag spikes, gold inflation, or item duplication.

Local validation:
- Standalone C++ verification harness `tools/test_synthetic_ah.cpp` (49 checks, 100% pass): verified phase transitions, work budget slicing, generation filtering and caps, pricing bands and variance, synthetic inventory isolation assertions, buyer engine policies and exclusions, override/ban behavior, safe expiration, and currency/item conservation.
- Standalone read model test `tools/test_auction_read_model.cpp` (59 checks, 100% pass).
- `python3 tools/verify_action_trigger_wiring.py` (exit code 0, live-missing=0).
- `bash tools/verify_tortoise_surface.sh` (exit code 0).
- `bash tools/verify_penqle_host_contract.sh --core ../tortoise-wow` (exit code 0).
- Docker native static builder `./dev/build-playerbots` passed (`[100%] Built target mangosd`).

## 2026-09-09 — Travel route selection policies and navigation data import (Issue #86)

Target commit / PR: `feat/issue-86-travel-routes`

Source donor:
- `playerbots-references/shyalya-tortoise-wow/modules/mod-playerbots/src/playerbot/TravelRoutePolicy.h`
- `playerbots-references/shyalya-tortoise-wow/tests/architecture/TravelRoutePolicyTest.cpp`
- `playerbots-references/shyalya-tortoise-wow/modules/mod-playerbots/src/playerbot/TravelNode.cpp`
- `playerbots-references/shyalya-tortoise-wow/modules/mod-playerbots/src/playerbot/TravelMgr.cpp`
- `playerbots-references/shyalya-tortoise-wow/modules/mod-playerbots/sql/world/classic/ai_playerbot_travel_nodes.sql`
- `playerbots-references/shyalya-tortoise-wow/modules/mod-playerbots/sql/world/classic/ai_playerbot_named_location.sql`

Files touched:
- `ai/playerbot/TravelRoutePolicy.h`
- `ai/playerbot/TravelNode.cpp`
- `ai/playerbot/TravelMgr.h`
- `ai/playerbot/TravelMgr.cpp`
- `tools/import_travel_nodes.py`
- `tools/test_travel_route_policy.cpp`
- `docs/PROVENANCE.md`

Copied / ported / independently reimplemented:
- Route weighting and travel policies (`TravelRoutePolicy.h`):
  - `GetTaxiRouteCost`: applies `PLAYERBOT_TAXI_ROUTE_DIVISOR` (450 * 8 = 3600) in `generateTaxiPaths` so discovered and affordable flight points are strongly preferred over continent-scale walking or swimming.
  - `GetWalkTravelTime`: models walking vs swimming with a 120-yard safe swim grace for short river crossings, combined with a 4.0x multiplier on sustained swimming so bots prioritize roads, bridges, and ferries over lengthy water crossings.
  - `GetStableRouteCostMultiplier`: deterministic pseudo-random 1.0..1.25 cost multiplier (up to 25% variation) keyed to party leader GUID (or bot GUID low) and spatial quantization. Ensures party members stay together while preventing different parties from marching single-file in identical lines.
  - `GetStableTravelSelectionSeed` / `MixTravelRouteSeed`: deterministic 32-bit avalanche hashing for party destination and point shuffling in `TravelMgr::GetPartitions`.
- Graph loading and startup decoupling:
  - Disentangled the historical conflation of online navmesh generation with DB cache loading in `TravelMgr::LoadQuestTravelTable()`. Persisted node, link, and path caches are loaded unconditionally from MariaDB (`ai_playerbot_travelnode`, `ai_playerbot_travelnode_link`, `ai_playerbot_travelnode_path`). If tables are empty, the system logs and degrades gracefully to direct movement and quest destinations.
  - Restored the post-load graph linking pass in `TravelNodeMap::generateAll()` (`calcMapOffset()`, `LoadMapTransfers()` for instance/portal triggers from `AreaTrigger.dbc`, `generateTaxiPaths()` for flight routes from `TaxiPath.dbc` / `TaxiNodes.dbc`, and reachability coverage warming).
- Navigation and fish location offline tooling (`tools/import_travel_nodes.py`):
  - Ingests and validates travel graph dumps (1,839 nodes, 6,200 links, 414,126 path points) and named fishing locations (54,038 spots).
  - Validates coordinate bounds and map IDs across all Classic/Tortoise WoW maps.
  - Produces clean, sanitized, high-performance replacement SQL migrations compatible with the canonical world schema (`20260824090000_world.sql`) and supports direct application via `--apply`.
- Route unreachable diagnostics:
  - Added explicit diagnostic logs (`sLog.outDetail`) when destination nodes are unreachable due to disconnected components, exhausted open lists, or missing start/end node associations, eliminating silent failure.

Reason: Groundwork for Issue #86 (keep #86 open for in-game client verification of live travel behavior). Enables autonomous bots to navigate intelligently via flight paths and roads while avoiding hazardous swimming and unnatural single-file party marching, backed by a supported offline import tool and graceful fallback.

Local validation:
- Standalone test suite `tools/test_travel_route_policy.cpp` (6 checks, 100% pass).
- Standalone regression test suites `tools/test_auction_read_model.cpp` (59 checks) and `tools/test_synthetic_ah.cpp` (53 checks).
- Offline import tool verification `python3 tools/import_travel_nodes.py --validate-only` (0 errors, 0 warnings across 1,839 nodes, 6,200 links, 414,126 path points, and 54,038 fish locations).
- `python3 tools/verify_action_trigger_wiring.py` (exit code 0, live-missing=0).
- `bash tools/verify_tortoise_surface.sh` (exit code 0).
- `bash tools/verify_penqle_host_contract.sh --core ../tortoise-wow` (exit code 0).
- Docker native static builder `./dev/build-playerbots` passed (`[100%] Built target mangosd`).

## Issue #85: Earned Progression Loop (leveling, trainers, recruitment) — 2026-09-09

Feature: ding-time synthetic gear removed; initial gear seeding restricted to fresh pool bots via dual heuristic (process-local `seeded` stamp + persisted `GetTotalPlayedTime`); trainer travel gated on cheapest-affordable-spell instead of full-batch price; real-player master adoption purges travel/grind state and halts movement.

Source references:
- `playerbots-references/mod-playerbots`: `XpGainAction` never mints gear — bots keep earned equipment (behavioral reference for the ding-time removal).
- `playerbots-references/shyalya-tortoise-wow` @ `83a61bc3edb66983256f64ffa89a8c8b61146571`: `UpdateGearSpells` hooked into `RandomPlayerbotMgr`/`XpGainAction` as a headless-pool shortcut (kept only for initial seeding, not progression).
- `tortoise-wow` core `Player::GiveLevel`: unconditionally sends `SMSG_LEVELUP_INFO`, which already drives `auto talents` through the packet handlers — no new talent plumbing needed.

Source files:
- `ai/playerbot/strategy/actions/AutoLearnSpellAction.cpp`
- `ai/playerbot/strategy/actions/XpGainAction.cpp`
- `ai/playerbot/strategy/values/TravelValues.cpp`
- `ai/playerbot/PlayerbotAI.cpp`
- `runtime/BotManager.cpp`
- `runtime/PlayerbotAIAdapter.cpp`
- `runtime/RandomBotService.cpp`
- `runtime/GearSeedingGuard.h`
- `tools/test_progression_loop.cpp`

Copied / ported / independently reimplemented:
- Dinging expires the travel target (trainer re-evaluation) via `AutoLearnSpellAction::LearnSpells` and `XpGainAction` instead of minting gear; `auto talents` arrives via the existing levelup packet path.
- `NeedsInitialGearSeeding(playedTime, seededMark)`: seed only when both are zero. Veterans survive restarts via played time; repeats are suppressed via the stamp. Stamp is written after the attempt, never before.
- Trainer travel requires `free money for <budget> >= min trainable spell cost`; per-spell affordability at the trainer itself is unchanged (`TrainerAction` still skips overpriced spells).
- Master adoption (`PlayerbotAI.cpp` group adoption and `PlayerbotAIAdapter.cpp` rebind) runs `Reset(true)` + `StopMoving()` to purge orphan travel/grind goals and tether immediately.

Reason: Complete Issue #85 reqs 1-4: earned gear/trainer progression, restart persistence, immediate owner control on recruitment.

Local validation:
- Standalone harness `tools/test_progression_loop.cpp` (all checks pass): full seeding truth table and partial-purse trainer gating.
- `python3 tools/verify_action_trigger_wiring.py` (live-missing=0).
- `bash tools/verify_tortoise_surface.sh` (exit code 0).
- `bash tools/verify_penqle_host_contract.sh --core ../tortoise-wow` (exit code 0).
- Docker native static builder `./dev/build-playerbots` passed (`[100%] Built target mangosd`).

## Bot text seed + cast fail reason — 2026-09-13

Feature: Full `ai_playerbot_texts` seed so bots speak sentences instead of raw
keys (`cast_spell_command_error`, `quest_accepted`, travel chatter); cast
failure now keeps the engine reason via a native `cast_spell_command_error_reason` row.

Source repository: `mod-playerbots/mod-playerbots`

Source commit: `b6696bdbd3740e575598d167d69f39f68cc0b907` (local
`playerbots-references/mod-playerbots` checkout; remote HEAD identical)

Source files:
- `data/sql/playerbots/base/ai_playerbot_texts.sql` (1,739 rows, ids 1-1739)

Copied / ported / independently reimplemented:
- Data-only port to `data/sql/world/20260913090000_world.sql`: row tuples copied
  verbatim, donor `DROP TABLE`/`CREATE TABLE` header deliberately omitted so the
  schema stays owned by `20260824090000_world.sql`; `INSERT IGNORE` keeps local
  customizations and makes re-application idempotent. Plus one native row 1740
  (`Cannot cast %spell (%fail_reason)`).
- Code: `ai/playerbot/strategy/actions/CastCustomSpellAction.cpp` can-cast
  failure path now uses the shim reason string directly (no bogus text-table
  lookup of plain English) and picks the `_reason` key when a reason exists.
  Previously `%fail_reason` was computed then discarded by a reason-less template.

Reason: Empty table made `GetBotText(name, placeholders)` fall back to the raw
key (`PlayerbotTextMgr.cpp`), so every bot text leaked as code words; combined
with `RandomBotSayWithoutMaster = 1` they surfaced in public `/s`. Full seed
chosen over curated subset per operator call: noise control stays in
`aiplayerbot.conf` (`EnableBroadcasts` master switch, `BroadcastToWorld` /
`BroadcastToGeneralGlobalChance` throttles, per-event `BroadcastChance* = 0`).

Local validation:
- Row-count audit: 1,739 donor tuples in, 1,740 out; key spot-checks
  (`cast_spell_command_error`, `quest_accepted`,
  `broadcast_quest_accepted_generic` x39, `thunderfury_spam` x36); no
  `DROP`/`CREATE` in migration; zero non-BMP chars (utf8mb3-safe).
- `python3 tools/verify_all.sh` + `git diff --check` (see commit).
- Module rebuild pending (see commit); live check pending: texts-loaded count in
  server log, raw keys gone in game, reason visible on failed casts.

## Item weight scales + first-boot cache generation (Issue #182) — 2026-09-16

Feature: Ship the Vanilla spec weight scales with the module, and let a fresh
install generate its optional item caches once, so random bots are geared by
`PlayerbotFactory` and judge upgrades instead of only filling empty slots.

Source repository: `Shyalya/tortoise-wow` (classic port of `mod-playerbots`)

Source commit: `05ba90b2e00ef5ee861111ec5bde670929eedfa4` (local
`playerbots-references/shyalya-tortoise-wow` checkout, remote HEAD identical)

Source files:
- `modules/mod-playerbots/sql/world/classic/ai_playerbot_weightscales.sql`
  (headerless HeidiSQL dump: 32 spec rows, 220 stat-weight rows, md5
  `6e9a6dbfce75330bcd2f811ec710b15c`)

Copied / ported / independently reimplemented:
- Data-only port to `data/sql/world/20260916090001_world.sql`: the 28 Vanilla
  spec rows and their 180 stat-weight rows are copied verbatim. Death knight ids
  16-19 and their stat rows are omitted; every other id is kept, because
  `PlayerbotFactory.cpp:1901-2321` and `RandomItemMgr.cpp:2816` hardcode spec ids
  (2, 3, 4, 5, 6, 14, 20, 21, 22, 29, 31). Schema stays owned by
  `20260824090000_world.sql`. The shipped id set is deleted before it is inserted
  because `ai_playerbot_weightscale_data` has no unique key and a duplicate stat
  row would double-count a weight; the death knight rows of an operator who
  imported the full dump are left alone.
- `RandomItemMgr::BuildEquipCache()` / `BuildRandomItemCache()`: the "generate
  when the table is empty" branch was unreachable (it required a successful
  `COUNT(*)` that was non-zero while the row query returned nothing). It is now
  reachable and gated by the new `AiPlayerbot.GenerateItemCaches` (default `1`).
  `BuildItemInfoCache()` is deliberately not gated: it loads the weight scales and
  builds the in-memory stat-weight map on every start, and has no database load
  path of its own.
- The equipment generator was restructured from one full walk of the item id
  space (~2M ids on Tortoise) per (class, spec, level, slot, quality) key — about
  246k keys — to a single pass over the loaded prototypes, and writes its rows as
  multi-row INSERT batches inside one transaction instead of one statement per
  cached item. `GetUpgrade(Player*, std::string, ...)` now scans every registered
  spec of the player's class instead of ids 1-4.

Reason: With `ai_playerbot_weightscale_data` empty, `BuildItemInfoCache()` returns
before any stat weight is computed, so `GetPlayerSpecId()` returns 0 for every
bot, `PlayerbotFactory::InitEquipment` skips provisioning ("InitEquipment skipped
(specId=0)") and every upgrade comparison scores zero. The generator also stalled
the world thread for ~50 minutes when it was made to run, which is why it had been
left unreachable.

Local validation (local `tortoise-docker-penqle` stack, 500-bot pool, 2026-09-16):
- Migration applied by the core Auto-Updater on the next boot
  (`Attempting to execute update 20260916090001_world for module TortoiseBots,
  hash 04FA7ACB990CCD24DE8B249E4754355038C75378`), followed by
  `Loaded 28 weightscale class specs` / `Loaded 180 weightscale stat weights`.
  Re-applying the same file into a scratch schema left 28 / 180 rows with no
  duplicate `(id, field)` pair.
- First start with empty character caches generated them: item info cache
  2.9s, equipment cache 10.6s for 2,945,755 rows, random item cache 4.4s for
  93,945 rows. A temporary measurement with the equip writes disabled put the
  prototype walk itself at 0.85s, so the rest is the batched InnoDB write.
- Second start loaded the caches instead of rebuilding:
  `Equipment cache loaded from 2945755 records` in 0.37s, random item cache in
  0.03s, and `World server is up and running! Loading time: 0 minutes 25
  seconds`.
- `ai_playerbot_item_info_cache` held 14,859 rows after the fix (0 while the
  weight scales were empty), and the cache carries real candidates per slot:
  level-15 hunter/beast-mastery had 55 green + 4 blue chests and 135 green + 46
  blue main-hand items.
- `python3 tools/verify_all.sh` and `git diff --check` pass; the weight-scale
  seed is now asserted by `tools/verify_tortoise_surface.sh`.
- Not yet observed: a fresh bot arriving dressed or swapping a looted upgrade.
  That is the plan's pool wipe plus soak (`#182` §5.2/§5.4), which needs the
  operator's go before deleting characters.

## Issue #192: On-demand companion hiring (`<Mercenary Hire>` & `.bot hire`) — 2026-09-18

Feature: inn-recruiter gossip wizard (class -> race -> gender -> spec/role -> confirm) plus `.bot hire <class> [role] [race] [gender]` fast path; RNDBOT-pool candidate reuse with `CharacterCreation::CreateCharacter` fallback; level sync, role-matching premade talents, `ProvisionSpellsAndGear`, tank strategy kit, native invite+accept; 5-minute master-disconnect grace with guard stance and party rejoin greeting; group-remove/disband dismissal (instant logout, or hearth-to-pool when the living world is under target).

Source repository: Tortoisebots native implementation (no donor copy). Behavioral references only:
- `playerbots-references/mod-playerbots` `HireAction.cpp` (trade-discount-gated ownership transfer — NOT ported; replaced by gold-fee + Headless login + durable ownership).
- `playerbots-references/shyalya-tortoise-wow` gossip/group patterns (reference for `CreatureScript` sender/action wizard shape and native invite/accept flow).
- `tortoise-wow` core `Player::GiveLevel`, `CharacterCreation::CreateCharacter`, `Group::RemoveMember/Disband`, `GossipMenu::AddMenuItem/SendGossipMenu`.

Source commit: n/a (native feature; donor SHAs not applicable).

Source files:
- `runtime/HireCost.{h,cpp}`
- `runtime/HireProvisionService.{h,cpp}`
- `runtime/HireLifecycle.{h,cpp}`
- `host/HireRecruiterScript.{h,cpp}`
- `host/HireRecruiterAdapter.{h,cpp}`
- `host/HireGroupAdapter.{h,cpp}`
- `commands/BotCommands.cpp` (HandleHire + hire release on remove/logout)
- `ai/playerbot/PlayerbotAIConfig.{h,cpp}` + `aiplayerbot.conf.dist.in` (Hire* knobs)
- `ai/playerbot/PlayerbotFactory.{h,cpp}` (ProvisionSpellsAndGear)
- `data/sql/world/20260918120000_world.sql` (46 recruiter templates, entries 95000-95045)

Copied / ported / independently reimplemented: independently reimplemented. No donor code copied; donor hire semantics (discount-gated `Randomize(false)` wipe) deliberately rejected in favor of fee + incremental provisioning.

Reason: Complete Issue #192: RPG-native mercenary recruitment on both low-spec (zero background bots) and living-world servers, with fair level-scaled economy and disconnect resilience, zero core modifications.

Local validation:
- Throwaway cost harness (defaults: 1.5g/2.5g/4g/7g party curve, 1g raid flat, linear level scale, free-hire zeros, index/level clamps) — PASSED.
- Gossip sender/action bit-packing round-trip check — PASSED.
- Recruiter SQL structural check (46 templates, script-bound, 80 cols, idempotent DELETEs) — PASSED.
- Docker native static builder `./dev/build-playerbots` — mangosd linked with hire symbols (`HireRecruiterAdapter`, `HireGroupAdapter`, `HireProvisionService::Hire`, `HireLifecycle::Claim`, `HireCost::ForHireIndex`).
- `bash tools/verify_all.sh ../tortoise-wow` — all checks passed.
- `git diff --check` — clean.
- Not yet observed: live in-game hire (recruiter gossip click-through, gold deduction, bot join). Needs a running server with the migration applied — flagged in the PR.

## Tank target stickiness (smart ranking + hold gate) — 2026-09-24

Feature: tank bots keep/finish the mob they hold instead of walking off a
nearly-dead mob to a loose add. `TankTargetValue` now buckets attackers
(loose first/nearest, then held-in-melee, then held-out-of-melee, lowest
personal threat as tie-break) and `TankAssistTrigger` only retargets while
the tank still holds its current target (`has aggro`), so the switch is
reversible. Explicit `.bot action attack` and RTI (skull) precedence and the
CC skips are unchanged.

## CC stage 1: exclusive mark ownership, dismissal, dungeon gate, AoE interlock — 2026-09-24

Feature: one mark = one owner (assigning `.bot action cc <mark>` resets every
other owned live party bot holding it to `none`, persisted); `.bot action cc
clear` dismisses ownership (targeted bot, or whole owned party); CC chooser
never CCs over an existing breakable/unbreakable CC (own-aura re-CC still
flows via `current cc target`); `HasCcTargetTrigger` fires only on the bot's
`rti cc target` inside non-raid dungeons (open world keeps free picks);
`AoeTrigger` refuses packs holding (or splashing) a breakable CC.
Generalizes the `fix/warlock-fear-gating` Fear-only guard to every CC spell
without changing its semantics.

Source repository: `mod-playerbots/mod-playerbots`

Source commit: `b6696bdbd3740e575598d167d69f39f68cc0b907` (local
`playerbots-references/mod-playerbots` checkout); behavior commits
`a63c6b67` ("smarter dps target and tank target") and `0a76fc1d`
("Better tank target selection (#996)").

Source files:
- `src/Ai/Base/Value/TankTargetValue.cpp:49-136` (`FindTankTargetSmartStrategy::IsBetter/GetIntervalLevel`, smart `TankTargetValue::Calculate`)
- `src/Ai/Base/Trigger/GenericTriggers.cpp:536-550` (`TankAssistTrigger::IsActive` has-aggro gate)
- `src/Ai/Base/Value/AttackerCountValues.cpp:13-32` (`HasAggroValue::Calculate` victim semantics)
- `src/Bot/PlayerbotAI.cpp:2071-2083` (`PlayerbotAI::HasAggro`)

Local additions beyond the donor (review follow-up): among held mobs the
current target wins the tie-break, and the assist gate never peels while the
held current target is at or below `AiPlayerbot.LowHealth`. Without them two
held mobs ping-ponged on threat and a low mob was still left for a loose add.

Copied / ported / independently reimplemented: ported, adapted to the 1.12
codebase. Ranking (`IsBetter`/`GetIntervalLevel`), the trigger gate, and the
live-victim + threat-manager victim helpers are behavior-identical; the
donor's multi-tank/explicit-MT pin (`IsExplicitMainTank`, `GetGroupTankNum`,
`TargetValueExclusionType::Tank`) is skipped as non-trivial single-tank
plumbing. The old lowest-threat `FindTargetForTankStrategy` is replaced
(donor keeps it commented-out; here it is removed since nothing else
references it).

Reason: the flat lowest-threat tournament plus the victim-based assist gate
made the tank abandon a nearly-dead mob for any lower-threat add and then
forbade switching back (one-way door) — see
`scratchpad/research/tank-target-switching.md` RC-1.

Local validation:
- `python3 tools/verify_okf.py` + `./tools/verify_all.sh` (see commit); `git diff --check` clean.
- No build (per task constraints); live in-game check pending: multi-mob pull, tank finishes its mob, still picks up healer adds, no stuck-on-door.

## Warlock fear gating (mark-only Fear, PvP-only Howl of Terror) — 2026-09-24

Feature: `fear on cc` fires only on the bot's `rti cc target` (and never replaces an existing breakable/unbreakable CC); `enemy ten yards -> howl of terror` moved from the base warlock `cc` strategy to `cc pvp`. Fixes warlocks fearing arbitrary (even dotted) mobs in PvE groups, which scattered pulls.

## Healer priest off-spec damage gate (`healer should attack`) — 2026-09-24

Feature: new generic `HealerShouldAttackTrigger` (`healer should attack`, `healer should wand`); `PriestOffdpsStrategy` damage (SW:P, Holy Fire, Smite, Starshards, Mind Blast) now fires only when solo, or when no party member is below `almostFullHealth` and mana is above a balance-scaled reserve, at `ACTION_DEFAULT` relevance. A healthy party with low mana gets a wand instead. Removed the ungated per-tick `smite`/`holy fire`/`very often -> starshards` nodes and the per-attacker SW:P node; Holy Nova is kept behind `melee medium aoe and healer should attack` (the donor's `medium aoe and healer should attack` -> Mind Sear). Note: `healer should attack` is now a registered trigger name, which the unregistered healer-dps strategies of druid/paladin/shaman also reference — registering those strategies later arms them.

Source repository: `mod-playerbots/mod-playerbots`

Source commit: `b6696bdbd3740e575598d167d69f39f68cc0b907`

Source files:
- `src/Ai/Base/Trigger/RtiTriggers.cpp` (`RtiCcTrigger::IsActive` — CC only on the RTI CC target)
- `src/Ai/Class/Warlock/WarlockTriggers.h` (`FearTrigger : RtiCcTrigger`)
- `src/Ai/Class/Warlock/Strategy/GenericWarlockStrategy.cpp` (`WarlockCcStrategy`: banish/fear on cc only, no Howl of Terror)

Copied / ported / independently reimplemented: ported semantics (no code copied); implemented as a `FearTrigger::IsActive` override on the existing `HasCcTargetTrigger`, plus an extra "target not already CC'd" guard via `PossibleAttackTargetsValue::HasBreakableCC/HasUnBreakableCC`.

Reason: live play report — warlock bots fear constantly in dungeon groups.

- `src/Ai/Base/Trigger/GenericTriggers.cpp` (`HealerShouldAttackTrigger::IsActive`)
- `src/Ai/Class/Priest/Strategy/GenericPriestStrategy.cpp` (`PriestHealerDpsStrategy::InitTriggers`)

Copied / ported / independently reimplemented: ported (logic re-expressed with this module's values; solo check uses group membership instead of `GetNearGroupMemberCount`; Tree of Life clause dropped; `highMana` = the existing 65% `high mana` line). Starshards (1.12 Night Elf racial) kept inside the gate.

Reason: live play report — Holy priest bots DPS like a damage spec and run out of mana instead of healing.

Local validation: cached `MODULE_TORTOISEBOTS=static` build; `tools/verify_all.sh`; `git diff --check`. In-game observation pending.

`playerbots-references/mod-playerbots` checkout); behavior commits `d9ee5198`
("fix(dungeons): stop the generic cc strategy from firing in 5-man dung…",
PR #2648) and `a63c6b67` / `0a76fc1d` (CC bucketing context).

Source files:
- `src/Ai/Base/Trigger/GenericTriggers.cpp:575-595` (`HasCcTargetTrigger::IsActive/IsCcTargetFree` — dungeon RTI gate)
- `src/Ai/Base/Trigger/RtiTriggers.cpp:25-36` (`RtiCcTrigger` — CC only on the RTI target)
- `src/Ai/Base/Value/CcTargetValue.cpp:23-85` (candidate filters incl. AoE-position skip)
- `src/Ai/Class/Druid/Action/DruidActions.cpp:158-170` (`CastStarfallAction::isUseful` CC guard — AoE idea source)
- `src/Bot/PlayerbotAI.cpp:1786` (`IsInNonRaidDungeon`)

Copied / ported / independently reimplemented: ported semantics, adapted to
the 1.12 codebase (`Map::IsDungeon && !IsRaid` instead of `MapEntry::
IsNonRaidDungeon`; `PossibleAttackTargetsValue::HasBreakableCC/
HasUnBreakableCC` as the already-CC'd test; central `AoeTrigger` gate instead
of per-spell guards). Also fixes doc overclaims: Warlock *Seduce* is not a CC
executor (no CC-flagged seduction action; aura not in the breakable set);
Hunter (*Freezing Trap*/*Scare Beast*) and Paladin (*Turn Undead*) added as
eligible executors.

Reason: CC stage 1 of the raid-mark ownership plan (player keeps maximum
control: explicit orders > automation). No core changes; module only.

Local validation:
- `python3 tools/verify_okf.py` + `./tools/verify_all.sh` (see commit); `git diff --check` clean.
- No build (per task constraints); live in-game check pending (see `scratchpad/research/impl-cc-stage1.md`).

## CC stage 3: opt-in smart auto CC ("auto cc" strategy) — 2026-09-25

Feature: OFF-by-default `auto cc` combat strategy toggled via
`.bot action auto cc [on|off]` (persisted per bot through PlayerbotDbStore
like the loot toggle; bare `auto cc` flips; also directly via
`.bot strategy +/-auto cc`). While ON, CcTargetValue may pick a loose add
that is hitting a party healer/caster (never the tank), that nobody in the
group is attacking (members + pets, via GetVictim), that carries no
SPELL_AURA_PERIODIC_DAMAGE aura from any source, and that is not the last
live enemy, skull-marked, or the tank's target. Explicit `.bot action cc`
marks always win (assigned target returns first); the toggle bypasses the
stage-1 5-man dungeon mark gate while ON. One bot per mob falls out of the
shared aura state (first sheep trips the stage-1 no-CC-over-CC guard for
everyone else); one auto target per bot falls out of the HasMyAura pre-pass;
no re-sheep once DoT'd/attacked. Stage-1 AoE interlock protects the
auto-sheeped mob. Works through the existing per-class CC actions for every
CC-capable class (mage Polymorph primary; no new spells).

Source repository: `mod-playerbots/mod-playerbots`

Source commit: `b6696bdbd3740e575598d167d69f39f68cc0b907` (local
`playerbots-references/mod-playerbots` checkout); shyalya DoT-avoidance
precedent `CcTargetValue` check #8 (no-CC-on-dotted, fear/banish exempt —
here generalized to every spell with no exemptions).

Source files:
- `src/Ai/Base/Value/CcTargetValue.cpp:23-85` (candidate filter order)
- `src/Ai/Base/Trigger/GenericTriggers.cpp:575-595` (dungeon RTI gate bypassed)
- `src/Ai/Class/Druid/Action/DruidActions.cpp:158-170` (AoE-guard idea, reused as-is)

Copied / ported / independently reimplemented: independently reimplemented
(no donor code copied). New `AutoCcStrategy` marker strategy +
`IsAutoCcTarget` candidacy in the existing chooser; `HasCcTargetTrigger`
dungeon gate bypass; `auto cc` intent in ParseAction/HandleAction mirroring
the aoe/loot toggle loop (persist + ACK on|off|mixed).

Reason: CC stage 3 — owner's "sheep the loose add, but never a mob being
attacked or DoT'd". Player keeps maximum control: automation is opt-in,
explicit orders always win. No core changes; module only.

Local validation:
- Cached `MODULE_TORTOISEBOTS=static` build via `./dev/build-playerbots`
  (tortoise-docker-penqle) — `mangosd` linked.
- `python3 tools/verify_action_trigger_wiring.py` (live-missing=0),
  `python3 tools/verify_okf.py`, `./tools/verify_all.sh`; `git diff --check`.
- Live in-game check pending (see `raport-nocny-2026-09-25/research/impl-cc-stage3.md`).

## Nearby quest mirror on master NPC-accept (#278) — 2026-09-25

Feature: grouped bot near its master takes the same quest when the master
accepts it from an NPC, without walking to the giver and without enabling
SyncQuestWithPlayer.

Source repository: `mod-playerbots/mod-playerbots`

Source commit: `917a22bc30272f5fe7956abdcc7b8c1e7c893ba6` (local
`playerbots-references/mod-playerbots` checkout; donor default
`AiPlayerbot.SyncQuestWithPlayer=1`).

Source files:
- `src/Ai/Base/Actions/QuestAction.cpp:187` (distance gate bypassed when
  sync enabled) and `:243-248` (`AddQuest` fallback after the opcode fails
  the distance check)
- `src/Ai/Base/Actions/TalkToQuestGiverAction.cpp:38-50,272` (donor progress
  sync; deliberately NOT ported — no donor-style progress sync exists here)
- `src/Ai/Base/Actions/LootAction.cpp:508` and
  `src/Ai/Base/Value/ItemUsageValue.cpp:120-131` (why sync stays off: bots
  refuse quest-item loot / treat master's quest items as their own)

Copied / ported / independently reimplemented: ported (adapted). The donor's
`AddQuest` fallback after the accept opcode fails the core's
`INTERACTION_DISTANCE` check is kept, but instead of `SyncQuestWithPlayer` it is
gated on `QuestAction::IsNearGroupedMaster()` (bot alive, grouped with its
master, same map, within `reactDistance`) plus `CanTakeQuest`/`CanAddQuest`,
NPC/object givers only. It only fires for the quest being accepted (the
sniffed master accept), never for the giver's other quests. Shared quests
(`CMSG_PUSHQUESTTOPARTY`) untouched.

Reason: a follow-distance bot sniffs the master's
`CMSG_QUESTGIVER_ACCEPT_QUEST`, but the core refuses the replayed accept
beyond `INTERACTION_DISTANCE`, so the bot never got the quest.

Local validation:
- `python3 tools/verify_okf.py`, `bash tools/verify_all.sh`;
  `git diff --check`.
- Module build by orchestrator (workers do not run the docker builder).
- Live in-game check pending (group with bot at follow distance, accept an
  eligible quest, bot takes it; ineligible/full-log/full-bag bots decline
  with the usual message).

## Seed gear source tiers + rare world epics + low-level coverage — 2026-09-25

Feature: per-item source-tier classification (base / end-game dungeon /
raid, orthogonal REP/PVP flags) computed once at startup, persisted in
`ai_playerbot_item_info_cache`, and enforced on the fresh-seed/hire gear
path behind `RandomGearMaxSourceTier` (default 0) + REP/PVP toggles; rare
world-epic per-slot gate (`RandomGearSeedEpicChance`, default 0.02);
weight-1 jewellery allowed below 30 plus a usable-item fallback sweep for
thin low-level slots.

Source repository: native implementation on owner spec (roadmap #289);
research in scratchpad `research-gear.md` (§6 rare epics, §7 crafted chain,
§8 tier proposal) and `research-gear-gaps.md` (§9 empty slots). Donor
`mod-playerbots` offers no reuse here: its pool has no raid/tier/rep filter
(map/rep checks commented out, `RandomItemMgr.cpp:2205,2316,2441`).

Source files (donor, reference only):
- `src/Mgr/Item/RandomItemMgr.cpp` (`BuildCacheEquipNew`, `IsValidItem`)
- `src/Bot/Factory/PlayerbotFactory.cpp` (`InitEquipment`, `InitAmmo`)

Copied / ported / independently reimplemented: independently reimplemented
(no donor code copied). Loot→spawn→map minima with lowest-tier-source-wins
(shared generic tables stay base), crafted products via the recipe chain
(item class 9 → LEARN spell → craft spell → CREATE_ITEM), quest rewards via
the quest reverse-lookup (raid quest raises, other quests keep base),
world-epic attestation via direct world-map loot rows.

Reason: owner rules — no rep/PvP/raid/end-game-dungeon gear on seeded bots,
rare (not flooded) world epics, no empty low-level slots — with a
configurable tier cap so players can later unlock higher tiers for hired
bots instead of a hard-coded exclusion list.

Local validation:
- `python3 tools/verify_okf.py`, `bash tools/verify_all.sh`;
  `git diff --check`.
- DB before/after estimates from tw_char/tw_world (see summary).
- Module build by orchestrator (workers do not run the docker builder).

| Level-appropriate bot enchant selection (candidate pool + weight scorer + proc model) | `mod-playerbots` `src/Bot/Factory/PlayerbotFactory.cpp:5105-5290` (`ApplyEnchantAndGemsNew`: per-slot DBC scan, `IsFitToSpellRequirements` mask fit, `StatsWeightCalculator::CalculateEnchant` best-pick) + `Player::CastItemCombatSpell` proc-chance formula (`tortoise-wow` `src/game/Objects/Player.cpp:8995-9001`) | `mod-playerbots` donor behavior per research-enchant-code.md §7 (live-path reference, no SHA pinned) / core proc formula read 2026-09-25 | `ai/playerbot/RandomItemMgr.{h,cpp}` (`LoadBotEnchantCandidates`, `CalculateBestBotEnchantId`, `CalculateProcEnchantWeight`), `ai/playerbot/PlayerbotFactory.{h,cpp}` (`ApplyBestEnchant`), `ai/playerbot/strategy/actions/UpdateGearAction.{h,cpp}`, `data/sql/world/20260925150000_world.sql` | Reimplemented: curated SQL candidate pool (slot/min-level/tier/rep/premium from DBC+DB research, not a live DBC scan) with owner quality ceiling (grey none, white min_level+10, green min_level+5, blue non-premium, epic premium); proc scoring via expected-damage PPM model instead of donor's unweighted path | `python3 tools/verify_okf.py`, `bash tools/verify_all.sh`, `git diff --check`; module build by orchestrator (workers do not run the docker builder); armory per-bot enchant verification pending |
| Masterless random-bot quest-log upkeep (drop COMPLETE-but-unrewardable quests, grey accept gate unless equip upgrade, hand-in travel from the first finished quest, clean on nearly-full timer) | `mod-playerbots` `src/Ai/World/Rpg/Action/NewRpgBaseAction.cpp` (`IsQuestWorthDoing`/`IsQuestCapableDoing` `:556-587`, `OrganizeQuestLog` `:590-691`, donor `src/PlayerbotAIConfig.cpp:717` `AiPlayerbot.DropObsoleteQuests`) @ b6696bdbd3740e575598d167d69f39f68cc0b907 | `src/Ai/World/Rpg/Action/NewRpgBaseAction.cpp:556-693` | `ai/playerbot/strategy/actions/DropQuestAction.{h,cpp}` (COMPLETE `!CanRewardQuest` drop, class-protected, log-nearly-full timer), `ai/playerbot/strategy/actions/AcceptQuestAction.cpp` (grey = `GetQuestLevelForPlayer+5 < level` unless `NeedQuestRewardValue` upgrade), `ai/playerbot/strategy/actions/ChooseTravelTargetAction.cpp` (hand-in latch from the first finished quest for masterless randoms), `ai/playerbot/strategy/triggers/GenericTriggers.h` + `TriggerContext.h` (`quest log nearly full`), `ai/playerbot/strategy/generic/MaintenanceStrategy.cpp`, `ai/playerbot/PlayerbotAIConfig.{h,cpp}` + `aiplayerbot.conf.dist.in` (`AiPlayerbot.BotQuestLogUpkeep`, default 1) | Reimplemented as this tree's own predicates and trigger wiring (donor idea only, no donor code copied): donor grey threshold uses `CONFIG_QUEST_LOW_LEVEL_HIDE_DIFF`, this tree uses core quest-color `+5` per the task spec; DELIVER/incomplete rules and upkeep gating are tree-local | `tools/verify_all.sh`, `git diff --check`; no docker build or runtime (per task constraints) |

## Hostile-town avoidance + player-killer avoidance — 2026-09-27

Feature: random masterless bots refuse travel destinations, travel-node route
legs and grind targets guarded by town guards hostile to their team
(`AiPlayerbot.AvoidHostileTowns = 1`), and a bot killed by a named player
avoids re-engaging that killer for 10 minutes (retaliation preserved).

Source repository: `mod-playerbots` @ `b6696bdb` (local checkout
`../playerbots-references/mod-playerbots`).

Source files (donor, reference only):
- `src/Bot/RandomPlayerbotMgr.cpp:1661-1665` (RandomTeleport enemy-zone reject:
  `if (zone->team == 4 && bot->GetTeamId() == TEAM_ALLIANCE) continue;` and the
  Horde mirror), `:1861-1870` (same in GetPlayerZoneTeleportLocations),
  `:3200-3203` (battlemaster hub team reject).
- `src/Ai/Base/Value/EnemyPlayerValue.cpp:20-33` (NearestEnemyPlayersValue:
  opposing + flagged + not-prohibited + master-flagged).
- `src/Ai/Base/Value/AttackersValue.cpp:176-202` (unflagged-player reject,
  both-sides prohibited reject).

Copied / ported / independently reimplemented: reimplemented, not copied. The
donor filters whole zones by `AreaTable.team` at teleport time and has no
random-vs-random PvP exemption (its bots fight on PvP realms). This change
fills the gap the donor leaves: contested-zone towns (Splintertree, Booty Bay,
Southshore, Menethil) carry no enemy zone team, so the check is per-spawn —
static `creature` spawn data + faction-template hostility + guard-level gate
(level_max >= bot+5) — at the three module choke points that already own
faction gating (`SetBestTarget`, `RouteIsSurvivable`, `AttackersValue::
IgnoreTarget`), plus a 10-minute named-killer avoidance (donor has none)
keyed like the existing lethal-kind rule in `PlayerbotAI::OnDeath`.

Reason: live 500-bot realm 2026-09-27: 12.6% of deaths are +13-level town
guards (Splintertree, Nijel's Point, Lakeshire, Menethil, Theramore,
Astranaar, Southshore, Booty Bay elites); 11% are bot-vs-bot PvP with a
5-bot Ashenvale cluster trading kills every 48-64 s for 10+ min.

Local validation:
- `python3 tools/verify_okf.py`, `bash tools/verify_tortoise_surface.sh`,
  `python3 tools/verify_action_trigger_wiring.py`, `bash tools/verify_all.sh`;
  `git diff --check`.
- Live verification (no docker build in this worktree): guard-faction masks
  from `tw_world` SELECTs, `deaths.csv` killer breakdown, `bot_events.csv`
  `debug travel` skip lines after the next module build.

## Bounded consumable seeding (oils/stones/poisons) + reagent top-up clamp — 2026-09-28

Feature: `PlayerbotFactory::AddConsumables` tops one small stack of the
current level tier only (oils x2 like the donor, stones/poisons x5 as seeded
before) and `InitReagents` tops up to its stack target, never above. The
top-up helper only removes STRICTLY lower tiers of the same line, keeps any
higher tier the bot owns (raid consumables, self-made stacks), never trims
the target stack down, grants at most the missing count, and hunters receive
no melee sharpening/weight stones. Wizard vs mana oil is spec-aware
(mage/warlock/shadow priest get wizard oil, other priests mana oil) with two
separate families so one line never purges the other.

Source repository: `mod-playerbots` @ `b6696bdb` (local checkout
`../playerbots-references/mod-playerbots`).

Source files (donor, reference only):
- `src/Bot/Factory/PlayerbotFactory.cpp:1090-1303` (`InitConsumables`:
  spec-aware oil pick — shadow priest wizard oil, holy mana oil, mage
  wizard oil — x2; stones/poisons x20 via `items.push_back({id, N})` top-up
  at `:1298-1303`: `count = N - GetItemCount; if (count > 0) StoreItem`).

Copied / ported / independently reimplemented: reimplemented, not copied.
The donor tops up per item id with no family purge and no raid-tier guard;
this change adds the low-to-high family ordering (lower tiers purged, higher
kept) and clamps the local overshooting `InitReagents` grant
(`> maxCount` + unconditional `urand(max/2, max*regCount)`) to a
never-above-target top-up. Poison/stone keep count stays 5 (local seed
shape), not the donor's 20.

Reason: in this DB oils are `stackable = 1`, so the old `StoreItem(id, 5)`
per tier granted 5-10 bag slots per caster with no family bound and no
re-seed purge; `InitReagents` overshot its target every run.

Local validation:
- `bash tools/verify_all.sh`; `git diff --check`.
- Module build by orchestrator (workers do not run the docker builder).

## Loot every kill; bounded give-up on unreachable corpses; money loot event — 2026-09-28

Feature: after combat, a bot loots every corpse it (or its group) killed and is
allowed to loot — nearest first — before pulling the next mob, instead of only
when already standing on a corpse or when no hostile is in range. A corpse that
cannot be pathed to is abandoned after a bounded number of failed approaches.
Looting money now writes a `LootMoney` row (copper amount) to
`bot_events.csv`, so money income is measurable.

Source repository: `mod-playerbots` @ `5397110` (local checkout
`../playerbots-references/mod-playerbots`).

Source files (donor, reference only):
- `src/Ai/Base/Trigger/LootTriggers.cpp:12-56` — `LootAvailableTrigger`
  (donor: in-range OR `all targets` empty, plus a "stale target that became
  not loot-possible is reported active so `loot` can pick another") and
  `FarFromCurrentLootTrigger` (`>= INTERACTION_DISTANCE - 2`).

Copied / ported / independently reimplemented: reimplemented, not copied.
Kept the donor's stale-selection rescue; dropped the `all targets` empty gate
(it only loots when hostiles are gone, which starves the chain in the dense
grind zones here). Added a native bounded give-up (`LootObjectStack`
approach-failure memory: `Add` refuses and `OrderByDistance` skips an abandoned
guid until the memory ages out) wired from `MoveToLootAction`, and a
`StoreLootAction` money log line. Donor has no equivalent give-up or money row.

Reason: the live economy trace (2026-09-28) found ~22% of kills looted;
`attack anything` (5.0) pulled the next mob before the loot-selection action
ever ran while hostiles were near, so the "far from current loot" ->
"move to loot" path never started.

Local validation:
- `bash tools/verify_all.sh`; `git diff --check`.
- Module build by orchestrator (workers do not run the docker builder).

### Follow-up 2026-09-29 — the ported triggers now use the core's own loot rules

No new donor code; the ported triggers above were re-pointed at the core rules
after the 2026-09-28 economy review showed the donor shape fighting them:

- Loot range is one predicate now (`LootObject::IsInLootRange`): 3D
  `Player::GetMaxLootDistance` for corpses (what `Player::SendLoot` enforces),
  `INTERACTION_DISTANCE` for game objects. The donor's `>= INTERACTION_DISTANCE - 2`
  2D shape left `can loot` (8.0) firing while `open loot` failed the server's 3D
  gate every tick on sloped ground, so `move to loot` (7.0) never ran.
- Corpse entitlement is `LootObjectStack.cpp`'s `MayLootCorpse`: the core's
  `Player::IsAllowedToLoot` (the same call that masks `UNIT_DYNFLAG_LOOTABLE` per
  viewer, i.e. where the round-robin turn lives) with two clauses trimmed that
  only exist so that a *player* at the corpse can open it for the party:
  an unblocked over-threshold item no longer entitles the whole party (the roll
  reaches bots wherever they are, the turn holder opens the corpse) and under
  MASTER_LOOT only the master looter -- in the overworld too -- or a bot with
  personal quest/FFA loot opens it (the module's own check only covered
  dungeons). Free-for-all, the allowed-looter set and per-player items stay with
  the core. A pet-only kill stays excluded: `Unit::Kill` credits no player for
  it, so no loot is ever rolled and `Player::SendLoot` refuses the corpse.
- Corpse items are looted before skinning (`LootObject::Refresh` takes the loot
  path first and arms the skin path only when `loot.isLooted()` and
  `Creature::IsSkinnableBy`), matching the Skinning conditions in
  `Spell::CheckCast`.
- The master safe-range rule (`AiPlayerbot.LootDistance`) is applied inside
  `LootObjectStack::OrderByDistance`, so "has available loot", target selection
  and "far from current loot" cannot disagree.
- `MoveToLootAction` counts a launched-but-stationary approach as a failure, so
  the bounded give-up also covers navmesh paths that degrade to a direct spline.

### Follow-up 2026-09-29 (2) — loot owns the kill before the next pull

No new donor code. Live economy trace on the 500-bot level-1 pool (main
`a072ed0`, `bot_events.csv` + `loot.log`): 0.30 loot lines per kill, 25% of kills
looted within 30s. A kill followed by an attack order within 2s produced loot on
7% of corpses, against 43% when the bot stayed out of the next fight for 5-10s,
and the median loot delay was 5s — the two ported pieces above were still
fighting the engine's own state machine:

- `LootAvailableTrigger` (`LootTriggers.cpp`) tested `AI_VALUE2(bool, "combat",
  "self target")`, i.e. `UNIT_FLAG_IN_COMBAT` (+ any group member in combat
  within `AiPlayerbot.ReactDistance`, 150 yd). The engine's combat/non-combat
  state comes from the "combat start"/"combat end" reaction (`has attackers`),
  not from that flag, so the flag's short post-kill linger suppressed "loot"
  (6.0) while `attack anything` (5.0) was free to order the next pull. The loot
  chain only exists in the non-combat engine, so the clause added nothing except
  the loss. The gate is `bot->GetAttackers()` now: the core's own "attacking me"
  set, filled in `Unit::Attack` (an add counts the moment it aggros, before its
  first hit) and drained in `CombatStop`/`AttackStop` (its death or evade). That
  keeps a fresh kill looting at once - the dead mob is already out of the set -
  while closing the engine's 2s-cached `has attackers` blind spot, in which a
  corpse could make the bot kneel for `lootDelay` next to an add that already had
  it as its victim. `mounted` still blocks.
- `LootObjectStack` dropped a queued corpse 30s after its **first** `Add` and
  never refreshed the timestamp on a repeat `Add`. Corpses are queued when the
  bot starts attacking (`AttackAction::Execute`) and re-offered on the kill's
  `SMSG_LOG_XPGAIN` and by every `add all loot` sweep, so a level-1 fight (where
  a kill can take longer than 30s) expired the corpse before it was even dead,
  and after that no add could revive it. TTL is 180s now (`Corpse.Decay.NORMAL`
  is 300s) and re-adding refreshes the entry's age.

Local validation:
- `bash tools/verify_all.sh`; `git diff --check`.
- Module build by orchestrator (workers do not run the docker builder).
- Live indicator to watch: `loot.log` lines per `XpGainAction` kill 0.30 -> >0.8;
  `StoreLootAction`/`LootMoney` rows in `bot_events.csv`.

## Hunter quiver / warlock soul-bag handling — 2026-09-28

Feature: hunter quiver/ammo-pouch slot reservation (`InitBags` leaves one
slot free for hunters) and tier upgrades across quiver tiers (`InitAmmo`
moves ammo aside into backpack/bag space before equipping the better
quiver, so a quiver full of ammo can still be upgraded); quiver in the loot
push filter; class-aware bag-slot choice in `EquipAction` (a quiver may
only replace a quiver or take an empty slot, a plain bag never evicts a
quiver or soul bag, a soul bag replaces a soul bag or takes an empty slot
and, as the first one, evicts the smallest plain bag, with loud
`INVENTORY_FULL` instead of silent no-ops); bag and quiver items bypass
per-slot resolution in the equip audit (core resolves an `INVTYPE_BAG`
probe to a single slot and vetoes it while a quiver is worn, so a quiver
upgrade never resolved to a slot); a bag swap reads the item's live
position (evacuating the replaced bag can move the upgrade bag itself) and
only reports success when the bag really reached the slot; quiver + soul
bag usage in `ItemUsageValue` (first quiver/soul bag equips, bigger soul
bag replaces a smaller one, plain-bag upgrades measured against plain
containers only).

Source repository: `mod-playerbots` @ `b6696bdbd3740e575598d167d69f39f68cc0b907`
(local checkout `../playerbots-references/mod-playerbots`).

Source files (donor, reference only): no donor equivalent - donor
`InitBags` fills all four slots uniformly and its equip path only handles
`ITEM_CLASS_CONTAINER`; it never equips quivers/soul bags specially.

Copied / ported / reimplemented: reimplemented, not copied. Bag-slot rules
follow the core storage contracts in `tortoise-wow`
`src/game/Objects/Player.cpp` (`CanEquipItem` one-quiver veto,
`CanUnequipItem` non-empty-bag rule, `SwapItem` bag-exchange gate).

Reason: seeded hunters filled all four bag slots with plain bags before
`InitAmmo` ran, so the quiver never equipped; hunters with a quiver full
of ammo could not upgrade across tiers; warlocks never equipped a soul bag
(`ITEM_USAGE_NONE` hard-coded).

Local validation:
- `bash tools/verify_all.sh`; `git diff --check`.
- Module build by orchestrator via `scratchpad/build-commit.sh` (workers do
  not run the docker builder).



## Ranged kit keeps its casting band (`enemy out of spell` -> `reach spell`) — 2026-09-29

Feature: the `ranged` combat strategy (`RangedCombatStrategy`, registered as the
`ranged` strategy) also fires `reach spell` at `ACTION_HIGH` when its current
target is out of the caster's spell range, so a ranged kit pushed out of its
band walks back in instead of idling. The trigger it hangs on (`enemy out of
spell`, class `EnemyOutOfSpellRangeTrigger`) additionally requires a hostile
target, so a stale or friendly `current target` cannot drag a ranged bot across
the zone.

Source repository: `mod-playerbots` @
`b6696bdbd3740e575598d167d69f39f68cc0b907` (local checkout
`../playerbots-references/mod-playerbots`).

Source files (donor, reference only): `src/Ai/Base/Strategy/CombatStrategy.cpp`
(`"enemy out of spell"` -> `reach spell`, `ACTION_HIGH` — the donor's first
trigger in its base combat strategy).

Copied / ported / reimplemented: reimplemented, not copied. The donor registers
the node in its base `CombatStrategy`; the module's `CombatStrategy` live hook
(`CombatStrategy::InitCombatTriggers`) is only reached by the unregistered
vector-API class trees (`Generic{Druid,Hunter,Paladin,Priest}Strategy`), while
every live class kit chains through `ClassStrategy`. The rule therefore sits
with the ranged kit itself, next to the complementary
`enemy too close for spell` -> `flee` node, and melee specs — which own a
higher-relevance `reach melee` — are not given a spell-range closing rule.

Reason: donors keep a ranged bot inside its casting band from both sides. Without
the rule nothing closed the distance again once a target left the band: the
hunter's auto-shot prerequisite only covers "beyond the weapon's maximum range",
and a caster's spell prerequisite only exists while a spell is castable (out of
mana or on cooldown, it has no distance action at all).

Local validation:
- `bash tools/verify_all.sh` (OKF, surface, action/trigger wiring, engine unit
  tests, policy tests, decision trail, talent presets, host contract);
  `git diff --check`.
- Module build via `build-commit.sh`; runtime measurement via the hunter
  telemetry rows (`AutoShot`, `SwitchToMelee`, `SwitchToRanged`) — see
  `docs/classes/hunter.md` and the 2026-09-29 hunter fix summary.

### Same section — dead-zone step back (`enemy too close for auto shot` -> `flee`)

Feature: a hunter that still has the `ranged` kit and a loaded ranged weapon
steps back out of the shot's own minimum range (`EnemyTooCloseForAutoShotTrigger`
in `HunterTriggers.h`, node in `HunterStrategy::InitCombatTriggers` at
`ACTION_MOVE - 1` → `flee`), because the generic `enemy too close for spell`
flee is suppressed while a fast target is glued to the bot and no shot is
possible inside that range.

Source files (donor, reference only): `src/Ai/Class/Hunter/Strategy/GenericHunterStrategy.cpp`
(`"enemy too close for auto shot"` → `disengage`, `flee`).

Copied / ported / reimplemented: reimplemented. Only the trigger *name* is
donor's; the donor's own file is the unregistered forward-port in this module
(`ai/playerbot/strategy/hunter/GenericHunterStrategy.cpp`, references the
never-registered `disengage` action), so the live trigger is written against
the module's existing `flee` action and gated on the loaded ranged weapon
instead of the donor's action list. Relevance sits one step below the melee
switch so a hunter that may switch (level >= 10, or without a loaded ranged
weapon) keeps its melee fallback first.

## A grind order walks the bot to its target (`AttackAnythingAction`) — 2026-09-30

Feature: the autonomous grind order (`AttackAnythingAction::Execute`, node
`"no target"` -> `"attack anything"` in `GrindingStrategy`) carries the approach
to the mob it just selected. When the target is outside attack range the action
now runs the kit's reach action (`reach melee` for a melee kit, `reach spell`
for a ranged one) instead of stopping the bot where it stands; only a target
already inside attack range falls back to `ai->StopMoving()`.

Source repository: `mod-playerbots` @
`b6696bdbd3740e575598d167d69f39f68cc0b907` (local checkout
`../playerbots-references/mod-playerbots`).

Source files (donor, reference only): `src/Ai/Base/Actions/ChooseTargetActions.cpp`
(`AttackAnythingAction::Execute` sets the `pull target` and clears the motion
master, with `// bot->StopMoving();` deliberately commented out, so the order
never breaks the approach) and `src/Ai/Base/Strategy/GrindingStrategy.cpp`
(the same `"no target"` -> `"attack anything"` node).

Copied / ported / reimplemented: reimplemented. The port had replaced the
donor's commented-out stop with an unconditional `ai->StopMoving()` after the
order, and left the approach to the combat engine's reach action alone. The
reach action is real and works (live: 22 `reach melee` and 25 `reach spell`
action samples among the 188 of 395 bots that moved), but it is not the only
thing racing for the engine's single action slot per tick, so it must not be
the order's only path to the target.

Reason: with the stop unconditional, a bot that ordered a mob 20-60 yd away
stayed exactly where it was. Live measurement over the whole random-bot pool
(395 bots, 150 s, 1 Hz observability samples): 207 bots ended the window at the
position they started it (0 yd displacement) and none of them ever executed a
reach action, while the 188 that moved did; 53 of the frozen bots spent the
window looping `reset`/`unstuck` (the combat-stuck rescue, which forces the
engine back to non-combat and clears the target), 15 looped `select new target`,
and 18 placed grind orders that changed nothing. The mob never gets to aggro
either (`victim=0` in every `GrindTargetRepeat` row), so the fight never starts
and the bot re-orders the same creature for hours (frozen sequences of 140 min
average, up to 280 min, byte-identical positions).

Local validation:
- `bash tools/verify_all.sh` (OKF, module surface, action/trigger wiring, engine
  unit tests, policy tests, decision trail, talent presets, host contract);
  `git diff --check`.
- Module build via `build-commit.sh` (no deploy).
- Live indicators to watch after deploy: `GrindTargetRepeat` rows with
  `dist` > 10 yd and `victim=0` (the never-approached order) should vanish;
  kills/attack for level 1-3 bots should rise from ~0.03-0.2 toward the
  level 5 rate (~1.0); `UnstuckTrip` rows per bot-hour should fall with the
  `reset`/`combat (long) stuck` share; the frozen-population share measured by
  the observability daemon (bots with 0 yd displacement over a 60 s window)
  should collapse from ~53%.

### Follow-up 2026-09-30 — review of the grind approach (PR #361): walk wait and the leveling druid

Two review findings, both fixed in the tree that carries the approach rule.

**1. The wrapper action erased the movement wait.** `AttackAnythingAction::Execute`
runs the reach action nested (`ai->DoSpecificAction`), and the reach books its own
wait (`ReachTargetAction::Execute` -> `WaitForReach` -> `Action::SetDuration` ->
`Engine::ListenAndExecute` -> `PlayerbotAI::SetActionDuration`). The engine applies
the duration of the action it executed *as a whole* after `Execute()` returns, and
that outer action (`AttackAnythingAction`) carries the `Action` default
(`sPlayerbotAIConfig.reactDelay`), so the wait collapsed to the 100 ms default: the
bot ticked again immediately, the combat engine re-ran `reach melee`/`reach spell`,
and the spline was relaunched once per tick for the whole walk. The wrapper now
carries the wait the reach asked for (`SetDuration(ai->GetAIInternalUpdateDelay())`)
and releases it again to the default when no movement was taken, so a cached action
object cannot keep a stale walk wait. Local validation: `tools/verify_all.sh`;
module build via `build-commit.sh` (no deploy); the walk wait is observable as
`sPlayerbotAIConfig.MaxWaitForMove`-capped AI ticks (default 3 s) instead of one
tick per 100 ms while a bot closes distance.

**2. The below-10 druid had no reach node at all.** `LevelingDruidStrategy` wired
its Wrath node to the trigger name `enemy out of melee range`, which no creator
registers — `TriggerContext.h` registers `enemy out of melee` (`EnemyOutOfMeleeTrigger`;
`enemy out of melee range` is only that trigger's own display name, which never
participates in node lookup, `Engine::ProcessTriggers` resolves by node name).
`tools/verify_action_trigger_wiring.py` reported it as a dead-tree trigger, so the
node never fired, and because the kit owns neither `close` nor `ranged` the generic
`enemy out of melee` -> `reach melee` rule was missing from the engine as well: a
below-10 druid could not re-close a fight (knockback, a mob that runs) and had no
ranged answer while out of reach. Fixed by naming the live trigger and by adding the
same melee rule every other melee kit carries (`reach melee`, `ACTION_MOVE`, which
outranks the Wrath node so the bot walks in and still casts Wrath whenever reach
cannot run). Validator after the fix: dead-tree triggers 124 -> 123, no new missing
actions.

### Follow-up 2026-09-30 — refuse grind orders on mobs the bot cannot path to

The mine evade signature (`dun-elwynn.md`: 56/60 probe rows `notreach=1` with the mob still chasing, `|dz|>5` in 25/60, mine k/a 0.30 vs 0.45 in the field) is ordered by the bot, not caused by the core: the core flags a creature it cannot reach after 3 s (`Creature::IsEvadeBecauseTargetNotReachable`, `m_TargetNotReachableTimer`, full reset only at 24 s) while the grind loop kept ordering it for the whole window. Donor `mod-playerbots` (`GrindTargetValue.cpp`, `InvalidTargetValue.cpp`, `AttackersValue.cpp` @ `b6696bdb`) gates the pick on LOS and the Z gap (`INTERACTION_DISTANCE`) and drops evading holders, but has no notreach gate either — the window before the walk home is unhandled there too.

Reimplemented natively, cheapest shape: the grind loop refuses a notreach-flagged candidate in the attackers fast path and runs one `WorldPosition::canPathTo` on the chosen winner only (never per candidate per tick); `AttackAnythingAction` refuses a cached pick the core flagged after selection before `current target` arms; `InvalidTargetValue` drops a held target the moment the flag appears, so the existing reach give-up (15 s, `unreachable targets`/`unreachable entries`, 5 min) blacklists the guid/kind instead of the bot holding a ghost. Local validation: `tools/verify_all.sh`; module build via `build-commit.sh` (no deploy). Live indicator: mine k/a converging to field rate, `evading` grind rejects falling.

## Pool bots stop grouping with each other (`RandomBotGroupNearby` off) — 2026-09-30

Feature: `AiPlayerbot.RandomBotGroupNearby` defaults to `0` (code fallback and
template example), matching mod-playerbots. `InviteNearbyToGroupAction` and
`InviteGuildToGroupAction` skip masterless pool-bot targets, and
`AcceptInvitationAction` declines an invite whose leader is a masterless pool
bot. Invites from real players still group; hired companions and owner-account
bots keep their existing paths (hires bind via `HireLifecycle`, owned bots are
not random pool records, adopted party bots carry a real-player master).

Source repository: `mod-playerbots` @
`b6696bdbd3740e575598d167d69f39f68cc0b907` (local checkout
`../playerbots-references/mod-playerbots`).

Source files (donor, reference only): `conf/playerbots.conf.dist`
(`AiPlayerbot.RandomBotGroupNearby = 0`) and
`src/Bot/RandomPlayerbotMgr.cpp` (`leave group if leader is rndbot`:
`IsRandomBot(group->GetLeader())` -> `LeaveOrDisbandGroup`).

Copied / ported / reimplemented: reimplemented. No dissolve sweep: the pool is
being reset anyway, so existing bot-only groups vanish with it; the decline
gate uses the existing `SMSG_GROUP_DECLINE` + `UninviteFromGroup` reply path.

Reason: pool bots inviting each other (`invite nearby` / `invite guild` on
`often` / `random`, `GroupStrategy`) formed ~109 bot-only groups in a 500-bot
pool; members leash via `far from master` / grouped-travel and idle as
followers.

Local validation: `bash tools/verify_all.sh`; `git diff --check`. No docker
build (per task scope).

## Grind XP-eligibility predicate (`IsHonorOrXPTarget` instead of pre-attack `XP::Gain`) — 2026-09-30

Feature: `GrindTargetValue` skips the "not xp and not needed for quest" branch
only for mobs that truly give no XP, using the pure grey-level/no-XP-flag
predicate instead of a pre-attack `MaNGOS::XP::Gain` call.

Source project: `mod-playerbots` (`bot->isHonorOrXPTarget(unit)` gate in
`GrindTargetValue.cpp`) + current core `Player::IsHonorOrXPTarget`
implementation.

Source commit: `mod-playerbots@b6696bdbd3740e575598d167d69f39f68cc0b907`.

Source files: donor
`src/Ai/Base/Value/GrindTargetValue.cpp:74`; core
`src/game/Objects/Player.cpp:21934-21952` (`IsHonorOrXPTarget`),
`src/game/Formulas.h:34-42` (`GetGrayLevel`), `:102-170` (`Gain`),
`src/game/Objects/Creature.h:992-998` (`GetXPModifierDueToDamageOrigin`).

Ported / reimplemented: one-line predicate swap in
`ai/playerbot/strategy/values/GrindTargetValue.cpp:256`
(`!MaNGOS::XP::Gain(bot, creature)` -> `!bot->IsHonorOrXPTarget(unit)`);
comment records why. Log reason text and `urand` behaviour unchanged.
Core `IsHonorOrXPTarget` is exactly the donor-equivalent grey check the task
asked for (mob level above `GetGrayLevel`, not totem/pet,
`xp_multiplier != 0`, no `UNIT_STAT_NO_KILL_REWARD`); no `CREATURE_FLAG_EXTRA_NO_XP`
exists in this core, so no such flag check was added.

Reason: `Gain` multiplies by `GetXPModifierDueToDamageOrigin()`, which is 0
for a creature nobody has damaged yet — every untouched overworld mob counted
as "no XP" and was skipped 50/51 times, ending 87-98% of level 1-3 pool grind
picks with "no grind target found".

Other `MaNGOS::XP::Gain` uses in the module: none — the grind call was the
only one (`rg 'XP::Gain' ai/`), so no sibling fix was needed.

Local validation: `bash tools/verify_all.sh`; `git diff --check`.

## Grind leash: solo bots skip `CanFreeTarget`, dead far-from-master block removed — 2026-09-30

Feature: `GrindTargetValue` skips both `CanFreeMoveValue::CanFreeTarget` rejects
(attackers loop and scan loop) only when the bot has no master at all
(`ai->GetMaster() == nullptr`, i.e. solo pool bots); any follower — real-player
master, bot group leader, hired/owned bot in a party — keeps the free-move
leash. The "far from master" block is deleted: it was unreachable (the local
`master` was nulled for real players, which never have an AI, so
`master && HasRealPlayerMaster()` could never be true), and restoring it only
for bot masters was rejected — it would reintroduce the F-02 yoyo (follower
picks a target past follow range, `FollowMasterStrategy`'s "out of free move
range" recall at `ACTION_HIGH` (20) beats `attack anything` (5.0), bot
oscillates). `CanFreeTarget` already bounds followers to free-move range around
the follow target, so no second distance gate is needed.

Source project: `mod-playerbots` (grind leash shape in
`src/Ai/Base/Value/GrindTargetValue.cpp`) + donor report items 4-5.

Source commit: `mod-playerbots@b6696bdbd3740e575598d167d69f39f68cc0b907`.

Source files: donor `src/Ai/Base/Value/GrindTargetValue.cpp:83-93` (wider
`grindDistance` leash commented out; `follow`-only check vs `lootDistance`);
local `ai/playerbot/strategy/values/GrindTargetValue.cpp:104-107,168-176`
(`CanFreeTarget` gates; far-from-master block deleted).

Ported / reimplemented: `ai->HasRealPlayerMaster()` replaced with
`ai->GetMaster()` on both `CanFreeTarget` conditions; the far-from-master block
(and its now-unused `master` local) removed. Log reason texts unchanged.

Correction to the previous entry (F-04): `PossibleTargetsValue::IsPossibleTarget`
does NOT call `CanFreeAttack` — only `PossibleTargetsValue::IsValid` does, and
`CanFreeAttack` is a no-op anyway: `PlayerbotAI::GetRange("attack")` returns 0
(`PlayerbotAI.cpp:6935`), so `CanFreeMove` short-circuits at `if (!range) return
true` (`FreeMoveValues.cpp:94-95`). It never leashed grind picks. Follower
cohesion comes from the `CanFreeTarget` pick gate plus the follow strategy's own
"out of free move range" recall — not from `CanFreeAttack`.

Reason: level 1-3 pool bots (no master) ended ~80-92% of grind picks with "no
grind target found"; top rejects were "out of free range" (~30%, free-move
range around the bot group leader) and "far from master" (~17%, fired on
`follow` OR `wander` for bot-led pool groups). Bot-led pool groups dissolve on
the sibling branch, but hired/owned bots may still follow a bot leader in a real
player's party — they keep the leash so grind picks stay inside the follow
recall radius.

Local validation: `bash tools/verify_all.sh`; `git diff --check`.

## Level-appropriate grind destinations (`GrindSpotPolicy`) — 2026-10-01

Feature: a grind *destination* must be able to pay level-appropriate XP. For
autonomous (masterless random) bots the accepted creature window is now
`[botLevel - 2, botLevel + 1]` instead of the old "roughly half the bot's level"
window; owned/hired bots keep the old conservative window. Because the window
travels with the bot, outgrowing a spot invalidates the travel target that led
there (`GrindTravelDestination::IsActive` -> `IsPossible`), the cooldown forces a
new request, and the next request walks the bot through the travel graph (foot,
taxi, boat, zeppelin - no teleports) to the nearest field/zone whose creatures
still fit. Same window at every level, so the ladder runs 1 -> 60.

Source project: `mod-playerbots` (NewRPG "go grind": level-bracketed random grind
POIs and their re-roll), plus donor report items 1-2.

Source commit: `mod-playerbots@b6696bdbd3740e575598d167d69f39f68cc0b907`.

Source files: donor `src/Ai/World/Rpg/Action/NewRpgBaseAction.cpp:964-1015`
(`SelectRandomGrindPos`: uniform pick from the level's POI set, 500 yd / 2500 yd
ranges, `/3` below level 5), donor `src/Mgr/Travel/TravelMgr.cpp:4812-4828` (POI
cache bucketed per level as `[midLevel - RandomBotTeleLowerLevel,
midLevel + RandomBotTeleHigherLevel]`), donor `conf/playerbots.conf.dist:1599,1604`
(`RandomBotTeleLowerLevel = 1`, `RandomBotTeleHigherLevel = 3`); local
`ai/playerbot/TravelMgr.cpp:620-648` (`GrindTravelDestination::IsPossible`),
`ai/playerbot/GrindSpotPolicy.h` (new), `ai/playerbot/TravelMgr.cpp:2431-2444`
(`IsLocationLevelValid` zone floor, unchanged, now the second half of the same
gate), `ai/playerbot/strategy/values/GrindTargetValue.cpp:23-31`
(`MaxGrindLevelOverBot`, the order cap the new ceiling respects).

Ported / reimplemented: donor picks a random POI from a level-bracketed set and
re-rolls on its RPG timeout; we filter the existing per-entry grind destinations
by the same kind of level bracket and let the existing destination-validity and
travel-target-expiry machinery do the re-roll - no new status machine, no new
timer, no teleport. Donor's lower bound (creature level >= bot level - 1) is our
`- 2` and donor's upper (bot level + 3) is our `+ 1`: our own order cap refuses
targets more than one level above a sub-10 bot (`MaxGrindLevelOverBot`), so a
destination further up would only ever be walked to for nothing. Beginner clamp
(levels 1-4 = own level) and the coinless-starter-beast rule are preserved for
owned bots, and subsumed by the ladder window for autonomous ones.

Reason: measured on the live pool (2026-10-01, 500 fresh level-1 bots). At level
5 the old window collapsed to exactly `level_max == 3` creatures and the zone
floor built on the same numbers rejected the point's own starting valley, so
level-5 bots received *zero* `Grind` travel targets (0 of 2,308 targets in the
last hour: 732 trainer, 683 skinning, 601 generic RPG, 258 herbalism, 34 mining)
and sat at level 5 - 157/500 after 90 minutes, 5 -> 6 never completing, raw XP
per kill down to 31.7 (against ~50 at levels 1-4). Offline replay against the
live world DB for a level-5 bot at the Northshire camp (1500 yd, gold-bearing,
non-elite, matching the purpose map): old band admitted exactly one creature kind
(Kobold Worker, level 3, 164 yd, inside the rejected starter area); new band
admits the Goldshire field (Defias Cutpurse 5-6 at 331 yd, Kobold Tunneler 5-6 at
921 yd) and, with the unchanged area gates, the camp itself is left behind.
Durotar/Valley of Trials: new band admits Vile Familiar (3-4), Yarrog Baneshadow
(5) and Kul Tiras Sailor (5-6, Tiragarde).

Local validation: `tools/test_grind_spot_policy.cpp` (band shape at levels 5/6/8/
60, monotone ladder 1-60, grey-level floor, owned-bot windows unchanged,
gear-condition independence) via `bash tools/verify_all.sh`; `git diff --check`;
module build via `build-commit.sh` (no deploy). Live indicators to watch after
deploy: `TravelTarget` rows with purpose `Grind` for level 5+ bots (should stop
being zero), `LeaveOutgrownZone` unchanged, XP per kill at levels 5-10 rising
toward the level-appropriate value, and the level-5 population draining.

### Follow-up 2026-10-01 — review of the grind ladder: coinless wildlife, level-10 ceiling, per-spot spread, outgrow expiry

The ladder as first landed (`06d1058`) had one arithmetic fix and three
follow-on defects, found in review against the live realm data and fixed in four
commits (`9f611ac`, `b868299`, `09522e2`, `4a6e62c`).

Feature: (1) coinless creatures are grind destinations for autonomous bots at
every level (critters stay out; owned/hired bots keep the copper-only set);
(2) the destination ceiling is +1 below level 10 (the grinder's own order cap) and
+2 from level 10 (combat cap is +4 there, but autonomous bots run on weak gear);
(3) a grind destination is skipped once as many travel targets hold it as it has
room for - a third of its spawn points, never fewer than two - so a backlog
spreads across the zone instead of stacking on the nearest field; (4) a grind
destination that went inactive because the bot outgrew it expires at once instead
of freezing the bot for the standard 60 s cooldown.

Source project: `mod-playerbots` (NewRPG go-grind level bracket and random POI
pick), plus the review report `review-ladder-report.md` (this task).

Source commit: `mod-playerbots@b6696bdbd3740e575598d167d69f39f68cc0b907`.

Source files: donor `src/Ai/World/Rpg/Action/NewRpgBaseAction.cpp:964-1015`
(`SelectRandomGrindPos` picks uniformly from the level's POI set), donor
`src/Mgr/Travel/TravelMgr.cpp:4812-4828` (POI cache bucketed per level), donor
`conf/playerbots.conf.dist:1599,1604` (`RandomBotTeleLowerLevel = 1`,
`RandomBotTeleHigherLevel = 3`); local `ai/playerbot/GrindSpotPolicy.h`
(`GrindPreyAllowed`, `GrindSpotCapacity`, the split ceiling),
`ai/playerbot/strategy/values/TravelValues.cpp:104-112` (purpose map),
`ai/playerbot/TravelMgr.cpp` (`IsPossible` prey gate, `AcquireGrindSpot` /
`ReleaseGrindSpot` / `IsGrindSpotCrowded` / `DropCrowdedGrindPoints`,
`TravelTarget::SetTarget` / `~TravelTarget`, `GrindSpotOutgrown` + `CheckStatus`),
`ai/playerbot/strategy/actions/ChooseTravelTargetAction.cpp:58-63` (crowd filter),
`ai/playerbot/strategy/values/GrindTargetValue.cpp:23-29` (`MaxGrindLevelOverBot`,
the cap the sub-10 ceiling mirrors).

Ported / reimplemented: donor gets its spread from a uniform `urand` pick over
the level-bracketed POI set (50% within 500 yd, else 2500 yd). Ours keeps the
existing nearest-range pick (so everyday hunts stay local and short) and adds the
demand cap instead: a spot only takes the bots it has spawns to feed, and the
existing distance partitions push the surplus outward. Demand is a per-destination
counter kept incrementally by `TravelTarget` (assign / release / destruction) -
one hash lookup per candidate, never a scan over the bot population; the counter
map is intentionally never destroyed because pool bots log out during shutdown
and their destructors can run after static destruction. The purpose map change is
global (team- and bot-blind), so the per-bot rule lives where the bot is known:
`GrindPreyAllowed(goldMin, critter, autonomous, beginner)`.

Reason: review of `06d1058` against the live realm. (a) The purpose map handed
`Grind` only to coin-bearing creatures, so beasts - wolves, boars, spiders,
scorpids, bears - were not destinations at all; at level 5 within 1500 yd of each
starter camp the eligible pool was 3-10 creature kinds, all humanoid camps
(Durotar: Vile Familiar + Kul Tiras Sailor + one rare, capacity 23 for 29 level-5
bots), which is why the review predicted a swarm on Tiragarde Keep and the
Frostmane camps. With wildlife in, the same radius holds 11-27 kinds and 91-147
slots (wildlife alone 58-94), so the same bots spread over open field. (b) The
`+1` ceiling starved the ladder from level 10 (`MaxGrindLevelOverBot` allows +4
there). (c) Nearest-first picking stacked every bot on one spot. (d)
`CheckStatus` sent an outgrown destination to the 60 s cooldown, during which the
target still counted as active and blocked the next request, freezing the bot on
every ding.

Local validation: `tools/test_grind_spot_policy.cpp` (band at 5/6/8/9/10/60,
monotone 1-60, grey floor, owned windows, gear independence, `GrindPreyAllowed`
matrix, `GrindSpotCapacity`) via `bash tools/verify_all.sh`; `git diff --check`;
module build via `build-commit.sh` (no deploy). Offline replay against the live
world DB: eligible level-5 creatures within 1500 yd of each starter camp, before
vs after. Live indicators to watch after deploy: `Grind` travel targets for level
5+ bots (was zero), the spread of `TravelTarget` areas per zone, XP per kill at
levels 5-10, and the level-5 population draining.

Out of scope for this bundle (noted by the review, not implemented): social aggro
and low-health flee behaviour of humanoid camps is a combat-AI matter (pull
discipline, runner handling), not a destination-selection one.

### Correction to `06d1058`

The first version of this bundle also claimed the beginner gold exemption in
`IsPossible` let start-valley bots grind coinless beasts. It could not: the
purpose map never gave those creatures a `Grind` purpose, so the exemption could
only ever see the old Scarlet whitelist. `GrindPreyAllowed` now owns both halves
of the rule (purpose map + per-bot gate).

## Hunter dead-zone melee fallback below level 10 — 2026-10-02

Feature: `SwitchToMeleeTrigger` / `SwitchToRangedTrigger` lose their sub-10 level
gates, so a hunter below level 10 whose target is glued inside the shot's dead
zone trades into melee (melee auto-attack + *Raptor Strike*) instead of holding a
ranged kit that cannot fire, and hands the ranged kit back whenever the target is
off the bot, immobilized, too slow to follow, or out of melee.

Source project: `mod-playerbots` (hunter "ranged" / "close" kit switches).

Source commit: `mod-playerbots@5397110cba484a9b7209bc9f632652e9d4bd6a70` (same
checkpoint recorded above for the class-strategy port).

Source files: donor `src/Ai/Class/Hunter/HunterTriggers.cpp:112-126`
(`SwitchToRangedTrigger`: `close && victim != bot && distance > 8`;
`SwitchToMeleeTrigger`: `ranged && victim == bot && distance <= 8` — neither has a
level gate); local `ai/playerbot/strategy/hunter/HunterTriggers.h`
(`SwitchToRangedTrigger`, `SwitchToMeleeTrigger`, removal of the
`HUNTER_KITING_LEVEL` constant), `ai/playerbot/strategy/hunter/HunterStrategy.cpp`
(dead-zone step-back comment). `docs/classes/hunter.md` §Ranged Combat updated.

Ported / reimplemented: donor behavior matched where it applies — no level gate
on either switch, and the step back to ranged happens only when the target is not
on the bot and the bot is out of melee. Our `SwitchToRangedTrigger` keeps its
existing richer conditions (target immobilized / too slow to follow / distance >
8) rather than the donor's bare `victim != bot && distance > 8`. No new action,
trigger, config key or spell: `SwitchToMeleeAction` already does
`bot->Attack(target, true)` + `-ranged,+close`, and the existing `enemy is close`
→ `raptor strike` trigger covers the melee ability.

Reason: measured on the live cycle-3 pool (2026-10-01, 94 hunter bots, 90 min).
At levels 1-9 the hunter has no rotation (Auto Shot only; Arcane Shot and Serpent
Sting are unlearned because the pool cannot pay the trainer), and the custom
sub-10 gate turned the weapon's 8-yd minimum range into a no-damage state: 100 %
of 3,177 `AutoShot` rows were `ranged=1,close=0`, 0 `SwitchToMelee` /
`SwitchToRanged` rows, 17 % of shot (re)starts at 9-11 yd and 8 starts per kill —
a bot repeatedly re-opening fire instead of fighting or holding position. Hunters
were the lowest-throughput class (median 573 XP/bot-h vs warrior 1,298). This
change grants no gold and no spells (owner rule).

Local validation: `bash tools/verify_all.sh`; `git diff --check`; module build via
`build-commit.sh` (no deploy). Runtime not verified here: the worktree is for
morning review and was not deployed.

## Pre-pull rest threshold: free-food bots rest to MediumHealth (`UseFoodStrategy`) — 2026-10-02

Feature: a bot whose food and drink are free (the item cheat granted by
`AiPlayerbot.RndBotCheats = repair,breath,item`) rests to
`AiPlayerbot.MediumHealth` (70%) before it takes its next fight, instead of
stopping at `AiPlayerbot.LowHealth` (50%). `UseFoodStrategy` registers the
`food` action on the critical/low/medium health bands for those bots and
`ShouldEatValue` uses `MediumHealth` as its threshold; bots without the cheat
keep the `LowHealth` band and threshold unchanged. The `food` action sits at
relevance 6.0, above `attack anything` (5.0) and the travel chain (1.0), so the
pull is deferred until the bar is back.

Source repository: `mod-playerbots` @
`b6696bdbd3740e575598d167d69f39f68cc0b907` (local checkout
`../playerbots-references/mod-playerbots`).

Source files (donor, reference only):
`src/Ai/Base/Strategy/UseFoodStrategy.cpp` (with `BotCheatMask::food`,
`"medium health"` -> `food`; without it, `"low health"` -> `food`),
`src/Ai/Base/Trigger/HealthTriggers.h` (`MediumHealthTrigger` spans `[0,
MediumHealth)`) and `src/Ai/Base/Actions/NonCombatActions.cpp`
(`EatAction::isUseful` is `health < 100`, so the trigger alone owns the band).

Copied / ported / reimplemented: ported with a local adaptation. This module
has no `food` cheat mask — the free rations ride on `BotCheatMask::item`, which
`EatAction`/`DrinkAction` already branch on — so the donor's food-cheat branch
is keyed on `item`. The donor's `MediumHealthTrigger` covers `[0, 70)`; here it
is `[LowHealth, MediumHealth)` and `LowHealthTrigger` is
`[CriticalHealth, LowHealth)`, so the strategy names the critical, low and
medium bands to cover the same range (the engine executes at most one action
per tick, so the repeated trigger is free).

Reason: the item cheat feeds a bot conjured rations but only the `LowHealth`
band asked for them, so every pool bot ate to half a health bar, stopped, and
pulled the next mob from there. On the live level 6-9 pool
(`test/stage7` `9cd56ea`, 04:00-07:06Z, 3,153 deaths) 38% of deaths ended with
the killer already under half health and another 32% with the killer untouched
at 100% HP (an add), i.e. fights a fuller starting bar decides; the deaths
clustered on quest-objective camps at the bot's own level (46% on
kill-objective travel, killer below the bot in 37% of deaths, above in 39%).

Local validation: module build via `build-commit.sh` (no deploy); `bash
tools/verify_all.sh`; `git diff --check`. Live indicators to watch after
deploy: the share of `Food(24005)` casts per bot-hour (should rise), the
`tortoisebots_state_ratio{state="resting"}` fraction, deaths per bot-hour in
the level 6-10 bracket, and the share of deaths whose killer is under 50% HP
(the class a fuller bar should convert).

Measured but not implemented in this bundle (for the record): the ~30% of
deaths whose killer the bot never damaged (adds/social pulls) is a pull-
discipline question — a per-candidate "another hostile within assist range"
scan on the grind pick — and the 17% of deaths to a killer two or more levels
above the bot is largely the same add; both need a design that keeps the pick
scan off the per-tick world (the module's performance rule), so they are left
for a follow-up rather than bolted onto the destination gate.

## Grind grey hard-skip for autonomous pool bots (issue #396 part A) — 2026-10-02

Feature: `GrindTargetValue` refuses grey (no-XP) creatures outright for bots
with no real-player master, instead of the old 50/51 probabilistic lean.
Self-defence still applies (the attackers loop returns before this branch),
quest-objective kills stay allowed (`needForQuest` branch), and bots with a
real-player master (owned/hired/followed) plus battlegrounds keep the old
lean, so explicit orders are unchanged.

Source project: `mod-playerbots`
`src/Ai/Base/Value/GrindTargetValue.cpp:74`
(`if (!bot->isHonorOrXPTarget(unit)) continue;` — unconditional donor gate) @
`b6696bdbd3740e575598d167d69f39f68cc0b907`, adapted with the master/BG
exemptions the donor does not need (its equivalent gate is unconditional).

Source files: donor `GrindTargetValue.cpp:74`; core grey rule
`tortoise-wow` `src/game/Objects/Player.cpp:21934-21952`
(`IsHonorOrXPTarget`), `src/game/Formulas.h:34-42` (`GetGrayLevel`).

Ported / reimplemented: `ai/playerbot/strategy/values/GrindTargetValue.cpp`
grey branch — masterless non-BG bots `continue` on
`!bot->IsHonorOrXPTarget(unit)` with a distinct grind-log reason
("ignored (grey, no xp)."); text in
`docs/guides/living-world.md` (Grinding & Combat row).

Reason: live `bot_events.csv` (2026-10-02, 25,180 rows): 3,398
`AttackAnythingAction` orders, 584 (17.2%) on grey targets per
`GetGrayLevel`, from 112 of 400 ordering bots — e.g. a level-7 rogue in
Northshire ordering level-1/2 wolves. The pre-existing 50/51 skip let
roughly half of every grey candidate through.

Local validation: `bash tools/verify_all.sh`; `git diff --check`; module
build via `build-commit.sh` (no deploy).

## Neutral starter wildlife as grind prey (issue #393) — 2026-10-02

Feature: `GrindTravelDestination::IsActive` no longer requires the prey entry
to be hostile to the bot. Hostile entries stay prey; friendly entries never
are; neutral entries are prey when they are XP-paying wildlife (no NPC flag,
non-zero `xp_multiplier`). The rule is `GrindHostilityAllowed` in
`ai/playerbot/GrindSpotPolicy.h`, next to the other prey rules.

Source project: `mod-playerbots`
`src/Ai/Base/Value/GrindTargetValue.cpp:66-74` (loot-carrying neutrals are
kept — `lootid` + reaction gate — only non-hostile NPCs are refused) @
`b6696bdbd3740e575598d167d69f39f68cc0b907`, adapted from the live-target
filter to the destination gate (static `CreatureInfo`: `npc_flags`,
`xp_multiplier`).

Source files: donor `GrindTargetValue.cpp:66-74`; core reaction
`tortoise-wow` `src/game/Objects/Object.cpp:3526-3566`
(`GetFactionReactionTo`), faction data `tw_world.faction_template`
(ids 115/189/7), `src/game/Objects/Creature.h:191-243` (`CreatureInfo`).

Reason: live `bot_events.csv` (2026-10-02-1540, 235,780 rows): Teldrassil
level-2 bots log ~16 throttled `QuestTripNoTarget` rows each and never hold
a grind destination, while the same-zone starter beasts (Young Thistle Boar
faction 189, Young Nightsaber faction 7 — both REP_NEUTRAL against a player
faction template) are the only prey in range. The #397 beginner repark only
shortened the quest park; the grind search still came back empty because
`IsActive` rejected every neutral entry, so the bot re-searched quests every
minute instead of walking 200 yd to its wolves.

Local validation: `tools/test_grind_hostility_policy.cpp`
(5 groups: neutral wildlife in, hostile always in, friendly out, neutral
NPCs out, neutral no-XP out); `bash tools/verify_all.sh`;
`git diff --check`; no deploy (orchestrator compiles).
## Pool bots buy vendor weapon upgrades with their own gold (feat/vendor-weapons) — 2026-10-02

Feature: a masterless pool bot standing at a vendor during an existing errand
(rpg buy trigger / nearby-service sell visit) buys at most one weapon per
visit when that weapon is a real upgrade — spec-allowed type, usable now
(`CanUseItem`), better by the module's own scoring (`QueryItemUsageForEquip`
EQUIP, the same answer the equip audit uses) — and affordable out of its own
purse with the next trainer ranks kept funded (`VendorWeaponUpgradeAffordable`
against total money needed for spells). Bought weapons equip via the existing
`equip upgrades` path. No free gear, no gold injection, no extra shopping
trips, no player-character behavior change (owned/mastered bots untouched).

Source project: `mod-playerbots`
`src/Ai/Base/Actions/BuyAction.cpp` (vendor loop sorted by score, usage →
`NeedMoneyFor::gear` afford check, `equip upgrades` after EQUIP buys) @
`b6696bdbd3740e575598d167d69f39f68cc0b907`, adapted: donor checks
`AI_VALUE2(ItemUsage, "item usage")` EQUIP directly, but our classifier only
scores items seen in bags — vendor stock the bot does not own answers NONE —
so the fallback re-scores unseen weapons via `QueryItemUsageForEquip`
(`RandomBotFacade::CanEquipUnseenItem` slot probe) gated by the pure
tool/ammo filter and the trainer-first affordability rule; donor sorts by
stat score, ours keeps the existing ItemLevel order and equips one per visit.

Source files: donor `BuyAction.cpp`; `BudgetValues.cpp:170-171`
(`NeedMoneyFor::gear = level^3`) and `BudgetValues.h:69-70` (reserve order —
our pool inherits donor reserve semantics: spells rank above gear).

Ported / reimplemented: `ai/playerbot/strategy/actions/BuyAction.cpp`
(weapon-upgrade fallback buy + `equip upgrades`), `ai/playerbot/strategy/values/VendorValues.cpp`
(`vendor has useful item` upgrade arm so the rpg buy trigger fires),
`ai/playerbot/strategy/values/VendorWeaponUpgradePolicy.h` (pure
candidate/affordability rules), `tools/test_vendor_weapon_upgrade_policy.cpp`
(standalone g++ test, registered in `tools/verify_all.sh`).

Reason: live pool 2026-10-02-1540 froze 60/67 rogues on the starter Worn
Dagger (see snapshot `a-gear-bags.md` §1-2: loot never offers a spec-legal
sword/mace/dagger, while vendors sell Shortsword 54c / Stiletto 401c /
Gladius 536c — all within a level-10 purse once trainer ranks are reserved).

Local validation: `bash tools/verify_all.sh` (OKF, surface, wiring
`queued=1542 live-missing=0`, policy tests incl. new weapon test, decision
trail); `git diff --check`. Module build + runtime deploy left to the
orchestrator (worktree rule: no docker builds here).

## Pull-regen gate: wounded pool bots sit out the next pull (m-deaths fix A) — 2026-10-03
Feature: `AttackAnythingAction::isUseful` refuses a NEW grind pull while a
masterless pool bot is below `AiPlayerbot.MediumHealth` (mana users:
`AiPlayerbot.MediumMana` too), using existing thresholds. The gate only runs
with an empty core `GetAttackers()` set, so revenge (the
`GrindTargetValue` `possible attack targets` loop) is always answered;
owned bots, real-master bots and battlegrounds keep today's behaviour. The
pre-emptive "attack before being attacked" strike only starts a fresh pull
with no `possible adds` and inside the same grind level cap
(`PullGrindLevelCap`: +1 below 10, +4 from 10); a mob already fighting the
bot (victim set) is always answered. The level numbers live in
`ai/playerbot/PullRegenPolicy.h` (`PullGrindLevelCap`,
`ShouldDeferGrindPull`, `AllowPreemptiveStrike`), and
`MaxGrindLevelOverBot` now delegates to the policy so the cap stays
testable. Doc row: `docs/concepts/bot-mechanics-and-quirks.md`
(Grind Target mob pick).

Source project: `mod-playerbots` `src/Ai/Base/Actions/ChooseTargetActions.cpp`
(`AttackAnythingAction::isUseful`, no regen term) and
`src/Ai/Base/Value/GrindTargetValue.cpp` (no health filter) — verified
absent in the local checkout, so there is nothing to port; both rules are
local, measured on the level 1-12 pool (night2-4h: 6492 deaths, findings
1/3/7 in `m-deaths.md`).

Source files: `ai/playerbot/strategy/actions/ChooseTargetActions.cpp`,
`ai/playerbot/strategy/values/GrindTargetValue.cpp`,
`ai/playerbot/PullRegenPolicy.h`,
`tools/test_pull_regen_policy.cpp`.

Copied / ported / reimplemented: reimplemented (local rule, donor parity
checked, not copied).

Reason: 33.3% of pool deaths land <= 60 s after the victim's previous kill
(median 31 s between attack orders) with no HP/mana check before the next
pull; 37.0% of sub-10 deaths are by mobs above the +1 cap; the line-61
strike fired while travelling with no pack or cap check. Est. 20-30% +
5-10% of deaths saved.

Local validation: `tools/test_pull_regen_policy.cpp` (regen bands, cap,
strike gate); `bash tools/verify_all.sh`; `git diff --check`. No deploy
(orchestrator compiles).
## Death fix B: rest to almost-full, pool-only critical flee, attacker snapshot, never-empty rations — 2026-10-03

Feature: (1) cheat bots (free rations) eat to `AlmostFullHealth` and drink to
almost-full instead of stopping at 70/85 — `UseFoodStrategy` registers the
critical/low/medium/almost-full bands and `ShouldEatValue`/`ShouldDrinkValue`
stop there (start threshold unchanged; no-cheat bots keep 50/85). (2) The
donor `critical health -> flee` node is restored pool-only via a new
`critical health no master` trigger (no real player master, never PvP), and
grouped pool bots now carry the `flee` strategy too (the trigger itself keeps
owned/hired dungeon/raid bots still). (3) `deaths.csv` `adds` reads a live
per-tick attacker snapshot (30 s window, capped at 8, killer excluded by
name) instead of the victim-filtered loop that was structurally always 0.
(4) Every pool bot holds one full stack of the best vendor food for its
level (and water for mana users; warrior/rogue get none), refilled every
tick like ammo, stale tiers swapped on ding; owned/hired bots without the
cheat keep the earned restock path. Food/drink already classify KEEP, so
sell and smart-destroy never hand them over (verified by code read, no
change needed). Pure rules live in `ai/playerbot/SurvivePolicy.h`, pinned by
`tools/test_survive_policy.cpp` (registered in `tools/verify_all.sh`).

Source repository: `mod-playerbots` @
`b6696bdbd3740e575598d167d69f39f68cc0b907` (local checkout
`../playerbots-references/mod-playerbots`).

Source files (donor, reference only):
`src/Ai/Base/Strategy/FleeStrategy.cpp:17` (`critical health` -> `flee` at
`ACTION_MEDIUM_HEAL`), `src/Ai/Base/Strategy/UseFoodStrategy.cpp:11-24`
(food-cheat `medium health` -> `food`, `high mana` -> `drink`),
`src/Ai/Base/Trigger/GenericTriggers.cpp:112-157` (Panic = critical AND
low/no mana; OutNumbered with no pre-10 hunter/mage/druid or solo-priest
gate — ours adds those level gates natively, left untouched),
`src/Ai/Base/Trigger/HealthTriggers.h` (`MediumHealthTrigger` spans
`[0, MediumHealth)`), `src/Ai/Base/Actions/NonCombatActions.cpp`
(`EatAction`/`DrinkAction` cheat branches, `isUseful` at `< 100`).

Copied / ported / reimplemented: flee node ported with a local adaptation —
the donor fires `critical health` for every bot, ours fires a new
`critical health no master` trigger so real-master (owned/hired, dungeon,
raid) bots never flee on their own; the rest stop deliberately exceeds donor
parity (90 not 70) on the measured near-won-fight evidence; the attacker
snapshot, ration seeding/refill and policy header are native (no donor
equivalent).

Reason: night2-4h pool, 6492 deaths at 27/min: 29.6% end with the killer
under 30% HP (a fuller bar flips them), chain-pull with no regen gate, no
low-HP escape for the classes that die most, an always-0 `adds` column, and
level-1 food carried for levels.

Local validation: `bash tools/verify_all.sh` (OKF, surface, wiring
`queued=1543 live-missing=0`, policy tests incl. new survive test, decision
trail); `git diff --check`. Module build + runtime deploy left to the
orchestrator (worktree rule: no docker builds here). Live indicators to
watch after deploy: `Food(24005)` casts per bot-hour (should rise),
deaths per bot-hour in the level 5-10 bracket, share of deaths with killer
under 30% HP, nonzero `adds` counts in `deaths.csv`, and `flee` rows in the
decision trail for critical-HP pool bots.

## Quest destinations keep the sub-10 grind cap (q-level) — 2026-10-03
Feature: `QuestObjectiveTravelDestination::IsPossible` refuses a creature
past the pool grind cap (`QuestObjectiveLevelFits` in
`ai/playerbot/PullRegenPolicy.h`: +1 below 10, +4 from 10, same numbers as
`PullGrindLevelCap`; vendors exempt, owned/hired bots exempt), so an
over-level alternative drop of the same quest item never becomes a
destination — the capped search comes back empty and the caller parks the
purpose like any other empty search. `GrindTargetValue` closes the same
hole on arrival: below 10 with no real player master the quest-mob
exemption no longer applies, so a bot that walks into a mixed field never
orders the level 5-6 neighbours. Doc rows:
`docs/concepts/bot-mechanics-and-quirks.md` (Grind Target destination band
+ mob pick).

Source project: `mod-playerbots`
`src/Mgr/Travel/TravelMgr.cpp:1219-1233`
(`QuestObjectiveTravelDestination::isActive`: quest-level window + `+4` mob
check) — same shape, ported at the pool's +1 number below 10; donor
`src/Ai/Base/Value/GrindTargetValue.cpp` has no quest exemption at all (its
`needForQuest` only widens the pick to quest mobs), so the local exemption
is narrowed, not copied.

Source files: donor `TravelMgr.cpp:1219-1233`, `GrindTargetValue.cpp`;
local `ai/playerbot/TravelMgr.cpp:333-344`,
`ai/playerbot/strategy/values/GrindTargetValue.cpp:75-92`,
`ai/playerbot/PullRegenPolicy.h` (`QuestObjectiveLevelFits`),
`tools/test_quest_objective_level_policy.cpp`.

Copied / ported / reimplemented: reimplemented (donor shape, local numbers).

Reason: live pool 2026-10-03 (fresh level-1 pool, 1277 deaths since 09:11
UTC): 798 (62.5%) are bots level 1-4 killed by mobs 2+ levels above —
Defias Cutpurse 188, Mangy Wolf 150, Forest Spider 63, Tirisfal Plagued
Bear 63, Ravaged Corpse 64. 199 of those die on a `loot item 750` trip
(Tough Wolf Meat, entry 69, level_max 2) killed by the level 5-6
neighbours sharing the field; 302 die on giver/taker trips through the
same fields. Quest-giver/taker routing itself was checked and left alone:
takers already get the sub-10 route walk (`checkTakerRoute` in
`SetBestTarget`), both keep the +5 area band, and the traced deaths are
walk-through kills on the way, not bad addresses — the capped objectives
plus the arrival-side order cap are the fix that reaches them.

Local validation: `tools/test_quest_objective_level_policy.cpp` (cap,
travelling ceiling, +4 from 10, vendor/owned exemptions; registered in
`tools/verify_all.sh`); `bash tools/verify_all.sh`; `git diff --check`.
No deploy (orchestrator compiles).

## Spell-rank gate + instance AoE-fear ban (issues #381, #383) — 2026-10-03

Feature: bots only learn spell ranks at or below their level (taught spell's
own `spellLevel`, not the trainer row's `reqLevel`), across the factory seed,
the pool auto-learn, and the paid trainer visit; hire provisioning prunes
over-level ranks on the downgrade path and re-teaches the highest allowed
rank. AoE fear (priest Psychic Scream, warlock Howl of Terror, warrior
Intimidating Shout) never fires inside a dungeon/raid or for a bot grouped
with a real player: the scream trigger moves to the PvP-only cc kit (Howl
already lives there) and all three actions carry the instance/master gate.

Source repository: `playerbots-references/mod-playerbots` @ `b6696bdb`
(gameplay donor: fear-on-mark only, no automatic pack fear; Howl of Terror
kept out of the PvE cc kit — same shape, ported to the local `cc`/`cc pvp`
split; no donor code copied).

Source files:
- `ai/playerbot/SpellRankPolicy.h` (`SpellRankTeachableNow`,
  `SpellOverLevelForBot`), `tools/test_spell_rank_policy.cpp`
- `ai/playerbot/AoeFearPolicy.h` (`AoeFearAllowed`),
  `tools/test_aoe_fear_policy.cpp`
- `ai/playerbot/PlayerbotFactory.cpp` (`InitClassLevelSpells` rank gate,
  `PruneOverLevelSpellRanks`), `ai/playerbot/PlayerbotFactory.h`
- `ai/playerbot/strategy/actions/AutoLearnSpellAction.cpp`
  (`LearnSpellFromSpell` rank gate),
  `ai/playerbot/strategy/actions/TrainerAction.cpp` (`Learn` rank gate)
- `ai/playerbot/strategy/priest/PriestStrategy.cpp` (scream to `cc pvp`),
  `ai/playerbot/strategy/priest/PriestActions.h` (scream action gate),
  `ai/playerbot/strategy/warlock/WarlockActions.h` (howl action gate),
  `ai/playerbot/strategy/warrior/WarriorActions.h` (shout action gate)
- `docs/classes/priest.md`, `docs/classes/warrior.md`,
  `docs/classes/warlock.md`

Copied / ported / independently reimplemented: reimplemented (donor
behaviour, local code).

Reason: issue #381 — a hired level-16 priest carried Fortitude rank 3
(level 24); trainer rows with a lower reqLevel than the taught spell plus
the downgrade path that skips the trainer gate made over-rank books
possible on every class. Issue #383 — a hired holy priest cast Psychic
Scream three times in RFC and pulled extra packs.

Local validation: `tools/test_spell_rank_policy.cpp` + new
`tools/test_aoe_fear_policy.cpp` (both registered in
`tools/verify_all.sh`); `bash tools/verify_all.sh`; `git diff --check`.
No deploy (orchestrator compiles).
| Rogue no-enchant triggers + open-world poison upkeep | mod-playerbots RogueTriggers.h MainHand/OffHandWeaponNoEnchantTrigger, RogueAiObjectContext creators, GenericRogueNonCombatStrategy + GenericRogueStrategy wiring | ai/playerbot/strategy/rogue/RogueTriggers.{h,cpp} (classes + IsActive), RogueAiObjectContext.cpp (creators), RogueStrategy.cpp (upkeep on always-on RogueStrategy::InitNonCombatTriggers; spec pve/pvp/raid poison strategies unchanged) | Reimplemented: donor names shape, Tortoise per-poison apply actions kept | Generic upkeep was dead (unregistered names skipped by Engine::ProcessTriggers); open-world levelers never poisoned | verify_action_trigger_wiring.py (0 live-missing), test_class_consumable_policy.cpp, verify_all.sh |
| Organic warlock shard economy (Drain Soul harvest, no seeding/conjuring) | mod-playerbots CastDrainSoulAction::isUseful (<26 shards), OutOf==0/TooMany>=26 triggers (no cheat gate), core SpellAuras (shard only when drained victim yields XP/honor) | ai/playerbot/strategy/warlock/WarlockTriggers.{h,cpp} (cheat gates dropped; drain window 25->20 matching target critical health), WarlockActions.h (Create Soul Shard action disabled - DB-only spells 23464/24827; cheat shard-destroy in Shadowburn/Dark Harvest removed), PlayerbotFactory.cpp (InitReagents warlock seed capped at 5-keep band), runtime/ClassConsumablePolicy.h | Reimplemented: donor real-shard model kept under the pool item cheat instead of donor conjure | Pool warlocks held zero shards (all upkeep paths false under cheat); hired/owned unchanged - same real-shard path | test_class_consumable_policy.cpp, verify_all.sh |
| Class-consumable ding refresh + sell/destroy guards | mod-playerbots PlayerbotFactory rogue seed + AutoMaintenanceOnLevelupAction ding refresh | XpGainAction.cpp + AutoLearnSpellAction.cpp (pool-only AddConsumes + AddBandages, idempotent), ItemUsageValue.cpp (KEEP for masked poisons + upkeep stones/oils; dead class-ID-vs-mask block removed), PlayerbotFactory.h (coarse weightstone 3239->3240) | Reimplemented | Ding left real-item bots on stale tiers; zero live stock drained via vendor/destroy | test_class_consumable_policy.cpp, verify_all.sh |
| Combat spread + flee memory (issue #470) | mod-playerbots MovementAction::CheckLastFlee/FleePosition/BestPosition* (src/Ai/Base/Actions/MovementActions.cpp), CombatFormationMoveAction::Execute + DisperseSetAction/DisperseDistanceValue (default -1 disabled), MoveFromGroupStrategy/MoveFromGroupAction (default 20yd), RecentlyFleeInfo/LastFleeAngle/LastFleeTimestamp values (src/Bot/Engine/Value/Value.h) @ b6696bdb | ai/playerbot/CombatSpreadPolicy.h (veto math + spread gate, tested), strategy/values/LastMovementValue.h (last two flee headings), FleeManager.cpp (ring-spoke veto), strategy/actions/MovementActions.cpp (record heading on flee dispatch), strategy/actions/DungeonActions.cpp (RaidSpreadAction veto + fallback headings), strategy/triggers/DungeonTriggers.cpp (RaidSpreadNeededTrigger combat/master/hold gate) | Reimplemented: donor opt-in disperse knob replaced by combat+pool+no-hold trigger gate; donor FleeInfo list replaced by two LastMovement slots; melee/tank stacking unchanged | tools/test_combat_spread_policy.cpp, verify_all.sh |
| Failure-only flee/spread memory (issue #486) | Existing module behavior above, originating in mod-playerbots @ b6696bdb; outcome tracking is original module logic | ai/playerbot/CombatSpreadPolicy.h (bounded anchor/map/spline-scoped observation and expiry), strategy/values/LastMovementValue.h (separate flee/spread caches), PlayerbotAI.cpp (observe actual separation and lifecycle clears), FleeManager.cpp (prefer nonfailed points with fallback), strategy/actions/MovementActions.cpp and DungeonActions.cpp (start observations on dispatch) | Reimplemented correction: successful headings never become vetoes; failed heading requires observed no separation gain, expires quickly, and never blocks all escapes. Core chase mode and corpse-reclaim callers retain their existing behavior; no additional donor code imported | tools/test_combat_spread_policy.cpp, tools/test_flee_selection.py, full core/module compile; runtime comparison pending |
## Periodic quest-log triage for pool bots (E07) — 2026-10-03
Feature: upkeep bots drop FAILED quests and, once fewer than two log slots
are free, unfinishable solo picks — over-level (+3), elite/dungeon/raid
(type != 0), suggested-group (>= 2) and zone-mismatched quests — instead of
pinning slots (`QuestTriageShouldDrop` in `ai/playerbot/QuestLogPolicy.h`,
wired into `CleanQuestLogAction::IsDroppable` and mirrored in the
`QuestLogNearlyFullTrigger` pre-scan, which now also sees FAILED). COMPLETE
quests never triage; class quests stay preserved; no whole-log last resort.
Repeatable/seasonal drops not ported (no 1.12 seasonal API).

Source project: `mod-playerbots`
`src/Ai/World/Rpg/Action/NewRpgBaseAction.cpp:556-590`
(`IsQuestWorthDoing`, `IsQuestCapableDoing`, `OrganizeQuestLog` at `:590`,
`:614-616` FAILED drop, `:640-665` zone pass, `freeSlotNum >= 2` gate).

Source files: donor `NewRpgBaseAction.cpp:556-690`;
local `ai/playerbot/QuestLogPolicy.h` (`QuestTriageShouldDrop`),
`ai/playerbot/strategy/actions/DropQuestAction.{h,cpp}`,
`ai/playerbot/strategy/triggers/GenericTriggers.h`,
`tools/test_quest_log_triage_policy.cpp`.

Copied / ported / reimplemented: reimplemented (donor numbers, local
plumbing; donor reward-pick `BestRewardIndex` not ported).

Reason: accept-time gating only (`AcceptQuestAction::WouldAcceptQuest`) lets
the log clog with quests the bot can never finish; E07 rates the impact
MEDIUM (blocked slots). Donor defaults OFF for repeatables/seasonal kept
out per brief.

Local validation: `tools/test_quest_log_triage_policy.cpp` (FAILED,
+3/scaling, type, suggested, zone/sort/unknown, doable kept; registered in
`tools/verify_all.sh`); `bash tools/verify_all.sh`; `git diff --check`.
No deploy (orchestrator compiles).

## Loot-roll vote gates for pool bots (E08) — 2026-10-03
Feature: plain auto votes pass through three donor gates before the
`IsLootAllowed` verdict — FFA/master-loot rolls PASS, recipes NEED when
learnable (SKILL usage) / PASS when soulbound-unusable / GREED when
tradeable, epic junk-class tokens NEED only for classes in their
`AllowableClass` mask (empty mask = unrestricted), duplicate uniques
(already `UNIQUE_EQUIPPED`, or at `MaxCount`) demote NEED to GREED
(`LootMethodTakesRolls`, `RecipeRollVote`, `TokenUsableByClass`,
`UniqueCopyOwned` in `ai/playerbot/LootRollPolicy.h`, wired into
`RollAction::CalculateRollVote`). Recipe/token gates are pool-only
(`!HasRealPlayerMaster`); FORCE_NEED/FORCE_GREED and the bad-equip rule
win. Deliberate 1.12 divergence: the core has no DISENCHANT vote
(`CountRollVote` only records NEED/GREED/PASS), so DISENCHANT usage stays
GREED and enchanters disenchant post-win via maintenance.

Source project: `mod-playerbots`
`src/Ai/Base/Actions/LootRollAction.cpp:40-110`
(vote gates, `lootNeedRollLevel`/`lootRollRecipe`/`lootRollDisenchant`/
`lootGreedRollLevel` defaults `:707-710` in `PlayerbotAIConfig.cpp`),
`LootRollAction.h:29-30` (`CanBotUseToken`, `RollUniqueCheck` at `:185`,
`:197`).

Source files: donor `LootRollAction.cpp:40-210`, `LootRollAction.h:29-30`;
local `ai/playerbot/LootRollPolicy.h`,
`ai/playerbot/strategy/actions/LootRollAction.cpp`,
`tools/test_loot_roll_policy.cpp`.

Copied / ported / reimplemented: reimplemented (donor gates, local vote
plumbing; no new config keys — donor recipe/DE/greed toggles default off
and are folded into the pool-only scope instead).

Reason: E08 rates the gap MEDIUM — pool bots GREEDed soulbound recipes
they cannot learn, NEEDed duplicate uniques they cannot loot, and voted in
FFA/master rolls. No DE-skill config existed, so no new key was added.

Local validation: `tools/test_loot_roll_policy.cpp` (loot method,
recipe split, unique ownership, token mask; registered in
`tools/verify_all.sh`); `bash tools/verify_all.sh`; `git diff --check`.
No deploy (orchestrator compiles).
## 2026-10-03 — pet upkeep + gather tools + medium-mana potion (r-donor E01/E02/E10/E11)

Donor: `mod-playerbots` @ b6696bdbd3740e575598d167d69f39f68cc0b907.

Source files: donor `src/Ai/Base/Actions/PetsAction.cpp:342-405`
(`TogglePetSpellAutoCastAction`), `:442-514` (`SetPetStanceAction`),
`src/Ai/Base/Trigger/GenericTriggers.cpp:48-53,743-764`
(`HasPetTrigger`, `NewPetTrigger`), `src/Mgr/Item/LootObjectStack.cpp:337-343`
(tool allowlist), `src/Ai/Base/Strategy/UsePotionsStrategy.cpp:35-38`
(medium-mana node), `src/Bot/Factory/PlayerbotFactory.cpp:1304+`
(`InitPetTalents`); local `ai/playerbot/strategy/actions/GenericActions.{h,cpp}`
(new actions), `ai/playerbot/strategy/triggers/GenericTriggers.{h,cpp}` +
`TriggerContext.h` (triggers + creators), `ActionContext.h` (action creators),
`ai/playerbot/strategy/hunter/HunterStrategy.cpp` +
`ai/playerbot/strategy/warlock/WarlockStrategy.cpp` (live upkeep nodes),
`ai/playerbot/LootObjectStack.cpp` (allowlist), `runtime/PetUpkeepPolicy.h`
(denylist + toggle rule, pinned by `tools/test_pet_upkeep_policy.cpp`),
`runtime/GatherToolPolicy.h` (tool lists, pinned by
`tools/test_gather_tool_policy.cpp`),
`ai/playerbot/strategy/generic/UsePotionsStrategy.cpp` (medium-mana node),
`docs/classes/hunter.md`, `docs/classes/warlock.md`.

Copied / ported / reimplemented: ported with 1.12 adaptations — autocastable
= non-passive (no NO_AUTOCAST_AI bit in this core; `Pet::ToggleAutocast`
refuses passives the same way), stale-entry prune via `Pet::HasSpell`
(already excludes PETSPELL_REMOVED), denylist = the 1.12-existant subset
(WotLK-only Spell Lock 27276/27277 ranks, Leap 47482/58867, 48011 visual
excluded; all Cower ranks 1742/1753-1756/16697 disabled to agree with the
factory), stance = REACT_DEFENSIVE always (donor DefaultPetStance collapsed
to our InitPet/CanPetAttack invariant), guardian coverage via
`CallForAllControlledUnits(CONTROLLED_GUARDIANS)` (no m_Controlled in this
core), tool allowlist minus WotLK-only 40772/40892/40893.

Reason: the live hunter "pet" and warlock "pet" strategies queued
`toggle pet spell` / `set pet stance` with no creators (silent no-ops every
tick); bots carrying any non-default valid tool refused nodes; casters
waited for low mana before potion logic armed.

Local validation: `tools/test_pet_upkeep_policy.cpp`,
`tools/test_gather_tool_policy.cpp` (registered in `tools/verify_all.sh`);
`bash tools/verify_all.sh` (incl. wiring gate: live-missing 0);
`git diff --check`. No deploy (orchestrator compiles).

E10 verdict (pet talents): NOT APPLICABLE, skipped. Donor `InitPetTalents`
spends WotLK pet talent points (`GetMaxTalentPointsForLevel`,
`petTalentType`, `TalentEntry`/`TalentTab` pet masks) — none of those APIs
exist in this core (grep verified: no `GetMaxTalentPointsForLevel`,
`petTalentType`, or pet-talent store outside player talents). 1.12 pets use
training points (`Pet::m_TrainingPoints`, `GetTPForSpell`), and
`InitPetSpells` already teaches level-appropriate spells plus autocast
state. Nothing to port.
## Quest/grind points refuse hostile over-cap neighbours (q-points) — 2026-10-03
Feature: `TravelMgr::IsLocationLevelValid` refuses a quest-objective /
quest-loot / grind *point* whose 40 yd surroundings hold hostile spawns past
the bot's grind cap (`PointDangerApplies` + `PointDangerous` in
`ai/playerbot/PointDangerPolicy.h`: pool bots below 10 only, +1 numbers from
`PullGrindLevelCap`; owned/hired bots and level 10+ keep today's behaviour).
The neighbour lookup is a static per-map 32 yd cell index over the creature
spawn table (`WorldPosition::GetHighestHostileLevelNear`, built once via
`call_once` next to the hostile-town index: no world scan, no DB, no map
loads, no per-query allocation) counting only spawns the bot is hostile to
by static template reaction, so neutral camps, vendors and wildlife never
bar a point. When every point of a destination is dangerous the search comes
back empty and the caller parks the purpose like any other empty search.
Doc row: `docs/concepts/bot-mechanics-and-quirks.md` (Grind Target
destination band).

Source project: `mod-playerbots` — no donor shape: its `TravelMgr` never
looks at neighbouring spawns (`getCreaturesNear` only builds the destination
and node tables), so this is local, measured on the live pool.

Source files: local `ai/playerbot/PointDangerPolicy.h` (new),
`ai/playerbot/WorldPosition.h` + `WorldPosition.cpp` (danger-spawn index),
`ai/playerbot/TravelMgr.cpp` (`IsLocationLevelValid` gate),
`tools/test_point_danger_policy.cpp`.

Copied / ported / reimplemented: reimplemented (local rule, local numbers).

Reason: live pool 2026-10-03 (fresh level-1 pool since 10:51 UTC, server on
#418): 211 of 523 deaths are bots level 1-4 killed by mobs 2+ above —
#418 keeps the spawn entry itself in cap but says nothing about the point's
surroundings. Worst case: level-4 Kralnyrvar on the item-750 trip (Timber
Wolf entry 69, level_max 2, in cap) picks a Timber Wolf spawn point at
POINT(-73.97 -9254.43) outside Northshire and dies five times to the Defias
Cutpurse 5 / Forest Spider 6 / Mangy Wolf 6 standing next to it (spawn
table: Forest Spider 9 yd, Mangy Wolf 33 yd, Defias Cutpurse 38 yd away).
125 of the 211 die on the same `loot item 750` trip.

Local validation: `tools/test_point_danger_policy.cpp` (scope, live
level-4 case, travelling +1 ceiling; registered in `tools/verify_all.sh`);
`bash tools/verify_all.sh`; `git diff --check`. No deploy (orchestrator
compiles).
## Goal-directed flight transport for pool bots (issue #426) — 2026-10-03
Feature: a pool bot with a far travel target boards a flight TOWARD it
(`DecideFlightPlanForTarget` + `TryBoardFlightToTarget` in
`ai/playerbot/strategy/actions/MoveToTravelTargetAction.cpp`, pure rules in
`ai/playerbot/FlightErrandPolicy.h`): decided ONCE when the travel target
is set and stored on manual values (`flight from/to node`); travel ticks
only read the stored plan, never recompute. A KNOWN direct taxi hop from
the nearest flight master to the node nearest the destination, level-valid
(area at most +5 above the bot, unknown levels FAIL CLOSED, outgrown floor
−10 with a capital exemption, never capital-to-capital), on a trip >= 1500
yd that saves >= 500 yd of walking, affordable from the bot's own gold
above the class-trainer reserve. With a plan the bot walks to the flight
master as its intermediate move target until inside interaction range,
then boards; the bot unmounts, drops shapeshift and pays the normal fare
— no money injection.
Pool randoms only (no real master); owned/hired bots keep walking with
their player. No overlap with the zone-migration work (`fix/zone-migration`
touches pick radius and valley gates only, no flight logic): this fires
after the pick, inside the existing travel walk, and adds no new route
logic — the travel-node graph (with its flight legs) is untouched. Every
takeoff writes a `TaxiFlight` row to bot_events.csv (from → to node names).
The in-flight watch stays the core's: cross-map legs finish in
`TaxiStepFinished` (Player.cpp) and the movement/AI layers already stand
down while `IsTaxiFlying()` holds. No vmap load on this path: the node-zone cache (`LoadTaxiNodeZones`,
built once at startup from loaded terrain) maps DBC nodes to zones, and
`TryGetValidatedAreaLevel` supplies levels — both startup caches, read
once per travel target, never per tick.

Source project: `mod-playerbots` @ b6696bdbd3740e575598d167d69f39f68cc0b907.
Donor `src/Ai/World/Rpg/Action/NewRpgBaseAction.cpp:1065-1081`
(`SelectRandomFlightTaxiNode`), `:1215-1225` (availability gate),
`src/Mgr/Travel/TravelMgr.cpp:4405-4477` (`GetOptimalFlightDestinations`:
500 yd nearest-FM, level-bracket zones, no capital-to-capital shuffle),
`src/Ai/World/Rpg/Action/NewRpgAction.cpp:635-678` (`NewRpgTravelFlightAction::Execute`).

Source files: donor `NewRpgBaseAction.cpp`, `NewRpgAction.cpp`,
`TravelMgr.cpp` (`GetOptimalFlightDestinations`); local
`ai/playerbot/FlightErrandPolicy.h` (new),
`ai/playerbot/strategy/actions/MoveToTravelTargetAction.cpp`
(`TryBoardFlightToTarget` + `TaxiFlight` event),
`tools/test_flight_errand_policy.cpp`.

Copied / ported / reimplemented: reimplemented (donor node selection and
level-bracket zones as local +5/−10 walk-gate window with fail-closed
unknowns, trip/worth/afford gates; no zone-bracket table or cross-map taxi
resume ported — the core already continues cross-map flights, and no config
keys: donor `RpgStatusProbWeight.TravelFlight` is folded into the travel
walk).

Reason: live night2 pool (4 h): 0 `is flying from` rows in bot_events.csv —
pool bots walk everywhere, including the Teldrassil exit at 11-12 that
motivated the issue. Review revision: the first cut was a random RPG-taxi
errand (fail-open levels, free-fare cheat, per-candidate vmap reads);
rebuilt as transport toward the already-chosen travel target.

Local validation: `tools/test_flight_errand_policy.cpp` (band, capital
shuffle, fail-closed unknowns, leg worth, fare-vs-reserve, levelling shape;
registered in `tools/verify_all.sh`); `bash tools/verify_all.sh`;
`git diff --check`. No deploy (orchestrator compiles).

## Open-water fishing search for pool bots (#402)

Feature: masterless pool bots fish nearby open water when the travel fish
table is empty — visible fishing holes first, otherwise the nearest fishable
shore inside 40 yd with at most a short step to the bank, the cast aimed at
the water, the zone skill-gated like the travel fish errand, guarded shores
refused, failed searches rested a minute. Owned/hired bots never take the
path. No destination is picked and no route is walked, so the travel/level
gates (#418, #428, #434) are not bypassed.

Source project: `mod-playerbots` @ `b6696bd`
(`playerbots-references/mod-playerbots` local checkout).

Source files: `src/Ai/Base/Actions/FishingAction.cpp` (`FindWaterRadial`,
`FindFishingHole`, `HasFishableWaterOrLand`, `FindLandFromPosition` shape,
`MIN/MAX_DISTANCE_TO_WATER`, `SEARCH_INCREMENT`, fishing `FISHING_DISTANCE`
40 yd / `FISHING_DISTANCE_FROM_MASTER` 10 yd, `CanFishValue` swim/combat
exclusion), `src/Ai/Base/Value/FishValues.cpp` (`CanFishValue`,
`CanUseFishingBobberValue`), `conf/playerbots.conf.dist` (fishing distance
comments, incl. "Currently not relevant since masterless bots will not
fish").

Copied / ported / reimplemented: reimplemented — the radial/ring search,
hole scan and shore-stand geometry follow the donor, but liquid comes from
`TerrainInfo::GetWaterLevel` / `getLiquidStatus` (not donor `LiquidData`),
line of sight from `Map::isInLineOfSight`, skill from
`sObjectMgr.GetFishingBaseSkillLevel` with the travel errand's -5 head
start, guard from the existing `IsFishingSpotGuarded`, movement from the
existing `MoveTo` (no donor `MoveNearWater`/`fishing spot` value port).
Donor `master fishing` / `use bobber` / `EquipFishingPole` strategies are
not ported: the bot already owns a pole (factory seed), equips it in
`FishAction`, and opens its own bobber.

Reason: all 500 pool bots know fishing and carry a pole with `tfish` on,
but `FISH_LOCATION_*` is empty and generation is off, so `GetFishSpot`
never returns and the issue measures 0 casts in 2 h 46 min.

Local validation: `tools/test_fishing_spot_policy.cpp` (scope, windows,
cast range, depth, skill gate, dry stand; registered in
`tools/verify_all.sh`); `bash tools/verify_all.sh`;
`python3 tools/verify_action_trigger_wiring.py` (0 missing);
`git diff --check`. No deploy (orchestrator compiles).

### Review fixes (levelling first, fishing as a side activity)

Review verdict on `ac1df6f`: the `qualifier != "travel"` gate made the
fallback dead code for every `tfish` pool bot, the uncapped relevance-10
trigger would stall levelling on first water contact, and the 336-probe
search with a 60 s retry would cost ~117k terrain/raycast queries per
minute. Fixed on this branch:

- Qualifier: the fallback now runs inside the `tfish` (`::travel`) path —
  when the travel fish table yields nothing and the travel target is idle.
- Budget: `FishingSpotPolicy.h` session rules — one session/hour, max 5
  casts / 5 min, wrap-safe `WorldTimer` arithmetic; `CanFishValue`,
  `MoveToFishAction` and `FishAction` all enforce it; `FishStrategy`
  relevance drops to 3/4 (below quest 6.36 / grind 6.35). Never while a
  travel errand, rewardable finished quest, vendor/trainer/money/repair
  need, or >90% bags.
- Cost: 5 yd rings x 8 dirs (~88 probes), 15 s per-bot search throttle,
  15 min per-bot no-water park, shared per-map-cell water verdict cache
  (30 min, mutex-guarded).
- Combat: session ends at once on combat (`FishAction`, `PlayerbotAI`
  wake-up), `equip upgrades` fires immediately at session end, and the
  `DoneFishingValue` 30 s pole delay is skipped in combat.

Local validation: extended `tools/test_fishing_spot_policy.cpp` (session
budget incl. wrap, throttle/cache windows, cell keys); `bash
tools/verify_all.sh` green; `verify_action_trigger_wiring.py` 0 missing;
`git diff --check` clean.
## Organic AH buyer: in-place bids plus spare-gold travel demand (issue #405) — 2026-10-03, review 2
Feature: `AhMarketService::BuyAuctionCandidate` bids only with a pool bot
ALREADY standing at an auctioneer serving the listing's house object (no buyer
teleport per owner decision; teleport stays only the pre-existing stuck rescue
on the way there). The per-candidate scan is one pass with no blind spot
(review finding 6): examine == probe (8), every touched entry fully probed,
`m_buyerScanIndex` rotating start cycles the whole pool over passes. All house
lookups null-checked (finding 2: `GetAuctionHouseEntry` may return null and
`GetAuctionsMap` dereferences its argument; every other pointer on the path -
bot, session, auction, map entry - guarded too). Demand comes from the normal
AH travel purpose (`NeedTravelPurposeValue`, same destination the seller uses,
phase gate evaluated FIRST so the purse/position work only runs in the open
slice): a masterless pool bot level 10+ (finding 3: past the beginner death
belt), never a hire, ungrouped, not LFT/BG/instance, whose own map holds an AH
house (same-continent reachability via the cached entry-guidps map; cross-map
houses are unroutable FLT_MAX and never open the trip), holding 5 gold of
spendable "free money for anything" (finding 4: ONE money rule - exactly the
purse `AhBidAction` reads for AH/VENDOR/QUEST listings on arrival, so no trip
is futile), inside the first 3 minutes of the hourly RPG phase (~5% of the
pool/hour), one trip per bot per 10 minutes (pick-stamped "ah buyer trip
since", same pattern as the trainer/vendor stamps; parked AH purposes
respected so a failed search is not re-requested every tick). No
RESET_AI_VALUE2 anywhere on this path (finding 7): the cached value at its
normal checkInterval is read as-is. The existing destination/point gates
(area-level ceiling, grind cap, point-danger) keep applying on the way - this
policy only opens the door, never overrides a forbidden area. Pure numbers in
`runtime/AhBuyerPolicy.h` (ai namespace). Doc row:
`docs/guides/living-world.md` (Living Auction House Economy).

Source project: no donor shape — `mod-playerbots` has no market buyer (only a
commented-out `AuctionItem` in `LootAction.cpp`); the travel demand reuses our
own AH travel destination.

Source files: `runtime/AhBuyerPolicy.h`,
`runtime/AhMarketService.h` (`SellerIntervalMs` declaration restored - finding
1) + `runtime/AhMarketService.cpp` (in-place house-matched scan only),
`ai/playerbot/strategy/values/MaintenanceValues.h` + `MaintenanceValues.cpp`
(`AhBuyerTripNeeded`), `ai/playerbot/strategy/values/TravelValues.cpp` (AH
purpose buyer leg), `ai/playerbot/strategy/actions/ChooseTravelTargetAction.cpp`
(trip stamp), `docs/guides/living-world.md`,
`tools/test_ah_buyer_policy.cpp`.

Copied / ported / reimplemented: reimplemented (local rule, local numbers).

Reason: issue #405 — 37 listings in 2 h 46 min with 0 bids / 0 purchases and
`AhMarketBuyer = 1`, because pool bots almost never stand at an auctioneer at
low level. First iteration gave the buyer the seller teleport; owner decision
reverses that (NO buyer teleport), so demand now walks instead.

Local validation: `tools/test_ah_buyer_policy.cpp` (scan caps, level floor,
purse floor, phase window, same-continent rule; registered in
## POI-stall quest abandon (i423) — 2026-10-03
Feature: `QuestStallPolicy.h` (new) ports the donor's 5-min no-progress
verdict adapted to our quest-objective travel (1.12 has no POI table to walk:
no `QuestPOIVector` in core, no quest-poi DBC/DB rows). While arrived at
the objective (`TravelAction::Execute`, travel-target status WORK) the quest's
kill/item counters are anchored per quest in the facade store; a WORK tick
with unchanged counters past the 5-min horizon parks that quest's objective
fetch for 30 min (`no quest objective until::<questId>`, read in
`RequestQuestTravelTargetAction::Execute`) and nulls the current target, so
the next search moves on instead of walking useless laps. Travel time never
counts (the anchor is only created and compared while WORK). Any kill/item
progress on any objective re-anchors instead of stalling; explore/event/spell
objectives with no counter requirement are exempt; givers/takers are
unaffected, so parked quests still hand in; the quest stays in the log (no
removal — removal stays the nearly-full-log triage). No global quest-purpose
park: other quests and all hand-ins keep working. Pool upkeep bots only
(`botQuestLogUpkeep`, no active master, `IsRandomBot`); owned/hired bots keep
today's pursuit. Existing gates (#418, #428, #434) untouched: gated
destinations are never picked, so they never anchor. One `bot_events.csv` row
per stall (`QuestObjectiveStalled`); no `TravelTarget` pick log for the
stall-dropped target (the drop returns before the pick logging).

Source project: `mod-playerbots` @ b6696bd (gold-standard behaviour donor).

Source files: `src/Ai/World/Rpg/Action/NewRpgBaseAction.h` (`POIInfo`,
`GetQuestPOIPosAndObjectiveIdx`), `src/Ai/World/Rpg/Action/NewRpgAction.cpp`
(`DoIncompleteQuest`/`DoCompletedQuest`, `poiStayTime = 5 * 60 * 1000`,
`lowPriorityQuest`), `src/Bot/PlayerbotAI.h:607` (`lowPriorityQuest`).

Copied / ported / reimplemented: reimplemented (donor walks POI coordinates
and times from POI arrival; ours anchors arrived travel-pursuit counters per
quest; donor marks a session set, ours parks time-boxed so a later ding
re-tests the quest).

Reason: no POI pursuit existed here (`POIInfo` zero hits), so a quest whose
objective area yields nothing is re-picked every minute (objectives expire
fast) — the search loops and long useless trips in #423.

Local validation: `tools/test_quest_stall_policy.cpp` (donor constants,
first WORK tick, horizon boundary, kill/item progress, zero-progress stall,
per-quest alternating objectives, non-counter exemption, anchor round-trip;
registered in `tools/verify_all.sh`);
`bash tools/verify_all.sh`; `git diff --check`. No deploy (orchestrator
compiles).

## Giver-stall release (night2 idlecheck) — 2026-10-09

Feature: `TravelAction::Execute` expires a quest-giver target on arrival
(status WORK) when the giver NPC is within talk range and its menu holds no
rewardable hand-in and no acceptable quest (`AcceptAllQuestsAction::
OffersAcceptableQuest`, the same predicate the nearby-service rule uses, so
the two cannot drift). Donor `mod-playerbots` invalidates a questgiver
purpose whose arrival yields nothing (validity gates flip false once the
errand is done: `TravelMgr.cpp:1141-1215` questgiver/taker gates, `NewRpg`
watchdog expiry + arrival-noop back to idle); ours held WORK for the full
5-min expiry with the stay-alive conditions still green, so the bot idled at
the NPC on the 0.5-relevance `check values` floor. Takers keep their own
hand-in path (including the stuck-hand-in settle); pool upkeep bots only.
One `bot_events.csv` row per release (`QuestGiverStalled`).

Source project: `mod-playerbots` (gold-standard behaviour donor).

Source files: `src/Mgr/Travel/TravelMgr.cpp:1141-1215` (questgiver/taker
validity gates), `src/Ai/World/Rpg/Action/NewRpgAction.cpp:271-327,383-412`
(watchdog expiry, NPC arrival-noop back to idle).

Copied / ported / reimplemented: reimplemented (donor flips destination
validity; ours expires the arrived target once, the next tick re-picks).

Reason: live 2026-10-08 pool — 115 of 153 still questgiver bots showed no
accept, move-fail or drop after their giver pick; 30 old-idle
check-values/very-often bots split questgiver 10 / grind 8 / none 7 with the
questgiver share arrived-and-exhausted (accepted everything offered, WORK
held to expiry).

Local validation: `bash tools/verify_all.sh`; `git diff --check`. No deploy
(orchestrator compiles).

## Local grind and camp picks (issue #424) — 2026-10-03

Feature: pool bots search the grind errand inside the donor's local window
(2500 yd, 833 below level 5) instead of 10000 yd, and camp (GenericRpg inn
hub) errands inside 500 yd at level <= 5 / 2500 yd above. Owned/hired bots
keep the full radius (their player decides); leave-outgrown-zone grinds keep
it too (a zone exit is far by design). All destination gates stay in force.

Source project: `mod-playerbots` (NewRPG local picks).

Source commit: `mod-playerbots@b6696bdbd3740e575598d167d69f39f68cc0b907`.

Source files: donor
`src/Ai/World/Rpg/Action/NewRpgBaseAction.cpp:964-1062`
(`SelectRandomGrindPos`: same-map/zone level-bucket picks, 500 yd / 2500 yd,
/3 below level 5, 50% near bias; `SelectRandomCampPos`: same-map/zone
inn-hub picks, 500 yd at level <= 5 else 2500 yd, 50 yd push),
`src/Mgr/Travel/TravelMgr.h:880,884` (`GetTravelHubs`, `GetLocsPerLevelCache`),
`src/Mgr/Travel/TravelMgr.cpp:4490` (`GetTravelHubs`),
`src/Mgr/Travel/TravelMgr.cpp:4812-4828` (level-bucketed POI cache); local
`ai/playerbot/LocalPickPolicy.h` (new),
`ai/playerbot/strategy/actions/ChooseTravelTargetAction.cpp`
(`RequestTravelTargetAction::Execute` request radius),
`tools/test_local_pick_policy.cpp`.

Copied / ported / reimplemented: reimplemented the distance windows as a
request-side search radius (one float down the async `GetPartitions` call);
same-map is already covered (`GetDestinations` drops unreachable maps),
level/zone picks are superseded by the stronger local gates (creature band
`GrindSpotPolicy.h`, area ceiling `IsLocationLevelValid` /
`GrindTravelDestination::IsPossible`, point danger `PointDangerPolicy.h`
#418/#428, taker route #434). The 50% near-coin is dominated by our
nearest-partition pick (recorded as `LOCAL_GRIND_NEAR_BIAS_PERCENT`); the
50 yd camp push applies to resting camp status only, not errand trips.

Reason: without the cap a pool bot with nothing suitable nearby walks up to
10000 yd for grind while the donor caps at 2500 yd (833 below level 5).
Measured baseline (night2-4h, 2026-10-02, 4 h): median vendor re-pick gap
1113 s across 598 consecutive-pick gaps; grind/camp radii previously shared
the uncapped 10000 yd search with every other purpose.

Local validation: `tools/test_local_pick_policy.cpp` (donor window numbers,
pool-only scope, leave-outgrown exemption, camp bands; registered in
## Vendor buys ordered by item score (feat/vendor-buy-score, #427) — 2026-10-03
Feature: the vendor "buy useful" loop walks stock ordered by live
spec-relevant stat weight (`sRandomItemMgr.GetLiveStatWeight`, best upgrade
first) with the old item-level order kept only as the fallback when either
side scores 0, and the gear budget (`NeedMoneyFor::gear`) now covers all
three gear usages — EQUIP, BAD_EQUIP and BROKEN_EQUIP — instead of EQUIP
alone. Bought gear still equips at once via the existing `equip upgrades`
path. No REPLACE usage exists on our side (donor `ItemUsage` has
REPLACE=2 where ours has BAD_EQUIP=2; our EQUIP already covers both the
empty-slot and the better-than-equipped answers the donor splits across
REPLACE/EQUIP), so the gear set is complete at EQUIP/BAD/BROKEN; BROKEN_AH
stays unbought. Bounded: one weight lookup per vendor item per visit
(O(stock)), no per-tick scans; travel/level gates untouched.

Source project: `mod-playerbots`
`src/Ai/Base/Actions/BuyAction.cpp:69-88` (weight sort with item-level
fallback), `:140-147` (`REPLACE/EQUIP/BAD/BROKEN` → gear budget),
`:176-179` (`equip upgrades` after gear buys) @
`b6696bdbd3740e575598d167d69f39f68cc0b907`, adapted to 1.12
(`ItemTemplate` → `ItemPrototype`, `FindEquipSlot` path not needed — the
existing usage loop already re-checks `item usage` per item) and to our
scopes (`sRandomItemMgr.GetLiveStatWeight(bot, itemId)` like the AH loop,
pure ordering/budget rules in `VendorBuyPolicy.h` for the standalone test).

Source files: donor `BuyAction.cpp`.
Ported / reimplemented: `ai/playerbot/strategy/actions/BuyAction.cpp`
(score-ordered sort, gear-budget mapping, equip trigger),
`ai/playerbot/strategy/values/VendorBuyPolicy.h` (pure rank/budget rules),
`tools/test_vendor_buy_policy.cpp` (standalone g++ test, registered in
`tools/verify_all.sh`), `docs/concepts/bot-mechanics-and-quirks.md` (Gear
Upgrades & Scoring row).

Reason: our loop sorted by raw item level and refused (`usageAllowed=false`)
every stock item the classifier answered BAD_EQUIP/BROKEN_EQUIP for, so
vendor upgrades in those classes were never bought and a worse slot-fill
could precede the best upgrade.

Local validation: `tools/test_vendor_buy_policy.cpp` (gear-budget mapping,
score-vs-level ordering, unweighted fallback, tie stability; registered in
`tools/verify_all.sh`); `bash tools/verify_all.sh`; `git diff --check`. No
deploy (orchestrator compiles).

## Weighted RPG status mixer (issue #422) — 2026-10-03
Feature: a pool bot's next leisure journey (quest / grind / camp / explore)
is drawn from a weighted table (quest 60, grind 15, camp 10, explore 5 -
the donor's quest-heavy shape) among the purposes available right now,
instead of the four triggers racing on static relevance. Service and named
errands (vendor, repair, AH, mail, trainer, city, guild, ...) bypass the
mixer: need-gated business outranks leisure by design. Verdict cached per
bot ("rpg mixer pick" + "rpg mixer until"), revalidated on every read and
re-rolled the moment its purpose parks (no sticky dead verdict), spent on
every pick or parked search, so one trip rolls once - no per-tick cost, no
world scan.

Follow-up hardening (review y-422 CRITICAL 1, 2, 4 - verified in code):
availability mirrors the real request gates with cached values only
(quest = free log slots AND quest purpose unparked AND rpg-quest strategy
on; grind/camp = purpose unparked plus their NeedTravelPurposeValue phase
windows, camp also level 5+; explore = purpose unparked AND explore
strategy on). Quest takes part in the same roll (a Grind/Camp/Explore
verdict gates the quest request too - otherwise quest 6.30 always
pre-empts a camp 6.28 / explore 6.29 win and the mixer only steals from
grind); the quest errand (`request quest travel target`, empty qualifier -
"quest" is only its stored purpose string) keeps two bypasses: an aboard
rewardable finished quest (the 6.36 hand-in row and taker-only latch) and
an explicit player focus order. Grind is the fallback: with nothing else
available the verdict is grind and no grind request is ever mixer-blocked,
so the mixer can never idle a bot or level it slower than main.
`std::stoul` try/catch replaced with `Qualified::isValidNumberString`.

Source project: `mod-playerbots`.

Source commit: `mod-playerbots@b6696bdbd3740e575598d167d69f39f68cc0b907`.

Source files: donor `src/PlayerbotAIConfig.cpp:730-737`
(`RpgStatusProbWeight`: DoQuest 60, WanderNpc 20, WanderRandom/GoGrind/Flight
15, Camp/PvP 10, Rest 5),
`src/Ai/World/Rpg/Action/NewRpgBaseAction.cpp:1083` (`RandomChangeStatus`:
weighted roll over available statuses, rest fallback), `:1215`
(`CheckRpgStatusAvailable`: per-status gates),
`src/Ai/World/Rpg/Action/NewRpgAction.cpp:241` (IDLE fans out to the roll);
local `ai/playerbot/RpgMixerPolicy.h` (new: weights, availability-gated
roll, slot-purpose mapping, scope, verdict lifetime),
`ai/playerbot/strategy/actions/ChooseTravelTargetAction.cpp` (file-local
`RpgMixerRollVerdict` / `RpgMixerGateAllows` on the generic request gate +
spent-verdict clears on pick/park paths),
`tools/test_rpg_mixer_policy.cpp`.

Copied / ported / reimplemented: reimplemented the mixer as a request-side
gate on the existing generic travel requests (no donor status machine -
our journeys are destinations, not statuses). WanderNpc/WanderRandom map to
the camp (GenericRpg inn-hub NPC wandering) and explore slots; flight stays
transport under #426/#447, not a mixer slot; rest/PvP have no travel form
here (the quest errand's own park covers "nothing available"). Leave-
outgrown-zone grinds bypass (forced travel, not leisure); all destination
gates stay in force (#418/#428/#434, local picks #424/#441, stall parks
#423/#442, death protection, instance/BG blocks); owned/hired bots keep
player control.

Reason: without the mixer quest-vs-grind priority is one static number -
tuning questing means retuning a global relevance. Live night2 pool shows
the symptom (GenericRpg picked 2x as often as Grind whenever any purpose
was active); the donor tunes the same balance through the weight table.

Local validation: `tools/test_rpg_mixer_policy.cpp` (donor weights,
quest-heavy roll shape, availability gating, slot-purpose mapping,
pool-only scope, verdict window, parked-quest exclusion, stale-verdict
re-roll, grind fallback, quest gated by non-quest verdicts; registered in
`tools/verify_all.sh`);
`bash tools/verify_all.sh`; `git diff --check`. No deploy (orchestrator
compiles).
## Zone migration: leave-errand excludes the zone being left — 2026-10-03
Feature: the leave-outgrown-zone Grind search no longer re-picks the zone
the bot is leaving (`ai/playerbot/ZoneMigratePolicy.h`:
`ZoneMigrationExcludeZone` — bot's current zone id, or 0 when the leave
reason is "capital" or the zone is unknown; threaded as `excludeZoneId`
through `TravelMgr::GetPartitions` into `TravelMgr::IsLocationLevelValid`,
which refuses Grind points in that zone; computed in
`RequestTravelTargetAction::Execute` from `WorldPosition(bot).GetArea()`
with sub-area → parent-zone resolution).

Copied / ported / reimplemented: reimplemented (local rule; donor
comparison only — donor `mod-playerbots` teleports random bots into
level-bracketed zones, `RandomTeleportForLevel`
`src/Bot/RandomPlayerbotMgr.cpp:1783` + `zone2LevelBracket`
`src/Mgr/Travel/TravelMgr.cpp:4561`, ours migrates organically over the
travel graph with no teleports).

Reason: the leave rule fired on sub-area level but the search floor
(botLevel - 5, zone-level) still admitted home, and `SetBestTarget` takes
the nearest partition first — night2: Galwurth fired 3x at 10.59 in Durotar
and re-picked Durotar every time. Capital-idle leaves exclude nothing
(trainers/AH/bank; night2 Zarortharur kept Dun Morogh correctly). Pool-only
(masterless Grind errand); quest/vendor/gather/rpg untouched; Brill/
Kharanos/Razor Hill spirit-revive arrivals are a separate fix
(fix/lowbie-graveyard-strand), not counted here.

Local validation: `tools/test_zone_migrate_policy.cpp` (outgrown exclusion,
capital no-op, unknown fail-open; registered in `tools/verify_all.sh`);
`bash tools/verify_all.sh`; `git diff --check`. No deploy (orchestrator
compiles).

## Quest starter-valley ceiling exemption (issue #440) — 2026-10-03
Feature: the zone-average ceiling in `TravelMgr::IsLocationLevelValid` no
longer vetoes quest objective / giver points for a level 1-4 masterless
pool bot (`ai/playerbot/GrindSpotPolicy.h`: `QuestValleyExempted`, same
scope as the existing `GrindValleyExempted` beginnerGrind gate; hand-ins
stay exempt as before). The quest's own gates already vet the trip - the
+1 quest-level window, the spawn entry's own template
(`QuestObjectiveLevelFits`), the taker window (`QuestTakerTripFits`) and
the 40 yd point-danger surroundings - so the area average is redundant
here, not protective.

Copied / ported / reimplemented: reimplemented (local rule; donor
comparison only - donor `mod-playerbots`
`src/Mgr/Travel/TravelMgr.cpp:1224` keeps the `questLevel + 5 > botLevel`
veto without `can fight equal` for fresh bots, i.e. no such ceiling
exists there either for a level-1 bot holding a QuestLevel 2 quest).

Reason: the ceiling vetoed every Plainstrider point (area level 6) for a
level-1 tauren holding The Hunt Begins (QuestLevel 2) at Camp Narache, so
the quest search came back with an empty list and parked the purpose -
live 2026-10-03: 57 tauren bots logged QuestTripNoTarget '0' at the spawn
point (-13412.02, -6597.58 display; Camp Narache world -6597.58, -257.98)
in the first two minutes. Zone 5536 in the issue title is the Blackstone
Island dashboard label for that box, not the bot's zone: core
`WorldObject::GetZoneId` (terrain) reports Mulgore 215 there, goblin/high
elf normalisation only fires on true 5536/5225 spawns (server log: 21
goblin 5536 relocations, zero tauren), and playercreateinfo puts tauren
(race 6) at Camp Narache 215. Owned/hired bots and level 5+ keep the
ceiling: their player decides / the valley wall ends at 5.

Local validation: `tools/test_quest_taker_level_policy.cpp` section 4
(exempt 1/4 masterless, bound at 5, owned/hired excluded, level 60
excluded); `bash tools/verify_all.sh`; `git diff --check`. No deploy
(orchestrator compiles).

## Owned-bot quality of life (issue #473) — 2026-10-04

| Feature | Source project | Source commit | Source files | Ported / reimplemented | Reason | Local validation |
| --- | --- | --- | --- | --- | --- | --- |
| `.bot maintenance` (consumables/reagents/ammo/repair refresh) | `mod-playerbots` | `b6696bdbd3740e575598d167d69f39f68cc0b907` | `src/Ai/Base/Actions/TrainerAction.cpp:188-280` (`MaintenanceAction::Execute`), `src/PlayerbotAIConfig.cpp:647` (`AiPlayerbot.MaintenanceCommand`) | Reimplemented: one owned-bot command reusing the local bounded kit helpers (`RestockCompanion` family: reagents/potions/food/consumables/bandages/ammo/thrown) plus a free `DurabilityRepairAll(false, 0.0f)`; no talent/pet/spell/attunement sweep, no vendor walk. Gated on `AiPlayerbot.OwnedBotMaintenanceEnabled` (default 0) and owned/hired-only (`runtime/OwnedBotQolPolicy.h`) | Donor maintenance re-learns talents/spells/pets through its factory; the local equivalent for one owned bot is a kit top-up plus repair, which the factory already owns idempotently | `tools/test_owned_bot_qol_policy.cpp`; `bash tools/verify_all.sh`; compile via orchestrator; no deploy |
| `.bot autogear` (re-gear to level within quality/ilvl caps) | `mod-playerbots` | `b6696bdbd3740e575598d167d69f39f68cc0b907` | `src/Ai/Base/Actions/TrainerAction.cpp:569-732` (`AutoGearAction::Execute`: match/quality/ilvl/reset parsing, cap clamping), `src/Bot/Factory/PlayerbotFactory.cpp:3911-3938` (`AutoGear`/`DestroyEquippedGear`/`CalcMixedGearScore`), `src/PlayerbotAIConfig.cpp:672-673` (`AiPlayerbot.AutoGearQualityLimit`, `AiPlayerbot.AutoGearScoreLimit`) | Reimplemented: incremental-only `PlayerbotFactory::AutogearOwned(cappedQuality, ilvlCap)` through the existing `InitEquipment(true, false)` candidate pool (no wipe, no master sync); quality words/ilvl parsing with the donor's <=5-digits confusion guard in `runtime/OwnedBotQolPolicy.h`. Caps `AiPlayerbot.OwnedBotAutogearQualityCap` (default 2) / `AiPlayerbot.OwnedBotAutogearIlvlCap` (default 0 = uncapped), both default-off behind `AiPlayerbot.OwnedBotAutogearEnabled` | Donor wipes gear on `reset` and scores against a mixed gear score; the local pool already filters wearability/spec/level per slot, so the incremental path with a narrowed `randomGearMaxLevel` window is the faithful vanilla-1.12 adaptation | Same as above |
| Summon on group accept + condition knobs | `mod-playerbots` | `b6696bdbd3740e575598d167d69f39f68cc0b907` | `src/Ai/Base/Actions/AcceptInvitationAction.cpp:1-70` (bot-accepts-invite + summonWhenGroup/sightDistance gate), `src/Script/WorldThr/PlayerbotOperations.h:60-100` (bot-invites-player path), `src/Ai/Base/Actions/UseMeetingStoneAction.cpp:55-260` (`SummonAction::Teleport`: vehicle/teleporting guards, LOS landing, `BotRepairWhenSummon`, `AllowSummonInCombat`/`AllowSummonWhenMasterIsDead`/`AllowSummonWhenBotIsDead`, `ReviveBotWhenSummoned` 0/1/2), `src/PlayerbotAIConfig.cpp:626-639` (`AiPlayerbot.SummonWhenGroup`, `AllowSummon*`, `ReviveBotWhenSummoned`, `BotRepairWhenSummon`) | Reimplemented: post-bind hook in `AcceptInvitationAction` (owned = `IsOwnedBot()` or active hire, same-map, beyond sight distance) funnels into `PlayerConvenience::RequestGroupSummon` with `AiPlayerbot.OwnedBotSummonWhenGroup` + `OwnedBotSummonAllowInCombat/AllowMasterDead/AllowBotDead` + `OwnedBotSummonRevive` + `OwnedBotSummonRepair` (all default 0). Reuses the native 3s-delayed summon queue, its LOS landing and its follow restore; revive runs before queueing, repair after arrival | Donor teleports inline from the accept action; the local summon is an asynchronous world-tick transition with cancellation, so the hook only queues it and keeps every native precondition | Same as above |

## Update after review (issue #473) — 2026-10-04

Follow-up to the Gemini review of PR #480 (review-473.md):

- Global config mutation removed: `PlayerbotFactory::AutogearOwned` no
  longer narrows `sPlayerbotAIConfig.randomGearMaxLevel`. The ilvl cap is
  now an `InitEquipment(..., maxItemLevelOverride)` argument (0 = the
  global cap); the quality cap keeps using the existing `itemQuality`
  instance field, which is per-factory, not global. An ilvl above the
  global cap now applies (never silently ignored) within that one call.
- Summon abuse closed: new `AiPlayerbot.OwnedBotSummonCooldown` knob
  (default 300 s, code fallback = template = 300) records the last
  successful group summon per bot in `PlayerConvenience::m_groupSummonAt`;
  an uninvite/invite cycle inside the window is refused. Revive/repair on
  summon run only out of combat unless `OwnedBotSummonAllowInCombat` is
  explicitly on (still cooldown-bounded). Flags-off behaviour unchanged.

Local validation: extended `tools/test_owned_bot_qol_policy.cpp`
(cooldown + combat gate + revive/repair rule, 27 checks);
`bash tools/verify_all.sh`; `git diff --check`; orchestrator
`build-commit.sh` green.
| Pool-bot trade safety (`PoolBotTradeMode` 0-4 + `TradeActionExcludedPrefixes`, issue #469) | `mod-playerbots` @ b6696bdbd3740e575598d167d69f39f68cc0b907 | `src/PlayerbotAIConfig.cpp:569-570,700` (`TradeActionExcludedPrefixes`, `EnableRandomBotTrading`), `src/PlayerbotAIConfig.h:329,400`, `src/Ai/Base/Actions/TradeAction.cpp:14-24` (prefix guard), `src/Ai/Base/Actions/TradeStatusAction.cpp:47-55,235-250` (mode 0-3 gates), `conf/playerbots.conf.dist:780-783` | Ported/adapted: mode numbering kept (0 off / 1 trusted / 2 buy-only / 3 sell-only) with safe default 0 instead of donor 1 and an added open mode 4; trust extended to hire masters and same-account owners and reconciled with the legacy master/group `shouldTrade` block so open modes and ungrouped owners/hire-masters work; prefix guard and buy/sell settle gates reimplemented in `runtime/PoolBotTradePolicy.h`; inbound/settle gates in `TradeStatusAction`, prefix guard in `TradeAction`, Trade-channel-only mention guard in `PlayerbotAI`. Bot-to-bot trade life (RPG giveaways, enchants, `WTS`/`WTB`) intentionally untouched | Pool-bot stranger trades refused while master/group/hire-master trades pass; Trade-channel chatter without a mention ignored; addon-prefixed lines never open trade |
| Buff refresh window + single-to-group upgrade with reagent gate (`BuffNeedsRefresh` 15 s, `GroupBuffVariantFor`, `ShouldUpgradeToGroupBuff`; trigger/action/party-search wiring, greater-trigger reagent+training gate, single-party `isUseful` quorum stand-down) | `mod-playerbots` @ b6696bdb | `src/Ai/Base/Util/GenericBuffUtils.{h,cpp}` (`BuffBelowRefreshTarget`, `HasEnoughSameMapMissingPlayersForGroupVariant` requiredCount = 3, `GroupVariantFor`, `UpgradeToGroupIfAppropriate`, `HasRequiredReagents`), `src/Ai/Base/Trigger/GenericTriggers.{h,cpp}` (`BuffTrigger::IsActive`, `BuffOnPartyTrigger`), `src/Ai/Base/Actions/GenericSpellActions.{h,cpp}` (`CastAuraSpellAction`/`CastBuffSpellAction`/`GroupBuffSpellAction::isUseful`+`Execute`), `src/Ai/Base/Value/PartyMemberWithoutAuraValue.cpp`, `src/Bot/ForceRebuff.{h,cpp}` (reference only - no worktree equivalent) | `ai/playerbot/GroupBuffPolicy.h` (pure rules + `tools/test_group_buff_policy.cpp`), `ai/playerbot/strategy/triggers/GenericTriggers.cpp` (`BuffTrigger`, `GreaterBuffOnPartyTrigger`), `ai/playerbot/strategy/actions/GenericSpellActions.{h,cpp}` (`CastAuraSpellAction`, `BuffOnPartyAction`), `ai/playerbot/strategy/values/PartyMemberWithoutAuraValue.cpp` | Reimplemented for the worktree's static single/greater split: expiring-in-window counts as missing (refresh), singles stand down at quorum (3 same-map members lacking both, trained + reagent present) so the higher-priority greater node fires; donor's per-cast dynamic upgrade + announce + force-rebuff top-off not ported (no `GroupBuffSpellAction`, no announce path, blessings keep their own gates) | `tools/test_group_buff_policy.cpp` (33 checks) + `bash tools/verify_all.sh`; `git diff --check`; module build via orchestrator |
| Service-trip arrival + unwatched rescue + city need gate (AH/vendor/repair/mail/trainer/city) | mod-playerbots@b6696bdb `src/Ai/Base/Actions/MoveToTravelTargetAction.cpp` (stable per-(bot, destination) approach offset; no arrival shortcut, no teleport, donor drops to cooldown on max retry) + `src/Ai/Base/Actions/ChooseTravelTargetAction.cpp:58` (ungated 10% city trip to banker/battlemaster/auctioneer) | `ai/playerbot/ServiceTripPolicy.h` (new: service predicate, hall/counter arrival radii, stable offset, rescue gate), `ai/playerbot/strategy/actions/MoveToTravelTargetAction.{h,cpp}` (`CheckServiceArrival`, `TryRescueServiceTrip`, stable service jitter), `ai/playerbot/strategy/actions/ChooseTravelTargetAction.cpp` (city walks to auctioneer only), `ai/playerbot/strategy/values/TravelValues.cpp` (city needs `should ah sell` or buyer trip), `tools/test_service_trip_policy.cpp` | Reimplemented (donor behaviour harvested, architecture not copied: arrival via NPC spawn lookup, rescue via same-map unwatched TeleportTo at IsMaxRetry with 30-min cooldown + `ServiceTripTeleport` event, city gated on AH business) | Live 2026-10-03: AH purpose 1800 picks / 758 move-fails / 316 drops, city 1614 / 917 / 520; Orgrimmar bots stalled 5-8 yd out (path incomplete), 133 AH fails within 30 yd; `tools/test_service_trip_policy.cpp` green; `bash tools/verify_all.sh` green; `git diff --check` clean; compile via orchestrator |

## Rotation gaps: Demonology Immolate, Elemental/Enhancement Flame Shock, BM Intimidation (issue #467) — 2026-10-04
Feature: Demonology keeps `immolate` up (`ACTION_NORMAL + 1`, same slot as
Destruction); Elemental/Enhancement keep `flame shock` up first via a new
`flame shock upkeep` trigger (`FlameShockTrigger`, plain `DebuffTrigger`: no
flame-shock aura on target) above the generic `shock` -> `earth shock` line
and the separate `earth shock interrupt` duty — `ShockTrigger` stays blind
to the flame-shock aura so `earth shock` still spends the shared cooldown
while the DoT ticks; Beast
Mastery fires `intimidation` on cooldown via a new `IntimidationTrigger`
(`SpellCanBeCastedTrigger` on `self target`: the self-cast fails core
`CanCastSpell` with `SPELL_FAILED_TARGET_ENEMY` against the hostile current
target, plus a live-pet gate like `KillCommandTrigger` since the stun lands
via the pet) below `kill command` (`ACTION_NORMAL + 3` vs `+ 4`).

Copied / ported / reimplemented: reimplemented (donor
`mod-playerbots @ b6696bdbd3740e575598d167d69f39f68cc0b907`:
`src/Ai/Class/Warlock/Strategy/DemonologyWarlockStrategy.cpp` (immolate
upkeep 17.5 + immolate on attacker 19.0), `src/Ai/Class/Shaman/Strategy/
ElementalShamanStrategy.cpp` (flame shock 5.3) and `EnhancementShaman-
Strategy.cpp` (flame shock 19.0), `src/Ai/Class/Hunter/Strategy/
BeastMasteryHunterStrategy.cpp` (intimidation 40.0). Donor extras not
ported: `immolate/corruption on attacker` spread, `earth shock execute`,
`lava burst`/`maelstrom`/`feral spirit` kit, `kill command`/`kill shot`/
`serpent sting` kit — no matching 1.12 spells or engine values here; donor
`DebuffTrigger` target-lifetime gate (`estimated group dps`) not ported
either, ours already gates via shared-cooldown state. Donor `BuffTrigger`
refresh-ahead vs ours missing-aura-only kept as-is: re-casts land only
after full expiry).

Reason: the three specs queued no upkeep for those spells — Demonology had
no `immolate` node at all (Affliction/Destruction do), Elemental/Enhancement
queued only the generic `shock` -> `earth shock` line (`ShamanStrategy.cpp`
falls back to `flame shock` only when `earth shock` is unknown, and
`ShockTrigger` refuses to fire while any shock aura is present, so the DoT
never refreshed), and BM registered `intimidation` only as the scatter-shot
node fallback with no trigger pushing it (`intimidation on snare target`
needs a snare-state target and never fires as a cooldown).

Local validation: `python3 tools/verify_action_trigger_wiring.py` (0 live
missing), `bash tools/verify_all.sh`; `git diff --check`. No deploy
(orchestrator compiles).
## Shield ping-pong reverse guard + vendor shield-first (review #465) — 2026-10-04
Feature: the slot-aware spec weapon policy (`SpecWeaponPolicy.h`, new) pins
the owner weapon matrix as pure rules - prot warrior/paladin 1H main hand +
shield off hand, holy shield-or-held, arms/ret 2H-only, fury 1H pair with a
2H stand-in only before Dual Wield - and the module now enforces the
off-hand side: `RandomItemMgr::ShouldEquipWeaponForSlot` answers per
concrete slot (warrior/paladin via the policy, other classes fail open to
the old any-slot answer), the equip audit (`ItemUsageValue`) gates weapons
through it (compare, NONE-with-equipped, spec transitions, stand-in guard,
MH/OH hand-swap driver in `EquipAction`), so a bag 1H weapon never targets
the shield and a shield never answers EQUIP over a weapon in the off hand.
Vendor side: a shieldless shield-spec bot sorts shields before all other
weapon upgrades (`VendorShieldRank`, same EQUIP rules, own gold, trainer
reserve first) and never buys an off-hand weapon (`BuyAction` pass +
`VendorHasUsefulItemValue` trigger veto; the slot-aware audit would answer
NONE for it anyway).

Copied / ported / reimplemented: reimplemented (donor `mod-playerbots`
`src/Mgr/Item/StatsWeightCalculator.cpp:630-692` penalises 2H x0.05, x0.1
for shield specs, and `src/Mgr/Item/RandomItemMgr.cpp:1202-1265` keeps a
dead-code 1H+shield allowlist for prot, but neither forbids a weapon in
the off-hand slot - the reverse guard is new; donor SHA `b6696bdb`).

Reason: review #465 showed the J shield-transition alone ping-pongs - a bag
1H out-scores the shield (EQUIP over it next audit), the shield-transition
re-equips the shield, looping every audit cycle - and the vendor pass would
spend gold on those unwanted off-hand weapons.

Local validation: `tools/test_spec_weapon_policy.cpp` (matrix rows,
ping-pong reverse guard, fury pre/post-DW, shield detection, vendor rank;
registered in `tools/verify_all.sh`); `bash tools/verify_all.sh`;
`git diff --check`. No deploy (orchestrator compiles).
## Hunter & warlock pet presence, choice and ranks (task I) — 2026-10-04
Feature: hunter pets keep family-correct level-appropriate ranks with
sensible autocast (Growl on, Cower/Prowl off); solo pool warlocks upgrade
Imp -> Voidwalker once they know the summon (697, level 10); both relearn
missing ranks on the periodic initialize-pet tick without a relog/resummon.

Copied / ported / reimplemented: reimplemented. Donor `mod-playerbots` @
b6696bdbd3740e575598d167d69f39f68cc0b907: `src/Bot/Factory/PlayerbotFactory.cpp`
`InitPet` (hunter pet creation + autocast sweep; warlocks summon live, so no
factory pet), `src/Ai/Class/Warlock/Strategy/GenericWarlockNonCombatStrategy.cpp`
(summon fallback chain voidwalker -> imp; spec pet strategies imp/voidwalker/
succubus/felhunter with "wrong pet" nodes) and `src/Ai/Class/Warlock/WarlockTriggers.cpp`
`WrongPetTrigger` (exactly one pet strategy enabled + known summon spell).
Adapted to 1.12: no summon-strategy fan (this module's spec strategies
`pet <spec> pve` already default Voidwalker solo / Imp raid); one shared
"wrong pet" node on `WarlockPetPveStrategy`; rank ladders verified against
tw_world.spell_template baseLevel + the SkillLineAbility DBC (phantom
Dive 23146 / Dash 23100-23112 / skill-261 rows excluded, Turtle custom ranks
included); teach-time autocast pins Sacrifice/Seduction off (the sweep would
leave them on); families 35/36/39 mapped to their DBC skill lines.

Reason: live 2026-10-04: 50 online lvl10+ warlocks, 44 with Imp in the active
slot vs 6 with Voidwalker (41 vs 5 in the orchestrator's earlier sample) —
both summons known, "no pet" never fires while a pet lives, and the
spec-strategy "wrong pet" nodes had no trigger creator (dead wiring); hunter
pets at 10-18 carried only the tame-time ranks (Growl 14916, Bite 17255,
Claw 16828...) with wrong Dive/Screech/Claw/Furious-Howl levels and no
Thunderstomp 51156 rank, and fox/serpent/moth families had no table at all.

Local validation: `tools/test_warlock_pet_policy.cpp`,
`tools/test_pet_spell_rank_policy.cpp` (registered in
`tools/verify_all.sh`); `bash tools/verify_all.sh` (incl. host contract);
`git diff --check`. No deploy (orchestrator compiles).
## Rogue spec weapon types: subtlety joins the dagger-only set (task K) — 2026-10-04
Feature: `RandomItemMgr::ShouldEquipWeaponForSpec` treats the `subtle`
weight scale like `assas` (MH/OH daggers only) via the shared
`ai/playerbot/strategy/values/RogueWeaponPolicy.h:RogueSpecWantsDaggers`
predicate. Combat keeps swords/maces/fists; other classes unchanged. This
flows through the existing equip logic (equip audit, `QueryItemUsageForEquip`
spec transition, vendor weapon upgrades, loot/roll need) with no new rules.

Copied / ported / reimplemented: reimplemented from donor behaviour
(mod-playerbots `src/Mgr/Item/RandomItemMgr.cpp:ShouldEquipWeaponForSpec`
dagger-only rogue gate + `src/Mgr/Item/StatsWeightCalculator.cpp:699-701`
dagger 1.5x for assassination/subtlety and `:999-1005` slow-dagger-MH /
fast-dagger-OH speed bonus, at local `playerbots-references/mod-playerbots`
`b6696bdb`).

Reason: the `else` fallback let subtlety rogues keep swords/maces, but the
spec's Backstab/Ambush openers need a dagger main hand (core
`Spell::CheckItems` refuses the cast: `SPELL_FAILED_EQUIPPED_ITEM_CLASS`),
so a subtlety rogue wielding a sword queued doomed openers exactly like an
assassination rogue would. Live 2026-10-04: 69 online pool rogues, 64 at
level 10+; talent-marker census (ass 8, combat 27, subtle 12) found 8/12
subtlety rogues with a non-dagger main hand (maces/swords outscoring plain
daggers on `mledps`), 0/8 assassination rogues mismatched, and 1/107
NotBehind vs 479 BadTargets on Backstab casts (positioning, not weapons).

Local validation: `tools/test_rogue_weapon_policy.cpp` (assas/subtle
dagger-only, combat/other excluded; registered in `tools/verify_all.sh`);
`bash tools/verify_all.sh`; `git diff --check`. No deploy (orchestrator
compiles).
| Hunter dead-zone switch hysteresis + revenge-before-travel self-defence | mod-playerbots `src/Ai/Class/Hunter/HunterTriggers.cpp:112-128` (SwitchToRanged victim!=bot/immobilized/slow/dist>8; SwitchToMelee victim==bot AND dist<=8, no level gate either side) + `src/Ai/Base/Actions/ChooseTargetActions.cpp:104-133` (AttackAnythingAction::isUseful with no facing gate) | `ai/playerbot/HunterSwitchPolicy.h` (ShouldSwitchToMelee/ShouldSwitchToRanged + 5/10 yd edges), `ai/playerbot/strategy/hunter/HunterTriggers.h` (triggers delegate, AND/OR shapes unchanged), `ai/playerbot/strategy/actions/ChooseTargetActions.cpp` (revenge victim==bot answers first with no isInFront gate, before the QuestTaker walk-through exemption and the wounded-pool gate; pre-emptive strike keeps the front arc) | Reimplemented: donor switch shapes kept, only the shared 8 yd distance edge becomes a 5/10 yd hysteresis band; revenge reorder is donor-parity (no facing gate) | Pool hunters flipped kits every few ticks at the shared 8 yd line (inter-switch p50 15 s, 37% within 10 s, each flip rebuilding the combat trigger graph); travelling bots on completed hand-in walks never faced their attacker so the isInFront revenge gate never fired (taker trips fought back 5% vs 60-93% elsewhere) | `tools/test_hunter_switch_policy.cpp`, `bash tools/verify_all.sh`, `git diff --check` |
| Party threat back-off for real-master non-tank bots | `mod-playerbots` @ 5397110 `src/Ai/Base/Strategy/ThreatStrategy.cpp:11-60` (single >= 80 / AoE >= 50 veto, group-gated) | `ai/playerbot/AiFactory.cpp` (`AddDefaultCombatStrategies`: add `threat` for non-tank bots with a real player master, outside BGs) | Reimplemented wiring only: the multiplier already existed (`generic/ThreatStrategy.cpp`); previously never added to any engine. Pool bots unchanged; ThreatValue returns 0 with no tank so solo/no-tank parties unaffected | `bash tools/verify_all.sh`; `git diff --check`. Runtime pull-aggro check pending deploy |
| Warrior base-combat interrupts (Arms/Fury pummel + shield bash) | `mod-playerbots` `src/Ai/Class/Warrior/WarriorTriggers.h` (pummel/shield-bash interrupt + enemy-healer triggers) | `ai/playerbot/strategy/warrior/WarriorStrategy.cpp` (`WarriorStrategy::InitCombatTriggers`: four rows at ACTION_INTERRUPT) | Ported trigger rows; creators/actions/stance nodes already existed (`WarriorTriggers.h`, `WarriorAiObjectContext.cpp`). Pummel stance-dances via its berserker-stance node; shield bash has no stance gate so one always fires | `python3 tools/verify_action_trigger_wiring.py` (0 live-missing); `bash tools/verify_all.sh`. Runtime interrupt check pending deploy |
| Paladin tank taunt (Hand of Reckoning + Righteous Defense fallback) | `mod-playerbots` `src/Ai/Class/Paladin/Strategy/TankPaladinStrategy.cpp:116-121` + node factory (`hand_of_reckoning` -> `righteous defense` alternative) | `ai/playerbot/strategy/paladin/TankPaladinStrategy.cpp` (lose-aggro row + `TankPaladinStrategyActionNodeFactory`) | Ported row + node, mirroring live `ProtectionPaladinStrategy`. Turtle core: Hand of Reckoning is trainer spell 51303 (level 10); Righteous Defense ranks are 51328-51330 (`spell_paladin_righteous_defense`); DBC `Spell.dbc` names both plus Righteous Fury. Actions resolve via `PaladinAiObjectContext`. Note: file is an unregistered forward-port; live tanks run Protection (already correct) | `python3 tools/verify_action_trigger_wiring.py` (0 live-missing); `bash tools/verify_all.sh`. Runtime taunt check pending deploy |
| Protection righteous fury upkeep in combat | `mod-playerbots` `src/Ai/Class/Paladin/Strategy/TankPaladinStrategy.cpp:147-154` (righteous-fury trigger row) | `ai/playerbot/strategy/paladin/ProtectionPaladinStrategy.cpp` (`ProtectionPaladinBuffStrategy::InitCombatTriggers`) | Ported: prior live row existed only in `InitNonCombatTriggers`, so mid-pull loss (death/bubble) stayed off all fight. Same trigger, buff-level priority so taunts and Holy Shield win the tick. Dropped donor pieces stay dropped: seal of corruption/vengeance, shield of righteousness, hammer of the righteous, avenger's shield, avenging wrath, divine sacrifice (WotLK-only); live already covers taunt, holy shield, 2+ consecration, sanctuary/kings, righteousness seal | `bash tools/verify_all.sh`; `git diff --check`. Runtime fury-uptime check pending deploy |
| Ranged keep-away verification (no change) | `mod-playerbots` `src/Ai/Base/Strategy/RangedCombatStrategy.cpp:10-16` (enemy-too-close -> flee) | `ai/playerbot/strategy/generic/RangedCombatStrategy.cpp:7-22` (already has `enemy too close for spell` -> `flee` at ACTION_MOVE + `enemy out of spell` -> `reach spell`) | Verified present; no edit. Applies to every bot with the `ranged` kit, pool bots included (unchanged behavior, as required) | Code read; `bash tools/verify_all.sh` |
| DPS target tournament + skull snap (LD-1/LD-4) | `mod-playerbots` @ 79bd4281 `src/Ai/Base/Value/DpsTargetValue.cpp:53-306` (caster/general/combo tournaments, CC-moon skip, skull snap), `src/Ai/Base/Value/TargetValue.cpp:124-143` (IsHighPriority) | `ai/playerbot/DpsTargetPolicy.h` (new pure bucket rules + `tools/test_dps_target_policy.cpp`), `strategy/values/DpsTargetValue.cpp` (caster/general/combo tournament strategies, skull snap with sticky flag; explicit > RTI > tank-follow entry order unchanged, LD-2 deliberately not ported) | Reimplemented in place: `prioritized targets` half of IsHighPriority has no equivalent here (skull only); small groups (<=3 near) always run the general pick; no `IsCombo` helper existed (rogue or cat-aura druid inline) | `bash tools/verify_all.sh` (incl. new policy test); `git diff --check`. No live dungeon test |
| `focus` single-target burn strategy (LD-3) | `mod-playerbots` @ 79bd4281 `src/Ai/Base/Strategy/ThreatStrategy.cpp:44-64` (FocusMultiplier vetoes AoE + CastDebuffSpellOnAttackerAction), `src/Ai/Base/StrategyContext.h:145` (focus registration) | `ai/playerbot/strategy/generic/FocusStrategy.{h,cpp}` (new FocusMultiplier + FocusStrategy, off by default), `strategy/StrategyContext.h` (`focus` creator), `docs/guides/player-controls.md` (`co +focus` toggle row) | Reimplemented in place: donor's single attacker-debuff class maps to ours split `CastMelee/RangedDebuffSpellOnAttackerAction`; heal exemption kept (CastHealingSpellAction) | `bash tools/verify_all.sh`; `git diff --check`. No live CC-pack test | Deliberate divergence (review PR #584): spell-data detection vetoes more than the donor (consecration, holy nova, thunder clap, shouts, frost nova, fears, chains) — breaking less CC is the toggle's purpose.
| `end pull` stuck-pull escape hatch (LD-9) | `mod-playerbots` @ 79bd4281 `src/Ai/Base/ActionContext.h:116,331` (`end pull` = `ChangeCombatStrategyAction(-pull)`) | `strategy/actions/ActionContext.h` (`end pull` creator reusing `PullEndAction`), `strategy/triggers/ChatTriggerContext.h` + `strategy/generic/ChatCommandHandlerStrategy.cpp` (chat wiring), `docs/guides/player-controls.md` (whisper row) | Reimplemented: donor drops the `pull` strategy; ours runs `PullEndAction` bookkeeping (target clear, party release, movement restore) and keeps `pull` armed for the next pull — no re-enable needed | `bash tools/verify_all.sh` (incl. wiring check); `git diff --check`. No live stuck-pull test |
| Cast-time lifetime veto (LD-7) | `mod-playerbots` @ 79bd4281 `src/Ai/Base/Strategy/CastTimeStrategy.cpp:11-65` (cast-time vs health/estimated-group-dps lifetime to 0.1x, dest-location exclusions, channeled-duration add) | `strategy/generic/CastTimeStrategy.cpp` (HP%+ladder replaced with donor lifetime comparison; criticalHealth gate dropped) | Reimplemented in place: channeled add via local `IsChanneledSpell`/`GetSpellDuration` idiom (donor `SpellInfo::IsChanneled`) | `bash tools/verify_all.sh`; `git diff --check`. No live cast-observation test |
| Conditional tank RTI + combat-gated auto-mark (LD-5/LD-6) | `mod-playerbots` @ 79bd4281 `src/Ai/Base/Value/TankTargetValue.cpp:110-131` (take RTI only for non-tank victims or different-RTI tanks), `src/Ai/Base/Trigger/RtiTriggers.cpp:10-20` (NoRti false out of combat), `MarkRtiStrategy.cpp:13` (NORMAL relevance) | `strategy/values/TankTargetValue.cpp` (victim gate; victimless pre-pull marks still taken), `strategy/triggers/RtiTriggers.h` (`!IsInCombat` refuse), `strategy/generic/MarkRtiStrategy.cpp` (EMERGENCY→NORMAL) | Reimplemented in place: victimless RTI still taken (donor falls through; preserves today's pre-pull tank open); pet victims taken (donor only checks players) | `bash tools/verify_all.sh`; `git diff --check`. No live 2-tank test |

## Quest accept/drop churn + banned quests + Bone Chew Toy — 2026-10-04
Feature: masterless pool bots refuse war-effort item turn-ins (AQ sort
-365 with item objectives: copper/thick-leather turn-ins, signet quests)
and banned quests (CLUCK! 3861, inactive Method-disabled templates) at
accept (`WouldAcceptQuest` + raw-id/share/confirm/details guards), and the
clean action drops them with the same predicate (banned at any status for
every bot; war-effort when incomplete/failed for upkeep bots; COMPLETE
war-effort never dropped - turned in instead). Bone Chew Toy (item 51751)
is never looted (`IsLootAllowed` veto + usage NONE), its GO piles
(1000380) never queue, and copies in bags are destroyed by smart-destroy.
Targeted by id - no generic quest-class purge, so quest starters (Free
Ticket Voucher 19338 etc.) keep working.

Copied / ported / reimplemented: reimplemented (local policy in
`ai/playerbot/QuestLogPolicy.h`, tested by
`tools/test_quest_log_triage_policy.cpp` §§7-10); donor behaviour
`mod-playerbots` `src/Ai/World/Rpg/Action/NewRpgBaseAction.cpp`
(`IsQuestWorthDoing` `:556-571` refuses repeatables, `OrganizeQuestLog`
`:590-641` drops not-worth/capable + sort quests `ZoneOrSort < 0`)
@ b6696bdbd3740e575598d167d69f39f68cc0b907 - modulated here to the
war-effort sort only (breadcrumbs 8792/8795 with no item objective stay
open) and to an accept-side filter matching the drop rule.

Reason: live pool 2026-10-03/04: 16 695 accepts vs 7 194 drops (43%).
War-effort turn-ins were ~60 accepts/h per capital with same-tick
mass-drop bursts (Jaegaewog 22:19:55 dropped 19 quests at once after
accepting 2); CLUCK! 153 accepts / 149 drops across 22 re-cycling bots;
892 Bone Chew Toys sat in 118 bags from 897 StoreLoot rows on GO 1000380.

Local validation: `tools/test_quest_log_triage_policy.cpp` (10 sections);
`bash tools/verify_all.sh`; `git diff --check`. Module build by
orchestrator (workers do not run the docker builder).

## Lowbie taker-corridor self-defence + stable quest approach — 2026-10-04
Feature: (1) the grind pick answers an attacker whatever its level
(`GrindTargetValue::FindTargetForGrinding` attackers loop no longer applies
the +1 `levelTooHigh` gate; evade/unreachable and follower-leash guards
stay, and the gate is untouched in the new-pull possible-targets loop);
(2) the travel walk keeps one stable per-(bot, destination) approach point
for every purpose, not just service trips (the quest/giver/taker jitter no
longer re-rolls every re-entry).

Copied / ported / reimplemented: ported donor behaviour, both halves from
mod-playerbots @ b6696bdbd3740e575598d167d69f39f68cc0b907 —
`src/Ai/Base/Value/GrindTargetValue.cpp` (attackers loop returns the first
live attacker unconditionally, no level check; the +4 check lives only on
the `possible targets` loop) and
`src/Ai/Base/Actions/MoveToTravelTargetAction.cpp:79-104` (stable
angle/mod per (bot, destination) pair for the whole action, no
purpose-gated jitter).

Reason: live pool Oct 2026 — (1) Renees/Jorik taker walks died 3/83 with a
fight-back vs 48% on grind trips: the pick came back null on over-level
attackers so `AttackAnythingAction::isUseful` bailed before its revenge
block and bots walked 800 yd through level 7-8 ground without a swing
(Morupurtre 20:40:06 Greater Duskbat 6 at 3 yd, zero orders in 37 s);
(2) per-tick jitter oscillated quest-trip walks across platform edges
(Dolanaar/Sen'jin/Kharanos env-death pits, all within yards of a service
NPC, repeat same-point deaths).

Local validation: `bash tools/verify_all.sh`; `git diff --check`;
`$SCR/build-commit.sh <sha>` green (DEPLOY never set).

## World buffs at capital recruiters (issue #492) — 2026-10-05

Feature: the six capital `<Mercenary Hire>` recruiters offer a `World buffs`
branch (level 60+, real players only) with seven purchasable buffs. Each
unlocks once per character via a migration quest (90000-90013, native
negative-`RewOrReqMoney` fee: 200g raid bosses, 100g rest, no XP, no reward,
non-sharable). Purchases apply the original aura directly with
`Unit::AddAura(spellId, 0, npc)` on buyer + live in-world group/raid members
within 40 yd of the NPC (charge-first/refund, hire pattern); Sayge picks one
of 8 fortunes from a submenu (others stripped first), DM lands all three.
Onyxia/Nefarian credit invisible Rally entry 95100 for the group in reward
distance; Rend/Hakkar use direct kill objectives. DM/Sayge/Songflower quests
are event quests completed receiver-only on real aura gains (recruiter
casters ignored, incl. Chronoboon-style restores with no live caster);
Silithyst counts 5x opposite-faction player kills in Silithus (zone 1377)
for the group. `KeepWorldBuffsInRaids=1` restores the 18 DB-listed spells
with remaining time on raid entry; `=0` strips them in Upper Karazhan (814).
Unlock quests are pool-bot banned (90000-90013 in `IsBannedQuest`).

Source files:
- `runtime/WorldBuffPolicy.h` (gates, senders 505-508, prices, quest/aura/boss/teleport maps)
- `runtime/WorldBuffService.h` (price pairs, spell lists, Sayge picks)
- `runtime/WorldBuffRaidKeeper.h` (18-spell set + memory-only snapshot store)
- `host/HireRecruiterScript.{h,cpp}` (root/branch/Sayge menus, purchase, quest merge)
- `host/WorldBuffKillAdapter.{h,cpp}` (`OnCreatureKill` → Rally credit)
- `host/WorldBuffAuraAdapter.{h,cpp}` (`OnAuraApply` → receiver-only event credit)
- `host/WorldBuffPvpAdapter.{h,cpp}` (`OnPVPKill` → Silithyst credit)
- `host/WorldBuffRaidAdapter.{h,cpp}` (snapshot/restore/strip teleports)
- `data/sql/world/20261005090000_world.sql` (npc_flags 1→3 on 6 capitals, 14 quests, credit entries 95100/95101, relations)
- `tools/test_world_buff_policy.cpp` (233 checks: gating, routing, prices, quest/aura/boss/teleport maps, service pairs/spells, keeper store)

Copied / ported / independently reimplemented: independently reimplemented
(native mechanic; no donor — donor behaviour references are travel-to-buff
errands only, not purchases). Core API signatures verified in tortoise-wow
source before use (`AddAura`, `PrepareQuestMenu`, `RewardPlayerAndGroupAtEvent`,
`AreaExploredOrEventHappens`, `KilledMonsterCredit`, teleport hooks).

Reason: solo-with-bots endgame preparation behind real unlock effort
(one true kill / aura / 5 PvP kills per character), priced per head so raid
use stays a gold sink, automation-free (explicit purchase clicks only).

Local validation:
- `tools/test_world_buff_policy` 233 checks — PASSED.
- Migration applied to dev DB and rolled back (14 quests with correct
  Type/flags/fees, 42+42 relations, npc_flags 3, then clean revert) — PASSED.
- `bash tools/verify_all.sh` + `git diff --check` — clean each stage.
- `$SCR/build-commit.sh <sha>` green per stage (PR1 amend for dropped const,
  PR5 amend for private `FindQuestSlot` → public `GetQuestStatus`).
- Not yet observed: live in-game flow (menu, purchase, credit, raid keep).
  Needs the deployed server with the migration applied.

## Native pre-cast healing cancellation

Independent correction against `tortoise-wow/tortoise-wow` core `d94947b0db60c33e7248523ad0ba7f58af97fd09`, `src/game/Spells/Spell.cpp` (`Spell::Update`, `handle_immediate`) and `Spell.h` (`SpellState`). The cast bar is PREPARING; CASTING is the already-started channel. No donor code copied.

Files: `ai/playerbot/HealingCastPolicy.h`, `ai/playerbot/strategy/triggers/HealthTriggers.cpp`, `tools/test_healing_cast_policy.cpp`. Preserves the optional strategy and conservative 90%-health/50%-waste boundary, excludes multi-target and hybrid spells, and uses the real cast target for preheal prediction. Validation: standalone cancellation boundary/safety cases and full native-module/core compile; runtime verification still required.

### Preserve previous-run diagnostic logs

Original module implementation; no donor code. `LogFileRotation.h` and
`PlayerbotAIConfig::openLog` preserve enabled write-mode logs before truncation.
Motivation: a restart discarded the bot-event evidence for a live healer report.
Validation: filesystem fixture covers first/second startup, repeated refresh,
append mode and failed rotation with both current and previous data preserved.

## Party tank-face + melee rear (night2 research gap 2) — 2026-10-08

Feature: in a real-player-master party a tank bot holding a mob sidesteps so
the mob's front points away from the party (`tank face needed` trigger →
`tank face away` action on the `close` strategy); melee DPS on a mob that
targets someone else works its rear via `set behind` (`behind` strategy now
on every melee DPS kit, including retribution and enhancement, which lacked
it). Pool bots unchanged (trigger requires a real player master).

Source repository: `mod-playerbots/mod-playerbots`

Source commit: `b6696bdbd3740e575598d167d69f39f68cc0b907` (local
`playerbots-references/mod-playerbots` checkout).

Source files:
- `src/Ai/Base/Actions/MovementActions.cpp:2405-2471` (`TankFaceAction::Execute`: has-aggro + melee + stationary gates, party-average angle, +-3PI/5 near-point sidestep, 90-degree hysteresis)
- `src/Ai/Base/Actions/MovementActions.cpp:2327-2367` (`AverageGroupAngle`)
- `src/Ai/Base/Strategy/CombatStrategy.cpp:74-86` (`TankFaceStrategy` default action)
- `src/Ai/Base/Strategy/MeleeCombatStrategy.cpp:18-23` (`SetBehindCombatStrategy` wiring)

Copied / ported / independently reimplemented: ported, adapted to the 1.12
codebase. Trigger/action geometry (average party angle, +-108-degree
destinations, nearest-side pick, LOS/terrain check, 90-degree fire window) is
behavior-identical; the flee-info anti-oscillation cache is replaced by the
hysteresis window plus a 2 s trigger interval. Local additions: explicit-hold
exemptions (`stay`, `wait for attack`), creature-only scope, LOS fallback
mirroring `SetBehindTargetAction`. The raid-dragon `dragon flank` /
`dragon tank face away` paths are untouched (entry-gated raid geometry).
`SetBehindTargetAction` itself is unchanged — only its strategy coverage grew.

Reason: tank bots never turned mobs away (cleaves hit the party) and
retribution/enhancement DPS never left the mob's front (no `behind` kit).

Local validation: `python3 tools/verify_okf.py` + `bash tools/verify_all.sh`
(see commit); `git diff --check` clean. No build (per task constraints);
live in-game check pending: tank sidesteps on pull, melee work the rear,
no jitter, pool bots unchanged.

## Combat rotation ports: rogue finisher dump, hunter feign, druid rejuv gate, warlock tap (night2 rotations) — 2026-10-08

Feature: four small donor-parity rotation fixes from night2 research. (1)
Rogue: an almost-dead target (<=25% health) eats whatever combo points are
banked (1+) as *Eviscerate* at HIGH+2, ahead of the gated SnD/4CP finishers,
so points land as damage instead of dying with the mob. (2) Hunter: `medium
threat` fires `feign death threat` (HIGH) instead of the distracting-shot
taunt; the base kit already covers open-world combat, so feign now drops
aggro outside raids too. (3) Druid: the leveling kit casts *Rejuvenation*
only below the low-health line with mana to spare (scratches no longer
outbid the damage kit), and a 10+ druid sitting in Bear/Dire Bear/Cat form
idles the caster wrath/moonfire/heal nodes at HIGH while shifted so they
never outbid the feral form rotation. (4) Warlock: *Life Tap* fires at the
medium-mana line (default 40, health floor unchanged) at NORMAL+2, above the
dot upkeep it feeds, instead of waiting until 15% and wanding the rest of
the fight.

Source repository: `mod-playerbots/mod-playerbots`

Source commit: `b6696bdbd3740e575598d167d69f39f68cc0b907` (local
`playerbots-references/mod-playerbots` checkout).

Source files:
- `src/Ai/Class/Rogue/Strategy/DpsRogueStrategy.cpp:129-137` (`target with combo points almost dead` -> eviscerate HIGH+2, no CP gate)
- `src/Ai/Class/Hunter/Strategy/GenericHunterStrategy.cpp:71` (`medium threat` -> feign death 35)
- `src/Ai/Class/Druid/Strategy/BalanceDruidStrategy.cpp` (ranged defaults + no sub-100% heal trigger; rejuv gate is a local 1.12 adaptation)
- `src/Ai/Class/Warlock/Strategy/AfflictionWarlockStrategy.cpp` + `src/Ai/Class/Warlock/Strategy/GenericWarlockStrategy.cpp:22-30` (life-tap mana<85% at relevance 95)

Copied / ported / independently reimplemented: reimplemented against the
live list-based engine (NOT the unregistered new-style forward-ports:
`GenericMageStrategy`, `GenericWarlockStrategy`, `DpsRogueStrategy`,
`TankWarriorStrategy` remain untouched dead code). Rogue: new
`AlmostDeadFinisherTrigger` (1+ CP, target <=25%, eviscerate ready) instead
of the donor's group-DPS lifetime estimate — no `estimated lifetime` value
is registered in our context, and a flat execute band matches the existing
`target critical health` (20%) conventions nearby. Hunter: remapped the live
`medium threat` node to the existing `feign death threat` action node
(stand-up included); distracting-shot action kept for manual use. Druid: new
`InFeralFormTrigger` (Bear/Dire Bear/Cat aura state) + relevance-only
stand-down node carrying the existing `melee` default action; healer-party
behavior unchanged (restoration kit + offheal untouched). Warlock: threshold
mediumMana (40) rather than donor 85% — a 1.12 leveling adaptation keeping
the health floor; relevance NORMAL+2 above dots, below execute.

Reason: night2 rotation research (report-class-rotations.md D2/D3/D1/D4):
rogues never landed eviscerate (CP died with the mob), hunters taunted on
medium threat and died (9.7k deaths), 10+ druids chain-cast rejuvenation at
chip damage in the sub-10 kit (4/7 live druids mid-rejuv, 80 deaths/capita),
warlocks OOM-wanded the second half of every fight.

Local validation: `python3 tools/verify_okf.py` + `bash tools/verify_all.sh`
green on each of the four commits; `git diff --check` clean. No build (per
task constraints); live in-game check pending: rogue eviscerate in combat
last_action distribution, hunter feign rows + falling death rate, druid
rejuv share collapse, warlock tap-before-wand ordering.

## Combat rotation ports batch 2: mage blink-back, warrior rage/stack discipline, paladin builder, priest fade, shaman strike order (night2 rotations) — 2026-10-08

Feature: four small donor-parity rotation fixes, the honorable mentions of
night2 research. (1) Mage: new `BlinkBackTrigger` (live melee target inside
8 yd, blink off cooldown, not rooted/stunned) drives a HIGH+5 `blink` node
in the live base combat list, below the EMERGENCY root/stun blink and the
cc-strategy frost-nova pack root — a mob walking up to the mage now eats a
blink, then the nuke loop resumes. (2) Warrior: `HeroicStrikeTrigger` holds
heroic strike until 60 rage for every spec (the old 15-rage floor for
untalented levelers starved slam/shield-slam/MS/BT above it), and
`SunderArmorDebuffTrigger` stops at a full 5-stack (re-sunder only to
refresh). (3) Paladin: `crusader strike` promoted to NORMAL+2 above `holy
strike` NORMAL+1 in the live ret list — main builder first, seal/judge
upkeep untouched. (4) Priest: `medium threat` -> `fade` at HIGH in the base
combat list (any group; the action's group requirement keeps solo priests
on heals), alongside the existing raid EMERG-adjacent node. (5) Shaman:
`stormstrike` promoted to NORMAL+2 above the shield-consuming `lightning
strike` NORMAL+1, so the nature-vulnerability debuff lands first.

Source repository: `mod-playerbots/mod-playerbots`

Source commit: `b6696bdbd3740e575598d167d69f39f68cc0b907` (local
`playerbots-references/mod-playerbots` checkout).

Source files:
- `src/Ai/Class/Mage/Strategy/GenericMageStrategy.cpp:105` (`enemy too close for spell` -> blink back 35)
- `src/Ai/Class/Warrior/Strategy/TankWarriorStrategy.cpp:185-191,331-335` (slam HIGH+2 above high-rage-gated heroic)
- `src/Ai/Class/Paladin/Strategy/DpsPaladinStrategy.cpp:104-111` (crusader strike default+0.4 second builder)
- `src/Ai/Class/Priest/Strategy/GenericPriestStrategy.cpp:21` (medium threat -> fade 55)
- `src/Ai/Class/Shaman/Strategy/EnhancementShamanStrategy.cpp` (stormstrike top of default chain)

Copied / ported / independently reimplemented: reimplemented against the
live list-based engine; the dead new-style forward-ports stay untouched.
Mage blink id 1953 checked via `sServerFacade.IsSpellReady`; the 8 yd band
mirrors the hunter dead-zone hysteresis already in-tree. Crusader Strike
verified as an already-wired live trigger + action node
(`CrusaderStrikeTrigger` CD_TRIGGER, `CastCrusaderStrikeAction` melee
spell); only the priority moved. Skipped as not-small in this batch: mage
fire-immune fallback, scorch exclusivity/HP gate, blizzard 10s gate (D5
M-items); prot disarm/block/panic/intervene/overpower/thunder items, arms
death-wish/stance/execute (D6 S-items beyond the two rage/stack gates); ret
exorcism split, double-bubble, blessing refresh (D7); priest mana recovery,
ranged default, self-shield (D8); shaman totem bloat, earthbind, heal
protection (D9).

Reason: night2 rotation research (report-class-rotations.md D5-D9): mages
died to even-level melee with no escape (+0 killer gap, 10.9k deaths),
heroic spam starved slam while sunder stacked forever, crusader strike
fired last, priests never faded outside raids (11.3k deaths, most of any
class), lightning strike burned the shield before the nature debuff.

Local validation: `python3 tools/verify_okf.py` + `bash tools/verify_all.sh`
green on each of the commits; `git diff --check` clean. No build (per
task constraints); live in-game check pending: mage blink rows vs melee
deaths, heroic/slam cast split + sunder aura stacks capped at 5, ret
builder split, priest fade rows in groups, enh opener order.

## Combat rotation ports batch 3: prot thunder/disarm, mage nova gate, hunter wing clip (night2 rotations) — 2026-10-09

Feature: four small donor-parity rotation fixes, party-play first. (1)
Warrior: `medium rage available` -> `thunder clap` at HIGH+1 in the live
prot combat list — the base AoE tree gates thunder clap behind the opt-in
aoe toggle, so party-pull tanks never clapped; 40+ rage sits with
sunder/revenge below slam. (2) Warrior: prot `disarm` NORMAL -> HIGH+1,
where it can actually win a relevance contest (mitigation). (3) Mage: new `CastFrostNovaAction::isUseful`
veto (already-frozen target via `sServerFacade.IsFrozen`, freeze-immune
target via `IsImmuneToSpellEffect` over the spell effects), so the GCD
goes to damage instead of a wasted re-nova. (4) Hunter: `wing clip` as
second NextAction under `raptor strike` on the live `enemy is close` node
(donor melee chain order), so a mob that closes in eats the snare.

Source repository: `mod-playerbots/mod-playerbots`

Source commit: `b6696bdbd3740e575598d167d69f39f68cc0b907` (local
`playerbots-references/mod-playerbots` checkout).

Source files:
- `src/Ai/Class/Warrior/Strategy/TankWarriorStrategy.cpp:209-216` (disarm HIGH+1) and `:338-345` (medium rage -> thunder clap HIGH+1)
- `src/Ai/Class/Mage/MageActions.cpp:67-78` (`CastFrostNovaAction::isUseful`: not-frozen, no freeze-mechanic immunity, 10 yd)
- `src/Ai/Class/Hunter/Strategy/GenericHunterStrategy.cpp:80-82` (melee chain: mongoose bite 22, wing clip 21)

Copied / ported / independently reimplemented: reimplemented against the
live list-based engine; the dead new-style forward-ports stay untouched.
Thunder Clap 6343/8198/8204/8205 (Battle+Defensive stances) and Wing Clip
2974/14267 verified in Turtle `tw_world_spell_template.sql`; disarm 676
is a pre-existing live trigger/action pair. Skipped in
this batch (already live, not small or not a clear gain): rogue expose
armor above the damage finishers (a solo-levelling DPS loss on trash), warrior overpower (live twice:
arms HIGH + prot stance-dance; donor taste-for-blood path is WotLK-only),
paladin blessing refresh (live blessing-on-party ladder is a superset of
the donor per-buff strategies), priest inner fire upkeep + self-shield
(both live; no melee-gated shield donor source exists), shaman earthbind
vs fleeing (no donor wiring — donor snares fleeing via frost shock; novel
totem-slot behavior), mage scorch exclusivity/HP gate (donor list is WotLK
raid-debuff homogenization), hunter/warlock pet sanity (live pet-attack
trigger strictly stronger than donor; donor comments it out of combat),
fear ward on main tank (needs a new main-tank target value — not small).

Reason: night2 rotation research (report-class-rotations.md D3/D5/D6):
prot tanks with zero thunder-clap coverage in normal pulls, disarm never
firing at NORMAL, nova GCDs
wasted on frozen mobs, hunters with a registered-but-unpushed wing clip.

Local validation: `python3 tools/verify_okf.py` + `bash tools/verify_all.sh`
green on each of the commits; `git diff --check` clean. No build (per
task constraints); live in-game check pending: prot thunder-clap cast
share, disarm rows, nova casts per frozen
target, wing-clip casts in melee.

## Ranged party keep-away (caster steps out of melee toward tank) — 2026-10-09
Feature: non-hunter casters in a group step out when a mob is in melee
reach of THEM even while the tank holds it. Donor `enemy too close for
spell` fires at melee range regardless of victim
(`src/Ai/Base/Trigger/RangeTriggers.cpp:14-18`); the module's victim gate
(`RangeTriggers.h`, "casters flee only when the mob targets them") means a
tank-held mob standing on the mage/priest/lock never fires it, so the
caster stands in melee and eats cleaves.

Source repository: `mod-playerbots` @
`b6696bdbd3740e575598d167d69f39f68cc0b907` (local checkout
`../playerbots-references/mod-playerbots`).

Source files (donor, reference only):
`src/Ai/Base/Strategy/RangedCombatStrategy.cpp:10-16` (`enemy too close for
spell` -> flee) + `src/Ai/Base/Actions/MovementActions.cpp:1367-1399`
(victim==bot -> flee to tank) + `src/Ai/Base/Trigger/RangeTriggers.cpp:14-18`
(victim-independent melee-range fire condition).

Copied / ported / reimplemented: reimplemented as a narrow gate inside the
live `EnemyTooCloseForSpellTrigger`, ahead of the victim gate: non-hunter,
grouped, mob in melee reach of this bot (`CanReachWithMeleeAutoAttack`),
live same-map tank groupmate (`LiveGroupMembers` + `ai->IsTank`) -> true.
The existing `flee` action then runs to the tank (victim==bot branch,
`MovementActions.cpp:1824-1837`) or steps out via FleeManager when no tank
is near. Deliberately narrower than donor: no trigger when solo (slow-kite
guard still refuses: chasing a mob you cannot outrun only stops the casts),
no trigger for hunters (own dead-zone trigger), melee-reach instead of the
spell-band fraction so it is a short step out of melee, not a long kite.
Solo pool bots and hunters: unchanged.

Reason: night2 party-combat gap 6 ("ranged has no keep-away/flee-to-tank
equivalent of donor `enemy too close`"): the base flee row + flee-to-tank
endpoint were already ported (prior "keep-away verification" row), only the
firing condition was missing for the tank-holds-it case.

Local validation: `bash tools/verify_all.sh`; `git diff --check`. No build
(per task constraints); live in-game check pending: party caster steps out
of melee toward the tank while the tank holds the mob, no long kite, solo
casters unaffected.

## Idle wander beside the prey rule (night2 idlefallback) — 2026-10-09
Feature: `idle wander` no longer waits for the grind target to come back
empty. Donor `mod-playerbots` idles through local motion (`NewRpg`
`WanderRandom`/`WanderNpc` + `MoveRandomNear`, `getNewTarget` falling
through to local grind/rpg/wander instead of parking); the module gated
its drift on no-grind-target-at-all, so a held-but-never-attacked pick
(out of the front arc, leader travelling, tapped since the pick) vetoed
the only motion that could break the standstill — live night2 pool: 0
wander rows for ~289 parked purpose-None bots while `attack anything`
refused the held prey. The drift (50 yd, relevance 0.6, `often` trigger,
mesh-vetted reachable point + ordinary core path) now fires beside the
prey rule; the attack row (5.0) still wins whenever the prey is usable.
No travel destination is touched, no park is re-armed: unreachable spots
cannot be re-picked by this change. Pool (masterless random) bots only.

Source repository: `mod-playerbots` @
`b6696bdbd3740e575598d167d69f39f68cc0b907` (local checkout
`../playerbots-references/mod-playerbots`).

Source files (donor, reference only):
`src/Ai/Base/Actions/ChooseTravelTargetAction.cpp:42-178`
(`getNewTarget` fallthrough to `SetGrindTarget`, idle only if grind fails)
+ `src/Ai/World/Rpg/Action/NewRpgBaseAction.cpp:240-280`
(`MoveRandomNear`) and `:964-1062` (`SelectRandomGrindPos` local window).

Copied / ported / reimplemented: reimplemented (one predicate parameter
dropped in `GrindSpotPolicy.h::IdleWanderAllowed` + call site in
`IdleWanderAction::isUseful`; trigger row unchanged).

Reason: night2 idlefallback quantify — 289 purpose-None stalled bots, 123
earning no XP in 12 min, wander firing 0x pool-wide while the local prey
path served the other 166.

Local validation: `tools/test_grind_spot_policy.cpp` test 10 (gate beside
the prey rule); `bash tools/verify_all.sh`; `python3 tools/verify_okf.py`;
`git diff --check`. No build/deploy (orchestrator compiles); live check
pending: `idle wander` rows for parked purpose-None bots, stalled share,
no change in grind pick/re-park rates.


## Party buffs batch 2: blessing claim + in-combat motw/AI/spirit fallback — 2026-10-09
Feature: two paladins no longer double-cast the same blessing on one member,
and mark of the wild / arcane intellect / divine spirit land in the quiet
moments of long fights. Native work (no donor port): the blessing picker was
a bare CastSpellAction with no claim, and only priest fortitude had an
in-combat fallback row.

Source files (module, modified):
`ai/playerbot/strategy/paladin/PaladinActions.{h,cpp}`
(`CastBlessingOnPartyAction`: `isUseful` stands down while another bot holds
a live claim on the resolved blessing+target, `Execute` claims the resolved
pair only on a cast that actually starts) +
`ai/playerbot/strategy/{druid/DruidStrategy,mage/MageStrategy,priest/PriestStrategy}.cpp`
(combat fallback rows for the single-target party buffs at ACTION_DEFAULT,
the priest-fortitude shape: below every heal and attack).

Copied / ported / reimplemented: reimplemented from the module's own
patterns. Claim reuses the shared BuffClaimRegistry (4 s TTL) exactly like
CastBuffSpellAction::Execute (stamp on cast start, never on a whiff).
Fallback rows reuse the existing fortitude precedent (plain combat row at
ACTION_DEFAULT, no master/mana gate in the row: the buff trigger's own aura
gate plus the upkeep mana floor and retry cooldown in CastBuffSpellAction
already decide whether the cast is worth it). Cure tiering (task item 3)
deliberately untouched: live paladin/druid/shaman rows already match the
donor's flat self/party split (donor tiers nothing across dispel types).
Buff rank by target level (task item 4) skipped: no rank-selection helper
exists (SpellIdValue only reads the global mana save level), plumbing target
level through is not small.

Reason: night2 buffs-live findings 1+4: chain-pulling masters starve every
party buff except fortitude (follow beats out-of-combat buffs; combat rows
were empty), and two paladins resolve the same member through the same
shared value with no cross-bot coordination.

Local validation: `bash tools/verify_all.sh`; `git diff --check`. No build
(per task constraints); live in-game check pending: blessing double-cast
rate with two paladins, buffs-active share across long fights.

## Party gaps round 2: pre-pull RTI marks + between-pull regen wait (night2 partygaps2) — 2026-10-09
Feature: two small donor-parity party fixes from night2 research. (1) RTI
marks resolve pre-pull: `RtiTargetValue::Calculate` no longer requires the
marked unit to sit in "possible targets" (units already fighting the bot);
it accepts a marked unit on legality + sight range + LOS instead. The
mage's moon-sheep and the party's skull pre-focus now work on approach,
not only after someone takes a hit. (2) The party waits for regen between
pulls: `GroupReadyValue` drops the live `hasAttackers` conjunct on the
health wait, restoring the donor's unconditional between-pull hold, so
travel/grind/RPG movement stays parked while members sit wounded or OOM
and the party drinks/eats together.

Source repository: `mod-playerbots` @
`b6696bdbd3740e575598d167d69f39f68cc0b907` (local checkout
`../playerbots-references/mod-playerbots`).

Source files (donor, reference only):
`src/Ai/Base/Value/RtiTargetValue.cpp:35-78` (marked-unit resolution with
the attackers gate deleted, LOS + sight range + master-distance guards) +
`src/Ai/Base/Value/GroupValues.cpp:134-174` (`GroupReadyValue`, no
attacker gate on the health/mana wait).

Source files (module, modified):
`ai/playerbot/strategy/values/RtiTargetValue.h` (pre-pull resolution via
`PossibleAttackTargetsValue::IsPossibleTarget` sight-range/legality check
+ `IsWithinLOSInMap`; the old possible-targets membership test removed) +
`ai/playerbot/strategy/values/GroupValues.cpp` (`GroupReadyValue` health
wait without the `hasAttackers` conjunct; in-combat skip, mana gate,
dungeon alive-gate and master-distance skip unchanged).

Copied / ported / reimplemented: reimplemented inside the live values. The
donor's master-distance chase guard is already covered live by the
master-distance member skip above the wait. The donor's 2D range shape is
already covered live by `IsPossibleTarget`'s `IsWithinDistInMap`.

Reason: night2 partygaps2 research: with prior merges (threat, interrupts,
tank-face, formation, healer mana, buff claim) landed, the two most visible
remaining party gaps were CC/focus marks ignored before the pull and bots
walking on while the party sat to drink.

Local validation: `bash tools/verify_all.sh`; `git diff --check`. No build
(per task constraints); live in-game check pending: moon-sheep on approach,
skull pre-focus before first hit, party idle between pulls until topped up.

## Party gaps round 3: warlock CC type gates, resto chain-lightning range, healer LOS tie-break, balance hurricane (night2 partygaps3) — 2026-10-09
Feature: four small donor-parity party fixes from night2 research. (1)
Warlock CC legality: `CastFearOnCcAction::isPossible` refuses undead and
mechanical marks, `CastBanishOnCcAction::isPossible` accepts only demon
and elemental marks (players excluded outright: `GetCreatureType` answers
with the shapeshift form for players, so a bear-form druid reads as
beast). The core rejects illegal types, so firing there wasted mana and
the GCD. (2) Resto healer-DPS chain lightning: the dead-tree row queued
bare `medium aoe and healer should attack`, which has no creator (a
`TwoTriggers` on an unregistered name is a silent no-op); registered the
`ranged medium aoe and healer should attack` combo (3 attackers, spell
range — the melee one is PBAoE range, wrong for a 30 yd cast) and pointed
the row at it. (3) Healer target pick: `PartyMemberToHeal` tie-breaks the
missing-health sort on LOS — when the most urgent member is out of LOS and
another is within 30% of the top target's max health (the medium-health
band width), the reachable one goes first. Never a filter: a dying member
behind a pillar still outranks a scratched one in the open, and the reach
action still walks the healer into LOS for genuinely urgent picks. (4)
Balance hurricane: the only rows lived in the dead vector-style
`GenericDruid` tree (bare `medium aoe` has no creator); added one
`ranged medium aoe` (3 attackers, spell range) row to the live
`BalanceDruidStrategy`, covering pve/pvp/raid via inheritance. Gap 3
(auto-mark combat gate) confirmed moot: `MarkRtiStrategy` only queues on
`no rti target` in combat.

## Held pick outranks a new errand, starved bots only (night2 heldprey2) — 2026-10-09
Feature: `TravelActionMultiplier` vetoes travel request actions while the
bot holds a grind pick and has no active travel target, but only once the
bot's own recent travel searches prove starved: three or more of the
twelve counted purposes (quest errand + every numeric travel purpose -
Grind, GenericRpg, Explore, GatherMining/Herbalism/Fishing, Boss, Vendor,
Repair, AH, Mail) still inside their `no travel purpose until::<key>`
park. A successful pick sets no park, so a bot whose searches succeed
keeps questing exactly as today - its requests return 1.0 here. Named
errands (trainer class, city, ...) stay out of the count: their parks are
common on healthy bots (a trainer with nothing affordable parks ten
minutes), and counting them would veto questing bots that are succeeding
everywhere else. Healers without `offdps` exempt (they start no fights);
travelling bots untouched (the hasTarget veto still owns those ticks);
owned/hired bots keep today's order (player-ordered journeys). Scoped
down from round 1 (`agent/heldprey` 41990572, NOT merged), which vetoed
on the held pick alone and would have pinned a bot that just finished a
trip wherever mobs stand - no quests, trainers, vendors. Pure predicate
(`TravelSearchesStarved` + `TravelStarvedParkKeys`,
`TRAVEL_STARVED_PARKED_PURPOSES = 3`) in `TravelRepickPolicy.h`, tested
in `tools/test_travel_repick_policy.cpp`; both veto reads are
already-cached values (grind pick 2 s, manual timestamps free), no extra
world scan.

Source repository: `mod-playerbots` @
`b6696bdbd3740e575598d167d69f39f68cc0b907` (local checkout
`../playerbots-references/mod-playerbots`).

Source files (donor, reference only):
`src/Ai/Class/Warlock/WarlockActions.cpp:33-63` (banish demon/elemental
only, fear not on mechanical/undead) +
`src/Ai/Class/Shaman/Strategy/RestoShamanStrategy.cpp:63` (chain
lightning on `medium aoe and healer should attack`) +
`src/Ai/Base/Value/PartyMemberToHeal.cpp:124-136` (`Check`: same map, not
charmed, 2x heal distance, in LOS) +
`src/Ai/Class/Druid/Strategy/GenericDruidStrategy.cpp:170-179`
(hurricane on `medium aoe`) + `src/Ai/Base/TriggerContext.h:102,106,313`
(`medium aoe` = 3 attackers at 8 yd; the healer combo).

Source files (module, modified):
`ai/playerbot/strategy/warlock/WarlockActions.h` (both OnCc `isPossible`
gates) + `ai/playerbot/strategy/triggers/TriggerContext.h` (registered
the ranged healer combo) +
`ai/playerbot/strategy/shaman/RestoShamanStrategy.cpp` (row retargeted) +
`ai/playerbot/strategy/values/PartyMemberToHeal.cpp` (LOS tie-break after
the missing-health sort, before the multi-healer spread) +
`ai/playerbot/strategy/druid/BalanceDruidStrategy.cpp` (hurricane row) +
`docs/classes/warlock.md`, `docs/classes/druid.md` (behaviour lines).

Copied / ported / reimplemented: reimplemented in place. Deviations from
the donor, all deliberate: (a) donor's heal `Check` is a hard LOS filter;
live keeps out-of-LOS members as candidates (the reach action closes the
gap) and uses LOS only as a bounded tie-break, so a dying tank behind a
pillar is never ignored; (b) donor's chain-lightning trigger is the bare
`medium aoe` name (WotLK tree still registers it); live has only
ranged/melee splits, so the row keys off `ranged medium aoe`; (c) donor's
hurricane rows sit in its generic tree; live's generic druid tree is dead,
so the row goes on the live balance strategy. Turtle creature types match
the donor 1:1 (`SharedDefines.h`: demon 3, elemental 4, undead 6,
mechanical 9); all four spells/mechanics exist in 1.12.

Reason: night2 partygaps3 research: after parts 1-2 (marks, regen, solo
guard) these were the remaining ranked small items — wrong-CC-type casts,
a healer-DPS row that could never fire, LOS-blind heal picks, and a
missing balance pack cast.

Local validation: `bash tools/verify_all.sh`; `git diff --check`; wiring
audit `python3 tools/verify_action_trigger_wiring.py` (0 live-missing
before and after; `medium aoe and healer should attack` refs drop from 2
to 1 — the remaining one is the donor-faithful priest row in the dead
`GenericPriestStrategy.cpp`). No build (per task constraints); live
in-game check pending: no fear on undead/mechanical marks, no banish on
non-demon/elemental, resto chain lightning on ranged packs when nobody
needs healing, reachable-first heal picks, balance hurricane at 3+.

`src/Ai/Base/Strategy/GrindingStrategy.cpp:23-25`
(`no target` -> `attack anything` 4.0, no per-tick travel competition) +
`src/Ai/Base/Actions/ChooseTargetActions.cpp:88-102`
(`AttackAnythingAction::Execute` carries the approach: sets `pull target`,
clears the motion master, never breaks the walk).

Copied / ported / reimplemented: reimplemented (starved-gated veto branch
in `TravelActionMultiplier::GetValue`,
`ai/playerbot/strategy/generic/TravelStrategy.cpp`; local shape only — the
donor needs no equivalent because its travel decisions run on the manager
sweep, not in the per-tick queue).

Reason: night2 heldprey quantify — post-01:21-UK build, 01:24/01:32 snapshots
(2000/1996 bots): 41 purpose-None bots still 8 min apart, 16 earning XP via
the local path, ~5 attack orders per 40 still bots per 10 min while
`TravelSearchEmpty` rows rotate across purposes (empty/rejected) and zero
sub-6.x actions win; `EvadeProbe` 0 rows on the cohort (no wedged orders —
the picks are ordinary, the rank is the block).

Local validation: `bash tools/verify_all.sh`; `git diff --check`. No
build/deploy (orchestrator compiles); live check pending:
`AttackAnythingAction` rows for standing purpose-None bots with picks,
stalled-None share, no rise in over-level orders (level cap + adds gates
unchanged), `QuestRewarded` lag only on the no-XP cohort.

## Loot open-failure give-up (night2 lootfrozen) — 2026-10-09
Feature: `OpenLootAction::Execute` (`ai/playerbot/strategy/actions/LootAction.cpp`)
counts each failed open on the loot stack's existing per-corpse failure
memory (`LootObjectStack::NoteApproachFailure`, the same counter `move to
loot` uses for unreachable corpses) and drops the corpse once it is
abandoned (3 failures inside the 120 s window), clearing `loot target`
so `loot` selects the next corpse. Covers every `DoLoot` "not now"
path that previously retried forever with no counter: failed
open/skin/gather casts, a contended game object, no opening spell.
Player-ordered looting (`.bot` loot orders via `ChatCommandHandlerStrategy`)
issues the action once per order and can never fill a counter that
needs repeated failures. Documented in
`docs/concepts/bot-mechanics-and-quirks.md` (Loot Open-Failure Give-Up).

Source repository: `mod-playerbots` @
`5397110` (local checkout
`../playerbots-references/mod-playerbots`, read-only) —
`src/Ai/Base/Actions/LootAction.cpp` (open removes the corpse only on
success; no per-object attempt counter or timeout anywhere in the
donor loot chain) + `src/Mgr/Item/LootObjectStack.cpp` (stack-wide 30 s
TTL + 200-cap eviction only). No donor behavior to port: the donor has
the same unbounded retry on failed opens; the counter reuses our own
`MoveToLootAction` abandonment shape.

Reason: night2 lootfrozen live measure — two 120 s-apart dashboard
snapshots (01:30/01:33 UTC, 1999 bots) + `bot_events.csv` window:
30-44 of ~550-740 alive-out-of-combat frozen bots carry a loot
last_action, but only 6 persist across both snapshots and 5 of those
log loot progress in-window (StoreLoot/GatherLoot/LootMoney —
skinning chains and multi-corpse clears, not wedges). The hard wedge
is rare (~2/2000: `open loot`/`can loot` pinned 240 s+ with zero
displacement and no loot events). Bag-full is lossy but not a wedge
(per-item skip + release); solo pool bots show no group roll waits.

Local validation: `bash tools/verify_all.sh`; `python3
tools/verify_okf.py`; `git diff --check`. No build/deploy
(orchestrator compiles); live check pending: `open loot` last_action
share of frozen bots, `giving up on guid=... after repeated failed
opens` debug rate, no change to player-ordered loot completion.

## Rogue low-health vanish fallback (night2 deaths) — 2026-10-09
Feature: the live rogue low-health node was evasion -> feint. Evasion keeps
the kill when ready, but on its 5-minute cooldown (death loops hit the same
bot within 600 s 36-41% of the time) the bot feinted - a threat drop with
no tank to take over for a solo pool bot - and died. Vanish now sits
between them (evasion, vanish, feint): grouped rogues keep today's order
(evasion first, feint last for the tank save); solo rogues with evasion
spent break combat instead of dying. No new actions/triggers; same trigger,
same EMERGENCY relevance; untrained rogues fall through to feint via the
existing impossible-action path.

## Party gaps round 4: shaman off-target interrupt, cat cower in parties, paladin self-first cleanse (night2 partygaps4) — 2026-10-09
Feature: three XS donor-parity party fixes from night2 research. (1)
Shaman interrupt: new `EarthShockInterruptEnemyHealerSpellTrigger`
(`InterruptEnemyHealerTrigger` on "earth shock") + `earth shock on enemy
healer` row at ACTION_INTERRUPT+2 in base `ShamanStrategy`, so shamans
interrupt a second attacker casting a heal like every other interrupt
class (warrior/rogue/mage/druid/warlock all had the pair). (2) Cat
threat: `medium threat` -> `cower` moves from `DpsFeralDruidRaidStrategy`
to base `DpsFeralDruidStrategy`, so 5-man/party cats back off like the
donor (raid inherits). (3) Paladin cleanse: self rows at ACTION_DISPEL+2
above party rows at +1, matching the donor stagger (was: all six flat at
DISPEL).

Source repository: `mod-playerbots` @
`b6696bdbd3740e575598d167d69f39f68cc0b907` (local checkout
`../playerbots-references/mod-playerbots`).

Source files (donor, reference only):
`src/Ai/Class/Rogue/Strategy/DpsRogueStrategy.cpp:141` (vanish on medium
threat) + `:150` (evasion HIGH+9 / feint HIGH+8 on low health). Deviations,
deliberate: donor vanishes on threat (group-tank context); solo pool bots
have no threat signal worth reacting to, so the low-health band carries it,
behind evasion so winnable fights still end in kills, not resets.

Reason: night2 deaths research — post-02:20-UK build pool telemetry:
rogue 2.20/bot-h (#2 killer after mage 3.76), 173 fair-fight (<=+4, fought)
deaths/h with killer left at 55% HP; evasion casts pool-wide while vanish
casts zero (both self-casts, identically observable in bot_events.csv
SelfBuff rows), rogues dying with evasion up pre-death. The critical-band
blind -> vanish chain exists but never executes a vanish live.

Local validation: `bash tools/verify_all.sh`; `git diff --check`. No
build/deploy (orchestrator compiles); live check pending: `vanish` rows in
bot_events.csv SelfBuffs, rogue deaths/bot-hour, repeat-death share for
rogues (41% pool-wide within 600 s), adds>0 share must stay flat (vanish
pulls nothing new).

`src/Ai/Class/Warrior/WarriorTriggers.h:57-62` +
`src/Ai/Class/Warlock/WarlockTriggers.h:154-158` (interrupt pairs incl.
healer variants; donor shaman interrupt is WotLK-only wind shear
`src/Ai/Class/Shaman/ShamanTriggers.h:141-151`, no 1.12 equivalent — live
earth shock covers it) +
`src/Ai/Class/Druid/Strategy/CatDruidStrategy.cpp:194-197` (cower at
medium threat in base combat) +
`src/Ai/Class/Paladin/Strategy/GenericPaladinStrategy.cpp:40-54` (cleanse
self DISPEL+2 above party DISPEL+1).

Source files (module, modified):
`ai/playerbot/strategy/shaman/ShamanTriggers.h`,
`ai/playerbot/strategy/shaman/ShamanActions.h`,
`ai/playerbot/strategy/shaman/ShamanAiObjectContext.cpp`,
`ai/playerbot/strategy/shaman/ShamanStrategy.cpp` (healer trigger, action,
creators, row) + `ai/playerbot/strategy/druid/DpsFeralDruidStrategy.cpp`
(cower row moved to base) +
`ai/playerbot/strategy/paladin/PaladinStrategy.cpp` (cleanse stagger) +
`docs/classes/shaman.md`, `docs/classes/druid.md`,
`docs/classes/paladin.md` (behaviour lines).

Copied / ported / reimplemented: reimplemented in place. Deviations from
the donor, all deliberate: (a) earth shock keeps its debuff-spell action
class (`CastRangedDebuffSpellAction`) — only the targeting comes from the
new healer action; (b) no new spells — all three fixes are row/trigger
wiring on existing 1.12 spells; (c) the cross-cutting heal-vs-dispel
priority inversion (live dispels 50-53 lose to heals 60-82; donor dispels
beat heals) is recorded but NOT changed — heal-first may be intended
Tortoise tuning, needs owner call. Deferred: mage/warlock threat dumps
(no 1.12 mirror image/soulshatter), tank-aggro open gating (neither tree
has it), formation spread (no donor-portable trigger set live).

Reason: night2 partygaps4 research: interrupts/dispels/threat/positioning
comparison found rogue/mage/druid interrupts at parity (cc is default-on
for all classes), dispel coverage at parity, melee-behind and ranged
band-keeping at parity; these three were the ranked XS gaps.

Local validation: `bash tools/verify_all.sh`; `git diff --check`; wiring
audit via verify_all (0 live-missing). No build (per task constraints);
live in-game check pending: shaman earth shocks off-target healers, cat
cowers in 5-mans, paladin cleanses self first.

## Workidle empty-destination release (2026-10-09)

Donor: mod-playerbots NewRpg (`b6696bdbd3740e575598d167d69f39f68cc0b907`):
`src/Ai/World/Rpg/Action/NewRpgAction.cpp:248-256` (GO_GRIND returns to
WANDER_RANDOM on arrival; GO_CAMP to WANDER_NPC),
`src/Ai/World/Rpg/Action/NewRpgAction.cpp:375-410`
(NewRpgWanderNpcAction returns to IDLE when no NPC can be found),
`src/Ai/World/Rpg/Action/NewRpgAction.cpp:528-551,622-629`
(5-min no-progress POI verdict marks the quest low-priority and returns to
IDLE), `src/Ai/World/Rpg/Action/NewRpgBaseAction.cpp:1223-1230`
(WANDER_RANDOM requires a live grind target; IDLE re-rolls).

Source files (module, modified): `ai/playerbot/WorkIdlePolicy.h` (new pure
rule: 30 s horizon, anchor upkeep, stale verdict),
`ai/playerbot/TravelMgr.cpp` (CheckStatus WORK release for masterless pool
bots), `tools/test_work_idle_policy.cpp` (new standalone test) +
`docs/concepts/bot-mechanics-and-quirks.md`, `CHANGELOG.md` (doc lines).

Copied / ported / reimplemented: reimplemented in place. Deviations from
the donor, all deliberate: (a) no IDLE/WANDER state machine exists here, so
the release expires the travel target (TRAVEL_STATUS_EXPIRED, like the
GrindSpotOutgrown/quest-errand expiry) and the next visit requests a new
one; (b) the verdict is time-based (~30 s, three pool visits) on the
already-cached "grind target" pick plus the core attacker set, not a
destination re-search, so no per-tick DB/world scans; (c) quest-objective
POI tracking stays with the existing 5-min QuestStallPolicy - this rule
only covers the nothing-to-do hold, any prey or attacker holds the stay;
(d) masterless pool bots only, owned/hired bots unchanged.

Reason: live 2000-bot pool: a masterless bot that arrives with nothing to
do holds WORK (blocks requests and idle drift) until the ~5-min timer
expires; ~20% of all stall time sits in WORK.

Local validation: `bash tools/verify_all.sh` (run before commit); `git diff
--check`. No build (per task constraints); live in-game check pending.
| Voidwalker Suffering AoE taunt + Consume Shadows self-heal (PET-8a/8c) | New behavior (neither `mod-playerbots` nor the module ordered them; rank ladders pre-existed in `runtime/PetSpellRankPolicy.h`). Suffering gated on the PET-3 taunt permission | `runtime/VoidwalkerPolicy.h` (`CanCastSuffering`: live VW + taunt allowed + 3+ attackers; `CanCastConsumeShadows`: live VW + hurt + NC + unmounted) + `tools/test_voidwalker_policy.cpp`; `ai/playerbot/strategy/warlock/WarlockTriggers.{h,cpp}`, `WarlockActions.h` (`CastSufferingAction` / `CastConsumeShadowsAction : CastPetSpellAction`, policy gates repeated for the evaluation gap), `WarlockStrategy.cpp` (PvE combat suffering node at ACTION_HIGH; shared NC consume node at ACTION_NORMAL), `WarlockAiObjectContext.cpp` (4 creators) | Reimplemented in place: Paranoia skipped as niche PvP (rank ladder suffices). Cower folded into the PET-3 PR | `bash tools/verify_all.sh`; wiring 0 live-missing; standalone `test_voidwalker_policy` (15 checks); `git diff --check`. Compile via shared builder; no live in-game test |

## WSG bodyguard + objective reset (SOC-P5/SOC-P6, 2026-10-09)

Donor: mod-playerbots (`79bd4281`):
`src/Ai/Base/Strategy/BattlegroundStrategy.cpp:24` (generic `dead` ->
`bg reset objective force`), `:31` (Warsong `team flagcarrier near` ->
`bg protect fc`), `:36` (Warsong `timer bg` -> force), Alterac `:41`
(timer bg -> force),
`src/Ai/Base/Actions/BattleGroundTactics.cpp:1631-1643` (force branch:
stop + clear motion + resetObjective unless carrying),
`src/Ai/Base/Trigger/GenericTriggers.{h:634-643,cpp:480-491}`
(TimerBGTrigger, ~60 s watchdog).

Source files (module, modified): `ai/playerbot/BgForceResetPolicy.h` (new
pure gate: in-match, out-of-combat, once per 60 s),
`ai/playerbot/strategy/actions/BattleGroundTactics.{h,cpp}` (force branch +
isUseful gate + anchor stamp),
`ai/playerbot/strategy/actions/ActionContext.h` (force creator),
`ai/playerbot/strategy/triggers/PvpTriggers.{h,cpp}` (new TimerBgTrigger),
`ai/playerbot/strategy/triggers/TriggerContext.h` (timer-bg + team
flagcarrier-near creators),
`ai/playerbot/strategy/generic/BattlegroundStrategy.cpp` (Warsong
bodyguard + timer nodes, Alterac timer node),
`ai/playerbot/strategy/generic/DeadStrategy.cpp` (death reset node),
`tools/test_bg_force_reset_policy.cpp` (new standalone test),
`tools/verify_all.sh` (register test) + `CHANGELOG.md` (doc line).

Copied / ported / reimplemented: reimplemented in place. Deviations from
the donor, all deliberate: (a) the `dead` node lives in DeadStrategy, not
the generic BG strategy - bg strategies only evaluate inside the match
while alive, so the donor's generic `dead` node would never fire here; the
dead engine is the live path; (b) the force action carries a gate the donor
lacks (in-match, out-of-combat, 60 s latch): without it the dead node would
re-roll the role and re-path every tick while corpse-running and the timer
could stop the bot mid-fight; (c) `team flagcarrier near` needed a creator
(it had none - only the commented-out node referenced it); (d) flag-carrier
check covers the two WSG flag auras (no EY/Netherstorm in 1.12).

Reason: the friendly flag carrier died undefended (guard node commented
out, trigger unwired), and bots walked back to death spots or stood on
stale objectives with no forced re-pick.
| Pet taunt situation toggle: Growl/Torment autocast off with a real tank (PET-3) + Cower threat-drop when stood down (PET-8b) | New behavior (neither `mod-playerbots` nor the module toggled taunts by situation; donor sweep enables all non-denylisted autocast per `Ai/Base/Actions/PetsAction.cpp:23-47`) | `runtime/PetTauntPolicy.h` (pure `ShouldPetTaunt` + taunt/cower rank sets) + `tools/test_pet_taunt_policy.cpp`; `ai/playerbot/strategy/actions/GenericActions.{h,cpp}` (`IsPetTauntAllowed` group/tank read, dynamic sweep denylist); `strategy/warlock/WarlockActions.h` (Torment peel comment only, ungated); `strategy/hunter/Hunter{Triggers,Actions}.h`, `HunterTriggers.cpp`, `HunterStrategy.cpp`, `HunterAiObjectContext.cpp` (`pet has aggro` trigger + `cower` pet-cast action + combat node) | Reimplemented in place: solo/tankless-group keeps taunts on (pet is the tank), grouped-with-tank turns Growl/Torment autocast off within one sweep tick while the ordered Torment peel still rescues the owner (victim==bot only, cannot steal from the tank); hunter orders Cower while the pet holds aggro anyway (autocast stays off). Explicit `.bot pet autocast` orders still win for owned/hired pets (sweep never runs there) | `bash tools/verify_all.sh`; `python3 tools/verify_action_trigger_wiring.py` (0 live-missing); standalone `test_pet_taunt_policy` (17 checks); `git diff --check`. Compile via shared builder; no live in-game test |
| Succubus Seduction as warlock CC for humanoids (PET-2) | New behavior (donor has no seduction AI; only the breakable-CC aura entry). CC flow follows the live `HasCcTargetTrigger` / `banish on cc` shape | `ai/playerbot/strategy/warlock/WarlockTriggers.h` (`SeductionTrigger`), `WarlockActions.h` (`CastSeductionOnCcAction : CastPetSpellAction` with CC target + CC flags, succubus/humanoid gate via `runtime/SeductionPolicy.h`, cached-spellId refresh), `WarlockStrategy.cpp` (`WarlockCcStrategy` node below fear at ACTION_INTERRUPT), `WarlockAiObjectContext.cpp` (2 creators), `runtime/SeductionPolicy.h` + `tools/test_seduction_policy.cpp` | Reimplemented: pet-cast instead of owner-cast (no reach prerequisite; range resolves demon→mark, so she must already be near). Break-protection via existing breakable-CC list + `CanPetAttack` gates | `bash tools/verify_all.sh`; wiring 0 live-missing; standalone `test_seduction_policy` (9 checks); `git diff --check`. Compile via shared builder; no live in-game test |
| Kel'Thuzad fight (Naxx): role-split add priorities, center gather, phase-2 ring/tank spots, fissure flee, Detonate Mana runout | `mod-playerbots` | `79bd4281` | `src/Ai/Raid/Naxx/Action/NaxxActions_Kelthuzad.cpp`, `src/Ai/Raid/Naxx/NaxxBossHelper.h` (KelthuzadBossHelper), `src/Ai/Raid/Naxx/NaxxStrategy.cpp` (KT rows) | Reimplemented trigger-driven; phase via NOT_SELECTABLE (vanilla) not NON_ATTACKABLE; Detonate 27819 added to universal bomb runout; p1 totem/pet suppression omitted; donor debuff-on-attacker + phase-2 Blizzard/Frost Nova suppression legs omitted (no local equivalent: local debuff-on-attacker actions do not retarget current target; no WotLK shackle mechanic) | IDs verified in tw_world (15990, 16427/28/29/41, 16129, 27808/10/19/12, 28408); center verified vs core pullPortal | `bash tools/verify_all.sh` + `tools/test_kelthuzad_adds_policy.cpp`; build-commit + no live test |
| Grobbulus fight (Naxx): ranged behind-boss carriers, poison-cloud step-out | `mod-playerbots` | `79bd4281` | `src/Ai/Raid/Naxx/Action/NaxxActions_Grobbulus.cpp`, `src/Ai/Raid/Naxx/NaxxStrategy.cpp` (Grobbulus rows) | Reimplemented trigger-driven; ranged-carrier row raised to reaction level (EMERGENCY+7 over universal runout, else starved); cloud step-out synthesized from generic hazard mechanics (donor's cloud trigger is the MT rotation, omitted: no MT concept, needs live ring coords); return-to-center omitted (reach-to-attack covers) | Kit verified in tw_world + core boss_grobbulus.cpp (15931, 28169, 28240, cloud 15933) | `bash tools/verify_all.sh` + `tools/test_grobbulus_cloud_policy.cpp`; build-commit + no live test |

## MC Garr AoE-off + Shazzrah 26y range (raid1 item 3) — 2026-10-09

Donor: mod-playerbots @ `79bd4281` (local checkout
`../playerbots-references/mod-playerbots`):
`src/Ai/Raid/MC/MCMultipliers.cpp:26-53` (GarrDisableDpsAoeMultiplier:
DpsAoeAction + named AoE-spell list + any ACTION_THREAT_AOE cast while Garr
lives), `src/Ai/Raid/MC/MCTriggers.cpp:27-37` (McShazzrahRangedTrigger:
ranged inside ARCANE_EXPLOSION_DISTANCE), `src/Ai/Raid/MC/MCActions.cpp:51-60`
(step out to exactly 26y), `src/Ai/Raid/MC/MCHelpers.h:38` (26y constant),
`src/Ai/Raid/MC/MCStrategy.cpp` (trigger wiring).

Source files (module, modified): `ai/playerbot/McGarrShazzrahPolicy.h`
(new pure rule: ids, 26y, Garr-suppress + Shazzrah-leave predicates),
`ai/playerbot/strategy/triggers/MoltenCoreDungeonTriggers.h`
(Garr/ShazzrahStart+EndFightTrigger on entries 12057/12264,
header-inline ShazzrahRangedTrigger: ranged + within 26y + live fight),
`ai/playerbot/strategy/actions/MoltenCoreDungeonActions.h`
(Garr/ShazzrahEnable+DisableFightStrategyAction, ShazzrahMoveAwayAction:
MoveAwayFromCreature 12264/26y), `ai/playerbot/strategy/generic/
MoltenCoreDungeonStrategies.h/.cpp` (`garr` fight strategy with
GarrAoeOffMultiplier; `shazzrah` fight strategy with a ranged 26y reaction
at ACTION_EMERGENCY+5; start triggers on `molten core`; end-fight
cleanup), `ai/playerbot/strategy/generic/DungeonMultipliers.h/.cpp`
(GarrAoeOffMultiplier), `ai/playerbot/strategy/triggers/TriggerContext.h`,
`ai/playerbot/strategy/actions/ActionContext.h`,
`ai/playerbot/strategy/StrategyContext.h` (registrations),
`tools/test_mc_garr_shazzrah_policy.cpp` (new standalone test) +
`tools/verify_all.sh` (test list), `docs/guides/dungeon-tactics.md` (doc line).

Copied / ported / reimplemented: reimplemented in our per-boss fight
strategy idiom (mirror the Magmadar pattern). Deviations from the donor,
all deliberate: (a) no per-boss multiplier plumbing in the generic raid
strategy — each fight strategy carries its own multiplier, so Garr's
AoE-off cannot leak into other fights; (b) DPS = neither tank nor heal via
our role API (no IsDps exists); the donor's named AoE-spell list is
matched by action NAME (threat flags under-mark our real AoE and
over-mark heals + single-target dots, so type matching is wrong both
ways); (c) the Shazzrah reaction node is queued only for ranged/heal bots
at strategy level (melee never sees the trigger). Note: the veto is
unconditional while Garr lives — the addon's manual AoE switch does NOT
override it on this fight (single-target discipline is the mechanic).

Reason: raid1 gaps MC-GARR-AOE + MC-SHAZZRAH: stray AoE broke Garr
Firesworn control (banish-safe single-target), and ranged ate Arcane
Explosion at 26y.

Local validation: `bash tools/verify_all.sh` (all suites incl. the new
policy test pass); `git diff --check`. Creature entries 12057/12264
verified against tw_world. Build via build-commit.sh pending; live in-game
check pending.
## Razorgore cone escape + off-tank hold (raid1 item 5) — 2026-10-09

Donor: mod-playerbots @ `79bd4281` (local checkout
`../playerbots-references/mod-playerbots`):
`src/Ai/Raid/BWL/BWLTriggers.cpp:34-39` (NotMindControlled: boss lacks
Possess 19832), `src/Ai/Raid/BWL/BWLActions.cpp:59-106` (AvoidAoe: victim
holds; in-cone within 15y steps behind — melee 3y, ranged 15y; ranged
outside cone but close backs off for War Stomp), `:108-138` (MarkBoss:
off-tank moons boss while eggs live), `src/Ai/Raid/BWL/
BWLMultipliers.cpp:20-41` (off-tank tank-assist veto while eggs live;
non-victim tanks skip Cleave-facing after), `src/Ai/Raid/BWL/
BWLHelpers.h:21,40` (Possess aura, egg GO 177807), `:58-59` geometry
constants (15y cone radius, 180-degree arc, 15y ranged, 3y melee).

Source files (module, modified): `ai/playerbot/RazorgorePolicy.h` (new
pure rule: ids, geometry, phase/escape/back-off/hold predicates),
`ai/playerbot/strategy/triggers/BlackwingLairDungeonTriggers.h`
(RazorgoreStart/EndFightTrigger on entry 12435, header-inline
RazorgoreConeTrigger with victim + Possess gates, header-inline
RazorgoreRangedTrigger), `ai/playerbot/strategy/actions/
BlackwingLairDungeonActions.h` (enable/disable actions,
RazorgoreEscapeConeAction + RazorgoreBackOffAction: MoveAwayFromCreature
12435/15y), `ai/playerbot/strategy/generic/
BlackwingLairDungeonStrategies.h/.cpp` (`razorgore` fight strategy: cone
reaction EMERGENCY+5, ranged back-off EMERGENCY+4, potion node, end-fight
cleanup, RazorgoreOffTankMultiplier), `ai/playerbot/strategy/generic/
DungeonMultipliers.h/.cpp` (RazorgoreOffTankMultiplier: first living tank
by member-slot order holds via tank-assist veto), registrations
(`TriggerContext.h`, `ActionContext.h`, `StrategyContext.h`),
`tools/test_razorgore_policy.cpp` (new standalone test) +
## Warlock Life Tap top-up + out-of-combat pre-tap (WAR-5) — 2026-10-09

Donor: mod-playerbots (`79bd4281`):
`src/Ai/Class/Warlock/WarlockTriggers.cpp:92-103` (`LifeTapTrigger`:
mana<85 with health above `LowHealth`), per-spec
`src/Ai/Class/Warlock/Strategy/*WarlockStrategy.cpp:101-117` (`life tap`
filler at 5.1),
`src/Ai/Class/Warlock/Strategy/GenericWarlockStrategy.cpp:23-30`
(`low mana` emergency tap at 95.0),
`src/Ai/Class/Warlock/Strategy/GenericWarlockNonCombatStrategy.cpp:93`
(NC pre-tap at 23.0).

Source files (module, modified): `runtime/WarlockTapPolicy.h` (new pure
two-band rule: urgent at mana<=mediumMana, top-up below 85, both gated on
health above lowHealth),
`ai/playerbot/strategy/warlock/WarlockTriggers.{h,cpp}` (existing
`LifeTapTrigger` routes through the policy urgent band; new
`LifeTapTopUpTrigger` for the 85% band),
`ai/playerbot/strategy/warlock/WarlockAiObjectContext.cpp` (registered
`life tap top-up`),
`ai/playerbot/strategy/warlock/WarlockStrategy.cpp` (out-of-combat pre-tap
rows at NORMAL-1 for both bands; no combat filler — any trigger row beats
the relevance-200 default nuke),
`tools/test_warlock_tap_policy.cpp` (new standalone test, wired into
`tools/verify_all.sh`) + `docs/classes/warlock.md`, `CHANGELOG.md` (doc
lines).
## Druid parity DRU-7: Thorns on the party tank first — 2026-10-09
Feature: new `ThornsOnTankTrigger` (`BuffOnTankTrigger` on "thorns",
fire-shield conflict skip mirroring `ThornsOnPartyTrigger`) + new
`CastThornsOnTankAction` (`BuffOnTankAction`, targets "party tank without
aura", with an explicit `getName()` override returning "thorns on tank"
— the base reports spell+" on party", which would collide with the party
blanket in queue dedup and failure backoff) + non-combat row `thorns on
tank` at ACTION_NORMAL+3 in `DruidBuffStrategy`, above the party blanket
at +2 (same BuffOnTank shape as priest PRI-1 `fear ward on tank`,
verified on the PRI-1 branch).
## Far-away leave (SOC-G2, 2026-10-09; SOC-G4 rejected, 2026-10-10)

Donor: mod-playerbots (`79bd4281`):
`src/Ai/Base/Actions/LeaveGroupAction.cpp:156-159` (different map or
distance >= 2xRpgDistance -> leave).
SOC-G4 (`src/Ai/Base/Trigger/LfgTriggers.cpp:12-16`,
`src/Ai/Base/Strategy/LfgStrategy.cpp:15-16`) was ported in the first
version of this PR and removed on review: `PlayerbotAI::DoNextAction`
already yields leadership to any in-world real player every tick (broader
than the seldom node, which could never observe its precondition), and the
node lacked the deliberate `dungeonCrew` exemption while its inherited
`Execute` reset strategies mid-dungeon. No donor behavior is lost.

Source files (module, modified): `ai/playerbot/GroupHygienePolicy.h` (new
pure far-away gate),
`ai/playerbot/strategy/actions/LeaveGroupAction.{cpp}` (far-away clause in
LeaveFarAwayAction::isUseful, routed through the policy),
`tools/test_group_hygiene_policy.cpp` (new standalone test),
`tools/verify_all.sh` (register test) + `CHANGELOG.md` (doc line).

Copied / ported / reimplemented: reimplemented in place. Deviations from
the donor, all deliberate: (a) the clause evaluates above the
member-safety veto loop - it reads only the bot and the live-resolved
group master, and a cross-map master would otherwise veto its own leave
(IsSafe requires same map), making the cross-map branch dead; (b) the
far-away clause only binds when bot grouping is enabled
(RandomBotGroupNearby) - otherwise the action is already useful further up.

Reason: cross-map/straggler bots held dead groups.
## Proactive AoE avoidance with strafe-to-safety (POS-2) — 2026-10-09

Feature: `AvoidAoeAction` (`avoid aoe`, donor `AvoidAoeAction` shape) with
three sensors — dynobj aura affecting the bot, `nearest damaging traps`
(ownerless damage-trap GOs, donor `NearestTrapWithDamageValue` shape),
`possible triggers` (hostile not-selectable units with a periodic-trigger
→ school-damage aura, donor `PossibleTriggersValue` shape) — then a
strafe-first step-out that stays in combat range (donor
`BestPositionForMeleeToFlee` / `BestPositionForRangedToFlee` shape):
melee strafes ±90° off the target, ranged strafes inside the
TooClose..Spell band; straight-line landings band-checked; 15yd radius
cap (`MaxAoeAvoidRadius`); flee-heading memory vetoes failed directions
(POS-7) with a two-pass fallback. `AvoidAoeStrategy` now runs `avoid aoe`
at ACTION_EMERGENCY + 5 with the old reactive `flee` as the fallback at
+4; the cast-suppression multiplier is untouched. Pure ordering/band
rules in `ai/playerbot/AvoidAoePolicy.h`, tested by
`tools/test_avoid_aoe_policy.cpp` (wired into `verify_all.sh`). Reaction
engine membership unchanged (everyone).
## Warrior WAR-2 + WAR-6: shield-slam proc row and 40-rage gate (2026-10-09)

Feature: (WAR-2) new `improved shield slam proc` trigger fires `shield
slam` at HIGH+5 — above the rage ladder, below taunt (41) and tied with
shield block (block listed earlier wins ties), matching the donor's
taunt/block-above-proc order; (WAR-6) the baseline `shield slam` row moved
from `light rage available` (20+) to `medium rage available` (40+), keeping
HIGH+4 above thunder clap (HIGH+1) and revenge/sunder ordering intact, and
the tank sunder veto now defers to slam only when slam's medium-rage row
is actually live (cooldown-only `IsSpellReady` used to veto sunder through
the whole 15-39 band where slam couldn't fire).
## Chromaggus Hourglass Sand cleanse (raid1 item 2) — 2026-10-09

Donor: mod-playerbots @ `79bd4281` (local checkout
`../playerbots-references/mod-playerbots`):
`src/Ai/Raid/BWL/BWLTriggers.cpp:87-90` (BwlAfflictionBronzeTrigger: self
has SPELL_BROOD_AFFLICTION_BRONZE 23170), `src/Ai/Raid/BWL/BWLActions.cpp`
(BwlUseHourglassSandAction: cast SPELL_HOURGLASS_SAND 23645 on self),
`src/Ai/Raid/BWL/BWLStrategy.cpp` (trigger wiring), `src/Ai/Raid/BWL/
BWLHelpers.h:27-28` (spell ids).

Source files (module, modified): `ai/playerbot/ChromaggusSandPolicy.h`
(new pure rule: ids), `ai/playerbot/strategy/triggers/
BlackwingLairDungeonTriggers.h` (ChromaggusStart/EndFightTrigger on entry
14020, ChromaggusBronzeAfflictionTrigger: self aura 23170, header-inline
like the suppression triggers), `ai/playerbot/strategy/actions/
BlackwingLairDungeonActions.h` (ChromaggusEnable/DisableFightStrategyAction,
UseHourglassSandAction: UseItemIdAction qualifier {19183}),
`ai/playerbot/strategy/generic/BlackwingLairDungeonStrategies.h/.cpp`
(`chromaggus` fight strategy: bronze reaction at ACTION_EMERGENCY+5,
end-fight cleanup, start trigger on the `blackwing lair` strategy),
`ai/playerbot/strategy/triggers/TriggerContext.h`,
`ai/playerbot/strategy/actions/ActionContext.h`,
`ai/playerbot/strategy/StrategyContext.h` (registrations),
`tools/test_chromaggus_sand_policy.cpp` (new standalone test) +
## BWL bundle 1: Broodlord range, drake off-tank flank, Vael flank entry, Nef mage Ice Block (raid1 item 4) — 2026-10-09

Donor: mod-playerbots @ `79bd4281` (local checkout
`../playerbots-references/mod-playerbots`):
`src/Ai/Raid/BWL/BWLTriggers.cpp:59-71` (BwlBroodlordRangedTooCloseTrigger:
ranged non-victim within BROODLORD_SAFE_DISTANCE), `:102-106`
(BwlNefarianWildMagicTrigger: mage + SPELL_WILD_MAGIC 23410),
`src/Ai/Raid/BWL/BWLActions.cpp:269-278` (step out to exactly 18y),
`src/Ai/Raid/BWL/BWLStrategy.cpp:38-47,54-55` (drake rear flank, ice block
wiring), `src/Ai/Raid/BWL/BWLHelpers.h:55` (18y constant).

Source files (module, modified): `ai/playerbot/BwlBundle1Policy.h` (new
pure rule: ids, 18y, leave/flank/ice-block predicates),
`ai/playerbot/strategy/triggers/BlackwingLairDungeonTriggers.h`
(Broodlord/NefarianStart+EndFightTrigger on entries 12017/11583,
header-inline BroodlordRangedTrigger with victim hold, header-inline
NefarianWildMagicTrigger: mage + self aura 23410),
`ai/playerbot/strategy/actions/BlackwingLairDungeonActions.h`
(enable/disable actions, BroodlordMoveAwayAction 12017/18y),
`ai/playerbot/strategy/generic/BlackwingLairDungeonStrategies.h/.cpp`
(`broodlord` fight strategy with ranged 18y reaction; `nefarian` fight
strategy with mage Ice Block reaction reusing the existing `ice block`
action; start triggers; end-fight cleanup),
`ai/playerbot/strategy/triggers/DungeonTriggers.cpp` (Vaelastrasz 13020
added to IsRaidDragonEntry; DragonBreathRiskTrigger fires for tanks that
are not the victim), registrations (`TriggerContext.h`,
`ActionContext.h`, `StrategyContext.h`),
`tools/test_bwl_bundle1_policy.cpp` (new standalone test) +
`tools/verify_all.sh` (test list), `docs/guides/dungeon-tactics.md` (doc
line).

Copied / ported / reimplemented: reimplemented in our per-boss fight
strategy idiom. Deviations from the donor, all deliberate: (a) no orb MC
— bots never touch the orb, that stays a player job (same as donor
intent); (b) no moon-mark action: our generic `mark rti` covers marking
and the off-tank hold is enforced by the multiplier, so the extra mark
action would be ceremony; (c) off-tank = first living tank by member-slot
order via LiveGroupMembers (no IsAssistTankOfIndex exists; Golemagg will
revisit tank roles); (d) egg-liveness falls back to hold-the-boss when no
egg data is reachable — the safe side; (e) cone escape reuses
MoveAwayFromCreature's hazard-aware search instead of the donor's
incremental step + fuzz (same observable: out of the cone, behind boss).

Reason: raid1 gap BWL-RAZORGORE: no tactics at all — raid stood in Cleave
and War Stomp, nobody held the boss for the egg phase.

Local validation: `bash tools/verify_all.sh` (all suites incl. the new
policy test pass); `git diff --check`. Entries 12435/19832/177807
verified against tw_world. Build via build-commit.sh pending; live
in-game check pending.

## Review fixes: real egg check, victim guard, behind-boss escape, engage arm (PR #616) — 2026-10-10

All three blocking findings verified real and fixed; non-blocking 1/3/4
also applied (2 noted below).

(a) `eggsAlive` hardcoded true + missing post-egg Cleave branch: the
multiplier now reads the cached `nearest game objects` value for entry
177807 (donor AreRazorgoreEggsAlive) — the veto lifts when eggs die.
Post-egg the action no-ops and normal selection resumes (no separate
Cleave branch; no `TankFaceAction` exists in this codebase).
(b) Missing victim guard + no engage path: veto now returns 1.0 while the
off-tank holds nothing (donor `bot->GetVictim() != nullptr` guard), and a
new tank-only `razorgore engage` node attacks Razorgore while eggs live
(the MarkBoss attack arm; moon mark stays dropped, generic mark rti
covers marking). Post-egg the action no-ops and normal selection resumes.
(c) Escape actions were radial MoveAway flees landing ~17y out (melee
uptime destroyed): new custom `RazorgoreEscapeConeAction::Execute`
implements the donor behind-boss math (orientation + PI + fuzz, melee 3y
/ ranged 15y, LOS-checked). Non-blocking: direct `boss->HasAura(19832)`
(donor/core shape, skips the hostile-unit filter question), TankAssist
early-out hoisting + same-map election guard, production literals routed
through policy constants. Non-blocking 2 NOT separately fixed — the
TankAssist early-out at the top subsumes it (scans only run for vetoed
actions).

Local validation: `bash tools/verify_all.sh` (all suites incl. the policy
test pass); `git diff --check`. Build via build-commit.sh pending; live
in-game check pending.
strategy idiom. Deviations from the donor, all deliberate: (a) no
resist-aura triggers here (separate raid1 item 8); (b) Nefarian positioning
is already covered by the universal dragon flank (Nefarian is in the flank
list), so only the Wild Magic class call is new; (c) the drake change is a
one-condition edit on the shared trigger, so Onyxia/Nefarian/Solnius gain
off-tank flanking too — same geometry, no per-boss special case.

Reason: raid1 gaps BWL-BROODLORD (ranged ate Blast Wave), BWL-DRAKES
(off-tanks stood in breath), BWL-VAEL (13020 missing from flank list),
BWL-NEFARIAN-WILDMAGIC (mages never Ice Blocked).

Local validation: `bash tools/verify_all.sh` (all suites incl. the new
policy test pass); `git diff --check`. Entries 12017/13020/11583 and spell
23410 verified against tw_world. Build via build-commit.sh pending; live
in-game check pending.
## Quest-reward score tiebreak (AG-3/RPG-A1, 2026-10-09)

Donor: mod-playerbots (`79bd4281`):
`src/Ai/Base/Actions/TalkToQuestGiverAction.cpp:179-190` (among tied
BestRewards ids, pick max stat-weight score),
`src/Ai/World/Rpg/Action/NewRpgBaseAction.cpp:536-551` (same two-pass
shape for the RPG path).

Source files (module, modified): `ai/playerbot/QuestRewardPolicy.h` (new
pure winner rule over (index, weight) pairs, strictly-greater keeps vendor
order on exact ties),
`ai/playerbot/strategy/actions/TalkToQuestGiverAction.cpp` (auto-pick
caller scores tied candidates via GetLiveStatWeight and picks the winner;
winner score appended to the QuestRewarded event line),
`tools/test_quest_reward_policy.cpp` (new standalone test),
`tools/verify_all.sh` (register test) + `CHANGELOG.md` (doc line).

Copied / ported / reimplemented: reimplemented in place. Deviations from
the donor, all deliberate: (a) the tiebreak applies only to the autonomous
auto-pick caller - BestRewards itself still returns the full tied set, so
owned bots keep the ask-on-tie behavior and the guild-share override stays
first; (b) single-candidate and guild-share paths are untouched (no
scoring when there is nothing to break).

Reason: two EQUIP rewards tied on usage and the bot took the first in
vendor order, compounding into wrong picks over levels.
## Rogue lockbox in trade (AG-5 + AG-9, 2026-10-09)

Donor: mod-playerbots (`79bd4281`):
`src/Ai/Base/Actions/TradeStatusExtendedAction.cpp:14-80` (parse
extended-trade packet; locked NONTRADED slot + rogue -> unlock),
`src/Ai/Base/Strategy/WorldPacketHandlerStrategy` wiring (`:37`),
`src/Ai/Base/Trigger/ChatTriggerContext.h:163` + supported `:176`
(manual unlock chat trigger).

Source files (module, modified): `ai/playerbot/TradeLockboxPolicy.h` (new
pure usefulness gate),
`ai/playerbot/strategy/actions/UnlockTradedItemAction.{h,cpp}`
(isUseful gate via the policy),
`ai/playerbot/PlayerbotAI.cpp` (SMSG_TRADE_STATUS_EXTENDED packet
handler), `ai/playerbot/strategy/triggers/WorldPacketTriggerContext.h`
(trigger creator),
`ai/playerbot/strategy/generic/WorldPacketHandlerStrategy.cpp`
(extended-update -> unlock node),
`ai/playerbot/strategy/triggers/ChatTriggerContext.h` +
`ai/playerbot/strategy/generic/ChatCommandHandlerStrategy.cpp` (AG-9
manual whisper trigger), `tools/test_trade_lockbox_policy.cpp` (new
standalone test), `tools/verify_all.sh` (register test) +
`CHANGELOG.md` (doc line).

Copied / ported / reimplemented: reimplemented in place. Deviations from
the donor, all deliberate: (a) no packet parsing - the 1.12 extended
layout differs from the donor's WotLK parse (no gem fields), and the
unlock action already reads the box from TradeData, so the packet only
wakes the action; (b) the usefulness gate the donor lacks (rogue + locked
box present): trade updates arrive on every window change, so an ungated
action would chat errors every tick for non-rogues; fine checks (skill,
spell, level) stay in Execute, which still tells when it runs; (c) the
existing thorough unlock action (skill-vs-lock, pick cast, tells) is
reused, not the donor's DoSpecificAction stub.

Reason: the unlock action existed but never fired - rogues let trades
complete around locked boxes.

Local validation: `bash tools/verify_all.sh` (incl. new policy test +
wiring check live-missing=0); `git diff --check`; shared-builder compile
check; no live in-game test.
| Loatheb fight (Naxx), spores only: 1yd spore assignment + tank/ranged spots | `mod-playerbots` | `79bd4281` | `src/Ai/Raid/Naxx/Action/NaxxActions_Loatheb.cpp` (spore/position legs only) | Reimplemented trigger-driven; heal-suppression explicitly NOT ported (Necrotic Aura absent in vanilla); Doom-window healing is future design | Kit verified in tw_world + core boss_loatheb.cpp (16011, spore 16286, 29201/29204/29232/29865) | `bash tools/verify_all.sh` + `tools/test_loatheb_spores_policy.cpp`; build-commit + no live test |

## Golemagg full fight (raid1 item 9, LAST) — 2026-10-09

Implemented LAST per the task order. The main-tank value PR
(parity/ld-8-main-tank) was NOT merged at implementation time (verified:
branch exists with `main tank` value + MT stickiness, not an ancestor of
origin/feat/playerbots-parity), so this PR implements Golemagg on the
slot-order fallback and says so here.

Donor: mod-playerbots @ `79bd4281` (local checkout
`../playerbots-references/mod-playerbots`):
`src/Ai/Raid/MC/MCActions.cpp:115-301` (main-tank boss camp, assist rager
camps, Trust separation, splash back-off, healer midpoint, rager pickup),
`src/Ai/Raid/MC/MCTriggers.cpp:46-88` (splash/healer/main/assist role
triggers), `src/Ai/Raid/MC/MCMultipliers.cpp:105-144` (single-tank dance,
assist tank-assist veto, AoE-off, ranged melee-fallback ban, back-off
lock, 10% burn), `src/Ai/Raid/MC/MCStrategy.cpp:60-79,108-128` (role
wiring + Core Rager DPS exclusion), `:17-25` (camp coords, 30y Trust, 5y
step), `src/Ai/Raid/MC/MCHelpers.h:15,30-35` (entries, 20-stack, 12y).

Source files (module, modified): `ai/playerbot/GolemaggPolicy.h` (new
pure rule: ids, 20-stack/12y/8y/30y/10% constants, camp coords,
back-off/lock/exclusion/single-tank predicates),
`ai/playerbot/strategy/triggers/MoltenCoreDungeonTriggers.h`
(GolemaggStart/EndFightTrigger 11988, GolemaggSplashTrigger with
victim-agnostic stack + range gates, GolemaggHealerTrigger midpoint
check, GolemaggTankHoldTrigger on Trust aura),
`ai/playerbot/strategy/actions/MoltenCoreDungeonActions.h/.cpp`
(enable/disable actions, GolemaggBackOffAction 11988/12y,
GolemaggHealerPositionAction, GolemaggTankHoldAction with main/assist
camp split), `ai/playerbot/strategy/generic/
MoltenCoreDungeonStrategies.h/.cpp` (`golemagg` fight strategy: potion +
tank-hold + healer nodes, splash reaction EMERGENCY+5, end-fight cleanup,
GolemaggFightMultiplier; start trigger on `molten core`),
`ai/playerbot/strategy/generic/DungeonMultipliers.h/.cpp`
(GolemaggFightMultiplier), registrations (`TriggerContext.h`,
`ActionContext.h`, `StrategyContext.h`),
`tools/test_golemagg_policy.cpp` (new standalone test) +
`tools/verify_all.sh` (test list), `docs/guides/dungeon-tactics.md` (doc
line).

Copied / ported / reimplemented: reimplemented in our per-boss fight
strategy idiom. Deviations from the donor, all deliberate: (a) tank roles
= first living tank by member-slot order is main, next tanks are assists
(the ld-8 `main tank` value + IsAssistTankOfIndex do not exist on this
base; when ld-8 merges, role reads move to the shared value — no rework
of triggers/actions needed, only the two role expressions); (b) no skull
mark action (our generic `mark rti` covers it; extra action = ceremony);
(c) DPS rager exclusion enforced via the fight multiplier's AoE veto +
documented single-target discipline rather than a target-value blacklist
(the Majordomo half stays out — separate fight, separate PR); (d) melee
fallback ban covers ranged bots (donor IsRanged includes healers; ours
matches at strategy level); (e) camp coords used as-is from donor map
data — Turtle validation wants eyes in a live MC run.

Reason: raid1 gap MC-GOLEMAGG (L, high): the whole fight was missing —
tank camps, splash back-off, healer spot, rager exclusion.

Local validation: `bash tools/verify_all.sh` (all suites incl. the new
policy test pass); `git diff --check`. Entries 11988/11672 and spells
13880/20553 verified against tw_world. Build via build-commit.sh pending;
live in-game check pending (esp. camp coords on the Turtle map).

## Review fixes: Golemagg 2D range, combat MoveTo, healer priority, named AoE, any-stack lockout (PR #632) — 2026-10-10

All five blocking findings verified real in code and fixed; non-blocking
2 declined (kGolemaggEntry in the action header would need a new include
for one literal — reverted; the literal matches the file's existing
11982/12056 style).

(a) Splash trigger used `IsWithinDist(attacker, 12.0f)` (3D + reach
padding on a huge boss) while the 12y move search uses entry-range
lookups: fixed to the donor shape `GetDistance2d < kMagmaSplashBackOffDistance`.
(b) Healer/tank-hold `MoveTo(..., IsReaction(), ...)` forwarded a constant
false from combat context: fixed to explicit `false` per the review.
(c) Healer midpoint at ACTION_HIGH (20) lost to heals/dispels and could
drag healers into splash via reach-to-heal: raised to ACTION_MOVE + 5 (35).
(d) AoE veto reused the Garr threat-flag match, which under-marks real AoE
and over-marks heals/dots (same hole as PR #587 review): now uses the
local `IsGolemaggSuppressedAoeAction` name list (this branch predates the
Garr PR merge; merge both to one home when the branches land).
(e) Back-off lockout fired only at 20+ stacks (19 stacks re-engaged),
ignored non-boss targets, and missed `ReachTargetAction` (`reach melee`
is MovementAction-based): now holds on ANY remaining stack via
`ShouldHoldBackOff`, gates engages on `current target == boss`, and
covers Attack/Melee/Reach/ReachSpell actions.

Local validation: `bash tools/verify_all.sh` (all suites incl. the
golemagg policy test, which already pinned any-stack hold, pass); `git
diff --check`. Build via build-commit.sh pending; live in-game check
pending (esp. camp coords on the Turtle map).
## Onyxia Deep Breath safe-zone dodging (raid1 item 7) — 2026-10-09

Donor: mod-playerbots @ `79bd4281` (local checkout
`../playerbots-references/mod-playerbots`):
`src/Ai/Raid/Ony/OnyTriggers.cpp` (OnyxiaDeepBreathTrigger: boss
CURRENT_GENERIC_SPELL id in the 8 breath ids),
`src/Ai/Raid/Ony/OnyActions.h:52-93` (MoveToSafeZone: nearest of 2 safe
zones per breath direction + already-safe early-out + AttackStop/CastStop
first), `src/Ai/Raid/Ony/OnyStrategy.cpp:21-23` (trigger wiring at
ACTION_RAID).

Source files (module, modified): `ai/playerbot/OnyxiaBreathPolicy.h`
(new pure rule: 8 breath ids, axis pairing, 5y hold radius),
`ai/playerbot/strategy/triggers/OnyxiasLairDungeonTriggers.h`
(header-inline OnyxiaDeepBreathTrigger reading the boss current-target
generic-spell cast, gated on the `onyxia` fight strategy),
`ai/playerbot/strategy/actions/OnyxiasLairDungeonActions.h/.cpp`
(OnyxiaBreathSafeZoneAction: nearest zone of the matching donor pair,
already-inside hold, cast-stop first, MoveTo with reaction flag),
`ai/playerbot/strategy/generic/OnyxiasLairDungeonStrategies.cpp`
(reaction wired at ACTION_EMERGENCY+5 above the generic flank),
registrations (`TriggerContext.h`, `ActionContext.h`),
`tools/test_onyxia_breath_policy.cpp` (new standalone test) +
`tools/verify_all.sh` (test list), `docs/guides/dungeon-tactics.md` (doc
line).

Copied / ported / reimplemented: reimplemented in our Onyxia fight
strategy idiom. Deviations from the donor, all deliberate: (a) no
fallback-center zone (donor default arm): an unknown spell id fails the
trigger instead of walking the raid to mid-room — unreachable in practice
since the trigger gates the action on the same 8 ids; (b) priority
EMERGENCY+5 above the universal flank (donor runs both at raid priority):
cast-triggered lane dodge beats cone geometry while a breath casts; (c)
z taken from the donor spot table (lava-side heights differ per spot).

Reason: raid1 gap ONY-BREATH: the flank trigger escapes front/rear cones
but never dodged the breath lanes — the actual phase-2 killer.

Local validation: `bash tools/verify_all.sh` (all suites incl. the new
policy test pass); `git diff --check`. All 8 breath ids verified against
tw_world (all named Breath). Donor safe-zone coords used as-is; Turtle map
validation still wants eyes in a live Onyxia run (flagged in the report).
Build via build-commit.sh pending; live in-game check pending.

## Review fixes: attacker-scan boss lookup + stopped-targetless breath detection (PR #628) — 2026-10-10

Both blocking findings verified real against the core script
(`boss_onyxia.cpp` DoMovement + `Spell.cpp:3641`) and fixed:

(a) Boss via `current target` missed every whelp tank, melee, and healer
in phase 2 (their targets are whelps/friendlies, never Onyxia). Both the
trigger and the action now resolve Onyxia via the attacker-list scan for
entry 10184 (the StartBossFightTrigger pattern) — every bot reacts
regardless of its own target.

(b) `GetCurrentSpell(CURRENT_GENERIC_SPELL)` is ALWAYS null during Deep
Breath: the core casts the directional spells TRIGGERED
(`boss_onyxia.cpp:522`), and triggered non-channeled spells never populate
the caster's spell slot (`Spell.cpp:3641`). No core seam was added (module
stays decoupled): detection now reads the breath window's observable
side-effects — Hover aura up + stopped (no motion during the 5s window)
+ cleared target guid (DoMovement faces the destination and clears target
before the cast). Grounded phases always hold a victim and keep moving,
so false positives need hover + stopped + targetless together. The lane
axis comes from boss facing (core faces the destination pre-cast) via the
new testable `BreathAxisFromFacing` (8 eighths → donor's 4 lane pairs);
the spell-id table stays for documentation. Also dropped the redundant
`isPossible` double-CanMove (non-blocking 1).

Risk noted: facing maps to the lane only if the core's pre-cast facing
matches the breath travel direction; if live behavior shows otherwise
(e.g. facing snaps back mid-window), fall back to reacting to the
EMOTE_BREATH script text (-1249004) as the reviewer suggested. Safe-spot
coords still want live Turtle validation (non-blocking 2, unchanged).

Local validation: `bash tools/verify_all.sh` (all suites incl. the updated
policy test with facing-mapping coverage pass); `git diff --check`.
Build via build-commit.sh pending; live in-game check pending.
## Warlock Firestone / Spellstone create+equip (WAR-4) — 2026-10-09

Donor: mod-playerbots (`79bd4281`):
`src/Ai/Class/Warlock/Strategy/GenericWarlockNonCombatStrategy.cpp:200-222`
(per-spec stone strategies: affli/demo spellstone, destro firestone),
`WarlockTriggers.h:66-78,116-126` (`FirestoneTrigger`/`SpellstoneTrigger :
BuffTrigger`, `HasFirestoneTrigger`/`HasSpellstoneTrigger`) +
`WarlockAiObjectContext.cpp:330-331` (both use actions are
`UseSpellItemAction`), `WarlockActions.h:79-94`.

Source files (module, modified):
`ai/playerbot/strategy/warlock/WarlockStrategy.cpp` (base `no firestone` /
`no spellstone` create rows stay removed — creation is per-spec only),
`ai/playerbot/strategy/warlock/WarlockTriggers.{h,cpp}` (`FirestoneTrigger`
and `SpellstoneTrigger` as plain `Trigger`s with O(1) item/slot gates,
not `BuffTrigger`: no player spell is named "firestone"/"spellstone" so
the `BuffTrigger` HasSpell gate would never pass),
`ai/playerbot/strategy/warlock/WarlockAiObjectContext.cpp` (`firestone`
trigger + `EquipFirestoneAction`, `spellstone` trigger +
`EquipSpellstoneAction`; `create firestone`/`create spellstone` actions),
`ai/playerbot/strategy/warlock/DestructionWarlockStrategy.cpp` (`firestone`
use row) + `AfflictionWarlockStrategy.cpp` + `DemonologyWarlockStrategy.cpp`
(`spellstone` use rows) + `docs/classes/warlock.md`, `CHANGELOG.md`.

Copied / ported / reimplemented: reimplemented with vanilla item
semantics, verified in `tw_world` (report's 1254/5522-series ids are item
entries, not spells — the spells are Create Firestone 607 / Create
Spellstone 918). Both stones are off-hand held items (`inventory_type`
23), and both are *equipped*, never applied as weapon temp-enchants:
Firestone's item spells are on-equip auras (758 Firestone Passive on
Lesser 1254, 17945+ on the higher ranks), and Spellstone's on-use spells
(128/17729/17730, effect 38 `SPELL_EFFECT_DISPEL` + effect 6 aura 69
`SPELL_AURA_SCHOOL_ABSORB`, target 1 = caster, per the on-use description
"Removes all magic effects from the caster and will absorb ... magic
damage") likewise target the caster — while a sharpening stone works
through `UseItem` only because it is an `INVTYPE_NON_EQUIP` consumable,
`UseItemInternal` refuses equippable items sitting in bags, so a bag
stone could never be applied that way. Core's
`Player::RemoveItemDependentAurasAndCasts` exempts spellstone items
5522/13602/13603/21685 from aura removal on unequip ("Pierres de sort"),
so the equip aura (18384 Increased Critical Spell on all three ranks)
survives briefly after the on-use dispel/absorb is cast from the worn
stone. `EquipSpellstoneAction`/`EquipFirestoneAction` therefore equip a
bag stone into an empty off-hand beside a one-handed main-hand (never
displacing real gear or fighting a staff); the equip aura then applies
and the on-use stays available through normal use once worn. Create
Spellstone costs a shard (2362 reagent 6265x1, core fails gracefully when
shardless — same accepted pattern as soulstone).

Reason: both create rows were commented out and no use path was queued;
donor keeps per-spec stones up out of combat.

Local validation: `bash tools/verify_all.sh` (wiring audit covers the
`firestone`/`spellstone` names); `git diff --check`; shared-builder
compile via `build-commit.sh` (BUILD OK); live in-game check pending:
each stone created once, equipped without touching real off-hands.

## Review fixes (pr-618, CHANGES_REQUESTED → fixed)
First round (commit ec6423d): (1) firestone is a dedicated
`EquipFirestoneAction` (static slot-safe equip — the `UseSpellItemAction`
path refused equippables in bags per `UseItemInternal`, and firestones
have no on-use spell); (2) creation is per-spec (`no spellstone` in
affli/demo buff NC, `no firestone` in destro buff NC — no more cross-spec
shard drain or bag pollution); (3) both use-triggers are plain `Trigger`s
with O(1) item/slot gates. Second round: (1) the new
`ApplySpellstoneAction` routed through the same refused `UseItem` →
`UseItemInternal:441` path — real, fixed by replacing it with
`EquipSpellstoneAction` (same equip-into-empty-off-hand semantics as
firestone); post-equip verification added to both equip actions (return
whether the stone actually landed, so silent core refusals surface as
action failure with retry next tick). (2) the "spellstone as weapon
temp-enchant" claim was wrong — verified against `tw_world` + core
(`SpellDefines.h:182` effect 38 is `SPELL_EFFECT_DISPEL`, not 54 =
temp-enchant; on-use targets the caster per the spell description) — so
the temp-slot trigger/action gates are gone from both sides; both stones
are now plain off-hand equips (spec split kept: spellstone for
affli/demo, firestone for destro). Rejected nothing.

## Paladin resist aura auto-swap per boss (raid1 item 8) — 2026-10-09

Donor: mod-playerbots @ `79bd4281` (local checkout
`../playerbots-references/mod-playerbots`): per-boss resist actions under
`src/Ai/Base/Actions` (add paladin resist strategy + cast the aura now),
`src/Ai/Base/Trigger/BossAuraTriggers.cpp:13-25,169-185` (paladin-gated,
boss-alive, aura-missing checks), `src/Ai/Raid/MC/MCStrategy.cpp`
(Lucifron/Gehennas/Majordomo shadow; Magmadar/Garr/Geddon/Sulfuron/
Golemagg/Ragnaros fire), `src/Ai/Raid/BWL/BWLStrategy.cpp`
(Razorgore/Vael/Broodlord/Firemaw/Flamegor fire).

Source files (module, modified): `ai/playerbot/ResistAuraPolicy.h` (new
pure rule: fire list 11982/12057/12056/12098/11988/11502/12435/13020/
12017/11983/11981, shadow list 12118/12259/12018, swap-when-missing +
action routing), `ai/playerbot/strategy/triggers/DungeonTriggers.h/.cpp`
(fight-agnostic BossWantsFire/ShadowAuraTrigger: paladin gate, manual-aura
override guard, aura-missing guard, bounded attacker entry scan),
`ai/playerbot/strategy/actions/DungeonActions.h/.cpp`
(SwapFire/ShadowResistanceAuraAction: +aura strategy then cast now),
wired into `molten core` + `blackwing lair` combat triggers at
ACTION_HIGH+1, registrations (`TriggerContext.h`, `ActionContext.h`),
`tools/test_resist_aura_policy.cpp` (new standalone test) +
`tools/verify_all.sh` (test list), `docs/guides/dungeon-tactics.md` (doc
line).

Copied / ported / reimplemented: reimplemented fight-agnostic — one pair
of triggers reads boss entries off the attacker list instead of the
donor's per-boss trigger/action zoo (9 MC + 5 BWL nodes). All 14 entries
verified in tw_world (report's 12118/12098/11988/12017 queries resolve).
Deviations from the donor, all deliberate: (a) single PR for MC+BWL per
the report sketch; (b) manual `aura fire/shadow/frost` strategies suppress
the auto-swap (player control first — explicit orders beat automation);
(c) 5s check interval (resist fights are slow; no per-tick attacker
scans); (d) no frost/nature handling (no donor MC/BWL frost boss; hunter
nature aspect out of scope).

Reason: raid1 gap MC-AURA/BWL: aura actions existed but were manual-only
— paladins kept whatever aura was last set through fire/shadow bosses.

Local validation: `bash tools/verify_all.sh` (all suites incl. the new
policy test pass); `git diff --check`. Build via build-commit.sh pending;
live in-game check pending.
## Warrior WAR-8: tank Intervene on focused party member (2026-10-09)

Feature: `ProtectionWarriorStrategy` gains `protect party member` →
`intervene` at ACTION_EMERGENCY, plus the `intervene` → `defensive stance`
prerequisite node in the live base-warrior factory (it existed only in the
commented-out census block). Verified real first: Intervene 45595 is
Defensive-locked in `spell_template` and taught by 47277 (26 trainer
rows) — a genuine learnable 1.18.1 player spell, so the row was added.
## Druid parity DRU-2: Nature's Swiftness -> instant Healing Touch chain — 2026-10-09
Feature: new `NaturesSwiftnessActiveTrigger` (`HasAuraTrigger` on
"nature's swiftness", true while the buff sits on the bot) + `TwoTriggers`
combo `nature's swiftness heal` (active + party member critical health) +
two combat rows in `RestorationDruidStrategy`: pop `nature's swiftness` at
ACTION_CRITICAL_HEAL+1 on party-member-critical (below the instant
Swiftmend ladder, so no 3 min cooldown burns first), spend with
`healing touch on party` at ACTION_CRITICAL_HEAL+3 (above the ladder, so
the buff never idles).
## Priest parity shadow kit: PRI-14 fallback chain + PRI-3 DP spread + PRI-9 healer silence — 2026-10-09
Feature: (1) `ShadowPriestStrategy` now uses the existing
`ShadowPriestStrategyActionNodeFactory.h` (mind blast to mind flay to
smite to shoot fallbacks) instead of the empty local factory class that
shadowed it — an out-of-mana shadow bot wands instead of idling, and a
pre-Mind-Flay lowbie smites. (2) AoE kit spreads `devouring plague on
attacker` below the SW:P spread (Devouring Plague is the 1.12 second
DoT; vanilla has no Vampiric Touch). (3) Base shadow kit silences enemy
healers (the row used to need the `+cc` toggle; the cc kit keeps its own
copy).
## Druid parity DRU-4: Omen of Clarity clearcasting consumption — 2026-10-09
Feature: two combat rows, no new triggers or actions (both registered,
verified by the wiring check). Cat (`DpsFeralDruidStrategy`):
`clearcasting` -> `shred` at ACTION_NORMAL+6, above the bite/rip/builder
ladder and strictly above the faerie-fire row at +5 (queue ties keep the
first-pushed basket, so the proc must outrank the debuff refresh).
Restoration: `clearcasting` -> `rejuvenation on party` at
ACTION_LIGHT_HEAL+3, above the normal HoT rows.
## Druid parity DRU-5: Ferocious Bite execute + Rip-guard timing — 2026-10-09
Feature: new `FerociousBiteExecuteTrigger` (CP>=1 + target alive + target
HP%<25, with a `HasSpell("ferocious bite")` guard) + new
`FerociousBiteTimeTrigger` (CP5 + target Rip absent or >10 s left via
`ai->GetAura("rip", target, true)->GetAuraDuration()`) + two combat rows
in `DpsFeralDruidStrategy` replacing the flat CP5 row: execute-bite at
ACTION_NORMAL+6 (top finisher, strictly above the faerie-fire row at +5 —
ties keep the first-pushed basket), timed bite at ACTION_NORMAL+3 (below
the pve rip row at +4). Finisher order: execute > rip > timed bite.
## Priest parity PRI-1: Fear Ward on the party tank in combat — 2026-10-09
Feature: new `FearWardOnTankTrigger` (`BuffOnTankTrigger` on "fear ward",
cooldown-guarded like the donor) + `CastFearWardOnTankAction`
(`BuffOnTankAction`, targets "party tank without aura", `getName`
overridden to the registered name) + combat row `fear ward on tank` at
ACTION_HIGH+3 in `PriestBuffStrategy`, outranking the generic `fear ward`
row (demoted from ACTION_EMERGENCY to ACTION_HIGH+2) by relevance — the
engine executes the highest-relevance action, so the tank is warded first
and the generic row stays as a manual-target fallback.
## Druid parity DRU-1: out-of-combat Rebirth when no living resurrector — 2026-10-09
Feature: new `OocRebirthTrigger` (`RebirthTrigger` + living
priest/paladin/shaman group scan, cheap class check first, then the base
cooldown/spellbook/target checks) + non-combat row `ooc rebirth` ->
`rebirth` at ACTION_EMERGENCY in `DruidStrategy` (base, so all four wired
specs + leveling inherit) + pure rule `ShouldCastOocRebirth` in new
`ai/playerbot/OocRebirthPolicy.h` + `tools/test_ooc_rebirth_policy.cpp`
(6 checks, registered in `tools/verify_all.sh`).
## Druid parity DRU-3: feral/cat Innervate on the group healer — 2026-10-09
Feature: new local `HealerLowManaTrigger` (named exactly "healer low
mana", true while a living same-map party healer sits below the LowMana
line — mirror of the scan in `CastInnervateAction::GetTarget`) + combat
row `healer low mana` -> `innervate` at ACTION_HIGH-1 in
`DpsFeralDruidStrategy` (below Cower, above the rotation) + `innervate`
caster-form node in the cat action-node factory (was missing — balance
and resto each define their own; without it the cat casts from form and
fails). No new action: the existing `innervate` action already targets
the lowest-mana party healer with manual `.bot boost` winning.

Source repository: `mod-playerbots` @ `79bd4281` (local checkout
`../playerbots-references/mod-playerbots`).

Source files (donor, reference only):
`src/Ai/Class/Druid/Strategy/GenericDruidNonCombatStrategy.cpp:198-200`
(`thorns on main tank` 11.0 above `thorns` 10.0). Deviations, deliberate:
donor names say "main tank" on `BuffOnMainTankTrigger`; ours says "tank"
on the local `BuffOnTankTrigger` (same "party tank without aura" value,
cf. PRI-1). The report's "refresh-via-recast may need cancel aura
support" proved unnecessary: the without-aura value only targets a tank
lacking Thorns, so expiry re-fires the row naturally. No addon change:
the main-tank pick already exists via the role button.

Reason: druid parity report DRU-7 — Thorns fell out of the tank's
rotation once the party row was satisfied.
`src/Ai/Class/Druid/Strategy/RestoDruidStrategy.cpp:37-43` (critical ->
`nature's swiftness` 58.0, `nature's swiftness active` -> `healing touch
on party` 55.0) + `src/Ai/Class/Shaman/ShamanAiObjectContext.cpp:286-288`
(the `ancestral swiftness active` + `TwoTriggers` pairing pattern copied
here). Deviations, deliberate: donor pop row outranks its whole heal
ladder; ours sits below Swiftmend (instant already, cheaper than a 3 min
cooldown) — same shape, cheaper ordering for vanilla cooldowns.

Reason: druid parity report DRU-2 — we cast NS as a bare buff with no
spend row; the donor's core resto save was never rewired.

Source files (module, modified):
`ai/playerbot/strategy/druid/DruidTriggers.h`,
`ai/playerbot/strategy/druid/DruidAiObjectContext.cpp`,
`src/Ai/Class/Druid/Strategy/CatDruidStrategy.cpp:165-171`
(`clearcasting` -> `shred` 24.5, above rip 23.5) +
`src/Ai/Class/Druid/Strategy/RestoDruidStrategy.cpp:45-46`
(`clearcasting` -> free HoT 13.0). Deviations, deliberate: donor resto
spends the proc on Lifebloom-on-tank, which does not exist in 1.18.1 —
ours spends it on party Rejuvenation. The cat-swipe AoE combo row was
skipped (no `TwoTriggers` combo registered for it; single-target spend
covers the proc).

Reason: druid parity report DRU-4 — the `clearcasting` trigger was
registered but zero wired rows referenced it.

Source files (module, modified):
`ai/playerbot/strategy/druid/DpsFeralDruidStrategy.cpp`,
`ai/playerbot/strategy/druid/RestorationDruidStrategy.cpp` +
`docs/classes/druid.md` (behaviour lines).

Copied / ported / reimplemented: reimplemented in place in the live
strategy idiom. No new spells or actions: Nature's Swiftness 17116 and
Healing Touch ranks verified in spell_template; both trigger and action
creators already registered.

Local validation: `bash tools/verify_all.sh`; `git diff --check`. No
live test (no live test per parity brief); build via build-commit.sh.
`src/Ai/Class/Priest/Strategy/ShadowPriestStrategyActionNodeFactory.h`
(fallback chain) +
`src/Ai/Class/Priest/Strategy/ShadowPriestStrategy.cpp:72-106`
(second-DoT spread) + `:54-70` (always-on silence rows). Deviations,
deliberate: VT mapped to Devouring Plague 2944+ (undead racial, gated by
the trigger's HasSpell check); no Shadow Word: Death (no 1.18.1 spell
row — report PRI-4).

Reason: priest parity report PRI-14/PRI-3/PRI-9 — dead fallback chain,
single-DoT AoE, healer silence behind a toggle.

Source files (module, modified):
`ai/playerbot/strategy/priest/ShadowPriestStrategy.cpp`,
`ai/playerbot/strategy/priest/PriestTriggers.h`,
`ai/playerbot/strategy/priest/PriestActions.h`,
`ai/playerbot/strategy/priest/PriestAiObjectContext.cpp` +
`docs/classes/priest.md` (rotation lines).

Copied / ported / reimplemented: reimplemented in place. No new spells
(SW:P 589+, Devouring Plague 2944+, Silence 15487 in 1.18.1 data).

Local validation: `bash tools/verify_all.sh`; `git diff --check`.
Build via build-commit.sh. No live test.
strategy idiom. No new spells: Omen of Clarity 16864 / Clearcasting
16870 verified in spell_template.

Local validation: `bash tools/verify_all.sh`; `git diff --check`. No
live test (no live test per parity brief); build via build-commit.sh.
## Chat `pull back` alias + `attackers` wiring fix (SOC-C2/SOC-C4, 2026-10-09)

Donor: mod-playerbots (`79bd4281`):
`src/Ai/Base/Strategy/ChatCommandHandlerStrategy.cpp:72` (`pull back` ->
`pull my target`), `:68` (`attackers` -> `tell attackers`).

Source files (module, modified): `ai/playerbot/strategy/generic/ChatCommandHandlerStrategy.cpp`
(new `pull back` -> `pull my target` node; `attackers` node now points at
the existing `tell attackers` action instead of the nonexistent `attackers`
action), `ai/playerbot/strategy/triggers/ChatTriggerContext.h` (new
`pull back` trigger creator) + `CHANGELOG.md` (doc line).

Copied / ported / reimplemented: reimplemented in place (two trigger-node
lines + one creator line). No deviations: same alias target and same action
name as the donor. The `pull back` tank auto-pull strategy name is
unaffected - chat dispatch resolves whisper text against trigger creators,
not strategy names.

Reason: whispering a bot `pull back` resolved no trigger (only `pull` and
`pull rti` existed); whispering `attackers` matched the trigger but queued
an action with no creator, so the bot stayed silent.

Local validation: `bash tools/verify_all.sh` (incl. wiring check:
live-missing=0); `git diff --check`; shared-builder compile check; no live
in-game test.
| Felhunter Devour Magic purge + cleanse (PET-1) | `mod-playerbots` @ `79bd4281` `src/Ai/Class/Warlock/WarlockTriggers.h:161-171` (`DevourMagicPurgeTrigger`/`DevourMagicCleanseTrigger`), `src/Ai/Class/Warlock/WarlockActions.h:192-205` (`CastDevourMagicPurgeAction`/`CastDevourMagicCleanseAction`), `src/Ai/Class/Warlock/Strategy/GenericWarlockStrategy.cpp:63-78` (purge/cleanse nodes at 50.0) | `ai/playerbot/strategy/warlock/WarlockTriggers.h` (two trigger classes), `WarlockActions.h` (`CastDevourMagicPurgeAction` on current target + `CastDevourMagicCleanseAction` on party dispel target, both Felhunter-gated in `isUseful`), `WarlockStrategy.cpp` (`WarlockPetStrategy::InitCombatTriggers`: purge at ACTION_DISPEL+1, cleanse at ACTION_DISPEL), `WarlockAiObjectContext.cpp` (4 creators), `runtime/DevourMagicPolicy.h` + `tools/test_devour_magic_policy.cpp` | Reimplemented as pet-cast actions (donor casts through the owner; ours routes via `CastPetSpellAction` so range/cooldown resolve against the demon) wired into the live pet strategy all specs inherit; donor node priority 50.0 maps to ACTION_DISPEL+1/+0 | `bash tools/verify_all.sh`; `python3 tools/verify_action_trigger_wiring.py` (0 live-missing); standalone `test_devour_magic_policy` (5 checks); `git diff --check`. Compile via shared builder; no live in-game test |
## Warrior WAR-4: staggered tank defensives (2026-10-09)

Feature: protection warrior *Shield Wall* moved from `critical health`
(<20%, ACTION_EMERGENCY+1) to `low health` (20-50%, ACTION_MEDIUM_HEAL);
*Last Stand* stays at critical (ACTION_EMERGENCY+2). The two no longer
fire stacked at the same moment — Wall blunts damage early, Last Stand is
held for genuinely lethal moments, and Wall's 30-min cooldown is not spent
on fights Last Stand alone would survive.

## Warrior WAR-5: arms battle-stance pin (2026-10-09)

Feature: `ArmsWarriorBuffStrategy` combat pin swapped from `berserker
stance` to `battle stance` (ACTION_NORMAL). Charge, Overpower, Mocking
Blow, Sweeping Strikes, and Retaliation are all Battle-locked in 1.18.1
and never fired from the old berserker pin; Whirlwind keeps its
arms-scoped berserker prerequisite node so it still dances out and back.

## Warrior WAR-10: arms/fury boost rows (2026-10-09)

Feature: `ArmsWarriorBoostStrategy` gains `death wish` (HIGH) and `almost
full health` → `retaliation` (HIGH) rows; `FuryWarriorBoostStrategy` gains
`almost full health` → `retaliation` (HIGH) alongside its existing death
wish / recklessness rows. Retaliation fires at 70-90% hp — winning fights
where the counterattack shield punishes melee adds. All behind the default
`boost` strategy (player-togglable), protection boost stays empty.
## Shared flee-heading anti-oscillation memory (POS-7) — 2026-10-09

Feature: the existing `FleeFailureMemory` (donor `CheckLastFlee` shape:
45-degree same-heading veto, 5 s window, observed-failures-only) is now
consulted by the two combat sidesteps that previously consulted nothing.
`TankFaceAwayAction` vetoes sidestep headings remembered as failures
(anchored on the held mob) with a two-pass fallback so the cache can never
block every route; `SetBehindTargetAction` diverts to its flank-angle
fallback when the direct rear point repeats a failed heading. Both record
(`BeginAttempt`) and observe like the flee/spread paths. `FleeManager`
(plain flee) and `RaidSpreadAction` (spread) already consult — no change
there. No new value plumbing: all state lives on `LastMovement` as before.
## Warrior WAR-3: lost-aggro taunt priority (2026-10-09)

Feature: protection warrior `lose aggro` now fires `taunt` at
ACTION_INTERRUPT+1 (41) instead of ACTION_MOVE+4 (34) — above every DPS
spender row (HIGH band), the interrupt rows, and the out-of-melee charge
path; below EMERGENCY defensives. A mob peeling onto a non-tank member
gets taunted back within a GCD instead of waiting behind shield slam,
interrupts, and charge movement.
## Tank-face as a real strategy for any tank (POS-6) — 2026-10-09

Feature: `TankFaceStrategy` (`tank face`), donor `TankFaceStrategy` shape.
The old `tank face needed` → `tank face away` row moves off the shared
`close` strategy onto its own strategy; the trigger's `HasRealPlayerMaster`
gate becomes strategy membership, so pool/raid tanks face held mobs away
from the party too. `AiFactory` adds `tank face` to the warrior-protection,
paladin-protection, and druid-tank-feral kits (donor factory shape:
`IsTank → +tank face`). Stay/wait-for-attack exemptions kept; the geometry
(average party angle ±108°, nearest side, LoS/terrain, 90° hysteresis) is
unchanged. `TankFaceAwayAction` itself is untouched.
## Opt-in combat spread for owned/hired bots and melee (POS-3) — 2026-10-09

Feature: `SpreadStrategy` (`spread`, donor `formation` shape) + `spread
distance` manual value (donor `disperse distance` shape: -1 unset, 5yd
ranged / 2yd melee defaults) + `SpreadNeededTrigger` (`spread needed`) +
`.bot behavior <scope> spread on|off` (BehaviorToggles table, persisted,
reported in TBM:BOTSTATE automatically). `RaidSpreadAction` generalizes:
with `spread` on, owned/hired bots and melee are eligible and the manual
knob (or role default) is the "too close" radius; the legacy pool-only
path (ranged, 10yd, owner-exempt) is byte-identical when off. Combat-only
and hold orders (stay/follow/wait-for-attack/grind) veto in both paths.
Pure gate/radius rules extend `ai/playerbot/CombatSpreadPolicy.h`
(`ShouldOptInSpread`, `SpreadRadius`), tested by
`tools/test_spread_toggle_policy.cpp` (wired into `verify_all.sh`).
Default: nobody (opt-in); no factory change. The legacy pool path is
near-identical when off except the action now also requires ranged (aligns
with the trigger; Onyxia P2 direct dispatch no longer spreads pool
melee). The action returns true on a step (consumes the tick, displaces
only filler DPS at ACTION_NORMAL) where the donor always returns false.
## Generic rear-flank for melee (POS-1) — 2026-10-09

Feature: `RearFlankAction` (`rear flank`) + `RearFlankNeededTrigger`
(`rear flank needed`), donor `RearFlankAction` shape. A melee bot standing
in the mob's frontal arc (2x90 degrees) or tail cone (outside 2PI-120
degrees) sidesteps to the nearer of +-frand(90, 120)-degree polar offsets
at half melee range instead of walking straight through the cleave to the
exact rear point. Wired flank-first on the `behind` strategy
(`ACTION_HIGH + 1`, above `set behind` at `ACTION_HIGH`); every melee DPS
kit with `behind` gets it with no factory change. Tanks holding the mob
never flank (trigger excludes victim == bot, keeps the tank-face path);
dragon raid geometry untouched. Pure angle math in
`ai/playerbot/RearFlankPolicy.h`, tested by
`tools/test_rear_flank_policy.cpp` (wired into `verify_all.sh`).

Source repository: `mod-playerbots/mod-playerbots`

Source commit: `79bd4281` (local
`playerbots-references/mod-playerbots` checkout).

Source files:
- `src/Ai/Base/Actions/MovementActions.cpp` (`AvoidAoeAction::isUseful`/`Execute`, `AvoidAuraWithDynamicObj`, `AvoidGameObjectWithDamage`, `AvoidUnitWithDamageAura`, `BestPositionForMeleeToFlee`, `BestPositionForRangedToFlee`, `FleePosition`)
- `src/Ai/Base/Actions/MovementActions.h` (declarations)
- `src/Ai/Base/Value/PossibleTargetsValue.cpp` (`PossibleTriggersValue`)
- `src/Ai/Base/Value/NearestGameObjects.cpp` (`NearestTrapWithDamageValue`)

Source files (module, modified): `ai/playerbot/AvoidAoePolicy.h` (new),
`ai/playerbot/strategy/actions/MovementActions.{h,cpp}` (action),
`ai/playerbot/strategy/actions/ActionContext.h` (registration),
`ai/playerbot/strategy/values/PossibleTargetsValue.{h,cpp}` (trigger
sensor), `ai/playerbot/strategy/values/NearestGameObjects.{h,cpp}` (trap
sensor), `ai/playerbot/strategy/values/ValueContext.h` (registrations),
`ai/playerbot/strategy/generic/CombatStrategy.cpp` (strategy rewiring),
`tools/test_avoid_aoe_policy.cpp` + `tools/verify_all.sh` (new standalone
test), `docs/guides/dungeon-tactics.md` (doc line).

Copied / ported / reimplemented: ported, adapted to the 1.12 codebase.
Deviations from the donor, all deliberate: (a) 1.12 aura APIs
(`GetAurasByType`, `EffectTriggerSpell[]`, `sSpellRadiusStore`) instead of
AzerothCore `AuraEffect` lists; (b) candidates validate through `FindStep`
(LoS + path + unpulled-hostile aggro guard) instead of the donor's
collision-only check; (c) no `bot->Say` avoidance spam and no spell
whitelist config (no stack-mechanic false positives observed — add if
needed); (d) reactive `flee` kept as the fallback when no strafe landing
validates; (e) 1000 ms donor throttle replaced by the existing
flee-failure observation windows.

Reason: AoE avoidance was reactive-only (fired after the debuff landed)
and fled blindly — often toward the tank inside the same void zone.
- `src/Ai/Class/Warrior/Strategy/TankWarriorStrategy.cpp:299-305` (proc slam at INTERRUPT)
- `src/Ai/Class/Warrior/Strategy/TankWarriorStrategy.cpp:184-192` (slam at medium rage HIGH+2)

Copied / ported / independently reimplemented: reimplemented in place
(`ProtectionWarriorStrategy.cpp`, `WarriorTriggers.h`,
`WarriorAiObjectContext.cpp`, all `ai/playerbot/strategy/warrior/`).
Deviations from the donor, all deliberate: (a) no "Sword and Board" aura
name exists in 1.18.1 — Turtle's Improved Shield Slam talent
(51598/51599, PROC_FLAG 0x10 = melee-ability hit procs the 1-charge
+35%/+70% damage aura 51596/51597) fills the slot, matched by spell id
(`ai->HasAura(51596/51597, bot)`) because the permanent talent shares the
"Improved Shield Slam" name and would keep a name trigger active forever;
(b) the dead `SwordAndBoardTrigger` class (`HAS_AURA "sword and board"`,
unregistered since it could never fire) is deleted — the dead
new-architecture `TankWarriorStrategy.cpp:224` reference is left alone
(never instantiated); (c) WAR-7's briefed "challenging shout medium→high"
is NOT done: donor `high aoe` is 4+ enemies/8yd while ours fires at
`melee medium aoe` (3+/5yd) with the `aoe` strategy default-on for
warriors — moving to 6+ would make tanks shout LESS than the donor, a
regression, and the report's "opt-in toggle" premise is wrong
(`AiFactory.cpp` adds `aoe` by default). Challenging shout stays where it
is.

Reason: WAR-2/WAR-6 in the warrior parity sweep: the talented proc never
fired slam, and 20-rage slam starved the sunder stack and revenge GCDs.

Review fixes: (1) proc row INTERRUPT → HIGH+5 — the taunt half of the
finding is stale (merged PR #598 already puts taunt at 41, above the old
proc 40 = donor order), but the shield-block half was real (donor block
41 > proc 40), so proc now ties block with block winning ties; (2) sunder
veto rage-aware per above. Not changed: baseline stays HIGH+4 above
revenge/sunder (1-GCD vs donor tie; keeps proc>baseline ordering), dead
`TankWarriorStrategy.cpp:224` reference untouched (dead file, churn), proc
ids 51596/51597 verified in live `spell_template` (100% chance, 1 charge;
only the separate proc_event row is DBC-side).

Local validation: `bash tools/verify_all.sh`; `git diff --check`. No live
test (per task constraints).
- `src/Ai/Class/Warrior/Strategy/TankWarriorStrategy.cpp:322-329` (protect party member → intervene at EMERGENCY)

Copied / ported / independently reimplemented: reimplemented in place
(`ProtectionWarriorStrategy.cpp`, `WarriorStrategy.cpp`,
`ai/playerbot/strategy/warrior/`). Deviations from the donor: none in
behavior — same trigger, same priority. The trigger (`protect party
member` → `party member to protect` value), the PROTECT action, and both
context registrations already existed; only the strategy row and the live
stance node were missing.

Reason: WAR-8 in the warrior parity sweep: full Intervene infrastructure
with zero live consumers.

Local validation: `bash tools/verify_all.sh`; `git diff --check`. No live
test (per task constraints).
- `src/Ai/Class/Warrior/Strategy/TankWarriorStrategy.cpp:233-249` (shield wall at low health MEDIUM_HEAL; last stand at critical EMERGENCY)

Copied / ported / independently reimplemented: reimplemented in place in
`ProtectionWarriorStrategy.cpp` (`ai/playerbot/strategy/warrior/`).
Deviations from the donor, all deliberate: (a) no Enraged Regeneration at
critical — no such player spell row exists in 1.18.1; (b) Wall's existing
BUFF_ACTION aura check prevents re-firing while already up, so Wall firing
early at low health cannot consume it twice at critical.

Reason: WAR-4 in the warrior parity sweep: both cooldowns fired stacked
below 20%, wasting the longer-cooldown Wall.

Local validation: `bash tools/verify_all.sh`; `git diff --check`. No live
test (per task constraints).

## Accept-time solo-capability gate (RPG-A2, 2026-10-09)

Donor: mod-playerbots (`79bd4281`):
`src/Ai/World/Rpg/Action/NewRpgBaseAction.cpp:573-588`
(IsQuestCapableDoing: refuse questLevel > level+3, type != 0,
suggestedPlayers >= 2).

Source files (module, modified): `ai/playerbot/QuestLogPolicy.h` (new
pure QuestAcceptSoloCapable rule, same numbers as the drop triage minus
drop-only clauses), `ai/playerbot/strategy/actions/AcceptQuestAction.cpp`
(gate in WouldAcceptQuest for masterless random bots),
`tools/test_quest_log_triage_policy.cpp` (extended with accept-gate
cases; already registered in verify_all) + `CHANGELOG.md` (doc line).

Copied / ported / reimplemented: reimplemented in place. Deviations from
the donor, all deliberate: (a) grouped-and-able bots (can fight boss: 4+
following members, same value the travel pick gate uses) keep
elite/dungeon/group quests - the donor has no carve-out because its random
bots never group for this path; (b) pool bots below 10 keep the stricter
+1 taker-fit already in WouldAcceptQuest; (c) repeatable/seasonal refusal
not ported (no 1.12 seasonal API; `rpg repeat quest` is an explicit
feature); (d) owned/hired bots follow the player, unchanged.

Reason: nearby NPCs and travel could accept elite/group quests the
nearly-full log triage would later drop - refused at accept instead.

Local validation: `bash tools/verify_all.sh` (incl. extended triage test
+ wiring check live-missing=0); `git diff --check`; shared-builder
compile check; no live in-game test.
- `src/Ai/Class/Warrior/Strategy/ArmsWarriorStrategy.cpp:100-107` (battle stance pin at HIGH+10)

Copied / ported / independently reimplemented: reimplemented in place
(`ArmsWarriorStrategy.cpp`, `ai/playerbot/strategy/warrior/`).
Deviations from the donor, all deliberate: (a) pin at NORMAL, not HIGH —
the buff spec is additive with combat and a HIGH pin would fight
Whirlwind's berserker node every tick; NORMAL still beats no other stance
row in the buff spec so the pin holds; (b) no new stance-prerequisite
nodes in the live factory — sweeping strikes already has its arms-scoped
battle node, charge falls back to reach melee, and the pin itself puts
overpower/mocking/retaliation in the right stance by default; (c) the AoE
sweeping-strikes multipliers keep managing the pack stance choice
unchanged.

Reason: WAR-5 in the warrior parity sweep: arms bots sat in berserker and
five Battle-locked abilities never fired.
- `src/Ai/Class/Warrior/Strategy/ArmsWarriorStrategy.cpp:217-224` (death wish HIGH+2)
- `src/Ai/Class/Warrior/Strategy/ArmsWarriorStrategy.cpp:244-251` (retaliation at almost-full-health EMERGENCY+1)

Copied / ported / independently reimplemented: reimplemented in place
(`ArmsWarriorStrategy.cpp`, `FuryWarriorStrategy.cpp`,
`ai/playerbot/strategy/warrior/`). Deviations from the donor, all
deliberate: (a) rows at HIGH, not HIGH+2/EMERGENCY+1 — they sit alongside
the existing HIGH recklessness/death-wish siblings and the boost spec is
already gated; EMERGENCY would outbid real survival reactions; (b) no
retaliation stance node in the live factory — it fires from battle stance,
which the WAR-5 pin holds (soft dependency; without that PR the row waits
for battle rather than dancing); (c) no enraged-regen row — no such player
spell in 1.18.1.

Reason: WAR-10 in the warrior parity sweep: arms boost had recklessness
only (death wish just a fallback alternative), and neither spec gated
retaliation on the winning-health band.

Local validation: `bash tools/verify_all.sh`; `git diff --check`. No live
test (per task constraints).

## Vaelastrasz BA refinement: victim hold + repulsion flee (raid1 item 6) — 2026-10-09

Donor: mod-playerbots @ `79bd4281` (local checkout
`../playerbots-references/mod-playerbots`):
`src/Ai/Raid/BWL/BWLActions.cpp:142-265` (VaelastraszMoveAway: victim
holds while boss lives; weighted-repulsion flee from non-BA bots within
20y; BA carriers cluster; tail-sweep recovery past 30y; ranged past 25y
blends toward the boss), `src/Ai/Raid/BWL/BWLMultipliers.cpp:64-89`
(BA carriers: only the BA move runs, charge blocked), `src/Ai/Raid/BWL/
BWLStrategy.cpp:26-32` (rear-flank + BA runout wiring), donor constants
20y/30y/25y repulsion/recovery/bias.

Source files (module, modified): `ai/playerbot/VaelPolicy.h` (new pure
rule: repulsion/cluster/recovery/bias/victim-hold predicates),
`ai/playerbot/strategy/actions/DungeonActions.h/.cpp`
(VaelBurningAdrenalineFleeAction: victim hold, 20y weighted repulsion
ignoring BA carriers while boss lives, 30y tail-sweep recovery, 25y
ranged bias, 3y donor incremental steps via FindStep),
`ai/playerbot/strategy/generic/BlackwingLairDungeonStrategies.h/.cpp`
(`vael` fight strategy: bomb trigger re-queued to the flee at
EMERGENCY+7 above the universal runout, potion node, end-fight cleanup,
start trigger), `ai/playerbot/strategy/triggers/
BlackwingLairDungeonTriggers.h` (VaelStart/EndFightTrigger on entry
13020), `ai/playerbot/strategy/actions/BlackwingLairDungeonActions.h`
(enable/disable actions), registrations (`TriggerContext.h`,
`ActionContext.h`, `StrategyContext.h`), `tools/test_vael_policy.cpp`
(new standalone test) + `tools/verify_all.sh` (test list),
`docs/guides/dungeon-tactics.md` (doc line).

Copied / ported / reimplemented: reimplemented in our fight-strategy
idiom on top of the universal bomb trigger. Deviations from the donor,
all deliberate: (a) no new trigger — the universal `raid bomb debuff`
already fires on all three BA ids, so `vael` only re-queues it to the
smarter flee at higher priority; outside Vael the generic anchor-flee
runs unchanged; (b) no BA movement-lock multiplier: the donor blocks all
other movement once far from clean bots, but our flee re-fires each tick
until the aura expires and FindStep rejects bad landings, so a lock adds
ceremony without observable gain; (c) ranged LOS is inherited from
FindStep's own LOS check rather than the donor's two-pass LOS/no-LOS
search; (d) the 25y bias blends by snapping to the boss heading only when
fleeing away from it (avoids oscillation from weighted averaging).

Reason: raid1 gap BWL-VAEL (b): the universal runout flees blindly from
the anchor with no victim exemption and no BA clustering — the tank ran
and victims scattered into the clean raid.

Local validation: `bash tools/verify_all.sh` (all suites incl. the new
policy test pass); `git diff --check`. BA ids 18173/23478/23620 already
covered by the universal trigger; Vael 13020 in the flank list from the
BWL bundle PR. Build via build-commit.sh pending; live in-game check
pending.
`src/Ai/Class/Druid/DruidTriggers.h:368-420`
(`FerociousBiteTimeTrigger` with rip/roar duration reads,
`FerociousBiteExecuteTrigger` with CP/HP% gates) +
`src/Ai/Class/Druid/Strategy/CatDruidStrategy.cpp:158-178` (execute 24.0
> rip 23.5 > timed 22.5). Deviations, deliberate: donor savage-roar
clause dropped (no such spell in 1.18.1); donor absolute <20k-HP clause
dropped (tuned for WotLK pools — vanilla pools make HP% alone the right
execute gate).

Reason: druid parity report DRU-5 — flat CP5 bite fired regardless of
rip/bite windows, clipping Rip refreshes and missing executes.
`src/Ai/Class/Druid/Strategy/CatDruidStrategy.cpp:100-106`
(`healer low mana` -> `innervate on healer` 35.0). Deviations,
deliberate: per the parity coordination note this was implemented with a
LOCAL trigger because the shared `healer low mana` value/trigger
(parity/heal-2) is not on the integration branch yet — the local trigger
uses the exact shared name, so when that lands this trigger is deleted
and the cat row needs no change. No pvp/raid exclusion either (donor
ports sometimes scope this to pve; a thirsty healer needs Innervate in
any bracket).

Reason: druid parity report DRU-3 — feral drained nothing back to the
healer; innervate rows were balance/resto self-only.

Source files (module, modified):
`ai/playerbot/strategy/druid/DruidTriggers.h`,
`ai/playerbot/strategy/druid/DruidTriggers.cpp`,
`ai/playerbot/strategy/druid/DruidActions.h`,
`ai/playerbot/strategy/druid/DruidAiObjectContext.cpp`,
`ai/playerbot/strategy/druid/DruidStrategy.cpp` +
`docs/classes/druid.md` (behaviour lines).

Copied / ported / reimplemented: reimplemented in place in the live
strategy idiom. No new spells: Thorns ranks trainer-taught.
`ai/playerbot/strategy/druid/DruidAiObjectContext.cpp`,
`ai/playerbot/strategy/druid/DpsFeralDruidStrategy.cpp` +
`docs/classes/druid.md` (behaviour lines). The old flat
`FerociousBiteTrigger` creator stays registered (harmless, no live rows
reference it anymore).

Copied / ported / reimplemented: reimplemented in place in the live
strategy idiom. No new spells or actions: Ferocious Bite / Rip ranks
trainer-taught, creators pre-registered.
`src/Ai/Class/Druid/Strategy/GenericDruidNonCombatStrategy.cpp:116`
(`party member dead` -> `revive`, via the generic `PartyMemberDeadTrigger`)
`src/Ai/Class/Priest/Strategy/...` + paladin/shaman equivalents for the
ACTION_EMERGENCY non-combat row shape (`PaladinStrategy.cpp:95-97` etc).
Deviations, deliberate and verified against live `tw_world`: the donor's
`revive` spell does not exist as a druid-taught spell in 1.18.1 — report
claim "Revive 2435" is wrong (2435 = Numbing Strike here); the only
`Revive` row (24341) is a Zul'Gurub boss spell (boss_mandokir.cpp) with no
trainer or skill-line entry, while druid trainers teach Rebirth
(20484+, skill class_mask 1024 = druid). So the port casts Rebirth out of
combat instead of a non-existent Revive, and gates it on no living
priest/paladin/shaman in the group (their normal rez is always
preferred). Combat rebirth rows are untouched.

Reason: druid parity report DRU-1 — wired specs never resurrected out of
combat; every other healer class has the non-combat row.

Source files (module, modified): `ai/playerbot/OocRebirthPolicy.h` (new),
`ai/playerbot/strategy/druid/DruidTriggers.h`,
`ai/playerbot/strategy/druid/DruidTriggers.cpp`,
`ai/playerbot/strategy/druid/DruidStrategy.cpp`,
`ai/playerbot/strategy/druid/DruidAiObjectContext.cpp`,
`tools/test_ooc_rebirth_policy.cpp` (new), `tools/verify_all.sh` +
`docs/classes/druid.md` (behaviour lines).

Copied / ported / reimplemented: reimplemented in place in the live
strategy idiom. No new spells: Rebirth 20484/20739/20742/20747/20748
verified in spell_template (family 7, druid skill line).
`docs/classes/druid.md` (behaviour lines).

Copied / ported / reimplemented: reimplemented in place in the live
strategy idiom. No new spells: Innervate 29166 verified in
spell_template; trigger/action creators registered in the druid context.

Local validation: `bash tools/verify_all.sh`; `git diff --check`. No
live test (no live test per parity brief); build via build-commit.sh.

## Review fixes round 2 (2026-10-10, PR #590 CHANGES_REQUESTED)
Blocking finding verified real and fixed: new `thorns on tank` action
had no ActionNode — `Engine::CreateActionNode` falls back to a bare node
with NULL prerequisites, so a shapeshifted druid fails the cast via
`GetErrorAtShapeshiftedCast` and the tank row never beats the blanket
for shifted druids. Fixed: `thorns_on_tank` caster-form node in
`DruidStrategyActionNodeFactory` (same shape as every sibling buff row).
Non-blocking: check interval matched to sibling/donor 4; MotW-vs-thorns
ordering kept (tank-first is the feature); early-refresh overlap left as
harmless (reviewer agrees).
verify_all.sh + build-commit.sh + push to same branch per brief (see summary).
## Review fixes (2026-10-09, PR #582 CHANGES_REQUESTED)
Blocking finding verified real and fixed: without an absolute-HP gate,
the execute row (CP>=1 at +6) eats every combo point on any sub-25%
target before Rip (CP>=3 at +4) can refresh — confirmed by trigger/row
priorities, so Rip would fall off for the whole execute phase on bosses.
Fixed with the donor's absolute gate scaled to vanilla: fire only when
remaining HP < 4000 (donor: < 20000; WotLK top-rank bite hits roughly an
order of magnitude harder than vanilla ranks, verified via
spell_template bite values). Bosses keep Rip; trash and near-dead
targets still eat early bites.
Non-blocking raid note (no rip row in raid kit): pre-existing gap,
unchanged by this PR (pre-PR flat CP5 bite behaved the same there);
left for a follow-up, not widening this diff.
verify_all.sh PASSED, build-commit.sh BUILD OK (commit pending push to same branch).
- `src/Ai/Base/Actions/MovementActions.cpp` (`MovementAction::CheckLastFlee`; consults in avoid-aoe `FleePosition`, tank-face, set-behind)

Source files (module, modified):
`ai/playerbot/strategy/actions/MovementActions.cpp`
(`TankFaceAwayAction::Execute`, `SetBehindTargetAction::Execute`),
`docs/concepts/bot-mechanics-and-quirks.md` (doc line).

Copied / ported / reimplemented: ported, adapted to the 1.12 codebase.
Deviations from the donor, all deliberate: (a) failures are observed
outcomes (2 yd rule), not dispatches — a successful heading stays
repeatable, where the donor vetoes any recent flee heading; (b) two-pass
fallback (vetoed headings allowed on the second pass) so the memory can
never strand the bot; (c) tank-face and set-behind share the `fleeFailures`
instance anchored per-target, no new `LastMovement` fields; (d) the
`AvoidAoeAction::FleePosition` consumer arrives with POS-2 (this PR wires
the helper into the existing sidesteps only).

Reason: tank sidesteps and rear approaches could alternate headings every
tick when two triggers disagreed, jittering instead of settling.
- `src/Ai/Base/Strategy/CombatStrategy.cpp` (`TankFaceStrategy`: triggerless, default action `tank face` at ACTION_MOVE)
- `src/Ai/Base/Strategy/CombatStrategy.h` (declaration)
- `src/Bot/Factory/AiFactory.cpp` (`IsTank → +tank face`)

Source files (module, modified):
`ai/playerbot/strategy/generic/MeleeCombatStrategy.{h,cpp}` (new strategy,
row moved off `close`), `ai/playerbot/strategy/StrategyContext.h`
(registration), `ai/playerbot/strategy/triggers/GenericTriggers.cpp`
(membership gate), `ai/playerbot/AiFactory.cpp` (3 tank kits),
`docs/concepts/bot-mechanics-and-quirks.md` (doc row).

Copied / ported / reimplemented: ported, adapted to the 1.12 codebase.
Deviations from the donor, all deliberate: (a) the row keeps its trigger
(`tank face needed`) instead of going fully triggerless — the trigger
carries the hysteresis math and the stay/wait exemptions the donor lacks;
(b) priority stays ACTION_MOVE (donor shape) rather than the old
ACTION_MOVE + 5; (c) no `recently flee info` consult yet (POS-7
generalizes the helper later).

Reason: pool/raid tanks never faced mobs away — cleaves hit the party
whenever no real-player master led the group.

Local validation: `bash tools/verify_all.sh`; `git diff --check` clean.
Build via build-commit.sh (see PR summary); no live in-game check.

## Review fixes (2026-10-09, reviewer muse-1.3 max)

Blocking 1 (failure-memory success rule vs sidesteps — REAL, fixed by
revert): tank sidesteps preserve radius and rear approaches stay in melee
by construction, so a successful sidestep never gains the 2 yd the
`fleeFailures` success rule needs; the per-tick global observer would
record it as a failure after 3 s and veto the heading for genuine flees
too. Removed both consults (`SetBehindTargetAction` rear veto +
`BeginAttempt`, `TankFaceAwayAction` two-pass veto + `Observe` +
`BeginAttempt`); both actions are byte-identical to pre-PR behaviour plus
a comment naming the 90-degree trigger window as the anti-oscillation.

Blocking 2 (3 s rule cannot damp per-tick ping-pong — REAL, same fix):
alternating L/R sidesteps restart `BeginAttempt`'s pending clock each
dispatch, so no failure is ever recorded. Reverted rather than adding a
new dispatch-time veto: per the report, the trigger's 90-degree
hysteresis window (fires only while the mob's front points at the party
side; the tank lands ~108 degrees off, outside the window) is the
dampener, and no live jitter has been observed. If a live test shows
alternation, the fix is a sidestep re-dispatch throttle, not the failure
memory.

Blocking 3 (stale-anchor consult order — MOOT after revert): the rear
`IsHeadingFree` check before any `Observe` is gone with the consult
itself. No fix needed.

Non-blocking "reversal veto" wording — ACCEPTED: the code vetoes the same
heading as the failure while the donor vetoes its reverse
(`info.angle + PI`). Reworded the PROVENANCE entry and memory doc row to
"same-heading veto".

Non-blocking "hold instead of second pass" — MOOT after revert (no
second pass remains).

Non-blocking "no live jitter test" — ACKNOWLEDGED: still no live test;
needs an in-game sidestep-alternation check before merge.
| Sapphiron fight (Naxx): hover-based air detection, iceblock hide, blizzard step-out, ground flank | `mod-playerbots` | `79bd4281` | `src/Ai/Raid/Naxx/Action/NaxxActions_Sapphiron.cpp`, `src/Ai/Raid/Naxx/NaxxBossHelper.h` (SapphironBossHelper), `src/Ai/Raid/Naxx/NaxxStrategy.cpp` (Sapphiron rows) | Reimplemented trigger-driven; air detection via MOVEFLAG_HOVER not IsFlying (vanilla SetFly commented out); blizzard avoid via NPC 16474 grid step-out (no dynobj dependency) | IDs verified in tw_world (15989, 28522, 28534/28547, 16474, 181247); core boss_sapphiron.cpp hover + icebolt/blizzard confirmed | `bash tools/verify_all.sh` + `tools/test_sapphiron_ice_policy.cpp`; build-commit + no live test (hide timing wants a live run) |
- `src/Ai/Class/Warrior/Strategy/TankWarriorStrategy.cpp:217-232` (`lose aggro` -> taunt at ACTION_INTERRUPT+1)

Copied / ported / independently reimplemented: reimplemented in place in
the live list-based engine (`ProtectionWarriorStrategy.cpp`,
`ai/playerbot/strategy/warrior/`). Deviations from the donor, all
deliberate: (a) no heroic-throw fallback chain — Heroic Throw has no
1.18.1 player spell row, and the existing `taunt` -> `battle shout taunt`
fallback node already covers taunt failure; (b) the `lose aggro` trigger
itself (`GenericTriggers.cpp`, `ai->IsTank` check + non-tank-victim gate)
is kept as-is — no `main tank` value or LowTankThreat wiring since that
LD-8 branch is not on the integration branch yet — plus a player-target
guard the bump made load-bearing (players have no threat list; without it
the trigger is spuriously active in PvP and the 41-priority taunt would
outbid real interrupts for zero effect); (c) `taunt on snare target` stays
at ACTION_MOVE (different target: adds, not the lost-aggro mob).

Reason: WAR-3 in the warrior parity sweep: the tank's taunt sat below
interrupts and the intercept/charge path, so a peeled mob waited behind a
missed kick and a charge GCD while the healer took hits.

Local validation: `bash tools/verify_all.sh`; `git diff --check`. No live
test (per task constraints).
## Equip-upgrade score threshold (AG-1, 2026-10-09)

Donor: mod-playerbots (`79bd4281`):
`src/PlayerbotAIConfig.cpp:716` (`EquipUpgradeThreshold = 1.1f`),
`src/Ai/Base/Value/ItemUsageValue.cpp:308`
(`itemScore > oldScore * threshold`).

Source files (module, modified): `ai/playerbot/EquipThresholdPolicy.h`
(new pure better verdict: strictly above old * threshold),
`ai/playerbot/strategy/values/ItemUsageValue.cpp` (isBetter score arm
routed through the policy),
`ai/playerbot/PlayerbotAIConfig.{h,cpp}` +
`ai/playerbot/aiplayerbot.conf.dist.in` (new
`AiPlayerbot.EquipUpgradeThreshold = 1.1`, default 1.1),
`tools/test_equip_threshold_policy.cpp` (new standalone test),
## Ready-check rebuff defer (SOC-S5, 2026-10-09)

Donor: mod-playerbots (`79bd4281`):
`src/Ai/Base/Actions/ReadyCheckAction.cpp:160-182` (defer reply when
ForceRebuffOnReadyCheck + force-rebuff strategy; ReportReadiness +
SendReadyConfirm split), `:252-276` (ForceRebuffAction + ReadyReplyAction),
`src/Bot/ForceRebuff.{h,cpp}` (pending window, GCD/cycle guards, buff-first
multiplier), `src/Ai/Base/Trigger/GenericTriggers.cpp:720`
(ForceRebuffPendingTrigger), `src/Ai/Base/StrategyContext.h:164-165`
(force-rebuff strategy registration).

Source files (module, modified): `ai/playerbot/ReadyRebuffPolicy.h` (new
pure rule: 8 s grace once not casting, 30 s hard cap that always replies),
`ai/playerbot/strategy/actions/ReadyCheckAction.{h,cpp}` (defer branch on a
real ready-check packet when the key is on and out of combat; ReadyCheck
split into ReportReadiness + SendReadyConfirm; new ReadyReplyAction),
`ai/playerbot/strategy/triggers/GenericTriggers.{h,cpp}` (new
ForceRebuffPendingTrigger: anchor set, cheap first),
`ai/playerbot/strategy/triggers/TriggerContext.h`,
`ai/playerbot/strategy/actions/WorldPacketActionContext.h`,
`ai/playerbot/strategy/generic/WorldPacketHandlerStrategy.cpp` (pending ->
ready-reply node on the always-present default strategy),
`ai/playerbot/PlayerbotAIConfig.{h,cpp}` +
`ai/playerbot/aiplayerbot.conf.dist.in` (new
`AiPlayerbot.ForceRebuffOnReadyCheck = 0`, default off),
`tools/test_ready_rebuff_policy.cpp` (new standalone test),
`tools/verify_all.sh` (register test) +
`docs/guides/configuration-tuning.md`, `CHANGELOG.md` (doc lines).

Copied / ported / reimplemented: reimplemented in place. Deviations from
the donor, all deliberate: (a) the threshold applies only to the
`isBetter` score arm - exact ties still fall through to the sheet /
quality / item-level tiebreaks, and spec-transition, broken-gear, forced,
and zero-weight-first-stats swaps return above untouched; (b) 1.0 restores
any-gain swaps for operators who want them.

Reason: bots swapped gear on any epsilon stat-weight gain, churning swaps
across audits for nothing.
the donor, all deliberate: (a) no force-rebuff strategy, buff-cycle hooks,
or heal-suppression multiplier - buffs flow through the normal per-tick
engine during the hold, only the confirm packet is delayed; (b) the pending
state is a per-bot "manual time" anchor value, not a PlayerbotAI member, so
no AI header change; (c) the reply can never wedge: past the 30 s cap the
action replies even mid-cast (the donor window can expire unanswered), and
the finish path clears the anchor so no double confirm goes out; (d) manual
"ready" whispers (empty packet) and in-combat checks answer immediately.

Reason: bots answered ready instantly and then buffed through the pull; the
raid saw ready while the bot was still casting.

Local validation: `bash tools/verify_all.sh` (incl. new policy test +
wiring check); `git diff --check`; shared-builder compile check; no live
in-game test.
## Warlock Immolate spreading (WAR-2) — 2026-10-09

Donor: mod-playerbots (`79bd4281`):
`src/Ai/Class/Warlock/Strategy/DemonologyWarlockStrategy.cpp:58-81`
(`immolate on attacker` 19.0 + `immolate` 17.5),
`WarlockTriggers.h:202-207` + `WarlockActions.h:287-296`
(`DebuffOnAttacker` pair).

Source files (module, modified):
`ai/playerbot/strategy/warlock/WarlockTriggers.h`
(`ImmolateOnAttackerTrigger : DebuffOnAttackerTrigger`),
`ai/playerbot/strategy/warlock/WarlockActions.h`
(`CastImmolateOnAttackerAction : CastRangedDebuffSpellOnAttackerAction`),
`ai/playerbot/strategy/warlock/WarlockAiObjectContext.cpp` (registered
both names), `ai/playerbot/strategy/warlock/WarlockStrategy.cpp`
(`WarlockAoeStrategy` row at HIGH-1 next to corruption on attacker),
`ai/playerbot/strategy/warlock/DemonologyWarlockStrategy.cpp` +
`DestructionWarlockStrategy.cpp` (spec-level row at NORMAL) +
`docs/classes/warlock.md`, `CHANGELOG.md` (doc lines).

Copied / ported / reimplemented: reimplemented as an exact mirror of
the live corruption on-attacker pair — no new policy header (nothing to
decide beyond the shared `DebuffOnAttacker` + 16-cap guard). Deviations
from the donor, all deliberate: (a) no ≤20% target-health gate on the
spreader (the corruption spreader has none either — the gate lives on
the single-target trigger only); (b) affliction spreads via the aoe row
only, keeping its single-target Agony/Siphon economy untouched.

Reason: demo/destro bots dotted one mob while adds beat on them; donor
spreads Immolate at spec level.

Local validation: `bash tools/verify_all.sh` (wiring audit covers the two
new names); `git diff --check`; shared-builder compile via
`build-commit.sh` (BUILD OK); live in-game check pending: 2+ attackers
each gain immolate, single-target rotation unchanged.
## Warlock spec-level DoT spreading (WAR-10) — 2026-10-09

Donor: mod-playerbots (`79bd4281`):
`src/Ai/Class/Warlock/Strategy/AfflictionWarlockStrategy.cpp:32-48` +
`DemonologyWarlockStrategy.cpp:49-65` +
`GenericWarlockStrategy.cpp:142-160` (corruption/UA/agony on attacker at
spec level, independent of aoe).

Source files (module, modified):
`ai/playerbot/strategy/warlock/AfflictionWarlockStrategy.cpp`
(`corruption on attacker` + `siphon life on attacker` at NORMAL),
`ai/playerbot/strategy/warlock/DemonologyWarlockStrategy.cpp`
(`corruption on attacker` at NORMAL) + `docs/classes/warlock.md`,
`CHANGELOG.md` (doc lines). No new triggers/actions — the on-attacker
pairs and the `WarlockAoeStrategy` HIGH-1 rows already exist.

Copied / ported / reimplemented: reimplemented as priority layering.
Deviations from the donor, all deliberate: (a) the aoe HIGH-1 rows stay
— spec NORMAL rows are a fallback when aoe is off (aoe is default-on, so
this only bites when the player disables it); (b) NORMAL sits below
single-target upkeep (NORMAL+1/2), urgent tap and Dark Pact, above filler
tap — spreading never starves the main rotation or mana recovery (the
mana sanity); (c) no Unstable Affliction (WotLK-only); agony spread stays
aoe-gated in the base curses strategy; (d) destruction gets no row here —
its spreader is immolate, which is WAR-2's unmerged row; immolate-on-aoe
composes with this when both land.

Reason: with aoe toggled off, multi-mob pulls dotted one target while
adds beat on the bot; donor spreads at spec level regardless.

Local validation: `bash tools/verify_all.sh` (no new names — wiring
unchanged); `git diff --check`; shared-builder compile via
`build-commit.sh` (BUILD OK); live in-game check pending: aoe-off
multi-pull dots each attacker, mana stays out of the urgent band.
| Resto shaman healer-dps (SHM-1) | `mod-playerbots` `src/Ai/Class/Shaman/Strategy/RestoShamanStrategy.cpp:57-64` (`healer should attack` flame shock / lightning bolt / chain lightning) @ `79bd4281`, priest `PriestOffdpsStrategy` (`PriestStrategy.cpp:616-645`) as the live pattern | `ai/playerbot/strategy/shaman/ShamanStrategy.{h,cpp}` (new `ShamanOffdpsStrategy` + pve/pvp/raid), `ShamanAiObjectContext.cpp` (`offdps` placeholder + `OffdpsSituationStrategyFactoryInternal` + registration), `ShamanActions.h` (update-strats nodes), `ShamanHealerDpsPolicy.h` + `tools/test_shaman_healer_dps_policy.cpp` | Ported minus lava burst (WotLK-only): `healer should attack` flame shock +0.2 / lightning bolt default, `ranged medium aoe and healer should attack` chain lightning +0.3. Fills the dangling `offdps` name `AiFactory.cpp` already adds for resto when `enableOffSpecStrategies` is on. Spells: Flame Shock 8050, Lightning Bolt 403, Chain Lightning 421 (all verified in tw_world.spell_template) | `bash tools/verify_all.sh` (incl. new policy test), `git diff --check`; shared-builder compile + no live test per parity pipeline |
## Review fixes (2026-10-09, reviewer muse-1.3 max, CHANGES_REQUESTED)

Blocking 1 (solo ungrouped feral-tank override — REAL, fixed): the
solo→tank flip row now adds `tank face` alongside `tank assist`/`close`,
so a druid flipped before joining a group faces mobs away.

Blocking 2 (BG feral-tank override — REAL, fixed): the BG tanking row now
adds `tank face`, closing the pool-BG-tank hole.

Non-blocking "triggerless comment" — FIXED: comment now says trigger row
(kept for hysteresis + stay/wait exemptions), matching code and
PROVENANCE.

Non-blocking "party-angle distance filter" — ACKNOWLEDGED, not fixed:
donor filters to ranged within sight; ours averages all live
same-map members. Pre-existing, amplified by raid scope. Needs a live
raid check or a distance filter — left for playtesting.

Non-blocking "off-spec forced tanks" — ACKNOWLEDGED, not fixed: arms/ret
with forced tank role never get the `tank face` kit (old code fired via
`close` + `IsTank`). Donor misses these too and the case is rare; noting
the regression for a follow-up.
- `src/Ai/Base/Actions/MovementActions.cpp` (`CombatFormationMoveAction::Execute`: disperse-distance step-out; `DisperseSetAction::Execute`: enable/reset 5yd ranged / 2yd melee, disable, +-1yd)
- `src/Ai/Base/Actions/MovementActions.h` (`DEFAULT_DISPERSE_DISTANCE_RANGED/MELEE`)

Source files (module, modified): `ai/playerbot/CombatSpreadPolicy.h`
(rules), `ai/playerbot/strategy/values/RangeValues.{h,cpp}` (`spread
distance` value), `ai/playerbot/strategy/values/ValueContext.h`
(registration), `ai/playerbot/strategy/generic/CombatStrategy.{h,cpp}`
(`spread` strategy), `ai/playerbot/strategy/StrategyContext.h`
(registration), `ai/playerbot/strategy/triggers/DungeonTriggers.{h,cpp}`
(`spread needed` gate), `ai/playerbot/strategy/triggers/TriggerContext.h`
(registration), `ai/playerbot/strategy/actions/DungeonActions.cpp`
(action generalization + tank veto + radius-scaled step),
`ai/playerbot/strategy/actions/SpreadDistanceAction.{h,cpp}` (new knob
writer) + `ChatActionContext.h` (registration),
`commands/BotCommands.cpp` (behavior toggle +
usage), `tools/test_spread_toggle_policy.cpp` + `tools/verify_all.sh`
(new standalone test), `docs/guides/player-controls.md`,
`docs/guides/dungeon-tactics.md` (doc lines).

Copied / ported / reimplemented: ported, adapted to the 1.12 codebase.
Deviations from the donor, all deliberate: (a) no `disperse set` chat
increments — the knob is a persisted value, the toggle is `.bot behavior`;
(b) hold-order vetoes kept even when opted in (owner "explicit orders beat
automation"; stay/guard/wait-for-attack gate in combat, follow/grind params are
near-dead NON_COMBAT checks kept for symmetry); (c) step-out reuses the existing failure-memory + FindStep
path (LoS/path/aggro-checked) instead of the donor's blind flee; (d) melee
at 2yd only when the player opts in (default stacking unchanged).

Reason: a player's own raid clumped on chain-cleave bosses — spread was
pool-bot-only and ranged-only.
- `src/Ai/Base/Actions/MovementActions.cpp` (`RearFlankAction::isUseful` + `::Execute`: front/tail arc checks, polar offsets at meleeRange x 0.5, nearest side)
- `src/Ai/Base/Actions/MovementActions.h` (declaration: 90-degree min / 120-degree max cone constants)

Source files (module, modified): `ai/playerbot/RearFlankPolicy.h` (new),
`ai/playerbot/strategy/actions/MovementActions.{h,cpp}` (action),
`ai/playerbot/strategy/triggers/GenericTriggers.{h,cpp}` (trigger),
`ai/playerbot/strategy/{actions/ActionContext.h,triggers/TriggerContext.h}`
(registration), `ai/playerbot/strategy/generic/MeleeCombatStrategy.cpp`
(flank-first rows), `tools/test_rear_flank_policy.cpp` +
`tools/verify_all.sh` (new standalone test),
`docs/concepts/bot-mechanics-and-quirks.md` (doc row).

Copied / ported / reimplemented: ported, adapted to the 1.12 codebase.
Deviations from the donor, all deliberate: (a) destination validates
through LoS-hit + `IsWithinLOS` mirroring `SetBehindTargetAction` (the
donor moves blind); (b) trigger excludes the tank-held case so tank-face
keeps owning it; (c) `BossRearFlankAction` per-boss overrides not ported
(no per-boss raid scripts in generic scope); (d) no anti-oscillation
consult yet (POS-7 generalizes the helper later).

Reason: melee DPS walked directly through frontal cleaves/tail cones to
reach the exact rear point, eating avoidable damage on any mob.

Local validation: `bash tools/verify_all.sh` (incl. new policy test); `git
diff --check` clean. Build via build-commit.sh (see PR summary); no live
in-game check.

## Review fixes (2026-10-10, reviewer muse-1.3 max, CHANGES_REQUESTED)

Blocking 1 (deleted `nearest dynamic objects no los` registration — REAL,
fixed): my sensor registration edit had dropped the line; both the new
`AvoidAuraWithDynamicObj` and the existing `HasAreaDebuffValue` read that
key. Restored alongside the new `nearest damaging traps` entry.

Blocking 2 (strafe gated behind reactive trigger — REAL, fixed): `avoid
aoe` now runs as a combat + reaction default action (donor shape,
self-gated via `isUseful` over all three sensors every tick); the `has
area debuff` row keeps only the reactive `flee` fallback.

Blocking 3 (inverted melee candidates + strict tank lockout — REAL,
fixed): melee order is now strafe/strafe/toward-target(strict) with
away-from-target and away-from-hazard as non-strict tanking fallbacks;
strict rule is index-based for melee. Test updated to donor order.

Blocking 4 (raw center distance vs reach — REAL, fixed): landing now
subtracts `target->GetCombatReach(bot, false, 0.0f)` before the band
tests, so large mobs/bosses don't fail every strict landing.

Non-blocking "direct context lookup" — FIXED: `AI_VALUE` macro like the
rest of the module. Non-blocking "NearestDynamicObjects empty stub" —
ACKNOWLEDGED: sensor case 1 degrades gracefully (documented); core grid
visitor is a host-side gap, not this PR.

## Review fixes round 2 (2026-10-10, reviewer muse-1.3 max, CHANGES_REQUESTED)

Blocking 1 (default actions never evaluated — REAL, fixed differently
than suggested): verified in code that `ReactionEngine::FindReaction`
calls only `ProcessTriggers()` (never `PushDefaultActions()`), and
`avoid aoe` is only on the reaction engine (not the combat engine), so
the reviewer's `getDefaultActions` fix would also be dead. Instead added
a real `aoe threat nearby` trigger (dynobj aura OR damaging traps OR
trigger NPCs, all value-cached) and pointed the strategy row at it, with
`avoid aoe` first and reactive `flee` as fallback. Removed the dead
`GetDefault*Actions` overrides.

Blocking 2 (no-target fallback steps toward the hazard — REAL, fixed):
with no target the offset-0 slot headed at the zone center with the band
check skipped. Now the heading base falls back to away-from-hazard (donor
else-branch shape).

Prior blocking #4 follow-up (melee-range helper over-subtracts — REAL,
fixed): landing now subtracts raw `target->GetCombatReach()`.

Non-blocking "donor melee strict needs IsWithinMeleeRange" —
ACKNOWLEDGED, not fixed: the port band-rejects whenever a target exists
even out of melee range. Donor-faithful would skip the band check when
already out of range; left for playtesting.

Non-blocking "no AutoAvoidAoe/owner gate" — ACKNOWLEDGED, deliberate:
enabled for all bots per the feature brief (reaction engine
membership). No config gate added.

Non-blocking "trap scan interval" — FIXED: `NearestDamagingTrapsValue`
now checkInterval 2 (1 s cadence, like `possible triggers`).
## Review fixes (2026-10-09, reviewer muse-1.3 max, CHANGES_REQUESTED)

Blocking 1 (dead `spread distance` knob — REAL, fixed): added
`SpreadDistanceAction` (`spread distance` chat action, RangeAction
mirror: `<yards>` set, `?` read, `off`/`reset` reset to role default),
registered in `ChatActionContext.h`. `RESET_AI_VALUE` restores the -1.0
default, so the manual branch of `SpreadRadius` is now reachable.

Blocking 2 (12yd step vs 2yd/5yd radii ping-pong — REAL, fixed): step is
now `min(radius + 1, hazardEvasionDistance)` (donor shape: 3yd melee /
6yd ranged steps), so a melee pair 1.5yd apart steps 3yd, not 12yd out
of melee range.

Blocking 3 (no tank exemption — REAL, fixed): `IsTank` veto in both the
`SpreadNeededTrigger` gate and the `RaidSpreadAction` opt-in block, so
`.bot behavior party spread on` can no longer drag the boss through the
raid. Tanks keep the legacy stacking behavior.

Non-blocking "follow/grind dead checks" — ACCEPTED with the reviewer's
prescribed fix (document, don't widen): `ShouldOptInSpread` comment now
notes follow/grind are NON_COMBAT-only so those params are near-dead;
stay/guard/wait-for-attack do the real combat veto work. PR
description/provenance/docs claims corrected to stay/guard.

Non-blocking "legacy path not byte-identical" — ACKNOWLEDGED, intent
confirmed: the added `IsRanged` check in the non-opt-in `Execute` path
aligns the action with the trigger (Onyxia P2 direct dispatch no longer
spreads pool melee). PROVENANCE claim corrected.

Non-blocking "default action comment" — FIXED (one trigger row, not
triggerless).

Non-blocking "returns true vs donor returns false" — RECORDED in
deviations: at ACTION_NORMAL this only displaces filler DPS.

Non-blocking "guard missing" — FIXED: `guard` added to both opt-in
gates (guard sits on COMBAT, unlike follow/grind).
| Shaman Stoneclaw panic totem at low health (SHM-3) | `mod-playerbots` `src/Ai/Class/Shaman/Strategy/ElementalShamanStrategy.cpp:38-45` + `RestoShamanStrategy.cpp:22` (`low health -> stoneclaw totem` 40.0) + `ShamanActions.cpp:48-52` (solo-only isUseful) @ `79bd4281` | `ai/playerbot/strategy/shaman/ShamanTriggers.h` (new `StoneclawPanicTrigger`), `ShamanAiObjectContext.cpp` (creator), `ElementalShamanStrategy.cpp` + `RestorationShamanStrategy.cpp` (totems combat rows), `ShamanStoneclawPolicy.h` + `tools/test_shaman_stoneclaw_policy.cpp` | Reimplemented in live classic style: new `stoneclaw panic` trigger (self health <= LowHealth 50, stoneclaw not down, solo-only unless manual `totem earth stoneclaw` ordered) queued at ACTION_HIGH above the fixed earth-totem rows in ele + resto totems strategies. Donor 40.0 numeric priority maps to ACTION_HIGH in our enum. Spells: Stoneclaw ranks 5730/6390/6391/6392/10427/10428 (existing `CastStoneclawTotemAction`, verified in 1.18.1). Deviations: enhancement excluded (melee totem set stays; scope per report ele+resto) | `bash tools/verify_all.sh` (incl. new policy test), `git diff --check`; shared-builder compile + no live test per parity pipeline |
`src/Ai/Class/Priest/PriestTriggers.h:94` +
`src/Ai/Class/Priest/PriestTriggers.cpp:35-42`
(`FearWardOnMainTankTrigger::IsActive` with spell-cooldown guard) +
`src/Ai/Class/Priest/PriestActions.h:247` (`CastFearWardOnMainTankAction`
on `BuffOnMainTankAction`) +
`src/Ai/Class/Priest/Strategy/GenericPriestStrategy.cpp:25-26`
(row at ACTION_HIGH+3). Deviations, deliberate: donor names say "main
tank" on `BuffOnMainTankTrigger`; ours says "tank" on the local
`BuffOnTankTrigger` (same "party tank without aura" value, cf. shaman
`earth shield on party tank`) — no new value needed.

Reason: priest parity report PRI-1 — biggest tank-survival gap in fear
dungeons; we only buffed whoever lacked Fear Ward.

Source files (module, modified):
`ai/playerbot/strategy/priest/PriestTriggers.h`,
`ai/playerbot/strategy/priest/PriestTriggers.cpp`,
`ai/playerbot/strategy/priest/PriestActions.h`,
`ai/playerbot/strategy/priest/PriestAiObjectContext.cpp`,
`ai/playerbot/strategy/priest/PriestStrategy.cpp` +
`docs/classes/priest.md` (behaviour line).

Copied / ported / reimplemented: reimplemented in place in the live
strategy idiom. No new spells: Fear Ward 6346/19337 verified in
spell_template.

Local validation: `bash tools/verify_all.sh`; `git diff --check`. No
live test (no live test per parity brief); build via build-commit.sh.
Blocking 1 (flank↔set-behind oscillation — REAL, fixed): the tail-cone
clause fired at set-behind's exact-rear destination while flank outranks
it with a freshly re-rolled random angle every tick. Dropped the `inRear`
clause from both the trigger and `isUseful`: generic flank is now
front-arc-only (vanilla trash has no tail swipes); the full donor
front+tail shape is reserved for boss/dragon contexts. Belt and braces:
the trigger also returns false when `behind` is already true.

Blocking 2 (explicit holds lose — REAL, fixed): stay/wait-for-attack
exemptions added to `RearFlankNeededTrigger` (mirrors
`TankFaceNeededTrigger`); `isUseful` gates stay via
`MovementAction::isUseful` as before.

Blocking 3 (tank guard trigger-only — REAL, fixed): victim==bot guard
added to `RearFlankAction::isPossible` (mirrors
`SetBehindTargetAction::isPossible`), so an aggro flip between trigger
poll and Execute cannot walk a tank off its mob.

Non-blocking "dead policy header" — FIXED by aligning, not deleting:
`NeedsRearFlank` is now front-arc-only (tail param documented
ignored); the test pins the front-only expectations. (Production still
inlines `HasInArc` for the hot path — the header pins the geometry
contract, including the 90° exclusivity `HasInArc`'s inclusivity would
otherwise drift from.)

Non-blocking "generic scope vs donor per-boss" — ACKNOWLEDGED, partially
addressed: front-arc-only already narrows the blast radius to mobs whose
front actually matters; no elite/boss gate added (cleave exists on
non-elite trash too, e.g. SM/RFK). Left for playtesting.

Non-blocking "dragon interplay" — ACKNOWLEDGED, not verified live:
reaction-engine `dragon flank` (entry-gated dragons, EMERGENCY+4) vs
combat-engine `rear flank` (half melee range). Different ranges/engines;
arbitration needs a live dragon check before merge.

Non-blocking "LOS-fail no fallback" — ACKNOWLEDGED, not fixed: returns
false, retries next tick with a re-rolled angle. Same shape as before;
minor spin risk on LOS-blocked geometry, left for playtesting.
| Raid-tactics framework: classic-raid auto-enable rows (ZG 309 / AQ20 509 / AQ40 531), name-based `find target` / `boss target` lookup, per-fight `neglect threat` suppression (read-once flag, ThreatMultiplier bypass) | `mod-playerbots` | `79bd4281` | `src/Ai/Base/Value/ThreatValues.h` (NeglectThreatResetValue), `src/Ai/Base/Strategy/ThreatStrategy.cpp` (neglect-threat bypass), `src/Ai/Base/Value/TargetValue.{h,cpp}` (FindTargetValue/BossTargetValue), `src/Ai/Bot/PlayerbotAI.cpp` (ApplyInstanceStrategies map rows), `src/Ai/Raid/Naxx/NaxxMultipliers.cpp` (per-boss suppress pattern) | Reimplemented: trigger-driven enter/leave rows (kept, extended), grid+attackers name lookup, ManualSet read-once flag | Unblocks AQ20/Naxx per-boss tactics ports | `bash tools/verify_all.sh` + `tools/test_raid_framework_policy.cpp`; build-commit + no live test |
## Baron Geddon Inferno runout (raid1 item 1) — 2026-10-09

Donor: mod-playerbots @ `79bd4281` (local checkout
`../playerbots-references/mod-playerbots`):
`src/Ai/Raid/MC/MCTriggers.cpp` (McBaronGeddonInfernoTrigger: boss has
SPELL_INFERNO 19695), `src/Ai/Raid/MC/MCActions.cpp:34-49`
(McMoveFromBaronGeddonAction: everyone runs INFERNO_DISTANCE 20y out,
stops casts first), `src/Ai/Raid/MC/MCMultipliers.cpp`
(BaronGeddonAbilityMultiplier: only the runout moves while Inferno or
Living Bomb is up), `src/Ai/Raid/MC/MCStrategy.cpp:45-47` (trigger wiring).

Source files (module, modified): `ai/playerbot/GeddonInfernoPolicy.h`
(new pure rule: ids, 20y distance, trigger/multiplier predicates),
`ai/playerbot/strategy/triggers/MoltenCoreDungeonTriggers.h/.cpp`
(GeddonStart/EndFightTrigger on entry 12056, GeddonInfernoTrigger: aura
19695 on Geddon + within 20y), `ai/playerbot/strategy/actions/
MoltenCoreDungeonActions.h` (GeddonEnable/DisableFightStrategyAction,
GeddonMoveAwayAction: MoveAwayFromCreature 12056/20y),
`ai/playerbot/strategy/generic/MoltenCoreDungeonStrategies.h/.cpp`
(`geddon` fight strategy: inferno reaction at ACTION_EMERGENCY+5, potion
combat node, end-fight cleanup, GeddonInfernoMultiplier),
`ai/playerbot/strategy/generic/DungeonMultipliers.h/.cpp`
(GeddonInfernoMultiplier declaration + implementation),
`ai/playerbot/strategy/triggers/TriggerContext.h`,
`ai/playerbot/strategy/actions/ActionContext.h`,
`ai/playerbot/strategy/StrategyContext.h` (registrations),
`tools/test_geddon_inferno_policy.cpp` (new standalone test) +
`tools/verify_all.sh` (test list), `docs/guides/dungeon-tactics.md` (doc line).

Copied / ported / reimplemented: reimplemented in our per-boss fight
strategy idiom (StartBossFightTrigger + enable/disable actions, mirror the
Magmadar/Geddon pattern). Deviations from the donor, all deliberate: (a)
the donor casts spell 23645 directly; here the bot uses the Hourglass Sand
item (19183, exact-qualifier UseItemIdAction) so no-cast-without-item is
impossible — a bot with no sand simply fails the action usefully; (b) no
loot change: bots already loot the sand off Chromaggus trash like any other
drop, and the item stacks to 200; (c) Bronze id 23170 used directly (bot
self-aura, no boss lookup needed).

Reason: raid1 gap BWL-CHROMAGGUS: nothing cleansed the Bronze slow; one DB
lookup away per the report.

Local validation: `bash tools/verify_all.sh` (all suites incl. the new
policy test pass); `git diff --check`. Spell ids 23170/23645, item 19183
(casts 23645, stack 200), creature 14020 verified against tw_world — note
the report's classic-21171 item guess was wrong for 1.18.1. Build via
build-commit.sh pending; live in-game check pending.
Magmadar pattern). Deviations from the donor, all deliberate: (a) the
donor blocks movement via per-action type checks (MovementAction,
CastReachTargetSpellAction); here the multiplier first type-gates on the
same two action families via dynamic_cast (heals/DPS/consumables always
pass), then name-matches so only `move away from geddon` and the
universal `raid bomb runout` pass — same observable behavior without donor
class coupling; (b) the trigger is
range-gated (fires only within 20y) so already-safe bots do not attempt a
failing move each tick; (c) Living Bomb needs no new code — the universal
`raid bomb debuff` runout already covers spell 20475; (d) no cast-stop in
the trigger/action — MoveAwayFromCreature movement already interrupts via
the movement path, and the multiplier prevents re-approach casts from
queuing while Inferno burns.

Reason: raid1 gap MC-GEDDON-INFERNO: Living Bomb was covered universally
but nothing moved bots out of the 20y Inferno pulse.

Local validation: `bash tools/verify_all.sh` (all suites incl. the new
policy test pass); `git diff --check`. Spell ids 19695/20475 and creature
12056 verified against tw_world. Build via build-commit.sh pending; live
in-game check pending.

## Review fixes: Geddon multiplier movement-gate + dungeon-wide registration (PR #568) — 2026-10-09

Review verdict on the Inferno runout PR was CHANGES_REQUESTED with two
blocking findings; both verified real against the donor
(`src/Ai/Raid/MC/MCMultipliers.cpp:55-66`,
`src/Ai/Raid/MC/MCStrategy.cpp:101-106`) and fixed:

(a) The multiplier vetoed EVERY action by name while Inferno burned or a
bomb was carried — healers at 30y could not heal, ranged did zero DPS, and
the fight's own fire-protection potion was vetoed by its own strategy. The
donor only vetoes MovementAction (except the two runouts) plus
CastReachTargetSpellAction. Fixed: `GeddonInfernoMultiplier::GetValue`
returns 1.0 immediately for non-movement/non-reach actions (also skipping
the attacker/aura scan for ~90% of evaluated actions), and
`ShouldBlockGeddonMove` takes an `actionMovesOrReaches` gate computed via
dynamic_cast at the call site so the policy stays unit-testable.

(b) The multiplier lived on the ephemeral `geddon` fight strategy, so the
post-death Living Bomb carrier lost approach suppression when `-geddon`
removed the strategy. The donor registers on the dungeon-wide MC strategy.
Fixed: multiplier moved to `MoltenCoreDungeonStrategy::
InitCombatMultipliers` + `InitNonCombatMultipliers`; the `geddon` fight
strategy keeps only triggers (Inferno reaction, potion, end-fight
cleanup). The non-combat registration covers the out-of-combat bomb case.

Non-blocking findings also addressed: the multiplier scan is now gated
behind the movement check (finding 1), and the policy test asserts
non-movement immunes (`greater heal`, `flash heal`, `renew`, `shoot`,
`fire protection potion`, `tank assist` with Inferno + bomb — all pass).

Local validation: `bash tools/verify_all.sh` (all suites incl. the updated
policy test pass); `git diff --check`. Build via build-commit.sh pending;
live in-game check pending.
## Review fixes (2026-10-09, PR #567 CHANGES_REQUESTED)
All three blocking findings verified real in code and fixed:
- Finding 1 (cross-map priest veto deadlocks the corpse): confirmed — the scan had no map/range check while `PartyMemberValue::Check` requires same map + sight. Fixed: scan now skips members off-map or beyond sightDistance of the corpse (reviewer's shape, cf. ReleaseSpiritAction precedent).
- Finding 2 (explicit `revive target` orders vetoed): confirmed — `SpellTargetTrigger::IsActive` validates manual targets through the same `IsTargetValid`. Fixed: non-empty manual `revive targets` bypasses the gate (player control first).
- Finding 3 (policy header dead code): confirmed — nothing included it. Fixed by refactoring the trigger onto it instead of deleting: `IsTargetValid` now builds `OocRebirthState` and calls `ShouldCastOocRebirth`, so the 6-check test exercises the shipped gate; unused `OocResurrectClass` enum removed.
- Non-blocking citation fixed (`DruidTriggers.h:120-153` is FaerieFireFeral — now cites the generic `PartyMemberDeadTrigger` path). Toggle note: no toggle added — OOC auto-rez matches the other three classes; revisit if owners complain.
verify_all.sh + build-commit.sh + push to same branch per brief (see summary).
## Group-AoE-heal gates (HEAL-1 + HEAL-4) — 2026-10-10

Donor: mod-playerbots @ `79bd4281` (local checkout
`../playerbots-references/mod-playerbots`):
`src/Ai/Base/Trigger/HealthTriggers.cpp:34-50` + `.h:195-206`
(`AoeInGroupTrigger`: hurt threshold scales with near-group count),
`src/Ai/Base/TriggerContext.h:165-166,283-289` (creators
`"group heal setting"` -> almost-full band, `"medium group heal setting"`
-> medium band), `src/Ai/Base/Value/AoeHealValues.cpp:24-25`
(`"almost full"` qualifier branch).

Source files (module, modified): `ai/playerbot/GroupHealPolicy.h` (new
pure rule: donor scaling table + below-5 refusal),
`ai/playerbot/strategy/triggers/HealthTriggers.h/.cpp` (new
`AoeInGroupTrigger`; near count on the 30y heal radius via
`LiveGroupMembers`), `ai/playerbot/strategy/values/AoeHealValues.cpp`
(`"almost full"` branch), `ai/playerbot/strategy/triggers/TriggerContext.h`
(registrations: `"group heal setting"`, `"medium group heal setting"`,
`"almost full aoe heal"` — folds HEAL-4), `tools/test_group_heal_policy.cpp`
(new standalone test) + `tools/verify_all.sh` (test list),
`docs/classes/{priest,druid,paladin,shaman}.md` (doc lines).

Copied / ported / reimplemented: reimplemented. Deviations from the
donor, all deliberate: (a) near count uses the 30y heal radius our
`AoeHealValue` scans (donor uses sight distance) so a far-away raid
subgroup cannot arm the gate; (b) iteration via `LiveGroupMembers`
(ObjectAccessor resolution, never a stale GroupReference pointer — see
`ai/playerbot/GroupMembers.h`); (c) no strategy edits — the 6 dead nodes
(resto druid tranquility, heal paladin holy light, heal priest
shield/prayer, resto shaman chain heal x2) light up unchanged.

Reason: support parity gap HEAL-1 (high/S): the 6 TriggerNodes dangled
(engine skips null creators) so group-wide heals never fired.

Local validation: `bash tools/verify_all.sh` (all suites incl. the new
policy test pass); `git diff --check`. Build via build-commit.sh pending;
live in-game check pending.

## Mage Improved Scorch shared-slot gate ABANDONED (MAG-8) — 2026-10-10

Donor: mod-playerbots (`79bd4281`):
`src/Ai/Class/Mage/MageTriggers.cpp:126-146`
(`ImprovedScorchTrigger::IsActive` skips scorch while the target carries
Shadow Vulnerability 17794-17800, Winter's Chill 12579, or Fire
Vulnerability 22959).

Decision: deliberately NOT ported (review PR #647 finding 2). In this
core the exclusivity premise is false: mage `MOD_ATTACKER_SPELL_CRIT_CHANCE`
auras explicitly return `false` from `_IsExclusiveSpellAura`
(`tortoise-wow/src/game/Spells/SpellAuras.cpp`, "Winter's Chill /
Improved Scorch" comment), cross-family pairs return `false` from
`IsNoStackSpellDueToSpell` (`SpellMgr.cpp`), and there is no mage↔warlock
no-stack rule linking 12579/22959/17794-17800 — Chill and Fire
Vulnerability coexist rather than overwrite. Worse, the debuffs benefit
different schools (DB: Chill aura 179/frost misc 16, Fire Vuln aura
87/misc 4, Shadow Vuln aura 87/misc 32), so holding scorch on Chill or
Shadow Vulnerability is a group-DPS loss, not a save. The original MAG-8
code (exclusive-debuff gate on `NoImprovedScorchDebuffTrigger`,
`docs/classes/mage.md` line) is reverted by this review-fix commit.
Code and doc are back to the pre-PR state; no behaviour change ships.

## Review fixes (2026-10-10, PR #647 CHANGES_REQUESTED)
All three blocking findings verified real in code and fixed by
abandoning the port (no live test needed — nothing ships):
- Finding 1 (ungated `no fire vulnerability` still scorches over Chill
via `ACTION_NORMAL+2`): confirmed — `MageTriggers.h` `NoFireVulnerabilityTrigger`
returns true with no Chill/ShadowVuln check. Fixed by the revert: both
triggers are back to pre-PR behaviour, so the claimed hold no longer
exists and there is nothing left to bypass.
- Finding 2 (no shared exclusive slot in this core): confirmed verbatim —
`_IsExclusiveSpellAura` returns `false` for mage 179 auras,
`SpellMgr.cpp:1275-1279` returns `false` for cross-family pairs, no
mage↔warlock rule. This is the reason for abandonment (see above).
- Finding 3 (gated trigger dead: `DebuffTrigger("improved scorch")`
never fires): confirmed — `ChatHelper::PopulateSpellNameList` builds
`SpellIds("improved scorch")` from DB spell names, and no spell is named
exactly that (castable is "Scorch", debuff is "Fire Vulnerability";
talent ranks are "Fire Vulnerability", DB attributes 448 = passive).
`SpellIdValue` then filters passive talents, so `HasSpell` is always
false. Fixed by the revert: the dead gate is gone with the rest.
- Non-blocking notes folded in: `ai->HasAura(uint32)` vs donor
`target->HasAura` parity gap and the donor null/dead-target guard are
moot (no shipped gate); the "refresh trigger untouched" deviation note
is superseded by this abandonment record.

Local validation: `bash tools/verify_all.sh`; `git diff --check`;
shared-builder compile via `build-commit.sh` (BUILD OK).

## Healer-low-mana value + trigger (HEAL-2/MANA-2) — 2026-10-10

Donor: mod-playerbots @ `79bd4281` (local checkout
`../playerbots-references/mod-playerbots`):
`src/Ai/Base/Value/PartyMemberToHeal.cpp:138-162` + `.h:41-48`
(`HealerLowMana`: lowest-mana alive group healer, skips self),
`src/Ai/Base/Trigger/HealthTriggers.cpp:25-32` + `.h:146-153`
(`HealerLowManaTrigger`: target mana below LowMana),
`src/Ai/Base/ValueContext.h:136/455` + `src/Ai/Base/TriggerContext.h:69,396`
(registrations under the exact name `"healer low mana"`).

Source files (module, modified): `ai/playerbot/strategy/values/
PartyMemberToHeal.h/.cpp` (new `HealerLowMana` value),
`ai/playerbot/strategy/triggers/GenericTriggers.h/.cpp` (new
`HealerLowManaTrigger`, pure donor mana check, interval 1),
`ai/playerbot/strategy/values/ValueContext.h` +
`ai/playerbot/strategy/triggers/TriggerContext.h` (registrations),
`ai/playerbot/strategy/druid/DruidActions.h` (`CastInnervateAction` reads
the shared value; `isPossible`/`isUseful` carry #615's guards: spell
known, cooldown-ready, target in range, no Innervate aura yet).

Copied / ported / reimplemented: reimplemented. Deviations from the
donor, all deliberate: (a) iteration via `LiveGroupMembers`
(ObjectAccessor resolution, never a stale GroupReference pointer);
(b) mana percent via `GetPower`/`GetMaxPower` with a zero-max guard
(non-mana healers skipped) matching our neighbouring druid picker;
(c) the shared trigger is a pure mana check (donor parity) so shaman Mana
Tide rows can share it — Innervate-specific guards live in the druid
action's `isPossible`/`isUseful`, which the engine checks before queueing
(same observable behaviour as #615's trigger guards, kept verbatim).

Reason: support parity gap HEAL-2/MANA-2 (med/S-M): no generic
lowest-healer-mana routing existed, so mana batteries could only target
self.

Local validation: `bash tools/verify_all.sh` (all suites pass);
`git diff --check`. Build via build-commit.sh pending; live in-game check
pending.
## Warlock Unending Breath on swimmers (WAR-6) — 2026-10-09

Donor: mod-playerbots (`79bd4281`):
`src/Ai/Class/Warlock/Strategy/GenericWarlockNonCombatStrategy.cpp:89-90`
(self 12.0 + party 11.0), `WarlockTriggers.h:37-50` +
`WarlockTriggers.cpp:67-75` (swim-gated buff + on-party pair).

Source files (module, modified):
`ai/playerbot/strategy/warlock/WarlockTriggers.{h,cpp}` (new
`UnendingBreathTrigger : BuffTrigger` + `UnendingBreathOnPartyTrigger :
BuffOnPartyTrigger`, both swim-gated),
`ai/playerbot/strategy/warlock/WarlockActions.h`
(`CastUnendingBreathAction : CastBuffSpellAction` +
`CastUnendingBreathOnPartyAction : BuffOnPartyAction`),
`ai/playerbot/strategy/warlock/WarlockAiObjectContext.cpp` (registered
all four names), `ai/playerbot/strategy/warlock/WarlockStrategy.cpp`
(`WarlockBuffStrategy` NC rows at NORMAL+1/NORMAL) +
`docs/classes/warlock.md` (the old upkeep claim is now true),
`CHANGELOG.md` (doc lines).

Copied / ported / reimplemented: reimplemented in the live list-engine
tree following the shaman Water Breathing idiom (`ShamanTriggers.h`,
`ShamanNonCombatStrategy.cpp:91-95`) — the forward-ported
`GenericWarlockNonCombatStrategy` rows were dead (file registered
nowhere). Unending Breath 5697 verified in `tw_world.spell_template`.

Reason: doc claimed upkeep the bot never performed; donor buffs self +
party while swimming.

Local validation: `bash tools/verify_all.sh` (wiring audit covers the
four new names); `git diff --check`; shared-builder compile via
`build-commit.sh` (BUILD OK); live in-game check pending: swim with a
warlock bot, self + party gain the buff, nothing fires on land.
## Combat resurrection trigger (RES-1) — 2026-10-10 — no behaviour change

Donor: mod-playerbots @ `79bd4281` (local checkout
`../playerbots-references/mod-playerbots`):
`src/Ai/Base/Trigger/HealthTriggers.h:164-170` + `.cpp:19`
(`CombatPartyMemberDeadTrigger`, interval 1),
`src/Ai/Base/TriggerContext.h:136` (creator `"combat party member dead"`),
consumed only by the donor's `GenericDruidStrategy` combat-rez rows
(`src/Ai/Class/Druid/Strategy/GenericDruidStrategy.cpp:67-76`).

Investigation result: our combat Rebirth already fires. The live
Balance / Feral / Restoration `"rebirth"` rows (`BalanceDruidStrategy.cpp:46`,
`RestorationDruidStrategy.cpp:36`, `TankFeralDruidStrategy.cpp:101,142`,
`DpsFeralDruidStrategy.cpp:79`, via `RebirthTrigger`: spell known/ready +
dead valid target) predate this PR, and our `GenericDruidStrategy` copy of
the donor's combat-rez rows is dead — the class is never instantiated (zero
`new` sites, no creator in `DruidAiObjectContext.cpp:158-197`; live bots run
the placeholder→pve/pvp/raid hierarchy). A `"combat party member dead"`
creator would feed zero live `TriggerNode`s, so this PR adds no code: no
new trigger, no new creator, no strategy edits. Divergences from the donor,
all deliberate: (a) no `"combat party member to resurrect"` value alias —
donor's trigger returns `"party member to resurrect"` with no new value
either; (b) no `Predator's Swiftness` port — the aura has no 1.18.1
spell-template row (checked `tw_world`: Predatory Strikes only, no
swiftness proc), and the `"predator's swiftness ..."` rows in the dead
`GenericDruidStrategy.cpp:60,121-123` stay untouched dead-file tech debt
(wiring audit `DEAD_FILES`-listed, same as PR #648's HEAL-1 fix). Range
handling stays with the existing reach-to-rez action per the deliberate
RES-3 decision.

Reason: support parity gap RES-1 (high/S) investigated and closed with no
code change — combat Rebirth already fires mid-fight through the existing
`"rebirth"` rows.

Local validation: `bash tools/verify_all.sh` (all suites pass);
`git diff --check`. Rebirth 2011 verified in the report. Build via
build-commit.sh pending; live in-game check pending.

## Review fixes (2026-10-10, PR #653 CHANGES_REQUESTED)
Both blocking findings verified real in code and fixed by removing the
no-op: (1) the `"combat party member dead"` trigger + creator fed zero
live `TriggerNode`s (dead `GenericDruidStrategy` only) and its `IsActive`
merely re-ran the already-live `"rebirth"` trigger — removed, no
replacement; (2) docs now state no behaviour change (combat Rebirth
already fired via the existing `"rebirth"` rows). Non-blocking notes
accepted: no donor value alias added (donor has none — confirmed
`HealthTriggers.h:168` returns `"party member to resurrect"`); Predator
rows in `GenericDruidStrategy.cpp:60,121-123` predate this PR (dead file,
untouched — the "port removed" note in the summary referred to the
never-merged `PredatorsSwiftnessTrigger`, not those rows); leftover brief
text deleted from this entry.

## Mage flamestrike to blizzard sequencing (MAG-2) — 2026-10-10

Donor: mod-playerbots (`79bd4281`):
`src/Ai/Class/Mage/Strategy/GenericMageStrategy.cpp:182-186,196,202-205`
(`medium aoe` → flamestrike 23 then blizzard 22; `flamestrike active and
medium aoe` → blizzard 24),
`src/Ai/Class/Mage/MageTriggers.cpp:107-124` (`FlamestrikeNearbyTrigger`:
own flamestrike DynamicObject within 30yd).

Source files (module, modified):
`ai/playerbot/FlamestrikeWindowPolicy.h` (new pure rule:
`ShouldBlizzardAfterFlamestrike` fires when the last cast was a
flamestrike castable rank id 2120/2121/8422/8423/10215/10216 (3s cast in
1.12; not the 2124-line trainer Learn spells) within the last 6s after
cast start
and the pack is still grouped),
`tools/test_flamestrike_window_policy.cpp` (new standalone test, 15
checks) + `tools/verify_all.sh` (registered),
`ai/playerbot/strategy/mage/MageTriggers.h/.cpp` (new
`FlamestrikeWindowTrigger : Trigger` reading `last spell cast` into the
policy, then confirming the pack via the live `ranged medium aoe`
trigger — cheap cast-id/time gates run before the density scan),
`ai/playerbot/strategy/mage/MageAiObjectContext.cpp` (registered
`flamestrike window`), `FrostMageStrategy.cpp` /
`FireMageStrategy.cpp` / `ArcaneMageStrategy.cpp` (each AoE strategy:
`flamestrike window` → blizzard at HIGH+2 FIRST, then the existing
medium-aoe rows), `docs/classes/mage.md` (doc line).

Copied / ported / reimplemented: reimplemented in the live list-based
style. Deviations from the donor, all deliberate: (a) the donor's
"flamestrike active" check (own dynobj within 30yd via
`Aura::GetDynobjOwner`) cannot port — this core has no aura→dynobj link
and our `NearestDynamicObjects` value is an empty stub (no dynobj grid
searcher), so the port tracks "I cast flamestrike ≤6s ago" via the
already-maintained `last spell cast` value instead; (b) donor ordering
(blizzard-on-active 24 > flamestrike 23 > blizzard 22) preserved as
HIGH+2 > HIGH+1 > HIGH; (c) arcane gains the pair too (donor sequences
it for arcane; both spells trained by all specs); the fire
`fire spells locked` fallback blizzard is untouched (different case).

Reason: bots cast flamestrike OR blizzard as independent same-trigger
rows (engine picks the first available) — never stacking
flamestrike under the pack and channeling blizzard on top. Real AoE DPS
loss.

Local validation: `bash tools/verify_all.sh` (all suites incl. the new
policy test pass); `git diff --check`; shared-builder compile via
`build-commit.sh` (BUILD OK); live in-game check pending: 3+ mob pack,
flamestrike lands, blizzard follows while the pack holds.
| Elemental earth-shock execute discipline (SHM-4) | `mod-playerbots` `src/Ai/Class/Shaman/ShamanTriggers.cpp:50-64` (`EarthShockExecuteTrigger`: <25% AND <1500hp) + `Strategy/ElementalShamanStrategy.cpp:58-65` (execute node 5.5) @ `79bd4281` | `ai/playerbot/strategy/shaman/ShamanTriggers.h` (new `EarthShockExecuteTrigger`), `ShamanAiObjectContext.cpp` (creator), `ElementalShamanStrategy.cpp` (`shock` row -> `earth shock execute` at same ACTION_NORMAL), `ShamanEarthShockPolicy.h` + `tools/test_shaman_earth_shock_policy.cpp` | Ported verbatim thresholds via `GetHealthPercent()` + absolute `GetHealth() < 1500` (house idiom; donor divides manually). Ele only; enhancement keeps ungated `shock` (melee threat tool); interrupt triggers untouched. Spell: Earth Shock 8042+ (existing action, verified) | `bash tools/verify_all.sh` (incl. new policy test), `git diff --check`; shared-builder compile + no live test per parity pipeline |
## Review fixes (2026-10-09, PR #615 CHANGES_REQUESTED)
All three blocking findings verified real in code and fixed:
- Finding 1 (fires with Innervate unknown/on cooldown, shift-then-fail churn): confirmed — Engine runs the caster-form prereq before isPossible. Fixed: spell-id + IsSpellReady gate at the top of IsActive (mirrors SpellTargetTrigger::IsSpellReady).
- Finding 2 (no skip for already-Innervated healers): confirmed — action's auraCheck refuses them while the trigger stays true. Fixed: HasAura("innervate") skip in the loop.
- Finding 3 (no range gate, action has no reach): confirmed — CastInnervateAction targets "self target" so no reach prereq is added. Fixed: GetSpellRange gate in the loop (sightDistance covered by the tighter spell-range check).
- Non-blocking: relevance below cower kept deliberately (threat-drop first is safer for a cat; ~1 tick delay); checkInterval raised to 2 for the per-tick talent scan; manual `.bot boost` row added (`innervate` trigger watches "boost targets" — player control first); name-collision delete-on-merge already recorded in the entry above.
verify_all.sh + build-commit.sh + push to same branch per brief (see summary).
| Shaman reactive situational totems (SHM-2: tremor vs fear/charm, grounding vs party-aimed casts, poison/disease cleansing vs party debuffs, earthbind vs runners) | New automation (not a donor port — verified: mod-playerbots picks situational totems manually per fight via per-slot strategies, `ShamanAiObjectContext.cpp:67-68`, plus slot-maintenance `NoXxxTotemTrigger`s `ShamanTriggers.cpp:413-447`; donor dungeon code carries `TODO: tremor totem`). Reuses donor `CastCleansingTotemAction::isUseful` shape (`ShamanActions.cpp:43-46`, no clobber) @ `79bd4281` | `ai/playerbot/strategy/shaman/ShamanTriggers.h` (new `TremorTotemReactiveTrigger`, `GroundingTotemReactiveTrigger`, `Poison/DiseaseCleansingTotemReactiveTrigger`, `EarthbindTotemReactiveTrigger`), `ShamanAiObjectContext.cpp` (creators), `Elemental/Enhancement/RestorationShamanStrategy.cpp` (totems combat rows), `ShamanSituationalTotemPolicy.h` + `tools/test_shaman_situational_totem_policy.cpp` | Split poison/disease triggers firing the registered `poison/disease cleansing totem` actions (1.12 has no `Cleansing Totem` spell). Tremor covers fear+charm auras only (effect 8146 dispels charm/fear/sleep, never confuse); no any-cast pre-drop. Grounding only when the in-flight cast targets us/party (`GetTargetGuid`, DeflectSpellTrigger pattern). Earthbind covers UNIT_STAT_FLEEING|CONFUSED + snared party member; cleansing reuses the cure path HasAuraToDispel scan. Spells: Tremor 8143, Grounding 8177, Earthbind 2484, Poison Cleansing 8166, Disease Cleansing 8170 (all verified in tw_world.spell_template) | `bash tools/verify_all.sh` (incl. new policy test), `git diff --check`; shared-builder compile + no live test per parity pipeline |
## Warlock Curse of Exhaustion snare strategy (WAR-7) — 2026-10-09

Donor: mod-playerbots (`79bd4281`):
`src/Ai/Class/Warlock/Strategy/GenericWarlockStrategy.cpp:194-208`
(CoE toggle strategy at 29.0, disabled by default),
`WarlockAiObjectContext.cpp:110,183`, `WarlockTriggers.h:257-262`.

Source files (module, modified):
`ai/playerbot/strategy/warlock/WarlockTriggers.h`
(`SNARE_TRIGGER(CurseOfExhaustionSnareTrigger)` — the shared snare
target already picks fleeing/chasing/kiting attackers),
`ai/playerbot/strategy/warlock/WarlockActions.h`
(`SNARE_ACTION(CastCurseOfExhaustionSnareAction)`),
`ai/playerbot/strategy/warlock/WarlockAiObjectContext.cpp` (registered
both `curse of exhaustion on snare target` names + the manual `curse
exhaustion` strategy) + `docs/classes/warlock.md`, `CHANGELOG.md` (doc
lines). The PvP `enemy ten yards` hardcode and the plain
`CastCurseOfExhaustionAction` are untouched.

Copied / ported / reimplemented: reimplemented in the death-coil snare
idiom. Deviations from the donor, all deliberate: (a) donor fires on the
current target; ours fires on the snare target (runners/chasers, not the
tank's mob) — the feature asked for kiting, and the value already
excludes rooted/stunned targets; (b) off by default like the donor (new
automation behind a toggle); order `.bot strategy +curse exhaustion`.

Reason: the action existed but no trigger or strategy ever queued it, so
bots never slowed runners outside the affliction-PvP hardcode.

Local validation: `bash tools/verify_all.sh` (wiring audit covers the
new names); `git diff --check`; shared-builder compile via
`build-commit.sh` (BUILD OK); live in-game check pending: ordered bot
slows a fleeing mob, untriggered bot unchanged.
## Trinket usage filters (CD-1) — 2026-10-10

Donor: mod-playerbots @ `79bd4281` (local checkout
`../playerbots-references/mod-playerbots`):
`src/Ai/Base/Actions/GenericSpellActions.cpp:509-663`
(`UseTrinketAction`: positive-only, aura-or-mana-restore-only, mana gates,
tank-defensive health gate, mixed-trigger exclusion, per-item +
per-category cooldown maps), `:81-115` (effect classifiers
IsManaRestore/IsManaEfficiency/IsDefensiveTankEffect).

Source files (module, modified): `ai/playerbot/TrinketUsePolicy.h` (new
pure rule) + `tools/test_trinket_use_policy.cpp` (new test),
`ai/playerbot/strategy/actions/UseTrinketAction.{h,cpp}` (filters +
cooldown memory), `tools/verify_all.sh` (test list).

Copied / ported / reimplemented: reimplemented. Deviations from the
donor, all deliberate: (a) 1.12 category mapping via `Effect[]` /
`EffectApplyAuraName[]` / `EffectMiscValue[]` (donor
SpellEffectInfo/ApplyAuraName/MiscValue) + `SPELL_SCHOOL_MASK_NORMAL`;
no rating-mask branch (no CombatRating enum on this core — resistance /
health / dodge / parry / block / damage-taken-taken cover 1.12 tank
trinkets); (b) per-item + per-category memory keyed on
(itemEntry, spellId) / category from the 1.12 `_ItemSpell` struct, using
the outer `IsSpellReady` check in `ItemCountValue.cpp:35-67` as before;
(c) no mixed-trigger exclusion (WotLK item-template concern, no 1.12
equivalent); (d) efficiency-only trinkets never fire (donor
aura-or-restore-only gate — kept as `None` classification rather than a
separate efficiency gate). No context/strategy/addon change — the `often`
rows now pick a filtered trinket instead of first-ready.

Reason: support parity gap CD-1 (high/M): trinkets fired blind (first
ready ON_USE wins), wasting mana restores at full mana and defensives at
full health.

Local validation: `bash tools/verify_all.sh` (all suites incl. the new
policy test, 23 checks, pass); `git diff --check`. Build via
build-commit.sh pending; live in-game check pending.
| Mend pet at medium health (PET-4) | `mod-playerbots` @ `79bd4281` `src/Ai/Class/Hunter/Strategy/GenericHunterStrategy.cpp:72-73` (`hunters pet medium health` below MediumHealth 70 → mend pet 22.0 combat / 60.0 NC) + `GenericHunterNonCombatStrategy.cpp:33` | `ai/playerbot/strategy/hunter/HunterTriggers.{h,cpp}` (`HuntersPetMediumHealthTrigger` below `sPlayerbotAIConfig.mediumHealth`), `HunterStrategy.cpp` (combat mend at ACTION_HIGH-1 below low's ACTION_HIGH; NC at ACTION_NORMAL below low's NORMAL+1), `HunterAiObjectContext.cpp` (creator) | Reimplemented in live strategy priorities (donor 22/21/60 maps onto our ACTION_HIGH-1/HIGH + NORMAL/NORMAL+1); low band still wins below 40 | `bash tools/verify_all.sh`; `python3 tools/verify_action_trigger_wiring.py` (0 live-missing); `git diff --check`. Compile via shared builder; no live in-game test |
| Combat call pet + safe combat revive, tame demoted (PET-5) | Transferable extension of donor NC-only `no pet` → `call pet` / `hunters pet dead` → `revive pet` (`GenericHunterNonCombatStrategy.cpp:29,34`); donor has no combat call/revive | `HunterStrategy.cpp` (combat `no pet` → `call pet` NORMAL+1, `safe to revive pet` → `revive pet` NORMAL; both combat+NC `tame beast` demoted EMERGENCY → NORMAL so instant call wins wherever castable and the engine falls through to tame only when call is impossible), `HunterTriggers.{h,cpp}` (`SafeToRevivePetTrigger` wired to `runtime/PetRevivePolicy.h`; `HunterNoPet` simplified to donor `NoPetTrigger` shape — petless + unmounted — so the call nodes are reachable for dismissed pets), `HunterAiObjectContext.cpp` (creator), `runtime/PetRevivePolicy.h` + `tools/test_pet_revive_policy.cpp` | Reimplemented: donor never called/revived in combat; revive gated on zero attackers (10s channel safety). Tame demotion via relevance fallback instead of a trigger spell gate | `bash tools/verify_all.sh`; wiring 0 live-missing; standalone `test_pet_revive_policy` (5 checks); `git diff --check`. Compile via shared builder; no live in-game test |
| Chain-heal group trigger verification + Fire Nova Totem drop gate (SHM-7/SHM-9) | Donor `CastFireNovaAction::isUseful` (`mod-playerbots` `ShamanActions.cpp:28-41`: fire-totem + 8y gate) is WotLK-3.3.0+ mechanics — 1.12 Fire Nova is a totem DROP (1535 line, detonates after 4s), not a pulse of a down totem, so the donor gate is NOT ported (it would refuse every drop). Ported as a placement gate instead @ `79bd4281` | `ai/playerbot/strategy/shaman/ShamanActions.h` (`CastFireNovaAction::isUseful`: bot-to-target <= 10y + policy call), `ShamanFireGatePolicy.h` + `tools/test_shaman_fire_gate_policy.cpp` | Chain heal: verified already wired — live `medium aoe heal -> chain heal` at ACTION_MEDIUM_HEAL matches priest/druid shape, no new trigger; donor `group heal setting` exists only in dead ports. Fire Nova: 10y placement gate (totem lands at our feet); manual `totem fire nova` and magma->nova continuer unaffected (no existing-totem requirement). Spells: Fire Nova Totem 1535 line / Fire Nova pulse 8350 line (verified in tw_world.spell_template; action resolves via spellbook to the trained drop) | `bash tools/verify_all.sh` (incl. new policy test), `git diff --check`; shared-builder compile + no live test per parity pipeline |

## Mage arcane rupture to missiles rhythm (MAG-5) — 2026-10-10

Donor (idea only): mod-playerbots (`79bd4281`)
`src/Ai/Class/Mage/Strategy/ArcaneMageStrategy.cpp:56-64` (blast-stack +
missile-barrage proc timing the missiles). The WotLK spells do not exist
in 1.18.1 — only the transferable idea ports ("arcane has a
builder/spender rhythm"), mapped onto Turtle's Arcane Rupture → Missiles
pair.

Source files (module, modified):
`ai/playerbot/ArcaneRupturePolicy.h` (new pure rule:
`ShouldCastArcaneRupture` fires when the rupture self buff is absent and
the spell is known; `IsArcaneRuptureCastId` covers 51949-51954),
`tools/test_arcane_rupture_policy.cpp` (new standalone test, 17 checks)
+ `tools/verify_all.sh` (registered),
`ai/playerbot/strategy/mage/MageActions.h` (new
`CastArcaneRuptureAction : CastSpellAction`),
`ai/playerbot/strategy/mage/MageTriggers.h/.cpp` (new
`ArcaneRuptureTrigger : Trigger`: `HasSpell` gate, spell-ready (15s
category) + live-target gates, then self-buff 52502/52588 check via
`ai->HasAura(uint32)` into the policy),
`ai/playerbot/strategy/mage/MageAiObjectContext.cpp` (registered
`arcane rupture` trigger + action),
`ai/playerbot/strategy/mage/ArcaneMageStrategy.cpp`
(`ArcaneMageStrategy::InitCombatTriggers`: → `arcane rupture` at
NORMAL+1 like the fire/frost rotational nukes; the missiles IDLE default
owns the GCD while buffed),
`docs/classes/mage.md` (doc line).

Copied / ported / reimplemented: reimplemented. Mechanics verified in
`tw_world.spell_template`: rupture casts 51949-51954 are school-6 nukes
(cast time index 19, 15s category 1013 unique to this line); 51955-51960
are effect-36 (SPELL_EFFECT_LEARN_SPELL) trainer wrappers that teach the
matching 51949-51954 cast and never enter the spellbook. Self buff
52502/52588 is effect-6 aura-108 (+19% missiles, target A=1 self, 8s
duration idx 31). Exact rupture cast seconds unverified (DBC not in
repo). Trainers teach ranks 2-6 (51956-51960) at 28-60 (Theocritus
et al.; rank 1 from the starting kit). Arcane Surge deliberately out of
scope (needs a "resist happened" value that does not exist).

Reason: arcane bots were bare missiles + explosion with zero combat
triggers of their own — no builder/spender rhythm at all.

Local validation: `bash tools/verify_all.sh` (all suites incl. the new
policy test pass); `git diff --check`; shared-builder compile via
`build-commit.sh` (BUILD OK); live in-game check pending: arcane bot
ruptures once, then missiles through the buff window.
| Totemic Recall out of combat + dead Call-spell cleanup (SHM-5) | `mod-playerbots` `src/Ai/Class/Shaman/ShamanTriggers.cpp:187-262` (`TotemicRecallTrigger`: dungeon boss guard, group combat guard, mana-tide/fire-ele sparing) + `Strategy/ShamanNonCombatStrategy.cpp:88` @ `79bd4281` | `ai/playerbot/strategy/shaman/ShamanStrategy.cpp` (non-combat `totemic recall` row), `ShamanTriggers.h` (`ReadyToRemoveTotemsTrigger` hardened + `TotemsAreNotSummonedTrigger` removed as orphan), `ShamanActions.h` (3 `CastCallOfThe...` classes deleted), `ShamanAiObjectContext.cpp` (6 dead creators removed), `ShamanRecallPolicy.h` + `tools/test_shaman_recall_policy.cpp` | Reimplemented in live classic style: trigger requires the spell trained + any OWN totem down (new owner-scoped `have any own totem` / `has own totem` values — recall refunds only ours, so strangers' totems never trigger and a teammate's tide never vetoes), vetoes bot/group-member/pet combat, queued at ACTION_NORMAL below rez/heal. Deviations: no dungeon boss-encounter check (no InstanceScript hook in triggers; group combat covers live fights); fire-ele sparing dropped (no fire-elemental totem action in 1.18.1). Base stays `CastBuffSpellAction`: Turtle recall costs 0 mana (verified powerType 0, manaCost 0), so the mana-floor veto cannot block the refund. Cleanup: `Call of the Elements/Ancestors/Spirits` return zero rows in tw_world.spell_template (verified) — deleted 3 action classes + 6 creators that could only log cast failures. Spells: Totemic Recall 45513/47340 (Turtle custom, verified) | `bash tools/verify_all.sh` (incl. new policy test), `git diff --check`; shared-builder compile + no live test per parity pipeline |
## Dangling-node/typo bundle (BUFF-6, SUPD-1, SUPD-2, MANA-1, CD-2, CD-3) — 2026-10-10

Donor: mod-playerbots @ `79bd4281` (local checkout
`../playerbots-references/mod-playerbots`):
SUPD-1 `src/Ai/Base/Trigger/GenericTriggers.h:739-743` (fear/charm/sleep
-> WotF); SUPD-2 `:745-751` (fear/sleep/sapped trigger); MANA-1
`:37-43` (EnoughMana > HighMana); CD-2 `:442-452` (generic boost:
balance<=50, PvP-target always); CD-3 `:400-406` (DebuffOnBoss:
IsDungeonBoss || isWorldBoss).

Source files (module, modified):
- SUPD-1 (`ai/playerbot/strategy/triggers/GenericTriggers.h`):
`WOtFTrigger` was 4 sequential returns (only fear fired); folded into one
`||` over fear/charm/stun/confuse aura types plus a sleep/sapped mechanic
scan (StoneformTrigger aura-holder idiom; no `HasAuraWithMechanic` on this
core).
- SUPD-2 (same header + `TriggerContext.h`): new `FearSleepSapTrigger`
(fear aura OR sleep/sapped mechanic); the two warrior strategy rows
already pushed `"fear sleep sap"` with no creator — they light up, no
strategy edit.
- MANA-1 (same header note: `HighManaTrigger` gains an optional name; plus
`TriggerContext.h`): new `"enough mana"` creator (same >65 verdict as high
mana, distinct name); the tank-paladin consecration consumer starts working.
- CD-2 (same header + `.cpp` + `TriggerContext.h` +
`generic/RacialsStrategy.cpp`): new `GenericBoostTrigger`
(balance<=50, PvP-target-always, combat-only); generic berserking/blood
fury rows rewired from bare per-spell triggers to one `"generic boost"`
node. Class `BoostTrigger` consumers untouched.
- CD-3 (same header + `.cpp`): new `DebuffOnBossTrigger` (debuff wanted
AND target is world boss or elite in dungeon/raid — `IsDungeonBoss` has no
1.12 equivalent; `RangeTriggers.h` precedent uses `IsWorldBoss`).
Registered nowhere by design (Base trigger only, class wiring = follow-up).
- BUFF-6 (`paladin/GenericPaladinNonCombatStrategy.cpp`): deleted the 2-line
`"greater blessing needed"` dead node (neither trigger nor action name
registered anywhere; full assignment port is a Class-layer M needing owner
input). Recommended delete per report.

Copied / ported / reimplemented: reimplemented. No addon changes (racials
auto-fire, no toggles).

Reason: six small support gaps in one focused bundle — 3 dead nodes firing
wrong/never, 1 gate missing, 2 difficulty/boss gates absent.

Local validation: `bash tools/verify_all.sh` (all suites pass);
`git diff --check`. Build via build-commit.sh pending; live in-game check
pending.

## Mage blizzard channel cancel when the pack thins (MAG-3) — 2026-10-10

Donor: mod-playerbots (`79bd4281`):
`src/Ai/Class/Mage/MageTriggers.h:293-304` +
`MageTriggers.cpp:148-170` (`BlizzardChannelCheckTrigger`: channel id
in {10, 6141, 8427, 10185, 10186, 10187, 27085, 42938, 42939} and
`attacker count` < 2),
`src/Ai/Class/Mage/Strategy/GenericMageStrategy.cpp:175` (→ `cancel
channel` 26.0), `MageAiObjectContext.cpp:123` (registration).

Source files (module, modified):
`ai/playerbot/strategy/mage/MageTriggers.h/.cpp` (new
`BlizzardChannelCheckTrigger`, cloned off the live
`IciclesChannelCheckTrigger` shape: current channeled spell id in the
per-rank channel set {10, 6141, 8427, 10185, 10186, 10187} — rank-1
effect 10 plus our rank rows 6141/8427/10185-87, all verified in
`tw_world.spell_template`, matching the donor's WotLK-extended list
minus the three WotLK-only ranks — and `attacker count` < 2),
`ai/playerbot/strategy/mage/MageAiObjectContext.cpp` (registered
`blizzard channel check`),
`ai/playerbot/strategy/mage/MageStrategy.cpp`
(`MageAoeStrategy::InitCombatTriggers`: → `cancel channel` at HIGH+3,
one place covering all specs), `docs/classes/mage.md` (doc line).

Copied / ported / reimplemented: reimplemented. Deviations from the
donor, all deliberate: (a) the row lives on the live list-based base
`MageAoeStrategy`, not the dead vector-API `GenericMageStrategy.cpp:149`
forward-port (PROVENANCE already records that file as unregistered dead
code — left untouched); (b) only the six 1.18.1 channel ids, no
27085/42938/42939 which have no 1.18.1 rank rows; (c) priority HIGH+3
matches our icicles cancel row rather than donor's flat 26.0.

Deviations from the donor, continued: (d) the trigger reads the
registered group-wide `attackers count` (plural), NOT the donor's singular
`attacker count` — the singular name is unregistered here and `AI_VALUE`
null-derefs on it (`ValueMacros.h:8`, `GetValue` returns NULL for unknown
names). Do not "fix" this back to the donor spelling. The same latent
wrong name in `EstimatedLifetimeValue.cpp:26` (dead code, value never
registered/used) was fixed to the plural alongside so nobody copies it.

Reason: bots channeled the full blizzard into one leftover mob while the
rest of the pack was dead — wasted channel time.

Local validation: `bash tools/verify_all.sh`; `git diff --check`;
shared-builder compile via `build-commit.sh` (BUILD OK); live in-game
check pending: blizzard stops early as the pack drops below two.

## Mage Hot Streak proc to hurried Pyroblast (MAG-1) — 2026-10-10

Donor: mod-playerbots (`79bd4281`):
`src/Ai/Class/Mage/Strategy/FireMageStrategy.cpp:51-58` (`hot streak` →
`pyroblast` 25.0), `src/Ai/Class/Mage/MageTriggers.h:81-85`
(`HotStreakTrigger : HasAuraTrigger("hot streak")`),
`src/Ai/Class/Mage/MageAiObjectContext.cpp:99` (trigger registration).

Source files (module, modified):
`ai/playerbot/HotStreakPolicy.h` (new pure rule:
`ShouldCastHotStreakPyroblast` fires only for proc auras 51930/51931 at
full 5 stacks), `tools/test_hot_streak_policy.cpp` (new standalone test,
17 checks) + `tools/verify_all.sh` (registered),
`ai/playerbot/strategy/mage/MageTriggers.h/.cpp` (new `HotStreakTrigger :
Trigger` with a custom `IsActive()` reading the proc IDs via
`ai->GetAura` into the policy — deliberately NOT a name-based
`HAS_AURA_TRIGGER`), `ai/playerbot/strategy/mage/MageAiObjectContext.cpp`
(registered `hot streak`; the `presence of mind aura` registration is
kept), `ai/playerbot/strategy/mage/FireMageStrategy.cpp`
(`FireMageStrategy::InitCombatTriggers` row: `hot streak` → `pyroblast`
at HIGH+2), `docs/classes/mage.md` (doc line).

Copied / ported / reimplemented: reimplemented in the live list-based
style (donor uses the newer vector/NextAction-float API). Deviations
from the donor, all deliberate: (a) fire-only wiring (Hot Streak is a
fire-tier-4 talent, 51927/51928; proc auras 51930/51931) — donor also
wires it on frostfire, which has no 1.18.1 equivalent; (b) priority
HIGH+2 instead of donor's flat 25.0 — ties with the base execute row
(`target critical health` → fire blast, also 22) but `Queue::Peek` uses
strict `>`, so the earlier-pushed base row wins ties deterministically
and the execute keeps priority, which is the intended winner; (c) the
donor's name-based aura check does NOT port: Turtle's proc is a stacking
cast-time reduction (-1001ms/stack, 5 stacks ≈ instant on the 6s
Pyroblast), not WotLK's binary instant, and the passive talent auras
51927/51928 share the "Hot Streak" name — so the trigger gates on full
proc stacks instead of any presence.

Reason: every fire mage with the Turtle Hot Streak talent procced free
faster Pyroblasts that bots silently ignored — the highest-value mage
gap in the parity report.

Local validation: `bash tools/verify_all.sh` (all suites incl. the new
policy test pass); `git diff --check`; shared-builder compile via
`build-commit.sh` (BUILD OK); live in-game check pending: talented fire
bot hardcasts normally at low stacks, hurries Pyroblast at full stacks.

## Review fixes (2026-10-10, PR #646 CHANGES_REQUESTED)
Both blocking findings verified real in code and fixed:
- Finding 1 (deleted PoM-aura trigger orphaned two live consumer rows):
confirmed — the first version replaced the `PresenceOfMindAuraTrigger`
declaration and its registration instead of adding alongside, which
would have silently no-op'd the `presence of mind aura` → pyroblast /
frostbolt rows (`Engine.cpp:866-867` skips unregistered triggers) while
arcane kept casting PoM. Fixed: PoM declaration + registration restored
(diff now shows pure additions on those lines); Hot Streak is an
additional trigger/row.
- Finding 2 (name-based check matches the permanent talent auras):
confirmed — 51927/51928 share SpellName "Hot Streak" (attributes 464
incl. passive) with the procs, and with non-empty `SpellIds("hot
streak")` the `HasAura` name path matches by ID list, so the trigger
meant "talent learned", not "proc up". Also confirmed the deeper point:
Turtle's proc is per-stack cast-time reduction (effect 107, -1001ms per
stack, 5 stacks, 1 charge), not an instant — firing on any presence
would spend a 1-stack proc. Fixed: custom `IsActive()` checks proc IDs
51930/51931 only (via `ai->GetAura`, bypassing the name path entirely)
and the new `HotStreakPolicy.h` rule requires full 5 stacks (17-check
standalone test).
- Non-blocking: Cold Snap doc line restored (behavior never changed);
HIGH+2 tie resolves to the execute row winning (verified `Queue::Peek`
strict-`>`); doc now says "hurries" not "free/instant" (no mana
reduction in the tooltip); live in-game check still pending.
| Health Funnel demon sustain (PET-6) | New behavior (neither `mod-playerbots` nor the module funneled; vanilla spell verified in `tw_world.spell_template`: 755/3698-3707/11693-11698/16569) | `runtime/HealthFunnelPolicy.h` (`CanCastHealthFunnel`: pet <50% + owner >60% + combat) + `tools/test_health_funnel_policy.cpp`; `ai/playerbot/strategy/warlock/WarlockTriggers.{h,cpp}` (`HealthFunnelTrigger`), `WarlockActions.h` (`CastHealthFunnelAction : CastSpellAction` on pet target, policy gate repeated for the evaluation gap), `WarlockStrategy.cpp` (`WarlockPetStrategy` combat node at ACTION_NORMAL+1), `WarlockAiObjectContext.cpp` (2 creators) | Reimplemented in place: owner-cast (warlock knows the spell, demon doesn't), combat-only v1, all demons eligible, below interrupt kit/pet attack | `bash tools/verify_all.sh`; wiring 0 live-missing; standalone `test_health_funnel_policy` (9 checks); `git diff --check`. Compile via shared builder; no live in-game test |
| Heigan safety dance (Naxx): Plague-Cloud-anchored per-bot clock, 4s-first/3s-cadence section walk on core sect spots, ranged platform hold | `mod-playerbots` | `79bd4281` | `src/Ai/Raid/Naxx/Action/NaxxActions_Heigan.cpp`, `src/Ai/Raid/Naxx/NaxxBossHelper.h` (HeiganBossHelper), `src/Ai/Raid/Naxx/NaxxStrategy.cpp` (Heigan rows) | Reimplemented with re-derived vanilla constants (dance 45s/4s/3s vs donor 45s/7s/4s; core sect coords vs donor WotLK waypoints; no shared clock — per-bot aura clock) | Core boss_heigan.cpp: dance 90s cycle, erupt 15s/10s fight + 4s/3s dance, section cycle 0-1-2-3-2-1 confirmed; Plague Cloud duration 45s verified in SpellDuration.dbc | `bash tools/verify_all.sh` + `tools/test_heigan_dance_policy.cpp`; build-commit + no live test (playtest gate: first dance timing) |

## Warrior WAR-1: DPS sunder with no tank warrior (2026-10-09)

Feature: arms/fury combat lists gain a bottom-rung `sunder armor` row
(NORMAL-1, below rend/intercept and every damage spender), and
`CastSunderArmorAction::isUseful` now returns false for non-tanks while a
tank warrior (any group member of class warrior that `IsTank`) shares
their map and is alive in-world. A DPS warrior in a tankless group keeps
the 5-stack up (stacking, then refreshing an expiring stack); with a tank
warrior present it spends rage on damage instead.

Source repository: `mod-playerbots/mod-playerbots`

Source commit: `79bd4281` (local
`playerbots-references/mod-playerbots` checkout).

Source files:
- `src/Ai/Class/Warrior/WarriorActions.cpp:48-73` (`CastSunderArmorAction::isUseful` group tank-warrior check, 5-stack/refresh logic)
- `src/Ai/Class/Warrior/Strategy/FuryWarriorStrategy.cpp:72` (sunder default +0.3)
- `src/Ai/Class/Warrior/Strategy/ArmsWarriorStrategy.cpp:83` (sunder default +0.05)

Copied / ported / independently reimplemented: reimplemented in place
(`WarriorActions.h`, `WarriorTriggers.cpp`, `ArmsWarriorStrategy.cpp`,
`FuryWarriorStrategy.cpp`, all `ai/playerbot/strategy/warrior/`).
Deviations from the donor, all deliberate: (a) donor returns false for
non-tanks with no group at all — ours still sunders ungrouped (a solo DPS
warrior benefits from the armor reduction, and no tank exists to defer
to); (b) row at NORMAL-1 rather than donor DEFAULT+0.x — same bottom
shape (above nothing but melee-idle) in our NORMAL/HIGH ladder; the
original NORMAL+1 wrongly tied heroic strike and beat rend; (c) the
group-tank scan stays inline in `isUseful` (donor-identical): it is a
bounded in-memory walk (≤40 refs, class gate first so `IsTank` runs only
for warriors) evaluated only while the stack is incomplete — no cached
group-role value exists and none is warranted; (d) `sunder armor` →
`melee` fallback nodes added to both DPS factories (prot pattern), so a
failed sunder falls through to auto-attack.

Review fixes: (1) ported the donor 6s-expiry refresh to both trigger
(5-stack re-arms at <=6s) and action (`!aura || stack<5 || <=6s`) — the
old trigger-side `>=5 → false` let full stacks fall off entirely (also
affected tanks; pre-existing, fixed here); (2) priority NORMAL+1 →
NORMAL-1 per above.

Reason: WAR-1 in the warrior parity sweep: zero sunder rows in Arms/Fury
meant tankless groups never got the armor-reduction stack.

Local validation: `bash tools/verify_all.sh`; `git diff --check`. No live
test (per task constraints).

## Mage blast wave review rework (MAG-7) — 2026-10-10

Donor: mod-playerbots (`79bd4281`)
`src/Ai/Class/Mage/Strategy/FireMageStrategy.cpp:60-76` (fire
`enemy too close for spell` → dragon's breath INT+1, `enemy is close` →
blast wave INT).

What I first tried: a second row in
`FireMageCcStrategy::InitCombatTriggers` mapping the victim-gated
`enemy too close for spell` → `blast wave` at INTERRUPT-1 alongside the
existing `enemy ten yards` → `blast wave` at INTERRUPT. Review showed
the new row could never fire a cast: (1) when a live mobile mob chews
on the mage, the trigger's "can't add distance" guard
(`RangeTriggers.h:30-34`, victim == bot, can move, target faster than
65% of the bot) returns false — the exact claimed case; (2) inside
10yd both triggers fire together but `Queue.cpp:17-23` dedupes by
action name keeping max relevance, so 39 never beats the existing 40,
and in the 10–15yd band the 10yd-radius `CastMeleeAoeSpellAction`
(`isUseful` distance <= radius; `isPossible` melee reach) always
fails; (3) the donor's actual blast-wave row is `enemy is close` →
`blast wave` (no victim gate, plain 5yd `TooCloseDistance` check),
while the donor's `enemy too close` row maps to dragon's breath (does
not exist in 1.18.1) and fires when victim != bot — the inverse of
our victim gate.

Fix applied: deleted the added row. The surviving behaviour is the
pre-existing `enemy ten yards` → `blast wave` at INTERRUPT in
`FireMageCcStrategy`, which already covers any enemy (tank-held or
not) inside 10yd of the current target — a superset of the donor's
5yd `enemy is close` row at the same relevance. The vector-path
`enemy is close` → `blast wave` row in `GenericMageStrategy.cpp:143`
is dead code (the `GenericMageStrategy`/`MageCcStrategy` vector-path
classes are never instantiated; only the `MageStrategy.h` list-path
hierarchies run), so no donor row was live anywhere else. Net diff
of this PR after the fix: docs only (`docs/classes/mage.md` dupe
line removed, this entry corrected). No behaviour change remains;
kept as a docs/correction PR rather than closed so the dead-end is
recorded.

Local validation: `bash tools/verify_all.sh`; `git diff --check`;
shared-builder compile via `build-commit.sh` (BUILD OK); live in-game
check not done.
| Anub'Rekhan fight (Naxx): adds-first targeting, swarm center-collapse, flee suppression | `mod-playerbots` | `79bd4281` | `src/Ai/Raid/Naxx/Action/NaxxActions_Anubrekhan.cpp`, `src/Ai/Raid/Naxx/NaxxStrategy.cpp` (Anub rows), `src/Ai/Raid/Naxx/NaxxMultipliers.cpp` (AnubrekhanGenericMultiplier) | Reimplemented trigger-driven; MT kite ring omitted (needs live waypoints) | Kit verified in tw_world + core boss_anubrekhan.cpp (15956, guard 16573, 28785, 28783) | `bash tools/verify_all.sh` + `tools/test_anubrekhan_swarm_policy.cpp`; build-commit + no live test |

## Mage close-range AoE: cone of cold + arcane explosion rows with guards (MAG-6) — 2026-10-10

Donor: mod-playerbots (`79bd4281`):
`src/Ai/Class/Mage/Strategy/GenericMageStrategy.cpp:206` (frost `light
aoe` → cone of cold 21), `src/Ai/Class/Mage/MageActions.cpp:81-109`
(cone/facing + 10yd `isUseful` guards; arcane explosion omni,
range-only).

Source files (module, modified):
`ai/playerbot/strategy/mage/MageActions.h`
(`CastConeOfColdAction::isUseful` now also requires
`AI_VALUE2(bool, "facing", "current target")`; the 10yd range gate
stays in `CastMeleeAoeSpellAction::isUseful`),
`ai/playerbot/strategy/mage/FrostMageStrategy.cpp`
(`FrostMageAoeStrategy`: new `ranged light aoe` → `cone of cold` row at
HIGH, below blizzard/flamestrike medium-aoe rows),
`docs/classes/mage.md` (doc line).

The first version of this PR also added a `melee medium aoe` →
`arcane explosion` row on the base `MageAoeStrategy` (all specs). That
row is REMOVED: review caught that it has no donor source (zero
`xplosion` hits in the donor mage AI; the cited guard block covers
cone/dragon's-breath/blast-wave, and the "omni, range-only" guard is
blast wave's, not arcane explosion's). Donor mages never cast arcane
explosion — the all-specs behaviour was invented scope. Arcane keeps its
pre-existing single-enemy `enemy too close for spell` → `arcane
explosion` row (`ArcaneMageStrategy.cpp:128`), which this PR does not
touch. The remaining cone work (facing `isUseful` + frost `ranged light
aoe` row) is donor-sourced and stays.

Copied / ported / reimplemented: reimplemented in the live list-based
style. Deviations from the donor, all deliberate: (a) [REMOVED with the
explosion row — see above]; (b) frost cone fires on our `ranged light
aoe` (2 attackers in sight) while the 10yd + facing `isUseful` does the
real gating, per the report's accepted v1 semantics; (c) cone action
stays registered under its existing name — only the guard is new, so
the frost-nova fallback node (`MageStrategy.cpp:26`) is untouched.
Finding 2 (cone/explosion tie starving the cone) is moot: with the
explosion row gone there is no tie — cone at HIGH sits above the
`ACTION_NORMAL + 1` nukes and the flamestrike row shares HIGH but pushes
later, so first-pushed-wins orders cone first (donor: cone 21 above
flamestrike/blizzard on the same light/medium pair).

Reason: cone of cold was registered but had zero trigger rows anywhere,
and arcane explosion only fired for arcane bots — solo frost/fire bots
with 2 mobs chewing on them never used either.

Local validation: `bash tools/verify_all.sh`; `git diff --check`;
shared-builder compile via `build-commit.sh` (BUILD OK); live in-game
check pending: 2-mob pack at melee range, cone fires while facing.

## Review fixes (2026-10-10, PR #648 CHANGES_REQUESTED)
Both blocking findings verified real in code and fixed:
- Finding 1 (dead strategies): confirmed — the 6 nodes sat in forward-ported donor-hierarchy classes (`RestoDruid`/`DruidTranquility`, `HealPaladin`, `HealPriest`, `RestoShaman`, `ShamanNonCombat`, plus the `GenericDruid` combat-rez host) with zero `new` sites and no strategy creators, while live bots run the new placeholder→pve/pvp/raid hierarchy. Fixed: the group-heal nodes now live in the equipped AOE strategies (`RestorationDruidAoe`, `HolyPaladinAoe`, `HolyPriestAoe` + `DisciplinePriestAoe`, `RestorationShamanAoe` — all auto-equipped via the spec update actions), and the 12 dead donor-hierarchy files are deleted (incl. the old feral `Feral`/`Cat`/`Bear` bases, unreferenced outside themselves).
- Finding 2 (`chain heal on party` has no action creator): confirmed — only an ActionNode alternative name, not a creator. Fixed: the live shaman node uses registered `NextAction("chain heal", ...)` (`CastChainHealAction`, `CastAoeHealSpellAction`-based, party-targeting).
Non-blocking notes: all four accepted as-is (90 almost-full band is pre-existing local config; `almost full aoe heal` registered-but-unreferenced matches donor; second O(n) group scan negligible; subgroup filter currently group-equality no-op). Also corrected the earlier claim: the trigger/value/policy infra from the first commit is unchanged and now feeds live rows.
verify_all.sh + build-commit.sh + push to same branch per brief (see summary).
## Mage threat dump NOT ported: Lesser Invisibility unobtainable (MAG-4) — 2026-10-10

Donor: mod-playerbots (`79bd4281`)
`src/Ai/Class/Mage/Strategy/GenericMageStrategy.cpp:96-97` (`high
threat` → mirror image, `medium threat` → invisibility).

Decision: WONTFIX (dead code removed instead). Availability check in
`tw_world`, all negative:
- `npc_trainer` (38,037 rows): zero rows teach Lesser Invisibility (66),
Invisibility (885), or the trigger-teacher spells 515/886/1202/1237
(which would grant 66/885 via effect-36).
- Tomes of Lesser Invisibility (item 1002) / Invisibility (item 4160):
zero `npc_vendor` rows, zero `creature_loot_template` /
`gameobject_loot_template` / `fishing_loot_template` rows, zero
`quest_template` item/spell rewards (`RewSpell`/`RewSpellCast`/`RewItem*`
all empty for these ids). No obtain path exists in 1.18.1.
- Mirror Image (the donor's high-threat answer) likewise has no
player-learnable 1.18.1 form.

Source files (module, modified):
`ai/playerbot/strategy/mage/MageActions.h` (deleted
`CastLesserInvisibilityAction`, 6 lines),
`ai/playerbot/strategy/mage/MageAiObjectContext.cpp` (deleted the
`lesser invisibility` registration, 1 line). Zero references to
`lesser invisibility` remain in `ai/`. No strategy rows ever referenced
it (the donor's `medium threat` → invisibility row was never ported),
so no trigger wiring changes. `ThreatMultiplier` (zeroes DPS at high
threat) stays the only mage threat response.

Reason: the registered-but-never-triggered action was dead weight
promising a spell no bot can ever learn; wiring a trigger to it would
produce an every-fight failing cast.

Local validation: `bash tools/verify_all.sh`; `git diff --check`;
shared-builder compile via `build-commit.sh` (BUILD OK); no live test
(nothing behaviourally changes — the action never fired).

## Mage mana gem timing (MAG-9) — 2026-10-10

Donor: mod-playerbots (`79bd4281`):
`src/Ai/Class/Mage/Strategy/GenericMageStrategy.cpp:108-120` (`high
mana`, i.e. below 65%, → use gem 90; `low mana` → evocation 90),
`src/Ai/Class/Mage/MageActions.cpp:31-65` (every gem rank gated on
in-combat + has-item).

Source files (module, modified):
`ai/playerbot/strategy/mage/MageStrategy.cpp`
(`MageStrategy::InitCombatTriggers`: `medium mana` (<40%) → `mana gem`
at HIGH+4; `low mana` (<15%) → `evocation` at HIGH+3 unchanged),
`ai/playerbot/strategy/mage/MageActions.h`
(`UseManaGemAction::isUseful`: in-combat gate via
`AI_VALUE2(bool, "combat", "self target")`; has-item stays in
`UseItemIdAction::isPossible`), `docs/classes/mage.md` (doc line).

Copied / ported / reimplemented: reimplemented. Deviations from the
donor, all deliberate: (a) gem at medium/40% rather than donor's
high/65% — 65% burns the gem in short fights where the mana is never
needed (report MAG-9 recommendation); (b) single best-rank gem action
kept (no six per-rank actions); the split ends the old competition
where gem and evocation fired on the same <15% trigger.

Reason: the gem at 15% lands too late to matter — the fight is nearly
over or evocation is already channeling — and the ungated action could
eat the gem topping up between pulls.

Local validation: `bash tools/verify_all.sh`; `git diff --check`;
shared-builder compile via `build-commit.sh` (BUILD OK); live in-game
check pending: long fight, gem consumed above 15%, evocation still the
last resort.

## Dual-wield MH->OH cascade (AG-2, 2026-10-09)

Donor: mod-playerbots (`79bd4281`):
`src/Ai/Base/Actions/EquipAction.cpp:144-252` (priority 1: new weapon
beats MH -> equip MH, demote old MH to OH when it fits and beats OH;
priority 2: new weapon not beating MH but beating OH -> equip OH).

Source files (module, modified): `ai/playerbot/DualWieldPolicy.h` (new
pure demotion verdict),
`ai/playerbot/strategy/actions/EquipAction.cpp` (audit loop captures the
displaced MH before a 1H MH upgrade and demotes it to OH via the policy +
explicit-slot equip), `tools/test_dual_wield_policy.cpp` (new standalone
test), `tools/verify_all.sh` (register test) + `CHANGELOG.md` (doc line).

Copied / ported / reimplemented: reimplemented in place. Deviations from
the donor, all deliberate: (a) only the demotion is ported - priority 2
already works here via secondary-slot resolution (GetPreferredEquipSlot
targets the weaker/empty hand); (b) all Titan Grip branches dropped (no
1.12 API); (c) a 2H MH upgrade never cascades (it blocks the off hand);
(d) the cascade claims the OH slot for the run and requires spec legality
+ core slot validation, so shield specs and later stale-usage candidates
cannot ping-pong it.

Reason: dual-wield bots left the old main hand's stats in the bags after
every MH upgrade.

Local validation: `bash tools/verify_all.sh` (incl. new policy test +
wiring check live-missing=0); `git diff --check`; shared-builder compile
check; no live in-game test.

## Warlock spec-aware curse default + curse-conflict awareness (WAR-3 + WAR-8) — 2026-10-09

Donor: mod-playerbots (`79bd4281`):
per-spec curse defaults
(`src/Ai/Class/Warlock/Strategy/GenericWarlockStrategy.cpp:138-240`,
`WarlockAiObjectContext.cpp:102-122`: affli Agony, destro Elements);
CoE/CoW conflict skips
(`src/Ai/Class/Warlock/WarlockTriggers.cpp:113-162`).

Source files (module, modified): `runtime/WarlockCursePolicy.h` (new pure
rule: destro Elements, affli/demo Agony, no default while any curse sits
on the target),
`ai/playerbot/strategy/warlock/DestructionWarlockStrategy.cpp`
(`DestructionWarlockCursesStrategy` queues CoE + aoe-gated on-attacker
CoE instead of the base Agony rows — base not called, no double queue),
`ai/playerbot/strategy/warlock/WarlockTriggers.cpp` (`NoCurseTrigger` +
`NoCurseOnAttackerTrigger` drop the owner check so a groupmate's curse
suppresses the default),
`tools/test_warlock_curse_policy.cpp` (new standalone test, wired into
`tools/verify_all.sh`) + `docs/classes/warlock.md`, `CHANGELOG.md` (doc
lines).

Copied / ported / reimplemented: reimplemented in place. Deviations from
the donor, all deliberate: (a) no `low mana` emergency row — ours already
taps at mana<=mediumMana (default 40, stricter than donor `low mana`
15%) at NORMAL+2, kept as the urgent band; (b) no combat top-up row — the
donor's 5.1 filler sits under its nuke, but here defaults are pushed at
relevance-200 so any trigger row would preempt the shadow-bolt default
and tap instead of nuking; the top-up band pre-taps out of combat only;
(c) the health floor stays ours (`lowHealth` default 50, stricter than
donor 45); (d) no glyph-buff row (WotLK glyph, no 1.18.1 spell); (e)
Affliction Dark Pact on low mana untouched and still wins the emergency.

Reason: warlock bots entered every pull at whatever mana the last fight
left and spent the second half wanding; donor tops up between pulls, so
ours pre-taps out of combat to enter near-full.

Local validation: `bash tools/verify_all.sh`; `git diff --check`;
shared-builder compile via `build-commit.sh` (BUILD OK); live in-game
check pending: bot enters pull near-full mana.
the donor, all deliberate: (a) the donor's WotLK conflict lists (Ebon
Plague, Earth and Moon, Vindication) do not exist in 1.18.1 — the rule
here is the vanilla one-curse-per-target gate on the 7-curse family,
which covers CoW-vs-shout overwrites too; (b) Curse of Shadow stays a
manual pick, not the affliction default (raid debuff-slot pressure);
(c) per-curse `DebuffTrigger`s already see anyone's aura, so only the
`NoCurse` pair needed the fix; (d) manual `curse X` strategies bypass the
gate — the player always wins.

Reason: destro bots opened Agony like everyone else (lost fire damage +
raid spell damage), and two warlocks on one target overwrote each
other's curse every refresh.

Local validation: `bash tools/verify_all.sh`; `git diff --check`;
shared-builder compile via `build-commit.sh` (BUILD OK); live in-game
check pending: destro opens CoE, grouped warlocks keep one curse.
| Elemental water-shield mana loop + pack-gated chain lightning (SHM-6) | Donor behavior: ele keeps water shield + chain-lightning-no-cd (`mod-playerbots` `ElementalShamanStrategy.cpp:76-83`, `ShamanAoeStrategy` `GenericShamanStrategy.cpp:161`) @ `79bd4281` | `ai/playerbot/strategy/shaman/ElementalShamanStrategy.cpp` (buff combat + non-combat water rows + lightning fallback rows; AoE `chain lightning filler` row), `ShamanTriggers.h` (`ChainLightningReadyTrigger` via CD_TRIGGER), `ShamanAiObjectContext.cpp` (`chain lightning filler` = TwoTriggers ready + ranged-medium-aoe), `ShamanManaLoopPolicy.h` + `tools/test_shaman_mana_loop_policy.cpp` | Water at ACTION_NORMAL combat + non-combat with lightning at NORMAL-1 fallback (Water trains 34, Lightning 8 — verified npc_trainer; low-level ele keeps a shield). Filler is pack-only by trigger conjunction (ready + 3+ ranged pack), queued in the AoE strategy below earthquake — never single-target (no CC breaks, no OOM spam). Spells: Water Shield Turtle ranks (verified), Chain Lightning 421 (existing). No WotLK-only spells | `bash tools/verify_all.sh` (incl. new policy test), `git diff --check`; shared-builder compile + no live test per parity pipeline |

## Druid parity DRU-6: Faerie Fire (Feral) spam — NOT PORTED (rejected on module mechanics) — 2026-10-09
Review of the original spam port (PR #585, review findings verified in
code and accepted): the donor trigger behaviour does not port as a
trigger-only change, so the override was reverted and the pre-existing
plain-debuff behaviour kept. Evidence, corrected after review round 2
(three original claims were factually wrong and are corrected here):
(1) the real blocker is module-side, not core-side:
`CastAuraSpellAction::isUseful` refuses recast while the aura stands
(`BuffNeedsRefresh` false for the sub-5-min debuff), so a trigger-only
port degrades to apply-once with extra failure modes. Core DOES apply
flat 108 threat per 16857 cast (`spell_threat` row, no debuff check) —
but that threat is unreachable through the aura-gated module action; a
future action-layer attempt (FFF action off `CastSpellAction`, donor
shape) could collect it and is not ruled out.
(2) the override bypassed `DebuffTrigger`'s `HasSpell` guard, evaluating
the spam branches every combat tick for bears/cats without the spell
(wasted per-tick evaluation; the engine drops the un-castable action at
`isUseful`/`isPossible`, so queue pollution, not pollution — harm
overstated originally). The "not trainer-taught" claim was false: 3739
IS `Faerie Fire (Feral)`, Effect 36 LEARN_SPELL teaching 17390, sold by
druid trainers at 30.
(3) Omen fishing is WotLK thinking: 16864 procs off melee flags
(`spell_proc_event`), so FFF casts don't fish procs, and FFF deals no
damage and builds no CP — filler GCDs buy nothing next to Shred, which
builds CP and can proc Omen. (Priority detail: the live wired cat row
is NORMAL+5, above the builders — the review's 5.0 figure is the dead
legacy `CatDruidStrategy`; the mechanics objection stands regardless.)

Source repository: `mod-playerbots` @ `79bd4281` (local checkout
`../playerbots-references/mod-playerbots`),
`src/Ai/Class/Druid/DruidTriggers.h:120-153` (reference only, not ported).

Source files (module, modified): none — full revert to pre-PR behaviour.
`docs/classes/druid.md` line added by the original PR removed again.

Local validation: `bash tools/verify_all.sh`; `git diff --check`. No
live test (no live test per parity brief); build via build-commit.sh.

## Druid parity DRU-8: resto healer-dps rows in the wired restoration strategy — 2026-10-09
Feature: `DruidOffdpsStrategy::InitCombatTriggers` rewritten priest-style
(`healer should attack` gate — already mana-gates via
`HealerShouldAttackTrigger(checkMana=true)` — with FF/IS/MF/SF/Wrath at
ACTION_DEFAULT+0.5..0.2, so every heal outbids every nuke) + `offdps`
placeholder creator + `OffdpsSituationStrategyFactoryInternal`
(`offdps pve/pvp/raid` -> existing orphaned `DruidOffdps*Strategy`
classes) + factory registration in the druid context constructor +
`{"offdps"}` update-strat entries in all three
`UpdateDruid{Pve,Pvp,Raid}StrategiesAction`s (copy of the priest entries).

Source repository: `mod-playerbots` @ `79bd4281` (local checkout
`../playerbots-references/mod-playerbots`).

Source files (donor, reference only):
`src/Ai/Class/Druid/Strategy/GenericDruidStrategy.cpp:151-158`
(`healer should attack` -> cancel-tree + moonfire/wrath/starfire 5.x) +
priest `PriestStrategy.cpp:616-650` (gating shape) +
`PriestAiObjectContext.cpp:26,109-118` (placeholder + situation factory
+ constructor registration). Deviations, deliberate: no `cancel tree
form` action was added (the report's M-sized sub-task proved
unnecessary) — every dps action already carries the caster-form
prerequisite node and `CastCasterFormAction::isUseful` covers tree of
life, so Tree exits through the normal shift. No `no mana` fallback row
either (priest wands; druids have no wand — an OOM wrath cast is
impossible, so the row would only queue failures). Default OFF (opt-in
`+offdps` via the existing `.bot strategy` mechanism, matches priest).

Reason: druid parity report DRU-8 — wired resto had no healer-dps rows;
priest/shaman/paladin all have new-style healer-dps.

Source files (module, modified):
`ai/playerbot/strategy/druid/DruidStrategy.cpp`,
`ai/playerbot/strategy/druid/DruidAiObjectContext.cpp`,
`ai/playerbot/strategy/druid/DruidActions.h` +
`docs/classes/druid.md` (behaviour lines).

Copied / ported / reimplemented: reimplemented in place in the live
strategy idiom. No new spells: Moonfire/Wrath/Starfire/Faerie
Fire/Insect Swarm trainer-taught, creators pre-registered.

Local validation: `bash tools/verify_all.sh` (wiring check:
live-missing=0); `git diff --check`. No live test (no live test per
parity brief); build via build-commit.sh.

## Review fixes (2026-10-10, PR #610 CHANGES_REQUESTED)
Both blocking findings verified real in code and fixed:
- Finding 1 (no tree-form exit): confirmed — zero ActionNode creators cover ff/is/mf/wrath, and the only starfire node is balance-spec-gated. My "caster-form prerequisite" comment was wrong. Fixed: `caster form` (instant RemoveShapeshift exit) rides as the first alternative in the offdps row, nukes as fallbacks — donor shape (cancel 5.4 > nukes).
- Finding 2 (exit/re-enter livelock): confirmed — tree re-entry at HIGH would win every tick after the exit. Fixed with the donor's `no healer dps strategy` gate: new `NoOffdpsTrigger` (`!HasStrategy("offdps", COMBAT)`) + `tree form and no offdps` TwoTriggers combo on the Tree row; offdps rows stay on plain `healer should attack` (strategy membership is the opt-in gate).
- Non-blocking: nuke order corrected to donor (wrath above starfire); insect swarm + faerie fire dropped (donor healer-dps is MF/Wrath/SF; IS is a balance talent most restos lack); doc corrected (auto-added for random bots like priest holy, not opt-in); solo-OOM claim dropped (gate returns true groupless — harmless fallthrough to melee default).
verify_all.sh + build-commit.sh + push to same branch per brief (see summary).

## Warrior WAR-9: skip Battle Shout under stronger Might (2026-10-09)

Feature: `BattleShoutTrigger` now compares attack-power values and stays
inactive while an equal-or-stronger Blessing of Might (regular or greater)
is on the bot. A stronger shout rank still fires over a weaker might.
Pure rule extracted to `ai/playerbot/BattleShoutPolicy.h` with standalone
test `tools/test_battle_shout_policy.cpp` (registered in
`tools/verify_all.sh`).

Source repository: `mod-playerbots/mod-playerbots`

Source commit: `79bd4281` (local
`playerbots-references/mod-playerbots` checkout).

Source files:
- `src/Ai/Class/Warrior/WarriorTriggers.cpp:75-140` (`BattleShoutTrigger::IsActive` AP comparison incl. Commanding Presence half)

Copied / ported / independently reimplemented: reimplemented in place
(`WarriorTriggers.h` delegates to the new policy header).
Deviations from the donor, all deliberate: (a) static AP tables instead of
the donor's SpellInfo scan — our 1.12 ids are stable and the trigger
already hardcodes shout ids (every value verified against
`spell_template` EffectBasePoints+1: shout 15/35/55/85/130/185/232, might
20/35/55/85/115/155/185 incl. R7 25291, greater 155/185); (b) the talent
multiplier ported under its Vanilla name — donor COMMANDING_PRESENCE_RANKS
are literally the Vanilla Improved Battle Shout ids
(12318/12857/12858/12860/12861, +5%/rank), credited via spellbook check;
(c) trigger only, the `battle shout` action itself is unchanged so an
explicit player order still shouts.

Reason: WAR-9 in the warrior parity sweep: every paladin group wasted rage
and a GCD shouting over a stronger might.

Local validation: `bash tools/verify_all.sh` (incl. new policy test);
`git diff --check`. No live test (per task constraints).

## Priest parity healer damage: PRI-2 default offdps (PRI-6 mana burn rejected) — 2026-10-09
Feature: discipline bots now ship with the `offdps` strategy on by
default instead of `offheal` (AiFactory, inside the existing
`enableOffSpecStrategies` gate — matching every other heal spec:
paladin holy, shaman resto, druid resto all get `offdps` only); holy
keeps its base `offdps`. The player can `-offdps` per bot. The `healer
should attack` mana and nobody-hurt gates are unchanged, so healers
still heal first. Mana Burn stays unwired: the donor's `low mana` burn
row lives in its solo-only DPS spec (never in the grouped-healer kit),
and the ladder entry was dead code (comfortable-mana trigger vs
below-half-mana action gate).

Source repository: `mod-playerbots` @ `79bd4281` (local checkout
`../playerbots-references/mod-playerbots`).

Source files (donor, reference only):
`src/Ai/Class/Priest/Strategy/GenericPriestStrategy.cpp:74-89`
(`PriestHealerDpsStrategy`: SWP/holy-fire/smite/mind-blast/shoot ladder,
no low-mana row) +
`src/Ai/Class/Priest/Strategy/HolyPriestStrategy.cpp:32-76`
(`HolyDpsPriestStrategy`: smite/mana-burn/starshards defaults + low-mana
burn row — solo-only spec per donor AiFactory.cpp:433, never grouped).
Deviations, deliberate: no new `holy dps` strategy name — the local
`offdps` ladder (SWP/holy-fire/smite/starshards/mind-blast, Holy Nova
for Mind Sear) already carries the grouped-healer kit behind the tested
healer gate; this PR only moves disc onto it. Mana Burn deliberately
left unwired (see Feature).

Reason: priest parity report PRI-2 — default disc bots dealt zero
damage when nobody needed healing (holy already had base `offdps`;
disc's `offheal` was the anomaly). PRI-6 Mana Burn rejected on review:
no grouped-healer donor precedent, contradicts the mana-conservation
design, and the action gate made the ladder entry dead code.

Source files (module, modified): `ai/playerbot/AiFactory.cpp` +
`docs/classes/priest.md` (off-spec section). (`PriestStrategy.cpp` mana-burn
wiring added then removed on review — no net diff.)

Copied / ported / reimplemented: reimplemented in place. No new spells
(Mana Burn 8129+ in 1.18.1 data).

Local validation: `bash tools/verify_all.sh`; `git diff --check`.
Build via build-commit.sh. No live test.
| Ossirian crystal tactic (AQ20): crystal-run timing by buff/debuff-vs-travel-time, wait-in-range + 25yd use guards, single CMSG_GAMEOBJ_USE | `mod-playerbots` | `79bd4281` | `src/Ai/Raid/Aq20/Aq20Triggers.cpp` (Aq20MoveToCrystalTrigger), `src/Ai/Raid/Aq20/Aq20Actions.cpp` (Aq20UseCrystalAction), `src/Ai/Raid/Aq20/Aq20Utils.cpp` (buff/debuff/crystal helpers) | Reimplemented: trigger-driven `ossirian crystal run` + `use ossirian crystal` on the new `ruins of ahn'qiraj`/`ossirian` strategies; 1.12 single queued use-packet (no report-use opcode; cf. suppression-device precedent) | Donor's entire AQ20 module, all IDs verified in tw_world | `bash tools/verify_all.sh` + `tools/test_ossirian_crystal_policy.cpp`; build-commit + no live test |
