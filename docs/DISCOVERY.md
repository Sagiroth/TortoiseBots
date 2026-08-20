# Playerbot Discovery — 1.12.1 mongosd / Turtle-WoW 1.18.1

**Date:** 2026-08-20  
**Context:** Research for Penqle/tortoise-wow (1.18.1 build 7272). Clean canvas after `PR 396` (deletes `src/game/PlayerBots`). Goal: understand bases for a self-contained 1.18.1 bot module like `mod-playerbots` for 3.3.5a, avoid inheriting legacy debt.

---

## 1. Latest playerbots for 1.12.1 mongosd (MaNGOS)

All target `1.12.1 (5875)` via MaNGOS-Zero / CMaNGOS-Classic family. They do NOT support `1.13.x` NuClassic.

| Rank | Repo | Core | Last Push (API) | Stars | Notes |
| ------ | ------ | ------ | ----------------- | ------- | ------- |
| **1 (RECOMMENDED)** | `cmangos/playerbots` — <https://github.com/cmangos/playerbots><br>`cmangos/mangos-classic` — <https://github.com/cmangos/mangos-classic> | CMaNGOS Classic | `2026-08-16` / `2026-08-19` | 130 / 1066 | Bot AI Core from ike3 for classic/tbc/wotlk. Modular: `BUILD_PLAYERBOTS=ON`, `src/modules/Bots`. `aiplayerbot.conf.dist.in` per expansion. Daily commits, GitHub Actions Linux/Mac/Windows. |
| 2 | `mangoszero/server` — <https://github.com/mangoszero/server> | MangosZero vanilla | `2026-08-20` TODAY | 1023 | Full core 1.12.1-1.12.3. Integrated lightweight `src/modules/Bots/playerbot` + `ahbot`. `AiPlayerbot.Enabled=1`. Option `PLAYERBOTS OFF` by default. Fresh correctness campaign `2026-08-20`. |
| 3 | `ike3/mangosbot` — <https://github.com/ike3/mangosbot> | MangosZero integrated | `2023-11-20` (stale) | 213 | Original integrated fork. `master`=1.12.1. Superseded by `cmangos/playerbots`. Do not use for new server. |
| 4 (legacy) | `playerbot/mangos` — <https://github.com/playerbot/mangos> | - | `2014-01-14` | 69 | Archive only |

Build `cmangos` example:

```bash
git clone https://github.com/cmangos/mangos-classic.git
git clone https://github.com/cmangos/playerbots.git mangos-classic/src/modules/Bots
cmake -S mangos-classic -B build -DBUILD_PLAYERBOTS=ON
```

---

## 2. The freshest 2 — verification

Verified via `api.github.com` on 2026-08-20:

* `ike3/mangosbot` `pushed: 2023-11-20T11:47:39Z` — ~2.7 years dead (your observation correct, ~4y in spirit)
* `cmangos/playerbots` `pushed: 2026-08-16T07:12:46Z` — 4 days ago — commit `Add misdirection for pull and fix pet attack`
* `mangoszero/server` `pushed: 2026-08-20T01:49:49Z` — today — commit `Playerbot correctness campaign for Zero: movement, visibility, class combat, lifecycle`
* `cmangos/mangos-classic` `pushed: 2026-08-19T20:57:21Z`

So freshest 2 for 1.12.1 mongosd are **cmangos/playerbots + cmangos/mangos-classic** and **mangoszero/server**. `ike3` is abandoned, its code lives on as `cmangos/playerbots`.

---

## 3. What Shyalya/tortoise-wow runs

* **Repo:** <https://github.com/Shyalya/tortoise-wow> — fork of `Penqle/tortoise-wow` targeting `1.18.1 build 7272` (Turtle-WoW). `pushed 2026-08-20T08:30:51Z`, 64 stars, ~1000 bots permanently online.
* **Bots:** Not either of the 1.12.1 cores above directly. Integrated from `r-o-sh/tortoise-wow:playerbots-integration-gh` which **vendors ike3's playerbots under `src/modules/PlayerBots/`**. `BUILD_PLAYERBOTS=ON`, gated by `AiPlayerbot.Enabled`.

From its `README.md`:
> *Integrated from r-o-sh's branch, which vendors ike3's playerbots under `src/modules/PlayerBots/`.*

`src/modules/PlayerBots/README.md` header identical to `cmangos/playerbots`:
> *Bot AI Core from ike3 for cmangos classic, tbc and wotlk*

Shyalya is therefore **same family as cmangos/playerbots (ike3 lineage), NOT mangoszero** — ported to Turtle 1.18.1 world. Cannot be cloned for 1.12.1.

