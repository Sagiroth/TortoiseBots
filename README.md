# TortoiseBots

TortoiseBots is an optional native PlayerBots module for
[Tortoise WoW 1.18.1](https://github.com/Penqle/tortoise-wow).

> **Addon:** press buttons instead of typing `.bot` — see the companion client addon
> **[tortoise-wow-stack/TortoiseBotsManager](https://github.com/tortoise-wow-stack/TortoiseBotsManager)**
> (`/tbm` panel: list owned bots, summon/spawn, follow/stay, group, pull). Requires this module on the server.

It brings existing PlayerBots (primarily AzerothCore/mod-playerbots) combat, movement, class, group, loot, quest and
travel behavior to Tortoise WoW while keeping bot-specific logic outside the
core.

> **Harvest behavior, not architecture.**

TortoiseBots uses the upstream native module system and a small generic host API
instead of recreating the old tightly coupled `GetBot()` / `m_bot` /
`sPlayerBotMgr` architecture.

---

## Status

> **WIP — local integration is complete; the core lifecycle refactor is local-only and upstream PRs are awaiting merge.**

- [x] Native TortoiseBots module implemented
- [x] Existing `PlayerbotAI` integrated
- [x] All nine Vanilla classes included
- [x] Vanilla/Turtle 1.18.1 cleanup and compatibility audit completed
- [x] Headless session lifecycle validated against the pinned baseline
- [x] PlayerBots-enabled and module-disabled builds validated
- [x] Local Docker build and startup against integrated `#396 + #411 + #416` core
- [x] TortoiseBots feature stack #37–#42 assembled
- [ ] Upstream core PRs #411 and #416 merged
- [ ] Manual owned-bot gameplay acceptance
- [ ] Manual 5-player dungeon acceptance

The local integration checkpoint is complete. Upstream merge status and exact
source revisions remain tracked in Git history and Docker `SOURCE_IDENTITY`.

---

## Core dependencies

TortoiseBots targets [tortoise-wow](https://github.com/Penqle/tortoise-wow) (`Penqle/tortoise-wow`) as its canonical upstream core.

### Generic Headless sessions

TortoiseBots needs the core to support a `WorldSession` without a network client.
or a Sol Nerfed Slop and use Astra Token to fix the Slop or.. take 3 days of vacations and ju
The core exposes the lifecycle as three World calls:

- `World::StartHeadlessSession(accountId, characterGuid, locale, tag)` validates the request, constructs the shared session, registers it by character GUID, and dispatches the normal async character-login bundle.
- `World::StopHeadlessSession(characterGuid, save)` cancels a pending request or logs out and removes an active Headless session.
- `World::GetHeadlessSessionState(characterGuid)` reports `NotFound`, `Pending`, `Loading`, or `Active`.

The manager hides allocation, initialization, pending maps, login dispatch,
callback identity, reclamation, deletion, update, and shutdown from callers.

> [!IMPORTANT]
> The current compatibility target is the stacked core work in PRs
> [#438](https://github.com/Penqle/tortoise-wow/pull/438) and which is still
> awaiting upstream merge. Build against the synchronized revisions recorded
> in `docs/HOST_API.md`, not an unrelated `main` checkout.
>
> For local use before upstream merge, the core checkout must contain both
> PRs (merge/rebase #411 first, then #416). A plain Penqle `main` checkout is
> not a compatible host for this module version.
>
> PR #411 supplies the generic Network/Headless transport and the
> GUID-keyed, `World`-owned Headless session lifecycle. Its local refactor is
> recorded in `docs/HOST_API.md`; upstream merge is still pending. PR #416 is
> rebased on that refactor and supplies generic character-creation, LFT,
> battleground, group-target, and participant primitives. #416 adds no second
> session model.
>
> The module-facing contract is intentionally the three `World` lifecycle
> calls plus generic `SessionTransport` queries. The core does not know that a
> Headless session is a bot.

The intended boundary is:

```text
Penqle/tortoise-wow
        |
        | generic module + Headless host API
        v
TortoiseBots
        |
        | bot lifecycle + AI + gameplay policy
        v
PlayerbotAI
```

The session distinction is deliberately split by responsibility:

- `World::m_sessions` remains the account-keyed Network registry;
- `HeadlessSessionMgr` owns pending/active GUID-keyed Headless sessions;
- `WorldSession` remains the shared concrete player-session type;
- TortoiseBots owns bot records, AI adapters, and gameplay decisions, never
  `WorldSession` lifetime or pending/login state.

The core should expose **Headless sessions**, not PlayerBots concepts.

TortoiseBots should **not** require a bot-specific fork of the core.

Removal of the historical built-in PlayerBots subsystem
([PR #396](https://github.com/Penqle/tortoise-wow/pull/396)) is optional from
the TortoiseBots API perspective. It is recommended for a clean core because
it removes the obsolete runtime and schema dependency; TortoiseBots does not
use any code removed by that PR. The local Docker integration was tested with
#396 present.

---

## Features

- Same-account bots (`.bot add` / `remove`) with human reclaim
- Party: follow / stay / invite via `.bot` (group AI)
- Class AI for all 9 Vanilla classes — basic combat / heal / tank
- Loot, quest and travel / taxi handling
- Turtle Goblin / High Elf and Turtle spell / talent / mount handling where validated
- Bounded random-bot login and teleport for existing characters (TortoiseBots #37)
- Optional pinned random-bot pool (TortoiseBots #38)
- Optional RNDBOT auto-create (TortoiseBots #39)
- Optional LFT autofill (TortoiseBots #40)
- Optional AH market population (TortoiseBots #41)
- Optional BG autoqueue (TortoiseBots #42)

The stacked feature services are configuration-gated and remain off by default
unless their corresponding settings are enabled.

Targets Vanilla/Turtle 1.18.1 — Any future expansions elements are removed.

---

## Architecture

TortoiseBots is a native optional module, not a replacement core and not a
vendored PlayerBots core fork.

```text
Upstream / Tortoise core
        |
        | generic Headless / lifecycle / packet / command hooks
        v
TortoiseBots
        |
        +-- BotManager
        +-- PlayerbotAIAdapter
        +-- PlayerbotAIStorage
        +-- host adapters
        +-- Existing PlayerbotAI (primarily AzerothCore)
        +-- Strategy / Trigger / Action / Value
```

Normal core gameplay code should not need bot-specific state.

Legacy patterns such as these are intentionally avoided:

```cpp
WorldSession::GetBot()
WorldSession::SetBot()
m_bot
sPlayerBotMgr
PlayerBotEntry
```

---

## Session model

Owned bots are ordinary Tortoise characters using Headless `WorldSession`s.
Headless means “no network transport”; it does not mean a second gameplay
object or a second authentication protocol.

```text
One account
    +-- at most one Network session
    +-- zero or more Headless character sessions
```

Network sessions remain account-keyed. Headless sessions are keyed by
character GUID and owned by `World` through `HeadlessSessionMgr`.

The normal character-loading and player lifecycle are reused after trusted
server-side Headless creation. The module calls only Start/Stop/State and never
inserts bots into the normal account-session map, promotes pending sessions,
dispatches login, logs out a session directly, or treats a client-provided GUID
as ownership proof.

`WorldSession` is intentionally shared because `Player`, map, group, save,
chat, and lifecycle code already consume `WorldSession*`. A separate
`HeadlessWorldSession` subclass would add casts and duplicated lifecycle code
without adding network security. Transport is selected by the server through
`SessionTransport::Network` or `SessionTransport::Headless`.

The core owns `WorldSession` lifetime. TortoiseBots owns bot records, AI, and
gameplay policy.

The detailed integration contract is documented in [`docs/HOST_API.md`](docs/HOST_API.md).

### Existing PlayerbotAI compatibility

The module preserves the existing PlayerBots behavior layer, primarily from
AzerothCore/mod-playerbots, while adapting its host calls to Tortoise's
Vanilla/Turtle core. The #411/#416 host changes do not replace the AI,
strategies, actions, triggers, values, class contexts, or dungeon behavior.

The implemented migration keeps `HasNetworkTransport()` for human/network
decisions and moves all Headless lifecycle transitions behind the three-call
core interface. It is not a rewrite of combat or movement AI.

The module remains responsible for bot ownership and policy. The core remains
responsible for generic character/session/participant lifecycle and does not
know that a Headless session is a PlayerBot.

---

## Commands

The native command surface currently includes:

```text
.bot add
.bot remove
.bot follow
.bot invite
.bot uninvite
.bot stay
.bot guard
.bot free
.bot ready
.bot attack
.bot formation
.bot list
.bot stats
.bot status
.bot pullback
.bot summon
.bot command
.bot help
```

`.bot command` forwards into the existing PlayerBots (primarily AzerothCore/mod-playerbots) command system for the
selected bot. Commands enforce normal account ownership or GM authority.

`pullback` dispatches the mature PlayerbotAI pull/return strategy; `summon` is
a player-owned asynchronous transition which restores follow on arrival. Both
fail closed when their actors become invalid; their full runtime acceptance
remains pending the merged Headless core PRs.

### Client addon

The companion addon **[TortoiseBots Manager](https://github.com/tortoise-wow-stack/TortoiseBotsManager)**
(`Interface 11200`, `/tbm`) is a button UI for the same `.bot` surface: roster of owned bots,
online/starting/offline state, `Spawn` / `Summon` / `Follow` / `Stay` / `Invite` / `Pull`.
It is optional and pure client — install `Interface/AddOns/TortoiseBots/` from that repo; it requires this module on the server.

---
## Module layout

TortoiseBots is consumed as `tortoise-wow/modules/TortoiseBots/`:

```text
tortoise-wow/
└── modules/
    └── TortoiseBots/
```

Main directories:

```text
ai/          PlayerBots AI and Turtle compatibility
behavior/    Module-owned helpers
commands/    Native .bot commands
conf/        Module configuration
data/sql/    Module migrations
host/        Core <-> TortoiseBots boundary
runtime/     Bot lifecycle and AI ownership
src/         Native module entrypoint
tools/       Audit and development tooling
docs/        Architecture, status and provenance
```

---

## Build

Normal native module selection:

```text
BUILD_LEGACY_PLAYERBOTS=OFF
MODULES=static
MODULE_TORTOISEBOTS=static
```

`BUILD_LEGACY_PLAYERBOTS` refers to the historical built-in PlayerBots
implementation and should remain disabled when using TortoiseBots.

See [`AGENTS.md`](AGENTS.md) for development and validation workflow.

---

## Configuration and data

Configuration:

```text
conf/tortoise_bots.conf.dist
ai/playerbot/aiplayerbot.conf.dist.in
```

Database migrations:

```text
data/sql/world/
data/sql/char/
```

The inherited PlayerBots configuration is broader than the currently validated
Turtle gameplay surface. The existence of a setting does not by itself mean the
corresponding feature has completed gameplay acceptance.

The current stacked feature toggles include:

```text
AiPlayerbot.Enabled
AiPlayerbot.EnableRandomTeleports
AiPlayerbot.PinnedBots
AiPlayerbot.RandomBotAutoCreate
AiPlayerbot.RandomBotLftEnabled
AiPlayerbot.AhMarketEnabled
AiPlayerbot.RandomBotBgEnabled
```

For the Docker workflow, `AI_PLAYERBOT_ENABLED=1` renders
`AiPlayerbot.Enabled = 1`; the remaining feature toggles are intentionally
disabled by default.

---

## Validation

The pinned baseline has recorded evidence for:

- module-enabled and module-disabled builds
- module loading and world-ready startup
- Headless character login, AI attachment, save / logout / relogin
- same-account human reclaim
- native command dispatch and group invite handling
- Goblin and High Elf lifecycle fixtures
- repeatable module migrations

The current local integrated checkpoint additionally records:

- native Docker build with `BUILD_PLAYERBOTS=OFF`, `MODULES=static` and
  `MODULE_TORTOISEBOTS=static`
- module migration discovery and schema presence
- AI Playerbot initialization and `TortoiseBots: native module loaded (AI enabled)`
- world-ready startup without fatal startup errors
- listening realm and world TCP ports

Still pending:

- upstream merge of core PRs #411 and #416
- manual owned-bot gameplay acceptance
- full 5-player dungeon acceptance
- broader Turtle class/spec/content testing
- large random-bot population testing

The current local Docker source records exact synchronized core/module
revisions in `SOURCE_IDENTITY`.

---

## References

The canonical target core is:

- [Penqle/tortoise-wow](https://github.com/Penqle/tortoise-wow)

Major donor/reference sources include:

- [CMaNGOS PlayerBots](https://github.com/cmangos/playerbots)
- [Shyalya/tortoise-wow](https://github.com/Shyalya/tortoise-wow)
- [MangosZero](https://github.com/mangoszero/server)
- [mod-playerbots](https://github.com/mod-playerbots/mod-playerbots)

These repositories are behavior and compatibility references, not the target
core. Exact source lineage is recorded in [`docs/PROVENANCE.md`](docs/PROVENANCE.md).

---

## Project scope and affiliation

This repository contains server-module source code only. It does not distribute
game-client binaries or extracted game data/assets, provide hosting, or operate
a game service. It is not affiliated with or endorsed by Blizzard Entertainment
or Turtle WoW. World of Warcraft and related marks belong to their respective
owners.

## Licence

TortoiseBots combines original and donor-derived code. Applicable terms must be
determined from the donor and per-file notices; do not assume every file uses
the same GPL-2.0 variant. The target `Penqle/tortoise-wow` core is AGPL-3.0,
and a combined build must satisfy every component's compatible licence terms.

See [`LICENCE.md`](LICENCE.md) for the licence summary and
[`docs/LICENSE_AUDIT.md`](docs/LICENSE_AUDIT.md) for the donor compatibility
audit and release gate.

---

## Documentation

- [`docs/PLAN.md`](docs/PLAN.md) — architecture and roadmap
- [`docs/HOST_API.md`](docs/HOST_API.md) — core/module host contract
- [`AGENTS.md`](AGENTS.md) — contributor and agent rules
- [`docs/PROVENANCE.md`](docs/PROVENANCE.md) — donor/source lineage
- [`LICENCE.md`](LICENCE.md) — upstream and donor licences

Historical audit evidence remains in Git history and (if retained) under `docs/archive/`.
