---
id: play-dungeons-and-raids
title: Dungeon & Raid Tactics Guide
category: play
summary: Field manual for clearing 5-player dungeons and raids with companion bots, covering corner pulling, CC discipline, boss tactics, wipe recovery, and loot rules.
tags: [dungeon, raids, tactics, pulling, cc, wipes, instances, loot]
relates_to:
  - play-commands-party-actions
  - play-dungeon-reference
  - play-known-limitations
  - server-settings-dungeons-raids
---

# Dungeon & Raid Tactics Guide

Leading a companion bot party through dungeons and raids requires deliberate positioning, pull discipline, and crowd control. Dungeon enemies hit hard, patrol packs add unexpectedly, and spellcasters stand at range. This guide explains how bot combat and movement mechanics work inside instances and how to lead your group safely.

For creature IDs, boss kill orders, and pooled rares for individual Turtle WoW 5-man dungeons, see the [Turtle 5-Man Dungeon Reference](dungeon-reference.md).

## The Five Golden Rules of Bot Dungeoneering

1. **Pull to Line-of-Sight Corners, Don't Charge:** Use [`.bot action pullback`](commands/party-actions.md#bot-action-pullback) so the tank pulls from range and runs back around a corner to your party. Never fight in open hallways where patrols roam.
2. **Toggle AoE Off on Crowd Control Packs:** Before pulling packs where you plan to use *Polymorph*, *Sap*, or traps, run [`.bot action aoe off`](commands/party-actions.md#bot-action-aoe). This stops damage-over-time spells and area attacks from breaking crowd control.
3. **Always Mark the Primary Target with Skull:** Issue [`.bot action focus skull`](commands/party-actions.md#bot-action-focus-skull). DPS bots treat the Skull mark as their kill priority while respecting CC and peel rules.
4. **Give the Tank Two Seconds:** Tanks apply threat abilities like *Sunder Armor* or *Torment* based on lowest personal threat. Give them 2–3 seconds to establish initial aggro before opening burst damage.
5. **Keep Summon Ready:** If a bot gets stuck on complex instance terrain, stairs, or elevators, use [`.bot summon <Name>`](commands/roster-and-hiring.md#bot-summon) to snap them to your location out of combat.

## Pulling Mechanics & Line-of-Sight (LoS)

Corridors are hazardous in dungeons like Deadmines, Scarlet Monastery, and Stratholme. Casters stand at 30 yards casting fireballs while patrols flank your healer.

### The Pullback Maneuver

When you target an enemy and issue [`.bot action pullback`](commands/party-actions.md#bot-action-pullback):

1. **Puller Selection:** The server picks the puller by precedence: your targeted bot, else any bot designated with the tank role, else a native tank-spec bot.
2. **Ranged Strike:** The tank uses its class pull action (ranged weapon shot, throw, *Judgement*, *Faerie Fire*, *Serpent Sting*, or *Lightning Bolt*). If no ranged attack is available, it closes in for a body pull.
3. **Corner Sprint:** After applying initial threat, the tank immediately sprints back to your party's current location. Non-tank bots hold fire until the tank reaches the regroup anchor, drawing the pack around the corner into your ambush.
4. **Hold Release:** The tank holds the regroup anchor only for the fight that the pull brought in. When the mobs are dead, the hold releases and the tank resumes normal follow formation. Any manual movement order ([`.bot action stay`](commands/party-actions.md#bot-action-stay) or [`.bot action follow`](commands/party-actions.md#bot-action-follow)) overrides this hold.
5. **Failed Pull Cleanup:** If a pull draws no aggro because the shot is blocked, the mob evades, or nothing arrives within 15 seconds, the tank drops the target, leaves combat, and rejoins the party. DPS bots are released immediately rather than waiting.

```mermaid
flowchart TD
    subgraph DangerRoom ["Corridor / Patrol Room (Unsafe)"]
        TargetMob["Target Mob Pack (Melee + Caster Adds)"]
    end

    subgraph PullStep ["Pull Sequence"]
        Command["Player Issues: .bot action pullback"] --> TankPull["Tank Pulls with Ranged Attack"]
        TankPull --> Retract["Tank Immediately Sprints Back Around Corner"]
        TargetMob -->|"LoS Broken: Casters Forced to Follow"| Corner["LoS Corner Pivot"]
    end

    subgraph AmbushZone ["Safe Ambush Area (Behind Corner)"]
        Corner --> Ambush["Ambush Zone: Party Holds Fire Until Tank Regroups"]
        Ambush --> Combat["All Bots Engage Safely Away from Patrols"]
    end
```

## Crowd Control Protocol

Crowd control is essential for managing multi-mob packs in level 40+ dungeons.

### Raid Target Icon Assignments

Every bot starts with **Moon** as its default CC mark. The table below represents the recommended convention:

| Raid Icon | Primary Class & Ability | Valid Targets | Notes |
| :---: | :--- | :--- | :--- |
| **Moon** | **Mage** (*Polymorph*) / **Rogue** (*Sap*) | Beasts, Humanoids (Sap: humanoids only) | Rogues stealth and Sap out of combat before the pull. |
| **Star** | **Priest** (*Shackle Undead*) / **Paladin** (*Turn Undead*, Retribution *Repentance* on Humanoids) | Undead (Repentance: Humanoids) | Essential in Stratholme, Scholomance, and Shadowfang Keep. |
| **Diamond** | **Warlock** (*Banish* / *Fear* / Succubus *Seduction*) | Demons, Elementals (Banish); Humanoids (Seduction) | Banish immunizes target; Fear only on assigned mark. |
| **Triangle** | **Hunter** (*Freezing Trap* or *Scare Beast*) / **Druid** (*Hibernate* / *Roots*) | Beasts, Dragonkin | Hunter Freezing Trap or Scare Beast; Druid Hibernate (beasts, dragonkin) or Roots (outdoors only). |

### Issuing CC In-Game

- Target the enemy mob and run [`.bot action cc <mark>`](commands/party-actions.md#bot-action-cc).
- **Exclusive Mark Ownership:** Each mark is assigned to exactly one bot. Reassigning a mark moves ownership.
- **Rogue Sap Before Pull:** Out of combat, a rogue stealths and Saps the humanoid marked with its CC icon. Mark the target first, wait for the Sap, then pull.
- **Dungeon 5-Man Rule:** Inside non-raid dungeons, bots CC only their explicitly assigned mark. They never CC random targets unless opt-in auto-CC is enabled ([`.bot action auto cc`](commands/party-actions.md#bot-action-auto-cc)).
- **Protection for Held Marks:** Party bots and pets strictly avoid attacking crowd-controlled targets until all other active threats are dead. Marked mobs are killed last.
- **Area Spell Discipline:** Area spells (*Blizzard*, *Rain of Fire*, *Multi-Shot*, *Frost Nova*) automatically check blast radii and will not fire if their radius touches a crowd-controlled enemy.
- **Healer Peel Taunt Exception:** If a crowd-controlled mob breaks free or an un-CC'd mark attacks a healer, the tank will taunt it to save the healer without breaking CC rules.

<details>
<summary>Detailed interrupt and pet discipline mechanics</summary>

When you order [`.bot action interrupt`](commands/party-actions.md#bot-action-interrupt) or let bots counter enemy casts, the AI probes for ready interrupt abilities across party members:
- Rogue: *Kick*
- Warrior: *Pummel* (Berserker) or *Shield Bash* (Battle/Defensive with shield)
- Shaman: *Earth Shock*
- Mage: *Counterspell*
- Priest: *Silence* (Shadow)
- Warlock: *Spell Lock* (Felhunter pet) or *Death Coil*
- Paladin: *Hammer of Justice* (stun) or *Repentance*
- Druid: *Bash* (Bear)

If the assigned bot is in range, it casts immediately; otherwise, it closes distance first.

Pets belonging to Hunter and Warlock bots strictly respect breakable CC (*Polymorph*, frozen effects, *Sap*, *Gouge*, *Shackle Undead*) unless explicitly directed to the Skull-marked target. A warlock's Succubus can use *Seduction* on the warlock's CC mark; the warlock's own Fear or Banish comes first. A seduced mob is protected from area-of-effect abilities like any other CC.

</details>

## Wipe Recovery & Instance Navigation

Inside a dungeon or raid, a dead companion bot waits at its body for a resurrection while you are alive on the same map. Once you are dead as well (a wipe), it releases by itself, runs back as a ghost and revives at its corpse. You only need the steps below to hurry a bot along or to fix a stuck one.

### Releasing Spirit & Corpse Runs

1. When dead, whisper your bots `/w <BotName> release` (or type `.bot command <Name> release`). See [Whisper Commands](commands/whispers.md#w--release).
2. Once in ghost form at the graveyard, issue `/w <BotName> corpse run`.
3. The ghost walks back to the dungeon entrance and goes in. In a dungeon with several entrances (the Scarlet Monastery wings, the back doors of Uldaman and Gnomeregan) it takes the one closest to where its body lies, so it comes back to life where it died.
4. A ghost cannot be summoned (`.bot summon` refuses dead bots). If a ghost is stuck, whisper it `/w <BotName> revive`: it takes the spirit healer (with resurrection sickness). Once it is alive again, summon it to you.

### Healers and Casters Closing Range

Inside an instance, a healer or caster that has line of sight to its target but stands out of range walks the navmesh path forward and stops at the first point within casting distance. Out of combat, a healer only walks to a party member who is injured (below [`AiPlayerbot.AlmostFullHealth`](../server/settings/combat.md#aiplayerbotalmostfullhealth)). In combat, healers stay close to the tank ([shared positioning rules](../classes/overview.md#healer-positioning)).

### Stepping Down Off Dais and Ledges

Dungeon navmeshes sometimes contain raised platforms or dais platforms (such as in Uldaman's Hall of the Keepers) that are not linked by stairs to the surrounding floor. Inside dungeons and raids, a bot that lands on an isolated dais steps off the edge: it searches for walkable floor 2–8 yards lower within 8 yards forward. The bot runs over the edge and drops down safely. It will not drop while actively taking melee hits. If no safe landing floor exists, use [`.bot summon <Name>`](commands/roster-and-hiring.md#bot-summon) out of combat.

The opposite case also happens: a short flight of stairs or a ramp that the navmesh does not link (for example the walkway up into Odo's room in Shadowfang Keep), so a following bot gets no path and stands still. Out of combat in a dungeon, when you are within 15 yards, in sight and at most 10 yards above (or 5 yards below) the bot, it takes one straight step of up to 12 yards toward you. It does this only when there is floor along the whole step close to the straight line between you, so it never walks through a wall or over a gap, and it tries again only every few seconds.

### Followers That Stand Still

When following out of combat inside instances, navmesh paths can occasionally stop responding if a bot is stuck on complex geometry. A bot that is more than 3 yards beyond its follow distance and has remained stationary for 2 seconds automatically attempts a local rescue walk to rejoin the leader.

### Bots in Deep Water

In deep water a bot swims a straight line to where it is going when the whole line is water and nothing is in the way. If it makes no progress, it swims up to the surface and to the nearest shore within 30 yards. A bot stuck in water out of combat can always be fetched with [`.bot summon <Name>`](commands/roster-and-hiring.md#bot-summon).

### Running Ahead Past Packs

Bots that are far behind follow the navmesh route to you. If your party skipped a pack in a narrow hallway, trailing bots can aggro it. When [`AiPlayerbot.DungeonFollowHoldBeforePacks`](../server/settings/party.md#aiplayerbotdungeonfollowholdbeforepacks) is enabled, any follower more than 30 yards behind stops outside the aggro range of unpulled packs and whispers `Pack in the way: <name>`. You can then clear the pack or walk back within 25 yards to resume follow.

### Elevators and Ledges

Bots do not jump autonomously inside dungeons or raids (the chase jump, follow jump, and RPG jump actions are disabled to prevent bots from landing on unpathable ledges). Wait for bots at the bottom of elevators or after ledge drops, then click **Summon** in `/tbm` or run [`.bot summon <Name>`](commands/roster-and-hiring.md#bot-summon) to regroup before engaging the next pack.

## Dungeon Loot & Supplies

### Loot Rolling Rules

- Your own alts and pool bots in your group take part in standard party loot rolls (`Need`, `Greed`, `Pass`). Hired companions do not: by default they never loot and always pass ([`AiPlayerbot.HireLoot`](../server/settings/hiring.md#aiplayerbothireloot) = `1` changes that).
- **Never Need Against a Player:** Genuine upgrades are Needed regardless of settings; both cases still wait for humans and step down to Greed/Pass if a human Needs. Bots hold their vote to give human players the full window, then vote once all humans have voted.
- **Empty Slot Filler:** When [`AiPlayerbot.RollBadItemsWithPlayer`](../server/settings/party.md#aiplayerbotrollbaditemswithplayer) is enabled, bots also roll Need on items that fill empty gear slots.
- **Whisper Overrides:** You can instruct a bot directly: whisper `/w <BotName> keep need [item]` before a roll or `/w <BotName> roll need [item]` while the roll is open. Whisper `/w <BotName> roll feedback` to have the bot report when it steps down. See [Whisper Commands](commands/whispers.md#inventory-items--banking).

### Reagents and Food Sharing

- Open trade with Mage or Warlock bots out of combat (`/w <Name> trade`). They automatically place conjured food, water, or healthstones into the trade window.
- To make an owned bot equip an item from its bags, whisper `/w <Name> equip [Item Link]`. See [Whisper Commands](commands/whispers.md#inventory-items--banking).

## Raid Tactics & Boss Mechanics

Entering a raid map automatically activates the dungeon transition engine, enabling raid strategies for Molten Core, Onyxia's Lair, Blackwing Lair, Naxxramas, Zul'Gurub, Ruins of Ahn'Qiraj, and Temple of Ahn'Qiraj.

Bots switch these strategies on by themselves when they enter the raid, also when they are summoned straight into a fight. A bot that was already online before an update may need a relog to pick up new raid strategies.

### Universal Raid Reactions

- **Bomb Runout:** Carriers of *Living Bomb* (Baron Geddon), *Burning Adrenaline* (Vaelastrasz), *Mutating Injection* (Grobbulus), or *Detonate Mana* (Kel'Thuzad) immediately run clear of the raid group ([`AiPlayerbot.BombRunoutDistance`](../server/settings/dungeons-raids.md#aiplayerbotbombrunoutdistance)). Ranged Grobbulus carriers run behind the boss.
- **Mutating Injection Cleansing Policy:** Healer disease cleansing spells intentionally skip Mutating Injection. Removing the debuff early drops a lethal poison cloud inside the raid; the carrier must run out and let the debuff expire harmlessly.
- **Living Bomb Spacing:** During the Baron Geddon fight, any bot standing within 10 yards of a Living Bomb carrier steps away to avoid the explosion.
- **Hazard Evasion:** Bots proactively step out of damaging ground effects, void zones, and trigger traps ([`AiPlayerbot.HazardEvasionDistance`](../server/settings/dungeons-raids.md#aiplayerbothazardevasiondistance)).
- **Living Poison Avoidance:** In Naxxramas, bots detect drifting Living Poison, step out inside 10 yd, and keep about 11 yd clear ([`AiPlayerbot.AvoidCreatureAuras`](../server/settings/dungeons-raids.md#aiplayerbotavoidcreatureauras)). Tanks step out as well because the explosion is fatal.
- **Dragon Positioning:** Non-tanks automatically flank dragons to avoid breath and tail sweep cones. Tanks can be ordered to turn dragons with [`.bot action raid tankface`](commands/party-actions.md#bot-action-raid-tankface).
- **Tank Assignment & Raid Assistant:** Main tanks and assist tanks coordinate boss and add assignments. Promoting an assist tank to **Raid Assistant** in the party frames assigns them secondary tank roles (such as holding Razorgore or Golemagg adds).
- **Ranged Spread:** Stacked ranged spellcasters step out to 10-12 yd to minimize chain damage. Wider spacing is opt-in only via `.bot behavior party spread on` (5 yd ranged / 2 yd melee defaults, tanks excluded).
- **Dispel Coordination:** Decursers and dispellers coordinate target claims to avoid casting multiple dispels on the same friendly target simultaneously.
- **Protection Potions:** Bots level 48+ automatically drink Greater Protection Potions on relevant bosses if carried in bags: Fire for Magmadar, Geddon, Golemagg, Ragnaros, Onyxia, Razorgore, and Vaelastrasz; Shadow for Lucifron, Gehennas, and Nefarian; Nature for Princess Huhuran and Viscidus.

### Specific Encounter Tactics

- **Molten Core:**
  - Douse runes using [`.bot action raid douse`](commands/party-actions.md#bot-action-raid-douse). During Baron Geddon, bots run out during *Inferno*.
  - *Garr & Shazzrah:* Garr holds DPS AoE off (Firesworn die single-target); Shazzrah pushes ranged out to 26 yd.
  - *Golemagg:* Melee back off at 20 Magma Splash stacks; assist tanks hold Core Ragers at fixed camps (30 yd Trust separation); healers work the camp midpoint; DPS leave ragers alone while Golemagg lives (burn below 10%).
- **Onyxia:** In Phase 2, bots switch to ranged attacks and spread out. Bots take cover in safe zones during *Deep Breath*. With [`AiPlayerbot.OnyxiaWhelpsAndEggs`](../server/settings/dungeons-raids.md#aiplayerbotonyxiawhelpsandeggs) enabled, ranged DPS clear hatched whelps while avoiding unbroken egg clusters.
- **Blackwing Lair:**
  - *Razorgore:* Bots assist the orb controller and intercept add waves without attacking the controller. Non-victims clear the frontal cone; assist tank holds the boss while eggs live.
  - *Vaelastrasz:* Tanks face away; Burning Adrenaline carriers flee from clean bots (tank holds).
  - *Broodlord & Nefarian:* Tanks face away; rogues disarm suppression devices in both combat and non-combat states. Mages use *Ice Block* to survive Nefarian Wild Magic. Bots whisper a warning if they lack the Onyxia Scale Cloak for Shadow Flame.
  - *Chromaggus:* Bots carrying Hourglass Sand (item 19183) use it to cleanse Bronze Affliction; bots step out of breath cones.
- **Naxxramas:**
  - *Anub'Rekhan:* Non-tanks collapse to the room center during Locust Swarm; non-tanks burn Crypt Guards first when no swarm is up; tank kite ring needs live waypoints (no bot kite tactic).
  - *Gluth:* Tanks rotate taunts at 5 Mortal Wound stacks; non-tank DPS execute sub-10% chow while tanks hold Gluth (kiting/slowing is the player's job; no bot kite ring).
  - *Thaddius:* Bots match Polarity Shift directions, shifting negative charges left and positive charges right.
  - *Loatheb:* Bots kill the spore underfoot for the Fungal Bloom buff and otherwise stay on Loatheb; no Necrotic Aura heal-timing exists in vanilla.
  - *Four Horsemen:* Mark carriers with 3+ stacks rotate clockwise to the next corner.
  - *Sapphiron:* Bots hide behind iceblocked players during the air phase and step out of blizzards.
  - *Heigan:* The group dances floor sections 0-1-2-3-2-1 during Plague Cloud.
  - *Kel'Thuzad:* Bots flee Shadow Fissures immediately; Frost Blast handling is not scripted beyond the shared bomb runout for Detonate Mana carriers.
- **Ahn'Qiraj (AQ20/AQ40):** Designated runners activate crystals while *Strength of Ossirian* is active. Kiting Ossirian remains the player's responsibility.
- **Viscidus (AQ40, opt-in):** `.bot strategy +viscidus glob focus @all` (off by default; only active on map 531 in an already engaged Viscidus fight). Mages use rank-one Frostbolt to freeze him and stop frost casts once frozen; melee and ranged physical damage keep hitting the frozen boss. After the shatter all DPS switch to the nearest-to-merging glob while tanks keep the boss and healers keep healing; tanks retake him after the globs re-form (a spare taunts if no tank holds him), and ranged/healers back out of his melee reach. Every role steps out of the poison cloud. Remove with `.bot strategy -viscidus glob focus @all`.
- **Paladin Aura Auto-Swap:** Paladins in raids automatically swap to *Fire Resistance Aura* or *Shadow Resistance Aura* on applicable bosses.

### Fankriss (opt-in roles)

Fankriss the Unyielding (Temple of Ahn'Qiraj) has opt-in roles, all off by default. A raid that sets none of them fights him as before. Give two warrior or bear tanks the boss role, optionally a third tank the spawn role, then the rest:

```text
.bot strategy +fankriss tank TankOne
.bot strategy +fankriss tank TankTwo
.bot strategy +fankriss spawn tank SpareTank
.bot strategy +fankriss spawn focus @ranged
.bot strategy +fankriss stack @melee
.bot strategy +fankriss healing @heal
```

- **`fankriss tank`:** the tank stays on Fankriss instead of picking loose adds. When the current tank reaches [`AiPlayerbot.FankrissSwapStacks`](../server/settings/dungeons-raids.md#aiplayerbotfankrissswapstacks) Mortal Wound stacks (default 5), one assigned tank with no stacks taunts (warrior Taunt, druid Growl); the other assigned tanks' automatic taunts on the boss are held. With `fankriss stack` as well (`@melee` includes tanks), the boss tanks hold him at his pull centre.
- **`fankriss spawn tank`:** a spare tank picks up loose Spawns of Fankriss near the raid and taunts one that is not on a tank. The boss role wins if a tank has both.
- **`fankriss spawn focus`** (DPS) and **`fankriss stack`** (melee DPS): the raid kills one shared Spawn at a time (lowest health, then the earliest to reach the raid) before going back to the boss, and from 10 s after a Spawn joins the fight a DPS with a stun or slow uses it on it. While Fankriss is engaged these DPS use single-target damage only (area, chain and swing-copy spells are blocked and running ones stopped), ignore the other bugs in the room except one attacking a healer (the three nearest DPS within 20 yd kill it), and do not chase into the tunnels. `fankriss stack` also keeps melee packed behind the boss.
- **`fankriss healing`:** a healer moved away by Entangle heals whoever it can reach from where it stands instead of walking back first.

An attack order, a raid mark on another enemy, or `stay`/`follow` still wins over the tank and stack roles. Remove a role with `-` instead of `+`.

### Patchwerk (opt-in roles)

Patchwerk (Naxxramas) has opt-in roles, all off by default. The tested set-up is four warrior or bear tanks on `patchwerk soak`, a ranged raid, and `patchwerk melee safety` on the DPS:

```text
.bot strategy -patchwerk main @tank
.bot strategy +patchwerk soak @tank
.bot strategy +patchwerk melee safety @dps
```

- **`patchwerk soak`** (tanks): the tank goes to Patchwerk as soon as he engages a raid member, in Defensive Stance or bear form, even while on `stay`, `follow` or a wait order (only for this encounter; the saved orders and stay point resume after he dies or resets). Soakers never taunt him. The first assigned tank holding him is the main; when it dies the surviving tank with the most threat takes over.
- **Healers** (no strategy needed): while assigned tanks fight him, the raid's healers are seated on the tanks in guid order, the main first (ten healers on four tanks heal 3/3/2/2), one healer per tank may heal before a hit, and while no assigned tank is hurt a priest pre-shields an engaged soaker. Seated healers cast no damage filler (Smite, Holy Fire, a wand) during the fight.
- **DPS:** they hold for at least 4 s and until the tanks have built threat (the holder 10,000, the weakest tank 5,000; melee wait for all four tanks), then keep their threat below 70 % of the lowest tank (resuming below 60 %). With a `patchwerk main` assigned they also hold for [`AiPlayerbot.PatchwerkOpeningDelay`](../server/settings/dungeons-raids.md#aiplayerbotpatchwerkopeningdelay) seconds from its engage. Melee with `patchwerk melee safety` step in only while the main and three soakers fill the four Hateful Strike threat slots, attack from behind, and back out when that stops being true.
- **`patchwerk main`** (optional, one of the four tanks *instead of* `patchwerk soak`: `.bot strategy -patchwerk soak TankName`, then `+patchwerk main TankName`; a tank with both roles has neither): on an ordered `.bot action pull` the soakers first gather beside this tank in stance and the shot waits for them (the normal 15 s pull window still ends it).

An attack order or raid mark on another enemy still wins over the tank roles. Remove a role with `-` instead of `+`.

## Custom Turtle Raids

When [`AiPlayerbot.EnableCustomRaidTactics`](../server/settings/dungeons-raids.md#aiplayerbotenablecustomraidtactics) is enabled, the raid engine handles custom Turtle WoW raid encounters:

- **Emerald Sanctum (Solnius):** Tanks keep the dragon facing away to avoid Acid Breath. Emerald Rot carriers run out via the shared bomb runout action. When Call of Nightmare triggers, DPS bots focus down incoming add waves.
- **Lower Karazhan (Araxxna & Moroes):** Araxxna's spiderling swarms trigger AoE cleave. Moroes' smoke bomb forces tanks and DPS to re-acquire their targets.
- **Karazhan Crypt:** Set [`.bot formation near`](commands/party-actions.md#bot-formation) before entering narrow tunnels to keep bots from clipping through walls. Levers and puzzle doors must be operated manually by the player.
