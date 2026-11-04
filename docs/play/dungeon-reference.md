---
id: play-dungeon-reference
title: Turtle WoW 5-Man Dungeon Reference
category: play
summary: Boss kill orders, creature IDs, pooled rares, and event triggers for 18 5-player dungeons in Turtle WoW.
tags: [dungeon, reference, bosses, instances, turtle-wow]
relates_to:
  - play-dungeons-and-raids
  - play-known-limitations
---

# Turtle WoW 5-Man Dungeon Reference

Companion bots follow you through a dungeon; they do not explore or clear it on their own. The exception is the [dungeon run leader](dungeon-run-leader.md) (`.bot run`, on by default but still work in progress): it walks a stored dungeon route by itself ([settings](../server/settings/dungeons-raids.md#aiplayerbotdungeonselfclear)).
- Its limits are in [Known Limitations](known-limitations.md#dungeon-run-leader-work-in-progress).
- This catalog is for human leaders: creature IDs, boss kill orders in walking order, pooled rares to ignore, and event triggers across 18 dungeons.
- Every ID below was checked against the world database.

For general pulling, crowd control, and wipe recovery tactics, see the [Dungeon & Raid Tactics Guide](dungeons-and-raids.md).

## Reading This Reference

- **Creature Entries:** Numbers in parentheses (e.g. `92107`) are database creature IDs from the Turtle WoW 1.18.1 world database.
- **Turtle Tag:** A *(Turtle)* marker indicates custom creatures and bosses exclusive to Turtle WoW.
- **Confirmed vs provisional:** Uldaman and Zul'Farrak kill orders have been walked in a live run; Ragefire Chasm and Wailing Caverns are mapped but not live-checked; the rest are read from spawn positions and still provisional. Where the stored run-leader plan walks a different order, the entry says so.
- **Atlas vs Walk:** Atlas = in-game Atlas map order; Walk = entrance-to-end walking order.
- **Do Not Wait For:** Lists rares, pooled spawns, escorts, and friendly NPCs that may not be present in your instance run.
- **Bracketed Items:** Items in brackets like `[Ring of Law]` represent non-boss objectives, doors, or event triggers.

## Pooled Rares to Never Wait For

Never halt your group waiting for these creatures to spawn. Each shares a spawn pool, so it may not be there on your run:

| Entry | Creature Name | Dungeon | Spawn Chance |
| :---: | :--- | :--- | :---: |
| 4842 | Earthcaller Halmgar | Razorfen Kraul | 30% |
| 6228 | Dark Iron Ambassador | Gnomeregan | 30% |
| 6488 | Fallen Champion | Scarlet Monastery (7 Graveyard spots) | 2% per spot |
| 9218 | Spirestone Battle Lord | Blackrock Spire | 15% |
| 10558 | Hearthsinger Forresten | Stratholme (one of four spots) | exactly one spawns |
| 12237 | Meshlok the Harvester | Maraudon | 30% |

## Dungeon Catalog

### Crescent Grove (Map 802)

| Field | Detail |
| :--- | :--- |
| **Status** | Map 802, provisional (spawn positions only) |
| **Kill Order** | Grovetender Engryss (92107) → Keeper Ranathos (92109) → High Priestess A'lathea (92108) → Fenektis the Deceiver (92111) → Master Raxxieth (92110, end boss) |
| **Do Not Wait For** | Inuvias Brightlance (92133), an unordered rare spawn. |
| **Quirks** | None reported. |

### Stormwrought Ruins (Map 818)

| Field | Detail |
| :--- | :--- |
| **Status** | Map 818, provisional (spawn positions only). The run-leader plan walks 5 more bosses after Tidebane: Subjugator Halthas (62652) → Mycellakos (62664) → Eldermaw (62661) → Lady Drazare (62665) → Ighal'for (62671). |
| **Kill Order** | Dagar the Glutton (62547) → Oronok Torn-Heart (62548) → Librarian Theodorus (62552) → Duke Balor the IV (62549) → Chieftain Stormsong (62551) → Deathlord Tidebane (62550) |
| **Do Not Wait For** | None listed. |
| **Quirks** | Tidebane is not the last boss: the plan continues past him (see Status). |

### Gilneas City (Map 815)

| Field | Detail |
| :--- | :--- |
| **Status** | Map 815, provisional (spawn positions only). Mortimer and Celia Harlow stand side by side and are fought as a pair; the run-leader plan only names Mortimer. |
| **Kill Order** | Matthias Holtz (61419) → Packmaster Ragetooth (61420) → Judge Sutherland (61421) → Dustivan Blackcowl (61422) → Marshal Magnus Greystone (61423) → Regent-Lord Mortimer Harlow (61264) → Regent-Lady Celia Harlow (61263) → Genn Greymane (61418, end boss) |
| **Do Not Wait For** | Horsemaster Levvin (61605). |
| **Quirks** | Mortimer and Celia Harlow stand together and must be engaged as a pair. |

### Hateforge Quarry (Map 808)

| Field | Detail |
| :--- | :--- |
| **Status** | Map 808, provisional (spawn positions only; the run-leader plan walks Bargul first, then Corrosis, Har'gesh, Figgles, Annihilator) |
| **Kill Order** | Engineer Figgles (60736) → High Foreman Bargul Blackhammer (60735) → Corrosis (60829) → Hatereaver Annihilator (60734) → Har'gesh Doomcaller (60737, end boss) |
| **Do Not Wait For** | Broody (60715), Figgles' companion bird. |
| **Quirks** | None reported. |

### Windhorn Canyon (Map 820)

| Field | Detail |
| :--- | :--- |
| **Status** | Map 820, provisional (two potential routes; the run-leader plan walks the reverse: Walgan → Vortalus → Narlgom → Stormhoof → Pathun → Shalk → Ahgk'tos) |
| **Atlas Kill Order** | Pathun Duskhide (62779) → Ahgk'tos the Pure (62778) → Ambassador Vortalus (62783) → Walgan Bloodcaller (62784) → Bonespeaker Narlgom (62780) → Prophet Stormhoof (62781) → Chieftain Shalk Blackwind (62782, end boss) |
| **Walk Kill Order** | Ahgk'tos the Pure (62778) → Pathun Duskhide (62779) → Ambassador Vortalus (62783) → Bonespeaker Narlgom (62780) (+ Champion Rotag (62785), summoned add) → Walgan Bloodcaller (62784) → Prophet Stormhoof (62781) → Chieftain Shalk Blackwind (62782, end boss) |
| **Do Not Wait For** | Grand Elder Skystrider (62837), a friendly quest NPC. |
| **Quirks** | Champion Rotag (62785) is summoned mid-fight by Bonespeaker Narlgom (the plan lists him as an add of that fight, not a boss). |

### Dragonmaw Retreat (Map 816)

| Field | Detail |
| :--- | :--- |
| **Status** | Map 816, provisional (spawn positions only). The run-leader plan only covers Bogpaw → Gowlfang → Searistrasz → Halgan → Zuluhed; the middle bosses below are still unwalked. |
| **Kill Order** | 11 bosses in sequence from Bogpaw Truthsay to Zuluhed the Whacked (details below). |
| **Do Not Wait For** | None listed. |
| **Quirks** | Bogpaw Truthsay and Gowlfang share one room. Reaching Web Master Torkon requires a gap jump; reaching Cavernweb Broodmother requires a drop. Entering from the far entrance reverses this route. |

**Full kill order:** Bogpaw Truthsay (62056) → Gowlfang (62057) → Web Master Torkon (62067) → Cavernweb Broodmother (62066) → Garlok Flamekeeper (62071) → Slagfist Destroyer (62068) → Halgan Redbrand (62069) → Overlord Blackheart (62070) → Elder Hollowblood (62038) → Searistrasz (62072) → Zuluhed the Whacked (62037)

### Stormwind Vault (Map 35)

| Field | Detail |
| :--- | :--- |
| **Status** | Map 35, provisional (spawn positions only; the run-leader plan walks Volkan → Damian → Aszosh → Bride → Tham'Grarr → Arc'tiras) |
| **Kill Order** | Volkan Cruelblade (80851) → Tham'Grarr (80852) → Black Bride (80850) → Damian (80854) → Aszosh Grimflame (80853) → Arc'tiras (93107, end boss) |
| **Do Not Wait For** | None listed. |
| **Quirks** | None reported. |

### Karazhan Crypt (Map 800)

| Field | Detail |
| :--- | :--- |
| **Status** | Map 800, provisional (spawn positions only; the run-leader plan walks Enkhraz → Gort → Andreon → Marrowspike → Hivaxxis → Corpsemuncher) |
| **Kill Order** | Commander Andreon (91919) → Corpsemuncher (91917) → Guard Captain Gort (92935) → Hivaxxis (91929) → Archlich Enkhraz (91916) → Marrowspike (91920) → [Crypt Watcher summons] Alarus (91928, end boss) |
| **Do Not Wait For** | None listed. |
| **Quirks** | Alarus is summoned via the Crypt Watcher trigger event and lacks a static spawn point. Mobs are level 60–63. |

### Razorfen Kraul (Map 47)

| Field | Detail |
| :--- | :--- |
| **Status** | Map 47, spawn positions only (the run-leader plan follows this order). |
| **Kill Order** | Roogug (6168) → Aggem Thorncurse (4424) → Death Speaker Jargba (4428) → Overlord Ramtusk (4420) → Rotthorn (62503, *Turtle*) → Agathelos the Raging (4422) → Charlga Razorflank (4421, end boss) |
| **Do Not Wait For** | Blind Hunter (4425) and Earthcaller Halmgar (4842) (30% rares); Willix the Importer (4508, friendly escort NPC). |
| **Quirks** | Rotthorn is a custom Turtle WoW boss. |

### Blackfathom Deeps (Map 48)

| Field | Detail |
| :--- | :--- |
| **Status** | Map 48, spawn positions only. The run-leader plan walks Serra'kis before Kelris; the order below kills him last on purpose. |
| **Kill Order** | Ghamoo-ra (4887) → Lady Sarevess (4831) → Gelihast (6243) → Velthelaxx the Defiler (62530) → Twilight Lord Kelris (4832) → [Fires of Aku'mai] → Aku'mai (4829) → Old Serra'kis (4830, underwater, end boss) |
| **Do Not Wait For** | Lorgus Jett (12902), a roaming rare spawn. |
| **Quirks** | Kill Serra'kis last: the deep pool beneath Twilight Lord Kelris can cause bot pathing issues when climbing out. The plan disagrees (Serra'kis mid-route), so expect it to struggle there. |

### Gnomeregan (Map 90)

| Field | Detail |
| :--- | :--- |
| **Status** | Map 90, spawn positions only. |
| **Kill Order** | Viscous Fallout (7079) → Electrocutioner 6000 (6235) → Crowd Pummeler 9-60 (6229) → Mekgineer Thermaplugg (7800, end boss) |
| **Do Not Wait For** | Dark Iron Ambassador (6228, 30% rare); Blastmaster Emi Shortfuse (7998, quest NPC). |
| **Quirks** | Grubbis is summoned by an event and does not appear in static spawns. |

### Scarlet Monastery (Map 189)

Each wing is treated as a separate dungeon run:

- **Graveyard:** Interrogator Vishas (3983) → Bloodmage Thalnos (4543) → Duke Dreadmoore (61972, *Turtle*) → Scorn (14693).
- **Library:** Houndmaster Loksey (3974) → Brother Wystan (61983, *Turtle*) → Arcanist Doan (6487).
- **Armory:** Armory Quartermaster Daghelm (61982, *Turtle*) → Herod (3975).
- **Cathedral:** High Inquisitor Fairbanks (4542) → Scarlet Commander Mograine (3976) → High Inquisitor Whitemane (3977).
- **Do Not Wait For:** Fallen Champion (6488, 2% per Graveyard spot).
- **Quirks:** The three Turtle WoW custom bosses have big health pools: Dreadmoore (50930), Wystan (10475), Daghelm (13487) — roughly 15×, 3× and 4× the local elites. Graveyard order above follows spawn positions; the run-leader plan walks Thalnos before Dreadmoore and adds Scorn last.

### Uldaman (Map 70)

| Field | Detail |
| :--- | :--- |
| **Status** | Map 70, walked live (the run-leader plan follows this order). |
| **Kill Order** | [Horde only: Baelog (6906)] → Revelosh (6910) → Ironaya (7228) → Obsidian Sentinel (7023) → [Altar of the Keepers] → Ancient Stone Keeper (7206) → Galgann Firehammer (7291) → Grimlok (4854) → [Altar of Archaedas] → Archaedas (2748, end boss) |
| **Do Not Wait For** | Annora (11073, enchanter NPC); Eric "The Swift" (6907) and Olaf (6908). |
| **Quirks** | Alliance players skip Baelog, who is a friendly quest giver for their faction. Loot Baelog's Chest for the Gni'kiv Medallion. Grimlok is the seventh boss in the sequence. |
| **Bot Tactics** | Opt-in per-boss rows for Revelosh, the Lost Dwarves, Ironaya and the Obsidian Sentinel: [Boss Tactics](boss-tactics.md). |

### Zul'Farrak (Map 209)

| Field | Detail |
| :--- | :--- |
| **Status** | Map 209, walked live. The run-leader plan walks Antu'sul's wing before Theka; both orders work since the bosses are in separate areas. |
| **Kill Order** | 7 bosses and events leading to Champion Razjal the Quick (details below). |
| **Do Not Wait For** | Sergeant Bly (7604), who fights during the pyramid event. |
| **Quirks** | Champion Razjal is invulnerable until gossip dialog starts the Farraki Arena event. The arena spawns Kath'zen the Brutal (62496), then Juthza the Cunning (62497), then Razjal. A party wipe resets the event. |

**Full kill order:** Theka the Martyr (7272) → Antu'sul (8127) → Witch Doctor Zum'rah (7271) → [Temple Summit Event] → Chief Ukorz Sandscalp (7267) → Hydromancer Velratha (7795) → [Sacred Pool, Gahz'rilla] → Zel'jeb the Ancient (62495, *Turtle*) → [Farraki Arena] → Champion Razjal the Quick (62498, *Turtle*, end boss)

### Maraudon (Map 349)

| Field | Detail |
| :--- | :--- |
| **Status** | Map 349, spawn positions only. The run-leader plans walk two wings and end Rotgrip → Theradras; the order below ends Theradras → Rotgrip (Rotgrip stands deeper). |
| **Kill Order** | 8 bosses from Noxxion through Princess Theradras to Rotgrip (details below). |
| **Do Not Wait For** | Meshlok the Harvester (12237, 30% rare). |
| **Quirks** | Creeping Sludge (12222) aura mechanics (see below). |

<details>
<summary>Creeping Sludge aura avoidance mechanics</summary>

Creeping Sludge (12222) pulses a poison aura within about 5 yards, even while idle. Walking close aggros the sludge.

With [`AiPlayerbot.AvoidCreatureAuras`](../server/settings/dungeons-raids.md#aiplayerbotavoidcreatureauras) enabled (default on), bots automatically manage this hazard:
- All bots step out when within 8 yards. Chase and follow movements stop 9 yards away and back off as a sludge creeps toward them.
- Melee bots that cannot reach anything else switch target after a few seconds. Fight sludges primarily with ranged DPS.
- Non-tank bots will not charge or flank within 9 yards of a sludge.
- Tanks are exempt in combat: they may enter the 9-yard ring to hold aggro.
- If your party is melee-only, explicitly order [`.bot action attack`](commands/party-actions.md#bot-action-attack) on the sludge to command melee into the pulse.

</details>

### Sunken Temple (Map 109)

| Field | Detail |
| :--- | :--- |
| **Status** | Map 109, spawn positions only. The run-leader plan also names Atal'alarion (8580) and the dragon pair Weaver (5720) / Dreamscythe (5721), but not Hazzas (5722). |
| **Kill Order** | Zolo (5712), Gasher (5713), Hukku (5715) (forcefield defenders) → [Central circle clear] → Jammal'an the Prophet (5710) → [Weaver & Dreamscythe] → Morphaz (5719) → Hazzas (5722) → Shade of Eranikus (5709, end boss) |
| **Do Not Wait For** | None listed. |
| **Quirks** | The lower pit wing (statues, Atal'alarion, the Avatar of Hakkar) is optional. |

### Blackrock Depths (Map 230)

| Field | Detail |
| :--- | :--- |
| **Status** | Map 230, provisional (spawn positions only; the run-leader plan walks Bael'Gar before Angerforge) |
| **Kill Order** | 12 encounters from Gerstahn to Emperor Dagran Thaurissan (details below). |
| **Do Not Wait For** | Pyromancer Loregrain (9024), Lord Roccor (9025), Warder Stilgiss (9041) and Panzor the Invincible (8923) — side bosses off the main route. Solakar Flamewreath (10264) has no spawn (event-summoned). |
| **Quirks** | The Grim Guzzler bar patrons are non-hostile initially. |

**Full kill order:** High Interrogator Gerstahn (9018) → Houndmaster Grebmar (9319) → [Ring of Law] → General Angerforge (9033) → Bael'Gar (9016) → [Shadowforge lock lever] → Golem Lord Argelmach (8983) → Fineous Darkvire (9056) → Lord Incendius (9017) → Ambassador Flamelash (9156) → [The Seven & Lyceum braziers] → Magmus (9938) → Princess Moira Bronzebeard (8929) → Emperor Dagran Thaurissan (9019, end boss)

### Razorfen Downs (Map 129)

| Field | Detail |
| :--- | :--- |
| **Status** | Map 129, spawn positions only. The run-leader plan also walks Death Prophet Rakameg (62679) between Glutton and Amnennar. |
| **Kill Order** | Tuten'kash (7355, summoned via entrance gong) → Mordresh Fire Eye (7357) → Glutton (8567) → Amnennar the Coldbringer (7358, end boss) |
| **Do Not Wait For** | Ragglesnout (7354, rare spawn). |
| **Quirks** | Tuten'kash must be summoned by striking the gong. |
