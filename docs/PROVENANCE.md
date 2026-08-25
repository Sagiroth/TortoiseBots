# PROVENANCE — TortoiseBots behavior harvesting log

> **Append-oriented historical record.** This file is the source-lineage and
> validation ledger for imported/adapted behavior; it is not the current
> roadmap. Start with [PLAN.md](PLAN.md).

Record every substantial port/reimplementation here for attribution,
licensing, reasoning and local validation.

| Feature | Source project | Source commit | Source files | Ported / reimplemented | Reason | Local validation |
| --- | --- | --- | --- | --- | ---: | --- |
| SessionTransport Headless pattern (`SessionTransport::Headless`, `IsHeadless()`, `HasNetworkTransport()`, `InitHeadlessSession()` with `NullSessionAnticheat`) | `Shyalya/tortoise-wow` (vendored `cmangos/playerbots` via `r-o-sh/tortoise-wow:playerbots-integration-gh`) and `Penqle/tortoise-wow` core's existing `NullSessionAnticheat` | `shyalya-tortoise-wow@1f9497e` (checkpoint `0af2567` 2026-05-10, vendored `cmangos/playerbots@c33dfac`) / `Anticheat/Anticheat.h:143` (`NullSessionAnticheat`) | `Anticheat/Anticheat.h:143-209` (`NullSessionAnticheat`, `NullAnticheatLib`), `shyalya` host hooks for bot sessions (80-file surface) | Reimplemented as generic transport enum, not copied — core now distinguishes `Network` vs `Headless` via `SessionTransport`, module interprets `IsHeadless() == true` as bot. `InitHeadlessSession()` directly assigns `NullSessionAnticheat` (existing core precedent) | Harvest the null-transport precedent without inheriting Shyalya's 80-file host surface; keep host integration ≤5 files | `rg` audits for `GetBot/m_bot` remain clean; `BUILD_PLAYERBOTS=ON/OFF` matrix builds; Docker runtime spike: headless session survives `World::Update` (not deleted as disconnected), `HandlePlayerLogin` succeeds after queued `AddSession` + deferred `LoginPlayer`, bot enters world and re-enters after logout |
| `IWorldUpdateListener` generic world-tick registry (`RegisterWorldUpdateListener`, `GetPendingWorldListenerFactories`, `RegisterPendingWorldListeners` in `World::Update`) | `HardcodedEvents`/`ZoneScriptMgr::Update` pattern in `Penqle/tortoise-wow` + `DiscordBot::RegisterHandlers` precedent in same core | `World.cpp:2448` (`World::Update`), `HardcodedEvents.h`, `ZoneScriptMgr.cpp:117` (`Update`), `World.cpp:2343` (`DiscordBot::RegisterHandlers`) | `World.h:889`, `World.cpp:2448`, `HardcodedEvents.h`, `ZoneScriptMgr` | Reimplemented as generic `IWorldUpdateListener` with explicit registration and pending-factory static initializers (no weak symbols, no `sBotHost` global) — inspirited by `DiscordBot`'s service registration but made generic | Bot AI must run on the world thread once per tick; no existing `WorldScript::OnUpdate` exists in MaNGOS `ScriptMgr`, so a single generic call site in `World::Update` is the correct seam | `World::Update` now calls listeners after `UpdateSessions`; `BotHostAdapter` receives tick, `BotManager` drives lifecycle; `rg -i PlayerBot` in `src/game` only shows `InitHeadlessSession` bridge |
| `BUILD_PLAYERBOTS` optional-module CMake wiring | `cmangos/mangos-classic` (`cmake/options.cmake: BUILD_PLAYERBOTS OFF`) and `mangoszero/server` (`CMakeLists.txt: PLAYERBOTS OFF`) | `cmangos-mangos-classic@9b682be`, `mangoszero-server@1817ae1` | `CMakeLists.txt`, `src/CMakeLists.txt`, `src/game/CMakeLists.txt`, `src/mangosd/CMakeLists.txt` | Reimplemented as explicit `option(BUILD_PLAYERBOTS OFF)` with `add_subdirectory(modules/TortoiseBots)` only when `ON`, and `target_link_libraries(mangosd tortoise_bots)` via `CMP0079` + `whole-archive` on Linux — no `FetchContent` auto-download, no `ENABLE_PLAYERBOTS` scattered defines | Keep `BUILD_PLAYERBOTS=OFF` first-class and `src/modules/TortoiseBots` absent/present matrix clean; harvest the option pattern without the `FetchContent` auto-clone | Matrix: `absent+OFF` OK, `present+OFF` OK (module present but not built), `present+ON` OK (module linked); `rg` audits clean |
| Headless queued-session lifecycle (`WorldSession::Update`, `CharacterScreenIdleKick`, queued add/remove) | `Shyalya`'s `NullSessionAnticheat` + `WorldSession::Update` null-socket handling (`WorldSession.cpp:163,383,736` already tolerates `m_Socket==nullptr` but deletes headless via `return false`) | `WorldSession.cpp:306,334,378`, `Handlers/CharacterHandler.cpp:548`, `World.cpp:283`, `LockedQueue.h` | Reimplemented: explicit `SessionTransport`, a one-pass `m_headlessLoginPending` keepalive, deferred `LoginPlayer` after queued `AddSession`, and generic pending-session inspection/cancellation. `BotManager` retains `Removing` records until cleanup. | The queued path must survive the first `UpdateSessions` pass without requiring synchronous insertion; immediate removal must cancel the queue entry instead of orphaning it. | Queued runtime spike passed; `PendingAddRemoveTest PASSED` with no active/pending session, player, or record; graceful shutdown clears both online flags. |

