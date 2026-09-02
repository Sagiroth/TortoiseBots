# PLAN.md — TortoiseBots architecture and roadmap

**Status:** Active architecture and roadmap
**Target:** Tortoise WoW 1.18.1 core
**Primary goal:** Useful PlayerBots for Turtle 1.18.1 without rebuilding the old tightly coupled core.

## 1. Product goal

Small human groups use owned bots for world content and 5-player dungeons, then grow deliberately to raids/BGs/random bots.

> give Tortoise a clean optional bot platform + reuse existing PlayerBots behavior (primarily AzerothCore/mod-playerbots) without inheriting its coupling

Core stays unaware of strategies, rotations, travel, or LLM.

## 2. Source of truth

1. pinned Tortoise core
2. Turtle SQL/DBC/extracted data
3. runtime behavior/logs
4. Shyalya for Turtle-specific reference
5. Vanilla/CMaNGOS/mod-playerbots/MangosZero for comparison

Knowledge Base = behavior spec, not architecture.

## 3. Non-negotiable architecture

See `AGENTS.md` §Architecture invariants for the 5 rules (optional module, no `GetBot`/`m_bot` coupling, prefer module-only changes, centralized host ≤5 files, generic Headless, LLM isolation) and the single-owner table. This file adds roadmap context only.

## 4. Implemented architecture (summary)

