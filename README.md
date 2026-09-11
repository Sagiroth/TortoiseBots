# TortoiseBots

Independent native PlayerBots module for the canonical **Tortoise WoW 1.18.1** core repository ([`tortoise-wow`](https://github.com/tortoise-wow/tortoise-wow), `bot-helpers` branch).

`TortoiseBots` delivers native AI companions through a decoupled C++ architecture: bot AI, combat strategies, and lifecycle management live entirely within this module, while session transport and character state stay cleanly owned by the core via generic headless sessions (`SessionTransport::Headless`). The core builds and runs 100% cleanly without the module (`MODULES=disabled`).

> **Companion in-game UI:** pair with [**TortoiseBotsManager**](https://github.com/Sagiroth/TortoiseBotsManager) (`/tbm`). All player-facing control — roster, lifecycle, and tactical party actions — is driven from this addon; the server-side command surface is an internal/advanced transport, not a player API.

---

## 🏛️ Architecture Boundary

```text
  Core Server (tortoise-wow: bot-helpers)
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

* **Clean decoupling:** Bot state is never attached to core classes (`WorldSession::GetBot()` / `m_bot` are strictly prohibited).
* **Optional native module:** Core compiles 100% cleanly without the module (`MODULES=disabled`).

---

## 📚 Knowledge Base (`docs/`)

All architecture, class AI, commands, mechanics, and operational guides are documented under the **Open Knowledge Format (OKF)** standard (see [`docs/manifest.yaml`](docs/manifest.yaml)):

| Guide / Reference | Content & Focus |
| :--- | :--- |
| 🧭 [**Documentation Catalog**](docs/README.md) | Central switchboard, role-based navigation, and **Quick Task Finder** |
| 🚀 [**Getting Started**](docs/guides/getting-started.md) | Spawning account alts, party setup, `/tbm` addon, and quick controls |
| ⚔️ [**Class AI & Rotations**](docs/classes/overview.md) | Rotations, specs, and Turtle WoW 1.18.1 custom abilities for all 9 classes |
| 🛡️ [**Dungeon & Raid Tactics**](docs/guides/dungeon-tactics.md) | Line-of-sight corner pulling (`pullback`), CC discipline, and wipe recovery |
| 🌍 [**Living World & Economy**](docs/guides/living-world.md) | Roaming bots, quest grinding, AH trading, guilds, and LFT/BG auto-queues |
| 🎮 [**Player Controls & Commands**](docs/guides/player-controls.md) | Complete `.bot` command suite, tactical intents, whispers, and addon protocol |
| ⚙️ [**Configuration & Tuning**](docs/guides/configuration-tuning.md) | Plain-English tuning guide for `aiplayerbot.conf` settings and knobs |
| 📊 [**Observability Dashboard**](docs/guides/observability-dashboard.md) | Web dashboard, 2D live map, Prometheus metrics, and stuck-bot tracker |
| 🧠 [**Strategy Engine**](docs/concepts/strategy-engine.md) | How the `UpdateAI` tick, triggers, actions, and backoff work under the hood |
| 🏛️ [**Architecture Invariants**](docs/concepts/architecture-invariants.md) | The 5 non-negotiable modularity rules and headless session lifecycle |
| 🔌 [**Host API Contract**](docs/HOST_API.md) | Technical host seams, generic interfaces, and packet routing |
| 📜 [**Source Provenance**](docs/PROVENANCE.md) | Donor lineage, porting ledger, and commit references |

---

## 🛠️ Quick Build

Inside your `tortoise-wow` (`bot-helpers` branch) checkout:

```bash
# Clone module into modules/
git clone https://github.com/Sagiroth/TortoiseBots.git modules/TortoiseBots

# Configure and compile
cmake -B build -DMODULES=static -DMODULE_TORTOISEBOTS=static
cmake --build build -j"$(nproc)"
```

Configuration templates are generated at build time (`conf/tortoise_bots.conf` and `aiplayerbot.conf`). See [**Configuration & Tuning**](docs/guides/configuration-tuning.md) for available settings.

---

## 📜 License & Provenance

Original module code combined with donor PlayerBots implementations under GPL-2.0 / AGPL-3.0 compatible licenses. See [`LICENCE.md`](LICENCE.md), [`docs/PROVENANCE.md`](docs/PROVENANCE.md), and [`docs/LICENSE_AUDIT.md`](docs/LICENSE_AUDIT.md).
