# Merged-core acceptance runbook

Use this runbook only after Penqle core PRs #411 and #416 have merged, or
when validating the exact reviewed pair in an explicitly supplied core
checkout. It records evidence; it does not claim that a source inspection is a
runtime pass.

## Record the exact pair

From the module and core checkouts, record the two source revisions before any
configuration or runtime claim:

```bash
git -C /explicit/path/to/TortoiseBots rev-parse HEAD
git -C /explicit/path/to/tortoise-wow rev-parse HEAD
```

The core must include #411 before #416. Do not infer compatibility from a
branch name.

## 1. Host-contract gate

```bash
cd /explicit/path/to/TortoiseBots
bash tools/verify_penqle_host_contract.sh --core /explicit/path/to/tortoise-wow
bash tools/verify_turtle_surface.sh
git diff --check
```

Expected evidence:

* generic `StartHeadlessSession`, `StopHeadlessSession`, and
  `GetHeadlessSessionState` are present;
* generic character-creation, LFT participant, and BG demand interfaces are
  present;
* no legacy `GetBot`/`m_bot`/manager coupling exists in normal core gameplay
  source;
* TortoiseBots contains no direct raw Headless registry calls.

Stop here if any check fails. Do not recreate the missing capability in the
module or add PlayerBots-specific state to core.

## 2. Build gates

Configure against the same core checkout with the module at
`modules/TortoiseBots/`.

```bash
cmake -S /explicit/path/to/tortoise-wow -B /explicit/build/playerbots-on \
  -DBUILD_LEGACY_PLAYERBOTS=OFF \
  -DMODULES=static \
  -DMODULE_TORTOISEBOTS=static
cmake --build /explicit/build/playerbots-on --target mangosd
```

Then prove the core remains usable without the module:

```bash
cmake -S /explicit/path/to/tortoise-wow -B /explicit/build/playerbots-off \
  -DBUILD_LEGACY_PLAYERBOTS=OFF \
  -DMODULES=disabled \
  -DMODULE_TORTOISEBOTS=disabled
cmake --build /explicit/build/playerbots-off --target mangosd
```

Record both configure lines, target output, and the module/core revisions. A
successful ON build is not evidence for the OFF build, or vice versa.

## 3. Disposable fixture gate

Use only a `TBPLAY` account or `TBPLAY`-prefixed characters. Enable one
fixture at a time in `tortoise_bots.conf`:

* `PendingAddRemoveTest` — no pending session, live player, or bot record
  remains after add/remove;
* `AutoTest` — Headless login, AI attach, save, logout, relogin, cleanup;
* `PacketBridgeTest` — native command dispatch and group invite/accept/cleanup.

Preserve the relevant log lines. These fixtures do not prove real client
incoming packets or live gameplay.

## 4. Owned-bot manual-client gate

Use a real Network client and a same-account bot. Record each observed result:

| Journey | Required observation |
| --- | --- |
| Lifecycle | add, login, remove, logout/relogin, human reclaim, clean shutdown |
| Movement | follow, stay, guard, free, formation selection, teleport/map transition |
| Combat | attack selected target, ready check, tank/heal/DPS response, loot, death/resurrection |
| Convenience | pullback and summon: rejection states, timeout/cancellation, follow after summon arrival |
| Commands | list, stats, status, invite/uninvite, all newly exposed aliases |

Treat each non-observed item as pending. A server-side synthetic packet fixture
does not substitute for client incoming-packet evidence.

## 5. Optional service gates

Keep all services default-off until the owned-bot gate passes. Enable and test
one bounded service at a time:

1. random pool and auto-create;
2. LFT fill with real human demand and offer/cancel paths;
3. AH posting through native auctions;
4. WSG/AB/AV demand-aware queueing and master reclaim.

For each, capture its configuration, population/queue state before and after,
and relevant logs. Never enable broad population or multiple services together
as the first runtime experiment.
