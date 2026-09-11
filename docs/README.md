# TortoiseBots Knowledge Base & Documentation

Welcome to the **TortoiseBots** documentation catalog. This knowledge base follows the **Open Knowledge Format (OKF)** standard, providing a structured, traversable graph for players, server operators, and AI pair programmers.

The root graph specification is declared in [`manifest.yaml`](manifest.yaml). You can validate graph integrity anytime by running `python3 tools/verify_okf.py`.

---

## 🧭 Navigation by Role

| If you are... | Start here | Purpose |
| :--- | :--- | :--- |
| **A player wanting to adventure with bots** | [**Getting Started Guide**](guides/getting-started.md) | How to spawn owned bots, form a party, and run dungeons |
| **Looking for in-game commands or addon controls** | [**Player Controls & /tbm Addon**](guides/player-controls.md) | Tactical intents (attack, pull, pullback, CC, AoE, formations) |
| **Curious about how a specific class plays** | [**Class Catalog Overview**](classes/overview.md) | Rotations, Turtle custom spells, and spec capabilities |
| **Wanting a living world with wandering bots & AH** | [**Living World & Autonomous Bots**](guides/living-world.md) | Roaming bots, quest grinding, AH economy, and guilds |
| **Configuring bot settings or population** | [**Configuration & Tuning**](guides/configuration-tuning.md) | Pacing, health/mana thresholds, and random bot pools |
| **Monitoring fleet health or debugging stuck bots** | [**Observability Dashboard**](guides/observability-dashboard.md) | Live 2D world map, Prometheus metrics, and issue tracker |
| **Developing bot AI or host integration** | [**Architecture Invariants**](concepts/architecture-invariants.md) | Headless sessions, modularity rules, and C++ contracts |

---

## ⚡ Quick Task Finder (How Do I...?)

