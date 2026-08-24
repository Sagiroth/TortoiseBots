# Headless Session Diagnosis — Same-Account Alt Bot

**Date:** 2026-08-21
**Trigger:** `Sagiroth` (guid 2, account 4, Network) + `Dudette` (guid 1, account 4, Headless) — `AddBot` disconnects Sagiroth, `RemoveBot` never finishes, `relog` reclaims Dudette.
**Branch:** `phase4-follow` `f5e6f74` — Follow slice built, `tortoise_bots` linked. Host seam frozen before this diagnosis.

> This is **not** a TortoiseBots bug — it is a generic `World::m_sessions` assumption that is valid for `Network` clients but invalid for `Headless` character sessions. The file-watcher spam is the same root cause (re-queue on same account).

> **Historical archive:** this diagnosis predates the current native session seam. The current implementation uses `World`-owned `Network`/`Headless` session collections and a module-owned `BotManager` record/AI adapter map; the old account-keyed collision model and `BotController` references below are retained only as investigation history. See `docs/HOST_API.md` §11–§12 for the active contract.

---

## 1. Exact reason `AddSession` disconnects Sagiroth

`WorldSession` is constructed as `WorldSession(accountId=4, nullptr, ..., Headless)` and queued via `sWorld.AddSession(session)` → `addSessQueue.push(session)`.

On the next `World::UpdateSessions` → `AddSession_` ( `World.cpp:328` ):

```cpp
SessionMap::const_iterator old = m_sessions.find(s->GetAccountId()); // find(4)
if (old != m_sessions.end()) {
    // duplicate account — treat as relog
    old->second->SetDisconnected(true); // or ForcePlayerLogoutDelay
    m_sessions.erase(old); // or move to m_disconnectedSessions
}
m_sessions[s->GetAccountId()] = s; // m_sessions[4] = headless Dudette
```

`m_sessions` is `std::unordered_map<uint32 /*accountId*/, WorldSession*>` — **one slot per account**. The human's `Network` session for `Sagiroth` already occupies `m_sessions[4]`. The headless `Dudette` also wants `m_sessions[4]`. The duplicate-account path therefore **kicks the human** to `m_disconnectedSessions` and replaces it. The client sees `SMSG_AUTH_RESPONSE` → disconnect. On human relog, the same code runs in reverse and kicks Dudette.

The queued `HasPendingSession(accountId, Headless)` check in `BotManager` is transport-aware, but `m_sessions` itself is **not** — the active map still collides.

---

## 2. Every place that assumes one `WorldSession` per account

**Core `World` (authoritative):**

- `World.h:1328` `SessionMap m_sessions` — `std::unordered_map<uint32, WorldSession*>` keyed by `accountId`
- `World.h:913` `FindSession(uint32 accountId)` — `m_sessions.find(accountId)` — single lookup, no transport
- `World.cpp:302` `AddSession(WorldSession* s)` — `addSessQueue.push(s)` — queue is homogeneous, transport not checked until `AddSession_`
- `World.cpp:328` `AddSession_` — `m_sessions.find(s->GetAccountId())` duplicate-account handling, `m_sessions[accountId]=s`
- `World.cpp:249` `InternalShutdown` drains `addSessQueue` — deletes whatever is queued, but `m_sessions` still single-keyed
- `World.cpp:276` `FindSession`, `290` `GetSessionCount`, `3500` `UpdateSessions` loop over `m_sessions`, `4554` `RemoveSession` erases by `accountId`
- `WorldSession.h:370` `GetAccountId()` — used as the map key everywhere
- `WorldSession.cpp:1040` `InitHeadlessSession()` — sets `m_transport=Headless` but still stores `m_accountId=4`

**Auth/DB (account as identity):**

- `account` table `id` PK, `characters` `account` FK, `account.online` flag — one `online` per account in `World::UpdateSessions` (sets `account.online=1` when `m_sessions` has entry, `0` when removed)
- `CharacterHandler.cpp:3105` `FindSession(accountId)` for `alreadyOnline` check on `CMSG_PLAYER_LOGIN`
- `WorldSocket.cpp:382` `HandleAuthSession` — `FindSession(accountId)` for duplicate auth

**TortoiseBots (inherited the assumption):**