Fixes on top: BG queue `recursive_mutex` restored, `NullSessionAnticheat` for bot sessions, navmesh tile keep, druid bear form, healer 125y range, stealth targeting, summoning, group loot, talent spec generation for Turtle reworked trees, etc.

---

## 4. Fork chain — Shyalya -> cmangos -> ike3/mangosbot-bots

`cmangos/playerbots` is a downstream fork reaching `ike3/mangosbot-bots` eventually, via:

```
ike3/mangosbot (integrated, fork of TrinityCore, 2023-11-20 stale)
  -> ike3/mangosbot-bots — Bot AI Core (a part of MangosBot project)
     created 2018-08-12, pushed 2024-01-10, 66 stars
     -> conan513/mangosbot-bots (fork of ike3, 2018-10-21, for cmangos-one 2.4.3)
        -> celguar/mangosbot-bots (fork of conan513, 2020-07-04, pushed 2026-08-02, 100 stars)
           -> cmangos/playerbots (desc: "forked from https://github.com/celguar/mangosbot-bots", pushed 2026-08-16)
              -> r-o-sh vendored 2026-05-10 -> Shyalya
```

GitHub `source` field confirms `celguar` source is `ike3/mangosbot-bots`, and `cmangos/playerbots` desc is `forked from celguar`.

---

## 5. Exact step Shyalya uses

Pinpointed commit `r-o-sh/tortoise-wow:playerbots-integration-gh` `0af2567` on `2026-05-10 22:38:49 +0200`:

> `checkpoint: cmangos/playerbots port grafted onto Penqle/tortoise-wow 1181dev (full-fat)`

> *End-to-end working bot port on a clean Penqle 1181dev tree... In: vendor (1009 files), shim, stubs, ~80 host hook files (3-way merged), BUILD_PLAYERBOTS CMake wiring...*

`Shyalya/tortoise-wow:playerbots-integration-gh` is a squashed fork of that `r-o-sh` checkpoint (git history depth = 1, head `1f9497e`). So **exactly the `cmangos/playerbots` step**, not direct `ike3`.

---

## 6. How old are Shyalya bots vs cmangos/playerbots HEAD?

* Vendored snapshot: `cmangos/playerbots:c33dfac` `Test: Fixed cleaning up mobs for vanilla` (2026-05-09, just before `0af2567` graft)
* HEAD at research time: `cmangos/playerbots:076045e` `Add misdirection for pull` (2026-08-16)
* **Delta: 87 commits, ~98 days behind**
* `HEAD vs Shyalya`: 581 files differ
* `snapshot vs Shyalya`: 519 files differ — divergence due to Turtle shims (`cmangos-compat-shim.h`, `cmangos-compat-stubs/`, `HostHooks.cpp`, `BotActionLog.cpp`, `BotLog.cpp`, `scripts/`, `LevelingDruidStrategy.cpp` etc) + Turtle-specific fixes.

`r-o-sh pushed 2026-07-23` / `Shyalya pushed 2026-08-20` are Turtle fixes on top of the May snapshot, not a re-sync.

---

## 7. What cmangos/playerbots fixed since Shyalya checked out (87 commits)

Summarized without git ids, grouped by area:

**Raid / Dungeon AI:**

* Proper Prince Malchezaar fight strategy
* Mechanar strategy / action / trigger files
* Add dungeon strategy as default (loads on enter)
* Auxiliary stuff, pull delay triggers

**Healer / Druid:**

* Skip healing party members who can't receive healing due to debuff (-100% healing aura)
* Proper druid casting form requirements per expansion (Tranquility in tree form WotLK, Regrowth in tree form TBC)
* Make balance druid prioritize Starfire in vanilla/tbc
* Add missing HP triggers in non-combat for all healers (priest/druid)
* Add Swiftmend (regrowth/rejuv check at critical health)

**Protection / Paladin / Warrior:**

* Fix prot warriors overwriting Commanding Shout with Battle Shout
* Fix Shield Slam not used over Devastate
* Ret Seal and Judge Crusader logic
* Protection paladin adjustments (judgement, seal triggers, taunt, righteous defense)
* Adjust TBC weightscales for prot paladin

**CC / Targeting (major clunky fix):**

* Rework CC for bots in PvE — only marked RTI targets, correct matching, no layering same target, pets hold until done
* Add Avoid Creature by ID command
* Add random CC if RTI CC is none
* Increase priority for warlock CC / mage Polymorph
* Bots stop attacking targets with large thorns/reflect shields
* Fix War Stomp useful distance in PvE (only if already within 8y)
* Change too close to cast trigger when flee range too low / stay strategy
* Rework immunity mask checking (all schools)

