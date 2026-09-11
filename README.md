# TortoiseBots

Independent native PlayerBots module for the canonical **Tortoise WoW 1.18.1** core from Penqle ([`Penqle/tortoise-wow`](https://github.com/Penqle/tortoise-wow), `bot-helpers` branch).

`TortoiseBots` delivers native AI companions through a decoupled C++ architecture: bot AI, combat strategies, and lifecycle management live entirely within this module, while session transport and character state stay cleanly owned by the core via generic headless sessions (`SessionTransport::Headless`). The core builds and runs 100% cleanly without the module (`MODULES=disabled`).

> **Companion in-game UI:** pair with [**TortoiseBotsManager**](https://github.com/tortoise-wow-stack/TortoiseBotsManager) (`/tbm`). All player-facing control — roster, lifecycle, and tactical party actions — is driven from this addon; the server-side command surface is an internal/advanced transport, not a player API.

---

## Features

### Class Combat AI
- **Nine Vanilla Classes (Warrior–Druid):** Spec-aware talent trees, single-target rotations, and AoE pacing.
- **Role Mastery:** Threat generation and tab-target holding for tanks, priority healing ladders for healers, and burst/sustain DPS.
- **Turtle WoW 1.18.1 Content:** Custom spells integrated into rotations (*Master Strike*, *Holy Strike*, *Carve*, *Lacerate*, *Aspect of the Viper*, *Chastise*, *Earthquake*, *Icicles*, *Dark Harvest*, *Tree of Life Form*).
- **Tactical Utility:** School-locking interrupts (*Kick*, *Pummel*, *Counterspell*), raid-marker crowd control (*Polymorph*, *Sap*, *Freezing Trap*), buffs, consumables, and simultaneous eating/drinking.

### Companion Party System
- **Your Own Account Alts:** Turn characters from your own account into headless bots that adventure with you in the open world and 5-player dungeons.
- **Tactical Controls & Addon:** Seamless pairing with the **TortoiseBotsManager** (`/tbm`) addon for instant party commands (`attack`, `pull`, `pullback`, `cc <mark>`, `aoe on/off`, `focus skull`).
- **Human Reclaim Always Wins:** Logging into an account character instantly and cleanly disconnects the bot.

### Living World Ecosystem
- **Wandering & Leveling Bots:** Autonomous random bots (`RNDBOT*`) roam zones, travel via flight paths/boats, grind mobs, gather herbs/ore, and turn in quests.
- **Dynamic Level Scaling:** Random bot population dynamically scales with online players, ensuring zones near your level feel active and alive.
- **Organic Groups & Guilds:** Random bots invite solo players to quest, form organic adventuring groups, buy guild charters, and create their own guilds.

### Simulated Economy
- **Living Auction House:** Bots evaluate surplus materials and BoE gear, posting them at realistic prices and buying equipment upgrades with real gold.
- **Trade & Repair:** Bots visit blacksmiths to repair durability, sell vendor trash, and buy reagents.

### Dungeons & Battlegrounds
- **Looking-For-Trouble (LFT) Fill:** Automatically injects bots into LFT queues to fill vacant Tank, Healer, or DPS slots when human players are waiting.
- **Battleground Auto-Queue:** Injects bots into Warsong Gulch, Arathi Basin, and Alterac Valley to launch active PvP matches.

### Fleet Observability & Diagnostics
- **Live Web Dashboard:** Standalone Go telemetry daemon in [`tools/observability`](tools/observability) with an interactive 2D world map, macro-state breakdowns, and Prometheus metrics (`/metrics`).
- **Persistent Issue Tracking:** Automatically detects and tracks any bot that gets stuck or loops actions for 5+ minutes so you can clear the root cause.

---

## Architecture & host integration

```text
  Penqle Core (tortoise-wow: bot-helpers)
  ┌─────────────────────────────────────────┐
  │ Canonical 1.18.1 World Server           │
  │ Generic Headless Sessions (No bot deps) │
  └──────────────────▲──────────────────────┘
                     │ Generic Seam API
  TortoiseBots Module│ (modules/TortoiseBots)
  ┌──────────────────┴──────────────────────┐
  │ Decoupled Native C++ PlayerBots Engine  │
  │ Class Combat AI, Actions & Strategies   │
  └──────────────────▲──────────────────────┘
                     │ .bot transport / TBM: protocol
  TortoiseBotsManager│ (Client Addon)
  ┌──────────────────┴──────────────────────┐
  │ In-game 1.12 / 11200 UI (/tbm)          │
  │ Tactical Actions & Roster Management    │
  └─────────────────────────────────────────┘
```

- **Clean decoupling:** bot state is never attached to core classes (`WorldSession::GetBot()` / `m_bot` are strictly rejected).
- **Optional native module:** compile with `-DMODULE_TORTOISEBOTS=static` to include; `-DMODULES=disabled` builds a clean core.

---

## Requirements

- A [Tortoise WoW core](https://github.com/Penqle/tortoise-wow) checkout on the `bot-helpers` branch.
- C++17 toolchain and CMake (see build below).

---

## Build & development

### 1. Fast iteration (Docker + ccache)

Using [`tortoise-docker-penqle`](https://github.com/Sagiroth/tortoise-docker-penqle):

```bash
# In tortoise-docker-penqle/:
./dev/start              # Start the persistent builder container
./dev/build-playerbots   # Incremental build with ccache (~40s)
./dev/restart-server     # Inject the new mangosd into the live container
./dev/ccache             # Inspect ccache hit rates
```

### 2. Direct CMake build

```bash
# Inside your tortoise-wow (bot-helpers) checkout:
git clone https://github.com/Sagiroth/TortoiseBots.git modules/TortoiseBots

cmake -B build -DMODULES=static -DMODULE_TORTOISEBOTS=static
cmake --build build -j"$(nproc)"
```

---

## Configuration

Module configuration is generated at build time:

- `conf/tortoise_bots.conf.dist` → installs as `tortoise_bots.conf` (module toggles, service rates, telemetry).
- `ai/playerbot/aiplayerbot.conf.dist.in` → installs as `aiplayerbot.conf` (AI strategies, combat thresholds, economy).

All autonomous/optional services are **disabled by default** and enabled by configuration.

Common toggles (env names as used by the Docker stack):

| Toggle | Purpose |
| :--- | :--- |
| `AI_PLAYERBOT_ENABLED` | Master gameplay on/off; module still loads when off. |
| `RANDOMBOTS_ENABLE` / `RANDOMBOTS_AUTOCREATE` | Autologin the random-bot pool; optionally auto-create the deficit. |
| `MIN_RANDOM_BOTS` / `MAX_RANDOM_BOTS` | Random-bot pool bounds. |
| `OBSERVABILITY_ENABLE` / `OBSERVABILITY_HOST` / `OBSERVABILITY_PORT` | Telemetry emitter and daemon address. |
| `ISSUE_MIN_AGE_SEC` | Only surface persistent bot issues older than this (default 300). |

---

## Repository structure

```text
TortoiseBots/
├── ai/           PlayerBots strategy engine, triggers, actions, multipliers, and class contexts
├── behavior/     Module-owned tactical helpers (CC marks, pullbacks, targeting, convenience)
├── commands/     Native .bot command parsing and dispatch (internal transport)
├── conf/         Module configuration templates
├── data/sql/     Module-owned database migrations (world and character schemas)
├── host/         Generic host adapters (sessions, packets, player binding)
├── runtime/      BotManager, background services, and the observability emitter
├── tools/        Diagnostics, contract verifiers, and the optional observability daemon
└── docs/         Architectural contracts, host seams, provenance, and license records
```

---

## Documentation

Documentation follows the **Open Knowledge Format (OKF)** standard (see [`docs/manifest.yaml`](docs/manifest.yaml)):

- [**Documentation Catalog**](docs/README.md) — Central switchboard and navigation index.
- [**Class AI & Rotations**](docs/classes/overview.md) — Rotations, specs, and Turtle WoW custom abilities for all 9 classes.
- [**Living World & Autonomous Bots**](docs/guides/living-world.md) — Wandering bots, quest grinding, AH trading, guilds, and battlegrounds.
- [**Player Controls & /tbm Addon**](docs/guides/player-controls.md) — Tactical intents, party actions, and addon transport protocol.
- [**Configuration & Tuning**](docs/guides/configuration-tuning.md) — Plain-English guide to `aiplayerbot.conf` settings and knobs.
- [**Bot Mechanics, Quirks & Gaps**](docs/concepts/bot-mechanics-and-quirks.md) — Targeting math, threat distribution, movement, interrupts, and quirks.
- [**Observability Dashboard**](docs/guides/observability-dashboard.md) — Web dashboard, live 2D world map, Prometheus metrics, and issue tracker.
- [**Architecture & Invariants**](docs/concepts/architecture-invariants.md) — Headless sessions, modularity rules, and core boundary.
- [**Host API Contract**](docs/HOST_API.md) — Technical C++ host boundary seams and packet bridges.
- [**Roadmap & Plan**](docs/PLAN.md) — Design invariants, roadmap, and definition of done.
- [**Source Provenance**](docs/PROVENANCE.md) — Donor lineage and attribution ledger.
- [`AGENTS.md`](AGENTS.md) — Contributor workflow, dev environment, and engineering rules.

---

## License

This project is a native server module. It contains original code combined with donor PlayerBots implementations under GPL-2.0 / AGPL-3.0 compatible licenses. See [`LICENCE.md`](LICENCE.md), [`docs/PROVENANCE.md`](docs/PROVENANCE.md), and [`docs/LICENSE_AUDIT.md`](docs/LICENSE_AUDIT.md).
