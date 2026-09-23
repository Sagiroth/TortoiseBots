# Changelog

## 2026-09-23

### Combat & AI
- Fixed player-owned bots never looting: an uncommented empty `AiPlayerbot.NonCombatStrategies` was overriding the C++ default `+return,+delayed roll,+loot`, so companion alts spawned with zero non-combat strategies. Restoring the default means owned bots now loot corpses, return to master, and roll on loot as intended. [#259](https://github.com/Sagiroth/TortoiseBots/pull/259)

### Roles & Targeting
- Added runtime human role overrides and a group-aware stance/form/equipment baseline; DPS bots now assist the active tank target after explicit orders and raid marks. [#264](https://github.com/Sagiroth/TortoiseBots/issues/264)

---

### Enchants & Progression

- `ai_playerbot_enchants` now ships 259 rows covering all 27 class specs across all 11 enchantable slots — Head, Shoulders, Chest, Legs, Boots, Bracers, Gloves, Cloak, Main Hand, Off Hand/Shield, and Ranged. No more bots walking around half-enchanted because their spec fell through the cracks. [#260](https://github.com/Sagiroth/TortoiseBots/pull/260)
- Non-raid baseline enforced: the Molten Core-only healing power enchant (Spell 22750) is gone, replaced with Mighty Intellect (+22 Int, Thorium Brotherhood Revered) so casters aren't gated behind raid progression just to be geared. [#260](https://github.com/Sagiroth/TortoiseBots/pull/260)
- AQ40 glove power enchants (25073+) swapped out for Minor Haste (+1% Spell/Attack haste, world drop) — keeps the throughput feel without requiring a 40-man raid attunement farm. [#260](https://github.com/Sagiroth/TortoiseBots/pull/260)
- Head and Legs use 5-man Libram arcanums (BRD Voracity, Dire Maul Focus) instead of raid-only alternatives, and Shoulders lean on Argent Dawn rep rather than raid drops — bots stay progression-appropriate on servers without Molten Core clears. [#260](https://github.com/Sagiroth/TortoiseBots/pull/260)

### Core Sync & Fixes
- Real player authority now wins: `PlayerbotAI::GetGroupMaster()` prefers a live human master (or any human in the party) over a headless bot wearing the leader crown. [#261](https://github.com/Sagiroth/TortoiseBots/pull/261)
- Bots now automatically yield party leadership to a live player via `ChangeLeader` — enforced on rebind, AI tick, invite, and uninvite, so you stop fighting a bot for the crown. [#261](https://github.com/Sagiroth/TortoiseBots/pull/261)
- `HandleAction` resolves its scope dynamically, so bot behavior tracks the current human leader instead of a stale headless one. [#261](https://github.com/Sagiroth/TortoiseBots/pull/261)

### Commands & AI
- Fixed a fatal `.bot action` double-free crash: the missing `return true;` in `HandleAction` let `-O3` builds emit duplicated destructor calls on local `std::string`/`std::vector` objects, hard-crashing the server (`free(): double free detected in tcache 2` / SIGSEGV) whenever an action completed. [#262](https://github.com/Sagiroth/TortoiseBots/pull/262)

### Core Sync & Fixes
- Bots now rebind to their owner's player on login via `BotManager::RebindOwnedBots`, restoring master/follow relationships across relogs instead of losing them. [#262](https://github.com/Sagiroth/TortoiseBots/pull/262)
- Hardened group iteration to avoid unsafe traversal while processing party members. [#262](https://github.com/Sagiroth/TortoiseBots/pull/262)

## 2026-09-22

### Combat & AI
- Jump landings no longer blindly snap to the map's floor height — bots stop settling through structures they actually landed on after a hop. [#241](https://github.com/Sagiroth/TortoiseBots/pull/241)
- Both `JumpAction::DoJump` and the fall-after-jump branch in `PlayerbotAI::UpdateAI` now respect the collision test result instead of trusting `UpdateAllowedPositionZ` unconditionally, cutting down on bots sinking into geometry or dropping through terrain after jumps and knockbacks. [#241](https://github.com/Sagiroth/TortoiseBots/pull/241)

---

### Combat & AI

- The navmesh "landed" check now only accepts floors at or under the bot's feet, so jump/fall simulation stops treating overhead walkable polys as ground. [#245](https://github.com/Sagiroth/TortoiseBots/pull/245)
- Fixes phantom landings and mid-air height snaps near canal bridges, where the map lookup missed the street below and the deck above was inside the old ±4 yd probe box. [#245](https://github.com/Sagiroth/TortoiseBots/pull/245)

### Combat & AI

- Paladin bots now only pop Consecration when it will actually hit a pack — three attackers within 8 yd, or two when mana is 70%+ — so no more dumping it on a single enemy. [#244](https://github.com/Sagiroth/TortoiseBots/pull/244)
- Judgement is gated behind medium mana; below the threshold the seal stays up and the paladin keeps auto-attacking instead of burning the bar. [#244](https://github.com/Sagiroth/TortoiseBots/pull/244)

### Gear & Progression

- Fresh-bot seeding no longer funnels everyone into the same raid epics: instead of always taking index 0 of a best-first sort, it collects all passing candidates and rolls uniformly from a top-N window (5 weapons, 3 jewelry, 8 armor). Sixty bots now look like sixty different characters. [#242](https://github.com/Sagiroth/TortoiseBots/pull/242)
- Added a provenance gate on the seed path: raid-sourced drops (world-boss rank or raid-map spawn) and raid-quest rewards (Type 62, `SuggestedPlayers > 5`, or raid-map `ZoneOrSort`) are rejected outright. Non-raid quest rewards still pass as long as quest level ≤ bot level. Fail-open, so missing data can't starve seeding. [#242](https://github.com/Sagiroth/TortoiseBots/pull/242)
- Dupe guard on paired finger/trinket slots — no more dual-wielding the exact same ring or trinket. [#242](https://github.com/Sagiroth/TortoiseBots/pull/242)
- Required-level gate with wearability descent, so sub-60 bots get weapons and armor they can actually equip instead of dead weight in their bags. [#242](https://github.com/Sagiroth/TortoiseBots/pull/242)

### Combat & AI
- Paladin Protection bots now weight stamina at `15` (up from `6`), matching the warrior protection treatment from [#242](https://github.com/Sagiroth/TortoiseBots/pull/242) — Protection paladins should stop melting in sustained tanking fights. [#246](https://github.com/Sagiroth/TortoiseBots/pull/246)
- Defense (`8`) and block rating (`14`) weights were already healthy and are untouched, so this is a pure survivability buff with no threat/avoidance reshuffling. [#246](https://github.com/Sagiroth/TortoiseBots/pull/246)
- Retribution's `str 4` vs Protection's `str 1` inversion remains as-is — deliberate balance call, not a bug. [#246](https://github.com/Sagiroth/TortoiseBots/pull/246)

### Armory & Itemization

- Armory item tooltips now render full numeric stat values and spell effects — equip bonuses, chance-on-hit, and on-use effects for defense, block, parry, dodge, crit, hit, attack power, spell power, and mana regen are all formatted properly [#247](https://github.com/Sagiroth/TortoiseBots/pull/247).
- Proc descriptions are shown in-tooltip, so you can finally see what that "chance on hit" actually does instead of guessing [#247](https://github.com/Sagiroth/TortoiseBots/pull/247).
- Integrated `ItemRandomProperties.dbc` so random-suffix and random-property rolls display with their correct names and stats [#247](https://github.com/Sagiroth/TortoiseBots/pull/247).
- Spec column added and layout fixed on the armory dashboard — no more broken tables or missing class/spec data [#247](https://github.com/Sagiroth/TortoiseBots/pull/247).

### Observability & Engine

- New template placeholder engine parses `$s`, `$o`, `$d`, `$h`, `$l`, `$g`, division formulas, and cross-spell references, pulling from `spell_template` and `SpellDuration.dbc` for accurate values [#247](https://github.com/Sagiroth/TortoiseBots/pull/247).
- Enchant data now resolves and displays correctly in tooltips, closing the last gap in armory item observability [#247](https://github.com/Sagiroth/TortoiseBots/pull/247).

### Observability & Engine
- Stats panel layout is now locked down: strict `minmax(0, 1fr)` grid columns, width/overflow containment on text, and a fixed 560px paperdoll section with a 28px gap — equipped gear no longer bleeds into character stats on narrow windows or long item names. [#248](https://github.com/Sagiroth/TortoiseBots/pull/248)

### Gear & Consumables

- **Hunters stop being stuck with the starter quiver forever.** New `RandomItemMgr::GetQuiver(level)` resolves the best **vendor-sold** quiver/pouch per level bucket by joining `npc_vendor` — raid/drop containers are impossible by construction — and ordering `required_level DESC`, so bots progress Light Quiver (1) → Medium (10) → Heavy (30) at cap. [#249](https://github.com/Sagiroth/TortoiseBots/pull/249)

- **`InitAmmo` now actually equips the container**, dropping a strictly worse one first so the core's one-quiver rule isn't violated. No more silent no-ops when a bot has ammo but nowhere to put it. [#249](https://github.com/Sagiroth/TortoiseBots/pull/249)

- **Vendor ammo wired into the fresh-seed field kit**, so newly created bots can shoot and reload without a manual gear pass from an operator. [#249](https://github.com/Sagiroth/TortoiseBots/pull/249)

- **Food added to the starter loadout** — seeded bots can regenerate between pulls instead of slowly bleeding out on long grinds. [#249](https://github.com/Sagiroth/TortoiseBots/pull/249)

- **Per-spec weapon and glove enchants** are now applied at seed time, giving fresh bots spec-appropriate throughput from minute one rather than a generic or missing enchant. [#249](https://github.com/Sagiroth/TortoiseBots/pull/249)

- **Follow-up cleanup to [#246](https://github.com/Sagiroth/TortoiseBots/pull/246):** this round closes out the owner's live-server observations on seeded gear, tightening the gap between "bot exists" and "bot is actually field-ready." [#249](https://github.com/Sagiroth/TortoiseBots/pull/249)

### Observability & Engine
- Fixed double-counting of Healing Power on generic spell damage gear — healing power is no longer added twice from `SPELL_AURA_MOD_DAMAGE_DONE`, with a safeguard clamping Healing Power to at least Spell Damage. Stats panels now reflect real caster throughput instead of inflated numbers. [#250](https://github.com/Sagiroth/TortoiseBots/pull/250)
- Added a per-magic-school damage breakdown, replacing the old compact string list with structured data so you can see exactly where fire, frost, shadow, and friends are landing. [#250](https://github.com/Sagiroth/TortoiseBots/pull/250)

### Observability & Engine
- Spell Power now shows a hover tooltip breaking down all six magic schools (Holy, Fire, Nature, Frost, Shadow, Arcane) with per-school icons, totals, and bonus tags — no more stretched stat card just to see where your damage is coming from. [#252](https://github.com/Sagiroth/TortoiseBots/pull/252)
- Healing now gets its own popover showing Base Spell Power, Pure Healing bonus, and Total Healing Power, so healers can diagnose their throughput at a glance. [#252](https://github.com/Sagiroth/TortoiseBots/pull/252)
- Tooltips follow the cursor smoothly via a new `mousemove` handler in `bindSpellTooltips`. [#252](https://github.com/Sagiroth/TortoiseBots/pull/252)

### Gear & Inventory
- Fixed bots hoarding leftover starter ammo: `InitAmmo` now purges every projectile stack that doesn't match the chosen tier after the tier is known, scoped to the equipped ranged weapon type. Previously fresh seeds with `ammoId = 0` kept a Rough Arrow stack sitting next to the correct Jagged Arrow stock — 52 stray stacks in the reseed battery. [#251](https://github.com/Sagiroth/TortoiseBots/pull/251)
- Code-only change in `ai/playerbot/PlayerbotFactory.cpp` — keeps the right stock, drops every tier-mismatched projectile. [#251](https://github.com/Sagiroth/TortoiseBots/pull/251)

### Gear & Equipment Fixes
- Duplicate `(guid,bag,slot)` equipment rows are now pruned at the end of `MakeComplete`, killing the ghost starter-kit rows that the factory item map couldn't see — no more lvl60s parading around in lvl50 starter junk (3243 stale rows cleaned). [#253](https://github.com/Sagiroth/TortoiseBots/pull/253)
- Enchant map corrections stop gear from rolling the wrong enchantment for its slot/item, so generated loadouts match what the item actually is. [#253](https://github.com/Sagiroth/TortoiseBots/pull/253)

### Companions & Hiring
- Fixed a recursive `Group::Disband` loop that could fire when dismissing a hired companion from a party of 2 or fewer members — triggered by master disconnect grace expiry, a member leaving, or a bot being kicked. Dismissals now unwind cleanly without re-entering group teardown mid-disband. [#254](https://github.com/Sagiroth/TortoiseBots/pull/254)

### Commands
- `.bot kick` now aliases to `HandleUninvite` — no more “Unknown command” when dismissing a bot. [#255](https://github.com/Sagiroth/TortoiseBots/pull/255)
- `.bot interrupt <bot>` finally dispatches to the mature `ResolveInterruptExecutor` probe, matching the advertised usage. [#255](https://github.com/Sagiroth/TortoiseBots/pull/255)
- `.bot strategy <change> [bot]` is now a real verb: applies via `ChangeStrategy(ALL)` over dynamic scope and persists with `DbStore`. [#255](https://github.com/Sagiroth/TortoiseBots/pull/255)
- Tidied the `.bot` usage string and dropped a dead corpse-run first-word disjunct. [#255](https://github.com/Sagiroth/TortoiseBots/pull/255)

### Configuration & Wiring
- Wired up previously inert config flags (e.g., non-GM free summon) from the wiring audit, so documented settings now actually affect runtime behavior. [#255](https://github.com/Sagiroth/TortoiseBots/pull/255)

### Validation & Stability
- All wiring audit validators pass: `verify_all.sh`, OKF, action/trigger live-missing=0, and host contract. [#255](https://github.com/Sagiroth/TortoiseBots/pull/255)

### Docs & Onboarding
- Onboarding pages (getting-started, dungeon-tactics, README quick finder) now spell out that `.bot summon <Name>` needs `NonGmFreeSummon=1` or GM status — no more mystery "summon failed" for regular players. [#256](https://github.com/Sagiroth/TortoiseBots/pull/256)
- BoostFollow re-described as mount-to-catch-up in the tuning guide and bot-mechanics docs, replacing the old "temporary speed multiplier" wording so tuning advice matches what bots actually do. [#256](https://github.com/Sagiroth/TortoiseBots/pull/256)

### Combat & AI
- Fixed melee AoE trigger creators instantiating the Ranged trigger class, so Blade Flurry, Cleave, Whirlwind, Consecration, and Oil of Immolation now use melee density (5y) instead of ranged density (40y) — they fire when enemies are actually in your face, not across the room. Wiring verifier live-missing=0 and verify_all.sh clean. [#257](https://github.com/Sagiroth/TortoiseBots/pull/257)

### Bot Hiring & Accounts
- Fixed `NoCandidate` hire failures on saturated pools: the hire check now validates the per-realm character cap (10) first, falling back to per-account limits, so genuinely full accounts are skipped instead of being picked and then rejected by `CreateCharacter`. [#258](https://github.com/Sagiroth/TortoiseBots/pull/258)
- Unhandled `SERVER_LIMIT` refusals no longer kill the whole hire — saturation now correctly falls through to fresh-account creation. [#258](https://github.com/Sagiroth/TortoiseBots/pull/258)
- Fresh-account creation retries the *same* character name (10 × 200ms) until the async LoginDatabase `GetId` drains, instead of minting a new orphan on every attempt. [#258](https://github.com/Sagiroth/TortoiseBots/pull/258)
- Hire failures are never destructive: no bot is deleted as part of the failure path. [#258](https://github.com/Sagiroth/TortoiseBots/pull/258)
- Added failure diagnostics so operators can see *why* a hire failed instead of getting a silent dead end. [#258](https://github.com/Sagiroth/TortoiseBots/pull/258)

## 2026-09-21

### Movement & Navigation
- Fixed bots clipping through the floor on crypt staircases (e.g. The Sepulcher) by seeding the follow-spot ground query with correct Z instead of inheriting the target's height — no more falling into the void on descents. [#226](https://github.com/Sagiroth/TortoiseBots/pull/226)
- Bots now correctly board and exit elevators in Undercity, Thunder Bluff, and Freewind Post, and no longer hard-freeze their movement until relog after a failed lift ride. [#226](https://github.com/Sagiroth/TortoiseBots/pull/226)
- Both fixes live entirely inside the TortoiseBots module — zero core changes, safe drop-in for existing servers. [#226](https://github.com/Sagiroth/TortoiseBots/pull/226)

---

### Commands & Party Management
- Fixed the SIGSEGV when issuing a party-wide follow from TortoiseBotsManager (`TBM\taction follow`): party scope is now resolved guid-first and `PlayerbotAIStorage::GetAI` is called through a safe lookup, so stale or invalid player pointers can no longer crash the server. [#234](https://github.com/Sagiroth/TortoiseBots/pull/234)
- Hardened the old fallback path that would have kept crashing even after removing the guid fallback; module-only fix with zero core changes, so it drops cleanly into existing installs. [#234](https://github.com/Sagiroth/TortoiseBots/pull/234)

### Observability & Engine
- Replaced placeholder zone map art in the telemetry dashboard with 15 authentic Warcraft-style maps from Maps of Mystery (WebP, 1002x668) — includes 6 placeholder replacements, 3 new maps (Balor Island, Grim Reaches, Northwind), and 6 high-res upgrades for clearer zone visualization. [#236](https://github.com/Sagiroth/TortoiseBots/pull/236)

### Combat & AI
- Bots now give up on targets they can see but can't reach — a mob behind a fence, a cliff, or across a stream no longer eats minutes of chase time. No progress for 15 s counts as "stuck," with or without line of sight; rooted/stunned bots are exempt, and an "attacking" mob that never closes in gets dropped too. [#231](https://github.com/Sagiroth/TortoiseBots/pull/231)
- A target kind that already killed the bot twice gets blacklisted, so bots stop feeding themselves to the same spawn spot. [#231](https://github.com/Sagiroth/TortoiseBots/pull/231)

### Fishing & Professions
- No more pole-swap tick loops: a fishing pole is never treated as an "equip upgrade," so bots don't unequip/reequip a Strong Fishing Pole every frame. "Done fishing" also stays false for 30 s after the last cast. [#230](https://github.com/Sagiroth/TortoiseBots/pull/230)
- Bots stop casting next to hostile creatures instead of fishing while something is chewing on them. [#230](https://github.com/Sagiroth/TortoiseBots/pull/230)
- Getting attacked mid-fishing now drops the channel immediately and pulls the real weapon back out — no more 20 s of dead fish-time followed by a fight with a fishing pole in hand. [#229](https://github.com/Sagiroth/TortoiseBots/pull/229)

### Random Bots & Population
- New opt-in `AiPlayerbot.LevelLadder` lets you fill the online target by level band instead of drawing from the whole pool. Bands (1-5, 6-10, ... 56-59) each get an equal share, level 60 gets a hard-capped `LevelLadderMaxLevelShare` percent, the band with the biggest shortfall fills first, and within a band the highest level wins the login. Ties go to the faction with fewer bots online. [#232](https://github.com/Sagiroth/TortoiseBots/pull/232)

### Core Sync & Fixes
- Fixed a world-server crash when a bot between two maps played a text emote (or sound). `PlayEmote`/`PlaySound` now refuse when the bot isn't in the world, is mid-teleport, or has no map — no more `GetMap()` assertion taking the server down. [#235](https://github.com/Sagiroth/TortoiseBots/pull/235)
- Fixed an instance corpse-run timeout crash: the bot is now resurrected *before* the teleport to the dungeon entrance, so it never has a map-less resurrect hitting the core assertion. [#227](https://github.com/Sagiroth/TortoiseBots/pull/227)

### Observability & Engine
- A multiplier veto (factor 0) now holds for the entire tick. Previously the same action object could sneak back in through another ability's prerequisite basket with a fresh relevance, undoing the veto the engine already issued. [#228](https://github.com/Sagiroth/TortoiseBots/pull/228)

### Class Abilities & Utility

- **Druid Innervate** now retargets the lowest-mana party healer under `AiPlayerbot.LowMana`, respects manual `.bot boost` assignments, and falls back to self — no more wasted casts on full-mana targets. [#238](https://github.com/Sagiroth/TortoiseBots/pull/238)
- Fixed Innervate's mana-percent check that was always reporting zero due to integer division; both the action and trigger now use `ai->GetManaPercent`, so the low-mana gate actually fires when it should. [#238](https://github.com/Sagiroth/TortoiseBots/pull/238)
- **Druid Barkskin** typo (`barskin`) corrected in both the action and its creator, registered properly, and wired into the Balance/Restoration paths so the defensive actually gets used. [#238](https://github.com/Sagiroth/TortoiseBots/pull/238)

### Core Sync & Fixes

- Utility mechanics harvested surgically into the live list-engine — no engine replacement, zero core changes, so it drops in cleanly on existing installs without breaking custom configs. [#238](https://github.com/Sagiroth/TortoiseBots/pull/238)

### Raid & Dungeon Engine
- The raid/dungeon transition engine is back online: entering a raid now triggers zone-in tactics for MC, Onyxia, BWL, and Naxx, and leaving correctly tears them down instead of leaving bots stuck in raid behavior. [#239](https://github.com/Sagiroth/TortoiseBots/pull/239)
- `dungeon` is now part of the default combat and non-combat strategies in AiFactory, so instance transitions keep ticking rather than stalling out. [#239](https://github.com/Sagiroth/TortoiseBots/pull/239)
- Molten Core is unquarantined from `DEAD_FILES` — MC bots are live again. [#239](https://github.com/Sagiroth/TortoiseBots/pull/239)
- Entirely module-side: zero core changes, so no rebuild of the server core or risk to existing installs for operators. [#239](https://github.com/Sagiroth/TortoiseBots/pull/239)

### Combat & AI
- New universal raid survival heuristics run in the reaction engine on any raid map, so bots stop eating avoidable raid-wide damage. [#239](https://github.com/Sagiroth/TortoiseBots/pull/239)
- Bomb runout: bots carrying Living Bomb (20475), Burning Adrenaline (23620/18173/23478), or Mutating Injection (28169) now clear ~30yd from the raid anchor instead of nuking the stack. [#239](https://github.com/Sagiroth/TortoiseBots/pull/239)
- Four Horsemen mark rotation: bots at 3+ stacks rotate off the mark instead of riding it into the floor. [#239](https://github.com/Sagiroth/TortoiseBots/pull/239)

### Raid Tactics & Dungeons
- Added opt-in custom Turtle WoW raid tactics for Emerald Sanctum, Lower Karazhan, and Karazhan Crypt behind the new `AiPlayerbot.EnableCustomRaidTactics` flag, so operators can enable scripted bot handling without core changes. [#240](https://github.com/Sagiroth/TortoiseBots/pull/240)
- Wired enter/leave triggers for maps 807, 532, and 800 into DungeonStrategy and registered the matching strategy, trigger, and action creators in all three contexts, letting bots transition and fight correctly in each raid. [#240](https://github.com/Sagiroth/TortoiseBots/pull/240)
- Delivered the statically implementable Part A of #203 for ES/LKH/Crypt, covering Emerald Sanctum IDs and raid-specific behavior now that can be handled module-side. [#240](https://github.com/Sagiroth/TortoiseBots/pull/240)

### Build & Module Integration
- Updated the CMake KARAZHAN denylist to bypass native `LowerKarazhan*`, `KarazhanCrypt*`, and `EmeraldSanctum*` files while still failing on donor TBC/WotLK Karazhan assets, preventing false build errors and donor-map conflicts. [#240](https://github.com/Sagiroth/TortoiseBots/pull/240)

## 2026-09-20

### Dungeon Finder & Roles
- Managed bots now report their roles through native core hooks (`PLAYERHOOK_IS_MANAGED_BOT` / `PLAYERHOOK_GET_BOT_ROLES`), so LFT role checks and dungeon queues work without any core patches. [#224](https://github.com/Sagiroth/TortoiseBots/pull/224)
- Rotations, gear selection, and ammo handling cleaned up for bots queued via LFT — fixes the role/rotation/gear/ammo chain reported in #218–#221. [#224](https://github.com/Sagiroth/TortoiseBots/pull/224)
- Zero core modifications required: leverages existing upstream seams in `LFTQeueue.cpp`, keeping the module drop-in compatible. [#224](https://github.com/Sagiroth/TortoiseBots/pull/224)

### Loot & Alt Bots
- Alt characters summoned via `.bot add <altname>` can now loot corpses again — five separate barriers were blocking player-owned bots from looting when grouped with their master. [#223](https://github.com/Sagiroth/TortoiseBots/pull/223)
- Fixed a stale DB snapshot wipe where replaying a pre-#208 strategy preset stripped all non-combat strategies including `+loot`; baseline strategies now re-add `+loot` and `+delayed roll` on load. [#223](https://github.com/Sagiroth/TortoiseBots/pull/223)
- Corrected the follow-leash priority inversion that kept alt bots locked to their master instead of looting nearby corpses. [#223](https://github.com/Sagiroth/TortoiseBots/pull/223)

---

## 2026-09-18

### Companions & Hiring
- On-demand companion hiring is live: use the `<Mercenary Hire>` inn-recruiter gossip wizard to pick class → race → gender → spec/role → confirm, no addon or class swap required. [#205](https://github.com/Sagiroth/TortoiseBots/pull/205)
- Prefer typing? `.bot hire <class> [role] [race] [gender]` is the fast path to the same provisioning pipeline. [#205](https://github.com/Sagiroth/TortoiseBots/pull/205)
- Both entry points share one provision service, so hired bots come out consistent regardless of how you summon them. [#205](https://github.com/Sagiroth/TortoiseBots/pull/205)

### Provisioning & Loadout
- Hires reuse RNDBOT pool candidates when available and fall back to fresh character creation, then sync level via `GiveLevel` so they match your progress. [#205](https://github.com/Sagiroth/TortoiseBots/pull/205)
- Role-matching premade talents are applied with the role forced, plus spells, skills, and incremental gear — hired bots arrive combat-ready instead of naked and confused. [#205](https://github.com/Sagiroth/TortoiseBots/pull/205)
- Tank hires get a dedicated strategy kit so they actually hold threat instead of sightseeing. [#205](https://github.com/Sagiroth/TortoiseBots/pull/205)
- Invites use the native invite + mature accept flow, keeping party state clean and avoiding half-broken group joins. [#205](https://github.com/Sagiroth/TortoiseBots/pull/205)

### Economy & Costs
- Hiring is routed through a dedicated cost service, so companions are a gold sink rather than a free army. [#205](https://github.com/Sagiroth/TortoiseBots/pull/205)

---

### Combat & AI
- Divine Favor is back in the paladin boost kit (guarded by learned-spell checks), Improved Scorch now fires before the fire-vulnerability fallback, instant Slam procs land in both Arms and Fury, and Felhunter Spell Lock finally shuts down enemy healers instead of idling [#214](https://github.com/Sagiroth/TortoiseBots/pull/214)
- Shaman Earth Shock interrupt is registered at proper interrupt priority, warlocks stop wasting long DoTs on targets below 20% health, and paladin Holy Shield / Consecration now respect learned-spell gates [#213](https://github.com/Sagiroth/TortoiseBots/pull/213)

### Classes & Resources
- Warlock soul shards are capped at 5 out of combat with a matching KEEP threshold, and hunter shots are gated behind a shared ammo check so empty-quiver bots swap to melee instead of standing around dry-firing [#210](https://github.com/Sagiroth/TortoiseBots/pull/210)
- Feral druids can finally DPS: role bitmask widened to TANK|DPS, unassigned Feral defaults to Cat, forced roles from owned companions take precedence, and spec index flows through gossip and `.bot hire` (Cat kit plus stealth included) [#207](https://github.com/Sagiroth/TortoiseBots/pull/207)

### Trade & Inventory
- Conjured food, water, and level-matched healthstones now auto-populate the trade window on trade start — mages feed mana users, warlocks hand over stones, and bots/already-stoned traders are skipped. Toggle via `AiPlayerbot.AutoShareConjuredOnTrade` (default on) [#209](https://github.com/Sagiroth/TortoiseBots/pull/209)

### Auth & Access Control
- Login hashes are now uppercase SHA1 with case-insensitive SQL matching, and GM rank resolution uses the highest `account_access` gmlevel instead of getting masked by zero-rank rows. Minimum GM level drops to 2 via the configurable `MinGMLevel` flag/env [#215](https://github.com/Sagiroth/TortoiseBots/pull/215)

### Quality of Life & Automation
- Owned bots now default to auto-loot through the NonCombatStrategies fallback (`+loot`), so your roster picks up drops without babysitting. [#208](https://github.com/Sagiroth/TortoiseBots/pull/208)
- New `.bot loot [on|off]` toggle persists via DbStore, so loot preference survives relogs and restarts. [#208](https://github.com/Sagiroth/TortoiseBots/pull/208)

### Commands & Scripting
- `.bot repair` and `.bot sell` now fan out as mature actions over dynamic scope — bulk-service an entire bot roster in one command instead of one bot at a time. [#208](https://github.com/Sagiroth/TortoiseBots/pull/208)
- Full `.bot action` parity for loot, repair, and sell with TBM ACK/ERR protocol, giving scripters a consistent, machine-readable interface. [#208](https://github.com/Sagiroth/TortoiseBots/pull/208)

### Docs & Verification
- Docs synced: config.tsv `NonCombatStrategies` row, commands.tsv loot/repair/sell rows, plus player-controls direct and action tables. [#208](https://github.com/Sagiroth/TortoiseBots/pull/208)
- `verify_okf.py` PASSED (25 nodes) and `diff --check` clean; build deferred per instruction across all 9 issues. [#208](https://github.com/Sagiroth/TortoiseBots/pull/208)

### Combat & AI
- Totems now only deploy when stationary or locked in close melee, killing the pre-combat rest-totem spam that ruined pulls. [#212](https://github.com/Sagiroth/TortoiseBots/pull/212)
- Dungeon and raid bots reuse the tight raid follow leash, so they stop drifting into extra packs mid-run. [#212](https://github.com/Sagiroth/TortoiseBots/pull/212)

### Commands & Controls
- `.bot rest` now fans mature food and drink actions over dynamic scope, with `.bot drink` / `.bot eat` aliases and full `.bot action` parity. [#212](https://github.com/Sagiroth/TortoiseBots/pull/212)
- Command docs updated (`commands.tsv` and player-controls action rows) to match the new aliases. [#212](https://github.com/Sagiroth/TortoiseBots/pull/212)

### Companion Utility & Recovery
- `.bot release` and `.bot corpse run` now work on dead scoped bots via mature release/corpse-run actions, enabling wipe recovery without manual bot wrangling. [#211](https://github.com/Sagiroth/TortoiseBots/pull/211)
- `.bot learn` drives the mature trainer action, and `.bot trade` opens trade with the targeted alive bot. [#211](https://github.com/Sagiroth/TortoiseBots/pull/211)
- `.bot action` now has parity for release, corpse run, learn, and trade through the TBM protocol. [#211](https://github.com/Sagiroth/TortoiseBots/pull/211)

### Docs & Validation
- Updated `commands.tsv` and player-controls lifecycle/action rows for the new command surface. [#211](https://github.com/Sagiroth/TortoiseBots/pull/211)
- `verify_okf.py` passed with 25 nodes and `diff --check` is clean; build was intentionally deferred per instruction across all 9 issues. [#211](https://github.com/Sagiroth/TortoiseBots/pull/211)

## 2026-09-17

### Observability & Engine

- New Discord alert on `issues.opened`: short embed with number, title, author, labels and link — posted as **TortoiseBots Issue**. No body dump, so the channel stays readable. [#190](https://github.com/Sagiroth/TortoiseBots/pull/190)
- Issue alerts reuse `DISCORD_WEBHOOK_URL`; set optional `DISCORD_ISSUE_THREAD_ID` to route them into a thread instead of the main channel. [#190](https://github.com/Sagiroth/TortoiseBots/pull/190)
- Missing webhook secret now skips cleanly with a warning instead of failing the workflow. [#190](https://github.com/Sagiroth/TortoiseBots/pull/190)
- Changelog poster renamed to **TortoiseBots Changelog** (default + workflow) so both bots read consistently in Discord. [#190](https://github.com/Sagiroth/TortoiseBots/pull/190)

---

### Bot Progression & Completeness
- Random-pool bots now self-complete in a fixed order — talents → spells → gear — at every level, with no gold charged and no core edits required [#191](https://github.com/Sagiroth/TortoiseBots/pull/191)
- New `PlayerbotFactory::MakeComplete()` helper chains talents → knob-gated spells → skills → incremental gear, so completion is a single call instead of scattered logic [#191](https://github.com/Sagiroth/TortoiseBots/pull/191)
- Free spell learning is scoped strictly to the random pool via `IsFreeLearnBot`; the paid trainer-with-gold path is completely untouched [#191](https://github.com/Sagiroth/TortoiseBots/pull/191)

### Performance & Caching
- Per-class trainer data is now built once per server run instead of a full creature scan for every bot — a big cost win on busy servers with large bot pools [#191](https://github.com/Sagiroth/TortoiseBots/pull/191)

### Gear & Roles
- Spec-correct gear generation falls back to a class-generic weight scale when spent talents are unknown, so off-meta or partially specced bots still get sane itemization [#191](https://github.com/Sagiroth/TortoiseBots/pull/191)
- Bots now match their LFT role against their spec-correct gear, so dungeon finder groups get properly equipped tanks/healers/DPS instead of mismatched builds [#191](https://github.com/Sagiroth/TortoiseBots/pull/191)

## 2026-09-16

### Bots, Loot & Inventory
- Fresh pool bots can now start at a random level instead of always walking up from level 1: `AiPlayerbot.RandomBotStartLevelMin`/`Max` seeds the level once on a bot's first login, before its weapon skills, professions and starter gear are seeded, so a test or fresh pool starts playable (default `1`/`1` keeps the old behaviour).
- Group loot rolls are no longer a ninja-fest: bots pass Greed on armor lighter than their spec's native type (no more Enhancement shaman stealing cloth), and reserve Need for native-spec gear or solo/world-map drops (#183)
- Off-armor upgrades now actually get equipped once the slot is emptied, fixing bots hoarding upgrades in bags instead of wearing them (#183)
- Bag audit pass cleans up inventory handling so bots stop stranding useful gear and quest items (#183)
- Safe quest cleanup prevents bots from corrupting quest state during routine pruning (#183)
- Module-only changes — no core patches required, safe drop-in for existing servers (#183)

### Observability & Tooling
- New self-contained bot armory inspector ships under `tools/observability/` for DB-only bot audits (#180)
- Inspect any bot's full profile: identity, all 38 equipment/bag slots with resolved item GUIDs, live stats for online bots, spells with icon + effect metadata, skills, and talents (#180)
- Searchable bot listing with LIKE-escaped account-prefix filtering and name search, capped at 1000 results (#180)
- Stats layer falls back to `character_stats` when `character_armory_stats` is missing, so the inspector works across setups (#180)

### CI & Releases
- Changelog updates now auto-post to the dedicated Discord thread straight from GitHub Actions — no bot invite or server ownership needed (#184)
- Delta-only delivery: only newly merged PRs get announced, and anything already in `CHANGELOG.md` is skipped, so nobody gets spammed with the full history (#184)

---

### Observability & Engine
- The armory Spells tab now shows the *full* spellbook instead of a DB-only view, so starting spells like Sinister Strike and Eviscerate no longer vanish from fresh characters — race/class defaults are pulled in properly. [#186](https://github.com/Sagiroth/TortoiseBots/pull/186)
- Spells are grouped class-aware and split into active vs. passive, replacing the fragile `Rank N` name heuristic that made the CLASS SPELLS counter wildly wrong (e.g. a level-10 hunter reading as nearly empty). [#186](https://github.com/Sagiroth/TortoiseBots/pull/186)

### Gear & Itemization
- Ship the actual item weight scale data, not just the empty schema — `ai_playerbot_weightscales` and `ai_playerbot_weightscale_data` now contain rows, so `RandomItemMgr::GetPlayerSpecId()` returns a real spec instead of 0 and bots can finally judge gear. [#185](https://github.com/Sagiroth/TortoiseBots/pull/185)
- `PlayerbotFactory::InitEquipment` no longer bails out and stat comparisons stop scoring zero across the board — bots actually get equipped sensibly. [#185](https://github.com/Sagiroth/TortoiseBots/pull/185)
- Character-side caches (`ai_playerbot_equip_cache`, `ai_playerbot_rnditem_cache`) are now generated on first boot; the cache-building code was previously stuck behind a condition that could never be true. [#185](https://github.com/Sagiroth/TortoiseBots/pull/185)

### Core Sync & Fixes

- Purged the three dead `SyncLevel*` config keys (`SyncLevelWithPlayers`, `SyncLevelMaxAbove`, `SyncLevelNoPlayer`) — they were parsed in `PlayerbotAIConfig` but never consumed by any other translation unit, so this is a pure cleanup with zero behavior change. [#187](https://github.com/Sagiroth/TortoiseBots/pull/187)
- Rewrote `living-world.md` §3 to document the real Fresh-Bot Level Seed behavior: `RandomBotStartLevelMin/Max` applied via a one-shot `GiveLevel`, verified against the 10–15 test pool. No more chasing a dynamic level bracket that never existed. [#187](https://github.com/Sagiroth/TortoiseBots/pull/187)
- Dropped the stale conf comment claiming `Randomize()` reassigns levels — that code path has no callers, so the docs and `.conf.dist.in` now match reality. Fewer red herrings when tuning bots. [#187](https://github.com/Sagiroth/TortoiseBots/pull/187)

## 2026-09-15

### Performance & Engine

- Staggered the cell-grid spatial scan per bot on a 1s cadence instead of every tick, cutting the biggest chunk of world-tick CPU (~60%). Discovery of new grind targets is delayed, but whole-bot decisions stay live; in-combat, dead, low-HP/mana, and post-revive/teleport all force back to the full 100ms rate. (#179)
- Idle skips now suppress empty scans entirely: bots on taxi flights and RESTING+SANCTUARY or RESTING+stationary regen stop burning scan time for nothing. (#179)
- SweepStrandedBots no longer excludes bot-only groups, and the UpdateAI telemetry string concat + perf monitor start are gated behind perfMonEnabled. (#177)
- GetPriorityType / HasPlayerRelation now check real network sessions instead of iterating all 1000 random bots, with the IN_EMPTY_SERVER early-out restored. Noticeable tick savings on crowded servers. (#177)

### Combat & AI

- Dungeon corpse runs work cross-map: a bot that dies in an instance (e.g. Zul'Farrak) finds the entrance portal on the ghost's continent map, steps through, and pathfinds to its corpse inside. Also fixed the false master-resurrect block in FindCorpseAction. (#172)
- New `.bot role <name> tank|healer|dps|clear` command forces a role; tank kit mirrors native AiFactory strategies. IsPullCandidate now follows explicit selection > designated tank > native spec and never guesses DPS. (#167)
- Fixed the pull movement/freeze issue, added ranged fallback when no melee tank is available, and added a DPS threat window with pause support. (#167)
- Party and whisper chat commands are now routed into the AI, so bots respond to direct coordination without needing a full command channel. (#172)

### Rescue & Core Sync Fixes

- Misplaced-bot rescue (hopeless-death relocation + stranded sweep) now works for bots grouped with other bots: a group only protects a bot when a real player is in it. Bot-only-grouped bots are relocated and leave the group first. (#176)
- Death count is no longer wiped by XP packets, including exploration XP handed out when a ghost is repopped to a graveyard — so rescue thresholds fire correctly. (#176)

### Runtime & Build

- Replaced all 22 `boost::algorithm` call sites (`iequals`, `istarts_with`, `trim`) with hand-written equivalents. This drops boost-algorithm, the last compile-time Boost dep, and its ~35-package vcpkg transitive closure. Faster CI and cleaner builds. (#173)
- Removed three dead third-party includes (boost::stacktrace in MemoryMonitor, plus vestigial OpenSSL/Boost includes in PlayerbotLLMInterface and LootValues). No behavior change; less to link and maintain. (#171)
- Dropped the OpenSSL RAND_bytes fallback from GenerateRandomPassword, which was forcing `libcrypto-3-x64.dll` to load even on builds that otherwise didn't need it. (#170)

### Addon & Tooling

- New silent addon command channel: `host/BotAddonAdapter` hooks PLAYERHOOK_ON_ADDON_MESSAGE and forwards `TBM`-prefixed payloads to the existing BotCommands entry point. UI clicks drive `.bot` commands with no chat-frame spam and no echo to nearby players. (#165)
- Documented the merged host chat seams (transport, script hooks, headless drain, chat hardening, OnChatYell) in HOST_API.md and pinned the compatible baseline to merged main. (#164)
- Changelog CI now appends to an existing same-day section and updates the existing GitHub release instead of failing with HTTP 422 on tag collision. (#169)

---

## 2026-09-14

### Combat & AI
- Bots now path around obstacles to reach targets that are out of line of sight instead of walking into walls; targets that stay unreachable for 15s get blacklisted per-bot for 5 minutes, killing the `invalid target` trigger spam (#157)
- Capital city critters and NPCs are no longer grind targets — no more random bots picking fights with Gamon while the player is just trying to use the auction house; anything that attacks the bot still gets fought back (#158)

### Starter Zones & World
- Goblin and High Elf bots rescued by `TeleportMisplacedBot` are now routed to their homebind instead of being dumped on Blackstone Island or stranded in Hillsbrad at level 5 — no more bots stuck in zones with no way out (#161)

### Core Sync & Fixes
- Death is now logged and counted exactly once: repeated `OnDeath` fires during graveyard teleport and spirit-healer revive no longer inflate death counts several times per second (#159)
- Removed the bogus `UNIT_STAT_STUNNED` logout check — combat stuns no longer trigger a bot logout, restoring correct `isLogingOut()`-based behavior from the donor core (#160)

### Tooling & Docs
- Added `tools/generate_changelog.py` plus a `CHANGELOG.md` seed and a `generate-changelog.yml` workflow, so releases can be generated from merged PRs with OpenCode AI instead of hand-writing notes (#163)
- Updated canonical target core branch references from `bot-helpers` to `1181dev` in `README.md` and `CONTRIBUTING.md` after the upstream merge (#162)

---

## 2026-09-13

### Starter Zones & Survival
- **Unsurvivable Zone Repatriation:** Repatriate alive bots stranded in zones significantly above their level range ([#155](https://github.com/Sagiroth/TortoiseBots/pull/155)).
- **Custom Starter Island Blacklist:** Route Goblin and High Elf bots to standard starter zones and blacklist custom islands lacking egress paths ([#149](https://github.com/Sagiroth/TortoiseBots/pull/149)).
- **Classic Zone Level Population:** Populated `ai_playerbot_zone_level` with classic zone level mappings ([#148](https://github.com/Sagiroth/TortoiseBots/pull/148)).
- **Low-Level Travel Gating:** Reject travel destinations whose route crosses zones the bot cannot survive, keeping bots below level 10 within their starter regions ([#147](https://github.com/Sagiroth/TortoiseBots/pull/147), [#150](https://github.com/Sagiroth/TortoiseBots/pull/150), [#153](https://github.com/Sagiroth/TortoiseBots/pull/153)).

### Observability & AI
- **Rage Telemetry Units:** Converted rage values to normal 0-100 display units for dashboard visualization ([#154](https://github.com/Sagiroth/TortoiseBots/pull/154)).
- **Stuck Detector Sampling:** Throttled stuck evaluation to once per second rather than every world tick ([#152](https://github.com/Sagiroth/TortoiseBots/pull/152)).
- **AI Texts & Item Casting:** Seeded `ai_playerbot_texts` and repaired item cast validation checks ([#151](https://github.com/Sagiroth/TortoiseBots/pull/151)).
