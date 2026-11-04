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
| **Tank (experimental)** | Tank | Shield and Rockbiter Weapon, Earth Shock threat, Earthshaker Slam taunt, Stoneclaw on packs. A Turtle WoW meme spec, offered for players who want to try it unless [`AiPlayerbot.ShamanTankEnabled`](../server/settings/hiring.md#aiplayerbotshamantankenabled) is `0`. |

---

## What the bot does in combat

### Restoration (Healer)
- Casts *Chain Heal* when several nearby allies are injured.
- Holds *Spirit Link* until the tank drops to at or below the low-health line (50% default), then links at once as the save.
- Banks *Ancestral Swiftness* on a critically injured ally, then spends it on an instant *Healing Wave*.
- Deals off-spec damage (*Flame Shock*, *Lightning Bolt*, *Chain Lightning* into packs) per [healer off-DPS rules](overview.md#shared-bot-behaviors).

### Enhancement (Melee DPS)
- Fights in melee, keeping weapon imbues active.
- Casts *Stormstrike* before *Lightning Strike* to amplify nature burst.
- Cycles *Flame Shock* for periodic damage and *Earth Shock* for burst damage.
- Triggers *Bloodlust*: self frenzy; party melee crits spread the haste across the group.
- Off-heals allies at 20–50% health with *Healing Wave* per [off-healing rules](overview.md#shared-bot-behaviors).

### Elemental (Ranged DPS)
- Attacks from range with *Lightning Bolt* and casts *Chain Lightning* against enemy packs.
- Keeps *Water Shield* active for mana regeneration (falling back to *Lightning Shield* before training it).
- Activates *Elemental Mastery* as a damage cooldown.
- Saves *Earth Shock* as a finishing execute below 25% health and below 1500 absolute HP while reserving interrupts for casting targets.
- Respects [party threat back-off](overview.md#shared-bot-behaviors) on *Chain Lightning* when holding 50% or more of tank threat. *Earthquake* only backs off at high single-target threat (80%+), since it is not typed as an AoE threat spell.
- Off-heals allies at 20–50% health with *Healing Wave* per [off-healing rules](overview.md#shared-bot-behaviors).

### Tank (experimental)
- **Setup:** one-handed axe, mace or fist weapon with *Rockbiter Weapon* (the shaman tank's threat stance), a shield, and *Lightning Shield* for the pull. Switches to *Water Shield* at about two thirds mana (66%).
- **Pull:** *Chain Lightning* when the pull has 2 or more mobs, *Lightning Bolt* on a single mob. A tank shaman does not start a pull below 80% mana; it holds the pull until mana recovers.
- **Threat:** on bosses and elites, *Earth Shock* goes out on cooldown, followed by *Stormstrike* and *Lightning Strike*. On normal trash the bot saves mana: the strikes and auto-attack always run, and *Earth Shock* goes out only when another member catches up on threat or a mob is loose. Auto-attack never stops.
- **Taunt:** *Earthshaker Slam* (Turtle taunt) when a mob attacks someone else, with *Earth Shock* behind it. Tauren also use *Totemic Slam* as a damage plus slow filler while both strikes cool down — it is not a taunt.
- **Packs:** *Stoneclaw Totem* on trash packs when threat is needed or 3 or more attack. *Fire Nova Totem* on packs of 2 or more (the tank's fire pick: it outranks the *Searing* filler, and nothing replaces the totem while it counts down), and *Magma Totem* only as the fallback while *Fire Nova* is not ready and 3 or more mobs are still up with mana at or above 25%.
- **Earth totem:** *Strength of Earth* on bosses and elites, *Stoneclaw* on trash. The automatic pick never Stoneskins on a fight; a manual player totem order still wins. The fight decision is stable for the whole fight (any engaged elite keeps it a fight), so switching targets between an elite and its adds never swaps the totem.
- **Does not:** spend the shock cooldown on *Flame Shock*, keep *Earth Shield* on itself, or leave its target to place a totem.

<details>
<summary>Totem orchestration details</summary>

Shamans maintain 4-element totem sets tailored to group composition:
- **Earth Totem:** Drops *Strength of Earth Totem* if melee allies are present and the spell is known; otherwise the slot stays empty. Reacts to fears with *Tremor Totem* and to runners with *Earthbind Totem*. A tank shaman drops no earth totem out of combat: the earth pick depends on the fight type, so it is decided once the fight starts.
- **Fire Totem:** Drops *Searing Totem* on single targets; drops *Fire Nova Totem* on packs in detonation range (*Magma Totem* only while Nova cools down). A standing *Fire Nova Totem* counts as the slot being taken until it detonates: any fire totem dropped over it would unsummon it before its blast.
- **Water Totem:** Drops *Mana Spring Totem* if mana users are present; non-tank kits drop *Healing Stream Totem* otherwise. Reacts to poison and disease with cleansing totems.
- **Air Totem:** Drops *Windfury Totem* for melee groups; drops *Tranquil Air Totem* or *Grace of Air Totem* for casters. Reacts to incoming enemy spells with *Grounding Totem*.
- **Out of Combat:** Totems drop in combat and in the pre-pull window (ready check / rebuff) only — never otherwise out of combat, so a following bot leaves no totems behind in town. After the fight *Totemic Recall* refunds the standing set, and because nothing re-drops it out of combat the refund sticks until the next fight. `.bot totem` `mode manual` quiets the automatic rows (except *Stoneclaw Totem*, which still auto-drops on trash), and the reactive rows (*Tremor*, cleansing, *Grounding*, *Earthbind*) still fire on need.

</details>

---

## Turtle WoW custom spells

- **Earthquake:** Elemental AoE spell causing Nature damage and knockdown pulses.
- **Lightning Strike:** Instant melee strike releasing active shields for heavy Nature damage.
- **Spirit Link:** Restoration talent linking party health pools to distribute spike tank damage evenly.
- **Ancestral Swiftness:** Emergency instant-cast cooldown paired with *Healing Wave*.
- **Bloodlust:** Enhancement haste ability whose attack speed buff spreads through melee crits.

---

## Utility & interrupts

- **Interrupts:** Casts *Earth Shock* to interrupt dangerous enemy spellcasters and healing mobs.
- **Weapon Imbues:** Enhancement prioritizes *Windfury Weapon*, others *Flametongue Weapon*; first-known fallback is *Frostbrand Weapon*, then *Rockbiter Weapon*. A tank shaman always keeps *Rockbiter Weapon*.
- **Dispels:** Strips enemy buffs with *Purge* and cleanses allies using *Cure Poison* and *Cure Disease*.
- **Earth Shield:** Maintains *Earth Shield* on the group tank in the raid buff strategy.
- **Resurrection:** Revives dead allies out of combat with *Ancestral Spirit*.
- **Ghost Wolf:** Shifts to *Ghost Wolf* when carrying a flag in PvP.
- **Player Totem Controls:** Players can customize totem sets via [`.bot totem`](../play/commands/bot-panel.md#bot-totem) or manage them with `drop` and `status`.

---

## Talent builds

Bots learn pre-set talent orders at 5-level checkpoints (levels 10–60):

| Spec Name | Config ID | Role | 60 Allocation | Key Signatures & Synergies |
| :--- | :--- | :--- | :--- | :--- |
| **Restoration** | `7.0` | Healer | `13 / 0 / 38` | Ancestral Swiftness, Spirit Link, Improved Chain Heal, Water Shield, Elemental Focus dip. |
| **Enhancement** | `7.1` | Melee DPS | `17 / 34 / 0` | Stormstrike, Bloodlust, Windfury synergy, Flurry, Elemental Devastation. |
| **Elemental** | `7.2` | Ranged DPS | `42 / 0 / 9` | Elemental Mastery, Earthquake, Call of Thunder, Lightning Mastery, Resto efficiency dip. |
| **Tank** (experimental) | `7.3` | Tank | `15 / 36 / 0` | Spirit Armor, Totemic Alignment, Ancestral Guardian, Stormstrike, Bloodlust, Elemental Warding. No Shield Specialization. |

<details>
<summary>Leveling milestones and progression rationale</summary>

- **Restoration (`7.0`):** Early levels build heal throughput (*Improved Healing Wave*, *Tidal Focus*, *Ancestral Healing*), reaching *Ancestral Swiftness* (40) and *Spirit Link* (50), with an Elemental dip for efficiency.
- **Enhancement (`7.1`):** Early levels build melee damage (*Ancestral Knowledge*, *Thundering Strikes*, *Lightning Strike*), reaching *Flurry* (30), *Stormstrike* (40), and *Bloodlust* (50), with an Elemental dip for crit synergy.
- **Elemental (`7.2`):** Early levels build cast damage (*Convection*, *Concussion*), reaching *Elemental Mastery* (40) and *Earthquake* (45), with a Restoration dip for efficiency.
- **Tank (`7.3`):** The community shaman tank levelling order, spending early points in an allocation no other Shaman premade uses: *Lightning Strike* (20), *Spirit Armor*, *Totemic Alignment*, *Stormstrike* (31), *Elemental Focus*, *Flurry*, *Bloodlust* (56). First key tank talent at 25.

</details>

---

## Class settings

**How often random bots roll the tank spec:** `AiPlayerbot.PremadeSpecProb.7.3` is the tank's weight in the shaman spec draw (the other three specs ship at 100, the tank at 16, so about 5% of pool shamans). `0` turns the tank spec off for random bots; hiring it is unaffected by this key.

**Shaman tank hiring switch:** [`AiPlayerbot.ShamanTankEnabled`](../server/settings/hiring.md#aiplayerbotshamantankenabled) (`1` default) offers the tank build at recruiters, by `.bot hire`, and to pool shamans. `0` withholds it from hiring, raids, and pool rolls. Bots that already run the tank build keep it.

**When a shaman counts as a tank:** a bot with the tank spec or tank role always does. A player shaman counts as a tank when wearing a shield, with *Rockbiter Weapon* on the main hand and *Totemic Alignment* or *Spirit Armor* learned (from about level 25 with the tank levelling order), or after [`.bot role self tank`](../play/commands/roster-and-hiring.md#bot-role). A shaman tank also counts as a tank next to a warrior, paladin or druid tank. Hunter and warlock pets then stop taunting, healers treat the shaman as the tank, and the other bots keep their threat below it.

**A bot that rolled the tank spec stays a tank.** The tank build is identified by its own talents — *Totemic Alignment* or *Spirit Armor* learned; only the `7.3` premade allocates either — so it is read from the character rather than from whatever role was handed out for the moment. A pool shaman that rolled `7.3` therefore keeps tanking across a server restart, after leaving a group, and after a dungeon-finder fill ends, all of which drop the in-memory role. Below level 25, where neither key talent is learned yet, the levelling layout carries the same identity: the `7.3` order spends its points in an allocation no other Shaman premade uses, so the spent talents identify it from level 15 on, and the dungeon finder takes it as a tank. Levels 10–14 share the Enhancement opener and run the Enhancement kit until the layouts diverge.
