# TortoiseBots

An independent, native PlayerBots module for the canonical **Tortoise WoW 1.18.1** core repository ([`tortoise-wow`](https://github.com/tortoise-wow/tortoise-wow), `main` branch).

`TortoiseBots` delivers native AI companions through a decoupled C++ architecture: bot AI, combat strategies, and lifecycle management live entirely within this module, while session transport and character state stay cleanly owned by the core via generic headless sessions (`SessionTransport::Headless`). The core builds and runs 100% cleanly without the module (`MODULES=disabled`).

> **Companion in-game UI:** pair with [**TortoiseBotsManager**](https://github.com/Sagiroth/TortoiseBotsManager) (`/tbm`). All player-facing control — roster, lifecycle, and tactical party actions — is driven from this addon; the server-side command surface is an internal/advanced transport, not a player API.

---

## ✨ What It Does

* **A living world.** A pool of autonomous bots quests, grinds, trains, trades and travels on its own. The pool is configurable for different playstyles, from a fresh realm where everyone starts at level 1 to a populated world with bots at every level.
* **Your own party.** Play with bot alts from your own account or hire companions; they follow, fight, heal and tank in dungeons and raids, controlled from the TortoiseBotsManager addon.
* **Full class AI** for all 9 classes, including Turtle WoW 1.18.1 custom abilities.
* **Observability.** A web dashboard with a live map, per-bot activity and incidents.

Details and every setting are in the [documentation](docs/README.md).

## 🎮 What Bots Can Do

Player-facing summary, no internals. Every claim below exists in the code on this branch; each category links to the full guide.

<details><summary><b>🌍 Living world & autonomy</b> — questing, grinding, travel, professions, auction house, guilds</summary>

- Quest, grind and explore on their own: pick up level-fitting quests, fight level-appropriate mobs, rest with food and water, and return to town to repair, sell junk, learn spells and turn in.
- Travel like players: flight paths, zeppelins, boats and hearthstones (no teleports).
- Gather and craft: herbalism/mining runs from level 10, skinning what they kill, fishing while idle, materials feeding crafting and the Auction House.
- Trade on the Auction House with their own loot at market prices; your auctions get bought with real gold in your mailbox.
- Social life: greet passing players, invite lone questers to a party, form and join guilds with custom names.
-
- Details: [Living World & Autonomous Bots](docs/guides/living-world.md).
</details>

<details><summary><b>👥 Party play</b> — follow, assist, buffs, ready checks, loot, companions</summary>

- Follow, stay, guard or roam free; assist the tank or your target and focus the Skull-marked kill priority.
- Keep party buffs up automatically (single and group versions); bots can hold a ready-check confirm until buffs settle (opt-in).
- Answer ready checks, roll Need/Greed/Pass on loot, share conjured food, water and healthstones through trade.
- Hire companions at inn recruiters: fresh characters at your level with talents, spells and spec-weighted gear; summon stragglers to your side.
- Line-of-sight corner pulling (`pullback`), crowd-control marks that the party will not break, and an opt-in AoE fear/CC safety net.
-
- Details: [Player Controls](docs/guides/player-controls.md), [Dungeon & Raid Tactics](docs/guides/dungeon-tactics.md).
</details>

<details><summary><b>🛡️ Tanking · 💚 Healing · ⚔️ DPS</b> — rotations, interrupts, dispels, threat, positioning</summary>

- One rotation per class and spec, rebuilt when you set `.bot role <Name> tank|healer|dps` — tanks hold aggro and turn mobs away, healers triage the party, DPS focus fire.
- Interrupt on order (`.bot action interrupt`): the first party bot with a ready interrupt closes in and casts it.
- Dispel magic, curses, poisons and diseases off the party; ease off when near the tank's threat.
- Positioning: melee strike from behind the target, step out of AoE and hazards, spread apart on order, flee and regroup when told.
-
- Details: [Class AI & Rotations](docs/classes/overview.md).
</details>

<details><summary><b>🧙 Class highlights</b> — one line per class</summary>

- **Warrior:** Protection tanking with stance discipline, Arms/Fury DPS with Execute and cleave.
- **Paladin:** Holy healing, Protection AoE-threat tanking, Retribution burst; auras and blessings coordinated across the party.
- **Hunter:** Beast Mastery / Marksmanship ranged DPS plus Turtle Survival melee; tames a pet at 10, feeds, mends, calls and revives it.
- **Rogue:** stealth openers, combo-point finishers, poison upkeep, Sap/Blind control, lockbox unlocking.
- **Priest:** Holy/Discipline healing ladder, Shadow DPS, Shackle Undead, Fear Ward and dispels.
- **Shaman:** Restoration Chain Heal, Enhancement melee, Elemental burst; four-element totem sets and Earth Shock interrupts.
- **Mage:** Frost/Fire/Arcane DPS, Polymorph CC, Counterspell, Evocation and Ice Block safety.
- **Warlock:** DoT spread across attackers, demon summons (Voidwalker, Succubus, Felhunter), Soul Shards, Banish/Fear, Unending Breath while swimming.
- **Druid:** Bear tank, Cat DPS, Restoration healing, Balance casting — shapeshifts to the job, innervates healers, removes poisons and curses.
-
- Details: [Class AI & Rotations](docs/classes/overview.md) and each class page under `docs/classes/`.
</details>

<details><summary><b>🐉 Raids & dungeons</b> — only bosses with wired tactics are listed</summary>

- Entering a raid auto-enables its tactics; everywhere: bomb carriers run clear, non-tanks sidestep breath/tail cones, casters spread, tanks can drag dragons away on order.
- **Molten Core:** Magmadar, Baron Geddon (Inferno runout); rune dousing on order.
- **Blackwing Lair:** rogues disarm suppression devices; Broodlord ranged stand-off; Vaelastrasz Burning Adrenaline flee; Nefarian Wild Magic Ice Block.
- **Onyxia:** air-phase ranged fire with spread while she flies.
- **Naxxramas:** Four Horsemen void zones; Sapphiron air-phase hide and blizzard step-out.
- **Emerald Sanctum:** Solnius Emerald Rot runout and add focus. **Lower Karazhan:** Araxxna spiderling cleave, Moroes re-target. Karazhan Crypt puzzles stay manual.
- 5-player dungeons: corner pulls, CC marks, wipe recovery (release + corpse run), summon to regroup.
-
- Details: [Dungeon & Raid Tactics](docs/guides/dungeon-tactics.md).
</details>

<details><summary><b>⚔️ PvP battlegrounds</b> — WSG, AB, AV alongside real players</summary>

- Queue at the battlemaster with Join as Group (WSG/AB; AV solo): your party bots queue with you and auto-accept the invite.
- Random bots queue only while real players wait — to balance teams and start the match, never on their own.
- Per-map tactics: WSG flag capture and chasing the enemy flag carrier, AB/AV objectives, battleground buffs and banners.
-
- Details: [Living World](docs/guides/living-world.md#6-automated-dungeon--battleground-queues).
</details>

<details><summary><b>🔍 Dungeon finder (LFT)</b> — bots fill your missing roles</summary>

- On by default: when you queue and wait on a role (often tank or healer), matching idle bots queue and accept the invite.
- Role match is strict — a tank slot gets a real tank with a shield equipped, a healer gets real healing spells — or it stays empty with a logged reason.
- Your own party bots auto-accept the dungeon offer; everyone walks to the portal (no teleport — summon stragglers).
-
- Details: [Living World](docs/guides/living-world.md#6-automated-dungeon--battleground-queues).
</details>

<details><summary><b>🎛️ Player control</b> — commands, addon, formations</summary>

- `.bot action` intents: attack, pull, pullback, focus, cc, aoe on/off, flee, stop, stay, follow, summon, trade, release, corpse run.
- Roster commands: add, hire, role, invite, summon, maintenance, autogear, remove — or whisper a bot directly.
- [TortoiseBotsManager](https://github.com/Sagiroth/TortoiseBotsManager) (`/tbm`) addon: roster, party actions, bot panel (gear, bags, behaviour toggles) over a silent channel.
- Formations (arrow, queue, near, line, circle, shield), opt-in combat spread, mimic-consumables toggle, per-bot loot/AoE/mana/threat/potion switches.
-
- Details: [Player Controls](docs/guides/player-controls.md).
</details>

<details><summary><b>📊 Observability dashboard</b> — watch the fleet (optional)</summary>

- Optional web dashboard: live 2D world map with per-bot markers, bot list with gear/talent/activity profiles, stuck-bot issue tracker.
- Off by default, zero overhead while disabled.
-
- Details: [Observability Dashboard](docs/guides/observability-dashboard.md).
</details>

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

Install puts the templates next to `mangosd.conf` (`aiplayerbot.conf.dist`, `modules/tortoise_bots.conf.dist`) and creates `aiplayerbot.conf` and `modules/tortoise_bots.conf` from them on the first install only; a reinstall keeps your edited files. Module SQL in `data/sql/world` and `data/sql/character` is applied by the core auto-updater on startup. See [**Configuration & Tuning**](docs/guides/configuration-tuning.md) for available settings.

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