| Foundational Engine/AiObjectContext/Strategy/Trigger/Action/Value/ReactionEngine (Tortoise 1.18.1 baseline) | `Shyalya/tortoise-wow` (`playerbots-integration-gh` @ 1f9497e, vendored `cmangos/playerbots@c33dfac`) | `Shyalya` baseline provides the full `playerbot/strategy/Engine.{h,cpp}`, `AiObjectContext.{h,cpp}`, `AiObject.{h,cpp}`, `Strategy.{h,cpp}`, `Trigger.{h,cpp}`, `Action.{h,cpp}`, `Value.{h,cpp}`, `ReactionEngine.{h,cpp}`, `Queue/Event/Multiplier` etc, already translated for `MANGOSBOT_ZERO` (Vanilla 1.12/1.18.1) and `Penqle`'s `WorldLocation`/`Position`/`Map` APIs | `ai/playerbot/strategy/Engine.*`, `AiObjectContext.*`, `AiObject.*`, `Strategy.*`, `Trigger.*`, `Action.*`, `Value.*`, `ReactionEngine.*`, `Queue.*`, `Event.*`, `AiObject.*` (full `ai/playerbot` tree, 82 top-level + 14 strategy core + 113 generic) | Copied verbatim as the Tortoise/Vanilla translation reference for the foundational runtime; `cmangos-compat-shim.h` and `botpch.h` already handle `Penqle` `SpellEntry`/`ItemPrototype`/`MapStorage` translation, Vanilla `MANGOSBOT_ZERO` guards exclude `deathknight`/`TBC`/`WotLK` paths | Shyalya's `playerbots-integration-gh` is the only proven `1.18.1` PlayerBots that already runs on `Penqle`'s `WorldLocation` (`mapId/x/y/z/o`), `Transport`/`GenericTransport`, `GuidSet`/`AreaTableEntry` etc; using it as the baseline avoids reinventing `Penqle` API translation and keeps `Headless`/`IsHeadless()` as the only host seam | `ai/` now contains the full Shyalya `playerbot` tree (204 generic files after modern layer); `CMakeLists.txt` now builds the real `Engine`/`AiObjectContext`/`Strategy` stack instead of the stub `EngineStub`/`AiObjectContextStub`; native linkage uses explicit `MODULE_TORTOISEBOTS=static` and `MANGOSBOT_ZERO` |
| Modern generic Base behavior (204 files, `Follow`/`Combat`/`Dead`/`Ranged`/`Melee` etc) | `mod-playerbots` `src/Ai/Base/Strategy` @ 5397110cba48 (merge #2661) + `Shyalya` `strategy/generic` @ 1f9497e | `mod-playerbots: src/Ai/Base/Strategy/*.{h,cpp}` (91 files, modern `FollowMasterStrategy` now `getDefaultActions()` + `InitTriggers` vs Shyalya's `InitNonCombatTriggers`/`InitCombatTriggers` + `NextAction::array`) / `shyalya: strategy/generic/*` (113 files, includes `BlackwingLair`/`Karazhan` dungeon strategies) | `ai/playerbot/strategy/generic/*` (204 files after modern layer; donor SHAs are retained here instead of backup copies) | Forward-ported the modern `mod-playerbots` generic set on top of Shyalya's Tortoise-translated generic; Vanilla/Tortoise `MANGOSBOT_ZERO` guards kept and expansion-only paths excluded | Modern class/combat/follow behavior remains attributable to the pinned donor commits; obsolete `*.shyalya.bak` copies were removed once this provenance record was complete |
| Modern class factory + per-class Strategy subfolders (9 Vanilla classes) | `mod-playerbots` `src/Ai/Class/{Warrior,Mage,Priest,Druid,Hunter,Rogue,Paladin,Shaman,Warlock}/AiObjectContext.*` + `Strategy/*.{h,cpp}` @ 5397110 + `Shyalya` `strategy/{warrior,mage,priest,...}/` @ 1f9497e | `mod-playerbots: src/Ai/Class/Warrior/{WarriorAiObjectContext.*,Strategy/*.cpp}` etc / `shyalya: strategy/warrior/{WarriorAiObjectContext.*,Arms/Fury/Protection/Tank...Strategy}` etc | `ai/playerbot/strategy/{warrior,mage,priest,druid,hunter,rogue,paladin,shaman,warlock}/*` | Forward-ported the nine Vanilla class contexts and strategy families; `AiFactory` now instantiates each native class context rather than silently falling back to the generic context | Fresh runtime attached real Warrior, Mage, Priest, and Hunter contexts; Warrior/Mage/Priest completed packet group journeys and Hunter completed via the deterministic existing-action diagnostic path |

Notes (updated 2026-08-22 — large-batch forward-port):

- Foundational runtime is now substantially real (not a stub): `ai/playerbot` contains the full Shyalya `playerbot` tree (Tortoise `1.18.1` translation, `MANGOSBOT_ZERO`) plus the modern `mod-playerbots@5397110` generic and nine class contexts. The obsolete `*.shyalya.bak` and `.orig` migration copies were removed after donor SHAs and source paths were recorded above. `CMakeLists.txt` builds the real `Engine`/`AiObjectContext`/`Strategy`/`Trigger`/`Action`/`Value`/`ReactionEngine`/`AiFactory` stack; `deathknight`/`WotLK` remains excluded via `MANGOSBOT_ZERO`.
- Host seam remains generic and minimal: `SessionTransport`, `IsHeadless()`, `HasNetworkTransport()`, `IWorldUpdateListener` (≤5 host files, verified via `rg -n -i 'PlayerBot|BotService|Headless' src/game` only shows `InitHeadlessSession`). No `IsBot()`/`GetBot()`/`m_bot`/`sPlayerBotMgr` reintroduced. Same-account `1 Network + N Headless` GUID-driven lifecycle and human reclaim are preserved via `BotManager` + `BotSessionAdapter`.
- Upstream licenses (GPL-2.0 for MaNGOS/CMaNGOS/Shyalya, GPL-2.0 for `mod-playerbots`) are preserved; headers retain original copyright/license and this file records donor SHAs/source files before migration copies were deleted. No `AzerothCore` `PlayerbotMgr`/`BotSession` ownership model is reintroduced.
- The broad donor tree is now wired into the active CMake source set: real `PlayerbotAI`/`AiFactory`, generic behavior, all nine Vanilla class folders, Value/Trigger/Action families, Travel, grouping, loot, quests, dungeon/raid and PvP families are compiled as one coherent batch rather than left dead in-tree. `MANGOSBOT_ZERO` filters expansion-only folders. Fresh runtime probes now cover the packet bridge and Warrior/Mage/Priest/Hunter class attachment/group journeys.
| Follow (dead-zone 1.5y, MoveFollow behind M_PI, public native target/moving-state restart guard, CanFollow guards) | `cmangos/playerbots` + `mangoszero/server` | `cmangos-playerbots@076045e` / `mangoszero-server@1817ae1` | `cmangos: playerbot/strategy/actions/FollowActions.cpp:36-90`; `mangoszero: src/modules/Bots/playerbot/strategy/actions/MovementActions.cpp:440-560`; local `ServerFacade.cpp` adapter over Penqle `MotionMaster::GetCurrent()` | The real PlayerbotAI path uses `FollowMasterStrategy`/`FollowAction`; the Tortoise adapter reads the public native targeted-generator target and moving state instead of pretending Penqle's private angle/offset fields are available. `BotController` retains only an intent/diagnostic record and is never a gameplay fallback after AI attachment | Keep 1.5y jitter-free follow without a second movement owner or re-entrant generator replacement | Cached ON/static build; preserved AI-enabled runtime packet journey exercised `follow chat shortcut`, group invite/accept, and cleanup without a movement-state crash |
| Warrior vertical slice | Shyalya `playerbots-integration-gh` + modern `mod-playerbots` | `1f9497e` / `5397110` | Focused `Engine`/`Queue`/`Trigger`/`Action`/`Value` primitives, generic `FollowMasterStrategy`/assist/combat/non-combat/dead strategies, and Warrior Arms/Fury/Protection strategy files | Ported/adapted focused family; unrelated expansion systems remain excluded by the CMake source set | Make one owned Tortoise bot use strategy-driven follow and combat before widening the donor tree | Cached Docker build: `Built target tortoise_bots`, `Built target mangosd`; runtime: same-account Headless Sagiroth + Dudette, `dps assist`, successful Warrior Heroic Strike spell 78, encounter end/follow resume, clean removal; `PlayerbotAIStorage` logout use-after-free fixed and revalidated |
| Broad Vanilla/Turtle source-set checkpoint | Shyalya `playerbots-integration-gh` + modern `mod-playerbots` | `1f9497e` / `5397110` | `CMakeLists.txt` globs the real `PlayerbotAI`, generic, nine Vanilla class, Value/Trigger/Action, Travel, grouping, loot, quest, dungeon/raid, BG/PvP and economy families; `MANGOSBOT_ZERO` excludes Death Knight and other expansion-only paths | Adapted Penqle naming and data shapes in the module-local compatibility layer; native loot ownership, area names/flags, channel wrappers and const loot-list views replace unsafe CMaNGOS member assumptions | Compile and stabilize the broad family without adding core `GetBot`/`m_bot` ownership | The first broad Docker pass reached the module compilation stage and exposed a small remaining Penqle API family in `PlayerbotAI.cpp`; the correct Penqle `module-system` host snapshot must also carry the documented generic Headless/session seams before a broad runtime claim is made |

## Native module-system checkpoint — 2026-08-24

Feature: Native module packaging, module-local runtime support, and broad Vanilla/Turtle source selection

Source repository: local Penqle `tortoise-wow` sibling plus local TortoiseBots checkout

Source commit: core `73f32c063e6c4481a0415690896025178ca8076f` on branch `playerbots-integration-gh`; TortoiseBots `3484208` (`Finish native PlayerBots integration and playtest bridge`). This checkpoint is superseded by core `9487c5150a6553c665fafc1f4568669b8b00f011`; the core commits `133c6d19` and `9487c515` keep static-module include paths target-local and remove the stale `src/game/PlayerBots` common path.

Source files: `TortoiseBots.cmake`, `src/TortoiseBotsModule.cpp`, `host/*`, `runtime/*`, `ai/playerbot/*`, `strategy/{generic,druid,hunter,mage,paladin,priest,rogue,shaman,warlock,warrior}/*`, `strategy/{actions,triggers,values}/*`

Copied / ported / independently reimplemented: behavior was ported/adapted from local `shyalya-tortoise-wow@1f9497e0f42bfc1055841bb6ebdc7caa3515de0b`, `cmangos-playerbots@076045efa835da9aab7caa943bca752aebe1baad`, and `mod-playerbots@5397110cba484a9b7209bc9f632652e9d4bd6a70`; host lifecycle and BotManager ownership were independently reimplemented around generic `SessionTransport`/Headless APIs.

Reason: use existing combat/class/travel behavior without compiling donor manager, random-manager, login-manager, or second-session ownership into the target core.

Local validation: static `modules` target and `mangosd` link passed with `BUILD_PLAYERBOTS=ON`, `BUILD_LEGACY_PLAYERBOTS=OFF`, `MODULE_TORTOISEBOTS=static`; the complementary `BUILD_PLAYERBOTS=OFF`, `MODULES=disabled` `mangosd` build passed; the local Penqle runtime reached `TortoiseBots: native module loaded (AI enabled)` and `World server is up and running` after applying the three pending non-destructive world migrations required by the preserved database. Generated flags prove TortoiseBots definitions/includes/PCH are on `mod_tortoisebots_static`, not the combined `modules` target.

Explicit gaps at that earlier checkpoint: `AutoMaintenanceOnLevelupAction`, advanced `FishingAction`, `InventoryAction`/`TellEmblemsAction`, `NonCombatActions`, guardian-oriented `PetsAction`, `TellPvpStatsAction`, extended trade reporting, donor `TradeValues.cpp`, and expansion-only LFG/glyph/Karazhan/vehicle/Arena registrations remained excluded where they required WotLK/AzerothCore APIs or unsupported host data. Native loot, quest, inventory operations, travel, trade, class combat, pet-taming, and lockpicking paths are now compiled; no empty gameplay stubs were added.

Architecture note: core integration is generic Headless transport/session lifecycle plus ScriptMgr hooks. `MODULE_TORTOISEBOTS=static` selects the native module and does not pull the legacy vendored CMaNGOS tree; `BUILD_LEGACY_PLAYERBOTS` is a separate explicit escape hatch.

## Native runtime and Vanilla/Turtle behavior checkpoint — 2026-08-24

Feature: Pet taming/control, lockpicking, bounded random-bot lifecycle, AH/economy pricing, named-location travel lookup, cache-safe startup, and native module SQL packaging

Source repository: local `TortoiseBots` checkout; host/runtime seam in the local Penqle `tortoise-wow` sibling

Source commit: TortoiseBots native stabilization work after `6a78b5c`; behavior references `shyalya-tortoise-wow@1f9497e0f42bfc1055841bb6ebdc7caa3515de0b`, `cmangos-playerbots@076045efa835da9aab7c943bca752aebe1baad`, and `mod-playerbots@5397110cba484a9b7209bc9f632652e9d4bd6a70`; required core seam `9487c5150a6553c665fafc1f4568669b8b00f011` on `playerbots-integration-gh`

Source files: `TameAction.*`, `UnlockItemAction.*`, `UnlockTradedItemAction.*`, `ChatActionContext.h`, `WorldPacketActionContext.h`, `runtime/RandomBotService.*`, `runtime/PlayerbotRuntimeFacade.cpp`, `ai/playerbot/RandomItemMgr.cpp`, `data/sql/{world,char}/*`, `TortoiseBots.cmake`, `host/BotHostAdapter.cpp`, `conf/tortoise_bots.conf.dist`

Copied / ported / independently reimplemented:

- Tame-beast behavior was independently reimplemented around the host's real `SPELL_EFFECT_TAMECREATURE` path; rename and abandon use the native `Pet`/`Player` APIs. The old WotLK pet-stable construction was not retained.
- Lockpicking was ported to `ItemPrototype`, `LockEntry`, `ITEM_DYNFLAG_UNLOCKED`, the native Pick Lock spell, and the native trade-slot path. The old AzerothCore `ItemTemplate`/extended trade wrappers were not retained.
- Random bots use a module-local, startup-loaded pool of pre-existing characters on the configured random-account prefix. `World` owns Headless/Network session lifetime; `BotManager` owns module records and AI adapters. Account/character creation and donor login managers remain intentionally outside the module.
- Random-bot buy/sell multipliers are now cached per character with the Existing Vanilla ranges, and named-location lookup uses the native `ai_playerbot_named_location` table instead of a compatibility no-op.
- Empty optional item/equipment caches are accepted without synchronous world-thread cache generation. Populated existing caches still load normally.
- Schema-only native migrations cover the tables queried by the active Vanilla/Turtle AI initializer and per-bot state. Existing datasets remain deployable separately.

Reason: complete coherent Vanilla/Turtle families without reintroducing donor manager/session ownership or making optional AI startup depend on a large synchronous cache write.

Local validation: ON/static `mangosd` build passed after the cache, config, SQL-install, installed-module-path, economy, recovery, packet, command, and class-context changes; OFF/disabled `mangosd` build passed and the ON/static configuration was restored. Docker runtime with AI enabled loaded the module, attached real Warrior/Mage/Priest/Hunter contexts, passed packet-bridge group invite/accept and cleanup journeys, and retained the earlier save/logout/relog spike evidence. The preserved DB was not reset; only additive missing schema migrations and disposable `TBPLAY` class fixtures were added. Random pool startup correctly reported zero candidates because no `RNDBOT*` accounts exist in the fixture.

Known scope gates: the physical tree and positive CMake graph contain no DK,
glyph, vehicle, Arena, Karazhan, or expansion-only donor families. Native
core LFG/meeting-stone behavior remains available; the module retains only
applicable group-role helpers and does not recreate the donor automatic queue.
The donor `PetsAction` guardian-control wrapper and post-Vanilla fishing
wrapper remain excluded because native pet, fishing, travel, loot, and
profession paths provide the applicable Vanilla/Turtle behavior.
Account/character auto-creation remains the intentional random-bot product
gap; existing random characters, bounded login/logout, native TravelMgr
relocation, AI strategy rotation/recovery, gear refresh, and AH/economy pricing
are supported. Core `BattleGroundMgr` remains authoritative for Vanilla and
Turtle battleground entries; the existing value compatibility view reads the
same native `battlemaster_entry` table once at AI startup and does not own
battleground state.

## Packet/config/static-isolation and playtest gate — 2026-08-24

Feature: generic packet/event bridge, safe multi-value configuration, isolated native static-module settings, existing command surface, random-bot debt cleanup, and fresh class journeys.

Source repository: TortoiseBots `phase4-follow@3484208`; required Penqle core `playerbots-integration-gh@9487c5150a6553c665fafc1f4568669b8b00f011` (parent `133c6d19bf5898c1e4f5129b2890b1db89b17a07`).

Source files: `host/BotPacketAdapter.*`, `runtime/BotManager.*`, `runtime/PlayerbotAIAdapter.cpp`, `runtime/PlayerbotRuntimeFacade.cpp`, `runtime/RandomBotService.*`, `commands/BotCommands.cpp`, `ai/playerbot/PlayerbotAIConfig.*`, `ai/playerbot/strategy/ValueMacros.h`, `ai/playerbot/AiFactory.cpp`, `TortoiseBots.cmake`, `conf/tortoise_bots.conf.dist`, and Penqle `modules/CMakeLists.txt`/`Config`.

Copied / ported / independently reimplemented: the packet calls preserve the existing `PlayerbotAI` handlers but the mapping and ownership are independently implemented through `PlayerbotAIStorage` and `BotManager`; Penqle `Config::GetValues` is a generic core API over ACE configuration enumeration; static isolation is a generic per-static-module OBJECT-target mechanism.

Historical runtime evidence: fresh AI-enabled Docker runs emitted bot outgoing `SMSG_GROUP_INVITE`, master outgoing `SMSG_PARTY_COMMAND_RESULT`, and a synthetic master-incoming diagnostic, with existing group invite/accept success and cleanup. The same journey instantiated real `WarriorAiObjectContext`, `MageAiObjectContext`, `PriestAiObjectContext`, and `HunterAiObjectContext`; the Hunter no-network diagnostic used the existing `accept invitation` action directly after packet delivery because activity scheduling is intentionally not treated as human-client evidence. `Loading WorldBuffs` proves the real multi-value config reader. This evidence is retained for provenance, not final incoming-hook acceptance.

This checkpoint is historical and is superseded by the final correctness pass
below: the direct incoming diagnostic and Hunter direct-action diagnostic are
not final acceptance evidence.

Migration cleanup: removed `MinimalPlayerbotAI*`, `VerticalSlice*`, `cmangos-compat-shim.h.orig`, and all tracked `*.shyalya.bak` copies after retaining donor provenance above. The obsolete `BotController` has also been removed; `PlayerbotAI` is the sole gameplay update owner.

Intentional gaps only: random account/character auto-creation and the
post-Vanilla fishing wrapper remain outside the Vanilla/Turtle product
surface. A real human-client journey was not claimed from the automated
server-side packet fixture; the preserved runtime is left AI-enabled and
ready for manual client playtesting.

## Final pre-playtest correctness pass — 2026-08-24

Feature: movement-mode coherence, strict packet-trigger acceptance, human
master reconnect rebinding, controller cleanup, canonical random timing key,
and concrete PlayerbotAI value null-safety.

Source repository: TortoiseBots `phase4-follow@e0da302058cb1e5021c92272bf22983b2b5ad073`; required
Penqle core `playerbots-integration-gh@9487c5150a6553c665fafc1f4568669b8b00f011` (parent `133c6d19bf5898c1e4f5129b2890b1db89b17a07`, with `73f32c063e6c4481a0415690896025178ca8076f` as the original seam commit).

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

## Vanilla/Turtle source cleanup — 2026-08-24

Feature: subtractive product cleanup from the multi-expansion donor tree.

Source repository: TortoiseBots `cleanup/vanilla-turtle` working branch.

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
compatibility surface read as one Vanilla/Turtle product rather than a donor
tree hidden behind subtractive CMake filters.

Local validation: the persistent sibling builder's cached `BUILD_PLAYERBOTS=ON`
and complementary `BUILD_PLAYERBOTS=OFF` `mangosd` targets both compiled and
linked successfully. The ON wrapper's optional install step reports only that
`realmd` is not built; the `mangosd` artifact is produced. The existing Docker
stack was restarted from that artifact without an image rebuild or data reset;
the native module loaded and the world server reached ready. No manual
gameplay test was run, as requested.

## Surgical dead-code follow-up — 2026-08-24

Feature: residual Vanilla/Turtle cleanup after the main donor-tree removal.

Source repository: TortoiseBots `cleanup/vanilla-turtle`.

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

## Turtle audit closure pass — 2026-08-24/25

Feature: close the module-owned Turtle WoW 1.18.1 audit findings without
reintroducing core ownership coupling: effective configured SQL packaging, additive
schema repair, fail-closed startup caches, owner-input SQL safety, collection
mount lookup, later-expansion residue removal, and repeatable surface checks.

Source repository: TortoiseBots `audit/playerbots-turtle-1.18.1`

Source commit: `7e08fc810060e77839d4f38c813cc7eba9b05737` (final verified
implementation snapshot; the later provenance/docs update is documentation-only;
core-backed gossip/taxi/loot adapters, custom-start re-enable, talent validation
fixes, dead-shim cleanup, and native command fixture implementation; implementation `b76b5f4bf236b4d1bf370f0997e88cf30fd33695`, `fix: bound
engine action logging`), on top of `b863c6eedd3514a525f40e243bb8a61b2244fbe8` (`fix:
remove unreachable engine test logging`), `89a5e645e1485bd2e35b4944e88fdadfc6c95d05`
(`fix: remove remaining expansion-only item branches`), `887a6673675d06d716acc713aaeed8dca05d7e9f`
(`build: report native module source identity`), `9605a73c9bc16f0bf4fb4e84bba974a70f68c735`
(`fix: disable fish cache rebuild on startup`), `a6ea16605fde1b77e396ca588e0b34ddb1978bd5`
(`fix: align movement and channel shims with Turtle core`),
`7fa875a6c6bc51534b4a5a3f2f373f3dd7446208` (`fix: quarantine optional LLM
and stale tooling paths`), `2afd2d1` (`fix: match effective core SQL paths
and migration history`), and the preceding
`9db49df` and `3a96923` remediation commits.

Required target core: local Penqle `tortoise-wow`
`playerbots-integration-gh@9487c5150a6553c665fafc1f4568669b8b00f011`.

Source files: `TortoiseBots.cmake`, `README.md`,
`ai/playerbot/{PlayerbotAI,PlayerbotAIConfig,PlayerbotDbStore,PlayerbotFactory,RandomItemMgr,TravelMgr,TravelNode}.{cpp,h}`,
  the edited strategy/action/value/context files, `data/sql/{world,char}/*`,
`ai/playerbot/{ServerFacade.cpp,cmangos-compat-shim.h}`, the follow/movement
and range-trigger files, `ai/playerbot/aiplayerbot.conf.dist.in`,
`runtime/BotManager.cpp`, `conf/tortoise_bots.conf.dist`,
`tools/{analyze_quest_ledger.py,verify_turtle_surface.sh}`, and
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

- `tools/verify_turtle_surface.sh` passed.
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
  Penqle's `sScriptMgr` creature-gossip registry; it no longer unconditionally
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
- The real Turtle client was launched under Wine through normal and
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

## Final traced Turtle compatibility closure — 2026-08-25

Feature: close the remaining module-owned Turtle 1.18.1 compatibility
mismatches found by tracing active call sites against the pinned core: sparse
store bounds, core-defined custom races, path-filter fail-closed behavior,
native combat/interaction/auction/quest/skill semantics, later-ID cleanup,
localized names, factory class-spell initialization, native text-emote fallback,
loot status/roll state, and collection-mount caching.

Source repository: TortoiseBots `audit/playerbots-turtle-1.18.1`

Source commit: `d672048e86b9effc36210d3e6d076741fbeccc7f` (final source snapshot;
the initial traced implementation is `0f97403df42ee98b5085040a9a066ddc64608623`,
followed by `f594fc1` removing the unreachable fish-cache generator and
`d672048` closing the remaining active emote, loot, locale, spell-error, and
collection-mount fallbacks).

Reference repositories and commits:

- Local Penqle core: `tortoise-wow@9487c5150a6553c665fafc1f4568669b8b00f011`
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
`tools/verify_turtle_surface.sh`.

Copied / ported / independently reimplemented:

- Store upper bounds, DBC race-name loading, locale-map formatting, path
  fail-closed guards, native wrapper substitutions, and local heal prediction
  are independently reimplemented against the pinned core APIs.
- Class trainer/quest spell initialization is a module-local port of the
  existing CMaNGOS behavior, narrowed to the core's `Quest`, `TrainerSpell`,
  `SpellMgr`, talent, and class/race contracts. It does not add a
  `PlayerBots`-specific core hook.
- Absent expansion IDs and invalid Turtle branches are subtractive cleanup,
  validated against local DBC/SQL; no expansion behavior was introduced.

Reason: preserve Existing Vanilla behavior while ensuring that Turtle custom
IDs, Goblin/High Elf data, localized content, and native core semantics are
not silently hidden behind donor-era constants or no-op compatibility methods.

Local validation: `tools/verify_turtle_surface.sh`, `git diff --check`, and the
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

Feature: make `tools/verify_turtle_surface.sh` fail closed when its required
ripgrep dependency is unavailable, so the surface/audit gate cannot report a
false success after `rg` returns command-not-found.

Source repository: TortoiseBots `audit/playerbots-turtle-1.18.1`

Source commit: `9e9567c996d1cbf5c2c3f5949453499589600d4e` (implementation
commit; the subsequent audit/provenance edit is documentation-only).

Source files: `tools/verify_turtle_surface.sh`.

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
  `env -i PATH=/tmp/tortoisewow-no-ripgrep /bin/bash tools/verify_turtle_surface.sh`
  exited `1` and printed
  `TortoiseBots surface check failed: ripgrep (rg) is required to verify the
  Turtle module surface`; it did not print `Tortoise WoW 1.18.1 module surface:
  OK`.
- Full verifier with ripgrep available:
  `bash tools/verify_turtle_surface.sh` exited `0` and printed
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
and reconcile the locally provable Turtle ScriptName mismatches.

Source repository: local Penqle `tortoise-wow` core plus its local Turtle SQL;
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
  Turtle names were deliberately not implemented because their behavior is not
  established by the pinned core/history.

Reason: keep Tortoise core generic and optional, make TortoiseBots the only
supported PlayerBots implementation, and avoid converting missing Turtle
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
