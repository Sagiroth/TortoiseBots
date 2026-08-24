# HOST_API — Tortoise WoW 1.18.1 Host Boundary Discovery (Phase 1)

**Date:** 2026-08-24
**Target core:** `Penqle/tortoise-wow` (online: <https://github.com/Penqle/tortoise-wow>) — snapshot packaged in a local `tortoise-docker-penqle/source` checkout (clean after PR #396)
**Docker wrapper commit:** `d07ec3fe8fd5` / `9310e37 Tortoise WoW (Penqle, no-bots)` — local Docker packaging
**Plan ref:** `docs/PLAN.md` (online: <https://github.com/tortoise-wow-stack/TortoiseBots/blob/main/docs/PLAN.md>) §9 / §26 — Phase 1 Host-boundary discovery
**Core source root inspected:** local `tortoise-docker-penqle/source` checkout if present (online canonical: <https://github.com/Penqle/tortoise-wow>)
**Reference checkouts (if present locally):** `playerbots-references/{cmangos-playerbots@076045e, mangoszero-server@1817ae1, shyalya-tortoise-wow@1f9497e, cmangos-mangos-classic@9b682be}` (online: <https://github.com/cmangos/playerbots>, <https://github.com/mangoszero/server>, <https://github.com/Shyalya/tortoise-wow>, <https://github.com/cmangos/mangos-classic>)

> Goal: enumerate the **minimum events/capabilities the PlayerBots module actually needs** from the core, and identify where an existing general-purpose hook suffices vs where a genuinely new centralized seam is required. Design is based on the **actual current source tree**, not assumptions from CMaNGOS/MangosZero/Shyalya.

> Current implementation note: the historical Phase 1 inventory below is
> retained for provenance. The reproducible core seam used by the current
> native module is `tortoise-wow` branch `playerbots-integration-gh` at
> `9487c5150a6553c665fafc1f4568669b8b00f011` (`Drop stale PlayerBots include
> path from module defaults`), based on `133c6d19bf5898c1e4f5129b2890b1db89b17a07`.

---

## 0. Baseline — Clean Canvas Verification

PR #396 deletes legacy `src/game/PlayerBots`. Verification on this snapshot:

```bash
rg -n 'GetBot\(\)|SetBot\(|\bm_bot\b|sPlayerBotMgr|PlayerBotEntry|PB_STATE_' src
# → no matches (legitimate PlayerAI / PlayerControlledAI remain)
rg -n 'BUILD_PLAYERBOTS|AiPlayerbot' source
# → no matches
```

**Result:** Clean. `PlayerAI` (`src/game/AI/PlayerAI.h:36`) and `PlayerControlledAI` (`AI/PlayerAI.h:58`) remain — these are legitimate mind-control/charm AI, not PlayerBots. No `BUILD_PLAYERBOTS` CMake option exists yet. This is the desired baseline per `PLAN.md` §8.

**Filesystem note (flattened 2026-08-20):** Per user request the former `docs/TortoiseBots/` nesting was flattened to just `docs/` (and the earlier `docs/playerbots/` duplication removed). `DISCOVERY.md` now lives alongside the active specs (`HOST_API.md`, `BASELINE.md`, etc.) as a clearly-marked historical archive. Also cleaned: stray `/playerbots/` line and `missi/playerbots/ng` typo in `AGENTS.md`.

---

## 1. Existing Extension / Script Infrastructure Inventory

### 1.1 What *does* exist — legacy MaNGOS script system

Tortoise is **not** TrinityCore/AzerothCore-style with `WorldScript`/`PlayerScript`/`AccountScript`/`GuildScript` registries. It retains the older MaNGOS `ScriptMgr` ( `src/game/ScriptMgr.h` 1654 lines, `ScriptMgr.cpp` 2914 lines):

| Facility | File | What it provides |
| --- | --- | --- |
| `struct Script` | `ScriptMgr.h:1449` | Old DB-script bindings: `pGossipHello`, `pQuestAcceptNPC`, `pItemUse`, `pEffectDummyCreature`, `GOGetAI`, `GetAI`, `GetInstanceData`, `GetSpellScript`, `GetAuraScript`, etc. Registered via `Script::RegisterSelf()` and `sScriptMgr.Initialize()` (`World.cpp:2180`). |
| `ScriptMapMap` DB scripts | `ScriptMgr.h:1120` | `sQuestEndScripts`, `sQuestStartScripts`, `sSpellScripts`, `sEventScripts`, `sGossipScripts`, `sCreatureMovementScripts` — loaded from DB tables, not C++ hooks. |
| `SpellScript` / `AuraScript` | `ScriptMgr.h:1380,1417` | Per-spell/auras hooks (`OnCheckCast`, `OnEffectExecute`, `OnAbsorb`, `OnPeriodicTick`, etc.). Useful for Turtle spell shims later, not for bot lifecycle. |
| `WorldSessionScript` | `WorldSession.h:278` | **Only session-level extensibility point:** virtual `OnWardenData`, `OnUnitKilled`, `OnLoot`, `OnPacket(uint32 opcode)`, `OnSpellCasted`, `OnLogin(Player const*)`, `OnWhispered`, `OnQuestKillUpdated`. Stored in `WorldSession::scripts` map (`WorldSession.h:586`) and invoked via `ALL_SESSION_SCRIPTS(this, OnLogin(pCurr))` (`Handlers/CharacterHandler.cpp:1005`). Added via `WorldSession::AddScript(name, script*)` (`WorldSession.h:575`). |
| `HardcodedEvents` / `WorldEvent` | `HardcodedEvents.h` | Invasion, Leprithus etc — `WorldEvent::Update()`. Not relevant for bots, but shows a pattern for world-tick events. |

**Absent:** No `PlayerScript::OnLogin/OnLogout`, no `WorldScript::OnUpdate`, no `GroupScript`, `GuildScript`, `MapScript`, `ChatScript`, command-registration hook, or module loader. Any modern “script hook” expectation must be treated as *missing*.

### 1.2 World / Session / Player lifecycle paths (actual code)

| Path | File:Line | Notes |
| --- | --- | --- |
| **Socket accept → session** | `Protocol/WorldSocket.cpp:382` (`HandleAuthSession`) | `ACE_NEW_RETURN(m_Session, WorldSession(id, this, ...), -1)` then `sWorld.AddSession(m_Session)` (`:370`). `WorldSession` constructor is `WorldSession(uint32 id, WorldSocket *sock, ...)` (`WorldSession.h:308`, `WorldSession.cpp:83`). The `sock` pointer is stored as `m_Socket` (`WorldSession.h:991`). |
| **Session map** | `World.h:905-906, World.cpp:283,288` | `SessionMap m_sessions` + `LockedQueue<WorldSession*> addSessQueue`. `World::AddSession` defers to `AddSession_(s)` in `World::UpdateSessions`. |
| **Auth & net-free checks** | `WorldSession.cpp:163,214,342,367,383,736` | `SendPacket` early-outs if `m_Socket==nullptr` (163). `Update` checks `!m_Socket` (383) and `IsClosed`. `LogoutPlayer` has explicit `if (_player->GetGroup() && !_player->GetGroup()->isRaidGroup() && m_Socket)` (736) — null-socket already partially handled. |
| **Character login** | `Handlers/CharacterHandler.cpp:490,548,561` | `HandlePlayerLoginOpcode` → `new LoginQueryHolder` → `CharacterDatabase.DelayQueryHolderUnsafe(&chrHandler, &CharacterHandler::HandlePlayerLoginCallback, holder)` → `WorldSession::HandlePlayerLogin(holder)` → `pCurrChar->GetMap()->ExistingPlayerLogin(pCurrChar)` + `OnLogin` scripts (1005). |
| **DB holder** | `Handlers/CharacterHandler.cpp:61,85` | `LoginQueryHolder::Initialize()` loads character, `QueryResult` reuse. Same holder reused by `WorldSession::LoginPlayer(ObjectGuid)` (548) for headless reuse. |
| **Logout / save** | `WorldSession.cpp:572,572-800` | `LogoutPlayer(bool Save)` → `SaveToDB`, `RemovePet`, `LeaveBattleground`, `TeleportToHomebind` if invalid instance, `UninviteFromGroup`, `group->UpdatePlayerOnlineStatus(...,false)`, `Map::Remove`, `CleanupsBeforeDelete`, `Map::DeleteFromWorld`, `SetPlayer(nullptr)`. |
| **World tick** | `World.cpp:2448,3293` | `World::Update(diff)` → timers → `sMapMgr.Update(diff)` → `UpdateSessions(diff)` (2490) → each `WorldSession::Update(updater)` → `sMapMgr` map/player updates. No external hook; `HardcodedEvents` and `sGameEventMgr.Update()` are called inside `World::Update`. |
| **Player tick** | `Objects/Player.cpp:1566` | `Player::Update(uint32 update_diff, uint32 p_time)` called from `Map::Update`. Where bot AI would naturally be driven (once per map tick). |
| **Map add/remove** | `Maps/Map.cpp:1124,1645` | `Map::Remove(Player*, bool)` and `DungeonMap::Remove`. `ObjectAccessor::FindPlayer*` family (`ObjectAccessor.h:106`). |
| **Chat** | `Handlers/ChatHandler.cpp:100, Handles/ChatHandler.cpp` | `WorldSession::HandleMessagechatOpcode` → `ChatHandler::HandleChat`. Chat commands via static `ChatHandler::getCommandTable()` (`Chat/Chat.cpp:42`) — a monolithic `ChatCommand` array. No `AddCommand` registration API. Addon messages via `Player::SendAddonMessage` (`Player.cpp:25414`) using `CHAT_MSG_GUILD` + `TW_CHAT_MSG_WHISPER` prefix hack. |
| **Group / Loot** | `Group/Group.cpp:121,357,438,516,1351,1418,1435` | `Group::Create`, `AddMember`, `RemoveMember`, `ChangeLeader`, `SendUpdate`, `UpdatePlayerOutOfRange`, `UpdatePlayerOnlineStatus`. `Group::GroupLoot`/`NeedBeforeGreed`/`MasterLoot`. No script callbacks. |
| **Anticheat seam** | `Anticheat/Anticheat.h:80,143` | `SessionAnticheatInterface` + `NullSessionAnticheat` — already a **headless/null transport precedent**. `WorldSession::InitAntiCheatSession(&K)` (`WorldSocket.cpp:338`) creates a real `SessionAnticheat`; `NullSessionAnticheat` is the fallback with no-ops for `Movement`, `Warden`, `ReadAddonInfo`, etc. Bots should reuse `NullSessionAnticheat`. |
| **DiscordBot precedent** | `DiscordBot/*`, `World.cpp:2343,2348` | Shows an *existing* non-player system that registers handlers (`DiscordBot::RegisterHandlers()`) and has `sDiscordBot->Setup(token)`. Not a player session, but demonstrates a *separate service* can be started from `World::SetInitialWorldSettings` without polluting `Player.cpp`. |

---

## 2. Host Capability Table

Per `PLAN.md` §9 — columns: need → existing general hook? → new core seam required? → rationale.

| # | Need | Existing hook? | New seam? | Why / Evidence |
| --- | --- | --- | :-: | --- |
| 1 | **World tick** (`diff` each server frame) | **No** — `World::Update(diff)` (`World.cpp:2448`) is closed. No `WorldScript::OnUpdate` registry. `WorldSessionScript` has no world-tick callback. | **Yes — single centralized hook** | Bot AI must run once per tick (follow, combat, threat). Options: (a) add one `if (sBotMgr) sBotMgr->Update(diff)` call inside `World::Update` (after `UpdateSessions`), or (b) reuse `HardcodedEvents`-like `WorldEvent` — but a direct call is clearer and less invasive. No need for per-player hook. |
| 2 | **Player login** (human or bot character enters world) | **Partial** — `WorldSession::HandlePlayerLogin(LoginQueryHolder*)` (`CharacterHandler.cpp:561`) exists and already calls `ALL_SESSION_SCRIPTS(this, OnLogin(pCurr))` (`:1005`). `LoginQueryHolder::Initialize()` is reusable. | **No for notification; Yes for bot creation** | For *observing* login, `WorldSessionScript::OnLogin` is sufficient. For *creating* a bot login without a socket, need a headless factory (see #6). No new hook for observation. |
| 3 | **Player logout / save / reload** | **Partial** — `WorldSession::LogoutPlayer(bool Save)` (`WorldSession.cpp:572`) does full save/cleanup. `WorldSessionScript` has **no** `OnLogout`. | **Optional — reuse or add `OnLogout`** | Module can track logout by owning the headless session lifecycle itself. Adding a symmetric `OnLogout` to `WorldSessionScript` is cheap (one line in `LogoutPlayer`) and useful for diagnostics, but not strictly required for MVP — bot logout is initiated by the module (`BotManager::LogoutBot`). |
| 4 | **AddToWorld / RemoveFromWorld** | **No** dedicated script. `Map::Add`/`Remove` (`Map.cpp:1124`) and `Player::AddToWorld` semantics are internal. | **No** | Module does not need to intercept `AddToWorld`. Bot login already goes through `HandlePlayerLogin` → `Map::ExistingPlayerLogin`. Bot-specific post-enter logic (follow, formation) lives in module after `OnLogin`. |
| 5 | **Player Update** (`Player::Update(diff)`) | **No** hook. `Player.cpp:1566` is called from `Map::Update`. No `PlayerScript::OnUpdate`. | **No** | Do not hook `Player::Update`. Instead drive bot decisions from the *single* world-tick hook (#1) through `PlayerbotAIAdapter::Update(diff)`. `BotController` is intent/diagnostic state only and has no gameplay update loop. |
| 6 | **Headless / synthetic session** (bot `Player` without a real `WorldSocket`) | **No seam, but partial tolerance** — `WorldSession` already tolerates `m_Socket==nullptr` in `SendPacket` (163), `Update` (383), `LogoutPlayer` (736). `NullSessionAnticheat` (`Anticheat.h:143`) is an existing null-transport precedent. `WorldSession` constructor still requires `WorldSocket*` (`WorldSession.h:308`). | **Yes — the key host seam (1 file + 1 factory)** | This is the *only* non-negotiable new core capability (PLAN Rule 5). Design below (§3). Must avoid `WorldSession::GetBot()` / `m_bot`. Prefer generic `HasNetworkTransport()` / `CanReceiveClientPackets()` (`WorldSession::GetSocket()==nullptr`) and a centralized `CreateHeadlessSession(...)` factory that creates a `WorldSession` with `nullptr` socket, `NullSessionAnticheat`, and null broadcaster. All bot-specific meaning stays in module. |
| 7 | **Chat / command execution** (`.bot add / follow / whisper`) | **Partial** — `WorldSessionScript::OnPacket` and `OnWhispered` exist, but `ChatHandler::getCommandTable()` (`Chat.cpp:42`) is monolithic with no `AddCommand` registry. Chat opcode handling is in `Handlers/ChatHandler.cpp`. | **Lean — No core chat hook if module owns commands; one optional hook if integrating with core chat** | For MVP, keep `.bot` commands **entirely in the module** (PLAN §12): module parses `CHAT_MSG_SAY/WHISPER/PARTY` via `OnPacket` or by having `WorldSession::HandleMessagechatOpcode` call a single `sBotChatHandler->OnChat(player, msg, type)` if module present (one `if (sBotMgr)` check in `ChatHandler.cpp`). Better: module registers its own `ChatCommand` sub-table via a new `ChatHandler::RegisterModuleCommands` seam (single file, generic). Either is ≤1 hook. Do not duplicate full command tables into core. |
| 8 | **Group membership / invite / leave / leader** | **No** script hook. `Group::AddMember/RemoveMember` (`Group.cpp:357,438`) and `Group::UpdatePlayerOnlineStatus` (1435) are direct. | **No** | Module can use existing `Group` API + `ObjectAccessor::FindPlayer` + `Player::GetGroup`/`InviteToGroup`/`UninviteFromGroup`. Polling group state each world tick is sufficient for MVP (5-player). No need to hook `Group.cpp`. |
| 9 | **Movement** (follow, stay, formation, path) | **No** hook, but **no hook needed** — normal `Player`/`Unit` movement APIs exist: `Player::TeleportTo`, `MovePoint`, `MotionMaster`, `Map::IsValid`, `ObjectPosSelector`. | **No** | Bot movement is just AI calling normal movement APIs from `BotController::Update`. Do not add `if (isBot)` branches into `MovementHandler.cpp` or `Unit.cpp`. If pathfinding needs `mmaps`, reuse existing `DetourNavMesh`. |
| 10 | **Loot / rolls** | No script hook. `LootMgr`, `Group::GroupLoot/NeedBeforeGreed/MasterLoot/CountRollVote` exist. | **No** | MVP loot = simple rules (free-for-all or round-robin). Module can call existing `Loot`/`GroupLoot` APIs. No new seam. |
| 11 | **Map / dungeon enter/leave, teleport, wipe recovery** | No dedicated hook. `Player::TeleportTo`, `Map::Remove`, `DungeonMap::Remove`, `TeleportToHomebind` exist. | **No** | Module reacts after the fact via world tick + `Player::IsBeingTeleported()` (`Player.h:2059`) + `IsInWorld()`/`FindMap()`. No hook needed for MVP. |
| 12 | **Packet send / broadcast** (updates, chat, addon) | **Partial — already handles null socket** (`WorldSession::SendPacket` early-out). `Player::SendAddonMessage` (`Player.cpp:25414`), `World::SendWorldText`, `PacketBroadcast/PlayerBroadcaster` exist. | **No** | Bot sessions simply do not send packets. `SendPacket` no-op is correct. Addon transport (`TW_CHAT_MSG_WHISPER`) can later be used for module↔addon state query, but MVP needs no packet hook. |
| 13 | **Object lookup / perception** (nearby units, party state, threat) | **Yes** — `sObjectAccessor` (`ObjectAccessor.h:106`: `FindPlayer`, `FindPlayerByName`, `GetPlayers`), `Map::GetCreature/Unit/GameObject`, `Player::GetMap()`, `HostileRefManager`, `ThreatManager`, `SocialMgr`, `ObjectMgr::GetPlayerCache`. | **No** | Module perception layer (`CombatState`, `PartyState`, `NearbyObjects` in PLAN §7) can be built entirely from these existing singletons — no new core seam. Avoid full-world scans every tick; use `Map::VisitNearby` / `Grid` queries. |
| 14 | **Character DB loading / creation** | **Yes** — `LoginQueryHolder::Initialize()` (`CharacterHandler.cpp:85`), `CharacterDatabase.DelayQueryHolderUnsafe`, `Player::LoadFromDB`, `Player::SaveToDB`, `sObjectMgr.UpdatePlayerCache`, `CharacterHandler::HandleCharCreateOpcode`. | **No** | Bot character reuse is preferred (`.bot add <existingCharacter>` per PLAN §12). Creating a new bot character would go through normal `HandleCharCreateOpcode` path or direct `Player::Create` — no new hook. |
| 15 | **Account / security / duplicate login / reconnect** | **Partial** — `World::FindSession(id)` (`World.h:908`), `World::AddSession`, `WorldSocket::HandleAuthSession` account checks, `WorldSession::ShouldLogOut`, `SetDisconnectedSession`, `UpdateDisconnected`. | **Yes — small auth seam** | Need safe handling for headless sessions coexisting with a human session for same account, and for human reclaim. Design: headless sessions use a **separate bot account** or a **sub-account guid** owned by the human account (PLAN §21 security). Core change is minimal: relax or parameterize the `FindSession`-by-account duplicate-login kill (`WorldSocket.cpp`) to allow one additional headless session if `HasNetworkTransport()==false`, or keep bot sessions entirely outside `World::m_sessions` accounting (stored only in `BotManager`). Prefer the latter — no change to duplicate-login logic, just don't use the same `accountId`. |

**Summary:** Of 15 needs, **1 mandatory new seam** (headless session, #6), **1 optional but strongly recommended** (world tick, #1), and up to **2 lean optional** hooks (chat command registration #7, `OnLogout` notification #3). All others can be satisfied via existing APIs or by keeping logic in the module.

---

## 3. Headless Session — Deep Dive & Historical Design Notes

> The final Phase 3 seam is recorded in §10; it supersedes the proposal wording in §§3–4.

### 3.1 Current session/transport assumptions (why this is the hard seam)

- **Creation is socket-coupled:** `WorldSocket::HandleAuthSession` does `ACE_NEW_RETURN(m_Session, WorldSession(id, this, ...), -1)` — the socket (`this`) is passed into the `WorldSession` constructor (`WorldSession.h:308`) and retained as `m_Socket` (`WorldSession.h:991`). There is no `CreateSession` factory; every new session originates from a TCP accept.
- **Session is registered via `World::AddSession`** (`World.cpp:283`). `AddSession_` enforces queue/online-account bookkeeping.
- **Null tolerance exists but is not designed:** `WorldSession::SendPacket` bails on `m_Socket==nullptr` (163), `Update` tolerates `!m_Socket` (383), `LogoutPlayer` branches on `m_Socket` (736). `m_Socket` is set to `nullptr` on disconnect (`WorldSession.cpp:124,370`). This proves a headless session *can* survive, but the creation path has not been formalized.
- **Anticheat is socket-coupled too:** `WorldSocket::HandleAuthSession` calls `m_Session->InitAntiCheatSession(&K)` (`:338`) which instantiates `SessionAnticheat` (which expects a real client). The null pattern already exists: `NullSessionAnticheat` (`Anticheat.h:143`) and `NullAnticheatLib` (`:209`) — used as safe fallbacks. Headless sessions should use `NullSessionAnticheat` directly, skipping Warden.
- **Precedent for non-network services:** `DiscordBot` (`World.cpp:2343`) shows a service can be initialized from `World::SetInitialWorldSettings` without touching `WorldSession`. Headless sessions should follow that pattern: a *module-owned* service, not a core player feature.

**Shyalya evidence of pain if this is done naïvely:** `shyalya-tortoise-wow` required ~80 host-hook files for its vendored `cmangos/playerbots` port, including `NullSessionAnticheat` for bot sessions, BG queue `recursive_mutex`, navmesh keep, healer 125y range, stealth targeting, talent generation backfills. The lesson is: do **not** reproduce ~80-file surface. Centralize.

### 3.2 Recommended semantic model (PLAN Rule 5)

Core understands a **capability**, not a bot identity:

```cpp
// WorldSession.h
bool HasNetworkTransport() const { return m_Socket != nullptr && !m_Socket->IsClosed(); }
bool CanReceiveClientPackets() const { return HasNetworkTransport(); }
// Optionally: bool IsHeadless() const { return !HasNetworkTransport(); }
```

No `IsBot()`, no `GetBot()`, no `m_bot`, no `PlayerBotEntry` in core. The module interprets `IsHeadless()==true` as “this session is managed by PlayerBots”.

Packet filtering already uses this shape: `WorldSessionFilter::PacketFilter` already distinguishes `m_processLogout` vs thread-safe packets.

### 3.3 Minimal factory — centralized, module-owned meaning

**Option A — In-core factory with generic name (recommended, ≤1 file changed for creation):**

```cpp
// WorldSession.h — add generic factory, still centralized
static WorldSession* CreateHeadlessSession(uint32 accountId, ObjectGuid characterGuid);
 // - allocates WorldSession(id, nullptr, SEC_PLAYER, 0, LOCALE_enUS, "127.0.0.1", 0x7F000001)
 // - accountId is the character's ordinary owning account; Headless registration is character-keyed
 // - InitAntiCheatSession → NullSessionAnticheat (skip K)
 // - LoadTutorialsData() (no-op for bots)
 // - sWorld.AddSession(session)
 // - immediately posts a LoginQueryHolder for characterGuid
```

Core change: **~30 lines** in `WorldSession.cpp` + a declaration in `WorldSession.h`. No spread to `Player.cpp`/`Unit.cpp`/`Spell.cpp`.

**Option B — Module-owned factory via friend/bridge (even less core change):**

Core only adds `HasNetworkTransport()` and relaxes `WorldSession` constructor visibility to allow `new WorldSession(id, nullptr, ...)`. Module (in `src/modules/TortoiseBots/host/BotSessionAdapter.cpp`) constructs the session and calls `sWorld.AddSession`. This keeps `CreateHeadlessSession` *out of core entirely* and leaves core with only a semantic query (`HasNetworkTransport`) plus null-socket tolerance already present.

Either option satisfies PLAN Rule 4 (`≤5 host files`). Option B is purer (core truly does not know about bots), Option A is more ergonomic for the module. Recommend **Option B for MVP** — smallest core diff, easy to review.

### 3.4 Lifecycle that must be verified (acceptance test per PLAN §11)

For a single configured character (existing character, owned by test account), with headless factory:

1. `CreateHeadlessSession` → `AddSession` → `LoginPlayer(guid)` → `HandlePlayerLoginCallback` → `Map::ExistingPlayerLogin` → bot `Player` is `IsInWorld()` for ≥5 minutes.
2. `Player::SaveToDB(false,false)` succeeds without socket.
3. `Group::AddMember`/`RemoveMember` does not crash when `m_Socket==nullptr` (already guarded).
4. Teleport (e.g. `.go xyz` via module) → `IsBeingTeleportedFar()` → `ExecuteSingleDelayedTeleport` → re-enters world.
5. `LogoutPlayer(true)` → `Map::Remove` → `Map::DeleteFromWorld` → `SetPlayer(nullptr)` → `World::SetSessionDisconnected` if needed.
6. Re-login same character again (duplicate login protection does not permanently ban the bot account).
7. Server shutdown (`World::Update` exit) cleanly deletes headless sessions without leaks (`World::UpdateSessions` `UpdateDisconnected` path already handles `!m_Socket` → `RemoveReference`).

**Key invariants to test:** no DB query per bot tick, no full-world scan per tick, no synchronous HTTP in map thread, no `GetBot()` reappearance, human login takes precedence (if same account somehow used, human wins — but we avoid same-account by using a separate bot account per PLAN §21).

---

## 4. Minimum Required Core Edits — Historical Proposal (superseded by §10)

**Target: ≤4 files directly PlayerBots-aware, all concentrated in a single host boundary.**

| File | Edit | Why it is necessary | Generic vs bot-specific |
| --- | --- | --- | --- |
| `src/game/WorldSession.h` + `WorldSession.cpp` | Add `bool HasNetworkTransport() const` + `bool IsHeadless() const` + (optional) `static WorldSession* CreateHeadlessSession(uint32 accountId, ObjectGuid guid)` or at minimum allow `WorldSocket* nullptr` construction. Formalize that `SendPacket`/`Update`/`LogoutPlayer` already handle null socket; add `NullSessionAnticheat` wiring for headless sessions. | The *only* way to get a bot `Player` into the world through normal `Player`/`Map` machinery without a TCP socket (PLAN Rule 5). Without this, bot login is impossible without forking socket code. | **Generic** — “headless/synthetic session” is a legitimate core concept (e.g. for future automation, tests). No bot word in the core API if Option B is chosen. |
| `src/game/World.h` + `World.cpp` | Add one module-update call in `World::Update(diff)` near `UpdateSessions`/`sMapMgr.Update`: `if (sBotHost) sBotHost->Update(diff);` (weak symbol / optional). And optionally a `SetBotHost(IBotHost*)` registration. | Bot AI needs a once-per-frame driver. `World::Update` is the only place that already drives all sessions and maps; there is no existing `WorldScript::OnUpdate`. Without this, module would have to poll from a thread — unsafe for `Player`/`Map` access. | **Generic** — `IBotHost` can be named `IHeadlessSessionHost` or `IWorldTickListener`; not bot-specific. Single call site. |
| `src/game/Chat/Chat.h` + `Chat/Chat.cpp` *(optional, deferrable)* | Add `ChatHandler::RegisterModuleCommand(const char* modName, ChatCommand* table)` or a single dispatch hook in `ChatHandler::HandleChat` (`if (sBotHost && sBotHost->HandleChat(player, msg, type)) return;`). | MVP `.bot` commands can already be handled via `WorldSessionScript::OnPacket` interception without touching `Chat.cpp`. This edit is **not required for Phase 3**. Propose to add only when chat-command ownership is validated in Phase 4. | **Generic** — module command registration. |
| `src/game/Anticheat/Anticheat.h` *(no edit, just wiring)* | Reuse `NullSessionAnticheat` for headless sessions (already exists). | Avoids bot sessions triggering Warden/anticheat checks that assume a real client. | Already generic — no change. |
| **New file (counts as host boundary, not a scattered edit):** `src/game/BotHostAdapter.h` + `BotHostAdapter.cpp` (or `src/modules/TortoiseBots/host/Module.cpp`) | Single bridge file that holds the weak `IBotHost*` pointer, implements `CreateHeadlessSession` if Option B, and translates core tick/login events to module calls. Core only includes this one header. | Centralizes all PlayerBots/core integration into one clearly named file per PLAN Rule 4. Keeps `World.cpp` edit to one include + one call. | This file is **allowed to be PlayerBots-aware** — it *is* the boundary. |

**Total: 2 files mandatory (`WorldSession`, `World`), 1 optional (`Chat`), plus 1 new bridge file.** Well under the `≤5` target and far under the `8–10` warning. No edits to `Player.cpp`, `Unit.cpp`, `Spell.cpp`, `MovementHandler.cpp`, `Group.cpp`, etc. — which is where Shyalya's 80-file surface went wrong.

### 4.1 What is deliberately *not* edited

- `Player.cpp` / `Player.h` — no `IsBot()` checks. Bots are just `Player`s with a headless session.
- `Unit.cpp` / `Spell.cpp` — no bot branches. Bot actions call normal `Unit::CastSpell`, `Unit::Attack`, etc.
- `MovementHandler.cpp` — no `if (isBot)` — bot movement is AI-driven via `MotionMaster`.
- `Group.cpp` / `Guild.cpp` / `Map.cpp` — existing `m_Socket` null guard is sufficient; no bot-specific branches.
- `WorldSocket.cpp` — no bot-specific socket exceptions. Socket path remains untouched; headless sessions bypass it entirely.

### 4.2 Alternative considered: zero core edits for world tick

Could the module drive bots from its own thread via `std::thread` + `World::GetMessager`? No — `Player`/`Map` are not thread-safe; all bot AI must run on the world/map thread via `World::Update`. Hence the single `World::Update` hook is the *correct* generic seam; not a bot-specific leak.

---

## 5. Per-Need Verification Notes

- **Perception** can be satisfied without any new hook: `sObjectAccessor.GetPlayers()`, `Map::GetCreature`, `Player::GetSelectionGuid`, `ThreatManager`, `SocialMgr`, `Group::GetMembers`. Cache immutable spell/talent metadata (`SpellEntry`) at startup per PLAN §20.
- **Loot** — `LootMgr`, `GroupLoot` already handle party loot; module only decides Need/Greed/Pass via existing `Group::CountRollVote` API.
- **Teleport / dungeon recovery** — already covered by `Player::TeleportTo` + `IsBeingTeleportedFar()` + `Map::Remove` semantics; module just polls `IsInWorld()` and re-issues follow.
- **Security / ownership** — per PLAN §21, bot characters must be on accounts owned by the requesting human account (or a dedicated bot sub-account). Duplicate login is handled by not reusing the human `accountId` for bots. Human session always takes precedence; bot session is evicted if conflict.

---

## 6. Stop Condition Check

- Proposed host-aware files **before MVP works**: 2 mandatory + 1 bridge = **3** — well under the `STOP/REDESIGN` threshold of ~20 files, and under the 8–10 warning.
- `BUILD_PLAYERBOTS=OFF` remains the default — core builds without `src/modules/TortoiseBots` and without any new hook enabled (the `if (sBotHost)` guard is null when module absent).
- No `GetBot()` / `m_bot` / `sPlayerBotMgr` reintroduced.

**Decision:** Proceed to **Phase 2 — Empty Module** (PLAN §10) after review, then **Phase 3 — Headless Session Spike** (PLAN §11) implementing the factory above and running the 7-step acceptance test.

---

## 7. Build Matrix (to verify in Phase 2)

```
Core + module absent + OFF  → must build (baseline)
Core + module present + OFF → must build, module ignored
Core + module present + ON  → must build, module registers, logs "PlayerBots module loaded"
```

No SQL or config required when `OFF`.

---

## 8. Provenance & References

- This document is the deliverable for `PLAN.md` §9 / §26. It is a *design* based on source inspection, not on CMaNGOS/MangosZero/Shyalya architecture.
- Source material consulted per `AGENTS.md` reference strategy: current Tortoise core (primary), then `DISCOVERY.md` (§1-10) for upstream chain, then `playerbots-references/` for confirmation (commits recorded in §0).
- Upstream behavior (CMaNGOS playerbots, MangosZero Bots, Shyalya Turtle fixes) was **not** used to define host seams — those will be harvested in Phase 6 (`PLAN.md` §14) via the `study → extract intent → reimplement → test` pipeline with `docs/PROVENANCE.md`.

---

## 9. Review status

- Factory location: **Option B**, module-owned `BotSessionAdapter`.
- World tick: **`IWorldUpdateListener`**, with callbacks copied under a mutex and invoked outside it.
- Login: queued `AddSession`; `BotManager` dispatches `LoginPlayer` only after the session is visible in `World::m_sessions`.
- `AddSessionDirect`: removed after the queued runtime spike passed.
- Chat/command registration: deferred to Phase 4; no chat seam was added here.
- Exact local core snapshot: Docker source is `d07ec3f` (the source tree is an ephemeral synced checkout, not a separate Git worktree).

---

## 10. Phase 3 final host seam

The final seam has three generic capabilities; the core never asks whether a `Player` is a bot:

| Capability | Public surface | Final behavior |
| --- | --- | --- |
| Transport | `SessionTransport`, `IsHeadless()`, `HasNetworkTransport()`, `InitHeadlessSession()` | `BotSessionAdapter` constructs `WorldSession(..., SessionTransport::Headless)` and uses `NullSessionAnticheat`. Transport is fixed at construction; there is no `SetHeadless` mutation. |
| Queued session lifecycle | `World::AddSession` (Network), `AddHeadlessSession` (Headless), `HasPendingHeadlessSession`, `CancelPendingHeadlessSession` | Network sessions remain account-keyed in `m_sessions`. A Headless session carries its character `ObjectGuid` through a separate pending queue, then enters `m_headlessSessions`; cancellation removes and deletes only that queued Headless session. `World::InternalShutdown` drains both queues. `AddSessionDirect` is gone. |
| Async login dispatch | `LoginQueryHolder { accountId, characterGuid, SessionTransport }` | The query callback retains no `WorldSession*`: Network holders resolve `FindSession(accountId)` and Headless holders resolve `FindHeadlessSession(characterGuid)`. This keeps same-account Headless login and real-client reclaim unambiguous. |
| World tick | `IWorldUpdateListener::OnWorldUpdate(uint32)` | `World` drains optional-module factories after construction, copies listeners under `m_worldUpdateListenersMutex`, then invokes them outside the lock. |

One account may have at most one active Network session in `m_sessions` and may have multiple active Headless character sessions in `m_headlessSessions`. Headless sessions are keyed by character `ObjectGuid`, not account ID; they do not affect Network-only session counts, queues, limits, or realm population. `account.online` remains `1` while either registry has a session for the account and becomes `0` only after neither does.

The module lifecycle is explicit: `PendingAdd → PendingLogin → InWorld`, with `Removing` retained until the session/player is gone. `AddBot → immediate RemoveBot` cancels the pending Headless entry and erases the record only after confirming there is no active Headless session, pending Headless session, player, or `BotRecord`.

Security is not altered by the module: `BotSessionAdapter` uses `sAccountMgr.GetSecurity(accountId)` verbatim. The runtime fixture used here is account `4`, character `Dudette` (guid `1`), whose stored account security is used; no `accountId == 4` elevation exists.

### Phase 3 evidence

- Queued `AddSession` spike: **passed** — login, enter-world, save, logout, and relog all completed; no `AddSessionDirect` remains.
- Pending add/remove regression: **passed** — `PendingAddRemoveTest PASSED ... active 0 player 0 record 0 pending 0`.
- Active-bot graceful shutdown: **passed** — before stop `characters.online=1`, `account.online=1`; after `docker compose stop mangosd`, both were `0`.
- Build and static audits: see §7 and the handover commands; no legacy `GetBot`/`m_bot`/`sPlayerBotMgr` matches remain.
- Real-client human reclaim: **passed** — headless `Dudette` (guid 1, acct 4, `Headless`) was InWorld (`online=1`), then a real `WorldSocket` `Network` session for the same account/guid performed `CMSG_AUTH_SESSION` (build 5875, `TestAuthBypass` for the Python spike) → `CMSG_PLAYER_LOGIN` 1. `World::AddSession_` moved the old headless to `m_disconnectedSessions` via `ForcePlayerLogoutDelay`, `HandlePlayerLogin` `alreadyOnline` did `old->SetPlayer(nullptr); pCurr->SetSession(new); broadcaster->ChangeSocket(newSocket)`, `BotManager` saw `!IsHeadless()||!isOurAccount` and logged `Bot Player Dudette (Guid: 1) reclaimed by network session acct 4 (headless 0) — releasing`, erased `BotRecord`, no tick, `SMSG_LOGIN_VERIFY_WORLD` 0x236 received, `SMSG_AUTH_RESPONSE` 0x1EE `0x0C`, logout → `online 0`, re-add via `AutoTest` relog `spike PASSED` again. No duplicate `WorldSession`/`Player`, no crash, character playable, immediate `AddBot→RemoveBot` still `PASSED`.

### Portability note

The current Linux static-library bootstrap uses whole-archive linking plus the used factory registrar. A Windows/MSVC integration must use `/WHOLEARCHIVE:tortoise_bots.lib` (or replace the registrar with an explicit `TortoiseBots::Initialize()` call from the host).

## 11. Current native module boundary — 2026-08-24

The Phase 1 material above is historical design/discovery. The implemented
boundary is now Penqle's native `modules/<name>/` loader:

- `src/TortoiseBotsModule.cpp` is the only loader-recursed source.
- `host/BotHostAdapter`, `BotPlayerAdapter`, and `BotChatAdapter` register
  generic `WorldScript`, `PlayerScript`, and `AllCommandScript` hooks.
- `runtime/BotManager` owns bot records, Headless sessions, controllers, and
  `PlayerbotAIAdapter` instances. Donor `PlayerbotMgr.cpp`,
  `RandomPlayerbotMgr.cpp`, and `PlayerbotLoginMgr.cpp` are not compiled.
- Core asks only about generic transport/headless capabilities and lifecycle
  hooks; it does not expose bot identity or bot-specific player fields.
- `BUILD_PLAYERBOTS=ON` selects the native TortoiseBots path. The old vendored
  CMaNGOS tree requires `BUILD_LEGACY_PLAYERBOTS=ON` explicitly.

The current link/runtime checkpoint is recorded in `docs/PROVENANCE.md`. The
runtime gate now also covers AI-enabled startup with the native auxiliary
schema, Headless AI attachment, pending add/remove cancellation, save/logout,
and relog. The random-bot service is bounded and only discovers pre-existing
characters; it does not create accounts or own sessions outside `BotManager`.
The native compatibility layer also serves the named-location table and
per-character AH buy/sell multipliers; random gear teleporting and account
creation remain outside the current boundary.
Expansion-only source families remain explicitly filtered rather than compiled
through compatibility no-ops.

## 12. Current packet/config/build seam — 2026-08-24

The reproducible core requirement is `tortoise-wow` branch
`playerbots-integration-gh` at
`9487c5150a6553c665fafc1f4568669b8b00f011` (parent
`133c6d19bf5898c1e4f5129b2890b1db89b17a07`). It contains the generic
`SessionTransport`, GUID-keyed Headless registry/queue, same-account
`1 Network + N Headless` lifecycle and reclaim behavior, generic ScriptMgr
hooks, packet hooks, and the per-static-module object-target build mechanism.

`host/BotPacketAdapter` is the only packet interpretation layer. Penqle calls
`ServerScript::CanPacketSend` before socket output and
`ServerScript::CanPacketReceive` before opcode dispatch. The adapter maps:

- Headless sends → `PlayerbotAI::HandleBotOutgoingPacket`;
- Network master sends → each owned AI's `HandleMasterOutgoingPacket`;
- Network master receives → each owned AI's `HandleMasterIncomingPacket`.

The module does not add bot-specific packet branches to core. The fresh
`PacketBridgeTest` runtime journey exercised Headless outgoing delivery,
Network-master outgoing delivery, and automatic mature Trigger → Action group
acceptance, then removed both Headless sessions cleanly. Incoming delivery is
registered through the same generic `ServerScript::CanPacketReceive` hook and
logs selected real-client opcodes; the final proof of that path is deliberately
left to the manual Network-client journey rather than a synthetic adapter call.

Static native modules are compiled as isolated `OBJECT` targets and folded
into the combined `modules` archive. TortoiseBots definitions, include paths,
and PCH are attached to `mod_tortoisebots_static`, not sibling static module
sources. Core commits `133c6d19` and `9487c515` remove static-module include
directories and the stale `src/game/PlayerBots` path from the combined archive
target; its generated loader needs only common module includes.

Penqle `Config::GetValues(prefix)` now enumerates actual ACE configuration
keys safely. `AiPlayerbot.LoginCriteria.*` and `AiPlayerbot.WorldBuff.*` load
without a casted object layout; the runtime emitted `Loading WorldBuffs`.

## 13. Final pre-playtest correctness pass — 2026-08-24

The native module now owns one centralized movement transition helper on
`PlayerbotAI`. Follow removes stale wander/stay state, wander removes
follow/stay, and stay removes follow/wander; guard, free, and passive retain
their mature shortcut relationships. `BotController` contains only bot/master
GUIDs and intent/diagnostic state. `BotManager` never invokes controller
movement; `PlayerbotAIAdapter` is the only gameplay update owner.

Native `.bot stay` now invokes the mature `StayChatShortcutAction`, so it also
records the current "return" and "stay" anchors. Native `.bot follow` uses
the mature follow shortcut when a live master is available. On reconnect,
`PlayerbotAIAdapter` rebinds the live master pointer and preserves the existing
mature movement strategies; it consults no stale `BotController` intent.

Random-bot adoption/release uses `BotManager::BindBotMaster` and
`ClearBotMaster`, keeping `BotRecord.masterGuid`, `PlayerbotAI::master`, and
the module-owned Headless session relationship synchronized.

The packet fixture is strict: it emits the native group invite and waits for
the normal `SMSG_GROUP_INVITE → WorldPacketTrigger → AcceptInvitationAction`
path to join the group. There is no direct `DoSpecificAction("accept invitation")`
rescue and no direct incoming adapter injection. The fixture passed automatic
group acceptance and cleanup after the final cached ON build.

Human logout clears only the live raw master pointer from each matching
Headless AI; the module retains `BotRecord.masterGuid`. A later Network login
rebinds matching existing Headless bots without replacing their sessions and
restores follow or stay intent. The canonical random timing loader key is
`AiPlayerbot.MaxRandomBotRandomizeTime`, with the former typo retained only as
a compatibility fallback.

The value audit fixed concrete null assumptions in mount-speed aggregation and
possible-adds evaluation. Temporary `AiFactory`/command/action `printf` or
info-level diagnostics were removed or demoted; selected packet-hook and
lifecycle errors remain operational diagnostics. The runtime is left with AI
enabled and the packet fixture disabled for manual client playtesting.
