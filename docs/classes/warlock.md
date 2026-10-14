---
id: class-warlock
title: Warlock Bot AI & Specs
category: classes
summary: Deep dive into Warlock DoT spreading, demon summoning, Soul Shard economy, Life Tap, and custom Dark Harvest / Power Overwhelming.
tags: [class, warlock, dps, ranged, pet]
relates_to:
  - class-overview
  - guide-player-controls
---

# Warlock Bot AI & Specs

Warlocks provide sustained Shadow and Fire DPS through curses and damage-over-time (DoT) spells, unique pet summons, Healthstones, and crowd control.

## Supported Specs & Roles

- **Affliction (Ranged DPS):** Dominant DoT dealer with *Corruption*, *Curse of Agony*, *Siphon Life*, and *Drain Life*.
- **Demonology (Pet DPS / Tanky):** Heavy pet empowerment, *Soul Link*, *Demonic Sacrifice*, and high durability. Keeps *Immolate* up like the other specs.
- **Destruction (Burst DPS):** Fire burst nuke specialist utilizing *Shadow Bolt*, *Immolate*, and *Conflagrate* (no *Searing Pain* wiring in the Destruction strategies).

---

## Combat Rotations & Priorities

### 1. DoT Upkeep & Shard Economy
- **Immune targets:** Damage spells (*Immolate*, *Shadow Bolt*, *Corruption*, ...) are skipped on a target immune to their school (e.g. *Immolate* on a fire-immune mob), and the rotation falls through to the next row.
- **DoTs:** *Corruption* and *Siphon Life* are skipped at/below 20% target health; *Immolate* has no health gate and is kept up by all three specs. Affliction also maintains *Siphon Life*. Spreading to extra attackers: under the aoe strategy, *Corruption* and *Immolate* spread in every spec; at spec level, Affliction spreads *Corruption* and *Siphon Life*, Demonology spreads *Corruption* and *Immolate*, and Destruction spreads *Immolate*, even with the aoe strategy off. Spreading runs below single-target upkeep and mana recovery, under the 16-debuff cap. *Corruption*, *Immolate* and *Siphon Life* (including the spread-to-attackers rows) check for the bot's OWN aura on the target, because the core stacks periodic damage per caster; a second warlock therefore still dots a mob the first one dotted. Curses are not per-caster (one curse per target, a same-rank curse from another warlock replaces it), so *Curse of Agony* and the curse picker keep their existing rules.
- **Curses:** Affliction and Demonology default to *Curse of Agony*; Destruction defaults to *Curse of the Elements* once trained (level 32), and to *Curse of Agony* before that. Other curses only when you enable them manually (`.bot strategy +curse <shadow|weakness|...|exhaustion>` still overrides). *Curse of Exhaustion* snares runners and kiters via the snare target. Different curses from different warlocks stack; the bot never recasts a curse anyone already holds.
- **Soul Shard Harvest:** Shards are never seeded or conjured — the bot harvests them organically with *Drain Soul* when the target is at/below 20% health, provided it holds fewer than 5 Soul Shards and has bag space; no elite check. Excess above 5 is destroyed out of combat. Healthstones (*Create Healthstone*, level 10+) and Soulstones (*Create Soulstone (Minor)*, level 18+) are created out of combat whenever the bot holds a shard and lacks one.