- `BotSessionAdapter.cpp:39` `sWorld.AddSession(headless)` — reuses the single account-keyed queue
- `BotManager.cpp:27` `FindSession(accountId) || HasPendingSession(accountId,Headless) || FindPlayer(guid)` — precondition check still account-keyed
- `BotManager.cpp:65` `FindSession(accountId)` in `AddBot` already-tracked check — hits human when sharing account
- `BotManager.cpp:108` `FindSession(accountId)` in `RemoveBot` — finds human, not bot, hence `found non-headless session acct 4 — releasing on reclaim`
- `BotManager.cpp:217` `FindSession(rec.accountId)` in `OnWorldUpdate` `PendingAdd → PendingLogin` — dispatches `LoginPlayer` only when `FindSession` returns the headless, but it returns the human
- `BotManager.cpp:239` `FindPlayer` + `IsHeadless`/`isOurAccount` reclaim check — correct, but `RemoveBot`'s `CancelPendingSession`/`FindSession` still account-keyed, so `Removing` never reaches `!FindSession` erase (human still in `m_sessions[4]`)

---

## 3. Why `FindSession(accountId)` is currently used by TortoiseBots

Because `Headless` was modeled as “a `WorldSession` without a socket” but still an **account session** — the same `WorldSession` type, same `accountId`, same `m_sessions` slot, same `AddSession` queue.

`BotManager` therefore treats the headless session **exactly like a normal login**:

- `CreateHeadlessSession(accountId,guid)` → `new WorldSession(accountId, nullptr, ..., Headless)` → `AddSession` → wait for `FindSession(accountId)` to become the headless → `LoginPlayer(guid)` → `HandlePlayerLogin` → `Map::ExistingPlayerLogin`

This works when bot characters are on **dedicated bot accounts** (different `accountId`s, no collision) — which is why the file-watcher `PendingAddRemoveTest` on `account 4` with no human online passed, and the `AutoTest` on a fresh account passed. It **fails** when the same human account drives its own alt headlessly, which is the explicit TortoiseBots requirement.

---

## 4. Smallest generic fix

**Make `Headless` a different *kind* of session with different *identity* — not a different account.**

Accounts remain the billing/DB owner (`characters.account`), but **runtime sessions are keyed differently**:

- `Network` sessions: keyed by `accountId` in `m_sessions` — exactly as today (one per account, `FindSession(accountId)` stays)
- `Headless` sessions: **not in `m_sessions` at all** — keyed by **character guid** in a new `m_headlessSessions` / `m_pendingHeadlessQueue`, found via `FindHeadlessSession(guid)` / `HasPendingHeadlessSession(guid)`

**Implementation sketch (generic, not TortoiseBots-only):**

```cpp
// World.h
SessionMap m_sessions; // Network only, as today
std::unordered_map<uint32 /*guidLow*/, WorldSession*> m_headlessSessions;
LockedQueue<WorldSession*> m_headlessQueue; // or reuse addSessQueue with transport check

WorldSession* FindSession(uint32 accountId) const; // unchanged — Network only
WorldSession* FindHeadlessSession(ObjectGuid guid) const;
bool HasPendingHeadlessSession(ObjectGuid guid) const;
void AddHeadlessSession(WorldSession* s); // enqueues to m_headlessQueue, not addSessQueue
void RemoveHeadlessSession(ObjectGuid guid);
```

```cpp
// World.cpp AddSession_ — transport-aware
void World::AddSession_(WorldSession* s) {
  if (s->IsHeadless()) {
    // Headless: key by character guid, no duplicate-account check
    auto it = m_headlessSessions.find(s->GetPlayerGuidLow()); // or guid from LoginPlayer? See §5
    if (it != m_headlessSessions.end()) { /* duplicate headless for same guid → replace */ }
    m_headlessSessions[guidLow] = s;
  } else {
    // Network: keep today's duplicate-account logic
    auto old = m_sessions.find(s->GetAccountId());
    if (old != m_sessions.end()) { /* kick to disconnected */ }
    m_sessions[s->GetAccountId()] = s;
  }
}
```

**Why this is generic:** `Headless` is not “a bot” — it is “a character session without a transport” (the same concept as `NullSessionAnticheat`). Any future automation (tests, `World` CLI, `Autobroadcast` as a fake player) would reuse it. The core still knows only `HasNetworkTransport()` / `IsHeadless()`, not `IsBot()`.

**Alternative considered and rejected:**

