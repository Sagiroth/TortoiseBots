# PLAN.md — TortoiseBots architecture and roadmap

**Status:** Active architecture and roadmap
**Target:** Tortoise WoW 1.18.1 / Penqle core
**Primary goal:** Useful PlayerBots for Turtle 1.18.1 without rebuilding the old tightly coupled PlayerBots core architecture.

## 1. Product goal

TortoiseBots should let a small number of human players use owned bots for
normal world content and 5-player dungeons, then grow deliberately into broader
class, dungeon, raid, battleground, random-bot and optional conversation
features.

The architectural goal is not simply “port PlayerBots to Tortoise”. It is:

```text
give Tortoise a clean optional bot platform
+
reuse existing PlayerBots behavior (primarily AzerothCore/mod-playerbots) without inheriting its coupling
```

The core should not need to understand bot strategies, rotations, classes,
travel logic, dungeon logic, personalities or LLM behavior.

## 2. Source of truth

For Turtle-specific spells, items, races, locations, scripts, start rows,
travel and gameplay contracts, use this order:

1. current pinned Tortoise core;
2. current Turtle SQL / DBC / extracted data;
3. observed runtime behavior and logs;
4. Turtle-specific references such as Shyalya;
5. Vanilla/CMaNGOS/mod-playerbots/MangosZero for comparison and existing behavior.

Do not infer Turtle 1.18.1 behavior from Vanilla or donor code when the target
core/data can answer the question.

The TortoiseWoW Knowledge Base is a behavioral/capability reference, not an
instruction to recreate its implementation architecture.

## 3. Non-negotiable architecture

### 3.1 PlayerBots stays optional

The Tortoise core must build and run without this module checkout.

Native module selection is explicit through:

```text
MODULE_TORTOISEBOTS=static   # or shared where supported
```

The legacy vendored PlayerBots path is separate and should remain disabled:

```text
BUILD_LEGACY_PLAYERBOTS=OFF
```

No PlayerBots runtime/config/SQL dependency may become mandatory for a normal
server build.

### 3.2 No legacy bot identity in normal core gameplay

Do not reintroduce:

```text
WorldSession::GetBot()
WorldSession::SetBot()
m_bot
sPlayerBotMgr
PlayerBotEntry
scattered if (IsBot()) / if (GetBot()) gameplay branches
```

Normal `Player`, `Unit`, `Spell`, movement, group and map code should not need to
know that a player is controlled by TortoiseBots.

If a feature requires bot-specific logic in unrelated core gameplay code, stop
and redesign the boundary before continuing.

### 3.3 Headless is a transport/session capability

The core may understand generic concepts such as:

```text
Network session
Headless session
HasNetworkTransport()
CanReceiveClientPackets()
```

The module interprets a Headless session as a PlayerBots-controlled character.
The core should not ask whether the session belongs to a bot.

### 3.4 Prefer module-only changes

Before modifying core code, ask:

1. Can the behavior live entirely inside TortoiseBots?
2. Does an existing ScriptMgr/lifecycle/packet/command hook already expose what
   is needed?
3. If not, can the missing capability be expressed as a genuinely generic core
   concept?

Only add a core seam when the current core cannot expose the behavior cleanly.
Keep unavoidable host integration centralized and small.

### 3.5 One owner per responsibility

The current ownership model is:

| Responsibility | Owner |
| --- | --- |
| Network session lifetime | Tortoise `World` |
| Headless session lifetime | Tortoise `World` |
| Pending Headless login queue | Tortoise `World` |
| Bot records / lifecycle state | `BotManager` |
| AI lifetime | `PlayerbotAIAdapter` |
| AI lookup | `PlayerbotAIStorage` |
| Gameplay decisions | `PlayerbotAI` |
| Movement semantics | Existing PlayerBots (primarily AzerothCore/mod-playerbots) actions/strategies |
| Durable master identity | `BotRecord.masterGuid` |
| Live master pointer | `PlayerbotAI` |
| Packet bridge | `BotPacketAdapter` |
| Player lifecycle bridge | `BotPlayerAdapter` |
| Chat bridge | `BotChatAdapter` |
| Native command surface | `BotCommands` |
| Existing command language | `PlayerbotAI::HandleCommand` |
| Random population | `RandomBotService` |

Do not introduce a second owner for movement, master identity, session lifetime
or AI state.

### 3.6 LLMs stay outside real-time gameplay

Combat, movement, healing, threat, interrupts and CC must work with:

```text
zero internet
zero LLM
zero external service
```

LLM/chat features, when used, must be optional and asynchronous.

## 4. Implemented architecture

### 4.1 Session invariant

```text
one account
    +-- at most one active Network session
    +-- zero or more active Headless character sessions
```

Network sessions are account-keyed. Headless sessions are character-GUID keyed.
A human Network login retains normal duplicate-login semantics; human reclaim
of an owned Headless character takes precedence over bot control.

The login holder carries account ID, character GUID and transport type rather
than retaining a raw session pointer. Network callbacks resolve the account
session; Headless callbacks resolve the character-GUID session.

### 4.2 Native module boundary

Penqle loads the repository as:

```text
modules/TortoiseBots/
```