| What do you want to do? | Direct Solution & Link |
| :--- | :--- |
| **Summon lost or stuck bots to your location** | Type `.bot summon` or use the Summon button in `/tbm` → [Player Controls](guides/player-controls.md#2-roster--lifecycle-commands) |
| **Have bots learn all class spells automatically** | Whisper `/w <BotName> trainer` while standing near a class trainer → [Whisper Cheat-Sheet](guides/player-controls.md#5-mature-ai-command-delegation--whispers) |
| **Make bot alts automatically match my level** | Set `AiPlayerbot.SyncAltLevelToMaster = 1` in `aiplayerbot.conf` → [QoL Configuration](guides/configuration-tuning.md#1-player-quality-of-life-qol-flags) |
| **Pull a pack safely and retreat around a corner** | Target enemy and type `.bot action pullback` → [Tactical Party Actions](guides/player-controls.md#1-tactical-party-actions-bot-action-intent) |
| **Prevent bots from breaking Polymorph or Freezing Trap** | Target mark and type `.bot action cc <mark>` (pets auto-disengage) → [CC Discipline](concepts/bot-mechanics-and-quirks.md#2-tactical-interrupt--cc-resolution-gimmicks) |
| **Enable an active player economy on the Auction House** | Set `AiPlayerbot.AhMarketEnabled = 1` → [Living AH Economy](guides/living-world.md#4-the-living-auction-house-economy-ahmarketservice) |
| **Blacklist or override price of an item on the AH** | Type `.bot ah item <id> 0 0` (admin only) → [AH Admin Commands](guides/player-controls.md#6-auction-house-management-bot-ah--ahbot) |
| **Autofill missing roles for LFT dungeon runs** | Set `AiPlayerbot.RandomBotLftEnabled = 1` → [LFT Autofill](guides/living-world.md#5-automated-dungeon--battleground-queues) |
| **Inspect stuck bots and live positions on a 2D map** | Run Go daemon in `tools/observability` and open `http://localhost:8080` → [Observability Dashboard](guides/observability-dashboard.md) |
| **Understand Turtle custom spells (Viper, Holy Strike, Carve)** | Check individual class deep-dives or census TSV → [Class Overview](classes/overview.md) & [Spell Coverage](reference/spell-coverage.tsv) |
| **Port a new feature or fix a bug cleanly** | Follow headless session invariant & donor hierarchy → [Architecture Invariants](concepts/architecture-invariants.md) & [Donor Hierarchy](concepts/donor-hierarchy.md) |

---

## ⚔️ Class AI & Rotations Catalog

Detailed combat rotations, Turtle WoW 1.18.1 custom abilities, pet handling, and talent progress for all nine classes:

| Class | Primary Roles | Key Features & Custom Content |
| :--- | :--- | :--- |
| **[Warrior](classes/warrior.md)** | Tank (Prot), Melee DPS (Arms/Fury) | Stance dancing, taunt priority, *Master Strike* (54023), *Defensive Tactics* |
| **[Paladin](classes/paladin.md)** | Healer (Holy), Tank (Prot), Melee DPS (Ret) | Aura/Blessing coordination, *Holy Strike* (679), *Bulwark of the Righteous* (51346) |
| **[Hunter](classes/hunter.md)** | Ranged DPS (BM/MM), Melee DPS (Survival) | Pet feeding/CC safety, *Aspect of the Viper* (45651) regen, *Carve*, *Lacerate* |
| **[Rogue](classes/rogue.md)** | Melee DPS (Combat, Assassination, Subtlety) | Stealth openers, poison upkeep, *Surprise Attack*, *Noxious Assault*, *Envenom* |
| **[Priest](classes/priest.md)** | Healer (Holy/Disc), Ranged DPS (Shadow) | Healing ladder, Weakened Soul refusal, *Chastise* (51478), *Ascendance* |
| **[Shaman](classes/shaman.md)** | Healer (Resto), Melee DPS (Enh), Caster (Ele) | 4-element totem sets, *Earthquake* (48306), *Spirit Link*, *Bloodlust* |
| **[Mage](classes/mage.md)** | Ranged DPS (Frost, Fire, Arcane) | *Polymorph* CC, *Counterspell*, *Arcane Power* safety gate, *Evocation* cancel |
| **[Warlock](classes/warlock.md)** | Ranged DPS (Affliction, Demonology, Destruction) | DoT spread, demon summons, Soul Shard harvest, *Dark Harvest*, *Power Overwhelming* |
| **[Druid](classes/druid.md)** | Tank (Bear), Melee (Cat), Healer (Resto), Caster | Form maintenance, *Tree of Life Form* (45705), *Berserk*, *Swiftmend* gating |

---

## 📚 Guides & Operations

- [**Getting Started**](guides/getting-started.md) — Spawning owned bots, account ownership, inviting, and dungeon basics.
- [**Living World & Autonomous Bots**](guides/living-world.md) — Wandering bots, quest grinding, AH trading, guilds, and battlegrounds.
- [**Player Controls**](guides/player-controls.md) — Roster vs. Actions, `.bot action` command intents, and the `TBM:` addon protocol.
- [**Configuration & Tuning**](guides/configuration-tuning.md) — Plain-English explanation of `aiplayerbot.conf` and `tortoise_bots.conf` settings.
- [**Observability & Dashboard**](guides/observability-dashboard.md) — Go telemetry daemon, Prometheus metrics, and live browser dashboard.

---

## 🏛️ Concepts & Architecture

- [**Architecture Invariants**](concepts/architecture-invariants.md) — Non-negotiable rules: decoupled C++, headless sessions, zero core coupling.
- [**Strategy Engine Lifecycle**](concepts/strategy-engine.md) — Triggers, actions, multipliers, reaction queues, and failure backoffs.
- [**Bot Mechanics, Quirks & Gaps**](concepts/bot-mechanics-and-quirks.md) — Targeting math, threat distribution, movement, interrupts, and quirks.
- [**Donor Hierarchy & Porting Rules**](concepts/donor-hierarchy.md) — Shyalya runtime parity rules vs mod-playerbots behavior rules.
- [**Known Limitations & Non-Goals**](concepts/known-limitations.md) — Current engine boundaries, blocked features, and deliberate non-goals.
- [**Roadmap & Definition of Done**](PLAN.md) — Long-term architecture roadmap and acceptance criteria.

---

## 🗄️ Reference & Specifications

- [**Host API Contract**](HOST_API.md) — Technical C++ host boundary seams, session lifecycle, and packet bridges.
- [**Player Control Catalog**](PLAYER_CONTROL.md) — Technical command delivery specification and addon serialization.
- [**Source Provenance Ledger**](PROVENANCE.md) — Append-oriented attribution and donor lineage ledger.
- [**License Audit**](LICENSE_AUDIT.md) — Donor license compatibility analysis and release gates.
- [**Spell Coverage Reference**](reference/spell-coverage.tsv) — Census of custom Turtle WoW spells mapped into AI strategies.
- [**Talent Builds Reference**](reference/talent-builds.tsv) — Level 10–60 preset build progression rationale.
- [**Donor Capability Census**](reference/capabilities.tsv) — Granular inventory of donor features vs TortoiseBots implementation.