- Fake `accountId` (e.g., `accountId+1000000`): **TortoiseBots-only hack**, breaks `account.online`, `characters.account` FK, `sAccountMgr` security, and `CharacterHandler` `alreadyOnline` — violates “no fake account IDs” in the task.
- Making `m_sessions` `multimap<accountId, Session*>`: still account-keyed, still needs transport check everywhere, and `FindSession(accountId)` becomes ambiguous (which of the two sessions for account 4?).
- Keeping headless in `BotManager` only (not in `World` at all): possible, but then `World::UpdateSessions` would not call `WorldSession::Update` for headless, and `ObjectAccessor`/`Map` would not see the `Player` as a `WorldSession` — we would need to reimplement `WorldSession::Update` in `BotManager`, which is larger.

---

## 5. Lifecycle ownership after the fix

**Owner:** `World` still owns **both** kinds of `WorldSession` objects (they live in `m_sessions` or `m_headlessSessions` and are deleted in `World::InternalShutdown` / `RemoveSession` / `RemoveHeadlessSession`). `BotManager` **does not own** the `WorldSession*` — it owns the `BotEntry{record, controller}` and *references* the session via `FindHeadlessSession(guid)` or `FindPlayer(guid)->GetSession()`.

**Why World and not BotManager:** `WorldSession` is a core type with `WorldSession::Update`, `SendPacket` (no-op for Headless), `LogoutPlayer`, `HandlePlayerLogin`, `m_anticheat`, `m_tutorials`, etc. — its lifetime must be tied to `World::UpdateSessions` and `World::InternalShutdown`, not to a module's `unique_ptr`. Keeping it in `World` also preserves the existing `Queued AddSession` → `LoginPlayer` → `Map::ExistingPlayerLogin` pipeline without reimplementing `LoginQueryHolder`.

**Pending queue:** `m_headlessQueue` (or a second `LockedQueue`) holds `Headless` sessions that have not yet been moved to `m_headlessSessions`. `BotManager::AddBot` enqueues via `AddHeadlessSession`, not `AddSession`. The queue is drained in `World::UpdateSessions` **after** `m_sessions` but **without** the `FindSession(accountId)` duplicate check — instead it checks `FindHeadlessSession(guid)`.

---

## 6. How login/logout/save/reclaim would work

**Login (add Dudette headless while Sagiroth Network is online, same account 4):**

1. `BotManager::AddBotWithMaster(4, guid 1, masterGuid 2)` → `new WorldSession(4, nullptr, sec, ..., Headless)` → `sWorld.AddHeadlessSession(session)` → `m_headlessQueue.push(session)` (no `m_sessions[4]` collision)
2. Next `World::UpdateSessions` → `AddHeadlessSession_` → `m_headlessSessions[1]=session`
3. Next `BotManager::OnWorldUpdate` → `FindHeadlessSession(guid 1)` is the headless → `session->LoginPlayer(guid 1)` → `CharacterDatabase.DelayQueryHolder` → holder transport is `Headless`, so the generic callback resolves `FindHeadlessSession(guid 1)` → `HandlePlayerLogin` → `Map::ExistingPlayerLogin` → `BotRecord.lifecycle=InWorld`, `BotController` created with `Follow`
4. `Player` Dudette is `IsInWorld()`, `GetSession()->IsHeadless()==true`, `GetAccountId()==4`, but **not** in `m_sessions`

**Logout (remove Dudette, Sagiroth stays):**

1. `BotManager::RemoveBot(guid 1)` → `FindHeadlessSession(guid 1)` → `BotSessionAdapter::LogoutHeadlessSession(session, true)` → `session->LogoutPlayer(true)` → `Player::SaveToDB`, `Map::Remove`, etc., but **not** `World::RemoveSession(accountId)` — instead `World::RemoveHeadlessSession(guid)`
2. `BotManager::OnWorldUpdate` sees `FindPlayer(guid 1)==nullptr` and `!FindHeadlessSession(guid 1)` and `!HasPendingHeadlessSession(guid 1)` → `m_bots.erase(guid 1)` → `BotController` destroyed
3. `Sagiroth`'s `Network` session in `m_sessions[4]` is untouched, `account.online` for `4` stays `1` because the human is still online — we must **not** set `account.online=0` when a headless logs out (see DB below)

**Save:** `Player::SaveToDB` works for Headless as for Network (already proven in Phase 3) — no `m_Socket` needed, `SendPacket` is no-op, `NullSessionAnticheat` is no-op. Save is triggered by `BotManager` or by `Player::Update` autosave, not by `WorldSession::Update`.

**Reclaim (real client logs in AS Dudette, headless Dudette already online):**