`src/TortoiseBotsModule.cpp` is intentionally the only loader-recursed source.
The real source graph is registered by `TortoiseBots.cmake`.

The host layer is concentrated in:

```text
host/BotHostAdapter
host/BotSessionAdapter
host/BotPlayerAdapter
host/BotChatAdapter
host/BotPacketAdapter
```

Bot behavior remains in module/runtime/AI code rather than the core.

### 4.3 PlayerBots runtime

The active gameplay runtime uses the existing PlayerBots (primarily AzerothCore/mod-playerbots) model:

```text
PlayerbotAI
Engine
AiObjectContext
Strategy
Trigger
Action
Value
```

The obsolete native `BotController` gameplay owner is gone.
`PlayerbotAIAdapter` is the AI update owner.

The source graph currently contains the nine Vanilla classes:

```text
Warrior
Paladin
Hunter
Rogue
Priest
Shaman
Mage
Warlock
Druid
```

Large TBC/WotLK/later donor families were removed from the product tree rather
than merely hidden behind build exclusions.

### 4.4 Packet bridge

`BotPacketAdapter` is the module packet interpretation layer.

Conceptually:

```text
Headless outgoing packet
    -> PlayerbotAI::HandleBotOutgoingPacket

Network master outgoing packet
    -> owned AIs HandleMasterOutgoingPacket

Network master incoming packet
    -> owned AIs HandleMasterIncomingPacket
```

The core exposes generic packet hooks; it does not contain bot-specific opcode
branches.

### 4.5 Native command surface

Current native commands include:

```text
.bot add
.bot remove
.bot follow
.bot invite
.bot uninvite
.bot stay
.bot list
.bot stats
.bot command
.bot help
```

`.bot command` delegates to `PlayerbotAI::HandleCommand` for Existing PlayerBots (primarily AzerothCore/mod-playerbots)
command behavior.

Ownership must remain account/GM based. A bot command must never become a path
for controlling another player's character.

### 4.6 Random bots

`RandomBotService` is bounded and module-owned. The current baseline discovers
pre-existing random-bot characters; it does not create accounts/characters.

Random account/character generation is a later feature, not a cleanup task.

## 5. Vanilla/Turtle product boundary

The active tree is intentionally a Vanilla/Turtle 1.18.1 product slice.

Preserve:

- all nine Vanilla classes;
- Vanilla raids and WSG/AB/AV behavior that remains applicable;
- Turtle custom races and validated custom spell/talent behavior;
- native Tortoise LFG/meeting-stone behavior;
- native transport/taxi behavior;
- existing generic gameplay behavior that genuinely applies to the target.

Do not re-add expansion systems merely because donor code contains them.
Numeric IDs are not evidence by themselves: validate unknown Turtle IDs against
the target data.

`tools/verify_turtle_surface.sh` is a regression guard for known prohibited
families/IDs. It is not a substitute for semantic review of new Turtle data.

## 6. Current milestone — gameplay acceptance

Broad architecture cleanup is frozen. Do not start another generic donor cleanup.

1. preserve architecture freeze
2. owned-bot manual acceptance (add/follow/combat/loot/death/relogin/teleport)
3. human + bots 5-player dungeon (tank/healer/DPS/interrupts/CC/loot/wipe recovery)
4. fix observed gameplay defects — not speculative completeness work
5. broaden class/Turtle coverage deliberately based on real failures

### 6.1 F-03/F-27 integration closure (completed for local baseline)

For the validated local baseline (core `7353989c94399f80572a2f8ec2eb73c63a6c79f8`):

- **F-03 — closed for the supported local integration:** legacy supported-path
  coupling removed (LFT filler, stale command/stub surface, bot slots, hardwired
  RNDBOT filters, stale include paths and bot-named diagnostics).
  `BUILD_LEGACY_PLAYERBOTS` remains an unsupported historical escape hatch and
  `MODULE_TORTOISEBOTS` is the supported selector. The historical
  `src/modules/PlayerBots` tree is retained disabled for inspection, not deleted.
- **F-27 — locally proven where source proves it:** `npc_teslinah` is now
  registered and invalid literal ScriptName `0` is cleared by migration
  `20260825090000_world.sql`; 17 Turtle ScriptNames remain explicitly
  unverified content gaps — not PlayerBots architecture blockers, no fake
  scripts were added. See Git history and
  [PLAYERBOTS_AUDIT.md](archive/PLAYERBOTS_AUDIT.md) for the 17 names.

Upstream Penqle publication of the core checkpoint is pending — validated as a
local pinned baseline, not a merged upstream PR.
the exact core SHA and upstream status.

### 6.2 Known-good pair

Recorded as:

```text
TortoiseBots tested code checkpoint: 07cf7976c546fac27083c7b46e73299c25b095f3
Pinned core checkpoint:              7353989c94399f80572a2f8ec2eb73c63a6c79f8
```

PR #15 adds documentation-only closure commits after the tested code checkpoint;
they do not change tested behavior.
final commit SHA.

## 7. Manual gameplay phase

After the core/data boundary is clean, development should become gameplay-led.

### 7.1 Owned-bot acceptance

