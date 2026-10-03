# TortoiseBots

An independent, native PlayerBots module for the canonical **Tortoise WoW 1.18.1** core repository ([`tortoise-wow`](https://github.com/tortoise-wow/tortoise-wow), `main` branch).

`TortoiseBots` delivers native AI companions through a decoupled C++ architecture: bot AI, combat strategies, and lifecycle management live entirely within this module, while session transport and character state stay cleanly owned by the core via generic headless sessions (`SessionTransport::Headless`). The core builds and runs 100% cleanly without the module (`MODULES=disabled`).

> **Companion in-game UI:** pair with [**TortoiseBotsManager**](https://github.com/Sagiroth/TortoiseBotsManager) (`/tbm`). All player-facing control — roster, lifecycle, and tactical party actions — is driven from this addon; the server-side command surface is an internal/advanced transport, not a player API.

---

## ✨ What It Does

* **A living world.** A pool of autonomous bots (size set by `MinRandomBots` / `MaxRandomBots`) is spread across levels 1–60 by default, or starts everyone at level 1 for a fresh-realm feel (`RandomBotStartLevelMin` / `Max`). From there the bots level on their own: questing, grinding, training, selling loot, gathering and moving on to new zones as they outgrow them. No gold is handed out, gear comes from loot, quests and vendors, and the auction house only sees what bots really loot.
* **Bots that keep going.** Bots rest before the next pull, flee at critical health, avoid spots where they keep dying, and are rescued automatically when they get stuck or stall as a ghost.
* **Sensible kit.** Every pool bot carries three 14-slot bags, level-appropriate food and drink (water only for mana users), ammo and a quiver or ammo pouch matched to its weapon, a soul pouch for warlocks, and buys a better vendor weapon with its own gold when it can afford one. Primary professions arrive at level 5.
* **Your own party.** Play with bot alts from your own account or hire temporary companions at `<Mercenary Hire>` recruiters; they follow, fight, heal and tank in dungeons and raids, controlled from the TortoiseBotsManager addon.
* **Full class AI.** Rotations for all 9 classes, including Turtle WoW 1.18.1 custom abilities.
* **Observability.** A web dashboard with a live map, per-bot activity, incidents and a one-command KPI report (`tools/pool_kpi_report.py`) for comparing pool runs.

---

## 🏛️ Architecture Boundary

```text
  Core Server (tortoise-wow: main)
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
| 📜 [**Changelog**](CHANGELOG.md) | Chronological log of gameplay fixes, AI updates, and engine releases |
| 📜 [**Source Provenance**](docs/PROVENANCE.md) | Donor lineage, porting ledger, and commit references |

---

## 🛠️ Quick Build

Inside your `tortoise-wow` (`main` branch) checkout:

```bash
# Clone module into modules/
git clone https://github.com/Sagiroth/TortoiseBots.git modules/TortoiseBots

# Configure and compile
cmake -B build -DMODULES=static -DMODULE_TORTOISEBOTS=static
cmake --build build -j"$(nproc)"
```

Configuration is generated at build time from `ai/playerbot/aiplayerbot.conf.dist.in` and `conf/tortoise_bots.conf.dist` and installed next to `mangosd.conf` (`aiplayerbot.conf`, and `modules/tortoise_bots.conf`). See [**Configuration & Tuning**](docs/guides/configuration-tuning.md) for available settings.

---

## 🙏 Credit

TortoiseBots is made by **Sagiroth**. Forks, repacks, Docker bundles, videos and
public servers are welcome — please credit it. Ready to paste:

```text
Playerbots: TortoiseBots by Sagiroth — https://github.com/Sagiroth/TortoiseBots
```

The in-game login message and `.bot about` show the same line; keep them in
modified builds (see [`LICENCE.md`](LICENCE.md#attribution-author-notice)).

## 📜 License & Provenance

Original module code combined with donor PlayerBots implementations under GPL-2.0 / AGPL-3.0 compatible licenses. See [`LICENCE.md`](LICENCE.md), [`docs/PROVENANCE.md`](docs/PROVENANCE.md), and [`docs/LICENSE_AUDIT.md`](docs/LICENSE_AUDIT.md).