1. `WorldSocket::HandleAuthSession` for account 4 → `sWorld.FindSession(4)` returns the human's `Network` session for `Sagiroth`, **not** Dudette's `Headless`. The real client's `LoginQueryHolder` carries `SessionTransport::Network`, so its generic async callback resolves `FindSession(4)`, never `FindHeadlessSession(guid 1)`.
2. `CharacterHandler.cpp:HandlePlayerLogin` then has `alreadyOnline = sObjectAccessor.FindPlayer(guid)` and `oldSession = oldPlayer->GetSession()` — that path transfers Dudette's existing Headless `Player` to the new Network session. The `BotManager` identity check sees that the GUID no longer belongs to `FindHeadlessSession(guid 1)` and erases the bot record — correct, **only Dudette** is reclaimed; normal one-Network-per-account handling still disconnects Sagiroth.

**Save of `account.online`:** Today `World::UpdateSessions` sets `account.online = (m_sessions.contains(accountId) ? 1 : 0)`. With two maps, it must be `account.online = (m_sessions.contains(accountId) || hasAnyHeadlessForAccount(accountId) ? 1 : 0)` or simply `hasAnyHeadlessForAccount` checks `m_headlessSessions` values with matching `GetAccountId()`. When `Sagiroth` logs out but `Dudette` headless remains, `account.online` must stay `1` — otherwise the account row would incorrectly go offline while a headless character is still in world.

---

## 7. Whether queued `AddSession` can still be reused

**Yes, but as `AddHeadlessSession` with its own queue.** The **pattern** (queue → `UpdateSessions` drain → `LoginPlayer` deferred until `FindHeadlessSession` visible) is exactly what Phase 3 proved correct for the race where `AddSession` is queued and `LoginPlayer` is dispatched on the next tick. We keep that, but we **do not** reuse the same `addSessQueue` and `AddSession_` for both transports, because the duplicate-account handling is different.

If we reuse the same `addSessQueue`, `AddSession_` must branch on `s->IsHeadless()` at the top and dispatch to the correct map — that is still “reusing” the queue, but the branch must be there. Smaller to have two queues, but reusing one with a branch is also generic and keeps `World::InternalShutdown` draining both.

**Do not** make `AddSession` transport-agnostic and key headless by `accountId` — that is the bug we are fixing.

---

## 8. Core files that would need changing

**Minimal generic seam (4 files, all generic session/lifecycle code; no PlayerBots types or callbacks):**

| File | Change | Why |
| ------ | -------- | ----- |
| `src/game/World.h` | Add `SessionMap m_headlessSessions` (or `std::unordered_map<uint32 /*guidLow*/, WorldSession*>`), `LockedQueue<WorldSession*> m_headlessQueue`, `FindHeadlessSession`, `HasPendingHeadlessSession`, `AddHeadlessSession`, `RemoveHeadlessSession` | Separate identity for Headless character sessions |
| `src/game/World.cpp` | Implement `AddHeadlessSession` (queue), `AddHeadlessSession_` (drain to `m_headlessSessions` keyed by guid, **no** `m_sessions[accountId]` check), `FindHeadlessSession`/`HasPending*`, `RemoveHeadlessSession`, `InternalShutdown` drain both queues, `WorldSession::Update` for both registries, and account-online recomputation | Keep `AddSession`/`m_sessions` and all population/queue counts Network-only |
| `src/game/WorldSession.h/.cpp` | Preserve fixed construction-time transport and `InitHeadlessSession()`/`NullSessionAnticheat`; use generic account-online recomputation during disconnect/logout | Transport is still generic, no `IsBot` |
| `src/game/Handlers/CharacterHandler.cpp` | Carry `SessionTransport` in `LoginQueryHolder`; its completion callback resolves Network sessions by account and Headless sessions by character `ObjectGuid` | The async holder retains no raw session pointer, while real-client reclaim stays Network-routed |

**TortoiseBots module (no new host files beyond the 3 above):**

- `host/BotSessionAdapter.cpp` → `CreateHeadlessSession` calls `sWorld.AddHeadlessSession` not `AddSession`
- `runtime/BotManager.cpp` → all `FindSession(accountId)` / `HasPendingSession(accountId,Headless)` / `CancelPendingSession` become `FindHeadlessSession(guid)` / `HasPendingHeadlessSession(guid)` / `CancelPendingHeadlessSession(guid)`; `OnWorldUpdate` `PendingAdd → PendingLogin` checks `FindHeadlessSession`, `Removing` checks `FindPlayer` + `IsHeadless`, not `FindSession(accountId)`

