# Changelog

### Bots & Behavior
- Bots no longer get stuck on the Deeprun Tram — wandering bots could be routed through the tram between Stormwind and Ironforge but cannot ride the train, so they stood on the platform for good; their routes now go overland, and any bot already stranded there is moved out through the nearest station exit.
- Bots stranded on islands without travel routes (Lapidis Isle) no longer stand at the shore forever — after three destinations in a row fail from the same spot they are moved to the nearest travel route instead of an island graveyard they would just walk back from.
- Bots move on from areas with nothing for them — a wandering bot (level 10+) whose last two searches for a grinding spot came back empty now heads for a zone that fits its level, the same way it leaves a zone it has outgrown; and a wandering bot with every errand on hold checks for a new one every 15 seconds instead of once a minute.
- Bots with nothing to do take a short walk instead of standing — a wandering bot whose every search for a destination comes back empty now walks 20-50 yards to a random reachable spot (never into a guarded enemy town), so new mobs come into view and its next search starts from somewhere else.
- Bots fighting in place no longer count as stalled — a bot that fought or gained experience in the last minute (camping a spawn between pulls) now shows as busy instead of stalled, so the stalled count only means standing with a destination and getting nowhere.
- Bots stop looping on a quest giver with nothing for them — a masterless pool bot that arrives at a giver whose menu never offers the quest (wrong chain step, already taken, accept rules) now leaves that giver and quest alone for 30 minutes after the second wasted visit, so the next pick goes elsewhere instead of walking back forever; owned bots are unchanged.
- Fewer "no route" trips — the walk from the bot to the first waypoint, and from the last waypoint to the target, now counts as reachable when it ends within 5 yards instead of 1, so a target standing just off the walkable ground (a doorway, a bridge edge, a vendor behind a counter) no longer refuses the whole route.
- Bots with no route keep walking toward their goal — when no path to a far target is found, a wandering bot standing off the walkable ground (in water, under a city, on a ledge) hops back onto the nearest walkable spot, and otherwise walks to a reachable point that is at least 5 yards closer and tries again from there, instead of standing and dropping the trip.
- Stuck-trip rescues land on the street, not under it — the class-trainer rescue teleport now picks the walkable spot closest to the trainer's own floor and checks it walks back to the trainer, instead of the mesh under Stormwind where over half of the rescues landed and stood for good.
- Bots stop standing in portals that refuse them — a world portal that turns a wandering bot away (the Booty Bay transpolyporter without its transponder) now takes it through anyway; any other refused trigger, such as a dungeon entrance below its level, is closed for that bot for 30 minutes so routes go around it.
- Bots walk on from an empty destination instead of standing out the clock — a masterless pool bot that arrives where there is nothing to do (no attackable grind prey, nobody fighting it) expires the WORK hold after ~30 s of nothing and requests a new target on the next visit, instead of standing until the 5-min WORK timer runs out; owned bots keep the full stay.
- Masterless pool bots no longer pick grind spots inside capital cities — a bot told to leave while standing in Orgrimmar, Stormwind or another capital walked to a spot inside the same city and stood there with nothing to kill; capital grind points are refused at pick time and the next candidate wins, while owned bots and capital vendor/trainer trips are unchanged.
- Bots travel through neutral towns again — Booty Bay, Gadgetzan, Ratchet and Everlook no longer count as hostile towns for travel routes and destinations, so bots stop standing in south Stranglethorn, Tanaris or Winterspring with every way out refused, and can take the Booty Bay–Ratchet boat and visit quest givers there; bots still never start a fight inside those towns.
- Bots move straight on to the next quest errand — a finished quest trip (quest accepted, objective done, reward taken) no longer sits in a one-minute cooldown that blocked every new pick, so pool bots stop standing 1-3 minutes after each hand-in; owned bots keep the old pacing.
- Trips that never arrive stop re-picking the same zone — a grind travel target that cools down while still on the way (the six-fail drop never fires because the 60 s cooldown expires first) parks its purpose for five minutes like a drop, so a bot standing in a capital stops re-picking the same grind zone every ~2.5 min; arrived trips keep today's behaviour.
- Starved bots fight what's beside them instead of re-requesting travel — a bot that holds a grind pick with no journey and whose recent travel searches came back empty across the rotation (3+ of quest/grind/camp/explore/gather/boss/vendor/repair/AH/mail parked) stops requesting new errands until the pick resolves, so `attack anything` (5.0) wins instead of losing every visit to a request (6.3-6.99) that refuses and parks; a bot whose searches succeed quests exactly as today.
- Parked bots drift instead of standing — the 50 yd idle wander no longer waits for the grind target to come back empty, so a held-but-unattackable pick stops vetoing the only motion that can break the standstill; the attack row still wins whenever the prey is usable.
- Targetless bots stop standing out the full quest park — a `move stuck` trip with no travel target caps the 10-minute quest-errand park at 1 minute, so the next decision may re-search instead of idling; unreachable spots stay refused by the existing pick gates.
- Bots stop re-picking the same quest giver forever — the third consecutive pick of one quest id parks the quest errand for a minute, bounding the stand-at-giver loop while the pick itself still lands.
- Bots give up on a grind spot the world mesh cannot reach — a same-map walk that probes `nopath` blacklists that creature kind for five minutes (the same list the give-up-on-a-wedged-mob rule uses), so after the doomed target drops the next pick walks a different kind instead of re-picking the same spot and firing `move stuck` resets in place.
- Grind picks skip spots off the world mesh — a candidate point with no walkable navmesh polygon nearby is refused at pick time, so the next candidate wins instead of six wasted walks, a drop, and a re-pick of the same unreachable spot.
- Bots stop standing on a travel walk that goes nowhere — a move that reports success while the bot stays inside 2 yards for 5 seconds now fills the failure budget like a refused move, so the six-fail drop, the purpose park and the kind blacklist engage instead of phantom successes cancelling real failures until the travel timeout.
- Gather trips walk on from an empty node instead of standing out the cooldown — a gather trip that reaches its node to find it looted, tapped or despawned no longer sits in a one-minute cooldown that blocked every new pick, so the next visit walks a live node instead of standing a median 57 yards out; arrived trips and owned bots keep today's behaviour.
- Bots get off their mount to gather — a bot that rode to its herb or vein now dismounts when it opens the node, like it already does for corpses, instead of burning its approach tries on casts that never start and abandoning the node.
- Gather (mining/herbalism) picks skip spots off the world mesh too — the same one-query pick-time sieve grind got, after 51 of 65 mining move-failures probed `nopath` the same way.

## 2026-10-08

