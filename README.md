# TortoiseBots — Tortoise WoW 1.18.1 PlayerBots (private)

Clean, optional PlayerBots module for **Tortoise WoW 1.18.1** (Penqle/tortoise-wow, after PR #396).

> **Goal:** Harvest mature PlayerBots behavior without inheriting mature PlayerBots coupling. Keep the Tortoise core usable without bots (`BUILD_PLAYERBOTS=OFF`).

- **Plan / source of truth:** [`docs/PLAN.md`](docs/PLAN.md) — also mirrored online at https://github.com/tortoise-wow-stack/TortoiseBots/blob/main/docs/PLAN.md
- **Host boundary (Phase 1):** [`docs/HOST_API.md`](docs/HOST_API.md)
- **Historical research:** [`docs/DISCOVERY.md`](docs/DISCOVERY.md) (archive, not the plan)
- **Agent guide:** [`AGENTS.md`](AGENTS.md) — read first; repo is agnostic to absolute paths — check for a local checkout first before pulling

This repository is the **private development** counterpart to the public `tortoise-wow-stack` org. It is intentionally agnostic to where checkouts live on disk — before cloning anything, look for a sibling local checkout (e.g. `playerbots-references/`, `TortoiseWoWKnowledgeBase`, `tortoise-docker-penqle`).

Online canonical references (use only if local not present):

- Knowledge Base: https://github.com/tortoise-wow-stack/TortoiseWoWKnowledgeBase
- Penqle core: https://github.com/Penqle/tortoise-wow
- Upstream behavior: https://github.com/cmangos/playerbots, https://github.com/mangoszero/server, https://github.com/Shyalya/tortoise-wow

## Status

Current development is in **Phase 4 stabilization**.

The module is packaged through Penqle's native `modules/<name>/` loader. Only
`src/TortoiseBotsModule.cpp` lives below the loader's recursive `src/` tree;
the positive Vanilla/Turtle source graph is selected explicitly by
`TortoiseBots.cmake`. Core-owned LFG/meeting-stone and transport concepts are
retained, while donor-only expansion families are absent from the module tree.

- Phase 3 headless lifecycle/session foundation is implemented.
- Native Penqle module discovery, ScriptMgr adapters, module-local AI ownership,
  and the broad Vanilla/Turtle strategy/value/trigger/action source set are
  compiled and linked into `mangosd`.
- `BUILD_PLAYERBOTS=OFF` with modules disabled and the explicit native
  `MODULE_TORTOISEBOTS=static` configuration both pass the cached `mangosd`
  build. The legacy `BUILD_PLAYERBOTS` switch is not the native module
  selector and should remain off unless the separate legacy escape hatch is
  deliberately being tested.
- The local runtime stack starts the native module and reaches “World server is
  up and running”; AI remains disabled unless `aiplayerbot.conf` explicitly
  enables it.
- The native install also carries the optional AI cache/state migrations and
  `tortoise_bots.conf`; random-bot autologin and account/character creation
  default to off. Enable random bots only with pre-existing configured random
  characters until the creation workflow is added.
- Module SQL is installed under the core's case-sensitive configured
  `world/` and `character/` AutoUpdater folders. The additive
  `20260824090002_*` migrations repair already-created cache/help tables; the
  `20260824090003_*` migrations remove only obsolete module-owned donor caches.
- `tools/verify_turtle_surface.sh` is the cheap pre-build guard for migration
  paths, removed donor/test families, known-absent IDs, and legacy-option
  forcing.
- The required generic Penqle seam is
  `playerbots-integration-gh@9487c5150a6553c665fafc1f4568669b8b00f011`.
- The fresh Docker packet journey passed native group invite/accept and
  cleanup, with real Warrior, Mage, Priest, and Hunter class contexts. The
  preserved runtime also contains disposable `TBPLAY` class fixtures for
  manual client playtesting.
- The final correctness pass centralizes follow/wander/stay transitions,
  removes the redundant controller state and old controller movement loop, strictly validates automatic
  packet-triggered invitation acceptance, preserves master GUIDs across human
  reconnects, and uses the documented `AiPlayerbot.MaxRandomBotRandomizeTime`
  key. The remaining live gate is the real-client journey, which is left to
  the manual playtest.
- Native `.bot stay` now reuses the mature stay shortcut, including its current
  position anchors. Reconnect preserves the live mature movement strategies,
  and random-bot human adoption/release updates the durable `BotRecord` master
  relationship.

The native `.bot` surface includes `add`, `remove`, `follow`, `invite`,
`uninvite`, `stay`, `list`, `stats`, and same-account mature-AI `command`.

Owned-bot commands require the character to belong to the requester’s account;
GM-level handlers intentionally retain an administrative override. `invite`
reports only that the native group invitation was sent, and `command` reports
forwarding to mature `PlayerbotAI`; neither claims that the resulting action or
group join succeeded. Built-in diagnostics require the disposable `TBPLAY`
account or `TBPLAY`-prefixed fixtures and are disabled by default.

The first product target is intentionally small and playable: log into one normal character, spawn your own alts as Headless bots, party with them, and level together.

## Quick start (local-first)

1. Check whether sibling checkouts already exist alongside this repo (`playerbots-references/`, `TortoiseWoWKnowledgeBase`, `tortoise-docker-penqle`).
2. If not present, clone from the online URLs above.
3. Read `AGENTS.md` → `docs/PLAN.md` → `docs/HOST_API.md`.

The native core build is enabled with:

```sh
cmake -S <tortoise-wow> -B <build> -DMODULES=static -DMODULE_TORTOISEBOTS=static \
  -DBUILD_LEGACY_PLAYERBOTS=OFF
cmake --build <build> --target mangosd
```

Do not set `BUILD_PLAYERBOTS=ON` to select this module. That option belongs to
the core's separate legacy PlayerBots escape hatch; native linkage is explicit
through `MODULE_TORTOISEBOTS` and the module build has no legacy vendored
PlayerBots source path.

For normal source edits, keep this checkout as the working repository and use
the sibling `tortoise-docker-penqle` checkout's persistent builder:

```sh
cd ../tortoise-docker-penqle
bash dev/build-playerbots
# only when runtime verification is needed:
bash dev/restart-server
```

Reuse the existing build directory and runtime stack; normal edits do not
require a Docker image rebuild or a sync/copy workflow.


## Development loop

Keep iteration fast:

```text
coherent edit batch
-> one cached native MODULE_TORTOISEBOTS build
-> smallest relevant runtime/manual check
-> continue
```

Do **not** run the full OFF/ON matrix after every edit. Run it once when a shared core/CMake change has stabilized or at a phase/PR/handover boundary.

If a manual gameplay test already passed and the relevant code has not changed, do not repeat it just to reconfirm unchanged behavior.
