# PROVENANCE — TortoiseBots behavior harvesting log

Per `PLAN.md` §5 and `AGENTS.md` Provenance: record every substantial port for license/attribution.

| Feature | Source project | Source commit | Source files | Ported / reimplemented | Reason | Local validation |
| --- | --- | --- | --- | --- | ---: | --- |
| SessionTransport Headless pattern (`SessionTransport::Headless`, `IsHeadless()`, `HasNetworkTransport()`, `InitHeadlessSession()` with `NullSessionAnticheat`) | `Shyalya/tortoise-wow` (vendored `cmangos/playerbots` via `r-o-sh/tortoise-wow:playerbots-integration-gh`) and `Penqle/tortoise-wow` core's existing `NullSessionAnticheat` | `shyalya-tortoise-wow@1f9497e` (checkpoint `0af2567` 2026-05-10, vendored `cmangos/playerbots@c33dfac`) / `Anticheat/Anticheat.h:143` (`NullSessionAnticheat`) | `Anticheat/Anticheat.h:143-209` (`NullSessionAnticheat`, `NullAnticheatLib`), `shyalya` host hooks for bot sessions (80-file surface) | Reimplemented as generic transport enum, not copied — core now distinguishes `Network` vs `Headless` via `SessionTransport`, module interprets `IsHeadless() == true` as bot. `InitHeadlessSession()` directly assigns `NullSessionAnticheat` (existing core precedent) | Harvest the null-transport precedent without inheriting Shyalya's 80-file host surface; keep host integration ≤5 files | `rg` audits for `GetBot/m_bot` remain clean; `BUILD_PLAYERBOTS=ON/OFF` matrix builds; Docker runtime spike: headless session survives `World::Update` (not deleted as disconnected), `HandlePlayerLogin` succeeds after queued `AddSession` + deferred `LoginPlayer`, bot enters world and re-enters after logout |
| `IWorldUpdateListener` generic world-tick registry (`RegisterWorldUpdateListener`, `GetPendingWorldListenerFactories`, `RegisterPendingWorldListeners` in `World::Update`) | `HardcodedEvents`/`ZoneScriptMgr::Update` pattern in `Penqle/tortoise-wow` + `DiscordBot::RegisterHandlers` precedent in same core | `World.cpp:2448` (`World::Update`), `HardcodedEvents.h`, `ZoneScriptMgr.cpp:117` (`Update`), `World.cpp:2343` (`DiscordBot::RegisterHandlers`) | `World.h:889`, `World.cpp:2448`, `HardcodedEvents.h`, `ZoneScriptMgr` | Reimplemented as generic `IWorldUpdateListener` with explicit registration and pending-factory static initializers (no weak symbols, no `sBotHost` global) — inspirited by `DiscordBot`'s service registration but made generic | Bot AI must run on the world thread once per tick; no existing `WorldScript::OnUpdate` exists in MaNGOS `ScriptMgr`, so a single generic call site in `World::Update` is the correct seam | `World::Update` now calls listeners after `UpdateSessions`; `BotHostAdapter` receives tick, `BotManager` drives lifecycle; `rg -i PlayerBot` in `src/game` only shows `InitHeadlessSession` bridge |
| `BUILD_PLAYERBOTS` optional-module CMake wiring | `cmangos/mangos-classic` (`cmake/options.cmake: BUILD_PLAYERBOTS OFF`) and `mangoszero/server` (`CMakeLists.txt: PLAYERBOTS OFF`) | `cmangos-mangos-classic@9b682be`, `mangoszero-server@1817ae1` | `CMakeLists.txt`, `src/CMakeLists.txt`, `src/game/CMakeLists.txt`, `src/mangosd/CMakeLists.txt` | Reimplemented as explicit `option(BUILD_PLAYERBOTS OFF)` with `add_subdirectory(modules/TortoiseBots)` only when `ON`, and `target_link_libraries(mangosd tortoise_bots)` via `CMP0079` + `whole-archive` on Linux — no `FetchContent` auto-download, no `ENABLE_PLAYERBOTS` scattered defines | Keep `BUILD_PLAYERBOTS=OFF` first-class and `src/modules/TortoiseBots` absent/present matrix clean; harvest the option pattern without the `FetchContent` auto-clone | Matrix: `absent+OFF` OK, `present+OFF` OK (module present but not built), `present+ON` OK (module linked); `rg` audits clean |
| Headless queued-session lifecycle (`WorldSession::Update`, `CharacterScreenIdleKick`, queued add/remove) | `Shyalya`'s `NullSessionAnticheat` + `WorldSession::Update` null-socket handling (`WorldSession.cpp:163,383,736` already tolerates `m_Socket==nullptr` but deletes headless via `return false`) | `WorldSession.cpp:306,334,378`, `Handlers/CharacterHandler.cpp:548`, `World.cpp:283`, `LockedQueue.h` | Reimplemented: explicit `SessionTransport`, a one-pass `m_headlessLoginPending` keepalive, deferred `LoginPlayer` after queued `AddSession`, and generic pending-session inspection/cancellation. `BotManager` retains `Removing` records until cleanup. | The queued path must survive the first `UpdateSessions` pass without requiring synchronous insertion; immediate removal must cancel the queue entry instead of orphaning it. | Queued runtime spike passed; `PendingAddRemoveTest PASSED` with no active/pending session, player, or record; graceful shutdown clears both online flags. |

