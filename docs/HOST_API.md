---
id: ref-host-api
title: Core Host Seams & Module API Contract
category: reference
summary: Technical specification of the generic C++ host seams, headless session lifecycle, packet bridge, and module integration contracts.
tags: [host-api, seams, c++, headless, packets, contract]
relates_to:
  - concept-architecture-invariants
  - concept-strategy-engine
---

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

The supported core target is merged upstream `main`, not a candidate branch:

```text
Core: tortoise-wow/tortoise-wow main @ 5fafe43b
      ("Merging headless session and module API expansion")
```

Every generic seam in this contract is merged upstream. The #411 and #416
candidates were superseded and closed; their combined surface landed through the
merged PRs below.

| Seam | Upstream |
| --- | --- |
| Transport plus `World` Headless lifecycle (`SessionTransport`, `InitHeadlessSession`, `StartHeadlessSession`, `StopHeadlessSession`, `GetHeadlessSessionState`) | [#438](https://github.com/tortoise-wow/tortoise-wow/pull/438) (supersedes [#411](https://github.com/tortoise-wow/tortoise-wow/pull/411)) |
| Generic participant primitives (`CharacterCreation::CreateCharacter`, copy-only `LFTMgr` queue API, `BattleGroundMgr::GetQueuedParticipants`) | [#438](https://github.com/tortoise-wow/tortoise-wow/pull/438) (supersedes [#416](https://github.com/tortoise-wow/tortoise-wow/pull/416)) |
| Module script hooks (`PlayerScript`, `WorldScript`) | [#469](https://github.com/tortoise-wow/tortoise-wow/pull/469) |
| Headless sessions drain synthesized client packets | [#475](https://github.com/tortoise-wow/tortoise-wow/pull/475) |
| Chat hooks hardened behind delivery checks; addon payloads no longer parsed as commands | [#476](https://github.com/tortoise-wow/tortoise-wow/pull/476) |
| `PlayerScript::OnChatYell` | [#493](https://github.com/tortoise-wow/tortoise-wow/pull/493) |

The pre-build gate is `tools/verify_penqle_host_contract.sh --core <core>`: a
read-only source check that the merged core still exposes the generic interfaces
this module calls, that legacy bot-object coupling has not returned to normal
gameplay code, and that prints the core revision it verified. It passes against
`5fafe43b`.

The last recorded compile-verified pair predates that merge and
must be refreshed at the next module build against merged `main`:

```text
Core:         e63161c2da7f13ab25687ea389026aa2e3c97647   (closed #411/#416 candidate)
TortoiseBots: b9c7784accb8c719e8d7aadd2f6a9e0bda8d07a2
```

The exact tested core/module pair must be recorded whenever the core changes;
do not infer compatibility from a branch name.

The world-update hook returns immediately while `AiPlayerbot.Enabled = 0`,
including when the AI configuration file is missing. Services are not ticked
against configuration or the managed-pool registry that disabled startup did
not initialize; periodic `BOTPERF` reporting and stale-hire recovery resume
only after restarting with AI enabled. Startup and shutdown diagnostics remain
available.

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
| `BotPlayerAdapter` | player lifecycle/reclaim attachment; answers core LFT managed-bot rolecheck (`IsManagedBot`/`GetBotRoles`); logs bot quest completions for `bot_events.csv` |
| `BotChatAdapter` | native `.bot` command integration |
| `BotPacketAdapter` | packet bridge into Existing PlayerBots (primarily AzerothCore/mod-playerbots) |
| `HireRecruiterAdapter` | `<Mercenary Hire>` recruiter gossip (`Hire bots` wizard + capital `World buffs` branch, issue #492; pure logic in `runtime/WorldBuffPolicy.h` + `runtime/WorldBuffService.h`) |
| `HireGroupAdapter` | hired-companion group hooks |
| `ClaimGuildAdapter` | `GuildScript::OnRemoveMember`/`OnDisband`: unclaims a claimed guild bot (issue #489) back to the wandering pool |
| `WorldBuffKillAdapter` | `PlayerScript::OnCreatureKill`: Onyxia/Nefarian → invisible Rally credit 95100 via `RewardPlayerAndGroupAtEvent` (issue #492) |
| `WorldBuffAuraAdapter` | `UnitScript::OnAuraApply`: DM/Sayge/Songflower aura → receiver-only event credit via `AreaExploredOrEventHappens`, recruiter casters ignored (issue #492) |
| `WorldBuffPvpAdapter` | `PlayerScript::OnPVPKill`: opposite-faction kill in Silithus (zone 1377) → Silithyst credit 95101 via `RewardPlayerAndGroupAtEvent` (issue #492) |
| `WorldBuffRaidAdapter` | `PlayerScript::OnBeforeTeleport` snapshot + `OnMapChanged` restore/strip + `OnLogout` memory cleanup for `KeepWorldBuffsInRaids` and Upper Karazhan 814 (issue #492; pure table in `runtime/WorldBuffRaidKeeper.h`) |
The module should prefer an existing generic hook before requesting a new core
seam.

## 8. World update

Bot AI runs on the normal world/game thread. The core exposes a generic world
update listener mechanism, and `BotHostAdapter` drives `BotManager` / AI,
module-owned `PlayerConvenience`, `AhMarketService`, and
`BattlegroundQueueService` updates from that tick.

The core listener is generic; it does not call a PlayerBots singleton.

Inside that pass (`BotManager::UpdateBots`) bots under a real player are
updated first and without a budget, then the random pool follows in a
resumable round-robin rotation; the pool's per-tick time budget only engages
once the previous world tick ran longer than
`AiPlayerbot.PoolBudgetWhenTickOverMs`
(see [docs/guides/configuration-tuning.md](guides/configuration-tuning.md)).
"Under a real player" means a live master with a network session, or a
character on an account the pool does not own (`runtime/PlayerBotClassification.h`);
the `random` flag and the `PlayerMaster` lease are not ownership signals.
This is module-internal scheduling and needs no core seam.

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
| Durable hire ledger | `HireLifecycle` (`tortoise_bots_hire`) |
| Durable claimed-guild-bot ledger | `ClaimLifecycle` (`tortoise_bots_claimed`, issue #489) |

A second owner for session lifetime, AI state, movement or master identity is an
architecture warning.

`BotRecord::InWorld` is bookkeeping, not command readiness. `BotManager` only
publishes a bot through its controllable/live snapshots when the Player has an
active module-owned Headless session, the `PlayerbotAIAdapter` is usable, and
the same `PlayerbotAI` is registered in `PlayerbotAIStorage`. An attach failure
marks the record for removal and stops the Headless session instead of leaving
an apparently online but inert bot.

## 10. Packet bridge and chat seams

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

Synthesized client packets (gameobject use, open/use item, chat) use the
core receive queues: headless sessions drain them via `CanProcessPackets:IsHeadless`
(core #475). This runs the canonical `ProcessPackets` wrapper (script receive
hooks, flood accounting, per-update cap, `ExecuteOpcode` teleport boundary).
Note: core stamps `packetTime` only for perflog profiling — it does not
`FillPacketTime`, so movement opcodes with `m_recvdTime == 0` remain subject to
`MovementHandler` reject-time drops.

The recorded fixture exercised Headless outgoing delivery, Network-master
outgoing delivery and the existing group-invite Trigger -> Action acceptance
path. Real-client incoming delivery remains a separate manual-client acceptance
boundary.

### Chat and addon-message seams (core #476)

A non-addon chat message that carries a body runs
`ProcessChatMessageAfterSecurityCheck` before its destination case:
`CheckChatMessageValidity`, `PLAYERHOOK_ON_BEFORE_SEND_CHAT_MESSAGE`, then
`ChatHandler::ParseCommands`. Synthesized bot speech (`SAY`, `YELL`, `PARTY`,
`GUILD`, …) that begins with `.` or `!` is therefore still parsed as a server
command; #476 removed addon payloads from that path only.
`PlayerbotAI::SanitizeCommandLikeChat` prepends one space to bot speech starting
with `.` or `!` (literal `..` / `!!` are left alone) and remains required by
design.

`LANG_ADDON` messages no longer reach the command parser:

- `CHAT_MSG_CHANNEL` runs `CheckChatMessageValidity` instead of
  `ProcessChatMessageAfterSecurityCheck`. Channel authorization — membership,
  level, moderation and mute checks — still runs in the destination case.
- `PARTY`, `GUILD`, `OFFICER`, `RAID`, `RAID_LEADER`, `RAID_WARNING`,
  `BATTLEGROUND` and `BATTLEGROUND_LEADER` call
  `sScriptMgr.OnAddonMessage(_player, msg)` after their destination and
  permission checks. Returning `true` consumes the message: it is neither
  relayed nor written to the chat log.
- `SAY`, `YELL`, `EMOTE`, `WHISPER` and `CHANNEL` dispatch no
  `OnAddonMessage`; `WorldSession::IsLanguageAllowedForChatType` (reached
  through `CheckChatMessageValidity`) rejects `LANG_ADDON` for every type except
  the eight above plus `CHANNEL`.
- Turtle's own addon protocols (`HandleTurtleAddonMessages`: LFT, custom
  merchant, guild bank, honor and threat requests) run before the destination
  switch and consume matching `LANG_ADDON` payloads first; a module addon
  protocol needs a prefix none of them claim.
- `LANG_ADDON` chat is forced onto the world thread
  (`WorldSession::GetChatPacketProcessingType`), so addon hooks dispatch with
  the same threading as the rest of the command path, and
  `CONFIG_BOOL_ADDON_CHANNEL=0` drops the message before any module sees it.

The remaining chat hooks fire on real delivery only:

| Hook | Delivery condition |
| --- | --- |
| `PLAYERHOOK_ON_CHAT_WHISPER` | whisper was allowed (`allowSendWhisper`) and is not `LANG_ADDON`; also fires for non-addon party text |
| `PLAYERHOOK_ON_CHAT_GUILD` | sender is in a guild and the language is not `LANG_ADDON` |
| `PLAYERHOOK_ON_CHAT_SAY` | language is not `LANG_ADDON` |
| `PLAYERHOOK_ON_CHAT_YELL` | language is not `LANG_ADDON` and the session is not fingerprint-banned ([#493](https://github.com/tortoise-wow/tortoise-wow/pull/493)) |
| `PLAYERHOOK_ON_TEXT_EMOTE_HEARD` | the emote resolved to a unit within `CONFIG_FLOAT_LISTEN_RANGE_TEXTEMOTE` |
| `PLAYERHOOK_ON_CHAT_CHANNEL` | non-addon channel message from a session that is neither muted nor banned; dispatched inside `Channel::Say` |

Threading caveat: `Channel::Say` runs on `ChannelBroadcaster::ThreadProc`, not
the world thread (`Channel::AsyncSay` therefore fires the generic
`WORLDHOOK_ON_CHANNEL_BROADCAST` on the caller's thread instead). Until PR
[#498](https://github.com/tortoise-wow/tortoise-wow/pull/498) moves
`PLAYERHOOK_ON_CHAT_CHANNEL` back onto the world thread, a module channel hook
must not touch world state.

TortoiseBots consumes one chat hook: `BotAddonAdapter` registers
`PLAYERHOOK_ON_ADDON_MESSAGE` for the `TBM` prefix (below). `BotChatAdapter`
owns the generic `AllCommandScript` entry for `.bot`, and `BotPlayerAdapter`
registers `PLAYERHOOK_ON_LOGIN`, `PLAYERHOOK_ON_MAP_CHANGED`,
`PLAYERHOOK_ON_PLAYER_COMPLETE_QUEST`, `PLAYERHOOK_ON_BEFORE_LOGOUT` and
`PLAYERHOOK_ON_LOGOUT`. No module code
registers `OnChatChannel`; do not add it until #498 is merged.

#### Addon command transport

The companion addon sends its UI commands over the addon channel once the
server says it can:

```text
client -> server   addon message, prefix "TBM", body "<verb> [args]"
                   (same grammar as `.bot <verb> [args]`)
server -> client   addon message, prefix "TBM", one line per command reply
                   (the same lines the chat path sends as CHAT_MSG_SYSTEM —
                   the same `TBM:` protocol lines a hand-typed `.bot`
                   command also prints in chat)
server -> client   "TBM:TRANSPORT|party" or "TBM:TRANSPORT|none", trailing the
                   roster response
```

`BotAddonAdapter` consumes `TBM\t...` on the `PARTY` and `RAID` destinations,
forwards the payload to the single `.bot` command entry, and returns `true`, so
the core neither relays the request nor writes it to the chat log. Replies are
delivered by a `ChatHandler` that overrides the virtual `SendSysMessage`, so
every command reply — human text and structured `TBM:` lines alike — becomes an
addon message instead of a yellow `CHAT_MSG_SYSTEM` line. Authorization is
unchanged: the same command layer checks ownership and GM status.

`TBM:TRANSPORT` is per-group state, not a capability flag. It reads `none` when
the requester's group is a battleground group with no pre-battleground group,
because the core drops those addon messages before the module hook
(`WorldSession::HandleMessagechatOpcode`); the addon keeps using `.bot` chat
whenever the verdict is missing, stale, or `none`.

## 11. Command contract

TortoiseBots owns the native `.bot` surface. Current commands include
(see [Available Bot Commands & Addon Controls](guides/player-controls.md) for
the complete player-facing list):

```text
add
remove
logout
roster
action attack|interrupt|stop|pull|pullback|come|stay|follow|flee
action focus skull
action cc <raid-mark> [bot]  # star/circle/diamond/triangle/moon/square/cross/skull (exclusive per-bot ownership; explicit name wins over target; unknown name -> ACTION_ERR no-bot; ACK scope bot:<Name>)
action cc clear [bot]  # dismiss ownership: named bot, targeted bot, or whole owned party; ACK scope bot:<Name> or party
action auto cc [on|off]  # opt-in smart auto CC (OFF default); per-bot persisted strategy toggle; ACK on|off|mixed
action aoe [on|off]
inv <bot>  # TBM:INV_BEGIN/INV_EQ/INV_ITEM/INV_END inventory snapshot
item <bot> trade | equip|unequip|give <bag> <slot>  # item order by server bag/slot; ACK/ERR intent "item <op>"
behavior <bot> <loot|aoe|autocc|savemana|boost|threat|potions> <on|off>  # allowlisted per-bot strategy toggle; ACK + TBM:BOTSTATE line
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

The surface has two transports: chat (`.bot <verb>` in a normal chat type) and
the addon command channel (§10, prefix `TBM`). Both run through the same parser,
the same authorization and the same replies; the addon transport exists so the
companion UI does not print request or reply lines into the chat frame.

`.bot roster` reads the requester's undeleted account characters and any
explicit cross-account ownership rows from the module-owned durable table. It
emits the stable six-field `TBM:ROSTER_BEGIN`, `TBM:ROSTER`, and
`TBM:ROSTER_END` system-message stream, followed by a separate
`TBM:CC_ASSIGN_BEGIN`, `TBM:CC_ASSIGN`, `TBM:CC_ASSIGN_END` stream for live
AI assignments, then a `TBM:BOTSTATE_BEGIN`, `TBM:BOTSTATE|<bot>|move=…,loot=on,…`,
`TBM:BOTSTATE_END` stream with the movement mode and behaviour toggles of every
controllable party bot (owned alts and hired companions). Keeping this metadata
in separate streams means an older addon can still consume the roster unchanged. The roster remains the source of truth for
offline and online owned rows; runtime `BotManager` records remain transient
Headless lifecycle state.

`.bot action` builds one request context from the requester's normal target and
group. Dynamic actions resolve to the targeted controllable owned bot or the
controllable party bots. Interrupt is an executor action: it probes the mature
class/pet action graph for a ready interrupt whose spell data can interrupt the
target's active cast, then executes it or queues the existing reach action.
Pull and Pullback both use the mature `PullStrategy`
but select different existing policy state: ordinary Pull removes `pull back`,
while Pullback enables its return-to-pull-position trigger. CC resolves a
requested raid mark and a suitable executor server-side. An explicit bot name
(`cc <mark> <Bot>`) assigns that owned live bot directly, winning over the
live target; unknown or uncontrollable names fail with `no-bot`. Otherwise
targeting an owned bot sets that bot's persistent `rti cc` preference, and
targeting an enemy (or an existing group mark) lets the server select a
capable executor and immediately attempt the mature CC action. Mark ownership
is exclusive: assigning a mark resets every other owned live party bot holding
it to `none` and persists the change. `action cc clear [bot]` dismisses
ownership (named bot, else targeted bot, else the whole owned party when no
bot is targeted); `none` is reported as `-` in the `TBM:CC_ASSIGN` snapshot.
Executor discovery walks the registered mature
CC actions, so Hunter traps/beast control, Paladin Turn Undead, Rogue Sap, and
the other class actions remain eligible without a second class policy table.
Among the bots whose mature action is usable, the best spell fit wins (Sap on an
unengaged target, then Shackle Undead, Banish, Hibernate, Polymorph, Freezing
Trap, Turn Undead, Scare Beast, Entangling Roots, and Fear last). Ties go to the
lowest bot GUID, so the choice never depends on party invite order (issue #58).
Assignment is persisted even when the current marked creature is not legal for
the selected bot; the immediate cast is best-effort and normal AI fallback
remains available. Inside non-raid dungeons the generic CC triggers fire only
on the bot's assigned mark; AoE triggers refuse packs holding a breakable CC.
Addon requests receive one structured
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

Static module compile definitions, include paths and the compatibility PCH are
applied from this repository's `TortoiseBots.cmake` through the core's module
CMake phase hooks (`TORTOISE_MODULE_CMAKE_PHASE`: `DISCOVERY`, `POST_TARGETS`);
the module keeps this setup out of the core build files.

## 13. Configuration and database contract

The module owns:

```text
conf/tortoise_bots.conf.dist
ai/playerbot/aiplayerbot.conf.dist.in
data/sql/world/
data/sql/character/
```

Schema belongs in migrations, not surprise runtime DDL. Missing optional data
should fail closed or use an explicit supported fallback. Expensive travel/cache
generation must not start implicitly on the world thread.

The inherited AI config is broader than the currently accepted Tortoise product;
a config key existing is not itself a support claim.

## 14. Tortoise data contract

Tortoise-specific legality/content should come from the target core/data where
possible:

- race/class legality from core player data;
- race/team identity from core data;
- start locations from `playercreateinfo`;
- Tortoise spells/talents/items from local DBC/SQL;
- collection mounts from the target mapping;
- LFG/meeting-stone and taxi behavior from native core APIs.

Do not replace target data with old Vanilla tables when the target already owns
the answer.

## 15. Unsupported capabilities

When a donor behavior has no meaningful equivalent in the pinned core, adapt it
to a real API, remove/disable it, or fail closed. Do not return fake success
only to satisfy a donor interface.

The completed audit removed or disabled several such compatibility surfaces;
evidence is preserved in Git history and `PROVENANCE.md`.

## 16. LFT queue integration (optional, default-on)

`LftBotFillService` observes the copy-only generic LFT API merged with the
participant primitives ([#438](https://github.com/tortoise-wow/tortoise-wow/pull/438))
and never owns `m_queue`, offers, groups, or a second queue.
The service actually uses only `GetQueuedPlayers`, `QueuePlayer`, `LeaveQueue`,
`IsQueued`, `IsInOffer`, and `AcceptOffer`; core retains all offer,
acceptance, cancellation, and group-formation semantics. `AcceptOffer` is
called only for module-owned Headless participants (fill-owned bots in
`m_pending`, plus a human's own non-random party bots whose master/group
leader is a real player in the same offer); humans still accept
through the native addon path.

Candidates are filtered in memory by team, hardcore state, group/live state,
role, and the authoritative `Soromeister/LFT` v0.0.3.3 `LFT.allDungeons`
dungeon `code`/`minLevel`/`maxLevel` range (exact code and normalized display-name
aliases; see `runtime/LftBotFillService.cpp:FindDungeonLevelRange`).
Instance names are normalized through the small module alias table; unknown,
corrupt, and absent (Tortoise-only/custom) ranges fail closed and are logged once. There is no average-human +/-5 approximation,
role hook, private-map access, addon-string injection, or DB query per tick.
Forced roles are cleared on pending exit paths, and reconciliation runs even
when the fill budget is zero.

Config: `AiPlayerbot.RandomBotLftEnabled=1` (set `0` to opt out),
`AiPlayerbot.RandomBotLftUpdateInterval=15000`,
`AiPlayerbot.RandomBotLftMaxFillsPerInterval=1`.
Random fill is demand-driven only: no human waiting means queued fill bots are
pulled back out. A human queuing with their own party bots works with the
switch on or off: the core auto-answers the rolecheck from the rolecheck hook
(`BotPlayerAdapter::GetBotRoles` — explicit forced role first, then named
combat specs, then `AiFactory` spec/gear fallback; ambient `tank assist` /
`dps assist` ignored) and the service auto-accepts the offer for managed non-random bots whose master/group
leader is a real player in the same offer, on the existing update cadence —
no per-bot-tick world scans, no fill leases or forced roles for party bots, and
fill-owned entries are never touched by the party path. So the addon role/spec
buttons drive the rolecheck (tank button via `.bot role`, the rest via named
combat specs). No teleport or summon
exists on either path: after the group forms the party walks (follow) to the
portal, so `.bot summon` stragglers first.

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
(`sLFTMgr.IsQueued`/`IsInOffer`, hard-requires the merged `LFT/LFTMgr.h` (#438) — build fails with `#error` if absent, no silent fallback),
or inside a battleground/instance (`InBattleGround`/`InBattleGroundQueue`/
`Map::IsDungeon`/`IsBattleGround`) are never selected, posted, or teleported;
per-bot AH action stays independent and never pulls owned/party bots from players.

No per-tick AH/DB scan or new AH-specific core seam is required.
`AiPlayerbot.AhMarketEnabled=0` remains the default; the feature also requires
`RandomBotAutologin=1`.

## 18. Random-bot auto-create (optional, default-off)

`RandomBotService` discovers the characters of **registered managed pool
accounts** (character-database table `tortoise_bots_pool_account`, seeded by
`data/sql/character/20260924120000_char.sql`); with
`AiPlayerbot.RandomBotAutoCreate=1` (default `0`, one character per
`RandomBotUpdateInterval`, world-thread) it creates the bounded deficit toward
`MinRandomBots`/`MaxRandomBots` through `AccountMgr::CreateAccount` (random
12-character alphanumeric password, hashed and never logged) and the generic
synchronous `CharacterCreation::CreateCharacter` seam (merged #438). Core owns account/character persistence and validation; the module
never writes `account`/`characters` rows directly, uses no DB worker or donor
creation loop, and does no per-tick `LIKE` scan. Because `LoginDatabase` queues
account creation asynchronously after `AllowAsyncTransactions` (independent of #438),
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

## 18.1 Managed pool registry and startup reset (optional, default-off)

`RandomBotAccountRegistry` (runtime/) is the authority for pool identity: an
account is a pool account only if it has a registry row, and the row is
validated against the login database (existence **and** username) before any
destructive use. `AiPlayerbot.RandomBotAccountPrefix` is used only to name new
module-created accounts and to list legacy accounts for explicit console
adoption (`bot pool adopt preview` / `confirm`); a prefix match never authorizes
deletion. Registration is written and read back before a character is created
on the account, so an account the module cannot prove it registered is not used.
The registry also owns the managed-account scope used by reporting and by the
reset (`AccountIdList`, `CountManagedCharacters`), and a failed read is never
reported as "no accounts": the adoption preview aborts and clears its pending
challenge instead of enrolling a set whose size it cannot state. The observability
daemon joins the same table for its armory views, so a personal account whose
name merely looks like a bot account is not shown as a bot there either.

`RandomBotPoolReset` (runtime/) executes `AiPlayerbot.RandomBotPoolReset` =
`off` | `once:<token>` | `always` during initial world startup only
(`BotHostAdapter::OnStartup` -> `PlayerbotAIConfig::Initialize` ->
`RandomBotService::Initialize`). It deletes one snapshotted character per world
tick through `Player::DeleteFromDB(guid, accountId, true, true)`, verifies that
no managed character remains, and records the generation in
`tortoise_bots_pool_state` **only after** deletion and verification succeed, so
an interrupted reset resumes on the next start. `.reload config` never starts a
reset and no live reset command exists.

Ordering relative to world startup and headless sessions: the plan is built
after the module's world-startup hook, so the database and DBC stores are
loaded. The reset then runs `Planning` (drain every pool session with
`BotManager::RemoveBot(guid, false)` plus `HireLifecycle::Forget`),
`SettlingAuctions` (settle every target-owned listing, one character per tick,
while all pool bidders still exist — a pool bot bidding on another pool bot's
auction would otherwise be gone by the time that listing is reached), and only
then `Deleting`. A character whose session has not closed is skipped, and a
session that refuses to close within a bounded timeout fails the reset instead
of deleting a live character. Human
network sessions are never logged out or deleted — a pool character being played
by a player aborts the reset. While maintenance is active (or after a failure)
`RandomBotService::IsPoolAvailable()` is false, which pauses auto-create,
autologin, pinned resolution, hiring, and battleground selection. Availability
requires **both** a validated registry and no active/failed reset, so an
unreadable registry never lets hiring create and register a new pool account on
top of a known-invalid one; the free-alt scan is skipped in that state too, so
existing pool accounts cannot be reclassified as personal alts. A reset whose
snapshot is empty (no registered accounts, or none with characters) is a
verified no-op that still records the one-shot generation.

Related-data handling keeps the core bot-agnostic and needs **no core change**:

- **Auctions** are settled through the existing public auction interfaces
  (`sAuctionHouseStore` -> `sAuctionMgr.GetAuctionsMap` -> `AuctionHouseObject`):
  an active bidder is refunded with the core's cancelled-to-bidder mail
  (`MailDraft(...).SetMoney(bid).SendMailTo(MailReceiver(...), auction, ...)`),
  the item is removed from the in-memory item map and `item_instance`, and the
  listing is removed from memory and the `auction` table. No raw SQL is used for
  auctions, so the in-memory auction manager cannot desynchronise. A bid whose
  bidder can no longer be resolved fails the reset **before** the listing is
  touched; a hardcore bidder is skipped exactly like the core's own cancel path
  (`Player::IsHardcore` / the cached `HARDCORE_MODE_STATUS_ALIVE|DEAD|HC60`
  status, which excludes `IMMORTAL`). Settlement runs in its own phase before
  any deletion, so recovery never depends on a bidder that the reset already
  deleted; the deletion step re-checks that the character owns no listing and
  stops the reset if one appeared after settlement.
- **Guilds** are preflighted from `guild`/`guild_member`: deleting a bot that
  leads a guild promotes another member or disbands an empty guild, so a
  bot-led guild holding any character outside the managed pool aborts the reset
  before the first deletion.
- **Module rows** for each deleted character (`ai_playerbot_db_store`,
  `ai_playerbot_custom_strategy`, `tortoise_bots_owned_character`,
  `tortoise_bots_armory_stats`, `tortoise_bots_hire`, `tortoise_bots_claimed`) are removed explicitly —
  the shared `CharacterCleanup` helper — and re-verified; character-owned data
  (inventory, mail, pets, groups, instances, petitions, guild membership) stays
  with `Player::DeleteFromDB`.

## 18.2 Hired companions are temporary (optional, default-on with hiring)

A hired companion is a **temporary character**, not a pool bot with extra
steps: it lives while its master is online plus
`AiPlayerbot.HireDisconnectGracePeriod` seconds after a disconnect, and when the
hire ends its character is deleted (`.bot hire`, `<Mercenary Hire>`, and the
`Hire*` knobs are described in [Player Controls](guides/player-controls.md)).
`HireLifecycle` (runtime/) owns that lifecycle end to end:

- **Ledger.** `HireLifecycle::Claim` writes one `tortoise_bots_hire` row per
  hire, before the fee is charged, for a character `HireProvisionService`
  itself created with `CharacterCreation::CreateCharacter`. A hire never adopts
  an existing character: a roaming pool bot is never consumed, and a player's
  own character is never taken over (the earlier candidate-reuse paths are
  gone). The row is therefore the only proof that a character was created by
  Hire(), and the deletion guard refuses without it.
- **Ending the hire.** Every ending path funnels through
  `HireLifecycle::Dismiss`: removal from the group (`HireGroupAdapter` group
  hooks), the **master** leaving the party while online (the group hooks dismiss
  the master's hires; a 5 s `SweepGracePeriod` backstop catches a departure no
  hook reported, e.g. a raid removal — a hire still being provisioned and a
  logged-out master are excluded, so logout keeps the grace period), group
  disband, master grace expiry (`SweepGracePeriod`), explicit
  `.bot remove`/`.bot logout` (`DismissNow`), and a vanished runtime record. The
  ledger row is marked `dismissed` first, the master binding and activity lease
  are released, and `BotManager::RemoveBot(guid, false)` logs the character off.
- **Deletion.** A live character cannot be deleted, so the actual wipe is
  asynchronous: `HireLifecycle::Update` pumps the queue on **every world tick**
  and processes one queued character per pass once its Headless session is gone
  (the same bounded step the pool reset's deletion phase uses), so a kicked
  party of four — or the stale hires recovered after a restart — is gone within
  ticks. A session that refuses to close is retried with a bounded budget: after
  30 s the module issues an explicit `BotSessionAdapter::StopHeadlessSession`
  once, and after 120 s it stops waiting and leaves the character alone (the
  ledger row stays `dismissed`, so the next server start recovers it) instead of
  polling and logging forever. Before deleting it revalidates the
  ledger row, that the account is a registered managed pool account
  (`tortoise_bots_pool_account`), and that `characters.account` still matches
  the ledger (`HireDeletionPolicy`, unit-tested in
  `tools/test_hire_deletion_policy.cpp`). Auctions are settled through
  `CharacterCleanup` first (the core's `Player::DeleteFromDB` does not touch
  listings); the core then removes the character, its group, guild/petition
  membership, items, pets and mail, and the module rows go with it. A character
  whose account no longer matches, or that a human session owns, is never
  deleted — it stays and the refusal is logged. While a managed-pool reset is
  active the queue waits it out (the reset settles every auction with all pool
  bidders present and deletes the same characters itself).
- **Crash safety.** No hire survives a restart: hire state is in-memory, so a
  fresh process recovers **every** `tortoise_bots_hire` row on its first ticks
  and deletes those characters (`state` is set to `dismissed` before the
  deletion, and the row is removed with the character). A crash between the
  logout and the deletion therefore cannot leave an orphan hire in the pool, and
  a master cannot be reunited with a companion after a restart — hiring again
  means paying the fee again.
- **Population accounting.** Deleting a hire frees one character slot on a
  managed pool account. The pool target (`MinRandomBots`/`MaxRandomBots`) is
  unaffected: the freed slot is refilled by the same bounded auto-create pass
  that fills any other missing pool character. A character whose ledger row is
  `dismissed` is **not** counted against the per-account character limit when
  immediately reuses the freed slot instead of being pushed onto a brand-new
  pool account (or rejected while the deletion queue drains).

## 18.3 Claimed guild bots are protected pool alts (issue #489)

A wandering pool bot invited into a player's guild via `/ginvite` becomes a
**claimed guild bot**: it stays on its pool account (avoiding the
10-character player account cap) but is owned by the claiming player for
control, gear, and raid assembly. `ClaimLifecycle` (runtime/) owns the
durable `tortoise_bots_claimed` ledger end to end:

- **Claim.** `GuildAcceptAction` records `(bot_guid, owner_account_id,
  owner_player_guid, guild_id)` when a random pool bot accepts a real
  player's guild invite. Policy gates live in `runtime/ClaimedBotPolicy.h`
  (unit-tested in `tools/test_claimed_bot_policy.cpp`).
- **Gear lock.** Claimed bots at level 60 never self-equip upgrades
  (`EquipAction`), never run synthetic gear updates (`UpdateGearAction`,
  `RandomBotService`/`BotManager` seeding), and never auto-vendor
  (`SellAction`). Below 60 they equip upgrades but preserve blue/epic items.
- **Control.** Guild members control claimed bots (`BotCommandContext`,
  `PlayerbotSecurity`); the roster streams a `TBM:CLAIMED_*` snapshot plus a
  `claimed-roster` capability for the addon's Guild tab.
- **Protection.** Claimed guids are excluded from pool-reset snapshots and
  managed-character counts (`RandomBotPoolReset`, `RandomBotAccountRegistry`);
  `CharacterCleanup` drops the claim row with a deleted character, and
  `ClaimGuildAdapter` unclaims on guild removal/disband, returning the bot to
  the wandering pool.

## 19. Battleground auto-queue (optional, default-on)

`BattlegroundQueueService` provides bounded, demand-aware WSG/AB/AV participation
for live Headless random bots through the existing native
`WorldSession::HandleBattlemasterJoinOpcode` (guid 1337) for join and the existing
native `WorldSession::HandleBattleFieldPortOpcode` action 0 (`CMSG_BATTLEFIELD_PORT`
mapId+0, fail-closed `GetBattleGroundTemplate`/`GetMapId` validation) for
master-reclaim leave. Demand is read from the copy-only generic
`BattleGroundMgr::GetQueuedParticipants` snapshot (merged #438): no human
waiting participant means no bot is queued, and a non-empty bucket selects its
queue type/bracket and underrepresented team. A waiting participant counts only
when its session is non-headless and it is not a random bot, so neither random
fill bots nor a human's own headless party bots create demand. The core remains
the owner of queue state, invites, and port events; the module never mutates
`m_BattleGroundQueues`, calls `BattleGroundQueue::RemovePlayer` directly, owns a
second queue, starts a worker thread, or writes queue structures. Candidates are
selected in memory and checked for the native level bracket, queue slots,
alive/idle state, deserter/taxi/combat status, and active human master;
reconcile is guarded by `InBattleGround`, `(guid, queueType)` ownership,
`HasActivePlayerMaster` and `InBattleGroundQueueForBattleGroundQueueType` with
fail-closed map validation, so a human's own party bots — never queued by the
service and never holding its `BgQueued` lease — are left alone. AV is always
queued solo and success is verified after the native handler; WSG/AB group joins
require every member to be a service-owned Headless bot. Cadence and per-interval
budget are clamped and the setting defaults on (`RandomBotBgEnabled=1`; set `0`
to opt out). Requires the merged session and participant primitives (#438).

A human queuing at the battlemaster with **Join as Group** alongside their own
managed bots needs no service involvement: the core group-join path accepts
headless members, each member receives the queue/invite status, and every bot's
existing `bg status` packet action auto-accepts `STATUS_WAIT_JOIN` and runs the
BG strategies (`follow` is dropped inside the battleground). AV group joins are
rejected by the core, so AV parties queue solo.

## 20. New core seam test

Before adding another core seam, establish that:

1. the behavior cannot live entirely inside TortoiseBots;
2. no current generic hook/API exposes it;
3. the proposed seam is a real generic core concept, not a bot special case.

If the design would make PlayerBots-specific checks spread through normal
core gameplay code, redesign it.

## 21. Historical closure

F-03/F-27 closure and validation boundary are recorded in `PROVENANCE.md`; full historical audit evidence is preserved in Git history. This contract covers only the current host API.