* **Session invariant:** `one account → at most one Network (account-keyed) + N Headless (GUID-keyed)`, World-owned, human reclaim wins. See `HOST_API.md` §3–4.
* **Headless lifecycle seam:** callers use only `World::StartHeadlessSession(accountId, characterGuid, locale, tag)`, `StopHeadlessSession(characterGuid, save)`, and `GetHeadlessSessionState(characterGuid)`; allocation, validation, async dispatch, callback identity, update, reclaim, deletion, and shutdown remain core-owned.
* **Account state:** Headless sessions update character online state while never writing `LoginDatabase` account `online`/`current_realm`; Network authentication and logout retain the normal account-state path.
* **Module boundary:** `modules/TortoiseBots/` — `TortoiseBotsModule.cpp` is the only loader file, `TortoiseBots.cmake` lists the real graph; host in `host/Bot*Adapter` (`Host/Session/Player/Chat/Packet`).
* **Runtime:** `PlayerbotAI` + `Engine/Strategy/Trigger/Action/Value`, 9 Vanilla classes (Warrior–Druid), `PlayerbotAIAdapter` owns AI.
* **Packet bridge:** `BotPacketAdapter` — Headless outgoing → `HandleBotOutgoingPacket`, master outgoing/incoming → `HandleMaster*`.
* **Player convenience:** `.bot summon` is a short-lived, player-owned transition in `PlayerConvenience`; `.bot pullback` dispatches the established PlayerbotAI pull/return strategy. `BotManager` remains lifecycle/AI ownership only. A convenience request is accepted asynchronously and fails closed if its bot, target or master becomes invalid.
* **Commands:** `.bot add/remove/logout/roster/action` now provide the Actions/Roster control plane; legacy `.bot follow/invite/uninvite/stay/guard/free/ready/attack/formation/list/stats/status/pullback/summon/command/help` remains account/GM-gated and compatible. `.bot command` still delegates to `PlayerbotAI::HandleCommand`; `.bot action` resolves target scope/executors server-side and emits structured `TBM:` results.
* **Random bots:** `RandomBotService`, bounded, discovers existing `RNDBOT*` characters; with `AiPlayerbot.RandomBotAutoCreate=1` (default `0`, one per `RandomBotUpdateInterval`, world-thread `CharacterCreation`, no raw SQL/DB worker) it also creates the deficit toward `MinRandomBots`/`MaxRandomBots` via `AccountMgr::CreateAccount` (random password, hashed) and generic `CharacterCreation::CreateCharacter` (core PR #416). LoginDatabase async INSERT (AllowAsyncTransactions, separate from core PR #416) is handled via pending-name retry (one pending, bounded/log-throttled retry while continuing existing-account creation, no orphan accounts); transient name collisions include `CHAR_NAME_PROFANE` and dynamic `CHAR_CREATE_DISABLED`/`PVP_TEAMS_VIOLATION` (faction balance) use 60s backoff, not permanent poisoning.
* **LFT fill:** `LftBotFillService` is default-off and bounded; it uses the generic core LFT participant/offer APIs, fills human-waiting role deficits with live Headless random bots, auto-accepts only module-owned machine offers, and leaves queue/group ownership in core.
* **Auction market:** `AhMarketService` is default-off and bounded; it uses real bot inventory, validated auctioneer positions, native auction transactions, and no donor market thread or direct auction writes.
* **BG auto-queue:** `BattlegroundQueueService` is default-off and bounded for WSG/AB/AV, consumes the generic #416 queued-participant snapshot so it fills only observed human demand, uses native queue ownership, validates brackets/state, forces AV solo, tracks service-owned queue pairs, and reconciles master reclaim.

Details: `HOST_API.md`.

## 5. Vanilla/Turtle boundary

Keep: 9 Vanilla classes, applicable raids/WSG/AB/AV, Turtle Goblin/High Elf + validated spells/talents, native LFG/taxi, applicable generic behavior. Don't re-add expansion systems (DK/glyph/vehicle/Arena) — `tools/verify_turtle_surface.sh` guards IDs.

## 6. Current milestone — gameplay acceptance

Freeze: no more broad donor cleanup.

1. preserve freeze
2. owned-bot acceptance (add/follow/combat/loot/death/relogin/teleport)
3. human+ bots 5-player dungeon (tank/heal/DPS/interrupts/CC/loot/wipe recovery)
4. fix observed defects only
5. broaden class/Turtle coverage from real failures

### 6.1 Player-owned convenience rule

Summon and pullback are first-class player experience features, not reasons to
put movement, combat or teleport state machines in `BotManager`. Summon keeps
only its short-lived transition in `behavior/PlayerConvenience`; pullback
dispatches the existing `PullRequestAction`/`PullStrategy` path, which owns
target checks, movement, spell choice, return and cleanup:

```text
request summon
query pending convenience work
world-thread update
```

`BotManager` remains the sole owner of Headless lifecycle, bot records and
durable master binding. A permitted requester who invokes summon or pullback
is rebound through `BotManager::BindBotMaster` before the action is accepted,
so a same-account character or GM is never told that an operation is queued
only for it to cancel because of a stale master GUID. Summon restores its
movement through `SetBotFollow` after arrival; pullback leaves movement to the
mature pull strategy. Neither path edits `BotRecord.masterGuid` or the AI's
live master pointer directly. Every pending summon must self-cancel on missing,
reclaimed, non-Headless, dead, teleporting or incompatible actors.

### 6.2 Merge-ready acceptance additions

When the actual merged #411/#416 pair is available, run these focused
acceptance checks alongside the existing owned-bot journey:

```bash
bash tools/verify_penqle_host_contract.sh --core /explicit/path/to/tortoise-wow
```

The verifier is read-only: it proves that the merged core exposes the generic
interfaces this module calls and that legacy bot-object coupling has not
returned to normal gameplay code. It is a pre-build gate, not a replacement
for the enabled/disabled build matrix or runtime acceptance.

Use [`MERGE_ACCEPTANCE.md`](MERGE_ACCEPTANCE.md) for the exact ON/OFF build,
fixture, real-client, and default-off optional-service sequence.

* **Summon:** same-map and cross-map owned bot; dead, combat, taxi and
  teleport rejection; duplicate-request rejection; completed teleport resumes
  durable follow; logout/reclaim during the delay cancels without a stale
  record or movement command.
* **Pullback:** a selected owned tank uses the mature `PullRequestAction` and
  `pull back` strategy to validate the target, pull, return and clean up. The
  module must not issue direct movement generators or duplicate those states.

These are runtime gates, not claims based on static inspection.

### 6.3 F-03/F-27 closure

`7353989c`/`07cf7976` pinned pair: F-03 (legacy LFT/slots/filters) and F-27 (`npc_teslinah` + `script_name='0'` → `20260825090000_world.sql`) closed where source proves it; 17 ScriptNames remain unverified content gaps (see `archive/PLAYERBOTS_AUDIT.md`).

### 6.4 Known-good pair

```text
TortoiseBots: b9c7784
Core #411:   8037fc8
Core #416:   e63161c (rebased on corrected Core #411)
```

## 7. Manual gameplay phase

**Owned bot:** add/login, follow/stay, combat, loot, death/res, logout/relogin/reclaim, teleport.

**5-player dungeon:** `human + 4 bots` — tank/heal/DPS, interrupts/CC, loot/quests, doors/gossip, wipe recovery, regroup.

First roles: Warrior tank, Priest healer, Mage/Rogue/Hunter DPS. Expand from failures.

Then Turtle-specific: Goblin/High Elf, class/talent changes, collection mounts.

## 8. Later roadmap

After 5-player is reliable: all classes/specs, more Turtle strategies, broader BG/AH/random-population tuning, addon UI, perf for larger pops, optional async LLM. The bounded default-off LFT fill, AH market, BG auto-queue, and RNDBOT auto-create slices are implemented now; don't scale to 1000s before the small party works.

## 9. Performance, security, provenance

* **Perf:** no DB/world scan/network on bot tick, no graph rebuild, cache immutable data, opt-in diagnostics — see `AGENTS.md` §Performance rules.
* **Security:** owned character = requesting account, human reclaim wins, bot commands verify owner/GM, chat not a remote-control channel.
* **Provenance:** donor pools = Knowledge Base, CMaNGOS PlayerBots, Shyalya, MangosZero, mod-playerbots; rule `study → extract intent → port → validate`; record in `PROVENANCE.md`.

## 10. Validation

Use smallest check that proves change — see `AGENTS.md` §Validation. Previous success remains evidence for unchanged code.

## 11. Definition of done

**Baseline:** core builds without module, module builds only when selected, no `GetBot` API, GUID-keyed Headless, safe Network+Headless, provenance recorded.

**First release:** human+ bots work in world + 5-player dungeon (tank/heal/DPS, follow/recover/regroup), core stays optional, install/build documented.

## 12. Working docs

* `PLAN.md` — roadmap (this file)
* `HOST_API.md` — host contract
* `PROVENANCE.md` — lineage
* `archive/PLAYERBOTS_AUDIT.md` — historical evidence