**Buffs:**

* Fix buffers not casting group buffs (Arcane Brilliance on party — now overwrites single Intellect)
* Fix Thorns on party trigger to search tanks first (was healers + ranged filter loop)
* Modify / fix Blessing of Protection targeting to prioritize casters and check HP
* Fix frost mage AoE priority (Blizzard vs Flamestrike when spell locked)
* Change ranged medium AoE triggers to ranged high AoE for ranged
* Adjust mage Polymorph priority

**Movement / Flying:**

* Fix flight movement back and forth (hovering/formations regression)
* Add formations to flying, wing flapping idle, point-based flying restore
* Fix bots moving in loops in WotLK
* Wander range type fix
* Fix spell interruption (was checking wrong things)
* Fix jump code for knockbacks (vertical angle, landing height)
* Fix GCD giving redundant reactDelay

**Professions / Enchants / Gems:**

* Better bot profession init (learn ranks correctly, max level handling)
* Add enchanting and tailoring as options
* Remove auto enchants on equip, keep on upgrade — add auto enchant upgrades option
* Enable socket gem and label gems in inventory
* Better way to check for item enchant mirroring enchanting item
* Add missing bot enchants tables

**Stability / System:**

* Stop Fel Domination from crashing server
* Fix aura iterator access violation
* Fix possible null dereference in ConfirmQuestAction
* Fix CanStoreItem / CanBankItem broken in Classic backport
* Change preprocessor for canStoreItem to `ifndef MANGOSBOT_ZERO`
* Correct immunity mask
* Fix Shield Slam vs Devastate choice
* Fix flightmaster mount bug (kept aura while dismounted)
* Bots only use flying mount if group leader uses one
* Remove problematic handlers for group quest relays (Ring of Blood)
* Fix check mount state action bug for vanilla
* Don't derank buffs for saving mana (persistent area auras, HoTs/DoTs)
* Role fix — players/bots not seen as tanks when in tanking aura
* AiFactory cleanup, log analysis crash fix, command fixes, travel fix (bots not requesting quests with focus target)
* Bots shouldn't dismount in combat when fleeing
* Add isPositiveAuraEffect check for Purge, check positive effect before dispel
* Enable Soul Link for warlock
* Add DK quest tests, missing bot enchants tables

---

## 8. Vanilla-specific vs generic

**Mix is ~50/50 but generic hits vanilla directly.**

Explicit vanilla/classic-labeled commits (3):

* Balance druid Starfire in vanilla/tbc
* Fix checkMountStateAction bug for vanilla
* Fix CanStoreItem broken in Classic backport

**Vanilla-relevant generic that explains Shyalya clunky (~40 commits):**
Healing HP triggers, group buff Thorns/Brilliance/BoP, CC rework (sap/sheep critical for vanilla dungeons), thorns/reflect melee check, War Stomp distance, too close to flee, spell interruption, bandage spam, derank buffs, wander, profession init, mount flee, chain heal/totem reach, Avoid Creature, CanStoreItem Classic, tank aura role — all apply to 1.18.1 world even when not labeled vanilla.

**TBC/WotLK/flying-only (~40 commits, not affecting Turtle):**
Misdirection, Prince Malchezaar, Steady Shot, Mechanar, Totemic Call TBC, Shield Slam vs Devastate, TBC weightscales, Fel Domination, flying formations/point-based flying, WotLK loops.

So even for `1.18.1`, Shyalya is missing ~40 generic vanilla-relevant fixes landed June-August in cmangos.

---

## 9. mangoszero/server — does it use cmangos/playerbots?

**No — something else.**

* `mangoszero/server` `src/modules/Bots` — 619 files — proprietary lightweight native bots, `OPTION(PLAYERBOTS OFF)`, gated by `AiPlayerbot.Enabled=1` in `aiplayerbot.conf.dist.in`
* `cmangos/playerbots` — 1069 files — heavy ike3-derived module

Different codebases, both `1.12.1 mongosd`.

* `mangoszero` = proprietary, built for Zero's core, fewer features but no ike3 debt. Recent correctness campaign (2026-08-20) fixed per-tick DB spam (hunter pet SELECT 10/sec, NearestGameObjects grid search 20/sec idle), teleport eviction loops (bot bounced zone every 60s), etc.
* `cmangos` = heavily-derived from old ike3 via `celguar` -> `cmangos`, feature-rich (BG/arena/dungeon) but carries downstream ike3 issues (trigger rebuild 105M/hour etc).

For `1.18.1`, `mangoszero` is architecturally closer (MaNGOS-Zero family) than `cmangos`.