| Foundational Engine/AiObjectContext/Strategy/Trigger/Action/Value/ReactionEngine (Tortoise 1.18.1 baseline) | `Shyalya/tortoise-wow` (`playerbots-integration-gh` @ 1f9497e, vendored `cmangos/playerbots@c33dfac`) | `Shyalya` baseline provides the full `playerbot/strategy/Engine.{h,cpp}`, `AiObjectContext.{h,cpp}`, `AiObject.{h,cpp}`, `Strategy.{h,cpp}`, `Trigger.{h,cpp}`, `Action.{h,cpp}`, `Value.{h,cpp}`, `ReactionEngine.{h,cpp}`, `Queue/Event/Multiplier` etc, already translated for `MANGOSBOT_ZERO` (Vanilla 1.12/1.18.1) and `Penqle`'s `WorldLocation`/`Position`/`Map` APIs | `ai/playerbot/strategy/Engine.*`, `AiObjectContext.*`, `AiObject.*`, `Strategy.*`, `Trigger.*`, `Action.*`, `Value.*`, `ReactionEngine.*`, `Queue.*`, `Event.*`, `AiObject.*` (full `ai/playerbot` tree, 82 top-level + 14 strategy core + 113 generic) | Copied verbatim as the Tortoise/Vanilla translation reference for the foundational runtime; `cmangos-compat-shim.h` and `botpch.h` already handle `Penqle` `SpellEntry`/`ItemPrototype`/`MapStorage` translation, Vanilla `MANGOSBOT_ZERO` guards exclude `deathknight`/`TBC`/`WotLK` paths | Shyalya's `playerbots-integration-gh` is the only proven `1.18.1` PlayerBots that already runs on `Penqle`'s `WorldLocation` (`mapId/x/y/z/o`), `Transport`/`GenericTransport`, `GuidSet`/`AreaTableEntry` etc; using it as the baseline avoids reinventing `Penqle` API translation and keeps `Headless`/`IsHeadless()` as the only host seam | `ai/` now contains the full Shyalya `playerbot` tree (204 generic files after modern layer); `CMakeLists.txt` now builds the real `Engine`/`AiObjectContext`/`Strategy` stack instead of the stub `EngineStub`/`AiObjectContextStub`; native linkage uses explicit `MODULE_TORTOISEBOTS=static` and `MANGOSBOT_ZERO` |
| Modern generic Base behavior (204 files, `Follow`/`Combat`/`Dead`/`Ranged`/`Melee` etc) | `mod-playerbots` `src/Ai/Base/Strategy` @ 5397110cba48 (merge #2661) + `Shyalya` `strategy/generic` @ 1f9497e | `mod-playerbots: src/Ai/Base/Strategy/*.{h,cpp}` (91 files, modern `FollowMasterStrategy` now `getDefaultActions()` + `InitTriggers` vs Shyalya's `InitNonCombatTriggers`/`InitCombatTriggers` + `NextAction::array`) / `shyalya: strategy/generic/*` (113 files, includes `BlackwingLair`/`Karazhan` dungeon strategies) | `ai/playerbot/strategy/generic/*` (204 files after modern layer; donor SHAs are retained here instead of backup copies) | Forward-ported the modern `mod-playerbots` generic set on top of Shyalya's Tortoise-translated generic; Vanilla/Tortoise `MANGOSBOT_ZERO` guards kept and expansion-only paths excluded | Modern class/combat/follow behavior remains attributable to the pinned donor commits; obsolete `*.shyalya.bak` copies were removed once this provenance record was complete |
| Modern class factory + per-class Strategy subfolders (9 Vanilla classes) | `mod-playerbots` `src/Ai/Class/{Warrior,Mage,Priest,Druid,Hunter,Rogue,Paladin,Shaman,Warlock}/AiObjectContext.*` + `Strategy/*.{h,cpp}` @ 5397110 + `Shyalya` `strategy/{warrior,mage,priest,...}/` @ 1f9497e | `mod-playerbots: src/Ai/Class/Warrior/{WarriorAiObjectContext.*,Strategy/*.cpp}` etc / `shyalya: strategy/warrior/{WarriorAiObjectContext.*,Arms/Fury/Protection/Tank...Strategy}` etc | `ai/playerbot/strategy/{warrior,mage,priest,druid,hunter,rogue,paladin,shaman,warlock}/*` | Forward-ported the nine Vanilla class contexts and strategy families; `AiFactory` now instantiates each native class context rather than silently falling back to the generic context | Fresh runtime attached real Warrior, Mage, Priest, and Hunter contexts; Warrior/Mage/Priest completed packet group journeys and Hunter completed via the deterministic mature-action diagnostic path |

Notes (updated 2026-08-22 — large-batch forward-port):

- Foundational runtime is now substantially real (not a stub): `ai/playerbot` contains the full Shyalya `playerbot` tree (Tortoise `1.18.1` translation, `MANGOSBOT_ZERO`) plus the modern `mod-playerbots@5397110` generic and nine class contexts. The obsolete `*.shyalya.bak` and `.orig` migration copies were removed after donor SHAs and source paths were recorded above. `CMakeLists.txt` builds the real `Engine`/`AiObjectContext`/`Strategy`/`Trigger`/`Action`/`Value`/`ReactionEngine`/`AiFactory` stack; `deathknight`/`WotLK` remains excluded via `MANGOSBOT_ZERO`.
- Host seam remains generic and minimal: `SessionTransport`, `IsHeadless()`, `HasNetworkTransport()`, `IWorldUpdateListener` (≤5 host files, verified via `rg -n -i 'PlayerBot|BotService|Headless' src/game` only shows `InitHeadlessSession`). No `IsBot()`/`GetBot()`/`m_bot`/`sPlayerBotMgr` reintroduced. Same-account `1 Network + N Headless` GUID-driven lifecycle and human reclaim are preserved via `BotManager` + `BotSessionAdapter`.
- Upstream licenses (GPL-2.0 for MaNGOS/CMaNGOS/Shyalya, GPL-2.0 for `mod-playerbots`) are preserved; headers retain original copyright/license and this file records donor SHAs/source files before migration copies were deleted. No `AzerothCore` `PlayerbotMgr`/`BotSession` ownership model is reintroduced.
- The broad donor tree is now wired into the active CMake source set: real `PlayerbotAI`/`AiFactory`, generic behavior, all nine Vanilla class folders, Value/Trigger/Action families, Travel, grouping, loot, quests, dungeon/raid and PvP families are compiled as one coherent batch rather than left dead in-tree. `MANGOSBOT_ZERO` filters expansion-only folders. Fresh runtime probes now cover the packet bridge and Warrior/Mage/Priest/Hunter class attachment/group journeys.
| Follow (dead-zone 1.5y, MoveFollow behind M_PI, restart guard same target/dist/angle, CanFollow guards) | `cmangos/playerbots` + `mangoszero/server` | `cmangos-playerbots@076045e` / `mangoszero-server@1817ae1` | `cmangos: playerbot/strategy/actions/FollowActions.cpp:36-90`; `mangoszero: src/modules/Bots/playerbot/strategy/actions/MovementActions.cpp:440-560` | The real PlayerbotAI path uses `FollowMasterStrategy`/`FollowAction`; `BotController` retains only an intent/diagnostic record and is never a gameplay fallback after AI attachment | Keep 1.5y jitter-free follow without a second movement owner | Cached ON/static build and fresh AI-enabled runtime packet journeys passed without controller fallback execution |
| Warrior vertical slice | Shyalya `playerbots-integration-gh` + modern `mod-playerbots` | `1f9497e` / `5397110` | Focused `Engine`/`Queue`/`Trigger`/`Action`/`Value` primitives, generic `FollowMasterStrategy`/assist/combat/non-combat/dead strategies, and Warrior Arms/Fury/Protection strategy files | Ported/adapted focused family; unrelated expansion systems remain excluded by the CMake source set | Make one owned Tortoise bot use strategy-driven follow and combat before widening the donor tree | Cached Docker build: `Built target tortoise_bots`, `Built target mangosd`; runtime: same-account Headless Sagiroth + Dudette, `dps assist`, successful Warrior Heroic Strike spell 78, encounter end/follow resume, clean removal; `PlayerbotAIStorage` logout use-after-free fixed and revalidated |
| Broad Vanilla/Turtle source-set checkpoint | Shyalya `playerbots-integration-gh` + modern `mod-playerbots` | `1f9497e` / `5397110` | `CMakeLists.txt` globs the real `PlayerbotAI`, generic, nine Vanilla class, Value/Trigger/Action, Travel, grouping, loot, quest, dungeon/raid, BG/PvP and economy families; `MANGOSBOT_ZERO` excludes Death Knight and other expansion-only paths | Adapted Penqle naming and data shapes in the module-local compatibility layer; native loot ownership, area names/flags, channel wrappers and const loot-list views replace unsafe CMaNGOS member assumptions | Compile and stabilize the broad family without adding core `GetBot`/`m_bot` ownership | The first broad Docker pass reached the module compilation stage and exposed a small remaining Penqle API family in `PlayerbotAI.cpp`; the correct Penqle `module-system` host snapshot must also carry the documented generic Headless/session seams before a broad runtime claim is made |

## Native module-system checkpoint — 2026-08-24

Feature: Native module packaging, module-local runtime support, and broad Vanilla/Turtle source selection

Source repository: local Penqle `tortoise-wow` sibling plus local TortoiseBots checkout

Source commit: core `73f32c063e6c4481a0415690896025178ca8076f` on branch `playerbots-integration-gh`; TortoiseBots `3484208` (`Finish native PlayerBots integration and playtest bridge`). This checkpoint is superseded by core `9487c5150a6553c665fafc1f4568669b8b00f011`; the core commits `133c6d19` and `9487c515` keep static-module include paths target-local and remove the stale `src/game/PlayerBots` common path.

Source files: `TortoiseBots.cmake`, `src/TortoiseBotsModule.cpp`, `host/*`, `runtime/*`, `ai/playerbot/*`, `strategy/{generic,druid,hunter,mage,paladin,priest,rogue,shaman,warlock,warrior}/*`, `strategy/{actions,triggers,values}/*`

Copied / ported / independently reimplemented: behavior was ported/adapted from local `shyalya-tortoise-wow@1f9497e0f42bfc1055841bb6ebdc7caa3515de0b`, `cmangos-playerbots@076045efa835da9aab7caa943bca752aebe1baad`, and `mod-playerbots@5397110cba484a9b7209bc9f632652e9d4bd6a70`; host lifecycle and BotManager ownership were independently reimplemented around generic `SessionTransport`/Headless APIs.

Reason: use mature combat/class/travel behavior without compiling donor manager, random-manager, login-manager, or second-session ownership into the target core.

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
- Random-bot buy/sell multipliers are now cached per character with the mature Vanilla ranges, and named-location lookup uses the native `ai_playerbot_named_location` table instead of a compatibility no-op.
- Empty optional item/equipment caches are accepted without synchronous world-thread cache generation. Populated mature caches still load normally.
- Schema-only native migrations cover the tables queried by the active Vanilla/Turtle AI initializer and per-bot state. Mature datasets remain deployable separately.

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
Turtle battleground entries; the mature value compatibility view reads the
same native `battlemaster_entry` table once at AI startup and does not own
battleground state.

## Packet/config/static-isolation and playtest gate — 2026-08-24

Feature: generic packet/event bridge, safe multi-value configuration, isolated native static-module settings, mature command surface, random-bot debt cleanup, and fresh class journeys.

Source repository: TortoiseBots `phase4-follow@3484208`; required Penqle core `playerbots-integration-gh@9487c5150a6553c665fafc1f4568669b8b00f011` (parent `133c6d19bf5898c1e4f5129b2890b1db89b17a07`).

Source files: `host/BotPacketAdapter.*`, `runtime/BotManager.*`, `runtime/PlayerbotAIAdapter.cpp`, `runtime/PlayerbotRuntimeFacade.cpp`, `runtime/RandomBotService.*`, `commands/BotCommands.cpp`, `ai/playerbot/PlayerbotAIConfig.*`, `ai/playerbot/strategy/ValueMacros.h`, `ai/playerbot/AiFactory.cpp`, `TortoiseBots.cmake`, `conf/tortoise_bots.conf.dist`, and Penqle `modules/CMakeLists.txt`/`Config`.

Copied / ported / independently reimplemented: the packet calls preserve the mature `PlayerbotAI` handlers but the mapping and ownership are independently implemented through `PlayerbotAIStorage` and `BotManager`; Penqle `Config::GetValues` is a generic core API over ACE configuration enumeration; static isolation is a generic per-static-module OBJECT-target mechanism.

Historical runtime evidence: fresh AI-enabled Docker runs emitted bot outgoing `SMSG_GROUP_INVITE`, master outgoing `SMSG_PARTY_COMMAND_RESULT`, and a synthetic master-incoming diagnostic, with mature group invite/accept success and cleanup. The same journey instantiated real `WarriorAiObjectContext`, `MageAiObjectContext`, `PriestAiObjectContext`, and `HunterAiObjectContext`; the Hunter no-network diagnostic used the mature `accept invitation` action directly after packet delivery because activity scheduling is intentionally not treated as human-client evidence. `Loading WorldBuffs` proves the real multi-value config reader. This evidence is retained for provenance, not final incoming-hook acceptance.

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
existing mature `FollowMasterStrategy`/`ChatShortcutActions` semantics; the
strict fixture is an independent runtime assertion over the existing packet
bridge; the null-safety changes are local defensive corrections.

Local validation: cached `BUILD_PLAYERBOTS=ON`, static native `mangosd` passed;
cached `BUILD_PLAYERBOTS=OFF`, `BUILD_LEGACY_PLAYERBOTS=OFF`, and
`MODULES=disabled` `mangosd` passed. The final AI-enabled Docker binary reached
world-ready, and the strict packet fixture passed automatic mature group
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

Feature: mature `.bot stay` anchors, mature-AI-authoritative reconnect,
durable random-bot master bind/clear, and corrected packet-fixture wording.

Source files: `commands/BotCommands.cpp`, `runtime/BotManager.{h,cpp}`,
`runtime/PlayerbotAIAdapter.{h,cpp}`, `ai/playerbot/PlayerbotAI.{h,cpp}`,
`ai/playerbot/strategy/actions/AcceptInvitationAction.h`,
`ai/playerbot/strategy/actions/LeaveGroupAction.cpp`,
`ai/playerbot/strategy/actions/BattleGroundJoinAction.cpp`,
`ai/playerbot/strategy/actions/BattleGroundTactics.cpp`, and the current
status/host-boundary documentation.

Copied / ported / independently reimplemented: native commands now reuse the
existing mature `StayChatShortcutAction`/`FollowChatShortcutAction` actions;
reconnect treats the existing mature strategy set as authoritative and only
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
`host/{BotHostAdapter,BotSessionAdapter}.cpp`, mature ownership-transition
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

Source commit: `7fa875a6c6bc51534b4a5a3f2f373f3dd7446208` (`fix: quarantine
optional LLM and stale tooling paths`), on top of `2afd2d1` (`fix: match
effective core SQL paths and migration history`) and the preceding `9db49df`
and `3a96923` remediation commits.

Required target core: local Penqle `tortoise-wow`
`playerbots-integration-gh@9487c5150a6553c665fafc1f4568669b8b00f011`.

Source files: `TortoiseBots.cmake`, `README.md`,
`ai/playerbot/{PlayerbotAI,PlayerbotAIConfig,PlayerbotDbStore,PlayerbotFactory,RandomItemMgr,TravelMgr,TravelNode}.{cpp,h}`,
  the edited strategy/action/value/context files, `data/sql/{world,char}/*`,
`ai/playerbot/aiplayerbot.conf.dist.in`, `tools/analyze_quest_ledger.py`,
`tools/verify_turtle_surface.sh`, and
`docs/PLAYERBOTS_AUDIT.md`.

Copied / ported / independently reimplemented:

- No new upstream gameplay was copied in this pass.
- Existing mature behavior was kept where the local core data validates it;
  the factory collection-mount selection is an independent module adapter over
  the core `collection_mount` table and `MountManager` contract.
- Removed RTSC/SeeSpell/BossAura and later-ID branches are subtractive cleanup,
  not replacements with expansion behavior.
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
- The real Turtle client was launched under Wine through normal and
  software-forced rendering paths; both rendered black with no observable
  login UI in this environment, so no real-client `.bot` command journey is
  claimed.