### Combat & AI
- Bot pools at 2000 no longer freeze: each bot is now timed off real seconds since its own last update, instead of counting every pool visit as a world tick — global cooldowns, pauses and timers no longer run 15–40x slow, so bots move and react in combat. [#529](https://github.com/Sagiroth/TortoiseBots/pull/529)
- Large pools (1000–2000 bots) behave again: far fewer corpses lying around, bots that actually move and fight, and noticeably faster levelling. [#529](https://github.com/Sagiroth/TortoiseBots/pull/529)

### Observability & Engine
- The dashboard now shows how long the server makes a player wait, replacing a world-tick number that hid the lag spikes. [#529](https://github.com/Sagiroth/TortoiseBots/pull/529)

---

### Bots & Behavior

- Trainer trips now actually run to completion — the "one trip at a time" rule was checked while the trip was already running, so every trip cancelled itself on its first step and bots stood still for minutes; trips now walk, and the sub-1% rate of trips that taught anything should be a thing of the past. [#542](https://github.com/Sagiroth/TortoiseBots/pull/542)
- Bots now look for a trainer nearby (500 yd at level 5) instead of burning their one decision on a dead-end walk. [#542](https://github.com/Sagiroth/TortoiseBots/pull/542)
- Idle bots no longer waste their single decision tick on no-op actions — with a big bot pool each bot only decides every 10-20 seconds, so any wasted decision showed up as bots standing around doing nothing. [#542](https://github.com/Sagiroth/TortoiseBots/pull/542)

### Combat & AI

- Graveyard trade-kills in contested zones are addressed — the spirit-healer fights where one side farms bots straight off resurrection should be far less common. [#542](https://github.com/Sagiroth/TortoiseBots/pull/542)

### Bots & AI
- Bots now leave a trainer or vendor as soon as they're done — previously a trip stayed "at work" for a full five minutes after arrival, so a bot that learned all its spells in one second loitered next to the trainer for the remaining time. [#543](https://github.com/Sagiroth/TortoiseBots/pull/543)
- Fixed the "standing around between errands" pattern at scale (2000 bots): most idle bots weren't broken, just blocked on activities that take minutes while only getting a decision every 15–25 seconds — those waits now resolve instead of parking bots in the world. [#543](https://github.com/Sagiroth/TortoiseBots/pull/543)

### Install & Migrations

- Character-database migrations are now in `data/sql/character`, matching the folder the core's auto-updater reads from `mangosd.conf` — they finally run on a normal build instead of being silently skipped. [#544](https://github.com/Sagiroth/TortoiseBots/pull/544)
- Migration tracking is by file name, so realms that already applied these changes won't have them re-run or double-applied. [#544](https://github.com/Sagiroth/TortoiseBots/pull/544)
- Reinstalling the module no longer overwrites your edited configs — admin tweaks survive an update. [#544](https://github.com/Sagiroth/TortoiseBots/pull/544)

### Combat & AI
- Dungeon/raid bots grouped with a human master no longer initiate fresh grind pulls on untouched packs; they still defend the group/pets and attack explicit targets or configured raid marks. [#538](https://github.com/Sagiroth/TortoiseBots/pull/538)

### Talents & Commands
- `talents shift <link>` now recounts the bot's current allocation before applying differences, so requested builds actually spend all available points instead of stopping short. [#536](https://github.com/Sagiroth/TortoiseBots/pull/536)
- Talent link validation rejects malformed extra tree separators like `1---2` before they reach `stoi("-")`; valid one/two/three-tree links still parse. [#531](https://github.com/Sagiroth/TortoiseBots/pull/531)

### Movement & Pathing
- Hazard avoidance now writes accepted waypoint detours back into the path instead of sending the unchanged route to movement. [#530](https://github.com/Sagiroth/TortoiseBots/pull/530)
- Travel route distance now includes the final edge, so two-node routes don't report zero and multi-edge routes don't undercount. [#540](https://github.com/Sagiroth/TortoiseBots/pull/540)
- Restored idle after hazard-chase movement clears the MotionMaster stack, preventing core `!empty()` assertions on the next player movement update. [#539](https://github.com/Sagiroth/TortoiseBots/pull/539)

### Threat & Stats
- Threat-rate queries return zero when history is cold instead of dereferencing an empty `list::front()`. [#532](https://github.com/Sagiroth/TortoiseBots/pull/532)
- Threat history now records accepted changes instead of dropping them and logging duplicate reads, preserving the samples needed by 10/15-minute movement checks. [#533](https://github.com/Sagiroth/TortoiseBots/pull/533)
- Threat sampling survives a missing current target by using an empty GUID, keeping reset/zero-threat behavior intact. [#534](https://github.com/Sagiroth/TortoiseBots/pull/534)
- Threat helpers recheck the victim after friendly-target redirection and return zero threat instead of null-dereferencing when no victim exists. [#535](https://github.com/Sagiroth/TortoiseBots/pull/535)
- Mana percentage returns zero for units without a mana pool, avoiding 0/0 and invalid float-to-byte conversion. [#537](https://github.com/Sagiroth/TortoiseBots/pull/537)

### Outfits & Persistence
- Saved-outfit parsing uses full-width string positions instead of `uint8`, so outfits over 255 characters no longer wrap and block listing/loading. [#541](https://github.com/Sagiroth/TortoiseBots/pull/541)

### Core Sync & Fixes
- Landed @waterys419's twelve-fix batch #530–#541 together, since they all add tests to the same check script and conflict if merged individually. [#545](https://github.com/Sagiroth/TortoiseBots/pull/545)

## 2026-10-07

### Combat & AI
- Four Horsemen mark comments in `DungeonTriggers.h` and `ReactionStrategy.cpp` now match reality: marks only trigger at 3+ stacks and map to a generic "move away from hazard" action — no rotation, no tank assignment, no safety check was ever implemented. Comment-only change; no behavior impact, but nobody should build expectations on a phantom rotation. [#520](https://github.com/Sagiroth/TortoiseBots/pull/520)

---

### Observability & Engine
- Write-mode bot logs now preserve one previous run as `<filename>.1`, including `bot_events.csv`, so restart no longer truncates evidence needed for live troubleshooting. [#522](https://github.com/Sagiroth/TortoiseBots/pull/522)
- Rotation runs once per filename per process, leaving the previous run intact across later dump refreshes; failed rotation refuses the destructive open, and append-only logs/CSV formats remain unchanged. [#522](https://github.com/Sagiroth/TortoiseBots/pull/522)

### Guild Bots & Roster
- Claimed Guild Bots now use a dedicated policy: level 60 gear locks preserve equipped gear, guild members get full control, and roster state streams over the TBM protocol. Verified clean via `verify_all.sh`, standalone claimed-policy tests, and cached mangosd build. [#523](https://github.com/Sagiroth/TortoiseBots/pull/523)

### Observability & Engine
- CI now runs `go vet` + `go test` and ships static `tortoise-observability-linux-amd64` / `-windows-amd64.exe` binaries, uploaded to per-merge tags and the daily `vYYYY-MM-DD` release — no more manual `go run` to get the dashboard up for external builds. [#524](https://github.com/Sagiroth/TortoiseBots/pull/524)
- The build also triggers on changes to `runtime/ObservabilityEmitter.*`, keeping the wire protocol and dashboard binaries in lockstep. [#524](https://github.com/Sagiroth/TortoiseBots/pull/524)
- New `tools/observability/run-dashboard.sh` / `.ps1` helpers pull the binary straight from the release and auto-read `LoginDatabase.Info` / `WorldDatabase.Info` / `CharacterDatabase.Info` from `mangosd.conf`, so setup works without Docker or a local Go toolchain. [#524](https://github.com/Sagiroth/TortoiseBots/pull/524)

### CI & Release
- Dashboard binaries workflow now uses `paths-ignore: ['CHANGELOG.md', 'VERSION']` instead of a path inclusion filter, so daily `vYYYY-MM-DD` release tags reliably build and attach static dashboard binaries. [#525](https://github.com/Sagiroth/TortoiseBots/pull/525)

### Discord & Tooling
- Discord changelog poster avoids appending ` • {tag}` when the title already contains the release tag or date, preventing duplicate headers. [#525](https://github.com/Sagiroth/TortoiseBots/pull/525)

### Login & Chat Polish
- Follow-up polish for in-game login/chat messages after #524 to clean up player-facing text. [#525](https://github.com/Sagiroth/TortoiseBots/pull/525)

### Guild Systems & Hired Bots

- Hired mercenary bots now refuse guild invitations: `/ginvite` gets a polite "I am a hired mercenary and cannot join a guild." and the invite packet is declined immediately. [#526](https://github.com/Sagiroth/TortoiseBots/pull/526)
- Hired bots can no longer be used to sign guild charters (`MSG_PETITION_SHOW_SIGNATURES`), closing a loophole around guild creation requirements. [#526](https://github.com/Sagiroth/TortoiseBots/pull/526)
- Hardened the `MayClaimBot` policy gate in `runtime/ClaimedBotPolicy.h` to reject hired bots as defense-in-depth, with matching unit test coverage in `tools/test_claimed_bot_policy.cpp`. [#526](https://github.com/Sagiroth/TortoiseBots/pull/526)

### Observability & Engine

- Cut ObservabilityEmitter line-of-sight cost: targets beyond 45yd short-circuit and VMap raycasts are now sampled, eliminating 140+ raycasts per world tick. [#527](https://github.com/Sagiroth/TortoiseBots/pull/527)
- Telemetry tracking maps switched to `std::unordered_map` for constant-time lookups, removing per-lookup scan overhead. [#527](https://github.com/Sagiroth/TortoiseBots/pull/527)

### World, Travel & Pathing

- Masterless random bots can no longer pick cross-map travel destinations (trainers, vendors, grind spots) via `IsLocationLevelValid`, killing cross-continent pathing failures and wedged retry loops. [#527](https://github.com/Sagiroth/TortoiseBots/pull/527)
- Travel and RPG priorities are now aligned so `ChooseRpgTarget` no longer hijacks an in-progress travel decision and leaves bots thrashing between goals. [#527](https://github.com/Sagiroth/TortoiseBots/pull/527)

### Combat & AI
- Added `AiPlayerbot.CombatTickBudgetUs` (default 15ms) for Pass 2 combat bots, capping how much combat AI work can run per world tick. [#528](https://github.com/Sagiroth/TortoiseBots/pull/528)
- Combat updates now use a round-robin `m_combatCursor` across pool bots for fair distribution when many bots fight, preventing world tick spikes; live 2000-bot testing dropped world ticks from ~150–250ms to ~70ms steady. [#528](https://github.com/Sagiroth/TortoiseBots/pull/528)

## 2026-10-06

### Observability & Engine

- When `AiPlayerbot.Enabled = 0`, the world-update hook now bails out before calling any AI services, killing the empty `BOTPERF` window that `BotManager` spammed into the log every 30 seconds. [#509](https://github.com/Sagiroth/TortoiseBots/pull/509)
- `HireLifecycle` no longer runs its 60-second stale-hire recovery loop against an unvalidated managed-pool registry while AI is disabled, removing pointless retries during normal (AI-off) play. [#509](https://github.com/Sagiroth/TortoiseBots/pull/509)
- This early return also covers a missing AI config file, so broken/absent config no longer ticks half-initialized services behind your back. [#509](https://github.com/Sagiroth/TortoiseBots/pull/509)
- With AI enabled, service tick order and behavior are unchanged — no gameplay impact for servers running bots. [#509](https://github.com/Sagiroth/TortoiseBots/pull/509)

---

### Combat & AI
- Fixed mana-using bots at exactly 0 mana being rejected from the noncombat drink action; `ShouldDrinkValue` now checks mana power type and the existing drink stop threshold instead of misreading current mana as the bot’s power type. [#513](https://github.com/Sagiroth/TortoiseBots/pull/513)
- Added focused policy tests for zero mana, the threshold boundary, and non-mana users to prevent regressions. [#513](https://github.com/Sagiroth/TortoiseBots/pull/513)

### Combat & AI
- Heal interruption now sees cast-time heals in the native `PREPARING` state, so bots cancel direct single-target heals that would land on a full or >90% target before mana is spent — no more finishing wasted heals while another healer already topped the target up. [#512](https://github.com/Sagiroth/TortoiseBots/pull/512)

### Loot & Items
- Bots no longer hover over corpses for items already taken — globally looted entries are now filtered out of the shared loot view before `ShouldLootObject` runs. [#510](https://github.com/Sagiroth/TortoiseBots/pull/510)
- Personal FFA and conditional loot is now read only through the per-player lists, so each bot's own looted flag is honored instead of being bypassed by the initial shared insertion. [#510](https://github.com/Sagiroth/TortoiseBots/pull/510)

### Combat & AI
- Flee/spread movement no longer vetoes headings just because a destination was dispatched; destination-based fleeing and combat spread now track failures in separate two-entry caches, only recording a heading if the bot gains less than 2 yd of separation after 3 s, preventing successful straight retreats from being blocked later. [#511](https://github.com/Sagiroth/TortoiseBots/pull/511)

### Observability & Engine
- Persist cumulative bot activity metrics across server sessions and daemon restarts, avoiding state wipes on session start. [#515](https://github.com/Sagiroth/TortoiseBots/pull/515)
- Add `OpenQuestsRollup` in `gear.go` and `store.go` to query and sync active in-progress quests from `character_queststatus`. [#515](https://github.com/Sagiroth/TortoiseBots/pull/515)
- Add `tools/backfill_activity_logs.py` to import historical loot items, mob coin drops, creature kills, deaths, vendor visits, AH listings, and quest cash rewards from server archives into `activity-state.json`. [#515](https://github.com/Sagiroth/TortoiseBots/pull/515)
- Add economic reconciliation to keep persisted activity/economy state consistent. [#515](https://github.com/Sagiroth/TortoiseBots/pull/515)

### Combat & AI
- Implement consumable mimicry according to `MIMIC_CONSUMABLES_PLAN.md`. [#514](https://github.com/Sagiroth/TortoiseBots/pull/514)
- When the master uses an elixir/flask/scroll/weapon imbue/protection potion/stat food out of combat, bots with `mimic` enabled drink the class/spec/level-appropriate equivalent, choosing the highest usable tier with `req <= level`; all item IDs and required levels verified against core `sql/base/tw_world_item_template.sql`. [#514](https://github.com/Sagiroth/TortoiseBots/pull/514)
- Make mimicry opt-in via `.bot behavior <bot> mimic on|off`, default OFF; `mimic consumables` strategy runs on the non-combat engine only. [#514](https://github.com/Sagiroth/TortoiseBots/pull/514)
- Use virtual casts via `BotUseItemSpell::Create + ForceSpellStart` with no inventory items, plus `EMOTE_ONESHOT_EAT` feedback. [#514](https://github.com/Sagiroth/TortoiseBots/pull/514)

### Core Sync & Fixes
- Module shutdown now cleanly removes and saves every tracked bot session — including hired companions outside `m_candidates` — before core map/database teardown, preventing bot state loss and shutdown-time errors. [#517](https://github.com/Sagiroth/TortoiseBots/pull/517)

### AI & Performance
- Replaced per-bot Auction House map lookups with a cached map set built once via `std::call_once` and zero-copy reference lookups, removing `EntryGuidps` deep copies from `AhBuyerTripNeeded` and turning the hot-path check into an O(1) `GetMapId()` call. [#519](https://github.com/Sagiroth/TortoiseBots/pull/519)
- Added an early financial exit for `spendable < ai::kBuyerTripMinSpareCopper`, so broke AH buyers skip travel evaluation entirely and stop burning world-thread CPU. [#519](https://github.com/Sagiroth/TortoiseBots/pull/519)
- Gated the city buyer trip path to avoid unnecessary travel evaluations on the world thread when the AH buyer flow already covers the bot’s needs. [#519](https://github.com/Sagiroth/TortoiseBots/pull/519)

### Performance & Travel
- Replaced O(N×M) hash-map scans in `TravelMgr::GetDestinations` with direct `entryDests.find(entry)` lookups, so ID-based quest/destination fetches no longer linearly scan every `destinationMap[purpose]` entry. [#521](https://github.com/Sagiroth/TortoiseBots/pull/521)
- Optimized destination filtering across `ChooseTravelTargetAction` / `TravelMgr::GetDestinations` / `TravelMgr::GetPartitions` / `WorldSquare`, reducing hot-path CPU during travel target evaluation. [#521](https://github.com/Sagiroth/TortoiseBots/pull/521)
- Reduced expensive cross-map distance query work in travel target selection, improving bot responsiveness and server CPU headroom. [#521](https://github.com/Sagiroth/TortoiseBots/pull/521)

## 2026-10-05

### World Buffs & Capital Recruiters
- Recruiters in Stormwind, Ironforge, Darnassus, Orgrimmar, Undercity, and Thunder Bluff now offer a *World buffs* menu for level 60 characters. [#493](https://github.com/Sagiroth/TortoiseBots/pull/493)
- Unlock each world buff per character via a short quest: earn it once the normal way (kill Onyxia/Nefarian, Rend, Hakkar; get a Dire Maul tribute buff, a Sayge fortune, or a Songflower; or win 5 PvP fights in Silithus) then pay a one-time 100–200g fee. [#493](https://github.com/Sagiroth/TortoiseBots/pull/493)
- Once unlocked, one click buffs you and every group/raid member within 40 yd of the recruiter for a small gold price (base fee + per person). [#493](https://github.com/Sagiroth/TortoiseBots/pull/493)
- Kills by your bots count toward your unlock progress. [#493](https://github.com/Sagiroth/TortoiseBots/pull/493)
- Random pool bots never receive these buffs. [#493](https://github.com/Sagiroth/TortoiseBots/pull/493)

### Configuration & Options
- New option `AiPlayerbot.WorldBuffsKeepInRaids` (off by default): when on, entering a raid no longer strips world buffs; when off, Turtle's normal rules apply and Upper Karazhan now strips them too. `AiPlayerbot.WorldBuffsEnabled` turns the whole feature off. [#493](https://github.com/Sagiroth/TortoiseBots/pull/493)

---

### World Buffs & Quests
- World-buff unlock quests now live under a proper quest log header, with objectives that state exactly what to do (e.g. "Onyxia or Nefarian slain") and the gold fee shown in the quest text before you accept. [#495](https://github.com/Sagiroth/TortoiseBots/pull/495)
- Recruiters display `!` when a quest is available and `?` when it's ready to turn in, so you can spot them at a glance. [#495](https://github.com/Sagiroth/TortoiseBots/pull/495)
- Buying a buff now plays a visible spell effect, and a confirm page lists the full price for you and every group member who will be buffed before you pay. [#495](https://github.com/Sagiroth/TortoiseBots/pull/495)
- Server owners can configure world-buff unlock fees via `AiPlayerbot.WorldBuffsUnlockFeeRaidCopper` (200g) and `AiPlayerbot.WorldBuffsUnlockFeeCopper` (100g); new fees apply after the next restart, while purchase prices were already configurable. [#498](https://github.com/Sagiroth/TortoiseBots/pull/498)

### Companions & Combat AI
- Hired companions no longer appear in gear far above their level, keeping their loadout believable and balanced. [#496](https://github.com/Sagiroth/TortoiseBots/pull/496)
- Hired companions now engage as soon as you target an enemy attacking your group, instead of waiting for you to land a hit. [#497](https://github.com/Sagiroth/TortoiseBots/pull/497)
- Priests reapply Power Word: Fortitude on the party during combat whenever it drops. [#497](https://github.com/Sagiroth/TortoiseBots/pull/497)

### Core Sync & Fixes
- TortoiseBotsManager: if the Mini button does nothing after an addon update, it now prompts you to restart the game client, since new addon files only load on a full restart. [#499](https://github.com/Sagiroth/TortoiseBots/pull/499)

### Questing & World

- World-buff unlock quests are no longer shareable with the group, and bots — including hired companions — will never pick them up. [#500](https://github.com/Sagiroth/TortoiseBots/pull/500)
- Pool bots caught in a death loop en route to quest NPCs now bail out and return later instead of feeding a camp endlessly — fixes the Ivar Patch/Rane Yorick meat grinder where 38 bots died 837 times overnight. [#502](https://github.com/Sagiroth/TortoiseBots/pull/502)

### Companions & Gear

- Hired companions at level 30+ now arrive properly geared with a neck and rings, picking suffix jewellery ("of the Eagle", etc.) that actually matches their spec instead of skipping it. [#501](https://github.com/Sagiroth/TortoiseBots/pull/501)

### Observability & Engine

- Stalled pool bots that earn no XP for 45 minutes get automatically relogged to unstick them — roughly 1 in 20 fresh bots could idle for hours after an early level-up. Battlegrounds and bots grouped with a real player are exempt. New option: `AiPlayerbot.RandomBotStallRelogMinutes` (`0` disables). [#503](https://github.com/Sagiroth/TortoiseBots/pull/503)
- Overnight log fixes rolled up from monitoring and player reports. [#504](https://github.com/Sagiroth/TortoiseBots/pull/504)

### Observability & Engine
- Dashboard redesign ships seven tabs, a single unified bot list, a slide-over bot profile, hash routing, and a responsive layout — far less clicking to find a specific bot or stat. [#505](https://github.com/Sagiroth/TortoiseBots/pull/505)

### Travel & Pathing
- Bots stuck with no valid path from their current position now trigger the long-stuck rescue after three failed targets instead of waiting 15 minutes — stranded bots recover in seconds, not quarter-hours. [#506](https://github.com/Sagiroth/TortoiseBots/pull/506)

## 2026-10-04

### Travel & Pathing
- Bots no longer get yanked to a random auctioneer on another continent while questing, fixing the "bot respawned in a far zone and then came back" reports. [#477](https://github.com/Sagiroth/TortoiseBots/pull/477)
- Trips to the auction house, banker, vendor, trainer, and mailbox now finish reliably: bots count as arrived once they stand at the NPC instead of circling the counter. If truly stuck and nobody is watching, they are placed at the destination (at most once per 30 minutes). [#477](https://github.com/Sagiroth/TortoiseBots/pull/477)

---

### Travel & Pathing
- Fixed bots stalling out when their route shrinks to a single step — they now keep walking instead of giving up a few yards from the target. Genuinely unreachable targets are still dropped as before. [#475](https://github.com/Sagiroth/TortoiseBots/pull/475)

### Group Buffs & Party
- Bots now refresh buffs *before* they expire, so groups stop losing uptime mid-fight. [#476](https://github.com/Sagiroth/TortoiseBots/pull/476)
- With three or more party members missing a buff, bots cast the group version when they know it and carry the reagent — Gift of the Wild, Arcane Brilliance, and the Prayer of Fortitude/Spirit/Shadow Protection line. [#476](https://github.com/Sagiroth/TortoiseBots/pull/476)

### Trade & Economy
- New config switch gates pool-bot trading with strangers. Off by default, so existing setups behave exactly as before unless you opt in. [#478](https://github.com/Sagiroth/TortoiseBots/pull/478)

### Combat & AI
- Combat spread behavior adjusted for the daily batch. [#481](https://github.com/Sagiroth/TortoiseBots/pull/481)

### Configuration & Options
- Additional owned-bot options exposed in this batch. [#481](https://github.com/Sagiroth/TortoiseBots/pull/481)

### Combat & AI
- Low-level bots now fight back against tougher mobs attacking them while en route to a quest giver or trainer, and their stable approach points stop them from falling off platform edges near busy NPCs in Dolanaar, Valley of Trials, and Kharanos. [#484](https://github.com/Sagiroth/TortoiseBots/pull/484)

### Observability & Engine
- Full daily changelog posting to Discord is now on main so the manual re-post workflow can run; changelog tooling only, no gameplay changes. [#482](https://github.com/Sagiroth/TortoiseBots/pull/482)

### Combat & AI
- Mages and priests can kite straight away from mobs again; the flee-direction memory that caused sideways zig-zagging and roughly doubled deaths is disabled for normal flees until a smarter version lands, [#487](https://github.com/Sagiroth/TortoiseBots/pull/487).

## 2026-10-03

### Levelling & Progression
- Levelling pack validated on a fresh 500-bot pool overnight (XPRate 3): quests turned in +25% and gathering +55% versus the previous cycle, with levels 5 and 6 reached 6–8 minutes sooner. [#411](https://github.com/Sagiroth/TortoiseBots/pull/411)
- Starter kit, vendor-sourced weapons, and professions available at level 5 give fresh pool bots a functional loadout instead of scavenging bare-handed. [#411](https://github.com/Sagiroth/TortoiseBots/pull/411)

### Death & Recovery
- Death loops are now self-correcting: after two escapes from the same death cluster within an hour, a bot avoids that spot — 100 yd / 15 min at level 5 and below, 300 yd / 60 min above. A level-up clears the entry, and up to 3 spots are tracked. [#411](https://github.com/Sagiroth/TortoiseBots/pull/411)
- Stuck-bot rescue pulls stranded bots back into the levelling flow rather than letting them idle out in the world. [#411](https://github.com/Sagiroth/TortoiseBots/pull/411)

### Vendors & Economy
- Vendor re-picks down 75%: one vendor journey at a time, with a 10-minute window cleared by any sale or level-up. Full bags still always get through. [#411](https://github.com/Sagiroth/TortoiseBots/pull/411)

### Observability & Engine
- AI pass timing is unchanged at ~51 ms with 500 bots — all the new routing and vendoring logic costs nothing measurable on the server tick. [#411](https://github.com/Sagiroth/TortoiseBots/pull/411)

---

### Combat & AI
- Pool bots no longer open a fresh grind pull while under ~70% health/mana unless they're already under attack — cuts the "died within a minute of the last kill" cluster that accounted for ~33% of deaths. [#412](https://github.com/Sagiroth/TortoiseBots/pull/412)
- The opportunistic "attack before being attacked" strike while travelling is now gated to a lone mob inside the current grind level cap, so bots stop picking suicidal fights on the way to camp. [#412](https://github.com/Sagiroth/TortoiseBots/pull/412)
- Bots carrying free food now rest up properly instead of limping into the next fight at low resources. [#412](https://github.com/Sagiroth/TortoiseBots/pull/412)

### Movement & Recovery
- Levelling pack 2 targets the root causes found in the overnight run: deaths, stuck bots, ghost runs, and action loops. [#412](https://github.com/Sagiroth/TortoiseBots/pull/412)
- Ration handling folded into the levelling behaviour so bots resupply instead of stalling or repeating the same action. [#412](https://github.com/Sagiroth/TortoiseBots/pull/412)

### Observability & Validation
- Pack compiles clean and passes `tools/verify_all.sh`. [#412](https://github.com/Sagiroth/TortoiseBots/pull/412)
- Not yet measured live — deploy on a fresh pool and diff against `tools/pool_kpi_report.py` before trusting the death/loop numbers. [#412](https://github.com/Sagiroth/TortoiseBots/pull/412)

### Documentation

- README now has a "What It Does" section spelling out the module's actual feature set — living pool, recovery behaviour, starter kit, own party / hires, class AI, and observability — so newcomers can grok the scope without digging through the docs map. [#413](https://github.com/Sagiroth/TortoiseBots/pull/413)
- Docs are now aligned with the behaviour shipping in main, cutting down on stale-description confusion for server operators evaluating the module. [#413](https://github.com/Sagiroth/TortoiseBots/pull/413)

### Docs & Configuration
- README overview no longer implies every pool bot starts at level 1 — pool bots default to a spread across levels 1–60, and a fresh-realm level-1 start is just one setting via `RandomBotStartLevelMin` / `RandomBotStartLevelMax`. [#414](https://github.com/Sagiroth/TortoiseBots/pull/414)

### Documentation
- README overview rewritten to stay high-level: highlights core features and configurable playstyles (fresh level-1 realm or bots at every level) without leaking implementation details, with the docs carrying the rest. [#416](https://github.com/Sagiroth/TortoiseBots/pull/416)

### Combat & AI
- Combat-stuck give-ups no longer blacklist an entire mob kind: only the single offending mob is skipped, so common starters stay valid targets. Fixes bot progression being pushed onto mobs 3+ levels above them (deaths to over-leveled mobs had climbed from ~20% to 31%). [#417](https://github.com/Sagiroth/TortoiseBots/pull/417)
- Give-up now skips targets that are actively attacking the bot instead of dropping the whole entry, cutting down the 608 stuck-cycles / 174 bots and 19% death-within-a-minute spike observed in the post-#412 live check. [#417](https://github.com/Sagiroth/TortoiseBots/pull/417)

### Travel & Questing
- Low-level quest trips now obey the same +1 level cap as grinding: below level 10, a quest objective creature must fit the target's level window before bots will travel to it [#418](https://github.com/Sagiroth/TortoiseBots/pull/418)
- Removed the "quest-needed" exemption that let level-2 bots get sent into fields of level 5-6 Defias Cutpurses, Mangy Wolves, and Forest Spiders — no more suicide runs for Tough Wolf Meat [#418](https://github.com/Sagiroth/TortoiseBots/pull/418)
- When no valid quest target fits the cap, the purpose parks like any other empty search instead of forcing a bad trip, cutting the ~62.5% of low-level deaths caused by 2+ level-difference mobs [#418](https://github.com/Sagiroth/TortoiseBots/pull/418)

### Bot Supplies & Rations
- Pool bots now buy plain vendor food and drink matching their level tier (1/5/15/25/35/45) instead of buff food like Spiced Wolf Meat, Smoked Sagefish, or Dirge's Chimaerok Chops — no more wasted Well Fed items burned as cheap rations. [#419](https://github.com/Sagiroth/TortoiseBots/pull/419)
- Cleaned up the drink lists: Tough Jerky and Smoked Sagefish are no longer miscategorized as beverages. [#419](https://github.com/Sagiroth/TortoiseBots/pull/419)
- Each bot now sticks to one stable ration per tier rather than re-rolling a new random pick on every 30 s top-up, so bags won't slowly fill with a dozen half-stacks of assorted food. [#419](https://github.com/Sagiroth/TortoiseBots/pull/419)

### Questing & Progression
- Pool bots below level 10 no longer grab end-of-valley delivery quests at level 1-2 and immediately march into the next town — hand-ins are now parked until the quest level and taker's area level are within +1 of the bot's own level, cutting the ~⅓ of deaths that happened on quest giver/taker trips [#420](https://github.com/Sagiroth/TortoiseBots/pull/420)
- Replaces six hard-coded breadcrumb quest IDs with level-aware gating, so new starter-zone delivery chains are covered automatically instead of needing manual ID maintenance [#420](https://github.com/Sagiroth/TortoiseBots/pull/420)
- Owned/hired bots and level 10+ pool bots keep their existing behavior — no change to player-controlled or established bots [#420](https://github.com/Sagiroth/TortoiseBots/pull/420)

### Combat & AI

- Bots that decline a duel — whether they're too low level or too low on health — no longer immediately accept it anyway; the handler now returns after sending the cancel instead of falling through to the accept packet [#421](https://github.com/Sagiroth/TortoiseBots/pull/421)

### Starter Zones & World
- Low-level pool bots (1–9) now skip quest objectives, quest loot, and grind points with a hostile spawn above their level cap within 40 yd — ends the Northshire death spiral where "Tough Wolf Meat" runs fed bots straight into Forest Spiders, Mangy Wolves, and Defias Cutpurses. In the first live data, 211 of 523 deaths were levels 1–4, and 125 of those were on a single trip. [#428](https://github.com/Sagiroth/TortoiseBots/pull/428)
- Threat checks use a per-map cell index over the spawn table, built once — no world scan per decision, so pathing stays cheap. [#428](https://github.com/Sagiroth/TortoiseBots/pull/428)
- Neutral wildlife and vendors don't count as threats, so lowbies still turn in, buy, and grind in safe areas normally. [#428](https://github.com/Sagiroth/TortoiseBots/pull/428)
- Owned/hired bots and all level 10+ bots are unchanged — no impact on established characters or player-controlled companions. [#428](https://github.com/Sagiroth/TortoiseBots/pull/428)

### Core Sync & Fixes
- Fixed a build break on main caused by travel code calling `WorldPosition::GetHighestHostileLevelNear`, which was still in the private section. Both accessors are now public, so the module compiles cleanly again. [#429](https://github.com/Sagiroth/TortoiseBots/pull/429)

### Combat & AI
- Hunter and warlock pets on pool bots now run the autocast sweep and default to defensive stance, matching mod-playerbots parity — the live pet strategies never queued those actions before. [#430](https://github.com/Sagiroth/TortoiseBots/pull/430)
- Pets with a real player master are left alone, so players keep full control of their own pet. [#430](https://github.com/Sagiroth/TortoiseBots/pull/430)
- Medium-mana potions are now wired into the potions strategy, so bots actually use them instead of sitting on a dead node. [#430](https://github.com/Sagiroth/TortoiseBots/pull/430)

### Professions & Tools
- Mining and skinning allowlists now accept every pick and skinning knife that exists in 1.12, instead of one hardcoded item each. [#430](https://github.com/Sagiroth/TortoiseBots/pull/430)

### Observability & Telemetry
- Skinning loot is now detected via the server-side loot type (the 1.12 client mislabels it as pickpocketing), so GatherLoot rows and the dashboard skinning counter actually populate. [#431](https://github.com/Sagiroth/TortoiseBots/pull/431)
- Per-revive `hopeless check ... not eligible` lines are demoted to quiet — relocations still fire (4 logged on 2026-10-02), only the log noise was dialed down. [#431](https://github.com/Sagiroth/TortoiseBots/pull/431)

### Vendors & Trading
- Trading activity lease now outlives the auction house post tick, eliminating premature lease expiry warnings. [#431](https://github.com/Sagiroth/TortoiseBots/pull/431)
- Vendor-flagged NPCs with no goods (e.g. Terry Palin) are no longer treated as vendors, stopping the `empty trading item list` error spam. [#431](https://github.com/Sagiroth/TortoiseBots/pull/431)

### Combat & AI
- Shaman weapon upkeep (Rockbiter) now stands down when it can't progress — sitting, eating, casting, or already enchanted — instead of firing every tick and getting refused by the cast gate's stand-or-facing delay [#432](https://github.com/Sagiroth/TortoiseBots/pull/432)
- Nearby-service selector no longer acts on stale cached answers; it stays quiet when there's nothing to hand in, accept, sell, or train at the current NPC [#432](https://github.com/Sagiroth/TortoiseBots/pull/432)
- Loot loop gets the same stand-down treatment, cutting wasted ticks when there's nothing to pick up [#432](https://github.com/Sagiroth/TortoiseBots/pull/432)

### Quests & Progression
- Bots now prune their quest log when it runs low: failed, far over-level, elite/dungeon/raid, group-suggested, and other-zone quests get dropped, so the log stops filling with content they can never finish [#433](https://github.com/Sagiroth/TortoiseBots/pull/433)
- Quests are triaged using the same logic as mod-playerbots (donor `OrganizeQuestLog`), keeping parity between pool bots and native bots [#433](https://github.com/Sagiroth/TortoiseBots/pull/433)

### Loot & Rolls
- No more greed rolls on soulbound recipes the bot can't learn — wasted rolls and dead loot are gone [#433](https://github.com/Sagiroth/TortoiseBots/pull/433)
- Bots no longer roll need on duplicate uniques they already own [#433](https://github.com/Sagiroth/TortoiseBots/pull/433)
- Bots refrain from voting entirely in free-for-all and master-loot rolls, matching mod-playerbots' `LootRollAction` gates [#433](https://github.com/Sagiroth/TortoiseBots/pull/433)

### Starter Zones & World

- Bots no longer rot in their start valley: ordinary quests are once again open up to +3 (green/yellow), restoring hand-ins and XP flow after the post-#420/#428 quest-trip collapse ([#434](https://github.com/Sagiroth/TortoiseBots/pull/434))
- Fixed the taker-area check that mis-scored start valleys (Camp Narache 6, Dun Morogh 7, Durotar 8) as far above fresh bots, which was blocking in-valley hand-ins ([#434](https://github.com/Sagiroth/TortoiseBots/pull/434))
- Only the leave-the-valley case is now gated: a hand-in in a different area than the bot's for a quest above its level waits its turn ([#434](https://github.com/Sagiroth/TortoiseBots/pull/434))

### Consumables & Class Fixes
- Rogues now actually poison their weapons in the open world — Instant on main hand, Deadly or Instant on off hand. The upkeep hooks it relied on were never registered, so it silently never ran. [#435](https://github.com/Sagiroth/TortoiseBots/pull/435)
- Level-ups refresh poisons, weightstones, oils, and bandages to your current tier for pool bots, and full-bag cleanups keep them instead of vendoring or destroying them. No more re-buying consumables every ding. [#435](https://github.com/Sagiroth/TortoiseBots/pull/435)
- Warlocks harvest soul shards organically: Drain Soul will finish off an XP-giving target when shards run low, and Healthstones/Soulstones are created from real shards instead of conjured out of thin air. [#435](https://github.com/Sagiroth/TortoiseBots/pull/435)

### Combat & AI
- AoE fears — Psychic Scream, Intimidating Shout, and Howl of Terror — are no longer cast inside instances, so bots stop yanking extra packs mid-dungeon pull. [#436](https://github.com/Sagiroth/TortoiseBots/pull/436)

### Classes & Spells
- Spell teaching now validates the taught spell's own level, not just the trainer row's, and any over-level rank granted during provisioning is stripped and replaced with the highest legal rank. No more level-16 priests rocking max-rank Power Word: Fortitude. [#436](https://github.com/Sagiroth/TortoiseBots/pull/436)
- Rank correction runs after provisioning for every class, so hire-time downgrade edge cases stay fixed going forward. [#436](https://github.com/Sagiroth/TortoiseBots/pull/436)

### Combat & AI
- Bots can now opt into AoE fears (Psychic Scream, Howl of Terror, Intimidating Shout) inside dungeons/raids and under a real player master via `co +aoe fear` (whisper or `.bot strategy`), and back out with `co -aoe fear` — defaults stay safe so PUGs and raids don't get surprise feared packs. [#437](https://github.com/Sagiroth/TortoiseBots/pull/437)
- Per-spell suppression still works everywhere through the skip-spell list (`ss`), so you can keep a single fear on lockdown while leaving the rest of the toolkit available. [#437](https://github.com/Sagiroth/TortoiseBots/pull/437)
- Behavior and commands documented in the player controls guide. [#437](https://github.com/Sagiroth/TortoiseBots/pull/437)

### Economy & Vendors

- Vendor buys are now ordered by the bot's own item score — item level only used as a tiebreaker — matching mod-playerbots behavior so upgrades land where they actually matter. [#438](https://github.com/Sagiroth/TortoiseBots/pull/438)
- The gear budget now also covers replacing bad or broken gear, not just filling empty slots, so bots stop running around in shredded equipment. [#438](https://github.com/Sagiroth/TortoiseBots/pull/438)
- Trainer-money reserve still applies — bots won't blow their training funds on vendor loot. [#438](https://github.com/Sagiroth/TortoiseBots/pull/438)

### Starter Zones & World
- Fresh level 1–4 pool bots no longer get vetoed by the area-average level ceiling in starter zones (Durotar, Dun Morogh ratings sit above new characters), so they can actually pick grind points and level up instead of standing idle. Mob level bands, point danger, grey/elite checks, and route gates still apply. [#439](https://github.com/Sagiroth/TortoiseBots/pull/439)

### Combat & AI
- Service NPCs that can't be reached — indoors, on ledges, or otherwise unreachable — no longer trap bots in a failing approach loop every tick. Three failed approaches park that NPC for 90 s, matching the existing failing-verb behavior, and dead or unloaded NPCs are skipped outright (finishes the #407 nearby service loop cleanup). [#439](https://github.com/Sagiroth/TortoiseBots/pull/439)

### Travel & Pool Bots
- Pool bots now search local windows for grind and camp spots (500 yd near / 2500 yd far, scaled down below level 5) instead of a 10000 yd sweep, so they stop ping-ponging across the zone. [#441](https://github.com/Sagiroth/TortoiseBots/pull/441)
- Outgrown-zone travel keeps the long-range search, so bots still relocate when it's actually time to move on. [#441](https://github.com/Sagiroth/TortoiseBots/pull/441)
- Radius change is request-side only — no extra world scans, no added server cost. [#441](https://github.com/Sagiroth/TortoiseBots/pull/441)
- Plays nice with existing level/point-danger gates and the idle-starter fallback scan. Closes #424. [#441](https://github.com/Sagiroth/TortoiseBots/pull/441)

### Quests & Objectives

- Bots now give up on a quest objective after ~5 minutes stuck at the objective with zero progress, so they stop farming a broken spawn or unreachable target and get back to something productive [#442](https://github.com/Sagiroth/TortoiseBots/pull/442)
- The stall timer only ticks while the bot is actually at the objective area — travel time is never counted, so long cross-zone runs won't false-trigger an abandon [#442](https://github.com/Sagiroth/TortoiseBots/pull/442)
- The verdict is anchored per quest across all of its counters, so a multi-part objective won't be abandoned just because one counter is slow [#442](https://github.com/Sagiroth/TortoiseBots/pull/442)
- Stalled quests are parked for 30 minutes instead of blacklisted; other quests and all hand-ins keep running normally while it cools down [#442](https://github.com/Sagiroth/TortoiseBots/pull/442)
- Exploration and scripted event objectives (no counters) are exempt, so world/POI-driven quests can't get stuck in an abandon loop [#442](https://github.com/Sagiroth/TortoiseBots/pull/442)
- Emits a single `QuestObjectiveStalled` event per verdict, making it easy to spot problem quests in logs without event spam [#442](https://github.com/Sagiroth/TortoiseBots/pull/442)

### Observability & Engine

- Pool KPI report now tracks **zone-migration**, answering the "do bots actually move to level-appropriate zones?" question with hard numbers instead of vibes [#443](https://github.com/Sagiroth/TortoiseBots/pull/443)
- New **fit-share per level band**: counts bots sitting in a zone within `level -2..+5`, using the exact same rule as the travel gates, so the report matches in-game behavior instead of a parallel heuristic [#443](https://github.com/Sagiroth/TortoiseBots/pull/443)
- **Movement telemetry added**: bots that changed zone, top `from -> to` pairs, and highest zone reached — enough to spot bots ping-ponging between zones or camping a single one forever [#443](https://github.com/Sagiroth/TortoiseBots/pull/443)
- **Stuck-in-starter detection**: flags bots still in their start zone above level 8, plus left-start counts per start zone, so a broken travel path or gate gets caught per-zone rather than blamed on "bots are dumb" [#443](https://github.com/Sagiroth/TortoiseBots/pull/443)
- Fully **backward compatible** with old baselines — existing stored reports still parse, no re-baselining required [#443](https://github.com/Sagiroth/TortoiseBots/pull/443)
- Metrics documented in the observability guide so server operators can read the new fields without reverse-engineering the script [#443](https://github.com/Sagiroth/TortoiseBots/pull/443)

### Economy & Auctions
- The AH buyer no longer sits idle: bids only happen when a pool bot has actually reached an auctioneer on its own, using a bounded rotating pool scan instead of teleporting bots into place. [#444](https://github.com/Sagiroth/TortoiseBots/pull/444)
- Added organic auction demand: level 10+ pool bots with at least 5 gold spendable above reserves and an auction house on their own continent can take the normal AH travel errand (~5% of the pool per hour, 10-minute cooldown) and bid on arrival. [#444](https://github.com/Sagiroth/TortoiseBots/pull/444)

### Hire & Companions
- Hired bots now post their spec and spell summary exactly once in party chat after joining the group, instead of leaking (or silently dropping) the level-up automation echo [#445](https://github.com/Sagiroth/TortoiseBots/pull/445)
- Fixes the flaky "no message at all" case caused by stale master pointers, bots not yet grouped, or nothing new to report [#445](https://github.com/Sagiroth/TortoiseBots/pull/445)

### Professions & World Activities
- Pool bots now actually fish: when the travel table has no fishing spot, they fall back to the ported mod-playerbots open-water search, so idle bots get a rare side activity instead of standing around. [#446](https://github.com/Sagiroth/TortoiseBots/pull/446)
- Strict "levelling first" gating: at most one fishing session per hour (5 casts over 5 minutes), and never while the bot has real work — travel errands, quest turn-ins, selling, trainer, repair, money runs, or bags over 90% full. Fishing relevance sits below questing and grinding. [#446](https://github.com/Sagiroth/TortoiseBots/pull/446)
- Bounded server cost: coarse radial search (~88 probes), throttled per bot, with a 30-minute shared per-cell water cache and a 15-minute park on dry spots so bots don't re-scan dead ground. [#446](https://github.com/Sagiroth/TortoiseBots/pull/446)
- No gear churn: the weapon goes back on immediately after the session ends, so fishing never leaves bots running around unequipped. [#446](https://github.com/Sagiroth/TortoiseBots/pull/446)

### Travel & Taxi
- Pool bots actually fly now: flight is treated as a way to reach the travel destination the bot already chose, instead of occasionally picking a random leg. [#447](https://github.com/Sagiroth/TortoiseBots/pull/447)
- Flight is decided once when a new far travel target is set — trips of 1500+ yd, or a known direct taxi leg that saves 500+ yd — and only if the destination level falls within the bot's range (unknown levels refuse). Travel ticks just read the stored plan. [#447](https://github.com/Sagiroth/TortoiseBots/pull/447)
- Bots walk to the flight master first, board, pay the normal fare from their own gold (trainer reserve kept intact), then walk the last leg to the destination. [#447](https://github.com/Sagiroth/TortoiseBots/pull/447)
- Taxi node zones are now cached once at startup, so travel pathing stops re-resolving terrain/VMAP lookups on every tick. [#447](https://github.com/Sagiroth/TortoiseBots/pull/447)

### Travel & Zone Migration

- **Outgrown-zone leaves now actually leave.** The leave-errand grind search was picking the nearest active grind point — which was still in the zone the bot was vacating (Galwurth "left" Durotar three times and re-picked Durotar every time). The search now excludes the zone being left, so bots roll into a level-appropriate spot in the next zone instead of ping-ponging in place. Travel graph already had the hops (including Teldrassil → Darkshore via the Darnassus/Rut'theran portal), so no pathing work was needed. Capital-idle leaves unchanged. [#448](https://github.com/Sagiroth/TortoiseBots/pull/448)

### Starter Zones & World
- Below level 10, pool bots no longer default to the spirit healer just because of their death count — the corpse run is the default again, so they stop piling up in Brill, Goldshire, Kharanos, Razor Hill, Bloodhoof, and Dolanaar. Stalled corpse walks and long dead timers still trigger a rescue. [#449](https://github.com/Sagiroth/TortoiseBots/pull/449)
- Added an extra safety net around revive and rescue teleports so a bot that does get moved can't just drift off to the next town unchecked. [#449](https://github.com/Sagiroth/TortoiseBots/pull/449)

### Starter Zones & World

- Fresh level 1-4 pool bots no longer dead-end on quest search in their starting valley. The zone ceiling (Camp Narache 6, Dun Morogh 7, Durotar 8) used to reject every objective, leaving e.g. 57 of 64 tauren bots standing around at Camp Narache — quest objectives and quest givers now get the same level 1-4 exemption grinding already had. [#450](https://github.com/Sagiroth/TortoiseBots/pull/450)
- Safety rails intact: each quest's own level and danger checks still run, so this only unblocks *level-appropriate* errands. Player-owned and hired bots are untouched — no behavior change for anyone's actual characters. [#450](https://github.com/Sagiroth/TortoiseBots/pull/450)

### Travel & Leisure AI

- Pool bots now pick free-time activities with a single weighted roll — questing 60, grinding 15, town/inn 10, exploring 5 — bringing us in line with mod-playerbots behavior. [#451](https://github.com/Sagiroth/TortoiseBots/pull/451)
- Fixed the old fixed-priority system where grinding basically always won; camp and explore activities were effectively dead content. Expect bots to actually wander into towns and set up camps again. [#451](https://github.com/Sagiroth/TortoiseBots/pull/451)
- The roll is possibility-aware: no quest rolls while quests are parked, no camping below level 5. Bots won't commit to activities they can't currently perform. [#451](https://github.com/Sagiroth/TortoiseBots/pull/451)
- Empty roll results trigger an immediate re-roll, and grinding stays as the guaranteed fallback — bots never stand idle. [#451](https://github.com/Sagiroth/TortoiseBots/pull/451)

### Core Sync & Fixes
- Fixed a server crash shortly after startup when a pool bot searched for a fishing spot with an empty fish location dataset (not generated by default); the lookup now safely returns "no spot" and the bot falls back to nearby open water or other activities [#452](https://github.com/Sagiroth/TortoiseBots/pull/452)

### Core Sync & Fixes
- Fishing no longer takes the server down when no fish spot exists in the dataset — the bot now bails to open-water fallback or picks another activity instead of dereferencing an empty result. [#453](https://github.com/Sagiroth/TortoiseBots/pull/453)

### Movement & Pathing
- Fixed unwatched bots teleporting across continents: the walk-skip hop now only triggers on the bot's current map, so a Valley of Trials troll can't end up stranded in the Hinterlands. Cross-map path points fall back to normal walking. [#454](https://github.com/Sagiroth/TortoiseBots/pull/454)

### Starter Zones & World
- Level 1–4 pool bots now stay in their own zone and within 500 yd of their homebind, so Valley of Trials recruits stop wandering into The Barrens to get eaten by level-9 Dreadmaw Crocolisks [#455](https://github.com/Sagiroth/TortoiseBots/pull/455)
- Same zone-lock keeps Northshire bots out of Goldshire's wolf and Defias camps; all starter quests and mobs still fit inside the tighter radius [#455](https://github.com/Sagiroth/TortoiseBots/pull/455)
- Kills the old ~830 yd search radius fallback that ignored the area-level ceiling for exempted lowbies — fewer early deaths, fewer corpse runs [#455](https://github.com/Sagiroth/TortoiseBots/pull/455)

## 2026-10-02

### Levelling & Progression

- Random bots now actually progress: median kills per bot-hour jumped from 5 to 23 and XP per bot-hour from ~611 to ~3,400 on a fresh level-1 pool. [#384](https://github.com/Sagiroth/TortoiseBots/pull/384)
- "Dead weight" bots are largely gone — the share of bots logging zero kills in a 90-minute window dropped from 32% to 2-13%. [#384](https://github.com/Sagiroth/TortoiseBots/pull/384)
- Bot pools no longer stall at level 7-9 in starter gear after 18 hours; the old ~10x slowdown versus expected levelling pace is fixed. [#384](https://github.com/Sagiroth/TortoiseBots/pull/384)

### Bot Behaviour

- Bots now level, quest, vendor and gather more like real players instead of idling or grinding inefficiently. [#384](https://github.com/Sagiroth/TortoiseBots/pull/384)
- Tuning was driven by seven full measure-fix-reset cycles on fresh level-1 pools, so the numbers above are repeatable, not a lucky run. [#384](https://github.com/Sagiroth/TortoiseBots/pull/384)

---

### Observability & Dashboard

- New per-bot **Activity tab** streams each bot's progress live in the dashboard, so you can track leveling without tailing logs or querying the DB by hand [#385](https://github.com/Sagiroth/TortoiseBots/pull/385)
- Dashboard counters now report trainer visits and spells learned, vendor sales, repairs, quest hand-in trips, give-ups, gathering and profession skill-ups, and AH listings per bot [#385](https://github.com/Sagiroth/TortoiseBots/pull/385)

### Quests & Loot Feeds

- Quest events are surfaced with names — accepted, completed, and handed in — making it easy to spot stuck bots or stalled quest chains [#385](https://github.com/Sagiroth/TortoiseBots/pull/385)
- Notable loot is logged with item name, quality, and source, so upgrades and lucky drops are visible at a glance [#385](https://github.com/Sagiroth/TortoiseBots/pull/385)
- Level-ups appear as first-class events in the activity stream instead of being inferred from stat deltas [#385](https://github.com/Sagiroth/TortoiseBots/pull/385)

### Gear & Progress Tracking

- Gear item level is now tracked and displayed per bot, giving a quick read on whether a bot is actually gearing up as it levels [#385](https://github.com/Sagiroth/TortoiseBots/pull/385)

### Dashboard Fixes

- Dashboard numbers were cross-checked against the database and the discrepancies found during that audit are fixed, so what you see on screen matches reality [#385](https://github.com/Sagiroth/TortoiseBots/pull/385)

### Death & Resurrection
- Bots now revive at the graveyard **closest to where they actually died**, instead of wherever the phantom `(0,0,0)` null travel target pointed (the middle of Alterac). No more level-5 bots waking up in Hillsbrad or Tarren Mill after an Elwynn death. [#392](https://github.com/Sagiroth/TortoiseBots/pull/392)
- Kills the 70 % of spirit-healer revives that landed 500+ yd away (median 1,229 yd, worst case 8,854 yd) — corpses, graveyards and spirit healers are consistent again. Ghost runs stop being a cross-continent field trip. [#392](https://github.com/Sagiroth/TortoiseBots/pull/392)

### Companions & Hires
- Raid hires are no longer gated by subgroup: arrival, reunite and master-left-group paths now use same-raid membership instead of same-subgroup. The 5th, 6th, 9th hire actually joins you instead of standing around ungrouped. [#390](https://github.com/Sagiroth/TortoiseBots/pull/390)
- 5-man parties behave exactly as before — the two checks are identical below raid size. [#390](https://github.com/Sagiroth/TortoiseBots/pull/390)

### Travel & Instances
- Companions with a real master now skip autonomous travel-target selection while on an instance map. Cheap map check before any search, so no more seconds-long stalls on the bot-update pass when a full raid of companions zones into a dungeon. [#391](https://github.com/Sagiroth/TortoiseBots/pull/391)
- Open-world travel is untouched — following the master, explicit travel commands and normal outdoor behavior all still work. [#391](https://github.com/Sagiroth/TortoiseBots/pull/391)

### Hiring & Raids
- Recruiter spec selection now applies to every class, not just druids — one shared policy table drives both the gossip menu and the talent path, so a requested spec actually arrives as requested ([#386](https://github.com/Sagiroth/TortoiseBots/issues/386)). [#395](https://github.com/Sagiroth/TortoiseBots/pull/395)
- Premade specs are cropped to the bot's current level before validation, preventing out-of-range talent requests from silently falling back to the wrong build. [#395](https://github.com/Sagiroth/TortoiseBots/pull/395)

### Dungeon Combat & AI
- Explicit ranged pulls inside dungeons now point-move the bot to a valid firing position instead of yanking from wherever they happen to stand. [#395](https://github.com/Sagiroth/TortoiseBots/pull/395)
- Pull positioning validates reachability before committing, so bots no longer path into walls or unreachable ledges trying to set up a shot. [#395](https://github.com/Sagiroth/TortoiseBots/pull/395)

### Quests & World
- Quest takers that bots cannot physically reach are now handled gracefully, cutting down on stuck bots and wasted pathing loops in crowded zones. [#395](https://github.com/Sagiroth/TortoiseBots/pull/395)

### Roleplay & Idle Behavior
- Idle roleplay behavior has been tightened up so bots stop spamming emotes or looping chatter when they should be chilling. [#395](https://github.com/Sagiroth/TortoiseBots/pull/395)

### Dashboard & Observability
- Follow-up fixes rolled in for the dashboard activity panels introduced in the previous release, keeping bot activity reporting in sync with actual behavior. [#395](https://github.com/Sagiroth/TortoiseBots/pull/395)

### Combat & AI
- Autonomous pool bots no longer initiate combat with grey (no-XP) creatures — grey candidates are skipped outright instead of slipping through the grind target lean. On live realms this was bleeding ~17% of all attack orders into zero-reward mobs, mostly from level 6-8 bots. [#397](https://github.com/Sagiroth/TortoiseBots/pull/397)
- Bots still defend themselves: if a grey mob attacks first, the bot fights back. Owned (non-pool) bots and explicit player-issued orders are untouched. [#397](https://github.com/Sagiroth/TortoiseBots/pull/397)

### Quests & Progression
- Quest-giver trips now skip quests that are grey for the bot, and skip givers sitting in areas the bot has outgrown (area level below the grind band floor), with capitals exempt. No more wasted cross-zone walks for zero-XP turn-ins. [#397](https://github.com/Sagiroth/TortoiseBots/pull/397)
- Quest hand-ins are unchanged — only pickup routing is filtered. Completed quests still get turned in as before. [#397](https://github.com/Sagiroth/TortoiseBots/pull/397)

### Behavior & Idle Handling
- Fixed idle beginner bots so freshly spawned low-level bots stop standing around and get into the grind loop. [#397](https://github.com/Sagiroth/TortoiseBots/pull/397)

### Dashboard & UI
- Fixed dashboard navigation issues that broke moving between dashboard views. [#397](https://github.com/Sagiroth/TortoiseBots/pull/397)

## 2026-10-01

### Party Buffs & Hire Cleanup

- Group buffs now actually go out: Prayer of Fortitude, Arcane Brilliance, Gift of the Wild, and the Prayers pick a party member missing *both* the group aura and the single-target buff, instead of picking someone who already has Power Word: Fortitude and then bailing — which was starving the group version for the entire session [#380](https://github.com/Sagiroth/TortoiseBots/pull/380)
- Hired priests stop hemorrhaging mana on redundant single-target buffs; the group version wins the cast decision when someone needs it [#380](https://github.com/Sagiroth/TortoiseBots/pull/380)
- Hires are now properly cleaned up when the player leaves the group — no more orphaned priest bots loitering in the world [#380](https://github.com/Sagiroth/TortoiseBots/pull/380)

---

## 2026-09-30

### Loot & Grinding

- Bots now loot their own kills before pulling the next mob, instead of wandering off and leaving roughly two thirds of corpses to rot or expire — expect a big jump in collected drops on fresh pools [#355](https://github.com/Sagiroth/TortoiseBots/pull/355)
- A bot won't start walking to a corpse while an add is charging it; loot waits until the fight is actually over [#355](https://github.com/Sagiroth/TortoiseBots/pull/355)
- Kept intact from #340: shared 3D loot range checks, skinning after items, and group loot rules [#355](https://github.com/Sagiroth/TortoiseBots/pull/355)

### Observability & Engine

- `BotDeath` is now a real event in `bot_events.csv` with killer, level, and position, so deaths are no longer mixed in with respawns of stuck bots [#356](https://github.com/Sagiroth/TortoiseBots/pull/356)
- `deaths.csv` is append-only, so a pool reset no longer nukes your death history mid-session [#356](https://github.com/Sagiroth/TortoiseBots/pull/356)
- `AutoSetTalentsAction` is only logged when talents actually change, killing the spam of no-op entries on every sub-level-10 level-up [#356](https://github.com/Sagiroth/TortoiseBots/pull/356)
- Bot behaviour itself is unchanged — this is purely cleaner data for pool analysis [#356](https://github.com/Sagiroth/TortoiseBots/pull/356)

---

### Combat & AI
- Low-level masterless bots (under level 10) now cap their grind targets at +1 level instead of +4, so they stop picking fights they can't finish — melee kill rates were sitting at 1-4% versus 23% for mages. Quest mobs stay exempt from the gate. [#352](https://github.com/Sagiroth/TortoiseBots/pull/352)
- Auto-attack is now armed whenever a bot has no ranged option, not just for melee specs. Low-level druids and elemental shamans without a wand or bow can finally swing back instead of standing there. [#352](https://github.com/Sagiroth/TortoiseBots/pull/352)

### Vendor & Economy
- Level 1-4 bots can now visit a camp vendor to sell starter loot, with low-level-friendly thresholds. A fresh pool of 500 bots was averaging ~2.7 copper each after two hours because lowbies never vendored anything. [#357](https://github.com/Sagiroth/TortoiseBots/pull/357)
- The batch vendor-trip rule no longer waits on a missing spell rank — a bag worth the trip is enough to trigger the run. The broader RPG town guard is untouched, so bots still don't wander into cities. [#357](https://github.com/Sagiroth/TortoiseBots/pull/357)

### Unstuck & Recovery
- Hearthstone rescue now only fires when the bot is genuinely far from homebind (`AiPlayerbot.UnstuckHearthMinDistance`, default 300 yd); closer in, the unstuck chain repops instead. Previously bots burned ~480 hearthstones in 100 minutes, 95% of them within 300 yd of where they started. [#353](https://github.com/Sagiroth/TortoiseBots/pull/353)
- `move long stuck` and `combat long stuck` now require 15 minutes of zero XP *and* zero money before the "no movement for 10 minutes" branch can trigger — no more false positives from a bot that's just slowly grinding. [#353](https://github.com/Sagiroth/TortoiseBots/pull/353)

### Observability & Engine

- Hunters now write two new `bot_events.csv` events: ranged auto-attack starts and ranged↔melee kit switches — so you can finally confirm whether they're actually shooting, since ammo counts lie (pool bots refill). [#358](https://github.com/Sagiroth/TortoiseBots/pull/358)
- Purely telemetry: no hunter behavior changes ship with this. The ranged-kit tweaks from the test run were dropped after hunters led the death charts, so consider this a measurement tool first. [#358](https://github.com/Sagiroth/TortoiseBots/pull/358)

### Quests & Progression
- Hand-in trips to a quest giver now outrank *starting* a fight, so bots stop getting distracted mid-delivery and actually finish the quests they've already completed — defending against mobs that attack first is untouched [#354](https://github.com/Sagiroth/TortoiseBots/pull/354)
- Upkeep-bot hand-in latch dropped from 2 to 1, matching the real backlog of a single finished quest and cutting the stale-quest pileup [#354](https://github.com/Sagiroth/TortoiseBots/pull/354)

### Observability & Engine
- New `bot_events.csv` entries `QuestCompleted` and `QuestRewarded` make it possible to audit quest throughput and confirm hand-ins actually land instead of guessing from accepted-quest counts [#354](https://github.com/Sagiroth/TortoiseBots/pull/354)

### Observability & Engine

- Added a read-only `EvadeProbe` diagnostic that samples each bot's current target every 2s (deduped to one row per bot per 30s) and logs to `bot_events.csv` only on suspicious behavior: mob in evade mode, unreachable target, or HP ticking up +15 while engaged — capturing mob name/level/HP, distance, height delta, line of sight, and the bot's move/cast state to help track down the unkillable/regen mob reports [#360](https://github.com/Sagiroth/TortoiseBots/pull/360)
- Purely observational: no gameplay or AI behavior changes, so it's safe to leave enabled on live servers while chasing the regen bug [#360](https://github.com/Sagiroth/TortoiseBots/pull/360)

### Combat & AI
- Hired priests no longer re-attempt their upkeep self-buff every tick, so idle healers stop bleeding mana and looping buff → drink → buff. The retry cadence is now documented for future tuning. [#363](https://github.com/Sagiroth/TortoiseBots/pull/363)

### Bot Pool & Availability
- Bots that missed their scheduled return after the 6-hour timed logout are no longer parked on a stale 1-hour hold — timed logouts are handled separately from quick logouts, so a lapsed hold can't keep a bot out of the world. Recovers roughly 105 bots (~6.7% of pool capacity), mostly levels 1–4. [#362](https://github.com/Sagiroth/TortoiseBots/pull/362)

### Hunter & Pets

- Pool hunters now get their pet at level 10 instead of level 1 — seeded pets below 10 are dropped at login, and a fresh, level-fitting pet is granted on reaching 10. Player-owned hunters, hired companions, and party bots are untouched. [#366](https://github.com/Sagiroth/TortoiseBots/pull/366)
- Hunters below level 10 no longer lose their ranged kit permanently: the "switch to melee" path is no longer a dead end below 10, and hunters keep a rule to preserve distance with bow or gun in hand. [#368](https://github.com/Sagiroth/TortoiseBots/pull/368)
- A hunter stuck in Auto Shot's 8-yard dead zone with a mob on it now steps back out instead of standing still and whiffing shots. [#368](https://github.com/Sagiroth/TortoiseBots/pull/368)

### Combat & Targeting

- Bots no longer pick, order, or hold onto creatures stuck in evade mode — the mob that walks home invulnerable and regenerating is skipped, so fights actually end in kills instead of endless poking. [#364](https://github.com/Sagiroth/TortoiseBots/pull/364)

### Movement & Grinding

- Grind orders now actually walk the bot to its target: the always-on `StopMoving()` on `attack anything` was fixed, `reach` actions get engine time, and `select new target` no longer wipes the target every tick. This fixes the 53% of pool bots frozen in place and the 3–6% kill rate on leveling. [#361](https://github.com/Sagiroth/TortoiseBots/pull/361)
- Follow-up cleanup on the reach flow: the grind order no longer overrides reach's movement wait (kills a tick of jitter), and the sub-10 leveling druid's out-of-melee trigger now matches the registered name, so it actually gets its reach in combat. [#365](https://github.com/Sagiroth/TortoiseBots/pull/365)

### Chat & Social

- Fresh pool bots stop spamming greetings and emotes at each other in starting zones. Real players still get greetings (limited), and hire/summon/reunite greetings for a real master are unchanged — `AiPlayerbot.RandomBotGreet` defaults to off for the pool. [#367](https://github.com/Sagiroth/TortoiseBots/pull/367)

### Combat & AI
- Pool bots (level 1-3) actually find grind targets again instead of idling: the XP pre-check was asking the core about mobs nobody had hit yet, so almost every untouched mob read as "no XP" and got skipped. It now uses the core's grey-level test (`Player::IsHonorOrXPTarget`), matching mod-playerbots behavior. [#370](https://github.com/Sagiroth/TortoiseBots/pull/370)
- Leash handling for masterless bots cleaned up, so bots stop breaking off fights and wandering instead of committing to a kill. [#370](https://github.com/Sagiroth/TortoiseBots/pull/370)

### Core Sync & Fixes
- Newly created bots no longer get stuck in their race intro on first login. While the intro plays the server won't let creatures target that character, so bots would hit a mob, the mob would find nobody to fight, evade, and heal to full. The module now ends the intro on world entry — no core change required. [#369](https://github.com/Sagiroth/TortoiseBots/pull/369)
- Also covers the second intro path via camera game objects (RPG use / cinematics), so bots don't silently re-enter an untargetable state afterward. [#369](https://github.com/Sagiroth/TortoiseBots/pull/369)

### Chat & Bot Behavior
- Pool bots no longer narrate their own autonomous actions in public or party chat; travel lines, "Selling [item]" spam, and bot-to-bot trade/enchant chatter are now silent unless a real player prompted them [#372](https://github.com/Sagiroth/TortoiseBots/pull/372)
- Direct replies to player commands still work as before — only the unprompted narration is suppressed, so nearby players stop getting spammed in `/say` [#372](https://github.com/Sagiroth/TortoiseBots/pull/372)
- `AiPlayerbot.BroadcastChanceSuggestSell` now defaults to `0` (down from `300`), killing the "WTS" broadcasts out of the box. Operators who actually want that noise can raise the value in config [#372](https://github.com/Sagiroth/TortoiseBots/pull/372)

### Core Sync & Fixes
- Pool bots no longer invite each other into bot-only groups, eliminating ~110 phantom groups per 500-bot pool and the travel loops and idle followers they caused [#371](https://github.com/Sagiroth/TortoiseBots/pull/371)
- `AiPlayerbot.RandomBotGroupNearby` now defaults to `0` (matching mod-playerbots); set it to `1` to restore the old bot-to-bot grouping behavior [#371](https://github.com/Sagiroth/TortoiseBots/pull/371)

### Grouping & Invites
- Real-player invites (`RandomBotInvitePlayer`) are unaffected — bots still accept and group with humans regardless of the new default [#371](https://github.com/Sagiroth/TortoiseBots/pull/371)
- Groups containing a real player continue to function normally; only bot-led, bot-only groups are suppressed [#371](https://github.com/Sagiroth/TortoiseBots/pull/371)

### Starter Zones & World

- New random bots now roll a race from the least-populated level-1 starting area instead of defaulting into the same handful of zones, spreading the pool evenly across all six starts. [#373](https://github.com/Sagiroth/TortoiseBots/pull/373)
- Fixes the crowded-valley problem after a pool reset: Valley of Trials was pulling ~147 bots, Northshire ~117 and Coldridge ~106 while Deathknell, Camp Narache and Shadowglen sat at ~40 each — the packed zones ran out of mobs and bots just stood around. [#373](https://github.com/Sagiroth/TortoiseBots/pull/373)
- Goblins and high elves moved off their isolated start zones into the general starting-area rotation, so they no longer skew the distribution. [#373](https://github.com/Sagiroth/TortoiseBots/pull/373)
- Population is counted once per creation batch, so bulk pool resets stay balanced rather than front-loading one zone. [#373](https://github.com/Sagiroth/TortoiseBots/pull/373)
- New config `AiPlayerbot.RandomBotEvenStartZones` (default `1`) toggles the behavior; set it to `0` to restore the old random-race picks. [#373](https://github.com/Sagiroth/TortoiseBots/pull/373)

### Combat & AI
- Level 1–3 bots no longer park at their spawn hub waiting for respawns. The Grind travel purpose now actually fires for beginners, so a bot with nothing in range walks to a real grind spot instead of idling (the native equivalent of mod-playerbots' `GO_GRIND`). [#374](https://github.com/Sagiroth/TortoiseBots/pull/374)
- Wild XP-granting critter-type beasts (deer, cows) are no longer skipped by the critter filter — the check now uses the grey-level test, so lowbies get XP from the wildlife around starter hubs instead of ignoring free kills. [#374](https://github.com/Sagiroth/TortoiseBots/pull/374)

### Starter Zones & World
- Fixes Coldridge Valley's dead-end grind loop: wolves and troggs sit 185–240 yd out, well beyond the 60 yd scan, so 86% of grind searches previously found nothing and the only reachable mobs were two rabbits. Bots now walk out to the field. [#374](https://github.com/Sagiroth/TortoiseBots/pull/374)

### Combat & AI
- Bots no longer lock onto mobs they physically can't reach — grind target selection now refuses any creature the core has flagged as unreachable for that bot, and drops a held target the moment it gets flagged. [#375](https://github.com/Sagiroth/TortoiseBots/pull/375)
- Kills the evade spiral around Fargodeep mine shafts, embankments, and similar geometry: the core flags a creature after 3 s but doesn't send it home until 24 s, and bots were burning that entire window chasing nothing (56 of 60 evade samples showed the "not reachable" flag with the mob still aggroed). [#375](https://github.com/Sagiroth/TortoiseBots/pull/375)
- Verified via `tools/verify_all.sh` plus runtime testing on the combined build — module builds clean. [#375](https://github.com/Sagiroth/TortoiseBots/pull/375)

### Observability & Engine
- Per-bot XP bar (current / next level) plus XP/h now shows up in the roster, the bot drawer, and the Armory profile header — you can finally see who's actually levelling [#376](https://github.com/Sagiroth/TortoiseBots/pull/376)
- New grinding panel tracks bots gaining XP, median XP/h, and kills/min, with adaptive level bands so the numbers stay meaningful across the whole level range [#376](https://github.com/Sagiroth/TortoiseBots/pull/376)
- Server panel exposes the effective rates and TortoiseBots runtime info in one place for quick sanity checks [#376](https://github.com/Sagiroth/TortoiseBots/pull/376)

### Activity & Idle Tracking
- Activity census rewritten: "idle" now means genuinely doing nothing for a while, while looting, eating, casting, and standing at a travel target all count as busy [#376](https://github.com/Sagiroth/TortoiseBots/pull/376)
- Fixes the old single 2s sample that flagged every pause as idle — dashboards showed ~50-60% idle when only ~6% of bots actually were [#376](https://github.com/Sagiroth/TortoiseBots/pull/376)

### Docs & Guides
- Guides swept up to match the wave of merges from #369–#376, so the docs no longer contradict live behavior. [#377](https://github.com/Sagiroth/TortoiseBots/pull/377)
- Mechanics and living-world guides now cover first-login intros, the grind XP check and critter rule, unreachable target handling, beginners walking to the grind field, no pool groups, chatter silence, and even start zones. [#377](https://github.com/Sagiroth/TortoiseBots/pull/377)
- Configuration guide lists every new or changed key with its default, plus the recommended `DynamicRespawn.MaxReductionRate = 0.75` and `MinRespawnTime = 15` for bot-heavy `mangosd.conf` setups (core ships 0.25 / 25) — tune these if respawns feel starved. [#377](https://github.com/Sagiroth/TortoiseBots/pull/377)
- Observability guide documents the combat / moving / busy / idle states the engine exposes, making it easier to read bot activity at a glance. [#377](https://github.com/Sagiroth/TortoiseBots/pull/377)

## 2026-09-29

### Combat & AI
- Automatic bag audit (`equip upgrades`) is disabled again; it was causing mobs to reset to full HP and stop fighting back when triggered by the maintenance strategy or on level-up. [#337](https://github.com/Sagiroth/TortoiseBots/pull/337)
- The rest of the upgrade work from #335 (slot selection, weapon spec transition, dual slots) stays intact, and equipping from loot, vendors, and quests continues to work as before. [#337](https://github.com/Sagiroth/TortoiseBots/pull/337)
- Bots no longer run a periodic bag audit, so expect normal, non-resetting mob behavior in combat again. [#337](https://github.com/Sagiroth/TortoiseBots/pull/337)

---

### Commands & Summoning

- `.bot summon <Name>` no longer trips over the GM-only `NonGmFreeSummon = 0` gate — normal players can once again summon bots they control (own account or master-bound hired companions). The command already scoped itself to bots the requester owns, so the check only ever blocked legit use. [#338](https://github.com/Sagiroth/TortoiseBots/pull/338)
- Fixed the addon's Summon button spitting out "Summoning is restricted to GameMasters" for regular players. [#338](https://github.com/Sagiroth/TortoiseBots/pull/338)
- `NonGmFreeSummon` still works as intended for the in-chat summon order (instant teleport vs. walk / meeting stone) — its config description and docs now actually say that instead of implying a GM lock. [#338](https://github.com/Sagiroth/TortoiseBots/pull/338)

### Economy & Trainer Progression

- Bots no longer hoard a phantom repair reserve — they repair for free, so the freed-up coin now goes toward class spells instead of stalling the 1-60 loop around level 5-8. [#339](https://github.com/Sagiroth/TortoiseBots/pull/339)
- Vendor trips now trigger when the next trainer spell is actually unaffordable, and only if the sellable stock covers the shortfall or the bot is genuinely buried in junk (8+ items, 60%+ bag usage). No more cross-zone pilgrimages for a single grey item. [#339](https://github.com/Sagiroth/TortoiseBots/pull/339)
- Junk destroy order tightened up for critically full bags, keeping inventory flowing so bots keep training and questing instead of standing around over-encumbered. [#339](https://github.com/Sagiroth/TortoiseBots/pull/339)

### Loot & Economy
- Bots now loot every corpse they're entitled to before pulling the next mob, fixing the case where they walked away from ~78% of their own kills. Expect noticeably better money, cloth, and vendor trash income in group play. [#340](https://github.com/Sagiroth/TortoiseBots/pull/340)
- Loot range now uses a single shared rule for the range check, the approach, and the open — previously a 2D check vs. a 3D open could deadlock a bot next to a corpse on a slope, retrying forever. Bots no longer stall on uneven terrain. [#340](https://github.com/Sagiroth/TortoiseBots/pull/340)
- Skinning order is now items first, then skin the beast corpse, so bots don't skip lootable drops in favor of leather. [#340](https://github.com/Sagiroth/TortoiseBots/pull/340)
- New `LootMoney` event line records the coin taken, giving server operators and log-parsers a clean signal for bot economy tracking. [#340](https://github.com/Sagiroth/TortoiseBots/pull/340)

### Inventory & Bags

- Hunters now keep a bag slot free for a quiver, and seeded ammo goes there instead of being shoved into an occupied slot — the core no longer eats the quiver on login. [#341](https://github.com/Sagiroth/TortoiseBots/pull/341)
- Quiver upgrades across tiers actually work now: the slot lookup was returning nothing once a quiver was worn, so a better quiver was never equipped. It swaps correctly even with ammo inside. [#341](https://github.com/Sagiroth/TortoiseBots/pull/341)
- Warlocks get their first soul bag reliably; a bigger soul bag replaces a smaller one, and the smallest plain bag is the one evicted. Soul bags never steal a hunter's quiver slot. [#341](https://github.com/Sagiroth/TortoiseBots/pull/341)
- Bag swaps read live container state instead of stale cached data, cutting down destroyed or duplicated items during gear and bag churn. [#341](https://github.com/Sagiroth/TortoiseBots/pull/341)

### Combat & AI
- Bag audits (`equip upgrades`) no longer fire mid-combat — bots were rewriting worn gear when the level-up packet arrived on a killing blow, which made nearby mobs reset to full HP and drop the fight. [#342](https://github.com/Sagiroth/TortoiseBots/pull/342)
- Upgrades now defer to the next out-of-combat audit cycle (~5 min periodic pass), so level-ups still get equipped without breaking active pulls. [#342](https://github.com/Sagiroth/TortoiseBots/pull/342)

### Combat & AI
- Bots no longer score profession tools (Mining Pick, Blacksmith Hammer, Skinning Knife, Arclight Spanner, Woodcutting Axe) as weapon upgrades — no more random picks in hand or chat spam announcing it. [#343](https://github.com/Sagiroth/TortoiseBots/pull/343)
- Misc-class weapons are now skipped by equip scoring entirely; the tools they actually need are still bought and kept by the skill rules. [#343](https://github.com/Sagiroth/TortoiseBots/pull/343)
- Quest items that happen to be misc weapons still count as quest items, so quest logic is untouched. [#343](https://github.com/Sagiroth/TortoiseBots/pull/343)

### Combat & AI
- Bots with a bow, gun, crossbow, or thrown weapon now actually know how to shoot — the core's weapon-skill reward pass is re-run for ranged skills (Bows, Guns, Crossbows, Thrown, Wands) during skill seeding, so `Shoot Bow`/`Shoot Gun`/`Shoot Crossbow`/`Throw` are properly learned and `.bot action pull` / `pullback` ranged pulls and the ranged fallback work as intended for both fresh pool bots and hired companions. [#344](https://github.com/Sagiroth/TortoiseBots/pull/344)

### Performance & Tick
- Bots owned by a real player, plus master-bound bots (hired companions, dungeon party), are now updated first every tick with no budget — no more 3-5 s command/movement lag even with a 500-bot random pool. [#345](https://github.com/Sagiroth/TortoiseBots/pull/345)
- The random pool now ticks in round-robin with a cursor persisted between ticks, so work is spread across passes instead of bloating a single world tick to 1-3 s. [#345](https://github.com/Sagiroth/TortoiseBots/pull/345)
- Both priority checks are O(1), so the fast path for player-relevant bots adds effectively zero overhead at scale. [#345](https://github.com/Sagiroth/TortoiseBots/pull/345)

### Combat & AI
- `.bot action pull` and `.bot action pullback` now actually work: tanks shoot to pull, hold position, and hand the mob back to the group instead of cancelling the shot and standing around — [#346](https://github.com/Sagiroth/TortoiseBots/pull/346)
- Fixed pulled mobs being dropped every tick (target was only set as `current target`, not `attack target`, so the combat engine cleared it and re-issued the pull loop) — [#346](https://github.com/Sagiroth/TortoiseBots/pull/346)
- Resolved leftover `stay` state after a pull fight ends; bots now correctly return to `follow` — [#346](https://github.com/Sagiroth/TortoiseBots/pull/346)

### Core Sync & Fixes
- Bots are now classified by **real ownership** instead of their `PlayerMaster` lease, so bot-group leaders no longer get counted as "a player's bot." [#347](https://github.com/Sagiroth/TortoiseBots/pull/347)
- The unbudgeted first pass now only runs bots actually owned by a real player — the rest of the pool stays in the **budgeted random pool** and gets throttled when the world tick is overloaded. [#347](https://github.com/Sagiroth/TortoiseBots/pull/347)
- Fixes a live-pool regression where ~212 bots (every non-leader bot in an all-bot group) bypassed tick budgeting on a 500-bot pool. [#347](https://github.com/Sagiroth/TortoiseBots/pull/347)

### Observability & Engine
- The `BOTPERF` line no longer misreports `playerBots` counts — it now reflects bots with a genuine player master, making tick-budget diagnostics trustworthy again. [#347](https://github.com/Sagiroth/TortoiseBots/pull/347)

### Combat & AI
- Bots now actually learn the abilities their skills unlock — Dual Wield, Shoot Bow, Block — instead of just holding the skill and never using it. Live data showed 22 of 24 Dual Wield bots had no spell, and 0 of 244 Bows bots could shoot. [#348](https://github.com/Sagiroth/TortoiseBots/pull/348)
- `EnsureSkillRewardedSpells` walks the skill list and teaches the matching spell, so the #344 skill re-issue now lands as a usable ability rather than a dead flag. [#348](https://github.com/Sagiroth/TortoiseBots/pull/348)
- Ranged bots are back on the field: bows finally fire, which also unblocks debugging for other weapon-skill gaps. [#348](https://github.com/Sagiroth/TortoiseBots/pull/348)

### Bots & Inventories
- Tanks stop dropping their starter shield and running around with an empty off hand — off-hand gear is preserved instead of being silently lost. [#348](https://github.com/Sagiroth/TortoiseBots/pull/348)
- Second weapons stay equipped, so Dual Wield bots look and fight like the spec they were rolled with. [#348](https://github.com/Sagiroth/TortoiseBots/pull/348)

### Hire System & Companions

- Hired companions are now a temporary service: when the hire ends, the character is deleted instead of hearting home and joining the roaming pool. [#349](https://github.com/Sagiroth/TortoiseBots/pull/349)
- Hires persist only while the hiring player is online plus a disconnect grace window (`HireDisconnectGracePeriod`, default 300s) before the character is wiped. [#349](https://github.com/Sagiroth/TortoiseBots/pull/349)
- Every termination path deletes the hire: kicked from group or group disbanded (instant), master offline past the grace period, and `.bot remove` / `.bot logout` on a companion. [#349](https://github.com/Sagiroth/TortoiseBots/pull/349)
- Re-hiring is a fresh paid transaction — no more level-and-gear hand-me-downs polluting a fresh realm's organic population. [#349](https://github.com/Sagiroth/TortoiseBots/pull/349)

### Skills & Progression
- Dual Wield is now gated by class level (warrior 10, rogue 10, hunter 20) instead of leaking into every low-level bot's ability pass — no more level-5 warriors and hunters swinging off-hands they shouldn't have. [#350](https://github.com/Sagiroth/TortoiseBots/pull/350)
- Root cause was the world DB overriding the skill's class requirement (`skill_race_class_info_mod` id 132), so the game data's "requirement" was effectively no gate; bots now follow trainer data. [#350](https://github.com/Sagiroth/TortoiseBots/pull/350)

### Core Sync & Fixes
- Shaman bots (and any other class outside warrior/rogue/hunter) now lose the illegally-seeded Dual Wield spell, skill, and session flag at login — no more bogus level-10 dual wield from the old hard-coded seeding. [#351](https://github.com/Sagiroth/TortoiseBots/pull/351)
- Cleanup is flag-and-skill only: equipped items are left untouched, so existing bots keep their gear without manual intervention. [#351](https://github.com/Sagiroth/TortoiseBots/pull/351)

### Combat & AI
- Grinding bots below level 10 now take on mobs at most one level above their own instead of up to four — orders against +2 to +4 mobs were 15% of all orders in a measured level-1 pool and converted about 1% of the time (0.3% for melee) against 16-28% at the bot's own level. Quest objectives and bots following a real player are exempt.
- A bot that already holds a creature in evade mode — unkillable, every point of damage against it is dropped by the core — now drops it as an invalid target instead of standing on it silently until the creature resets.
- Bots whose talent spec is "ranged" but which carry no ranged weapon (pre-10 druid on Wrath, elemental shaman) now arm their auto-attack, so they swing once the mob closes instead of standing on it with nothing to cast: druid averaged 0.008 and shaman 0.013 kills per attack order against 0.035-0.079 for warrior/paladin/rogue.
- New `GrindTargetRepeat` row in `bot_events.csv` when a bot orders a second attack on the same mob within 60 s — mob guid/entry, distance, height difference, combat state, victim match and the repeat count, the trace that separates "never arrived" from "arrived and stuck" in the grind loop. Throttled: from the third repeat, at most one row per minute per bot.

## 2026-09-28

### Database & Migrations

- Rolled the Mercenary Hire recruiter migration (`20260918120000_world.sql`) back to its exact shipped content (SHA1 `46221D0C71DDFDF72C4CAF2927155A067D3F1049`), so hash-checking migration runners stop aborting with *"previously applied migration changed"*. [#327](https://github.com/Sagiroth/TortoiseBots/pull/327)
- Fixed recruiter spawn guids (4000000–4000066) and the opposite-gender recruiter adjustments now land via a new migration instead of a retro-edited one, so realms that only track filenames no longer silently keep stale guids. [#327](https://github.com/Sagiroth/TortoiseBots/pull/327)

---

### Server Performance & Engine

- Bot travel planning no longer re-sorts its entire search queue on every step — it now uses a proper priority queue, so route search stays cheap as bot counts climb [#328](https://github.com/Sagiroth/TortoiseBots/pull/328)
- Nearby travel node sorting computes each distance once instead of on every comparison, which previously meant a portal lookup plus a memory allocation per check [#328](https://github.com/Sagiroth/TortoiseBots/pull/328)
- Cross-map (portal) distance checks are allocation-free now, removing constant allocator churn from the main thread [#328](https://github.com/Sagiroth/TortoiseBots/pull/328)
- Big win for high-population realms: with 500 bots online, travel planning was eating roughly two thirds of main-thread time with world ticks of 1.2–3.5s — that headroom now goes back to the server [#328](https://github.com/Sagiroth/TortoiseBots/pull/328)

### Performance & Travel Planning

- Bots now remember the verdict for a travel destination for 5 minutes, instead of re-running a full route survivability search on every single pick — [#329](https://github.com/Sagiroth/TortoiseBots/pull/329)
- Rejected or unreachable destinations were the most expensive case and got searched over and over; that redundant work is now gone — [#329](https://github.com/Sagiroth/TortoiseBots/pull/329)
- On a 500-bot test realm this path was still eating roughly three quarters of main-thread time even after the route-search speedups in #328, so expect a large drop in server tick cost with big bot populations — [#329](https://github.com/Sagiroth/TortoiseBots/pull/329)
- Verdicts expire after 5 minutes, so a bot re-checks eventually rather than being stuck with a stale route choice forever — [#329](https://github.com/Sagiroth/TortoiseBots/pull/329)

### Combat & AI
- Bots that can't find a destination for a travel goal now park that goal for a minute instead of re-running a full destination search every tick, so they actually get on with their other plans [#330](https://github.com/Sagiroth/TortoiseBots/pull/330)
- Fixed the goal cooldown clearing itself immediately and a key mismatch that left quest travel cooldowns disabled entirely — no more low-level bots endlessly "skinning with nowhere to go" [#330](https://github.com/Sagiroth/TortoiseBots/pull/330)

### Combat & AI
- Ghosts now resurrect the instant the server allows it (39 yd), fixing bots that sat at the graveyard for minutes because they kept re-picking the spot they were already standing on. [#331](https://github.com/Sagiroth/TortoiseBots/pull/331)
- Bots grouped with a player now wait up to 90 s for a resurrection instead of immediately bailing on the group. [#331](https://github.com/Sagiroth/TortoiseBots/pull/331)

### Observability & Engine
- `bot_events.csv` travel rows no longer log resets as trips to Alterac Mountains; empty goals at 0,0,0 were falsely recorded as new destinations, so travel entries now show real destinations only — [#332](https://github.com/Sagiroth/TortoiseBots/pull/332)
- Bot behavior is unchanged since no decision logic used this value, but log-based analysis is no longer misleading — [#332](https://github.com/Sagiroth/TortoiseBots/pull/332)

### Bots & Inventories

- Caster bots now spawn with just 2 weapon oils instead of 5–10, freeing up bag slots that were being wasted on unstackable copies. [#333](https://github.com/Sagiroth/TortoiseBots/pull/333)
- Oils are now level-appropriate: each bot gets the best tier it can actually use, matching mod-playerbots behavior. No more dead weight from low-level mana oils. [#333](https://github.com/Sagiroth/TortoiseBots/pull/333)
- Caster specialization is respected — mages, warlocks and shadow priests get wizard oil (spell damage), other priests get mana oil (regen). [#333](https://github.com/Sagiroth/TortoiseBots/pull/333)
- Outdated lower-tier oils, stones and poisons are stripped on spawn, but any better consumables the bot already owns — including raid-tier ones — are preserved. [#333](https://github.com/Sagiroth/TortoiseBots/pull/333)

### Bot AI & Training

- Bots that can't afford any of their available spells now shelve the trainer for 10 minutes instead of marching back every ~20 seconds — on one test realm that was 525 pointless trips against 30 actual lessons in 20 minutes. [#334](https://github.com/Sagiroth/TortoiseBots/pull/334)
- While the trainer is on cooldown, "visit the trainer" no longer counts as pending town business, so bots get back to grinding, questing, and vendoring instead of standing around waiting. [#334](https://github.com/Sagiroth/TortoiseBots/pull/334)

### Gear & Inventory
- Bots now re-scan their bags for upgrades on every level-up and on a rolling timer, so gear looted at low level actually gets equipped once they can use it [#335](https://github.com/Sagiroth/TortoiseBots/pull/335)
- Fixed the periodic gear-check that was never actually running — the root cause of bots hoarding upgrades in their bags indefinitely [#335](https://github.com/Sagiroth/TortoiseBots/pull/335)
- Covers the everyday offenders: stronger weapons left unused, empty slots with a matching item in bags, and ignored off-hands for casters [#335](https://github.com/Sagiroth/TortoiseBots/pull/335)

## 2026-09-27

<!-- Maintenance PRs kept out of the notes: #319 #320 -->

### Core Sync & Fixes
- Gave Mercenary Hire Recruiters explicit guids instead of `AUTO_INCREMENT`, eliminating collisions with core engine migrations that were spawning duplicate recruiter NPCs. [#314](https://github.com/Sagiroth/TortoiseBots/pull/314)
- Fixed on the module side rather than shifting core guids: module SQL isn't hash-tracked by mangosd's DB Auto-Updater, so the change won't re-run on environments that already applied the old core migration. [#314](https://github.com/Sagiroth/TortoiseBots/pull/314)

---

### World & Bot Population
- Random bots (levels 10–60) now stay scattered across level-appropriate zones instead of draining back to racial starting zones and capitals (~80% of the population was piling up there). [#312](https://github.com/Sagiroth/TortoiseBots/pull/312)
- Travel-node graph is now actually usable, with a fallback path so bots can navigate when the graph has no route — no more mass "long stuck" pileups. [#312](https://github.com/Sagiroth/TortoiseBots/pull/312)
- Ghost recovery works properly: bots no longer teleport back to their racial spawn point on repop (was ~351 repops in 4.7h). [#312](https://github.com/Sagiroth/TortoiseBots/pull/312)
- Unstuck hearthstones no longer yank bots home to the starting inn (~2042 uses in 4.7h from `move long stuck` / `combat long stuck`). Homebind behavior is far less disruptive. [#312](https://github.com/Sagiroth/TortoiseBots/pull/312)

### Core Sync & Fixes
- Fixed inverted `ExperienceValue::EqualToLast` check that falsely flagged actively-levelling bots in tight camps as "stuck" — this was the root cause of the spammy unstuck hearths. [#312](https://github.com/Sagiroth/TortoiseBots/pull/312)

### Roster & Addon
- Hired companions no longer show up in the `.bot roster` / addon Roster tab. Skips characters on registered random-pool accounts, so dismissed hires don't haunt your roster. [#311](https://github.com/Sagiroth/TortoiseBots/pull/311)

### Dashboard & Observability

- Issues tab now defaults to persistent problems (10+ minutes old), so you see actual ongoing issues instead of a wall of one-off noise nobody reads. [#315](https://github.com/Sagiroth/TortoiseBots/pull/315)
- `BOT_STUCK` and `STUCK` collapsed into a single `STUCK` issue (counter-only on the anomaly side); `ACTION_LOOP` is counter-only too — no more duplicate rows for the same underlying problem. [#315](https://github.com/Sagiroth/TortoiseBots/pull/315)
- 5-minute blackout after server restarts so `DEAD_LONG` stops flooding the feed while bots are still spinning up. [#315](https://github.com/Sagiroth/TortoiseBots/pull/315)
- `UNREACHABLE_TARGET` surfaces after 2 minutes instead of 5 — faster signal on bots that can't path to their target. [#315](https://github.com/Sagiroth/TortoiseBots/pull/315)
- Incidents panel is now explicitly labelled as a rolling window, so the scope of the numbers is no longer ambiguous. [#315](https://github.com/Sagiroth/TortoiseBots/pull/315)

### Stats & Fixes

- Players Online now counts real network sessions — it previously reported 0 for every human connection. [#315](https://github.com/Sagiroth/TortoiseBots/pull/315)
- Corrected several panels showing wrong numbers, based on a live audit of the running dashboard. [#315](https://github.com/Sagiroth/TortoiseBots/pull/315)

### Combat & AI
- Added `.bot action flee`: bots instantly drop combat and any pull hold, then fall back in passive follow using the existing mature flee shortcut. The next tactical order (attack, pull, pullback, focus skull, follow, stay) clears passive, and a fresh pull drags the whole party back into the fight [#317](https://github.com/Sagiroth/TortoiseBots/pull/317)

### Observability & Engine
- Roster trailer now advertises `TBM:BOTSTATE_*` snapshots so addons can read live bot state (movement mode: follow / stay / guard / free / flee, plus behavior) without polling or guesswork [#317](https://github.com/Sagiroth/TortoiseBots/pull/317)
- Added server-side bot panel commands backing the TBM addon; the addon hides these controls until the server advertises support, so mismatched client/server versions stay safe [#317](https://github.com/Sagiroth/TortoiseBots/pull/317)

### Core Sync & Fixes
- All changes are self-contained in `commands/BotCommands.cpp` — no core or host modifications, so nothing else in the module needs to be revalidated [#317](https://github.com/Sagiroth/TortoiseBots/pull/317)

### Server Configuration & Docs

- Configuration guide now covers many-bot realms: set `CleanupTerrain = 0` and `PlayerSave.Interval = 300000` to cut slow map ticks from 126 down to 20 per 10 minutes on a 500-bot realm [#318](https://github.com/Sagiroth/TortoiseBots/pull/318)
- Documented the catch: disabling terrain cleanup holds roughly 2 GB of continent terrain in memory, so keep `CleanupTerrain = 1` on low-RAM boxes [#318](https://github.com/Sagiroth/TortoiseBots/pull/318)

### Observability & Engine
- Zone names now render naturally everywhere, including custom zones (`Moonwhisper` instead of `Zone 5642`), and instanced bots show their instance rather than the outdoor entrance — no more guessing where a bot actually is. [#321](https://github.com/Sagiroth/TortoiseBots/pull/321)
- The Issues badge only counts persistent problems and disappears at zero, while Incidents now tracks warnings/errors instead of every bot death — signals you can trust at a glance. [#321](https://github.com/Sagiroth/TortoiseBots/pull/321)
- Fixed bots being listed as their own target in the bot list. [#321](https://github.com/Sagiroth/TortoiseBots/pull/321)
- The map zone list retains custom zones, so filtering no longer ejects you from the zone you're viewing; the world view is now labelled as a single continent. [#321](https://github.com/Sagiroth/TortoiseBots/pull/321)

### Mounts & Riding
- Random bots between levels 18–39 now get the Swift Riding Turtle (Torta's Egg reward) at creation and on dinging 18 — hired companions from the pool only, never a player's own alts. Tune with `AiPlayerbot.TurtleMountAtLevel` (default 18, `0` disables). [#323](https://github.com/Sagiroth/TortoiseBots/pull/323)
- Mount speed now mirrors Turtle's riding-skill rules instead of a flat bonus — no skill: level/2%, 75: 60%, 150: 100%. The turtle finally counts as a real mount, skills the core would force-unmount on are rejected as unusable, and level 60 bots with riding 150 ride at full 100%. [#323](https://github.com/Sagiroth/TortoiseBots/pull/323)

### Docs & Tooling
- Maintenance-only PRs no longer clutter the release notes; hidden comments keep their numbers in the file so the generator won't re-add them. `CHANGELOG.md` only. [#322](https://github.com/Sagiroth/TortoiseBots/pull/322)

### Random Bot Survival & AI
- Random bots now skip hostile towns as travel destinations, route legs, and grind targets — including neutral towns whose local faction hates them — cutting the ~12.6% of deaths that came from wandering into enemy guards. [#325](https://github.com/Sagiroth/TortoiseBots/pull/325)
- Bots remember who killed them and avoid that player, so the ~11% of deaths from a handful of bots feeding the same kill spot every minute dries up. [#325](https://github.com/Sagiroth/TortoiseBots/pull/325)
- Player-owned bots are unaffected and still obey their owner. Toggle with `AiPlayerbot.AvoidHostileTowns` (default on). [#325](https://github.com/Sagiroth/TortoiseBots/pull/325)

### Quests & Progression
- Random bots stop hoarding grey quests: they're skipped unless a reward is a genuine upgrade, and otherwise dropped. [#324](https://github.com/Sagiroth/TortoiseBots/pull/324)
- Quest logs get cleaned of greys and of finished quests that can no longer be handed in; a money shortfall never blocks a hand-in. Cleanup also fires when the log is nearly full, but only if something is actually droppable. [#324](https://github.com/Sagiroth/TortoiseBots/pull/324)
- Hand-in travel now starts at 2 finished quests and runs until the log is clear, fixing the lopsided ~5-taken-per-1-handed-in ratio. [#324](https://github.com/Sagiroth/TortoiseBots/pull/324)
- Random bots only — player-owned and alt bots are untouched. Toggle with `AiPlayerbot.BotQuestLogUpkeep` (default on). [#324](https://github.com/Sagiroth/TortoiseBots/pull/324)

### Bot Travel & Stuck Handling
- Move-stuck resets no longer wipe a bot's travel target. A stalled bot keeps its destination for up to 3 attempts without progress before the target is retired, instead of silently re-rolling and repeating the trip. [#326](https://github.com/Sagiroth/TortoiseBots/pull/326)
- Kills the pathological loop that had one bot make 122 identical trainer trips in 30 minutes. Quest hand-in runs also stop dying to the same reset. [#326](https://github.com/Sagiroth/TortoiseBots/pull/326)
- A destination that fails repeatedly now pauses only its own purpose for 5 minutes. Every other trip type stays available, so bots keep circulating instead of idling. [#326](https://github.com/Sagiroth/TortoiseBots/pull/326)

### Starter Zones & World
- Random bots now leave outgrown zones and capitals rather than piling up in starting areas and major cities. [#326](https://github.com/Sagiroth/TortoiseBots/pull/326)

## 2026-09-26

### Dungeon Finder & Battlegrounds
- LFT dungeon fill and BG queue are now **on by default** — no config edits needed to get bots filling roles while you wait in queue. Random bots still only fill when a real player is actually queued. [#310](https://github.com/Sagiroth/TortoiseBots/pull/310)
- Your own party bots now auto-accept dungeon offers instead of letting them expire after 90s, so you can run dungeons and battlegrounds with your regular crew rather than random fillers. Acceptance triggers only when the master or group leader is a real player. [#310](https://github.com/Sagiroth/TortoiseBots/pull/310)

### Attribution & Observability
- Author credit and source link now surface at startup, via `.bot version`, and as a one-time login system message for real players — forks, repacks, and videos carry attribution by default with no gameplay impact. Bot sessions are excluded from the login message. [#309](https://github.com/Sagiroth/TortoiseBots/pull/309)

---

## 2026-09-25

### Combat & AI

- Tank bots now stick to the mobs they're holding instead of abandoning them the moment an add shows up — held targets get priority over loose adds in the ranking logic. [#269](https://github.com/Sagiroth/TortoiseBots/pull/269)
- Added proper priority ordering: loose adds actively hitting someone else (nearest first), then mobs already held in melee range, then held mobs outside melee. No more random target flapping. [#269](https://github.com/Sagiroth/TortoiseBots/pull/269)
- Tanks return to finish off mobs they peeled away to grab — previously they'd wander off and never come back, leaving healers to tank. [#269](https://github.com/Sagiroth/TortoiseBots/pull/269)
- When holding two mobs at once, the current target wins the tie-break and lowest personal threat breaks remaining ties, so tanks stop ping-ponging on threat. [#269](https://github.com/Sagiroth/TortoiseBots/pull/269)
- `TankAssistTrigger` now gates on the donor's has-aggro-on-current-target check, reducing wasted taunts and target swaps during pulls. [#269](https://github.com/Sagiroth/TortoiseBots/pull/269)

---

### Combat & AI

- Warlock bots now only cast Fear on their own assigned RTI CC mark, so they stop burning Fear on random dotted adds and breaking group kill order. [#270](https://github.com/Sagiroth/TortoiseBots/pull/270)
- Existing CC is no longer overwritten by a redundant Fear cast — fewer lost traps, sheeps, and saps mid-pull. [#270](https://github.com/Sagiroth/TortoiseBots/pull/270)
- Howl of Terror moved behind the `cc pvp` strategy: no more 10-yard panic casts scattering PvE pulls, but warlocks still have it when PvP is enabled. [#270](https://github.com/Sagiroth/TortoiseBots/pull/270)

### Combat & AI
- Holy priests no longer secretly run a damage spec: Smite, Holy Fire, SW:P, and Starshards are now gated behind the `HealerShouldAttackTrigger`, so they only fire when solo or when the party is topped up and the priest has mana to spare. [#271](https://github.com/Sagiroth/TortoiseBots/pull/271)
- Parties no longer get a dry healer: a healthy group with a low-mana priest gets a wand instead of burning the last of the mana bar on off-spec damage. [#271](https://github.com/Sagiroth/TortoiseBots/pull/271)
- Damage nodes run at `ACTION_DEFAULT` relevance, keeping the healer rotation prioritized correctly while the balance-scaled mana reserve does the throttling. [#271](https://github.com/Sagiroth/TortoiseBots/pull/271)
- Ports mod-playerbots' `HealerShouldAttackTrigger` and `PriestHealerDpsStrategy`, so grouped holy priests behave like healers again instead of OOM-ing through trash. [#271](https://github.com/Sagiroth/TortoiseBots/pull/271)
- Holy Nova restored behind the same gating. [#271](https://github.com/Sagiroth/TortoiseBots/pull/271)

### Crowd Control & Commands

- **One mark, one owner:** `.bot action cc <mark>` now clears any other party bot holding that mark back to `none`, and the change persists across sessions — no more silent CC collisions between bots. [#272](https://github.com/Sagiroth/TortoiseBots/pull/272)
- **Assign by name:** `.bot action cc <mark> <Bot>` lets you hand out CC marks without targeting the bot first, going through the exact same path as selected-bot assignment (exclusive ownership plus the immediate-cast probe). [#272](https://github.com/Sagiroth/TortoiseBots/pull/272)
- **Clear what you set:** `.bot action cc clear [Bot]` drops CC for a single bot, or the entire party when no bot is named — handy for resetting before a pull or after a wipe. [#272](https://github.com/Sagiroth/TortoiseBots/pull/272)
- **Fail loud, not silent:** Unknown or uncontrollable bot names return `no-bot` instead of quietly doing nothing, so typos surface immediately. [#272](https://github.com/Sagiroth/TortoiseBots/pull/272)
- Ships alongside the stage-1 dungeon gate and AoE interlock, so CC assignments respect instanced content and don't get stomped by AoE routines. [#272](https://github.com/Sagiroth/TortoiseBots/pull/272)

### Combat & AI — Crowd Control
- Smart auto CC is now available but **off by default**: flip it per bot with `.bot action auto cc [on|off]` (no argument toggles) or `.bot strategy +/-auto cc`. Setting persists like the loot toggle, and the ACK reports `on`/`off`/`mixed`. [#276](https://github.com/Sagiroth/TortoiseBots/pull/276)
- Auto CC is deliberately conservative: it only fires when the group is fighting 2+ live enemies, so the last mob never gets sheeped, and it targets loose adds hitting healers/casters — never the tank. [#276](https://github.com/Sagiroth/TortoiseBots/pull/276)
- `.bot action cc <mark>` is now deterministic: CC actions are ranked by spell fit (Sap on unengaged targets, Shackle Undead, Banish, Hibernate, Polymorph, Freezing Trap, Turn Undead, Scare Beast, Entangling Roots, Fear), with lowest GUID breaking ties. No more executor-by-invite-order surprises. [#273](https://github.com/Sagiroth/TortoiseBots/pull/273)

### Bots & Population
- Fresh realms now spread random bots across **levels 1–60** instead of stacking the whole pool at level 1: `AiPlayerbot.RandomBotStartLevelMax` defaults to 60, and each pool bot is seeded exactly once on its first login. Existing bots are untouched (seeding only runs at zero played time). [#275](https://github.com/Sagiroth/TortoiseBots/pull/275)
- `AiPlayerbot.LevelLadder` now defaults to **on**, so the online population is picked per level band and stays spread out across levels instead of the whole pool drifting upward together. [#275](https://github.com/Sagiroth/TortoiseBots/pull/275)

### World & NPCs
- Recruiters are no longer carbon copies of their innkeepers: each `<Mercenary Hire>` NPC now gets the **opposite gender** of the innkeeper beside it, same race, fully dressed. [#274](https://github.com/Sagiroth/TortoiseBots/pull/274)
- All 67 recruiter displays are pulled from existing 1.18.1 vendor/trainer NPCs, so every model is known-good to render, all displays are distinct, and no innkeeper display is reused. [#274](https://github.com/Sagiroth/TortoiseBots/pull/274)

### Economy & Auction House
- Synthetic auction items stop spamming `AddToUpdateQueueOf … current owner (None)` errors — random properties are now written directly instead of triggering `SetState(ITEM_CHANGED)`, so player logs no longer look like bots are misbehaving. [#288](https://github.com/Sagiroth/TortoiseBots/pull/288)

### Commands & Chat Text
- Usage/help strings now use `/` instead of raw `|`, fixing garbled output in the WoW client (`[dps|tank|healer]` was rendering as `dps | tankealer`); cppovers `.bot action`, `hire`, `role`, `formation`, `loot`, `strategy`, `ah`, `bot pool` plus debug/quest/tame/formation/stance help. [#287](https://github.com/Sagiroth/TortoiseBots/pull/287)
- `position path` / `position route` accept `A/B` alongside the legacy syntax, so both styles just work. [#287](https://github.com/Sagiroth/TortoiseBots/pull/287)

### Quests & Group Play
- Grouped bots near their master now actually receive the quest the master accepted — the replayed accept was being refused beyond `INTERACTION_DISTANCE`, so `QuestAction::AcceptQuest` grants it directly when the bot is eligible, alive, on the same map, and within `reactDistance` of the grouped master. [#286](https://github.com/Sagiroth/TortoiseBots/pull/286)

### Loot & Corpses
- Bots now loot corpses whose only loot *for them* is a quest item — `LootAccess` is player-aware and merges shared loot with per-player quest, FFA, and conditional lists, skipping already-looted entries and duplicates while inheriting the same status to `ShouldLootObject`. [#285](https://github.com/Sagiroth/TortoiseBots/pull/285)

### Hiring & Group Management
- `.bot hire` provisions exactly once: `ProvisionHeavy` (level, talents, spells, gear, `SaveToDB`, tank kit) runs a single time, and retries only re-run `Reunite` (teleport + grouping) instead of 2-3 full passes on the world thread. [#284](https://github.com/Sagiroth/TortoiseBots/pull/284)
- Retries no longer try to group a bot mid-teleport, cutting hire-time hitching. [#284](https://github.com/Sagiroth/TortoiseBots/pull/284)

### Observability & Engine

- Every merge to `main` now gets its own build version in the form `<UTC date>-v<N>` (e.g. `2026-09-25-v3`), exposed in game and via `.bot version` / `TBM:VERSION` so players and operators can pinpoint exactly which build is running. [#290](https://github.com/Sagiroth/TortoiseBots/pull/290)
- `N` only counts real merges — bot `[skip ci]` commits are ignored — and resets to `v1` each day, so version numbers stay meaningful instead of inflating with changelog noise. [#290](https://github.com/Sagiroth/TortoiseBots/pull/290)
- Workflow now triggers on pushes to `main` only and uses a `concurrency` group, so back-to-back merges no longer race each other to stamp the `VERSION` file. [#290](https://github.com/Sagiroth/TortoiseBots/pull/290)
- Per-build lightweight tags are created without spinning up a release or firing a Discord post; the existing daily `vYYYY-MM-DD` release is untouched. [#290](https://github.com/Sagiroth/TortoiseBots/pull/290)

### Core Sync & Fixes

- Fixed the build-version workflow so it actually completes: stamped files are committed before `git pull --rebase`, which previously aborted because the working tree was dirty. [#291](https://github.com/Sagiroth/TortoiseBots/pull/291)
- Restored `release_exists_on_github`, which was removed in #290 but still called — that missing function broke the first run outright. [#291](https://github.com/Sagiroth/TortoiseBots/pull/291)
- Release creation/editing is now skipped unless notes were actually written, while the daily tag still moves as before, cutting down pointless empty releases. [#291](https://github.com/Sagiroth/TortoiseBots/pull/291)

### CI, Releases & Discord

- Release list and Discord embed titles now include the build range, so you can spot `v1–vN` without opening the release body; release titles use `<repo> <date> (builds v1–vN)` and Discord uses `<repo> build <date>-vN`, with links still pointing to the daily release. [#292](https://github.com/Sagiroth/TortoiseBots/pull/292)

### Configuration & Defaults
- Code fallbacks in `PlayerbotAIConfig.cpp` now match the shipped `aiplayerbot.conf.dist.in`, so deleting a line from `aiplayerbot.conf` no longer silently changes bot behavior — what you see is what runs. [#295](https://github.com/Sagiroth/TortoiseBots/pull/295)
- Commented examples in the template now show the effective values, so you can see the real numbers you're tweaking without digging through source. [#295](https://github.com/Sagiroth/TortoiseBots/pull/295)
- Dead knobs are explicitly marked `Currently has no effect (read but unused)` in the template, with a new guide section 8 covering them — no more chasing settings that do nothing. [#295](https://github.com/Sagiroth/TortoiseBots/pull/295)
- `RandomGearBlacklist` is now actually implemented across seed/hire/upgrade loops and upgrade suggestions; template default was fixed from `0` to empty. Exclude problem items from bots for real. [#295](https://github.com/Sagiroth/TortoiseBots/pull/295)

### Documentation & Developer Workflow
- `AGENTS.md` slimmed from 312 lines to ~60 — stale MVP-era scope rules ("no raids/BG/random bots before the dungeon MVP") and duplicated architecture rules are gone, with the latter now living in `docs/concepts/architecture-invariants.md`. [#298](https://github.com/Sagiroth/TortoiseBots/pull/298)
- Observability daemon internals relocated to `tools/observability/README.md` — no content lost, just moved next to the code it describes. [#298](https://github.com/Sagiroth/TortoiseBots/pull/298)
- Donor lookup now starts with `mod-playerbots`, so new contributors and tooling resolve the reference implementation first instead of guessing. [#298](https://github.com/Sagiroth/TortoiseBots/pull/298)
- Added explicit product direction: player control first, automation opt-in, bots always alive — stops future contributors from "fixing" the design in the wrong direction. [#298](https://github.com/Sagiroth/TortoiseBots/pull/298)
- Documented the Docker dev loop and the config fallback rule (fallback must match the shipped template) so operators don't chase load-time mismatches. [#298](https://github.com/Sagiroth/TortoiseBots/pull/298)

### Docs & Provenance

- Trimmed an external UX reference from the CC mark provenance entry per owner request — the mod-playerbots source attribution stays untouched, so licensing/credit records remain accurate with one less stray link. [#299](https://github.com/Sagiroth/TortoiseBots/pull/299)

### Docs & Configuration
- OKF documentation re-verified line-by-line against the actual code — every correction was confirmed in source before landing, and rejected findings are logged with reasons in the review notes. [#301](https://github.com/Sagiroth/TortoiseBots/pull/301)
- Fixed installed config paths and documented the real reload scope, so operators stop guessing which file changes actually take effect. [#301](https://github.com/Sagiroth/TortoiseBots/pull/301)
- Corrected wrong documented defaults for `CriticalHealth`, `LowMana`, and `MediumMana`, plus the Thunderfury broadcast setting. [#301](https://github.com/Sagiroth/TortoiseBots/pull/301)
- Clarified that bots are always active by default — no hidden toggle required to get them running. [#301](https://github.com/Sagiroth/TortoiseBots/pull/301)

### Bot Behavior & Defaults
- `RandomGearUpgradeEnabled` only seeds fresh bots; existing bots won't retroactively re-roll gear. [#301](https://github.com/Sagiroth/TortoiseBots/pull/301)
- `SyncAltLevelToMaster` requires the master to be the party leader and applies +1 level per update tick. [#301](https://github.com/Sagiroth/TortoiseBots/pull/301)
- `botActiveAlone` documented as a percentage rather than a boolean switch. [#301](https://github.com/Sagiroth/TortoiseBots/pull/301)
- Auction house synthetic switches, login-scatter preconditions, and profession pairing rules now match the implemented behavior. [#301](https://github.com/Sagiroth/TortoiseBots/pull/301)

### Gear & Itemization
- Every item now gets a source-tier tag (base / end-game dungeon 229-289-329-800 / raid) plus orthogonal REP and PVP flags, computed once from loot→spawn→map minima with lowest-tier-source-wins — bots can no longer roll gear above your configured tier. [#293](https://github.com/Sagiroth/TortoiseBots/pull/293)
- Recipe chains and quest sources are folded into the same classification, so crafted and quest-reward gear is tiered correctly instead of slipping through. [#293](https://github.com/Sagiroth/TortoiseBots/pull/293)
- New `RandomGearMaxSourceTier` (default 0) plus `RandomGearAllowReputation` / `RandomGearAllowPvP` knobs replace the old ad-hoc raid/PvP checks — one enforcement point on the seed/hire path, no double logic. [#293](https://github.com/Sagiroth/TortoiseBots/pull/293)
- Rare world epics get a per-slot gate: `RandomGearSeedEpicChance` (0.02) rolls loot-attested BoE world epics so seeds feel special without trivializing progression. [#293](https://github.com/Sagiroth/TortoiseBots/pull/293)
- Low-level seed coverage filled in — bots get sensible starter gear instead of empty slots at early levels. [#293](https://github.com/Sagiroth/TortoiseBots/pull/293)

### Core Sync & Fixes
- Classification is persisted in `ai_playerbot_item_info_cache` (migration `20260925140000`) so it's computed once, not per spawn. [#293](https://github.com/Sagiroth/TortoiseBots/pull/293)

### Bot Kits & Companions
- Bots now spawn with the full intended kit — bags, profession tools (including fishing pole), 40/60 mounts, fresh-seed money, Paladin Divinity/Kings, and level-tier bandages — across both pool bots and hired companions [#296](https://github.com/Sagiroth/TortoiseBots/pull/296)
- Hired companions restock on an hourly `HireLifecycle` cadence via `RestockCompanion()`, so long-lived hires stay supplied without manual intervention [#296](https://github.com/Sagiroth/TortoiseBots/pull/296)
- Seeding is idempotent: no duplicate kits, no wasted restocks, safe on repeated completions/provisioning passes [#296](https://github.com/Sagiroth/TortoiseBots/pull/296)

### Core Sync & Fixes
- Removed dead `Randomize()` and its orphaned helpers (wipe paths, quest/taxi/reputation seeding, second equipment set, immersive/inventory-trade/equip) to shrink the factory and cut maintenance surface [#296](https://github.com/Sagiroth/TortoiseBots/pull/296)

### Bot Enchanting
- Seeded and hired bots at level 10+ now receive level-appropriate permanent enchants matched to class/spec, closing the gear gap with same-level players. [#300](https://github.com/Sagiroth/TortoiseBots/pull/300)
- New `RandomItemMgr` picker selects enchants from the curated candidate pool, replacing the deleted template-only C++ path. [#300](https://github.com/Sagiroth/TortoiseBots/pull/300)

### Data & Cleanup
- Added `ai_playerbot_enchant_candidates` migration `20225150000`: 438 rows / 283 spells with slot, minimum character level, source tier, reputation, and premium flags. [#300](https://github.com/Sagiroth/TortoiseBots/pull/300)
- Default pool excludes 55 rows (37 spells) from dungeon/raid/rep sources; QA/Test/Copy/payload-less spells are never inserted. [#300](https://github.com/Sagiroth/TortoiseBots/pull/300)
- Emptied legacy `ai_playerbot_enchants` so bots rely only on the curated enchant system. [#300](https://github.com/Sagiroth/TortoiseBots/pull/300)

### Combat & AI

- Pull strategy now supports per-command return mode: pull behavior switches for that single command and restores itself at pull end, so it never sticks around and breaks later pulls. [#302](https://github.com/Sagiroth/TortoiseBots/pull/302)
- Party DPS now holds at the command anchor using stay plus the mature wait-for-attack window — no more stragglers wandering off mid-pull before the tank is set. [#302](https://github.com/Sagiroth/TortoiseBots/pull/302)
- Pullback join delay is now counted from the tank's actual *arrival* at the anchor rather than cast time, so bots commit to the fight when the tank actually gets there. [#302](https://github.com/Sagiroth/TortoiseBots/pull/302)
- Hold windows now adapt dynamically: the command widens the hold to cover the capped return, the arrival brake narrows it to the join delay and restarts the clock, and a return timeout zeroes held windows so the per-bot trigger fires on the next tick instead of hanging. [#302](https://github.com/Sagiroth/TortoiseBots/pull/302)
- Each held bot now releases itself through a new `pull hold expired` trigger and `release pull hold` action, giving per-bot control over release timing instead of a central drop. [#302](https://github.com/Sagiroth/TortoiseBots/pull/302)

### Observability & Engine

- Telemetry emitter no longer permanently falls back to `127.0.0.1` when the dashboard's docker hostname isn't resolvable at boot — it keeps the configured host and retries resolution every 30s, so metrics resume as soon as the dashboard container comes up (e.g. after a reboot). [#303](https://github.com/Sagiroth/TortoiseBots/pull/303)
- Host lookup runs on a background task, so DNS retries can never stall the world thread; the "telemetry active" log line now only appears once the host has actually resolved, making misconfig easy to spot. [#303](https://github.com/Sagiroth/TortoiseBots/pull/303)

### Security & Permissions
- Locked down bot chat commands behind a single authorization gate, so random players can no longer hijack someone else's bots mid-run. [#304](https://github.com/Sagiroth/TortoiseBots/pull/304)
- GM-only commands (`cheat`, `debug`/`cdebug`/`cs`/`log`, `set value`, `teleport`, AI resets) now require a real GM session — no more accidental server-breaking from a curious alt. [#304](https://github.com/Sagiroth/TortoiseBots/pull/304)
- Bot management (mail, bank, AH, trade, guild, craft, strategy edits like `co`/`nc`) is restricted to the bot's durable owner or a GM; unknown commands default to denied instead of silently running. [#304](https://github.com/Sagiroth/TortoiseBots/pull/304)
- Party members can still issue tactical commands, so in-combat coordination with your bots keeps working as before. [#304](https://github.com/Sagiroth/TortoiseBots/pull/304)
- Denied actions now whisper a short reason to the sender, and addon senders receive a structured `TBM:ACTION_ERR|cmd|denied|…` response so UI addons can surface the failure cleanly. [#304](https://github.com/Sagiroth/TortoiseBots/pull/304)

### Observability & Engine
- Web dashboard is now usable on phones (360–430 px) and tablets (768 px): sidebar collapses into an off-canvas drawer with a dimmed backdrop, stacked grids, and fluid controls [#305](https://github.com/Sagiroth/TortoiseBots/pull/305)
- New compact mobile topbar shows the active tab title plus an accessible hamburger toggle (`aria-label` / `aria-expanded`) — tap a tab, the backdrop, or hit Escape to dismiss the drawer [#305](https://github.com/Sagiroth/TortoiseBots/pull/305)
- Desktop layout at ≥ 1200 px is intentionally left untouched, so existing operator workflows and muscle memory stay intact [#305](https://github.com/Sagiroth/TortoiseBots/pull/305)

### Bot Gear & Enchanting

- White-quality gear can now carry non-premium enchants (min level ≤ bot level − 10) instead of nothing at all — freshly spawned low-level bots stop showing up effectively naked. [#306](https://github.com/Sagiroth/TortoiseBots/pull/306)
- Green-quality gear enchant threshold relaxed from bot level − 10 to bot level − 5, so green drops get meaningful enchants noticeably earlier. [#306](https://github.com/Sagiroth/TortoiseBots/pull/306)
- Grey gear stays unenchanted, and blue/epic ceilings are untouched — economy and high-end itemization balance are unaffected. [#306](https://github.com/Sagiroth/TortoiseBots/pull/306)
- Verified on dev after a random pool reset (500 bots): level 10–19 band went from 1/152 enchanted items to 281/1581, and 30–39 jewelry from 91/282 to 147/270. [#306](https://github.com/Sagiroth/TortoiseBots/pull/306)

### Observability & Engine

- Dashboard armory now reports the real server-side stats for online bots instead of a reconstructed approximation — no more ~1600 HP on a fully geared, enchanted level 60 warrior. [#307](https://github.com/Sagiroth/TortoiseBots/pull/307)
- Stats are copied straight from the live `Player` object into a new module table (`tortoise_bots_armory_stats`), so stamina-derived max HP, enchants, talents, buffs, and racials are all accounted for instead of only class base values plus raw item stats. [#307](https://github.com/Sagiroth/TortoiseBots/pull/307)
- Snapshots are throttled at 10 bots per 2 s (~100 s to cover 500 bots), keeping the emitter cheap and off the gameplay hot path. [#307](https://github.com/Sagiroth/TortoiseBots/pull/307)

### Combat & AI
- Fixed bots sitting at Defense 1 at every level: at 60 that was 1/300, meaning zero dodge/parry and mobs getting roughly +12% hit and crit chance against them. `InitSkills` now seeds Defense to `level*5`, so max-level bots actually defend like max-level characters. [#308](https://github.com/Sagiroth/TortoiseBots/pull/308)
- Bots can parry now. The trainer-only Parry chain (3128 → 3127, `SPELL_EFFECT_PARRY`) is granted at level 8 for warriors, paladins, hunters, and rogues, alongside the existing Dual Wield grant — no more free hits for anything swinging at your tank. [#308](https://github.com/Sagiroth/TortoiseBots/pull/308)

### Progression & Leveling
- Defense seeding also re-runs on every level-up for random bots (`auto learn spell`), so leveling bots keep pace instead of falling behind and getting shredded in the 50s. [#308](https://github.com/Sagiroth/TortoiseBots/pull/308)

## 2026-09-24

### Managed Random-Bot Pool Reset ([#265](https://github.com/Sagiroth/TortoiseBots/issues/265))
- Random-pool identity now comes from a **managed-account registry** (`tortoise_bots_pool_account`) instead of a username prefix. New pool and hire accounts are registered *before* their first character is created, and a prefix match never authorizes deletion — a personal account such as `RNDBOTPersonal` is ignored by the pool and is never reset. Existing hand-made bot accounts must be enrolled once at the server console (`bot pool adopt preview` / `bot pool adopt confirm <challenge>`); until then startup logs them as unmanaged and leaves them alone.
- Added `AiPlayerbot.RandomBotPoolReset` (`off` default, `once:<token>`, `always`) to rebuild the pool after changes to fresh-character seeding, gear, professions, or skills. A reset runs only during initial world startup, deletes the registered pool characters one per world tick, verifies that nothing remains, and records the generation only then — so a crash mid-reset resumes on the next start and re-using a token is a no-op. `.reload config` cannot start a reset, and there is no live reset command. Login accounts are retained; `RandomBotAutoCreate=1` is required so the pool regenerates instead of staying empty.
- Reset safety: registered accounts are re-validated (existence and username) before the first deletion, human sessions are never logged out or deleted, bot-led guilds holding non-pool members abort the reset, auctions are settled through the live auction subsystem (active bidders refunded, no raw SQL), and hiring/auto-create/autologin/battleground selection pause while the pool is rebuilding.
- Added administrator commands `bot pool status`, `bot pool accounts`, `bot pool adopt preview`, and `bot pool adopt confirm <challenge>`. The console-only commands require a session-less handler carrying `SEC_CONSOLE`, so remote-access and Discord commands (which reuse session-less handlers with the caller's own security level) cannot enroll accounts either.
- Reset hardening: the pool is unavailable whenever the registry cannot be validated (hiring, auto-create, autologin, free-alt enrollment, and battleground selection all stop), an auction bid that cannot be refunded aborts the reset before the listing is deleted, hardcore bidders follow the core's own cancel rule exactly, and an empty managed set is a verified no-op that still records the generation instead of leaving the token to wipe a pool auto-create builds later.
- Auctions are now settled in a dedicated phase before the first deletion. A pool bot bidding on another pool bot's listing used to be deleted before that listing was reached, which permanently blocked reset recovery ("refusing to delete the listing without a refund"); settlement now happens while every pool bidder still exists, and the deletion step stops the reset if a listing appears afterwards.
- The observability armory now identifies bots by joining `tortoise_bots_pool_account` instead of matching an account-name prefix; the daemon's `--bot-account-prefix` flag / `BOT_ACCOUNT_PREFIX` variable is gone with it.

### Combat & AI
- DPS bots now prioritize the group tank’s target after explicit attack commands and Skull/X raid marks, but before the least-HP fallback, improving focused fire and reducing target thrash. [#266](https://github.com/Sagiroth/TortoiseBots/pull/266)
- Tank-target candidates are validated as alive, hostile, in combat or attacked by the party, in line of sight, and not crowd-controlled before DPS bots commit to them. [#266](https://github.com/Sagiroth/TortoiseBots/pull/266)

### Commands & Role Overrides
- Added player role override storage so explicit player-assigned roles can steer bot behavior and override default DPS targeting priorities. [#266](https://github.com/Sagiroth/TortoiseBots/pull/266)

---

### Bot Pool Management
- Random-bot pool identity is now tracked in a character-DB table (`tortoise_bots_pool_account`) instead of a username prefix, so discovery, hiring, creation, and deletion of pool characters all go through one authoritative ownership record. [#267](https://github.com/Sagiroth/TortoiseBots/pull/267)
- The module now only touches accounts it actually owns — no more clobbering bots or stray accounts that happen to share a naming pattern. [#267](https://github.com/Sagiroth/TortoiseBots/pull/267)
- Added a safe startup rebuild via `AiPlayerbot.RandomBotPoolReset`: `off` (default), `always` rebuilds every boot, and `once:<token>` rebuilds exactly one time until the token changes. [#267](https://github.com/Sagiroth/TortoiseBots/pull/267)

### Config & Ops
- Use `once:<token>` (e.g. a version or date string) to force a single clean pool rebuild after a data or roster change, without paying the cost on every restart. [#267](https://github.com/Sagiroth/TortoiseBots/pull/267)

### Core Sync & Fixes
- Fixed failed world-SQL imports on MySQL/MariaDB — `SELECT ... WHERE NOT EXISTS` with no `FROM` isn't valid syntax in those engines, so the enchant migration now uses the `FROM DUAL` idiom to apply cleanly. [#268](https://github.com/Sagiroth/TortoiseBots/pull/268)
- Retired the broken enchant migration pair (`20260922170000_world.sql` / `20260922180000_world.sql`) after confirming the `FROM DUAL` fix alone still wasn't sufficient on a real re-run. [#268](https://github.com/Sagiroth/TortoiseBots/pull/268)
- Pipeline world-SQL imports no longer hard-fail partway through on these migrations, unblocking fresh world database setups. [#268](https://github.com/Sagiroth/TortoiseBots/pull/268)

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

### Combat & AI

- Party buff routines now only target actual party members — bots stop wasting mana and GCDs buffing random nearby players [#263](https://github.com/Sagiroth/TortoiseBots/pull/263)
- Paladin blessing triggers were re-scoped to `party member without aura`, so blessings land on the group instead of any friendly in range [#263](https://github.com/Sagiroth/TortoiseBots/pull/263)
- `ignoreOutOfGroup` is now enforced on `PartyMemberWithoutMyAuraValue` and `PartyTankWithoutAuraValue`, keeping aura checks strictly inside the group [#263](https://github.com/Sagiroth/TortoiseBots/pull/263)
- Bots that are grouped or bound to a master will never scan nearby strangers for party-buff targets, even when `allowBufOutOfGroupPlayers` is enabled [#263](https://github.com/Sagiroth/TortoiseBots/pull/263)

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