Validate in the real client/runtime:

- add/login;
- follow/stay;
- combat and target selection;
- loot;
- death/resurrection;
- logout/relogin;
- human reclaim;
- teleport/map transition.

Fix observed defects rather than starting another global audit.

### 7.2 Five-player dungeon milestone

Primary useful-release target:

```text
human player + four bots -> playable 5-player dungeon
```

Exercise:

- tank pulls and threat;
- healer behavior;
- DPS behavior;
- interrupts and CC;
- loot;
- quest interactions;
- doors/gossip;
- wipe/corpse recovery;
- regrouping;
- instance transitions.

Representative first role/class set:

```text
Warrior tank
Priest healer
Mage DPS
Rogue DPS
Hunter DPS
```

Expand to the remaining classes/specs as concrete failures or missing behavior
appear.

### 7.3 Turtle-specific acceptance

Then deliberately exercise:

- Goblin and High Elf gameplay beyond lifecycle;
- Turtle class/talent/spell changes;
- custom dungeons/portals;
- collection mounts;
- custom quests/travel;
- custom battleground/zone behavior only where target data confirms it.

## 8. Later roadmap

Only after the 5-player dungeon experience is reliable:

1. broader all-class/spec acceptance;
2. more Turtle dungeon/raid strategies;
3. battleground validation;
4. random account/character generation;
5. AH/economy/population behavior;
6. client addon/state UI;
7. performance work for larger bot populations;
8. optional asynchronous conversation/LLM features.

Do not optimize for hundreds/thousands of bots before the small-party product
is proven useful.

## 9. Performance rules

From the beginning:

- no DB query every bot tick;
- no full-world scan every bot tick;
- no synchronous network calls on game/map threads;
- no rebuilding large strategy graphs every update;
- cache immutable spell/talent metadata where practical;
- use event-driven invalidation where useful;
- keep expensive diagnostics opt-in;
- measure before optimizing.

Useful future metrics:

```text
bot update time
total bot CPU
DB query rate
movement/path requests
AI decisions/sec
memory per bot
```

## 10. Security and ownership

At minimum:

- owned character belongs to the requesting account or an explicitly supported
  ownership policy;
- duplicate login remains safe;
- human login/reclaim takes precedence over bot control;
- bot commands verify owner/group/GM authority;
- chat surfaces must not become arbitrary remote-control channels;
- debug/admin features require explicit privilege.

Do not copy permissive donor behavior without reviewing it against the target
core's account/security model.

## 11. Provenance and donor use

Major reference pools include:

- TortoiseWoW Knowledge Base — expected behavior/capability map;
- CMaNGOS PlayerBots — existing class/combat/movement behavior;
- Shyalya/Tortoise — Turtle compatibility lessons;
- MangosZero — lifecycle patterns;
- mod-playerbots — newer behavior reference.

General rule:

```text
study -> extract intent -> port/reimplement -> validate
```

A literal cherry-pick is appropriate only when the change is isolated,
compatible, licensed/attributed correctly and does not expand host coupling.

Record substantial copied/ported/reimplemented behavior in
[PROVENANCE.md](PROVENANCE.md) with source repository, commit, source files,
reason and local validation.

## 12. Validation policy

Use the smallest check that proves the current change.

- docs/comments/config only -> text/static checks, no C++ build;
- module-only C++ -> one cached native module build after a coherent batch;
- core seam -> cached ON iteration, broader matrix only when stable;
- build-gating change -> directly affected configurations;
- phase/PR boundary -> one final relevant optional-build/runtime gate.

Use your local build environment or Docker stack if you have one. Avoid
full image rebuilds for normal source edits.

A previous successful test remains evidence for unchanged code/behavior.
Do not replay historical lifecycle fixtures merely for ceremony.

## 13. Definition of done

### Architecture baseline

The architecture baseline is healthy when:

- core builds without the module;
- native module builds only when explicitly selected;
- legacy PlayerBots remains disabled/removed from the supported product path;
- host integration is generic and centralized;
- no `GetBot()`-style gameplay API exists;
- Headless lifecycle remains character-GUID keyed and World-owned;
- same-account Network + Headless lifecycle remains safe;
- imported behavior has provenance;
- current known-good core/module pair is documented.

### First useful release

A first useful release is ready when:

- a human can reliably use owned bots in normal world content;
- a human-led 5-player dungeon party is playable;
- tank/healer/DPS roles perform basic responsibilities;
- bots follow, recover and regroup reliably;
- core remains cleanly optional with bots disabled;
- important gameplay failures have deterministic diagnostics/tests;
- installation/build/runtime workflow is documented for another developer.

## 14. Working documentation

Use the documentation set by purpose:

- [PLAN.md](PLAN.md) — durable architecture and roadmap;
- [HOST_API.md](HOST_API.md) — implemented core/module contract;
- [PROVENANCE.md](PROVENANCE.md) — append-oriented source lineage/validation;
- [PLAYERBOTS_AUDIT.md](archive/PLAYERBOTS_AUDIT.md) — historical audit evidence.

Historical handovers and audit bodies are evidence, not active implementation
instructions. Use Git history when older design proposals are needed.
