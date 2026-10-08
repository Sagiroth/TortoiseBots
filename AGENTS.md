# AGENTS.md

**TortoiseBots** is an optional native PlayerBots module for **Tortoise WoW 1.18.1** (<https://github.com/Sagiroth/TortoiseBots>). The core ([`tortoise-wow`](https://github.com/tortoise-wow/tortoise-wow) — "upstream", "core" and "core PR" mean this repo) must stay fully usable without it.

It is a hobby project built for its owner and interested players: pragmatic, small, working changes beat ceremony.

## Product direction

- **Gameplay/AI gold standard: mod-playerbots.** When behaviour can be better, port the modern mod-playerbots behaviour adapted to 1.12 rather than the older shyalya/cmangos lineage this module partly inherited.
- **Player-facing controls follow the owner's spec** in the issue for that command; the issue text is the contract for `.bot action` and the TBM addon.
- **Player control first.** Explicit orders beat automation; new automation ships behind a toggle, off by default.
- **Bots are always alive.** Activity throttling (`DisableActivityPriorities = 0`, `botActiveAlone`) is an opt-in kept for other forks' users, not the direction.

## Docs (OKF) — read and keep in sync

`docs/` is the source of truth for architecture, commands, configuration, classes and mechanics; start at [`docs/README.md`](docs/README.md) / [`docs/manifest.yaml`](docs/manifest.yaml). The code is the truth when they disagree — fix the doc.

| When you change | Update | Gate |
| :--- | :--- | :--- |
| Commands / actions (`commands/*`, `actions/*`) | [`docs/guides/player-controls.md`](docs/guides/player-controls.md) | `python3 tools/verify_okf.py` |
| Config (`PlayerbotAIConfig.*`, `aiplayerbot.conf.dist.in`, `conf/tortoise_bots.conf.dist`) | [`docs/guides/configuration-tuning.md`](docs/guides/configuration-tuning.md) | `python3 tools/verify_okf.py` |
| Class AI (`strategy/<class>/*`) | the class doc in [`docs/classes/`](docs/classes/) | `python3 tools/verify_okf.py` |
| Host seams, sessions, lifecycle (`host/*`, `runtime/BotManager.*`) | [`docs/HOST_API.md`](docs/HOST_API.md), [`docs/concepts/architecture-invariants.md`](docs/concepts/architecture-invariants.md) | `tools/verify_penqle_host_contract.sh` |
| Ported donor behaviour | [`docs/PROVENANCE.md`](docs/PROVENANCE.md) (feature, source repo + SHA + files, copied/ported/reimplemented, reason, validation) | — |
| Doc structure | `docs/manifest.yaml`, `docs/README.md` | `python3 tools/verify_okf.py` |
| Observability daemon (`tools/observability`, `runtime/ObservabilityEmitter.*`) | [`tools/observability/README.md`](tools/observability/README.md) | see that file |

## Architecture invariants

Full rules and rationale: [`docs/concepts/architecture-invariants.md`](docs/concepts/architecture-invariants.md). The ones you will hit:

- The core builds and runs with the module off (`MODULE_TORTOISEBOTS` selects it natively; `BUILD_LEGACY_PLAYERBOTS=OFF`).
- Core code never knows a `Player` is a bot: no `WorldSession::GetBot()`/`m_bot`/`sPlayerBotMgr`, no `if (IsBot())` in core systems. Bot sessions are generic headless transport (`SessionTransport::Headless`).
- Solve it inside the module first; then an existing `ScriptMgr`/lifecycle hook; a new core seam only as a generic concept, kept in the small host boundary, and proposed upstream.
- Coupling audit on core changes: `rg -n 'GetBot\(\)|SetBot\(|\bm_bot\b|sPlayerBotMgr|PlayerBotEntry' src` must stay explainable.

## Donors and references

Read-only local checkouts live in `../playerbots-references/` (`mod-playerbots`, `shyalya-tortoise-wow`, `spp-classics-cmangos`). **Harvest behaviour, not architecture**: understand the donor behaviour, reimplement it inside the module, test it, record provenance. Never vendor a donor tree or expand host coupling to ease a port.

Lookup order: gameplay/class AI/CC/movement → mod-playerbots, then shyalya-tortoise-wow for 1.18.1 specifics; sessions/lifecycle → the current core; Turtle spells/talents/items → core data (DBCs in `../tortoise-docker-penqle/data/dbc`, world DB).

## Working rules

- **Configuration:** a key's code fallback (`Get*Default`) must equal the value the shipped template sets, and a commented example must show the value that actually runs — otherwise deleting a line silently changes behaviour. A key that is read but unused says so in the template.
- **SQL:** migrations go to `data/sql/world/` or `data/sql/character/` (the core's updater reads the module source tree, folder names must match `Database.AutoUpdate.*UpdateName`), named `YYYYMMDDHHMMSS_<world|char>.sql`, idempotent. Never edit an applied migration — add a new one.
- **Performance:** nothing per bot tick hits the DB, scans the world or rebuilds strategy graphs; startup caches are built in one pass (O(rows), never item × template loops); expensive diagnostics are opt-in. Measure rather than guess.
- **Edits:** when adding a declaration or line next to an existing one, check `git diff` for accidental removals (`-` lines) before committing; count prepared-statement placeholders against columns.
- **Git:** check `git status --short` before and after; never stage, reset or discard changes you did not make.

## Validation

Use the smallest check that proves the change, and never report runtime behaviour you only read in code.

- Docs/config text only: `bash tools/verify_all.sh` (OKF, surface, wiring, unit and decision-trail tests; ~1 s) and `git diff --check`.
- Module C++: one cached build after the batch is coherent — the Docker dev loop in `../tortoise-docker-penqle` (`./dev/build-playerbots`, then `./dev/restart-server`; it builds the live `TortoiseBots` checkout). Read that repo's README first; never run destructive Docker/DB commands unless asked.
- Runtime: the smallest relevant check (server boots, module loads, bot enters world and acts, clean logout/shutdown, no DB/session errors in the log).
- Core-seam or build-gating change: module build while iterating, full module ON/OFF matrix before handing over.

## Reporting

End each task with: files changed (core vs module), any new host hook and why, builds/tests run and what was not run, runtime evidence, open issues and provenance.
