# TortoiseBots

TortoiseBots is an optional native PlayerBots module for [**Tortoise WoW 1.18.1**](https://github.com/Penqle/tortoise-wow).

It brings mature PlayerBots behavior to the Tortoise/Penqle core while keeping
PlayerBots out of normal core gameplay architecture.

The project is built around one rule:

> **Harvest behavior, not architecture.**

TortoiseBots reuses mature PlayerBots combat, movement, class, group, loot,
quest and travel behavior, but does not recreate the tightly coupled
`GetBot()` / `m_bot` / `sPlayerBotMgr` architecture used by older integrations.

---

## Project status

TortoiseBots currently provides a working native PlayerBots runtime with:

- Headless character sessions
- Same-account owned bots
- Native bot lifecycle management
- Mature `PlayerbotAI`
- Strategy / Trigger / Action / Value engine
- All nine Vanilla classes
- Follow / stay / group behavior
- Combat and class AI
- Loot and quest behavior
- Travel and taxi integration
- Native `.bot` command surface
- Packet bridge between normal Tortoise gameplay and PlayerbotAI
- Turtle Goblin and High Elf compatibility
- Turtle-specific spell / talent / race handling where validated against local data
- Optional random-bot infrastructure
- Native World / Character database migrations

The source tree has been cleaned to target **Vanilla/Turtle 1.18.1** rather than
remaining a multi-expansion PlayerBots donor tree.

Large TBC/WotLK/later-era families such as Death Knights, glyphs, vehicles,
arenas and other unsupported expansion systems have been removed from the
active product.

See:

- [PLAYERBOTS_AUDIT.md](docs/PLAYERBOTS_AUDIT.md)
- [PLAYERBOTS_HANDOVER.md](docs/PLAYERBOTS_HANDOVER.md)
- [PROVENANCE.md](docs/PROVENANCE.md)

for the exact validation state and remaining known gaps.

---

# Architecture

TortoiseBots is designed as an **optional native module**, not as a fork of the
entire Tortoise core.

```text
Tortoise / Penqle core
        |
        | small generic host capabilities
        v
TortoiseBots native module
        |
        +-- BotManager
        +-- PlayerbotAIAdapter
        +-- PlayerbotAIStorage
        +-- packet / player / chat adapters
        +-- PlayerbotAI
        +-- Engine
        +-- Strategy
        +-- Trigger
        +-- Action
        +-- Value
        +-- class AI
        +-- travel / quest / loot / group behavior