### 2. Mana Management: Life Tap
- Affliction also casts *Dark Pact* on low mana, so *Life Tap* is not the only mana tool.
- Warlocks dynamically cast *Life Tap* to convert surplus health into mana.
- Safety check: both bands need health above the low-health line (default 50). The urgent band fires at/below the medium-mana line (default 40) in combat and between pulls; the top-up band (below 85% mana) only pre-taps between pulls — never in combat, where any trigger row would outrank the shadow-bolt default (engine pushes defaults at relevance-200) and tap instead of nuking. Out of combat it eats first (both bands stay quiet while a food/drink aura is up) and taps once the meal ends, so no sit/stand ping-pong, so the bot enters the fight near-full mana. Affliction still prefers *Dark Pact* on low mana. No incoming-damage check exists.
- **Wand only without mana (Shoot, Spell ID 5019):** On Turtle every wand shot puts the global cooldown on the shooter for the wand's whole swing, and the next shot renews it, so a running wand keeps *Shadow Bolt*, *Life Tap* and every other warlock spell "not ready". Before, a warlock that started wanding below the 5% `no mana` line (or on a wand pull) kept wanding after its mana came back, until it moved or the fight ended. Now the `wand blocks nuke` trigger stops the wand (`stop wand`, below the HIGH tier) once *Shadow Bolt* is held back by that GCD alone, waits out the last shot's GCD and casts again. With too little mana for *Shadow Bolt* or below the 5% line the wand keeps going; in a raid group it also stays while the warlock holds aggro (the raid `has aggro` → *Shoot* row), and while the warlock is silenced, locked out of the Shadow school, or out of line of sight of the target. It also stays while the `threat` strategy holds *Shadow Bolt* back (`high threat`: near the tank's threat on the target, or the tank has none there yet): stopping it then only left the warlock idle, or restarted the wand a swing later. The trigger is checked every 1-2 s, not every tick.
- **Wand stops for Banish and Fear:** the same GCD trap keeps *Banish* and *Fear* (their rows need the spell ready) from going out. With the `cc` strategy, the `wand blocks cc` trigger stops the wand (`stop wand`, above both rows) while the warlock has a *Banish* or *Fear* target waiting within the spell's range (Fear only ever on its own raid mark) and the spell is held back by that GCD alone. While that target waits, the warlock does not start the wand at all. *Seduction* is cast by the succubus, so the warlock's wand never blocks it. Same exceptions as the nuke stop (5% `no mana` line, raid aggro, silence, school lockout, line of sight).

---

## Turtle WoW 1.18.1 Custom Content

- **Dark Harvest (Spell ID 52550, per game data):**
  - Custom Turtle WoW Affliction talent requiring 2+ active own DoTs on the target.
  - Deals rapid Shadow damage with a 30-second cooldown that is automatically refunded if the target dies while afflicted, per game data.
- **Power Overwhelming (Spell ID 51714, per game data):**
  - Custom Demonology pet burst (crowd-control break plus damage buff) costing the demon a share of base health over the duration, per game data. The bot requires a live pet above 60% health and a live enemy.
- **Rain of Fire Channeling:**
  - Includes safe channel cancellation when fewer than 2 enemies remain in the AoE.
  -In a group with the `threat` strategy it is an area spell for the party threat back-off: it waits while the bot holds 50% or more of the tank's threat on any mob it is fighting, loose adds included (the same rule as mod-playerbots' Aoe threat type).

---

## Demon Summons & Utility

