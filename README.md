# TortoiseBots


[![Latest release](https://img.shields.io/github/v/release/Sagiroth/TortoiseBots?label=release&logo=github)](https://github.com/Sagiroth/TortoiseBots/releases/latest)
[![Linux + Windows build](https://img.shields.io/github/actions/workflow/status/Sagiroth/TortoiseBots/build.yml?branch=main&event=push&label=Linux%20%2B%20Windows%20build&logo=github)](https://github.com/Sagiroth/TortoiseBots/actions/workflows/build.yml?query=branch%3Amain)
[![Support on Ko-fi](https://img.shields.io/badge/Ko--fi-support-FF5E5B?logo=ko-fi&logoColor=white)](https://ko-fi.com/sagiroth)
[![Discord](https://img.shields.io/badge/Discord-join-5865F2?logo=discord&logoColor=white)](https://discord.gg/FT2rZcXm3)

An independent, native PlayerBots module for the canonical **Tortoise WoW 1.18.1** core repository ([`tortoise-wow`](https://github.com/tortoise-wow/tortoise-wow), `main` branch). This is an unofficial, community-driven project, not affiliated with Turtle WoW or Blizzard Entertainment.

TortoiseBots delivers native AI companions through a decoupled C++ architecture: bot AI, combat strategies, and lifecycle management live entirely within this module, while bots run as headless sessions with no game client and character state stays cleanly owned by the core. The core compiles and runs cleanly without the module.

> [!TIP]
> **New here? Start with [Getting Started](docs/play/getting-started.md).**
> It covers client setup, the [TortoiseBotsManager](https://github.com/Sagiroth/TortoiseBotsManager) (`/tbm`) addon, and bringing your own account alts into your party.
> Then go through the [feature table](docs/play/features.md) and turn on what you want. Each row says whether it's on by default and which flag enables it.

### 🐢 Vision

TortoiseBots should make Tortoise WoW feel like a living, populated vanilla server for a player who plays alone. The player levels and gears up at their own pace, and bots fill the world and their group the way real players would.

- **Organic living world:** Random pool bots start at level 1 in starter zones, questing, grinding, and traveling by foot and flight path. Long-range teleports are a stuck-recovery safety net, not normal movement.
- **Earned progression:** No free gold top-ups, gear handouts, or synthetic AH supply or buyers by default. Equipment and riches come from playing the game.
- **Player control first:** Explicit player orders always override automation. Extra automation for player-owned bots sits behind opt-in configuration flags.
- **Modern AI standard:** Gameplay and class AI adapt modern mod-playerbots behavior to Vanilla 1.12 and Turtle WoW 1.18.1 content.

### ✨ Highlights

- [Your Party](docs/play/features.md#your-party-owned-bots-hiring-following-commands): Adventure with your own account alts or hire mercenaries at inn recruiters; command formations, follow behavior, and tactical actions.
- [Combat & Classes](docs/play/features.md#combat--classes): Tailored combat rotations for all nine classes, Turtle custom abilities, interrupt coordination, and spec-based roles.
- [Dungeons & Raids](docs/play/features.md#dungeons--raids): Corner pullback line-of-sight pulls, CC protection, boss encounter tactics, hazard avoidance, and wipe recovery.
- [The Living World](docs/play/features.md#the-living-world-pool-bots-leveling-questing-wandering): Autonomous pool bots level from 1, grind quests, train affordable spell ranks at trainers, gather materials, and travel by foot and flight path.
- [Social Systems](docs/play/features.md#social-invites-guilds-trading): Roaming bots invite solo questers to groups, greet passing players, establish autonomous guilds, and trade conjured food or water.
- [Living Economy](docs/play/features.md#economy-auction-house): Optional native Auction House service where pool bots appraise loot, list surplus items, and bid on upgrades.
- [Dungeon Finder & PvP](docs/play/features.md#dungeon-finder--battlegrounds): Looking for Turtles queue backfill for missing roles and battleground matchmaking for WSG, AB, and AV.
- [Server & Monitoring](docs/play/features.md#server--monitoring-dashboard-logs): Live browser map, Prometheus telemetry metrics, in-game bot panel, and structured CSV logging.

## 📚 Documentation

Detailed guides, class breakdowns, commands, and architecture rules live in the documentation catalog:

| Guide / Reference | Description |
| :--- | :--- |
| **[Getting Started](docs/play/getting-started.md)** | **Start here:** client setup, `/tbm` addon, summoning alts, and first party setup |
| [What Bots Can Do](docs/play/features.md) | Feature catalog, defaults, and configuration toggle flags |
| [Documentation Start Page](docs/README.md) | Central switchboard, quick task finder, and audience directory |
| [Player Commands](docs/play/commands/README.md) | Complete `.bot` command reference, whisper syntax, and tactical intents |
| [Class AI & Rotations](docs/classes/overview.md) | Rotations, specs, and Turtle WoW 1.18.1 custom abilities for all classes |
| [Dungeon & Raid Tactics](docs/play/dungeons-and-raids.md) | Line-of-sight pullback pulls, CC discipline, and wipe recovery |
| [Living World & Economy](docs/world/living-world.md) | Autonomous roaming, quest grinding, vendor trips, and guilds |
| [Configuration & Tuning](docs/server/configuration.md) | Plain-English settings reference, presets, and configuration files |
| [Observability Dashboard](docs/server/dashboard.md) | Real-time web dashboard, 2D live map, and Prometheus metrics |
| [Architecture Invariants](docs/developers/architecture.md) | Decoupled C++ rules, headless sessions, and host boundary seams |

## 🛠️ Quick Build

Inside your `tortoise-wow` (`main` branch) checkout:

```bash
# Clone module into modules/
git clone https://github.com/Sagiroth/TortoiseBots.git modules/TortoiseBots

# Configure and compile
cmake -B build -DMODULES=static -DMODULE_TORTOISEBOTS=static
cmake --build build -j"$(nproc)"
```

Install puts the templates next to `mangosd.conf` (`aiplayerbot.conf.dist`, `modules/tortoise_bots.conf.dist`) and creates `aiplayerbot.conf` and `modules/tortoise_bots.conf` from them on the first install only; a reinstall keeps your edited files. Module SQL in the source-tree `data/sql/world` and `data/sql/character` is applied by the core auto-updater on startup. See [Configuration & Tuning](docs/server/configuration.md) for available settings.

## 🙏 Credit

TortoiseBots is made by **Sagiroth**. Forks, repacks, Docker bundles, videos and
public servers are welcome — please credit it. Ready to paste:

```text
Playerbots: TortoiseBots by Sagiroth — https://github.com/Sagiroth/TortoiseBots
```

The in-game login message and `.bot version` / `.bot about` show the project attribution line (`by Sagiroth`, source URL, licence); keep it visible in
modified builds (see [`NOTICE.md`](NOTICE.md#attribution-author-notice)).

### ☕ Support

If you enjoy TortoiseBots and would like to support its development, you can
buy me a coffee on [**Ko-fi**](https://ko-fi.com/sagiroth). It's completely
optional: every feature stays free and open for everyone.

## 📜 License & Provenance

Original TortoiseBots work is licensed under **AGPL-3.0-or-later** ([`LICENSE`](LICENSE)). If you distribute it, or run a modified version on a server players connect to, you must publish the full source of that version under the same licence. Contributions are accepted under the same terms. Donor code keeps its own notices (GPL-2.0-or-later / AGPL-3.0 lineages). See [`NOTICE.md`](NOTICE.md) and [`docs/developers/licensing-audit.md`](docs/developers/licensing-audit.md).

**Disclaimer:** TortoiseBots is a non-commercial hobby project. Voluntary donations (Ko-fi) only support development; they buy no access, features or service, and are not tied to any server. The author is not affiliated with, does not operate, endorse or receive money from any server that uses this code, and is not responsible for how third parties use it. TortoiseBots is not affiliated with Turtle WoW or its creators. World of Warcraft is a trademark of Blizzard Entertainment; this project is not affiliated with Blizzard.
