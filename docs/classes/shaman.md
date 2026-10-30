---
id: classes-shaman
title: Shaman Bot Guide
category: classes
summary: Complete guide to Shaman bot AI, totem sets, weapon imbues, chain heals, shocks, the experimental shaman tank, and Turtle WoW custom abilities.
tags: [classes, shaman, healer, dps, tank, totems, melee]
relates_to:
  - classes-overview
---

# Shaman Bot Guide

Shamans provide unmatched group utility through versatile totem sets, elemental shocks, weapon imbues, and potent chain spells across Melee DPS, Caster DPS, and Healing roles.

## Specs & roles

| Spec | Role | Primary Focus |
| :--- | :--- | :--- |
| **Restoration** | Healer | Chain Heal throughput, single-target wave downranking, and totem support. |
| **Enhancement** | Melee DPS | Front-line melee damage, Windfury burst, Stormstrike, and totem utility. |
| **Elemental** | Ranged DPS | High-damage Lightning Bolt and Chain Lightning spells with instant crits. |
| **Tank (experimental)** | Tank | Shield and Rockbiter Weapon, Earth Shock threat, Earthshaker Slam taunt, Stoneclaw on packs. A Turtle WoW meme spec, offered for players who want to try it unless [`AiPlayerbot.ShamanTankEnabled`](../server/settings/hiring.md#aiplayerbotshamantankenabled) is `0` ([#1125](https://github.com/Sagiroth/TortoiseBots/issues/1125)). |

---

## What the bot does in combat

### Restoration (Healer)
- Casts *Chain Heal* when several nearby allies are injured.
- Holds *Spirit Link* until the tank drops to half health, then links at once as the save. Rotation reference: stevemcqueenz/turtle-knowledge-base- (Turtle WoW class guides).
- Holds *Ancestral Swiftness* for a dying target instead of popping it early, then spends it on an instant *Healing Wave*. Rotation reference: stevemcqueenz/turtle-knowledge-base- (Turtle WoW class guides).
- Employs *Ancestral Swiftness* to instantly land emergency *Healing Wave* casts on dying targets.
- Deals off-spec damage (*Flame Shock*, *Lightning Bolt*, *Chain Lightning* into packs) per [healer off-DPS rules](overview.md#healer-off-dps-offdps).

### Enhancement (Melee DPS)
- Fights in melee using dual-wield or two-handed weapons, keeping weapon imbues active.
- Casts *Stormstrike* before *Lightning Strike* to amplify nature burst.
- Cycles *Flame Shock* for periodic damage and *Earth Shock* for burst damage.
- Triggers *Bloodlust* (custom Turtle ability): self frenzy; party melee crits spread the haste across the group.
- Off-heals allies at 20–50% health with *Healing Wave* per [off-healing rules](overview.md#dps-off-healing-offheal).

### Elemental (Ranged DPS)
- Attacks from range with *Lightning Bolt* and casts *Chain Lightning* against enemy packs.
- Keeps *Water Shield* active for mana regeneration (*Lightning Shield* before training).
- Activates *Elemental Mastery* for guaranteed critical strikes.
- Saves *Earth Shock* as a finishing execute below 25% health and below 1500 absolute HP while reserving interrupts for casting targets.
- Respects [party threat back-off](overview.md#party-threat-back-off) on *Chain Lightning* and *Earthquake* when holding ≥50% tank threat.
- Off-heals allies at 20–50% health with *Healing Wave* per [off-healing rules](overview.md#dps-off-healing-offheal).

### Tank (experimental)
- **Setup:** slow one-handed axe, mace or fist weapon with *Rockbiter Weapon* (the shaman tank's threat stance: +35% threat), a shield, and *Lightning Shield* for the pull. Switches to *Water Shield* at about two thirds mana.
- **Pull:** *Chain Lightning* when the pull has 2 or more mobs, *Lightning Bolt* on a single mob. A tank shaman does not start a pull below 80% mana; it drinks first.
- **Threat:** on bosses and elites, *Earth Shock* goes out on cooldown, followed by *Stormstrike* and *Lightning Strike*. On normal trash the bot saves mana: the strikes and auto-attack always run, and *Earth Shock* goes out only when another member catches up on threat or a mob is loose. Auto-attack never stops, because *Earthen Bulwark* builds from white hits.
- **Taunt:** *Earthshaker Slam* (Turtle taunt, trained at 10) when a mob attacks someone else, with *Earth Shock* behind it. Tauren also use *Totemic Slam*.
- **Packs:** *Stoneclaw Totem* on trash packs (with *Totemic Alignment* most of its threat goes to the shaman), *Fire Nova Totem* on packs of 2 or more (the tank's fire pick: it outranks the *Searing* filler, and nothing replaces the totem while it counts down), and *Magma Totem* only as the fallback while *Fire Nova* is on its 15 s cooldown and 3 or more mobs are still up, because it drains mana.
- **Earth totem:** *Strength of Earth* on bosses and elites, *Stoneclaw* on trash. Never *Stoneskin* on bosses. The fight decision is stable for the whole fight (any engaged elite keeps it a fight), so switching targets between an elite and its adds never swaps the totem; each pick owns the earth slot: the fight pick replaces the tank's own *Stoneclaw* from the pack just fought, and a pack takes the slot back from *Strength of Earth* or *Earthbind* — a leftover totem from the other fight type never locks the slot out.
- **Does not:** spend the shock cooldown on *Flame Shock*, keep *Earth Shield* on itself, or leave its target to place a totem.

<details>
<summary>Totem orchestration details</summary>

Shamans maintain 4-element totem sets tailored to group composition:
- **Earth Totem:** Drops *Strength of Earth Totem* if melee allies are present; otherwise leaves the slot open (*Stoneskin Totem* is the fallback when Strength is unknown). Reacts to fears with *Tremor Totem* and to runners with *Earthbind Totem*. A tank shaman drops no earth totem out of combat: the earth pick depends on the fight type, so it is decided once the fight starts.
- **Fire Totem:** Drops *Searing Totem* on single targets; drops *Fire Nova Totem* on packs in detonation range (*Magma Totem* only while Nova cools down). A standing *Fire Nova Totem* counts as the slot being taken until it detonates: any fire totem dropped over it would unsummon it before its blast.
- **Water Totem:** Drops *Mana Spring Totem* if mana users are present; drops *Healing Stream Totem* otherwise. Reacts to poison and disease with cleansing totems.
- **Air Totem:** Drops *Windfury Totem* for melee groups; drops *Tranquil Air Totem* or *Grace of Air Totem* for casters. Reacts to incoming enemy spells with *Grounding Totem*.
- **Out of Combat:** Totems drop in combat and in the pre-pull window (ready check / rebuff) only — never otherwise out of combat, so a following bot leaves no totems behind in town. After the fight *Totemic Recall* refunds the standing set, and because nothing re-drops it out of combat the refund sticks until the next fight. `.bot totem` `mode manual` drops nothing automatically (use `drop` to place a set), and the reactive rows (*Tremor*, cleansing, *Grounding*, *Earthbind*) still fire on need.

</details>

---

## Turtle WoW custom spells

- **Earthquake (Spell ID 48306):** Elemental AoE spell causing Nature damage and knockdown pulses.
- **Lightning Strike (Spell ID 51387):** Instant melee strike releasing active shields for heavy Nature damage.
- **Spirit Link (Spell ID 51363):** Restoration talent linking party health pools to distribute spike tank damage evenly.
- **Ancestral Swiftness (Spell ID 16188):** Emergency instant-cast cooldown paired with *Healing Wave*.
- **Bloodlust (Spell ID 45509):** Enhancement haste ability whose attack speed buff spreads through melee crits.

---

## Utility & interrupts

- **Interrupts:** Casts *Earth Shock* to interrupt dangerous enemy spellcasters and healing mobs.
- **Weapon Imbues:** Maintains *Windfury Weapon* and *Flametongue Weapon* (Enhancement) or *Flametongue Weapon* (Elemental/Resto), falling back to *Rockbiter Weapon* or *Frostbrand Weapon* when those are not known. A tank shaman always keeps *Rockbiter Weapon*.
- **Dispels:** Strips enemy buffs with *Purge* and cleanses allies using *Cure Poison* and *Cure Disease*.
- **Earth Shield:** Maintains *Earth Shield* on the group tank for reactive healing.
- **Resurrection:** Revives dead allies out of combat with *Ancestral Spirit*.
- **Ghost Wolf:** Shifts to *Ghost Wolf* when carrying a flag in PvP.
- **Player Totem Controls:** Players can customize totem sets via [`.bot totem`](../play/commands/bot-panel.md#bot-totem) or manage them with `drop` and `status`.

---

## Talent builds

TortoiseBots provides pre-configured talent progressions at 5-level checkpoints (levels 10–60) tailored for Turtle WoW 1.18.1:

| Spec Name | Config ID | Role | 60 Allocation | Key Signatures & Synergies |
| :--- | :--- | :--- | :--- | :--- |
| **Restoration** | `7.0` | Healer | `13 / 0 / 38` | Ancestral Swiftness, Spirit Link, Improved Chain Heal, Water Shield, Elemental Focus dip. |
| **Enhancement** | `7.1` | Melee DPS | `17 / 34 / 0` | Stormstrike, Bloodlust, Elemental Weapons (+40% Windfury AP), Flurry, Elemental Devastation. |
| **Elemental** | `7.2` | Ranged DPS | `42 / 0 / 9` | Elemental Mastery, Earthquake, Call of Thunder, Lightning Mastery, Resto efficiency dip. |
| **Tank** (experimental) | `7.3` | Tank | `15 / 36 / 0` | Spirit Armor, Totemic Alignment, Ancestral Guardian, Elemental Weapons (Earthen Bulwark), Stormstrike, Bloodlust, Elemental Warding. No Shield Specialization. |

<details>
<summary>Leveling milestones and progression rationale</summary>

- **Restoration (`7.0`):**
  - *Levels 10–35:* Takes *Improved Healing Wave* (5/5), *Tidal Focus* (5/5), *Ancestral Healing* (3/3), *Healing Way* (3/3), and *Restorative Totems* (5/5).
  - *Levels 35–50:* Takes *Improved Water Shield* (3/3), *Ancestral Swiftness* (40), *Tidal Surge* (2/2), and *Spirit Link* (50).
  - *Levels 50–60:* Dips into Elemental for *Convection* (5/5), *Earth's Grasp* (2/2), and *Elemental Focus* (1/1).
- **Enhancement (`7.1`):**
  - *Levels 10–35:* Takes *Ancestral Knowledge* (5/5), *Thundering Strikes* (5/5), *Stable Shields* (3/3), *Lightning Strike* (1/1), and *Flurry* (30).
  - *Levels 35–50:* Takes *Elemental Weapons* (3/3), *Enhancing Totems* (2/2), *Stormstrike* (40), and *Bloodlust* (50).
  - *Levels 50–60:* Dips into Elemental for *Convection* (5/5), *Concussion* (5/5), *Elemental Focus* (1/1), and *Elemental Devastation* (3/3).
- **Elemental (`7.2`):**
  - *Levels 10–40:* Takes *Convection* (5/5), *Concussion* (5/5), *Elemental Focus* (1/1), *Call of Thunder* (5/5), *Call of Flame* (3/3), *Storm Reach* (2/2), and *Elemental Mastery* (40).
  - *Levels 40–50:* Takes *Lightning Mastery* (5/5) and *Earthquake* (45).
  - *Levels 50–60:* Dips into Restoration for *Improved Healing Wave* (5/5) and *Tidal Focus* (4/5).
- **Tank (`7.3`)**, the community shaman tank levelling order:
  - *Levels 10–24:* *Ancestral Knowledge* (5/5), *Stable Shields* (3/3), *Thundering Strikes* (2/5), *Lightning Strike* (20), *Ancestral Guardian* (3/3), *Thundering Strikes* (3/5).
  - *Levels 25–33:* *Spirit Armor* (2/2), *Thundering Strikes* (5/5), *Totemic Alignment* (1/2), *Elemental Weapons* (3/3), *Stormstrike* (31).
  - *Levels 34–45:* Elemental *Convection* (5/5), *Elemental Warding* (2/3), *Elemental Devastation* (3/3), *Elemental Focus* (1/1), then *Totemic Alignment* (2/2).
  - *Levels 46–60:* *Element's Grace* (5/5), *Flurry* (5/5), *Bloodlust* (56), *Reverberation* (3/3), *Elemental Warding* (3/3).

</details>

---

## Class settings

There are no class-specific configuration flags for Shamans. General combat and resting settings are configured in [Combat settings](../server/settings/combat.md).

**How often random bots roll the tank spec:** `AiPlayerbot.PremadeSpecProb.7.3` is the tank's weight in the shaman spec draw (the other three specs ship at 100, the tank at 16, so about 5% of pool shamans). `0` turns the tank spec off for random bots; hiring it is unaffected by this key. [`AiPlayerbot.ShamanTankEnabled`](../server/settings/hiring.md#aiplayerbotshamantankenabled) `= 0` withholds it from hiring as well.

**When a shaman counts as a tank:** a bot with the tank spec or tank role always does. A player shaman counts as a tank when wearing a shield, with *Rockbiter Weapon* on the main hand and *Totemic Alignment* or *Spirit Armor* learned (from about level 25 with the tank levelling order), or after [`.bot role self tank`](../play/commands/roster-and-hiring.md#bot-role). A shaman tank also counts as a tank next to a warrior, paladin or druid tank. Hunter and warlock pets then stop taunting, healers treat the shaman as the tank, and the other bots keep their threat below it.

**A bot that rolled the tank spec stays a tank.** The tank build is identified by its own talents — *Totemic Alignment* or *Spirit Armor* learned; only the `7.3` premade allocates either — so it is read from the character rather than from whatever role was handed out for the moment. A pool shaman that rolled `7.3` therefore keeps tanking across a server restart, after leaving a group, and after a dungeon-finder fill ends, all of which drop the in-memory role. Below level 25, where neither key talent is learned yet, the levelling layout carries the same identity: the `7.3` order spends its points in an allocation no other Shaman premade uses, so the spent talents identify it from level 15 on, and the dungeon finder takes it as a tank. Levels 10–14 ship the same layout for `7.3` and `7.1` (enhancement), so such a bot plays the Enhancement kit there — the character cannot tell the two apart. An explicit [`.bot role`](../play/commands/roster-and-hiring.md#bot-role) order always wins at any level — `.bot role <bot> dps` keeps the DPS kit on a tank-built shaman, while `.bot role <bot> clear` hands it back to its build. A dungeon-finder fill that temporarily forces a role on a DPS shaman is unaffected: without the tank talents that bot reads as damage, and its own kit returns when the fill ends.