---

## 10. Advice for Penqle 1.18.1 native module (clean canvas PR 396)

**Goal stated:** Original TWoW with workable bots for 2 players to do dungeons/raids/whole experience, contextual convos, self-contained module like `mod-playerbots` for 3.3.5a, no legacy debt, easy on/off, AI-assisted.

**Why PR 396 matters:** Deletes `src/game/PlayerBots/` + `WorldSession::GetBot` + `sPlayerBotMgr` + SQL, leaves `PlayerAI/PlayerControlledAI` intact — that's the exact seam a module needs. Don't re-vendor old `PlayerBots` again.

**Shyalya context:** Shyalya building ground-up bots privately, also porting 1.18.1 to 3.3.5a, not sharing yet. You don't want to cross channels but Discord says “I definitely just want to play original TWoW with workable bots that aren't shit and you can actually have contextual convos with/play the game with so my partner and I can do dungeons/raids/whole experience.” No one else raising hands — good time to provide open alternative.

### Recommendation: mangoszero skeleton + greenfield AI

**Don't start from `cmangos/playerbots` (1069 files, ~80 host hook files, strategy rebuild perf bug) and don't start from pure zero (years to reimplement travel/dungeon).**

Base to start from: **`mangoszero/server/src/modules/Bots` (619 files)** as the *module plumbing*:

* Keep: `PlayerbotAI`, `PlayerbotMgr`, `RandomPlayerbotMgr/Factory`, `AhBot`, `strategy/generic` lifecycle, random bot create/teleport, loot, group follow — the stuff `mod-playerbots` keeps as `src/Ai/Base`.
* Rewrite from scratch with AI help: all `Strategy/Trigger/Action` per class. `ike3` trigger lists are the source of clunky (Turtle talent trees already break ike3 premade specs — Shyalya had to backfill bear form etc). Generate Turtle 1.18.1 talent weights/spell priorities from DBC, not wowhead vanilla links.

This gives the `mod-playerbots` pattern for 1.18.1:

```
Penqle/tortoise-wow (BUILD_PLAYERBOTS=OFF core builds clean)
  -> src/modules/Bots as submodule (penqle-playerbots repo)
  -> 1.18.1 build - no core edits except BotService hooks
```

### How to stay self-contained like mod-playerbots

`mod-playerbots` is self-contained *because* it lives in `modules/` and requires a forked `azerothcore-wotlk Playerbot` branch that only exposes hooks. Do same for MaNGOS:

1. Define `BotService.h` interface in module — core only calls `OnPlayerUpdate()`, `OnChat()`, `OnMovement()` via weak symbol, not patched `Player.cpp`/`Unit.cpp`.
2. Keep Turtle custom content isolated in a `shim` (like `r-o-sh`'s `cmangos-compat-shim.h`). If you also want 3.3.5a later (Shyalya's plan), make AI layer compile against both cores via shim — share strategies.
3. LLM contextual convos: separate `PlayerbotLLMInterface` as async service outside tick — `Shyalya` stripped `Remove LLM network client` for this reason — don't block map threads.

### Steps

1. Create `penqle-playerbots` repo — submodule, `PLAYERBOTS OFF` default — MVP: spawn / follow / loot / combat utility AI for 2 players
2. AI-generate Turtle 1.18.1 talent specs and dungeon stubs (not vanilla)
3. Then add LFG fill, BG, travel nodes — all inside module

Result: workable bots for small groups without Shyalya's 1000-bot scale complexity, and anyone can still build Penqle without bots.

---

## Appendix — Key links

* <https://github.com/cmangos/mangos-classic>
* <https://github.com/cmangos/playerbots> — Bot AI Core from ike3
* <https://github.com/mangoszero/server> — MangosZero vanilla + built-in Bots
* <https://github.com/ike3/mangosbot> / <https://github.com/ike3/mangosbot-bots> — original
* <https://github.com/celguar/mangosbot-bots> — fork chain step
* <https://github.com/Shyalya/tortoise-wow> — Turtle 1.18.1 + vendored bots (~1000 online)
* <https://github.com/r-o-sh/tortoise-wow/tree/playerbots-integration-gh> — vendor source (checkpoint `cmangos/playerbots` graft 2026-05-10)
* <https://github.com/Penqle/tortoise-wow/pull/396> — clean canvas
* <https://github.com/mod-playerbots/mod-playerbots> — AzerothCore 3.3.5a module model
* <https://github.com/mod-playerbots/azerothcore-wotlk/tree/Playerbot> — forked core required even for “module”

*Generated from research 2026-08-20 via conversation discovery.*