- **Pet Selection:**
  - *Voidwalker:* The PvE/dungeon default (higher-priority summon, also the non-combat default); off-tanks and uses *Sacrifice* for emergency shields. A solo pool bot that learns *Summon Voidwalker* (level 10) and holds a Soul Shard upgrades its Imp to a Voidwalker on its own (`runtime/WarlockPetPolicy.h`); below that it keeps the Imp, a shardless bot stays quiet (no failed summon queued), and a bot whose player picked a demon manually is never overridden. Facing 3+ mobs solo it AoE-taunts with *Suffering* (never in a real-tank group), and between pulls a hurt Voidwalker heals itself with *Consume Shadows*.
  - **Spec pet (`AiPlayerbot.WarlockPetBySpec`, default on):** in PvE (open world and dungeons; raid and PvP pet choices are unchanged) the warlock summons the demon that fits its tree, reimplemented from mod-playerbots' per-spec pet strategies (`runtime/WarlockPetPolicy.h`). Turtle 1.18.1 has no player Felguard, so the donor's Felguard step is skipped:

    | Spec | Preferred demon | Falls back to (what the warlock has learned) |
    | :--- | :--- | :--- |
    | Affliction | Felhunter (30) | Succubus (20), Voidwalker (10), Imp |
    | Demonology | Felhunter (30) | Succubus (20), Voidwalker (10), Imp |
    | Destruction | Imp | - |

    A warlock grouped with nobody filling the tank role keeps the Voidwalker (the pet is the tank; same read as the *Torment*/*Suffering* taunt rule). The demon is chosen whenever the warlock has none; pool bots also swap a default demon they already have (Imp, Voidwalker, Succubus or Felhunter) for the one that fits, outside combat and only with a Soul Shard (the Imp needs none). A bot with a player master keeps whatever it has, a `pet imp|voidwalker|succubus|felhunter` strategy order is never overridden, and a warlock without a shard keeps today's Imp fallback. While the warlock is mounted it neither summons nor swaps a spec demon; the choice waits until it dismounts. With the toggle off nothing here applies and the paragraph above holds.
  - *Imp:* The raid pet, providing *Blood Pact* (Stamina buff).
  - *Succubus:* Provides humanoid crowd control via *Seduce*.
  - *Felhunter:* Uses *Spell Lock* for ranged interrupts, and *Devour Magic* both ways: strips a magic buff off the current target (purge) and lifts magic off a party member (cleanse). Both fire only while a Felhunter is out; other demons idle those nodes safely.
- **Out-of-combat upkeep:** The bot maintains *Demon Armor* (with *Demon Skin* as fallback) and casts *Unending Breath* (5697) on itself and the party, but only while swimming — no buff churn on land.
  - **Pet upkeep:** the demon's known spells are swept for autocast (non-passive, except *Spell Lock* / *Devour Magic* deliberate-cast abilities and *Sacrifice*, which kills the Voidwalker on this realm and would otherwise fire in every fight) and pet plus guardians are pinned to *Defensive* after each summon — same autonomous pair as hunter pets. Explicit `.bot pet aggressive|defensive|passive` orders still apply immediately. *Torment* follows the group like hunter *Growl*: autocast-on solo or tankless, off when grouped with a real tank; the ordered single-target peel still rescues the owner (only when the warlock itself is the victim, so it never steals from the tank). Mid-fight the bot channels *Health Funnel* into a demon below half health while its own health stays above 60% — never a suicide channel, combat only.
  - **Pet ranks:** the demon learns the highest rank its level allows on summon and on the periodic initialize-pet tick (Torment, Firebolt, Lash of Pain...; *Sacrifice* and *Seduction* stay manual, never autocast), replacing lower ranks automatically — pool bots never read Grimoires, so without this they would cast rank 1 forever.
- **Weapon stones:** Affliction and Demonology create (one shard each) and equip *Spellstones* into an empty off-hand, Destruction creates and equips *Firestones* the same way — vanilla off-hand semantics, only beside a one-handed weapon, never displacing real gear or fighting a staff. Each spec only conjures its own stone. Turtle's own *Spellstone* (51933) and *Firestone* (51932) are bag consumables, not off-hand items: the bot keeps one in its bags and never tries to equip it (no bot logic uses them from the bag yet).
- **Healthstones & Soulstones:**
  - Creates and uses *Healthstones* during combat.
  - Creates and stores Soulstones on the party healer or tank whenever an in-range healer/tank lacks one (not timed to boss pulls).
- **Fel Domination (Demonology combat recovery):**
  - Burns the 5-minute *Fel Domination* cooldown only in combat with a dead pet, then immediately re-summons (Voidwalker fallback); never wasted out of combat where the free summon applies.
- **Crowd Control:**
  - Casts *Fear* only on its assigned raid CC mark, and never on a mob already under another CC (a feared mob pulls adds). There is no automatic Fear on unmarked mobs. Skips undead and mechanical marks (fear never lands on them).
  - *Howl of Terror* (AoE fear) is PvP-only; it is never cast in PvE groups, and never inside a dungeon or raid (a feared mob pulls neighbouring packs).
  - Casts *Banish* on Demons and Elementals (other marks are skipped: banish only lands on those two types).
  - Never hits its own CC mark (the mark it can Fear or Banish) with an area spell (*Rain of Fire*, *Hellfire*, ...) while other enemies are left, because it CCs that mob next. Once only CC marks are left, AoE on them is allowed. It starts *Fear*/*Banish* only in line of sight and 5 yd inside the spell's range, and walks closer first.
  - *Seduce* (Succubus) is a CC executor for humanoid marks: with a Succubus out, the bot orders *Seduction* on its assigned CC mark (pet-cast, so range resolves against the demon — she must already be near the mark). Other types are skipped, and fear wins ties.

---

## Premade Talent Specs & Progression (1.18.1)

TortoiseBots provides verified 5-level talent checkpoints (levels 10–60) tailored for Turtle WoW 1.18.1:

| Spec Name | Config ID | Role | 60 Allocation | Key Signatures & Synergies |
| :--- | :--- | :--- | :--- | :--- |
| **Affliction** | `9.0` | Ranged DPS | `40 / 0 / 11` | Dark Harvest (40), Shadow Mastery (40), Nightfall, Siphon Life, Bane -0.5s Shadow Bolt dip. |
| **Demonology** | `9.1` | Ranged DPS / Tanky | `7 / 44 / 0` | Power Overwhelming (30), Soul Link (40), Master Demonologist, Instant Corruption dip. |
| **Destruction** | `9.2` | Ranged DPS | `7 / 0 / 44` | Conflagrate (40), Ruin (30), Instant Corruption + Improved Life Tap dip. 0 points in Searing Pain threat. |

### Leveling Milestones & Progression Rationale

- **Affliction (`9.0`):**
  - *Levels 10–35:* DoT scaling foundation: *Improved Corruption* (5/5 instant cast), *Improved Life Tap* (2/2), *Nightfall* (2/2 instant Shadow Bolt on Corruption/Drain Life ticks), *Grim Reach* (2/2 range), *Soul Siphon* (3/3), and *Siphon Life* (30 periodic health siphon).
  - *Levels 35–45:* *Rapid Deterioration* (2/2), *Shadow Mastery* (40 +10% shadow damage), and *Dark Harvest* (40 Turtle custom finisher).
  - *Levels 45–60:* *Suppression* (5/5 spell hit cap) and 11 Destruction points (*Cataclysm* 5/5 + *Bane* 5/5 + *Shadowburn* 1/1) reducing Shadow Bolt cast time by 0.5s.
- **Demonology (`9.1`):**
  - *Levels 10–35:* Pet durability and burst: *Demonic Embrace* (5/5 Stamina), *Demonic Aegis* (3/3 Armor/healing buff), *Fel Intellect* (3/3), *Fel Domination* (1/1 fast pet summon), *Unholy Power* (3/3 pet damage), and *Power Overwhelming* (30 custom pet sacrifice burst).
  - *Levels 35–45:* *Demonic Precision* (3/3 pet spell hit), *Master Demonologist* (5/5 stat scaling), and *Soul Link* (40 30% damage transfer).
  - *Levels 45–60:* Completes Demonology utility (*Unleashed Potential* + *Nether Studies*), then takes 7 Affliction points into *Improved Corruption* (5/5) and *Improved Life Tap* (2/2).
- **Destruction (`9.2`):**
  - *Levels 10–35:* Fire/Shadow nuke path: *Cataclysm* (5/5 cost reduction), *Bane* (5/5 cast time reduction), *Shadowburn* (20 instant soul shard burst), *Devastation* (5/5 +5% crit), and *Ruin* (30 +100% crit damage bonus), per game data.
  - *Levels 35–45:* *Improved Immolate* (5/5) and *Conflagrate* (40 instant burst consuming Immolate), per game data.
  - *Levels 45–60:* Per the preset order, takes *Emberstorm* and *Demonic Swiftness* **before** the 7-point Affliction dip (*Improved Corruption* 5/5 instant cast + *Improved Life Tap* 2/2), then finishes deeper in Destruction. Avoids threat-increasing talents (*Improved Searing Pain*), per game data.

