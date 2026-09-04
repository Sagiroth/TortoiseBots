# HOST_API — current TortoiseBots host contract

**Target:** Tortoise WoW 1.18.1 core
**Purpose:** describe the implemented generic core/module boundary used by TortoiseBots.

This file describes the current contract. Historical Phase 1 discovery and design
proposals remain available in Git history and are not active implementation
instructions.

## 1. Boundary rule

The core exposes generic capabilities. TortoiseBots assigns bot meaning to those
capabilities.

```text
Tortoise core
    -> session / lifecycle / packet / module primitives
TortoiseBots
    -> bot records / AI / commands / gameplay behavior
```

Normal gameplay systems should not require PlayerBots-specific state. Do not
reintroduce `WorldSession::GetBot()`, `WorldSession::SetBot()`, `m_bot`,
`sPlayerBotMgr`, `PlayerBotEntry`, or scattered bot checks in normal gameplay
code.

## 2. Compatible baseline

The supported local host boundary is validated against:

```text
Core candidate (#411 + #416): e63161c2da7f13ab25687ea389026aa2e3c97647
TortoiseBots tested code:      b9c7784accb8c719e8d7aadd2f6a9e0bda8d07a2
```

Validated local core checkpoint:
`e63161c2da7f13ab25687ea389026aa2e3c97647` (corrected #411/#416 candidate).
It is based on the refreshed upstream `main` at `05912a49f7cd8f12afff04b3c37e6f852f981268`.

Upstream status:
generic Headless capability remains proposed in PR [#411](https://github.com/Penqle/tortoise-wow/pull/411)
(`8037fc8`, based on refreshed upstream `main`). It is not yet merged.
The module-facing surface is the three `World` lifecycle calls plus
`SessionTransport` queries.

Generic participant primitives remain proposed in PR [#416](https://github.com/Penqle/tortoise-wow/pull/416).
The corrected candidate is `e63161c`, based on the corrected #411 candidate,
and remains logically separate.

Compile-verified integration snapshot:

```text
Core:         e63161c2da7f13ab25687ea389026aa2e3c97647
TortoiseBots: b9c7784accb8c719e8d7aadd2f6a9e0bda8d07a2
```

The exact tested core/module pair must be recorded whenever the core changes;
do not infer compatibility from a branch name.

## 3. Session transport

`WorldSession` distinguishes transport capability from gameplay identity:

```text
SessionTransport::Network
SessionTransport::Headless
```

Generic transport queries include `IsHeadless()` and
`HasNetworkTransport()`. TortoiseBots interprets a Headless session as
module-controlled; the core does not expose a bot object through
`WorldSession`.

Headless initialization uses the core's null/no-network anticheat path rather
than pretending a real socket exists.

## 4. Session registry and lifetime

The supported invariant is:

```text
one account
    +-- at most one active Network session
    +-- zero or more active Headless character sessions
```

Network sessions are account-keyed. Headless sessions are character-GUID keyed.
`World` owns both Network and Headless `WorldSession` lifetime; the
`HeadlessSessionMgr` is the only Headless owner.

The module-facing lifecycle is:

```text
World::StartHeadlessSession(accountId, characterGuid, locale, tag)
World::StopHeadlessSession(characterGuid, save)
World::GetHeadlessSessionState(characterGuid)
```

Start performs account, character, lock, ownership, duplicate, and live-player
validation before constructing or dispatching anything. Stop hides pending
cancellation, logout, deletion, and character-online cleanup. State hides the
pending/active maps and reports `NotFound`, `Pending`, `Loading`, or `Active`.
An active Headless session whose materialized player remains out of world for five seconds, while neither loading nor teleporting, is stopped and its character-online state is cleared. This generic recovery prevents a failed map or instance transfer from blocking a later Network reclaim.

Headless sessions never enter the account-keyed Network map and never own
`LoginDatabase` account `online` or `current_realm` state.

## 5. Async login dispatch

Async login state carries immutable identity instead of a retained raw session
pointer:

```text
accountId
characterGuid
SessionTransport
request generation/token
```

Completion resolves the appropriate registry and requires every identity field
to match:

```text
Network  -> account-keyed session
Headless -> character-GUID-keyed manager entry
```

The core dispatches exactly one normal `LoginQueryHolder` bundle per accepted
Start request and then calls the shared character materializer. TortoiseBots
does not queue, promote, or dispatch login.

## 6. Human reclaim

A real Network session takes precedence when the same character returns under
human control. The core performs normal session/player lifecycle work only
after proving the existing Headless entry, transport, character, and account
match. It then detaches and deletes that manager-owned Headless session;
TortoiseBots releases or rebinds its record/AI state as appropriate.

The durable master relationship remains module-owned. The core owns the
generic session/player lifecycle and the reclaim transfer.

## 7. Native lifecycle hooks

TortoiseBots integrates through the native module/script system rather than
hard-wired manager calls in unrelated core files.

Current adapters:

| Adapter | Responsibility |
| --- | --- |
| `BotHostAdapter` | startup, shutdown and world update |
| `BotSessionAdapter` | Headless session lifecycle |
| `BotPlayerAdapter` | player lifecycle/reclaim attachment |
| `BotChatAdapter` | native `.bot` command integration |
| `BotPacketAdapter` | packet bridge into Existing PlayerBots (primarily AzerothCore/mod-playerbots) |

The module should prefer an existing generic hook before requesting a new core
seam.

## 8. World update

Bot AI runs on the normal world/game thread. The core exposes a generic world
update listener mechanism, and `BotHostAdapter` drives `BotManager` / AI,
module-owned `PlayerConvenience`, `AhMarketService`, and
`BattlegroundQueueService` updates from that tick.

The core listener is generic; it does not call a PlayerBots singleton.

## 9. Ownership model

| Responsibility | Owner |
| --- | --- |
| Network `WorldSession` lifetime | Core `World` |
| Headless `WorldSession` lifetime | `World::HeadlessSessionMgr` |
| Pending Headless requests | `World::HeadlessSessionMgr` |
| Headless validation and async callback identity | `World::HeadlessSessionMgr` |
| Bot record lifecycle | `BotManager` |
| AI lifetime | `PlayerbotAIAdapter` |
| AI lookup | `PlayerbotAIStorage` |
| Gameplay decisions | `PlayerbotAI` |
| Movement semantics | Existing PlayerBots (primarily AzerothCore/mod-playerbots) actions/strategies |
| Short-lived player convenience state | `PlayerConvenience` |
| Durable master GUID | `BotRecord.masterGuid` |
| Live master pointer | `PlayerbotAI` |

A second owner for session lifetime, AI state, movement or master identity is an
architecture warning.

## 10. Packet bridge

The core exposes generic packet send/receive hooks. `BotPacketAdapter` is the
module packet interpretation layer:

```text
Headless outgoing
    -> PlayerbotAI::HandleBotOutgoingPacket

Network master outgoing
    -> owned AIs HandleMasterOutgoingPacket

Network master incoming
    -> owned AIs HandleMasterIncomingPacket
```

No bot-specific opcode branches belong in core packet handlers.

The recorded fixture exercised Headless outgoing delivery, Network-master
outgoing delivery and the existing group-invite Trigger -> Action acceptance
path. Real-client incoming delivery remains a separate manual-client acceptance
boundary.

## 11. Command contract

TortoiseBots owns the native `.bot` surface. Current commands include:

```text
add
remove
logout
roster
action attack|stop|pull|pullback|come|stay|follow
action focus skull
action cc moon
action aoe [on|off]
follow
invite
uninvite
stay
guard
free
ready
attack
formation
pullback
summon
list
stats
status
command
help
```

`.bot roster` reads the requester's undeleted account characters and any
explicit cross-account ownership rows from the module-owned durable table. It
emits the structured `TBM:ROSTER_BEGIN`, `TBM:ROSTER`, and `TBM:ROSTER_END`
system-message stream. It is the source of truth for offline and online owned
rows; the runtime `BotManager` records remain transient Headless lifecycle
state.

`.bot action` builds one request context from the requester's normal target and
group. Dynamic actions resolve to the targeted controllable owned bot or the
controllable party bots. Pull and Pullback both use the mature `PullStrategy`
but select different existing policy state: ordinary Pull removes `pull back`,
while Pullback enables its return-to-pull-position trigger. CC resolves a
suitable executor server-side. Addon requests receive one structured
`TBM:ACTION_ACK` or `TBM:ACTION_ERR`; incidental mature-AI chat is suppressed
where the existing silent strategy supports it.

`.bot command` delegates to `PlayerbotAI::HandleCommand` for Existing PlayerBots
(primarily AzerothCore/mod-playerbots) command behavior. Authorization uses
the normal account/GM policy implemented by the module/core boundary. Legacy
named commands remain available for CLI and macro compatibility.

## 12. Native module/build contract

The core consumes the repository at:

```text
modules/TortoiseBots/
```

`src/TortoiseBotsModule.cpp` is intentionally the only loader-recursed source.
The broader source graph is registered by `TortoiseBots.cmake`.

Normal native selection:

```text
BUILD_LEGACY_PLAYERBOTS=OFF
MODULES=static
MODULE_TORTOISEBOTS=static
```

`BUILD_LEGACY_PLAYERBOTS` controls the separate legacy escape hatch; it is not
the native module selector.

Static module compile definitions/includes/PCH are isolated to the
TortoiseBots module target before it is folded into the combined modules
archive (local integration baseline; not yet upstream in #411 — separate
follow-up).

## 13. Configuration and database contract

The module owns:

```text
conf/tortoise_bots.conf.dist
ai/playerbot/aiplayerbot.conf.dist.in
data/sql/world/
data/sql/char/
```

Schema belongs in migrations, not surprise runtime DDL. Missing optional data
should fail closed or use an explicit supported fallback. Expensive travel/cache
generation must not start implicitly on the world thread.

The inherited AI config is broader than the currently accepted Turtle product;
a config key existing is not itself a support claim.

## 14. Turtle data contract

Turtle-specific legality/content should come from the target core/data where
possible:

- race/class legality from core player data;
- race/team identity from core data;
- start locations from `playercreateinfo`;
- Turtle spells/talents/items from local DBC/SQL;
- collection mounts from the target mapping;
- LFG/meeting-stone and taxi behavior from native core APIs.

Do not replace target data with old Vanilla tables when the target already owns
the answer.

## 15. Unsupported capabilities

When a donor behavior has no meaningful equivalent in the pinned core, adapt it
to a real API, remove/disable it, or fail closed. Do not return fake success
only to satisfy a donor interface.

The completed audit removed or disabled several such compatibility surfaces;
see [PLAYERBOTS_AUDIT.md](archive/PLAYERBOTS_AUDIT.md) for evidence.

## 16. LFT queue integration (optional, default-off)

`LftBotFillService` observes the copy-only generic LFT API from core PR #416
and never owns `m_queue`, offers, groups, or a second queue.
The service actually uses only `GetQueuedPlayers`, `QueuePlayer`, `LeaveQueue`,
`IsQueued`, `IsInOffer`, and `AcceptOffer`; core retains all offer,
acceptance, cancellation, and group-formation semantics. `AcceptOffer` is
called only for module-owned Headless participants; humans still accept
through the native addon path.

Candidates are filtered in memory by team, hardcore state, group/live state,
role, and the authoritative `Soromeister/LFT` v0.0.3.3 `LFT.allDungeons`
dungeon `code`/`minLevel`/`maxLevel` range (exact code and normalized display-name
aliases; see `runtime/LftBotFillService.cpp:FindDungeonLevelRange`).
Instance names are normalized through the small module alias table; unknown,
corrupt, and absent (Turtle-only/custom) ranges fail closed and are logged once. There is no average-human +/-5 approximation,
role hook, private-map access, addon-string injection, or DB query per tick.
Forced roles are cleared on pending exit paths, and reconciliation runs even
when the fill budget is zero.

Config: `AiPlayerbot.RandomBotLftEnabled=0`,
`AiPlayerbot.RandomBotLftUpdateInterval=15000`,
`AiPlayerbot.RandomBotLftMaxFillsPerInterval=1`.

## 17. AH market population (optional, default-off)

`AhMarketService` uses only the native auction transaction path: real bot
inventory items, `AhAction` pricing/usage values, `GetAuctionDeposit`,
`GetCheckedAuctionHouseForAuctioneer`, and
`WorldSession::HandleAuctionSellItem`. Core owns auction/item persistence,
deposits, limits, and ownership transfer. The service never writes auction
rows, fabricates items, or runs the donor `ahbot` thread/tables. No DB scan
per tick, no tick auction scan, no thread, no direct auction writes.

Auctioneer creature positions are captured once from the core object store,
validated for overworld/map/terrain/VMap ground and faction (no MMAP/pathfinding),
and used for a bounded teleport fallback before the native sell handler is invoked.
Active event-gated snapshot positions are not revalidated until restart/data reload.
A shared `try_lock`, 5..3600-second cadence, 1..5 batch cap, and per-bot attempt
cooldown bound world-thread work. Failed attempts are also rate-limited.

Fail-closed eligibility (world-thread read-only, no `m_queue` mutation): bots
with an active `PlayerbotAI` player master (`HasActivePlayerMaster`), any
grouped/manual-use bot (`Player::GetGroup`), LFT queued/in-offer
(`sLFTMgr.IsQueued`/`IsInOffer`, hard-requires core PR #416 `LFT/LFTMgr.h` — build fails with `#error` if absent, no silent fallback),
or inside a battleground/instance (`InBattleGround`/`InBattleGroundQueue`/
`Map::IsDungeon`/`IsBattleGround`) are never selected, posted, or teleported;
per-bot AH action stays independent and never pulls owned/party bots from players.

No per-tick AH/DB scan or new AH-specific core seam is required.
`AiPlayerbot.AhMarketEnabled=0` remains the default; the feature also requires
`RandomBotAutologin=1`.

## 18. Random-bot auto-create (optional, default-off)

`RandomBotService` discovers existing `RNDBOT*` characters; with
`AiPlayerbot.RandomBotAutoCreate=1` (default `0`, one character per
`RandomBotUpdateInterval`, world-thread) it creates the bounded deficit toward
`MinRandomBots`/`MaxRandomBots` through `AccountMgr::CreateAccount` (random
12-character alphanumeric password, hashed and never logged) and the generic
synchronous `CharacterCreation::CreateCharacter` seam (core PR #416). Core owns account/character persistence and validation; the module
never writes `account`/`characters` rows directly, uses no DB worker or donor
creation loop, and does no per-tick `LIKE` scan. Because `LoginDatabase` queues
account creation asynchronously after `AllowAsyncTransactions` (separate from core PR #416),
the service remembers exactly one successful account name whose id is not
immediately visible, retries that same name with bounded/log-throttled cadence
while continuing the existing-account selection path and without allocating
another fresh account (log once after prolonged unresolved period), and does not
allocate orphan accounts. DBC `ChrRaces`/`ChrClasses` and `PlayerInfo`
(`playercreateinfo`) are intersected before selection; permanent failures (mixed,
limit, materialization) are remembered, transient failures (`CHAR_CREATE_ERROR`,
dynamic `CHAR_CREATE_DISABLED`/`PVP_TEAMS_VIOLATION` via faction-balance, and
`LoginDatabase` allocation) back off with 60s throttling, and transient name
collisions (`CHAR_CREATE_NAME_IN_USE`/`CHAR_NAME_RESERVED`/`CHAR_NAME_PROFANE`/
`CHAR_CREATE_FAILED`) are retried silently with another candidate, so a healthy
account is not permanently poisoned by a single bad name or temporary balance
state. Created GUIDs enter the existing Headless candidate/login path.

## 19. Battleground auto-queue (optional, default-off)

`BattlegroundQueueService` provides bounded, demand-aware WSG/AB/AV participation
for live Headless random bots through the existing native
`WorldSession::HandleBattlemasterJoinOpcode` (guid 1337) for join and the existing
native `WorldSession::HandleBattleFieldPortOpcode` action 0 (`CMSG_BATTLEFIELD_PORT`
mapId+0, fail-closed `GetBattleGroundTemplate`/`GetMapId` validation) for
master-reclaim leave. Demand is read from the copy-only generic
`BattleGroundMgr::GetQueuedParticipants` snapshot (core PR #416): no human
waiting participant means no bot is queued, and a non-empty bucket selects its
queue type/bracket and underrepresented team. The core remains the owner of
queue state, invites, and port events; the module never mutates
`m_BattleGroundQueues`, calls `BattleGroundQueue::RemovePlayer` directly, owns a
second queue, starts a worker thread, or writes queue structures. Candidates are
selected in memory and checked for the native level bracket, queue slots,
alive/idle state, deserter/taxi/combat status, and active human master;
reconcile is guarded by `InBattleGround`, `(guid, queueType)` ownership,
`HasActivePlayerMaster` and `InBattleGroundQueueForBattleGroundQueueType` with
fail-closed map validation. AV is always queued solo and success is verified
after the native handler; WSG/AB group joins require every member to be a
service-owned Headless bot. Cadence and per-interval budget are clamped and the
setting defaults off (`RandomBotBgEnabled=0`). Requires core PRs #411 and #416.

## 20. New core seam test

Before adding another core seam, establish that:

1. the behavior cannot live entirely inside TortoiseBots;
2. no current generic hook/API exposes it;
3. the proposed seam is a real generic core concept, not a bot special case.

If the design would make PlayerBots-specific checks spread through normal
core gameplay code, redesign it.

## 21. Historical closure

F-03/F-27 closure and validation boundary are recorded in `PLAN.md` §6.1 and `PROVENANCE.md`; see `archive/PLAYERBOTS_AUDIT.md` for full evidence. This contract covers only the current host API.
