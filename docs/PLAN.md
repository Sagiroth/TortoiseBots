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
* **Module boundary:** `modules/TortoiseBots/` — `TortoiseBotsModule.cpp` is the only loader file, `TortoiseBots.cmake` lists the real graph; host in `host/Bot*Adapter` (`Host/Session/Player/Chat/Packet`).
* **Runtime:** `PlayerbotAI` + `Engine/Strategy/Trigger/Action/Value`, 9 Vanilla classes (Warrior–Druid), `PlayerbotAIAdapter` owns AI.
* **Packet bridge:** `BotPacketAdapter` — Headless outgoing → `HandleBotOutgoingPacket`, master outgoing/incoming → `HandleMaster*`.
* **Commands:** `.bot add/remove/follow/invite/uninvite/stay/list/stats/command/help` (`.bot command` → `PlayerbotAI::HandleCommand`), account/GM-gated.
* **Random bots:** `RandomBotService`, bounded, discovers existing `RNDBOT*` characters; with `AiPlayerbot.RandomBotAutoCreate=1` (default `0`, one per `RandomBotUpdateInterval`, world-thread `CharacterCreation`, no raw SQL/DB worker) it also creates the deficit toward `MinRandomBots`/`MaxRandomBots` via `AccountMgr::CreateAccount` (random password, hashed) and generic `CharacterCreation::CreateCharacter` (core PR #412/7084557, final 7084557). AccountMgr async INSERT is handled via pending-name retry (one pending, bounded/log-throttled retry while continuing existing-account creation, no orphan accounts).

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

### 6.1 F-03/F-27 closure

`7353989c`/`07cf7976` pinned pair: F-03 (legacy LFT/slots/filters) and F-27 (`npc_teslinah` + `script_name='0'` → `20260825090000_world.sql`) closed where source proves it; 17 ScriptNames remain unverified content gaps (see `archive/PLAYERBOTS_AUDIT.md`).

### 6.2 Known-good pair

```text
TortoiseBots: 07cf7976c546fac27083c7b46e73299c25b095f3
Core:         7353989c94399f80572a2f8ec2eb73c63a6c79f8
```text

## 7. Manual gameplay phase

**Owned bot:** add/login, follow/stay, combat, loot, death/res, logout/relogin/reclaim, teleport.

**5-player dungeon:** `human + 4 bots` — tank/heal/DPS, interrupts/CC, loot/quests, doors/gossip, wipe recovery, regroup.

First roles: Warrior tank, Priest healer, Mage/Rogue/Hunter DPS. Expand from failures.

Then Turtle-specific: Goblin/High Elf, class/talent changes, custom portals, collection mounts.

## 8. Later roadmap

After 5-player is reliable: all classes/specs, more Turtle strategies, BGs, random account creation, AH/economy, addon UI, perf for larger pops, optional async LLM. Don't scale to 1000s before small party works.

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