**Not changed:** `Player.cpp`, `Unit.cpp`, `Spell.cpp`, `MovementHandler.cpp`, `Group.cpp`, `Map.cpp`, `WorldSocket.cpp` — still no `IsBot` branches. `WorldSession::Update` already handles `nullptr` socket; `LogoutPlayer` already handles `m_Socket==nullptr` (Phase 3).

---

## 9. Whether this changes the Phase 3 HOST_API contract

**No — it *refines* it.** `HOST_API.md` §10 already defined the final seam as:

> `SessionTransport`, `IsHeadless()`, `HasNetworkTransport()`, `InitHeadlessSession()`, `IWorldUpdateListener`, queued `AddSession` + `HasPendingSession`/`CancelPendingSession`

The **generic capability** stays: “a `Headless` session without a socket that can `LoginPlayer`”. What changes is **where** a `Headless` session is registered:

- **Before (Phase 3 as built):** `Headless` was (incorrectly) registered in `m_sessions` via `AddSession` — the HOST_API did not state the map key, but the implementation did `m_sessions[accountId]=headless`.
- **After (this fix):** `Headless` is registered in `m_headlessSessions` via `AddHeadlessSession` — the HOST_API's `AddSession` row becomes `AddSession` (Network, `m_sessions`) + `AddHeadlessSession` (Headless, `m_headlessSessions`), and `HasPendingSession` becomes `HasPendingHeadlessSession`.

