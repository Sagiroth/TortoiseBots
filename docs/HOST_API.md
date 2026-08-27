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
Pinned Tortoise core checkpoint:     7353989c94399f80572a2f8ec2eb73c63a6c79f8 (historical pin; branch `cleanup/f03-f27-code-freeze`)
TortoiseBots tested code checkpoint: 07cf7976c546fac27083c7b46e73299c25b095f3
```

Validated local core checkpoint:
`7353989c94399f80572a2f8ec2eb73c63a6c79f8` (historical branch `cleanup/f03-f27-code-freeze`)

Upstream status:
generic Headless capability proposed as draft PR [#411](https://github.com/Penqle/tortoise-wow/pull/411)
(`feature/headless-world-session` @ `c37e28b`, based on upstream `main`
`61a8269`; merge-base `93a5faa`). Not yet merged; the pinned branch remains the validated baseline until #411 lands.

When the core changes, record the exact tested core/module pair
(e.g. in the commit message and README) instead of assuming compatibility from a branch name.

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
`World` owns both Network and Headless `WorldSession` lifetime.

The generic Headless lifecycle supports queue/register, GUID lookup, pending
inspection/cancellation, removal and shutdown cleanup. Headless sessions do not
become extra real Network sessions for the normal account-session map.

TortoiseBots owns lifecycle records and AI adapters, not `WorldSession*`
lifetime.

## 5. Async login dispatch

Async login state carries identity instead of a retained raw session pointer:

```text
accountId
characterGuid
SessionTransport
```

Completion resolves the appropriate registry:

```text
Network  -> account-keyed session
Headless -> character-GUID-keyed session
```

This keeps one Network master plus multiple Headless characters unambiguous.
`BotSessionAdapter` queues the Headless session and then uses the normal
Tortoise character-login path rather than duplicating player loading or map
entry.

## 6. Human reclaim

A real Network session takes precedence when the same character returns under
human control. The core performs normal session/player lifecycle work;
TortoiseBots releases or rebinds its record/AI state as appropriate.

The durable master relationship remains module-owned. The core only owns the
generic session/player lifecycle.

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
update listener mechanism, and `BotHostAdapter` drives `BotManager` / AI updates
from that tick.

The core listener is generic; it does not call a PlayerBots singleton.

## 9. Ownership model

| Responsibility | Owner |
| --- | --- |
| Network `WorldSession` lifetime | Tortoise `World` |
| Headless `WorldSession` lifetime | Tortoise `World` |
| Pending Headless queue | Tortoise `World` |
| Bot record lifecycle | `BotManager` |
| AI lifetime | `PlayerbotAIAdapter` |
| AI lookup | `PlayerbotAIStorage` |
| Gameplay decisions | `PlayerbotAI` |
| Movement semantics | Existing PlayerBots (primarily AzerothCore/mod-playerbots) actions/strategies |
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
follow
invite
uninvite
stay
list
stats
command
help
```

`.bot command` delegates to `PlayerbotAI::HandleCommand` for Existing PlayerBots (primarily AzerothCore/mod-playerbots)
command behavior. Authorization uses the normal account/GM policy implemented
by the module/core boundary.

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

## 17. Random-bot auto-create (optional, default-off)

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

## 18. New core seam test

Before adding another core seam, establish that:

1. the behavior cannot live entirely inside TortoiseBots;
2. no current generic hook/API exposes it;
3. the proposed seam is a real generic core concept, not a bot special case.

If the design would make PlayerBots-specific checks spread through normal
core gameplay code, redesign it.

## 18. Historical closure

F-03/F-27 closure and validation boundary are recorded in `PLAN.md` §6.1 and `PROVENANCE.md`; see `archive/PLAYERBOTS_AUDIT.md` for full evidence. This contract covers only the current host API.