The **module contract** (`BotManager` owns the `BotEntry`, `World` owns the `WorldSession*`, `IWorldUpdateListener` drives `Follow`) does not change. The **config** (`TortoiseBots.AutoTest`, `PendingAddRemoveTest`) does not change. The **provenance** (Phase 3's `SessionTransport`/`NullSessionAnticheat` precedent) does not change.

We should amend `HOST_API.md` §10 with one paragraph: “`Headless` character sessions are keyed by character guid in `m_headlessSessions`, not by `accountId` in `m_sessions`, so one `Network` account session and N `Headless` character sessions for the same `accountId` can coexist.” No new host files beyond the 3 already counted.

---

## 10. Acceptance tests

All run on `tortoise-docker-penqle` sibling (`192.168.2.172` vs `127.0.0.1` per `.env` `REALM_ADDRESS`/`GAME_BIND_IP`), with `Sagiroth` guid 2 and `Dudette` guid 1 both `account 4`, `BUILD_PLAYERBOTS=ON`, `mangosd` `638 MB` + `tortoise_bots` built. Use file watcher `/tmp/tortoisebots.cmd` (`add`/`remove`/`follow` as in Follow handover) or direct `BotManager` calls; no DB `UPDATE characters SET account` (the fix must make same-account work).

| # | Title | Steps | Expected | Fails if |
| --- | ------- | ------- | ---------- | ---------- |
| **A** | Human + headless same account simultaneously | `Sagiroth` logged in `Network` (online, `IsInWorld`), `AddBotWithMaster(4, guid 1, masterGuid 2)` headless | `m_sessions[4]` is `Sagiroth` Network, `m_headlessSessions[1]` is `Dudette` Headless, `GetPlayers()` has 2 `Player`s, `sObjectAccessor.FindPlayer(guid 1)` and `guid 2` both `IsInWorld()==true`, `account.online` for 4 stays `1` | `m_sessions` collision |
| **B** | `add Dudette` does not disconnect Sagiroth | While `Sagiroth` Network is `m_sessions[4]`, `echo "add Dudette" > /tmp/tortoisebots.cmd` | `Sagiroth` stays `IsInWorld()`, no `SMSG_AUTH_RESPONSE` `0x1EE` disconnect, `Dudette` goes `PendingAdd → PendingLogin → InWorld` in `m_headlessSessions`, `sWorld.FindSession(4)` still returns `Sagiroth` Network, `FindHeadlessSession(1)` returns Dudette | `AddSession` still uses `m_sessions[accountId]` |
| **C** | `remove Dudette` leaves Sagiroth untouched | `Dudette` InWorld Headless, `echo "remove Dudette" > /tmp/...` | `Dudette` `LogoutHeadlessSession` → `Map::Remove` → `FindPlayer(1)==nullptr`, `FindHeadlessSession(1)==nullptr`, `m_bots` erases `1`, `m_headlessSessions` erases `1`, `Sagiroth` still `IsInWorld()`, `m_sessions[4]` still `Sagiroth`, `account.online` stays `1`, `characters.online` for `guid 1` becomes `0` while `guid 2` stays `1` | `RemoveBot` still `FindSession(4)` or `account.online` cleared |
| **D** | `Sagiroth` logout does not remove Dudette | `Sagiroth` `LogoutPlayer` (via `CMSG_LOGOUT_REQUEST` or `docker exec` `account logout`) | `Sagiroth` `FindPlayer(2)==nullptr`, `m_sessions` erases `4`, `account.online` stays `1` because `m_headlessSessions` has `1`, `Dudette` still `IsInWorld()`, `BotEntry` for `1` still `InWorld`, its `Follow` now has `master FindPlayer(2)==nullptr` → safe no-op (no crash), not erased | `account.online` or `BotManager` erases headless when Network goes away |
| **E** | `Sagiroth` relog does not reclaim Dudette | `Sagiroth` offline, `Dudette` still Headless InWorld, `Sagiroth` `CMSG_AUTH_SESSION` + `CMSG_PLAYER_LOGIN 2` | `m_sessions[4]` becomes new `Sagiroth` Network, `m_headlessSessions[1]` stays Dudette Headless, `BotManager` sees `FindPlayer(1)` still Headless + `GetAccountId()==4` (still our account, but **not** `FindSession(4)`), so **no** `reclaimed by network session acct 4 (headless 0) — releasing` — `BotEntry` stays, `BotController` still `Follow` `Sagiroth` (now new `Player*` for guid 2) | `BotManager` reclaim still `FindSession(4)` |
| **F** | Real client login **as Dudette** reclaims only Dudette | `Sagiroth` Network InWorld + `Dudette` Headless InWorld (both account 4), real `WorldSocket` `CMSG_AUTH_SESSION` account 4 + `CMSG_PLAYER_LOGIN 1` (Dudette) | `HandlePlayerLogin` finds `oldPlayer = FindPlayer(1)` is Dudette Headless, `oldSession = oldPlayer->GetSession()` is Headless, does `oldSession->SetPlayer(nullptr); newNetworkSession->SetPlayer(oldPlayer); oldPlayer->SetSession(newSession)`, `BotManager` sees `FindPlayer(1)->GetSession()->IsHeadless()==false` → `m_bots.erase(1)` and `m_headlessSessions` erases `1`, `Sagiroth` `m_sessions[4]`? Actually `m_sessions[4]` is now the new Network session for Dudette, **not** Sagiroth — so `Sagiroth` must have been moved to `m_disconnectedSessions`? Wait, `m_sessions` is one per account, so `Sagiroth` Network and `Dudette` Network cannot both be `m_sessions[4]` — the second login must kick the first. This test therefore expects: after Dudette Network login, `Sagiroth` is disconnected (as today for same-account two characters), but `m_headlessSessions` is empty, `m_sessions[4]` is Dudette Network, and `BotManager` has no `BotEntry` for Dudette (reclaimed). `Sagiroth` is now offline, which is correct — you cannot have two `Network` characters of same account simultaneously, only one `Network` + N `Headless`. | Headless vs Network distinction |
| **G** | Two headless alts same account can coexist | `Sagiroth` Network `guid 2` + `AddBot` `Dudette` guid 1 Headless + `AddBot` `ThirdAlt` guid 3 Headless (all account 4) | `m_sessions[4]` is `Sagiroth`, `m_headlessSessions[1]` and `[3]` both exist, `GetPlayers()` has 3 `Player`s, each `BotEntry` has its own `BotController` with `Follow` to `Sagiroth`, `account.online` stays `1`, `RemoveBot` for one does not affect the other or `Sagiroth` | `m_headlessSessions` still single-keyed or `AddHeadlessSession` still checks `accountId` |
| **H** | Shutdown saves/logs all cleanly | `Sagiroth` Network + `Dudette` Headless InWorld, `docker compose stop mangosd` | `World::InternalShutdown` drains `addSessQueue` **and** `m_headlessQueue`, iterates `m_sessions` and `m_headlessSessions` calling `SaveToDB`/`LogoutPlayer` for each, `characters.online` for `guid 1` and `2` both become `0`, `account.online` for `4` becomes `0` only when **both** maps are empty, no `ASSERT` in `RemoveSession`, no leaked `BotEntry` (all `m_bots` erased) | `InternalShutdown` only drains `m_sessions` |

---

*Deliverable for this diagnosis is the 10-point analysis above. No `Stay`/`Assist`/`Combat` until the `m_headlessSessions` seam is reviewed and the `FindSession(accountId)` → `FindHeadlessSession(guid)` migration in `BotManager` is approved.*
