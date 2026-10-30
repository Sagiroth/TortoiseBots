"""Guards the dungeon plan tables (ai_dungeon_plan, ai_dungeon_leg, ai_dungeon_step).

The loader (ai/playerbot/DungeonPlanMgr.cpp) names its columns in SELECTs; a column that a
migration does not create would make the query fail at startup and leave every plan out. This
checks that each column the loader reads is created by a migration, and that any step rows a
later (exporter) migration inserts use a kind the loader knows.
"""
import glob
import math
import os
import pathlib
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KINDS = {"move", "move_direct", "move_hop_exact", "pull", "boss", "loot", "open", "open_proof", "use_item", "wait_object",
         "wait_clear", "rest", "ritual", "hold", "gossip", "use_on"}
# The opt-in Gahz'rilla plan for test runs (never applied automatically: not under data/sql/world).
OPT_IN = pathlib.Path(ROOT) / "tools/optional-plans/zulfarrak-gahzrilla.sql"
TABLES = ("ai_dungeon_plan", "ai_dungeon_leg", "ai_dungeon_step", "ai_dungeon_leg_need", "ai_dungeon_boss")
# The plans whose bosses ai_dungeon_boss lists, and their maps.
BOSS_PLANS = {"ragefire_chasm": 389, "wailing_caverns": 43, "uldaman": 70, "zulfarrak": 209, "deadmines": 36,
              "stormwind_stockade": 34,
              "razorfen_kraul": 47,
              "gnomeregan": 90,
              "scarlet_library": 189,
              "scarlet_armory": 189, "scarlet_cathedral": 189, "scarlet_graveyard": 189,
              "blackfathom_deeps": 48,
              "blackrock_depths": 230,
              "shadowfang_keep": 33,
              "sunken_temple": 109,
              "razorfen_downs": 129,
              "crescent_grove": 802,
              "dragonmaw_retreat": 816,
              "dire_maul_east": 429, "dire_maul_north": 429, "dire_maul_west": 429}
# Every ai_dungeon_boss row, by (map, entry): the creature_template name and rank in the core's data (Tortoise V2
# sql/base tw_world_creature_template.sql, read 2026-10-21). All rank 1 (elite) and no instance bind, hence the table.
SEEDED_BOSSES = {
    # Dire Maul: West (20261024020500): its six boss steps, names from the core's creature_template (all rank 1). Tsu'zee
    # 11467 (rank 2 rare) and Ferra 14308 (Illyanna's pet, a boss.first) are pulls/adds, not seeded.
    (429, 11489): "Tendris Warpwood", (429, 14690): "Revanchion", (429, 11488): "Illyanna Ravenoak",
    (429, 11487): "Magister Kalendris", (429, 11496): "Immol'thar", (429, 11486): "Prince Tortheldrin",
    (389, 11517): "Oggleflint", (389, 11520): "Taragaman the Hungerer", (389, 11518): "Jergosh the Invoker",
    (389, 11519): "Bazzalan",
    (43, 3653): "Kresh", (43, 3671): "Lady Anacondra", (43, 3669): "Lord Cobrahn", (43, 3670): "Lord Pythas",
    (43, 3674): "Skum", (43, 3673): "Lord Serpentis", (43, 5775): "Verdan the Everliving",
    (43, 3654): "Mutanus the Devourer", (43, 61965): "Vangros", (43, 61968): "Zandara Windhoof",
    (70, 6910): "Revelosh", (70, 6906): "Baelog", (70, 7228): "Ironaya", (70, 7023): "Obsidian Sentinel",
    (70, 7206): "Ancient Stone Keeper", (70, 7291): "Galgann Firehammer", (70, 4854): "Grimlok", (70, 2748): "Archaedas",
    (209, 8127): "Antu'sul", (209, 7272): "Theka the Martyr", (209, 7795): "Hydromancer Velratha",
    (209, 7271): "Witch Doctor Zum'rah", (209, 7274): "Sandfury Executioner", (209, 7275): "Shadowpriest Sezz'ziz",
    (209, 7267): "Chief Ukorz Sandscalp", (209, 7273): "Gahz'rilla",
    (36, 644): "Rhahk'Zor", (36, 642): "Sneed's Shredder", (36, 643): "Sneed", (36, 1763): "Gilnid",
    (36, 61963): "Masterpiece Harvester", (36, 61961): "Jared Voss", (36, 646): "Mr. Smite", (36, 647): "Captain Greenskin",
    (36, 639): "Edwin VanCleef", (36, 645): "Cookie",
    (34, 1696): "Targorr the Dread", (34, 1663): "Dextren Ward", (34, 1666): "Kam Deepfury", (34, 1717): "Hamhock",
    (34, 1716): "Bazil Thredd",
    (47, 6168): "Roogug", (47, 4424): "Aggem Thorncurse", (47, 4428): "Death Speaker Jargba",
    (47, 4420): "Overlord Ramtusk", (47, 62503): "Rotthorn", (47, 4422): "Agathelos the Raging",
    (47, 4421): "Charlga Razorflank",
    (90, 7079): "Viscous Fallout", (90, 6235): "Electrocutioner 6000", (90, 6229): "Crowd Pummeler 9-60",
    (90, 6228): "Dark Iron Ambassador", (90, 7800): "Mekgineer Thermaplugg",
    (189, 3974): "Houndmaster Loksey", (189, 61983): "Brother Wystan", (189, 6487): "Arcanist Doan",
    (48, 4887): "Ghamoo-ra", (48, 4831): "Lady Sarevess", (48, 6243): "Gelihast",
    (48, 62530): "Velthelaxx the Defiler", (48, 4830): "Old Serra'kis", (48, 4832): "Twilight Lord Kelris",
    (48, 4829): "Aku'mai",
    (189, 61982): "Armory Quartermaster Daghelm", (189, 3975): "Herod",
    (189, 4542): "High Inquisitor Fairbanks", (189, 3976): "Scarlet Commander Mograine", (189, 3977): "High Inquisitor Whitemane",
    (189, 3983): "Interrogator Vishas", (189, 14693): "Scorn", (189, 4543): "Bloodmage Thalnos", (189, 61972): "Duke Dreadmoore",
    (33, 3914): "Rethilgore", (33, 3886): "Razorclaw the Butcher", (33, 3887): "Baron Silverlaine",
    (33, 4278): "Commander Springvale", (33, 4279): "Odo the Blindwatcher",
    (33, 4274): "Fenrus the Devourer", (33, 3927): "Wolf Master Nandos", (33, 4275): "Archmage Arugal",
    (109, 8580): "Atal'alarion", (109, 5720): "Weaver", (109, 5721): "Dreamscythe",
    (109, 5710): "Jammal'an the Prophet", (109, 5719): "Morphaz", (109, 5709): "Shade of Eranikus",
    (129, 7357): "Mordresh Fire Eye", (129, 8567): "Glutton", (129, 62679): "Death Prophet Rakameg",
    (129, 7358): "Amnennar the Coldbringer",
    (429, 11490): "Zevrim Thornhoof", (429, 13280): "Hydrospawn", (429, 14327): "Lethtendris",
    (429, 11492): "Alzzin the Wildshaper",
    (802, 92107): "Grovetender Engryss", (802, 92109): "Keeper Ranathos", (802, 92108): "High Priestess A'lathea", (802, 92111): "Fenektis the Deceiver", (802, 92110): "Master Raxxieth",
    (816, 62056): "Bogpaw Truthsay", (816, 62057): "Gowlfang", (816, 62072): "Searistrasz",
    (816, 62069): "Halgan Redbrand", (816, 62037): "Zuluhed the Whacked",
    (429, 14326): "Guard Mol'dar", (429, 14321): "Guard Fengus", (429, 14323): "Guard Slip'kik",
    (429, 14325): "Captain Kromcrush", (429, 11501): "King Gordok",
}
# Not bosses here: adds of a boss step (Eric 6907, Olaf 6908, Nekrum 7796, Ruuzlu 7797), the friendly Naralex 3679 and his
# Disciple 3678, the Sergeant Bly event 7604, Miner Johnson 3586 (a rare in the original game), rares (Deviate Faerie Dragon 5912, Zerillis 10082, Sandarr Dunereaver 10080,
# Bruegal Ironknuckle 1720: rank 2, Stormwind Stockade).
# Razorfen Kraul: Spearhide 4438, Earthcaller Halmgar 4842, Blind Hunter 4425 — rank 2 in the world data).
# Grubbis 7361: no spawn row in the map 90 export and no boss step names him, Gnomeregan).
# Shadowfang Keep's extras that are not legs (Sever 14682, Prelate Ironmane 61969, the intro Arugal 10000).
# the BFD neutrals Argent Guard 4787 and Woji the Toad 62531 (both rank 0).
NOT_BOSSES = {6907, 6908, 7796, 7797, 3679, 3678, 7604, 5912, 10082, 10080, 3586, 1720, 4438, 4842, 4425, 7361, 4787, 62531, 14682, 61969, 10000, 7354, 8923, 9024, 9041, 9042, 8929, 12237, 14695, 16118, 14349, 14324, 11467, 14308, 10393, 10558, 11032, 14684, 16101, 16102, 10809}
# Ragglesnout 7354 (rank 2, Razorfen Downs: he is in a pull group).
# Zum'rah's 31 Shallow Graves (128403), in the order the leg opens them: the nearest-neighbour walk of the plan from 1859 1058
# (the exporter writes each as a stand move, a loot and a sweep pull, in this order). A reorder is a deliberate change.
ZUMRAH_GRAVES = (
    (1858.70, 1053.75), (1862.12, 1050.76), (1871.08, 1046.76), (1875.85, 1044.26),
    (1881.46, 1041.43), (1881.99, 1034.68), (1888.36, 1037.00), (1867.74, 1041.84),
    (1845.96, 1047.25), (1841.08, 1045.54), (1843.19, 1041.16), (1839.18, 1036.88),
    (1832.72, 1032.39), (1841.82, 1027.76), (1839.42, 1023.49), (1845.54, 1024.12),
    (1852.41, 1024.78), (1861.12, 1019.39), (1865.03, 1012.49), (1866.24, 1008.40),
    (1871.30, 1008.32), (1871.86, 994.26), (1890.12, 989.76), (1894.39, 977.01),
    (1887.46, 967.14), (1882.70, 971.83), (1879.89, 974.65), (1876.44, 972.21),
    (1892.57, 1006.37), (1891.60, 1023.06), (1887.94, 1029.08),
)
# Stormwind Vault (20261024010200): its map and its six boss steps, names from the core's creature_template (all rank 1). Added apart
# from the dicts above so that this change merges with the other plans' edits of them.
BOSS_PLANS["stormwind_vault"] = 35
SEEDED_BOSSES.update({(35, 80851): "Volkan Cruelblade", (35, 80854): "Damian", (35, 80853): "Aszosh Grimflame",
                      (35, 80850): "Black Bride", (35, 80852): "Tham'Grarr", (35, 93107): "Arc'tiras"})
# Frostmane Hollow (20261022210030): its map and its four boss steps, names from the core's creature_template (all rank 1). Added apart
# from the dicts above so that this change merges with the other plans' edits of them.
BOSS_PLANS["frostmane_hollow"] = 822
SEEDED_BOSSES.update({(822, 63131): "Battlemaster Ubukaz", (822, 63129): "Kan'za the Seer",
                      (822, 63133): "Tan'sha the Sleek", (822, 63130): "Hailar the Frigid"})
# Blackrock Depths (20261023130000): its map and its ten boss steps, names from the core's creature_template (all rank 1).
# Panzor 8923, Pyromancer Loregrain 9024, Warder Stilgiss 9041 and Verek 9042 are rank 2 rares the plan pulls explicitly, and
# Princess Moira 8929 is a friendly quest giver: none is seeded.
BOSS_PLANS["blackrock_depths"] = 230
SEEDED_BOSSES.update({(230, 9018): "High Interrogator Gerstahn", (230, 9319): "Houndmaster Grebmar",
                      (230, 9016): "Bael'Gar", (230, 9033): "General Angerforge",
                      (230, 8983): "Golem Lord Argelmach", (230, 9017): "Lord Incendius",
                      (230, 9056): "Fineous Darkvire", (230, 9156): "Ambassador Flamelash",
                      (230, 9938): "Magmus", (230, 9019): "Emperor Dagran Thaurissan"})

# Gilneas City (20261024020700): its map and its seven boss steps, names from the map-815 spawn export (all rank 1).
# Regent-Lady Celia Harlow 61263 is a boss step's `first`, not a boss step, so she is not seeded. Added apart from the
# dicts above so that this change merges with the other plans' edits of them.
BOSS_PLANS["gilneas_city"] = 815
SEEDED_BOSSES.update({(815, 61419): "Matthias Holtz", (815, 61420): "Packmaster Ragetooth",
                      (815, 61421): "Judge Sutherland", (815, 61422): "Dustivan Blackcowl",
                      (815, 61423): "Marshal Magnus Greystone", (815, 61264): "Regent-Lord Mortimer Harlow",
                      (815, 61418): "Genn Greymane"})

# Windhorn Canyon (20261023150100, re-exported by 20261023230000 and 20261023230200): its map and its seven boss steps, names from the map-820 spawn export
# (all rank 1). Added apart from the dicts above so that this change merges with the other plans' edits of them.
BOSS_PLANS["windhorn_canyon"] = 820
SEEDED_BOSSES.update({(820, 62784): "Walgan Bloodcaller", (820, 62783): "Ambassador Vortalus",
                      (820, 62780): "Bonespeaker Narlgom", (820, 62781): "Prophet Stormhoof",
                      (820, 62779): "Pathun Duskhide", (820, 62782): "Chieftain Shalk Blackwind",
                      (820, 62778): "Ahgk'tos the Pure"})
# Scholomance (20261024020000): its map and its ten boss steps, names from the map-289 spawn export (all rank 1). Lord Blackwood 14695
# is a Turtle boss fought as a member of the leg-7 hall pull, and Kormok 16118's spawn is SPAWN_FLAG_DISABLED: neither is seeded.
BOSS_PLANS["scholomance"] = 289
SEEDED_BOSSES.update({(289, 10505): "Instructor Malicia", (289, 10507): "The Ravenian",
                      (289, 10901): "Lorekeeper Polkelt", (289, 10502): "Lady Illucia Barov",
                      (289, 10503): "Jandice Barov", (289, 11622): "Rattlegore", (289, 10432): "Vectus",
                      (289, 10508): "Ras Frostwhisper", (289, 11261): "Doctor Theolen Krastinov",
                      (289, 10504): "Lord Alexei Barov"})

# Maraudon: Purple (20261024010000): its map and its six boss steps, names from the core's creature_template (all rank 1).
# Meshlok the Harvester 12237 (rank 2, a patrolling rare) is not a boss step and is not seeded.
BOSS_PLANS["maraudon_purple"] = 349
SEEDED_BOSSES.update({(349, 12236): "Lord Vyletongue", (349, 12225): "Celebras the Cursed",
                      (349, 12203): "Landslide", (349, 13601): "Tinkerer Gizlock",
                      (349, 13596): "Rotgrip", (349, 12201): "Princess Theradras"})
# Maraudon orange (20261024010100): its map and its six boss steps, names from the map-349 creature_template rows (all rank 1).
# Meshlok the Harvester 12237 is a rare and not on the route; Celebras the Cursed 12225, Lord Vyletongue 12236 and the friendly
# Celebras the Redeemed 13716 belong to the purple wing's plan: none is seeded.
BOSS_PLANS["maraudon_orange"] = 349
SEEDED_BOSSES.update({(349, 13282): "Noxxion", (349, 12258): "Razorlash", (349, 12203): "Landslide",
                      (349, 13601): "Tinkerer Gizlock", (349, 13596): "Rotgrip", (349, 12201): "Princess Theradras"})

# Hateforge Quarry (20261024020800): the first plan for the Turtle custom quarry, map 808, and its five boss steps, names from the
# map-808 spawn export (all rank 1). Broody 60715 is Engineer Figgles' boss.first, not a boss. Added apart from the dicts above so
# that this change merges with the other plans' edits of them.
BOSS_PLANS["hateforge_quarry"] = 808
SEEDED_BOSSES.update({(808, 60735): "High Foreman Bargul Blackhammer", (808, 60829): "Corrosis",
                      (808, 60737): "Har'gesh Doomcaller", (808, 60736): "Engineer Figgles",
                      (808, 60734): "Hatereaver Annihilator"})

# The Black Morass (20261024020900): its map and its four boss steps, names from the core's creature_template. Medivh 50105 is
# faction 35 (friendly to the party) and is a destination marker, not a kill; the plan still carries him as the leg 1 boss.
BOSS_PLANS["black_morass"] = 269
SEEDED_BOSSES.update({(269, 65125): "Antnormi", (269, 50105): "Medivh",
                      (269, 65116): "Time-Lord Epochronos", (269, 65113): "Chronar"})
NOT_BOSSES |= {65122, 65124, 61575}  # Rotmaw, Mossheart and Epidamu: scripted mobs inside pull groups, not leg ends

# Stratholme's live side (20261024060700): its map and its three boss steps, names from the core's creature_template (all rank 1).
# Dathrohan's step ends on Balnazzar 10813 (UpdateEntry on the same creature, no 10813 spawn row); Jarien 16101/Sothos
# 16102 are the step's first, Skul 10393, Hearthsinger Forresten 10558 and Malor the Zealous 11032 are not boss steps,
# Balzaphon 14684 is a never-started event spawn: none is seeded. The 10812 row of 20261024020100 stays seeded (Dathrohan
# is still a boss, the step just ends on his second form).
# Jarien 16101/Sothos 16102 are Dathrohan's first (an EventAI pair), Skul 10393, Hearthsinger Forresten 10558 and Malor the Zealous
# 11032 are not boss steps, and Balzaphon 14684 is a never-started event spawn: none is seeded.
BOSS_PLANS["stratholme_live"] = 329
SEEDED_BOSSES.update({(329, 10811): "Archivist Galford", (329, 10812): "Grand Crusader Dathrohan",
                      (329, 10813): "Balnazzar", (329, 10997): "Cannon Master Willey"})

# Stratholme, undead side (20261024020200): its map and its five boss steps, names from the core's creature_template (all rank 1).
# Baron Rivendare 10440 is rank 1 with detection 1 (his script gates him on the Slaughter Square event); the two hostile rares inside
# the pulls, Skul 10393 and Stonespine 10809, and the neutral rare Hearthsinger Forresten 10558, are not seeded.
BOSS_PLANS["stratholme_undead"] = 329
SEEDED_BOSSES.update({(329, 10435): "Magistrate Barthilas", (329, 10437): "Nerub'enkan",
                      (329, 10438): "Maleki the Pallid", (329, 10436): "Baroness Anastari",
                      (329, 10440): "Baron Rivendare"})
# Stormwrought Ruins (20261024021000): its map and its eleven boss steps, names from the map-818 export and Turtle's creature_template
# (all rank 1). Remains of the Innocent 62670 (17393 hp, a boss-grade trash pack of leg 10) is not a boss step and is not seeded.
BOSS_PLANS["stormwrought_ruins"] = 818
SEEDED_BOSSES.update({(818, 62547): "Dagar the Glutton", (818, 62548): "Oronok Torn-Heart",
                      (818, 62552): "Librarian Theodorus", (818, 62549): "Duke Balor the IV",
                      (818, 62551): "Chieftain Stormsong", (818, 62550): "Deathlord Tidebane",
                      (818, 62652): "Subjugator Halthas Shadecrest", (818, 62664): "Mycellakos",
                      (818, 62661): "Eldermaw the Primordial", (818, 62665): "Lady Drazare",
                      (818, 62671): "Ighal'for"})

# Karazhan Crypt (20261024021100): its map and its six boss steps, names from the core's creature_template (Enkhraz 91916,
# Corpsemuncher 91917, Marrowspike 91920 and Hivaxxis 91929 are rank 3; Commander Andreon 91919 and Guard Captain Gort 92935 rank 1).
# Alarus 91928 (rank 3) has no spawn row - the six Necrotic Runes summon him - and is not a boss step, so he is not seeded.
# Midnight 91927 is Enkhraz's boss.first, not a boss.
BOSS_PLANS["karazhan_crypt"] = 800
SEEDED_BOSSES.update({(800, 91916): "Archlich Enkhraz", (800, 91917): "Corpsemuncher",
                      (800, 91920): "Marrowspike", (800, 91929): "Hivaxxis",
                      (800, 91919): "Commander Andreon", (800, 92935): "Guard Captain Gort"})

# Lower Blackrock Spire (20261024020600): its map and its seven boss steps, names from the core's creature_template (rank 1,
# except the rank-3 NPC bosses 9236 and 9568). The four static rares 9217, 9218, 9718 and 10376 are pack members, 9219 patrols,
# and Mor Grayhoof 16080 is the brazier event at Voone's spot: none is seeded.
BOSS_PLANS["lower_blackrock_spire"] = 229
SEEDED_BOSSES.update({(229, 9196): "Highlord Omokk", (229, 9236): "Shadow Hunter Vosh'gajin",
                      (229, 9237): "War Master Voone", (229, 10596): "Mother Smolderweb",
                      (229, 9736): "Quartermaster Zigris", (229, 10220): "Halycon",
                      (229, 9568): "Overlord Wyrmthalak"})
NOT_BOSSES.update({9217, 9218, 9219, 9718, 10376, 16080, 16044})

def migrations():
    for path in sorted(glob.glob(os.path.join(ROOT, "data/sql/world/*_world.sql"))):
        with open(path, encoding="utf-8") as handle:
            yield os.path.basename(path), handle.read()


def newest_zulfarrak_rows():
    """The step rows of the newest migration that inserts the zulfarrak steps (its name, its text, its zulfarrak rows)."""
    newest = None
    for name, text in migrations():
        if re.search(r"INSERT INTO `ai_dungeon_step`.*?\('zulfarrak',", text, re.S):
            newest = (name, text)
    assert newest is not None, "no migration inserts the zulfarrak steps"
    name, text = newest
    return name, text, [r for r in DungeonPlanSql.step_rows(None, text) if r["plan"] == "zulfarrak"]


def newest_plan_text(plan):
    """The name and text of the newest migration that inserts the plan's steps."""
    newest = None
    for name, text in migrations():
        if re.search(r"INSERT INTO `ai_dungeon_step`.*?\('%s'," % plan, text, re.S):
            newest = (name, text)
    assert newest is not None, "no migration inserts the %s steps" % plan
    return newest


def newest_uldaman_rows():
    """The step rows of the newest migration that inserts the uldaman steps (its name, its text, its uldaman rows)."""
    name, text = newest_plan_text("uldaman")
    return name, text, [r for r in DungeonPlanSql.step_rows(None, text) if r["plan"] == "uldaman"]


def leg_rows(text, plan):
    """The ai_dungeon_leg rows of a migration for the plan as {leg: (name, comment)}."""
    return {int(m.group(1)): (m.group(2).replace("''", "'"), m.group(3).replace("''", "'")) for m in
            re.finditer(r"^\('%s', (\d+), '((?:[^']|'')*)', '((?:[^']|'')*)'\)[,;]$" % plan, text, re.M)}


def leg_steps(rows, leg):
    """The rows of one leg in run order: by seq, as the loader seals them, not by their place in the file."""
    return sorted((r for r in rows if r["leg"] == str(leg)), key=lambda r: int(r["seq"]))


def dist(row, x, y):
    return math.hypot(float(row["x"]) - x, float(row["y"]) - y)


def created_columns():
    columns = {}
    for _name, text in migrations():
        for table in TABLES:
            match = re.search(r"CREATE TABLE IF NOT EXISTS `%s` \((.*?)\n\) ENGINE" % table, text, re.S)
            if match:
                columns.setdefault(table, set()).update(re.findall(r"^\s+`(\w+)`", match.group(1), re.M))
            for alter in re.findall(r"ALTER TABLE `%s`(.*?);" % table, text, re.S):
                columns.setdefault(table, set()).update(re.findall(r"ADD COLUMN (?:IF NOT EXISTS )?`(\w+)`", alter))
    return columns


def loader_columns():
    with open(os.path.join(ROOT, "ai/playerbot/DungeonPlanMgr.cpp"), encoding="utf-8") as handle:
        text = handle.read().replace('"\n        "', "").replace('" "', "")
    found = {}
    for select in re.findall(r'"SELECT (.*?) FROM `(\w+)`"', text, re.S):
        found[select[1]] = set(re.findall(r"`(\w+)`", select[0]))
    return found


class DungeonPlanSql(unittest.TestCase):
    def test_tables_exist(self):
        columns = created_columns()
        for table in TABLES:
            self.assertIn(table, columns, "no migration creates %s" % table)

    def test_loader_reads_created_columns(self):
        created = created_columns()
        loaded = loader_columns()
        self.assertEqual(set(loaded), set(TABLES))
        for table, cols in loaded.items():
            missing = cols - created.get(table, set())
            self.assertFalse(missing, "%s: the loader reads columns no migration creates: %s" % (table, missing))

    def test_plan_reexports_delete_their_rows_before_inserting(self):
        # 20261024150700 lost its DELETE FROM `ai_dungeon_step` / `ai_dungeon_leg` lines in a hand edit after
        # export (the exporter emits all three DELETEs unconditionally: internal/planexport/sql.go WriteSQL),
        # so applying it over the live 150100 rows would have failed partway on the leg/step keys. Every
        # migration that inserts a plan's step rows must delete that plan's step and leg rows first, in the
        # same file, before the INSERT. Targeted leg patches (DELETE ... AND `leg` = N, e.g. 20261017230000)
        # are exempt.
        for name, text in migrations():
            for match in re.finditer(r"INSERT INTO `ai_dungeon_(?:step|leg)` .*?\('(\w+)',", text, re.S):
                plan = match.group(1)
                window = text[:match.start()][-600:]
                if re.search(r"DELETE FROM `ai_dungeon_step` WHERE `plan` = '%s' AND `leg` = \d+;" % plan, window):
                    continue
                self.assertIn("DELETE FROM `ai_dungeon_step` WHERE `plan` = '%s';" % plan, text,
                              "%s: inserts %s rows for %s without its step DELETE" % (name, match.group(0)[:40], plan))
                self.assertIn("DELETE FROM `ai_dungeon_leg` WHERE `plan` = '%s';" % plan, text,
                              "%s: inserts %s rows for %s without its leg DELETE" % (name, match.group(0)[:40], plan))
                self.assertLess(text.index("DELETE FROM `ai_dungeon_step` WHERE `plan` = '%s'" % plan), match.start(),
                                "%s: the step DELETE for %s comes after the INSERT" % (name, plan))
                self.assertLess(text.index("DELETE FROM `ai_dungeon_leg` WHERE `plan` = '%s'" % plan), match.start(),
                                "%s: the leg DELETE for %s comes after the INSERT" % (name, plan))


    def test_zulfarrak_keeps_its_crowd_control(self):
        # The Zul'Farrak plan row after all migrations (its newest INSERT, then the UPDATEs after it)
        # carries hand-set cc_always and kill_first (pending pjerra/turtle-agent#190); a re-export from
        # an exporter that does not write them must not land silently.
        row, name = None, None
        for name_, text in migrations():
            # A statement ends with the ';' that ends its line: a comment value may hold one ('live-checked; cut before ...').
            for statement in re.findall(r"(INSERT INTO `ai_dungeon_plan`.*?;$|UPDATE `ai_dungeon_plan`.*?;$)", text, re.S | re.M):
                if statement.startswith("INSERT"):
                    cols, values = re.match(r"INSERT INTO `ai_dungeon_plan` \(([^)]*)\) VALUES\s*(.*);", statement, re.S).groups()
                    columns = re.findall(r"`(\w+)`", cols)
                    for found in re.findall(r"\(('zulfarrak'.*?)\)\s*(?:,|;|$)", values + ";", re.S):
                        fields = [f.strip().strip("'") for f in re.findall(r"'(?:[^']|'')*'|[^,\s][^,]*", found)]
                        row, name = dict(zip(columns, fields)), name_
                elif row is not None and re.search(r"WHERE\s+`plan`\s*=\s*'zulfarrak'", statement):
                    for column, value in re.findall(r"`(\w+)`\s*=\s*'((?:[^']|'')*)'", statement.split("WHERE")[0]):
                        row[column] = value
                    name = name_
        self.assertIsNotNone(row, "no migration inserts the zulfarrak plan row")
        for column in ("cc_always", "kill_first"):
            self.assertTrue(row.get(column), "%s: the zulfarrak plan row has no %s" % (name, column))
        self.assertEqual(row.get("cc_always"), "7246")
        # 20261021005900 (the plan through the stairs event): the exporter writes the lists now; kill_first is the owner's
        # 8179,5650,7247 with the Ward of Zum'rah 7785 first (his leg is in the rows), and cc_priority stays the owner's list
        # (hand-set there: the plan file dropped 7247).
        # 20261021174716 (Gahz'rilla and Bly in the rows): Sergeant Bly's crew follows in his boss.first order, Murta 7608, Oro 7606,
        # Raven 7605, because his units hold has no kill order of its own (the plan-wide kill_first is focused in every fight).
        self.assertEqual(row.get("kill_first"), "7785,8179,5650,7247,7608,7606,7605")
        self.assertEqual(row.get("cc_priority"), "7246,7247,5648")

    def test_uldaman_keeps_its_event_steps(self):
        # The newest migration that inserts Uldaman's steps carries the two scripted events as hold rows
        # (#984; set by hand in 20261016120000, exported since 20261016220000): a re-export from an
        # exporter that has no `hold` kind must not drop them silently.
        newest = None
        for name, text in migrations():
            if re.search(r"INSERT INTO `ai_dungeon_step`.*?\('uldaman',", text, re.S):
                newest = (name, text)
        self.assertIsNotNone(newest, "no migration inserts the uldaman steps")
        name, text = newest
        for data, units in (("1", "'4857'"), ("2", "'2748,7309,7077,7076,10120'")):
            self.assertRegex(text, r"\('uldaman', \d+, \d+, 'hold', %s, [^)]*, 3, 40, 600, %s," % (data, units),
                             "%s: the uldaman hold on instance data %s is missing" % (name, data))
        self.assertEqual(len(re.findall(r"\('uldaman', \d+, \d+, 'ritual', \d+, [^)]*?, 0, 0, 3, 0, 0,", text)), 2,
                         "%s: the uldaman rituals need param 3" % name)

    def test_ragefire_wailing_deadmines_are_whole(self):
        # 20261017130000 exports the three plans (turtle-agent plan-export): each is deleted, then inserted, with
        # its bosses in leg order; the Wailing Caverns gm_cross is the only move_direct (a straight walk down to the
        # Serpentis ramp), and the Deadmines doors, gunpowder and cannon are plain steps (#984: nothing dropped silently).
        text = dict(migrations()).get("20261017130000_world.sql")
        self.assertIsNotNone(text, "20261017130000_world.sql is missing")
        bosses = {"ragefire_chasm": ["11517", "11520", "11518", "11519"],
                  "wailing_caverns": ["3669", "3670", "3674", "3673", "5775"],
                  "deadmines": ["644", "642", "61963", "1763", "646", "647", "639"]}
        for plan, entries in bosses.items():
            for table in ("ai_dungeon_step", "ai_dungeon_leg", "ai_dungeon_plan"):
                self.assertIn("DELETE FROM `%s` WHERE `plan` = '%s';" % (table, plan), text)
            self.assertLess(text.index("DELETE FROM `ai_dungeon_step` WHERE `plan` = '%s'" % plan),
                            text.index("INSERT INTO `ai_dungeon_plan` (`plan`, `map`, `cc_priority`, `cc_always`, `kill_first`, `source`, `comment`) VALUES\n('%s'" % plan))
            self.assertEqual(re.findall(r"^\('%s', \d+, \d+, 'boss', (\d+)," % plan, text, re.M), entries)
        self.assertEqual(re.findall(r"^\('(\w+)', \d+, \d+, 'move_direct'", text, re.M), ["wailing_caverns"])
        for kind, count in (("open", 3), ("loot", 1), ("wait_object", 6), ("wait_clear", 2)):
            self.assertEqual(len(re.findall(r"^\('deadmines', \d+, \d+, '%s'," % kind, text, re.M)), count, kind)
        self.assertRegex(text, r"\('deadmines', 9, \d+, 'open', 16398, [^)]*, 0, 5397, 0, 0, 0,")
        self.assertIn("gm_cross", text)  # the header names every step the run leader cannot do

    def test_shadowfang_keep_is_the_whole_plan_as_exported(self):
        # 20261024154000 (turtle-agent bed9fae, 2026-10-24, the r36 slotI Adamant fix on top of 20261024153300,
        # turtle-agent fed4fe0, the r36 slotA + r30 slotB Fenrus fix on top of 20261024050100, turtle-agent d1f521a,
        # the interaction audit's gone-door wait and Sorcerer's Gate steps on top of 20261023200000, turtle-agent
        # c77e8cf, the Data fix of r25 shadowfang_keep slot5): the whole draft is
        # 10 legs. Leg 4 is the courtyard_door leg (the Adamant gossip and the 18895 wait); leg 5 the old leg 4's
        # entrance-side pulls (10, no boss); leg 6 crosses the navmesh stair-top seam with the only move_direct, then the
        # four walkway pulls and Odo; legs 7-9 are Fenrus, Nandos and Arugal. The plan row carries cc_priority
        # 3855,61970,10000; the packs that stand with a boss are that boss row's entries. Legs 0-1 and 3-9 are
        # byte-identical to 20261023180000_world.sql; leg 2 is the r25 step-39 fix (see the test below); leg 7 ends in the
        # voidwalker wait and the Sorcerer's Gate wait (see the test after it). Every pin fails when its row is cut.
        # The Adamant fix changed no row: the re-export is byte-identical to 20261024153300_world.sql in every
        # step/leg/plan row but the plan row's source rev (bed9fae for fed4fe0).
        name, text = newest_plan_text("shadowfang_keep")
        self.assertEqual(name, "20261024154000_world.sql")
        self.assertIn("('shadowfang_keep', 33, '3855,61970,10000', '', '', 'turtle-agent bed9fae dungeons/shadowfang_keep.json', 'plan v1, draft (not mapped, not live-checked)');", text)
        legs = leg_rows(text, "shadowfang_keep")
        self.assertEqual(list(legs[i][0] for i in range(10)),
                         ["rethilgore", "razorclaw_the_butcher", "baron_silverlaine", "commander_springvale",
                          "courtyard_door", "odo_the_blindwatcher_yard", "odo_the_blindwatcher",
                          "fenrus_the_devourer", "wolf_master_nandos", "archmage_arugal"])
        self.assertEqual(len(legs), 10)
        rows = [r for r in self.step_rows(text) if r["plan"] == "shadowfang_keep"]
        self.assertEqual(len(rows), 318)
        self.assertEqual([len([r for r in leg_steps(rows, leg) if r["kind"] == "pull"]) for leg in range(10)],
                         [4, 16, 21, 5, 0, 10, 4, 13, 3, 2])
        self.assertEqual([r["entry"] for r in rows if r["kind"] == "boss"],
                         ["3914", "3886", "3887", "4278", "4279", "4274", "3927", "4275"])
        self.assertEqual({r["kind"] for r in rows}, {"move", "move_direct", "pull", "boss", "gossip", "wait_object", "wait_clear"})
        for leg in (0, 1, 2, 3, 6, 8, 9):
            self.assertEqual(leg_steps(rows, leg)[-1]["kind"], "boss", leg)
        self.assertEqual(leg_steps(rows, 4)[-1]["kind"], "wait_object")
        self.assertEqual([r["kind"] for r in leg_steps(rows, 7)[-6:]],
                         ["move", "move", "move", "boss", "wait_clear", "wait_object"])
        self.assertEqual(leg_steps(rows, 5)[-1]["kind"], "pull")
        self.assertEqual([r["entries"] for r in rows if r["kind"] == "boss"],
                         ["3861,3851", "3875,2110", "", "", "3866,3868", "", "", ""])
        # The Courtyard Door step and its wait: Adamant 3849's menu index 0 (the row's param is 1) and the door 18895
        # open (state active, param 1) within 60 s, or gone after it was seen (flags 256, kPlanFlagGoneIsDone on a
        # wait_object row: the escort of Adamant removes the door a few waypoints after it set it active).
        door = [r for r in leg_steps(rows, 4) if r["kind"] in ("gossip", "wait_object")]
        self.assertEqual([(r["kind"], r["entry"], r["param"]) for r in door], [("gossip", "3849", "1"), ("wait_object", "18895", "1")])
        gossip = door[0]
        self.assertEqual((gossip["x"], gossip["y"], gossip["z"]), ("-243.7", "2113.7", "81.3"))
        self.assertEqual(door[1]["timeout_s"], "60")
        self.assertEqual(door[1]["flags"], "256")
        self.assertEqual(gossip["flags"], "0")
        self.assertEqual((door[1]["x"], door[1]["y"], door[1]["z"]), ("-242.58", "2159.05", "90.62"))
        # The one move_direct: the stair-top seam crossing, 2.0 yd off the walkway on the far side.
        direct = [r for r in rows if r["kind"] == "move_direct"]
        self.assertEqual([(r["leg"], r["x"], r["y"], r["z"]) for r in direct], [("6", "-266.5", "2150.2", "95.9")])
        self.assertLess(float(direct[0]["x"]) + 264.8, -1.0)  # west of the seam's A edge
        self.assertEqual(re.findall(r"(?m)^DELETE FROM `ai_dungeon_step` WHERE `plan` = '(\w+)';", text), ["shadowfang_keep"])
        self.assertIn("(33, 4274, 'Fenrus the Devourer')", text)
        self.assertIn("(33, 3927, 'Wolf Master Nandos')", text)
        self.assertIn("(33, 4275, 'Archmage Arugal')", text)
        # Legs 0-1 and 3-9 unchanged by the r25 fix: every row of the map-33 export before it is the same text in both files
        # (20261023180000 against 20261023200000, which is no longer the newest).
        old = dict(migrations())["20261023180000_world.sql"]
        mid = dict(migrations())["20261023200000_world.sql"]
        old_rows = [r for r in self.step_rows(old) if r["plan"] == "shadowfang_keep" and int(r["leg"]) not in (2,)]
        mid_rows = [r for r in self.step_rows(mid) if r["plan"] == "shadowfang_keep" and int(r["leg"]) not in (2,)]
        self.assertEqual(len(old_rows), 282)
        self.assertEqual(old_rows, mid_rows)
        # The 2026-10-24 migration against 20261023200000: the same rows, except the flag and comment of leg 4's wait (row 47)
        # and the row leg 7 gained after Fenrus (seq 15). The 20261024153300 migration keeps that wait and adds the
        # boss approach (seq 14-16), the boss reseq and the wait_clear (seq 18); 20261024154000 keeps all of that (the
        # Adamant fix changed no row); byte-identity against 20261023200000 is checked on the earlier pair only, not here.
        mid_all = [r for r in self.step_rows(mid) if r["plan"] == "shadowfang_keep"]
        self.assertEqual(len(mid_all), 313)
        rest = [r for r in rows if not (r["leg"] == "7" and int(r["seq"]) >= 14)]
        old_rest = [r for r in mid_all if not (r["leg"] == "7" and int(r["seq"]) >= 14)]
        self.assertEqual(len(rest), 312)
        self.assertEqual(len(old_rest), 312)
        self.assertEqual([(r["leg"], r["seq"]) for r in rest], [(r["leg"], r["seq"]) for r in old_rest])
        mid_key = {(r["leg"], r["seq"]): r for r in mid_all}
        rows_key = {(r["leg"], r["seq"]): r for r in rows}
        changed = [(k, mid_key[k], rows_key[k]) for k in mid_key if k in rows_key and mid_key[k] != rows_key[k]]
        self.assertEqual([(k[0], k[1], a["kind"]) for k, a, b in changed],
                         [("4", "47", "wait_object"), ("7", "14", "boss")])
        _k, a, b = changed[0]
        self.assertEqual({k for k in a if a[k] != b[k]}, {"flags", "comment"})
        self.assertEqual((a["flags"], b["flags"]), ("0", "256"))
        # leg 7 grew: the boss moved 14 -> 17, the gate wait 15 -> 19, plus 3 approach moves and the wait_clear.
        self.assertFalse(("7", "14") in rows_key and rows_key[("7", "14")]["kind"] == "boss")
        self.assertEqual(rows_key[("7", "17")]["kind"], "boss")

    def test_shadowfang_keep_voidwalkers_and_the_sorcerers_gate(self):
        # 20261024153300, r36 slotA + r30 slotB (shadowfang_keep): Fenrus' death summons four Arugal's Voidwalkers
        # (4627) 14 s later and the fourth voidwalker death opens the Sorcerer's Gate 18972. Leg 7 walks the party up to
        # Fenrus' own floor (three boss-approach moves ending on the ranged stand -152 2168 z128.69), fights the boss,
        # holds on the voidwalker wait_clear until the four are dead, then waits for the gate to be active.
        # (20261024154000 keeps these rows: the Adamant fix changed no row.)
        name, text = newest_plan_text("shadowfang_keep")
        self.assertEqual(name, "20261024154000_world.sql")
        rows = [r for r in self.step_rows(text) if r["plan"] == "shadowfang_keep"]
        fenrus = leg_steps(rows, 7)
        approach, boss, clear, gate = fenrus[-6:-3], fenrus[-3], fenrus[-2], fenrus[-1]
        self.assertEqual([(r["kind"], r["seq"]) for r in approach],
                         [("move", "14"), ("move", "15"), ("move", "16")])
        self.assertEqual([(r["x"], r["y"]) for r in approach],
                         [("-145.07", "2182.13"), ("-150", "2175"), ("-152", "2168")])
        self.assertEqual((boss["kind"], boss["seq"], boss["entry"]), ("boss", "17", "4274"))
        self.assertEqual((clear["kind"], clear["seq"], clear["radius"], clear["timeout_s"]),
                         ("wait_clear", "18", "35", "120"))
        self.assertEqual((clear["x"], clear["y"], clear["z"]), ("-147.1", "2171.6", "128.6"))
        self.assertIn("4627", clear["comment"])
        self.assertEqual((gate["kind"], gate["seq"], gate["entry"], gate["param"], gate["timeout_s"], gate["flags"]),
                         ("wait_object", "19", "18972", "1", "90", "0"))
        self.assertEqual((gate["x"], gate["y"], gate["z"]), ("-129.034", "2166.16", "129.187"))
        self.assertIn("voidwalker", gate["comment"])
        # The header says the module change is needed (a build without it skips the flag-256 row and the plan is refused).
        self.assertIn("NEEDS THE MODULE CHANGE", text)
        self.assertIn("which the run client refuses", text)
        # Only the courtyard door's wait carries the gone flag: the gate is a plain wait for a state a script keeps.
        self.assertEqual([(r["leg"], r["entry"]) for r in rows if r["kind"] == "wait_object" and int(r["flags"]) & 256], [("4", "18895")])

    def test_no_plan_pulls_an_event_unit(self):
        # r36 shadowfang_keep slotI: the walk back replayed step 1 and pulled Deathstalker Adamant 3849, the friendly
        # prisoner whose gossip opens the courtyard door; he died and step 130's gossip could not be done (respawn
        # 4 h 36 min). No plan lists, in a pull/boss row's `entries` (or a boss row's `entry`) or the plan row's
        # cc_priority/cc_always/kill_first, an entry that one of its own gossip/open steps names: such a unit is
        # talked to, not fought. (A hold's entries are its scripted event's units: Zul'Farrak's Bly hold fights
        # Sergeant Bly 7604 after his gossip wakes the event, so holds are out of this pin.) Run over every plan in
        # the repo; shadowfang_keep is pinned clean (3849 in the gossip row only), other failing plans are listed,
        # not fixed here.
        EVENT_KINDS = {"gossip", "open"}
        UNIT_KINDS = ("pull", "boss")
        for name, text in migrations():
            rows = list(self.step_rows(text))
            plans = sorted({r["plan"] for r in rows})
            for plan in plans:
                event_entries = {r["entry"] for r in rows if r["plan"] == plan and r["kind"] in EVENT_KINDS and r["entry"] != "0"}
                if not event_entries:
                    continue
                for r in rows:
                    if r["plan"] != plan or r["kind"] not in UNIT_KINDS:
                        continue
                    listed = set(e for e in r["entries"].split(",") if e.strip()) if r["entries"] else set()
                    if r["kind"] == "boss" and r["entry"] != "0":
                        listed.add(r["entry"])
                    hit = listed & event_entries
                    self.assertEqual(hit, set(), "%s %s: %s/%s lists its own event unit(s) %s in a %s row" %
                                     (name, plan, r["leg"], r["seq"], sorted(hit), r["kind"]))
                for m in re.finditer(r"^\('%s', \d+, '([^']*)', '([^']*)', '([^']*)'," % plan, text, re.M):
                    for listed, what in ((m.group(1), "cc_priority"), (m.group(2), "cc_always"), (m.group(3), "kill_first")):
                        hit = set(e.strip() for e in listed.split(",") if e.strip()) & event_entries
                        self.assertEqual(hit, set(), "%s %s: the plan row lists its own event unit(s) %s in %s" %
                                         (name, plan, sorted(hit), what))
        # shadowfang_keep pinned: the gossip names 3849 and no pull/boss row (or plan list) lists it (or 3850).
        name, text = newest_plan_text("shadowfang_keep")
        rows = [r for r in self.step_rows(text) if r["plan"] == "shadowfang_keep"]
        self.assertEqual({r["entry"] for r in rows if r["kind"] == "gossip"}, {"3849"})
        for r in rows:
            if r["kind"] in UNIT_KINDS:
                listed = set(e for e in r["entries"].split(",") if e.strip()) if r["entries"] else set()
                if r["kind"] == "boss" and r["entry"] != "0":
                    listed.add(r["entry"])
                self.assertNotIn("3849", listed, "shadowfang_keep %s/%s" % (r["leg"], r["seq"]))
                self.assertNotIn("3850", listed, "shadowfang_keep %s/%s" % (r["leg"], r["seq"]))

    def test_shadowfang_keep_step39_split_and_stair(self):
        # 20261023200000, r25 shadowfang_keep slot5 (traces tests/r25/slot5sfk): the step-39 pull was one row at
        # -248.04 2346.95 radius 22.1 (entries 3854,61970,4075,3855) covering 12 living court units, the next pull's
        # Wolfguard and two Darksouls 20 yd off among them; the run wiped twice on it. It is two pulls of three with a
        # stand each (20 yd clear of every pack pulled later) and the rat out, and the walk up to the upper court is
        # three approach moves through the stair's real top. Every pin fails when its row is cut.
        name, text = newest_plan_text("shadowfang_keep")
        self.assertEqual(name, "20261024154000_world.sql")  # 20261023200000 added the split; its leg 2 rows are unchanged since
        rows = [r for r in self.step_rows(text) if r["plan"] == "shadowfang_keep"]
        steps = leg_steps(rows, 2)
        self.assertEqual(len(steps), 31)

        def at(kind, x, y):
            found = [r for r in steps if r["kind"] == kind and dist(r, x, y) < 0.1]
            self.assertEqual(len(found), 1, "%s at %.1f %.1f" % (kind, x, y))
            return found[0]

        # The stair: three moves (turn, the mesh's real top, the landing) right before the Darksoul trio's pull.
        stair = [(float(r["x"]), float(r["y"]), float(r["z"])) for r in steps if r["comment"] == "pull group 2 approach"]
        self.assertEqual(stair, [(-287.2, 2305.1, 90.7), (-284.3, 2320.8, 96.6), (-275.5, 2321.7, 96.2)])
        self.assertEqual([r["seq"] for r in steps if r["comment"] == "pull group 2 approach"], ["3", "4", "5"])
        trio = at("pull", -256.67, 2311.73)
        self.assertEqual((trio["seq"], trio["entries"], trio["radius"]), ("6", "3855", "11"))
        self.assertLess(float(stair[0][2]), float(stair[1][2]))

        # The split: south (2 Wolfguards + a Cleric) and north (a Cleric, a Darksoul and the folded-in Wolfguard),
        # each behind its own stand move, radius 8, no rat and no 3855 in the south row.
        south_stand = at("move", -248.5, 2321)
        south = at("pull", -247.37, 2339.1)
        north_stand = at("move", -250.7, 2332.5)
        north = at("pull", -249.96, 2355.84)
        self.assertEqual([south_stand["seq"], south["seq"], north_stand["seq"], north["seq"]], ["8", "9", "10", "11"])
        self.assertEqual((south["entries"], south["radius"]), ("3854,61970", "8"))
        self.assertEqual((north["entries"], north["radius"]), ("61970,3855,3854", "8"))
        self.assertEqual(north_stand["z"], "96.9")
        self.assertEqual(south_stand["z"], "97")
        # The rat the old row pulled first is out of the split (the leg's other pulls keep theirs).
        self.assertNotIn("4075", south["entries"])
        self.assertNotIn("4075", north["entries"])
        self.assertFalse([r for r in steps if r["kind"] == "pull" and dist(r, -248.04, 2346.95) < 4 and r["radius"] == "22.1"])
        # Both stands keep the 20-yd aggro reach clear of every pull row after them (the wipe's shape; the spawn-level
        # check is turtle-agent's TestShadowfangKeepStep39SplitAndStair).
        later = [r for r in steps if r["kind"] == "pull" and int(r["seq"]) > int(north["seq"])]
        self.assertEqual(len(later), 15)
        for stand in (south_stand, north_stand):
            for r in later:
                self.assertGreaterEqual(dist(r, float(stand["x"]), float(stand["y"])), 20.0, (stand, r))
        # Two pulls of three where the wipe's one row was: leg 2 still has its 21 pulls.
        self.assertEqual(len([r for r in steps if r["kind"] == "pull"]), 21)


    def test_deadmines_pack_fixes(self):
        # 20261025003200 (turtle-agent f2c230c, Deadmines: the hall's golem and Engineer pulls get west stands after the proof20d4
        # wipe) re-exports 20261025002800 (turtle-agent 456efc5, the laboratory and Jared Voss added after mine_a), which re-exports
        # 20261022200000 (turtle-agent 16efe83, r12-19z29 / r14-19z32: the Engineers and Craftsmen in the cc lists, the Sneed patroller's
        # group first, the Harvester room's golem split off, the Gilnid ramp pulled from the spiral and the floor ring camp by camp) with
        # two new legs after leg 0 and the Voss boss row. The old legs 2-15 are legs 4-17 now; every pin below fails when its rows are cut.
        name, text = newest_plan_text("deadmines")
        self.assertEqual(name, "20261025003200_world.sql")
        self.assertIn("('deadmines', 36, '622,1731,4418,1729,1732', '622', '636,622', 'turtle-agent f4344311 dungeons/deadmines.json',", text)
        rows = [r for r in self.step_rows(text) if r["plan"] == "deadmines"]
        self.assertEqual(len(rows), 254)
        self.assertEqual({leg: len(leg_steps(rows, leg)) for leg in (5, 7, 8, 9, 10)}, {5: 11, 7: 9, 8: 11, 9: 41, 10: 8})
        # The laboratory: leg 1 lab_tunnel, leg 2 laboratory. The boss row at Voss's spawn, its approach stand 21.3 yd out, and the pair
        # next to him (2583084/2583081, 15.4 yd from the boss) as one pull from -96 -501.
        lab = leg_steps(rows, 2)
        self.assertEqual([(r["kind"], r["entry"]) for r in lab[-2:]], [("move", "0"), ("boss", "61961")])
        self.assertEqual(lab[-2]["comment"], "boss approach")
        self.assertEqual((lab[-2]["x"], lab[-2]["y"], lab[-2]["z"]), ("-91.5", "-505.5", "54.73"))
        self.assertEqual((lab[-1]["x"], lab[-1]["y"], lab[-1]["z"]), ("-85.063", "-525.839", "54.4025"))

        def pulls(leg):
            return [(r["entries"], int(r["radius"]), round(float(r["x"]), 1)) for r in leg_steps(rows, leg) if r["kind"] == "pull"]

        def stand_before(leg, x, y):
            # the move row right before the pull at (x, y): its approach stand
            steps = leg_steps(rows, leg)
            for i, r in enumerate(steps):
                if r["kind"] == "pull" and abs(float(r["x"]) - x) < 0.02 and abs(float(r["y"]) - y) < 0.02:
                    self.assertEqual(steps[i - 1]["kind"], "move", "the pull at %s %s has no move row before it" % (x, y))
                    self.assertTrue(steps[i - 1]["comment"].endswith("approach"), steps[i - 1]["comment"])
                    return float(steps[i - 1]["x"]), float(steps[i - 1]["y"]), float(steps[i - 1]["z"])
            self.fail("no pull at %s %s in leg %d" % (x, y, leg))

        self.assertEqual(pulls(2)[-1], ("61959,61960", 7, -99.3))
        # Sneed: the group of the Woodcarver 79207's beat (radius 19) is the first pull.
        self.assertEqual(pulls(5)[0], ("641", 19, -277.3))
        self.assertEqual(pulls(5)[1], ("641", 12, -288.3))
        # Harvester room: the Taskmaster over the west half of its beat only (radius 12: the disc stops 17 yd from the Harvester); the Craftsman,
        # the Engineer and the golem that assists them are ONE pull of three from the west stand, 23 yd from the ledge Overseer's beat; the first
        # pull of the room counts the patrolling golem 2582923.
        hv = pulls(8)
        self.assertEqual(hv[0], ("622,61962", 11, -150.3))
        self.assertIn(("4417", 12, -95.9), hv)
        self.assertIn(("1731,622,61962", 9, -87.5), hv)
        self.assertNotIn(("61962", 6, -81.7), hv)
        self.assertEqual(stand_before(8, -87.49, -580.41), (-112.0, -573.0, 52.5))
        # Gilnid ramp: the two terrace Engineers FIRST, from the spiral (z 36.5, over the open pit), then the Craftsman under the spiral from
        # the floor (a spiral stand has no sight of him), then the floor ring.
        gl = pulls(9)
        self.assertEqual(gl[3:], [("622", 10, -202.7), ("1731", 8, -198.9), ("1731,622", 9, -233.8), ("1731,622", 5, -226.1),
                                  ("1731", 12, -209.6), ("1731,622", 8, -208.6), ("622,1731", 11, -187.1), ("1731", 6, -178.4)])
        self.assertEqual(stand_before(9, -202.74, -584.35), (-195.0, -596.0, 36.5))
        self.assertEqual(stand_before(9, -198.93, -603.78), (-215.0, -590.0, 21.1))
        self.assertEqual(stand_before(9, -233.75, -572.8), (-221.0, -596.0, 21.8))
        self.assertEqual(stand_before(9, -208.61, -552.0), (-217.0, -576.0, 21.1))
        self.assertEqual(stand_before(9, -187.05, -551.48), (-207.0, -568.0, 21.1))
        # The boss row is where it was (no boss approach moved) and the pair 79196/79201 is pulled in leg 7 only.
        self.assertEqual([(r["entry"], r["entries"], r["x"], r["y"]) for r in leg_steps(rows, 9) if r["kind"] == "boss"],
                         [("1763", "1731", "-177.362", "-574.458")])
        self.assertNotIn(("622,1731", 11, -187.1), pulls(10))
        # The hall (proof20d4 wipe, 2026-10-26): the golem pull and the two Engineer pulls each walk to a west stand first. Row 123 of
        # 20261025002800 (the golems) was pulled from -207.6 -548.3, south of the inner wall, 23.6 yd away with los no; the run leader
        # walked the tank in to 7.5 yd, the fight sat 6 yd from the 622 at -170.4 -535.2 and three Engineers (the third 20 yd away in
        # harvester group 0) plus a Craftsman joined: 5 deaths, the run's one wipe. The stands below are the plan's own approach points:
        # line of sight to the pack at the core's sight heights on the server's map 36 vmaps, and the next pack 29 yd or more away.
        hall = leg_steps(rows, 7)
        self.assertEqual([(r["kind"], r["comment"]) for r in hall],
                         [("move", "to"), ("pull", "pull group 0"), ("pull", "pull group 1"),
                          ("move", "pull group 2 approach"), ("pull", "pull group 2"),
                          ("move", "pull group 3 approach"), ("pull", "pull group 3"),
                          ("move", "pull group 4 approach"), ("pull", "pull group 4")])
        stands = {2: (-201.0, -534.0, 51.31), 3: (-186.0, -533.0, 51.31), 4: (-176.0, -544.0, 51.26)}
        hall_pulls = {2: ("61962", -185.99, -530.84), 3: ("622", -170.37, -535.16), 4: ("622,1731", -162.19, -555.34)}
        for group, (x, y, z) in stands.items():
            move, pull = hall[2 * group - 1], hall[2 * group]
            self.assertEqual((move["kind"], move["comment"]), ("move", "pull group %d approach" % group))
            self.assertEqual((float(move["x"]), float(move["y"]), float(move["z"])), (x, y, z))
            self.assertEqual((pull["kind"], pull["comment"]), ("pull", "pull group %d" % group))
            self.assertEqual((pull["entries"], float(pull["x"]), float(pull["y"])), hall_pulls[group])
        # The golem pull's stand is 30.6 yd from the Engineer row it used to drag in, and every stand is 29 yd or more from the next
        # pack the plan pulls (2-D, as the run leader sees it); the third Engineer is harvester group 0's, 35.9 yd on.
        self.assertGreaterEqual(dist(hall[3], float(hall[6]["x"]), float(hall[6]["y"])), 29.0)   # golem stand -> the 622 row
        self.assertGreaterEqual(dist(hall[5], float(hall[8]["x"]), float(hall[8]["y"])), 29.0)   # 622 stand -> the next 622 row
        harvester = leg_steps(rows, 8)
        self.assertGreaterEqual(dist(hall[7], float(harvester[1]["x"]), float(harvester[1]["y"])), 29.0)  # -> harvester 0's 622
        self.assertEqual([r["entry"] for r in rows if r["kind"] == "boss"], ["61961", "644", "642", "61963", "1763", "646", "647", "639"])

    def test_scholomance_is_the_plan_with_the_malicia_pack_splits(self):
        # 20261025002400 (turtle-agent 1a544f5, proof20d2 scholomance m910q-4, 2026-10-26): 13 of the run's 15 wipes came
        # before the Vectus leg, 11 of them in leg 0. Group 4 (step 11: 8 at once, the 10489 patroller and the 14861 steward
        # joining) is four single pulls from the stair stand, group 18 (step 41: 21 at once, 54 distinct, 2 wipes / 10
        # deaths) three, and the corpse room (step 61: a single 10481 on group 29's OWN spawn point, pulled from a stand 8-9
        # yd from the unpulled group-31 pack: 24 at once, 3 wipes / 19 deaths) is entered from the ramp, group 31 first and
        # the north-east cluster in two rows. Every pin fails when its rows are cut.
        name, text = newest_plan_text("scholomance")
        self.assertEqual(name, "20261025003000_world.sql")
        self.assertIn("('scholomance', 289, '10470,10472,10476,11257,10469,10475', '', '10471,11582,10477', "
                      "'turtle-agent 15e042b315776a19a0590d8f3ba6f49467cd7c8b dungeons/s',", text)
        rows = [r for r in self.step_rows(text) if r["plan"] == "scholomance"]
        self.assertEqual(len(rows), 589)
        leg0 = leg_steps(rows, 0)
        self.assertEqual(len(leg0), 97)
        pulls = [(r["entries"], float(r["radius"]), r["x"], r["y"]) for r in leg0 if r["kind"] == "pull"]
        self.assertEqual(len(pulls), 45)

        # step 11: the 4-member pack is four single pulls, in order (the healer 10471 first), and the old row is gone.
        self.assertEqual(pulls[4:8], [("10471", 5.0, "240.78", "45.35"), ("10470", 5.0, "241.01", "52.07"),
                                      ("10471", 5.0, "239.28", "53.66"), ("10499", 5.0, "241.6", "48.4")])
        self.assertNotIn(("10471,10470,10499", 5.0, "240.67", "49.87"), pulls)
        # step 41: the 3-member pack is three single pulls; the old row is gone.
        self.assertEqual(pulls[21:24], [("10477", 5.0, "276.2", "150.87"), ("10469", 5.0, "272.36", "155.6"),
                                        ("10498", 5.0, "276.71", "153.88")])
        self.assertNotIn(("10477,10469,10498", 5.0, "275.09", "153.45"), pulls)
        # The corpse room: the single 10481 pull at 221.92 96.4 (group 29's own spawn point, 0.0 yd from its 10485) is gone;
        # the group-31 6-pack is the room's first row, then the north-east cluster in two rows (the deleted unit is in the 4).
        self.assertNotIn(("10481", 5.0, "221.92", "96.4"), pulls)
        self.assertEqual(pulls[33:36], [("10495,10485,10481", 6.1, "200.73", "97.84"),
                                        ("10485", 5.0, "216.28", "99.3"),
                                        ("10485,10481,10495", 5.0, "221.94", "100.15")])

        # The stands: the group-31 row from the ramp 198 118 109.2, the two north-east rows from 201 114 107.8 (a move row
        # before each, as the exporter writes an approach per group), the east 6-pack from its unchanged stand.
        def stand_before(x, y):
            for i, r in enumerate(leg0):
                if r["kind"] == "pull" and r["x"] == x and r["y"] == y:
                    self.assertEqual(leg0[i - 1]["kind"], "move", "the pull at %s %s has no move row before it" % (x, y))
                    self.assertTrue(leg0[i - 1]["comment"].endswith("approach"), leg0[i - 1]["comment"])
                    return leg0[i - 1]["x"], leg0[i - 1]["y"], leg0[i - 1]["z"]
            self.fail("no pull at %s %s in leg 0" % (x, y))

        self.assertEqual(stand_before("200.73", "97.84"), ("198", "118", "109.2"))
        self.assertEqual(stand_before("216.28", "99.3"), ("201", "114", "107.8"))
        self.assertEqual(stand_before("221.94", "100.15"), ("201", "114", "107.8"))
        self.assertEqual(stand_before("217.93", "79.94"), ("226.1333", "102.7743", "106.1562"))
        # The boss rows are still the plan's ten, in leg order.
        self.assertEqual([r["entry"] for r in rows if r["kind"] == "boss"],
                         ["10505", "10507", "10901", "10502", "10503", "11622", "10432", "10508", "11261", "10504"])

    def test_scarlet_library_is_the_plan_with_the_pack_fixes(self):
        # 20261022210600 (turtle-agent prep/scarlet_library, library census 2026-10-22): the draft with the unit lists
        # (fleers 4287 4288 4291 4296 4299, cc 4299 4296, cc_always 4299, kill_first 4291), Loksey's three hounds and
        # Wystan's two camp Monks folded into their boss steps (boss.first), the two patrols on their pulls and the two
        # Doan-hall packs at radius 9. Every pin below fails when its rows are cut.
        name, text = newest_plan_text("scarlet_library")
        self.assertEqual(name, "20261022210600_world.sql")
        self.assertIn("('scarlet_library', 189, '4299,4296', '4299', '4291', 'turtle-agent c670d6e dungeons/scarlet_library.json',", text)
        rows = [r for r in self.step_rows(text) if r["plan"] == "scarlet_library"]
        self.assertEqual(len(rows), 88)
        self.assertEqual({int(leg): len(leg_steps(rows, leg)) for leg in (0, 1, 2)}, {0: 44, 1: 40, 2: 4})
        pulls = {leg: [(r["entries"], r["radius"], r["x"], r["y"]) for r in leg_steps(rows, leg) if r["kind"] == "pull"] for leg in (0, 1, 2)}
        self.assertEqual([len(pulls[0]), len(pulls[1]), len(pulls[2])], [17, 23, 2])
        # Loksey's cage: the hounds are not a pull any more; they are the boss's first.
        self.assertEqual([(r["entry"], r["entries"], r["x"], r["y"]) for r in leg_steps(rows, 0) if r["kind"] == "boss"],
                         [("3974", "4304", "119.938", "-260.869")])
        # Wystan's camp: the two Monks of 2583275/2583276 are the boss's first (and Fireball is a tactic).
        self.assertEqual([(r["entry"], r["entries"], r["x"], r["y"]) for r in leg_steps(rows, 1) if r["kind"] == "boss"],
                         [("61983", "4540", "209.491", "-401.61")])
        # The patrols of 2583271 (the Monk that walks 220.8 -386.7 to 220.9 -338.5 and back) and 40098 (the Chaplain's
        # 8-point beat to x 221.1 -329.2) stand on their pulls in the plan: pull rows of those exact spots still exist.
        self.assertIn(("4540", "10", "220.82", "-386.67"), pulls[1])
        self.assertIn(("4299", "10", "265.03", "-379.05"), pulls[1])
        # The Doan hall: both packs at radius 9, the boss 25.7 yd from the first stand.
        self.assertEqual([(r["entries"], r["radius"], r["x"], r["y"]) for r in leg_steps(rows, 2) if r["kind"] == "pull"],
                         [("4540", "9", "173.97", "-429.18"), ("4291,4540,4299", "9", "181.14", "-443.63")])
        self.assertEqual([r["entry"] for r in rows if r["kind"] == "boss"], ["3974", "61983", "6487"])
        # The boss table seeds the map's Library bosses (the three later wings add theirs with their own files).
        self.assertEqual([(m, e, n) for _name, _text in migrations() for m, e, n in self.boss_rows(_text) if m == 189 and e in (3974, 61983, 6487)],
                         [(189, 3974, "Houndmaster Loksey"), (189, 61983, "Brother Wystan"), (189, 6487, "Arcanist Doan")])


    def test_scarlet_armory_is_the_wing_with_the_census_fixes(self):
        # 20261023030000 (turtle-agent prep/sm-wings, armory census 2026-10-23): 2 legs, 42 pulls, 2 bosses; the six fleers are
        # plan-only, kill_first is the healer 4292 then the casters 4290/4297, and four radii are cut to 10 so a row does not take
        # an unplanned unit of its own entry (the ten patrol pulls have no row form).
        name, text = newest_plan_text("scarlet_armory")
        self.assertEqual(name, "20261023030000_world.sql")
        self.assertIn("('scarlet_armory', 189, '', '', '4292,4290,4297', 'turtle-agent ", text)
        rows = [r for r in self.step_rows(text) if r["plan"] == "scarlet_armory"]
        self.assertEqual(len(rows), 110)
        self.assertEqual({leg: len(leg_steps(rows, leg)) for leg in (0, 1)}, {0: 54, 1: 56})
        pulls = {leg: [(r["entries"], r["radius"], r["x"], r["y"]) for r in leg_steps(rows, leg) if r["kind"] == "pull"] for leg in (0, 1)}
        self.assertEqual([len(pulls[0]), len(pulls[1])], [31, 11])
        self.assertIn(("4286", "10", "1709.52", "-380.76"), pulls[0])   # group 7
        self.assertIn(("4298", "10", "1850.37", "-352.24"), pulls[0])   # group 22
        self.assertIn(("4295,4298", "10", "1878.32", "-384.53"), pulls[0])  # group 27
        self.assertIn(("4298", "10", "1894.39", "-380.89"), pulls[1])   # group 0
        self.assertEqual([(r["entry"], r["entries"]) for r in rows if r["kind"] == "boss"],
                         [("61982", ""), ("3975", "")])

    def test_scarlet_cathedral_is_the_wing_with_the_healer_fix(self):
        # 20261023090000 (turtle-agent cath-fix2, after r23 slot 5): 2 legs, 78 steps, 3 bosses; the pack fixes of
        # 20261023030100 stay (fleers, kill_first the three healers, no cc_always, Fairbanks' Monk as boss.first, the 0.4
        # double pull gone, five radii 9-14). The Mograine/Whitemane event broke in the run (Whitemane at 1 HP, the pair on
        # two legs, the boss corpse found 10 min late): the pair is now one boss hold (instance data 1, script value 4, flag 8
        # alone, radius 35, 600 s, entries 3976 3977) and the separate Whitemane leg is gone; the four entrance-hall packs
        # moved into the Fairbanks leg, so the run does not walk back to the door before Mograine; the chapel packs are
        # pulled from stands more than 30 yd from him (detection 20 yd, his aggro calls every Scarlet within 82 yd).
        name, text = newest_plan_text("scarlet_cathedral")
        self.assertEqual(name, "20261023090000_world.sql")
        self.assertIn("('scarlet_cathedral', 189, '', '', '4303,4300,4299', 'turtle-agent ", text)
        rows = [r for r in self.step_rows(text) if r["plan"] == "scarlet_cathedral"]
        self.assertEqual(len(rows), 78)
        self.assertEqual(leg_rows(text, "scarlet_cathedral"),
                         {0: ("high_inquisitor_fairbanks", ""), 1: ("scarlet_commander_mograine", "")})
        self.assertEqual({leg: len(leg_steps(rows, leg)) for leg in (0, 1)}, {0: 25, 1: 53})
        pulls = {leg: [(r["entries"], r["radius"], r["x"], r["y"]) for r in leg_steps(rows, leg) if r["kind"] == "pull"] for leg in (0, 1)}
        self.assertEqual([len(pulls[0]), len(pulls[1])], [9, 44])
        self.assertIn(("4300,4301,4302", "10", "1140.18", "1340.5"), pulls[0])   # the 0.5 pack, no duplicate 0.4 row
        # The four entrance-hall packs (x 865-890) are Fairbanks-leg pulls now; they were leg 1's last pulls, 250 yd back.
        for hall in (("4298", "15.7", "870.88", "1359.36"), ("4298,4295", "12.5", "870.85", "1406.03"),
                     ("4294,4295", "11.9", "889.97", "1376.13"), ("4298", "10", "890.28", "1360.67")):
            self.assertIn(hall, pulls[0])
            self.assertNotIn(hall, pulls[1])
        self.assertIn(("4303,4540", "9", "1131.07", "1406.46"), pulls[1])         # group 1
        self.assertIn(("4540,4302,4303", "10", "1114", "1400.55"), pulls[1])      # group 2
        self.assertIn(("4303,4540", "10", "1093.91", "1406.32"), pulls[1])        # group 3
        self.assertIn(("4301,4302", "10", "1101.94", "1422.63"), pulls[1])        # group 37
        self.assertIn(("4300,4301,4302", "14", "1112.35", "1376.42"), pulls[1])   # group 43
        self.assertFalse([r for r in pulls[0] if r[2] == "1134.68" and r[3] == "1338.78"])
        # The pair is one hold and it ends the plan: entry 1 is the instance data, param 4 its script value (0 not started,
        # 1 in progress, 2 Mograine died once, 3 revived, 4 both dead: not an encounter state, so flag 8 alone, never 40), and
        # both entries are the event's mobs.
        holds = [r for r in rows if r["kind"] == "hold"]
        self.assertEqual(len(holds), 1)
        h = holds[0]
        self.assertEqual((h["leg"], h["seq"], h["entry"], h["param"], h["flags"], h["radius"], h["timeout_s"], h["entries"]),
                         ("1", "52", "1", "4", "8", "35", "600", "3976,3977"))
        self.assertEqual((h["x"], h["y"], h["z"]), ("1140", "1398", "31.0314"))
        self.assertEqual(leg_steps(rows, 1)[-1]["kind"], "hold")
        self.assertAlmostEqual(dist(h, 1153.87, 1398.39), 13.9, places=1)  # 14 yd west of Mograine, 62 yd off Whitemane
        # No boss row for Whitemane and no row on her spawn (the pair is fought where Mograine stands).
        self.assertEqual([(r["entry"], r["entries"], r["x"], r["y"]) for r in rows if r["kind"] == "boss"],
                         [("4542", "4540", "1158.86", "1354.27")])
        self.assertFalse([r for r in rows if r["x"] == "1202.13" and r["y"] == "1399.07"])
        # Every chapel stand in leg 1 (an approach move) keeps more than 30 yd from Mograine.
        stands = [r for r in leg_steps(rows, 1) if r["kind"] == "move" and r["comment"].endswith("approach")]
        self.assertEqual(len(stands), 7)
        for r in stands:
            self.assertGreater(dist(r, 1153.87, 1398.39), 30.0, r["comment"])
        # The boss table's DELETE is scoped to this wing's three entries, never all of map 189 (four plans share the map).
        self.assertIn("DELETE FROM `ai_dungeon_boss` WHERE `map` IN (189) AND `entry` IN (4542, 3976, 3977);", text)

    def test_scarlet_graveyard_is_the_wing_with_the_census_fixes(self):
        # 20261023060000 (graveyard census 2026-10-23, then r23 slot 6): 5 legs, 47 pulls, 4 bosses; 6547 (faction 68 Horde
        # civilian) and 4075 (rat) are out of every group and four pure groups are gone; Vishas' Scryer and Scorn's guard are
        # boss.first; the hall leg's trash stays on the way to Thalnos and the Duke leg is third, its 36-spawn crypt pull
        # (radius 56.8) the plan's one needs_walk point; Scorn is a boss-only last leg (his spawn is in the inactive game event
        # 81, r23 paused on him as step 51 of 133) and the two leg-0 fights that killed the lead and wiped the party are split
        # into pulls of at most 3-4 census units.
        name, text = newest_plan_text("scarlet_graveyard")
        self.assertEqual(name, "20261023060000_world.sql")
        self.assertIn("('scarlet_graveyard', 189, '', '', '6490', 'turtle-agent 38db284 dungeons/scarlet_graveyard.json',", text)
        self.assertEqual(leg_rows(text, "scarlet_graveyard"),
                         {0: ("interrogator_vishas", ""), 1: ("graveyard_hall", ""), 2: ("bloodmage_thalnos", ""),
                          3: ("duke_dreadmoore", ""), 4: ("scorn", "")})
        rows = [r for r in self.step_rows(text) if r["plan"] == "scarlet_graveyard"]
        self.assertEqual(len(rows), 154)
        self.assertEqual({leg: len(leg_steps(rows, leg)) for leg in (0, 1, 2, 3, 4)}, {0: 15, 1: 37, 2: 29, 3: 50, 4: 23})
        pulls = {leg: [(r["entries"], r["radius"], r["x"], r["y"]) for r in leg_steps(rows, leg) if r["kind"] == "pull"]
                 for leg in (0, 1, 2, 3, 4)}
        self.assertEqual([len(pulls[0]), len(pulls[1]), len(pulls[2]), len(pulls[3]), len(pulls[4])], [13, 19, 11, 6, 0])
        # The split pulls: the 2x4306 group at radius 5 (it reached the patrolling Scryer's spawn at 11.8), the 4-unit
        # 4306/4293 pack as two pairs at radius 2.5, the jailer patrol at radius 5 (it reached the other two jailers at 9.3
        # and 9.5 yd), the crypt cluster as a group of three at radius 9 and the two jailers at radius 6.
        self.assertIn(("4306", "5", "1752.71", "1124.83"), pulls[0])              # group 3, step 5 of the old rows
        self.assertIn(("4306,4293", "2.5", "1755.58", "1145.74"), pulls[0])       # group 4, split pair A
        self.assertIn(("4306,4293", "2.5", "1758.72", "1146.27"), pulls[0])       # group 5, split pair B
        self.assertIn(("61971", "5", "1824.24", "1120.95"), pulls[0])             # group 8, the Jailer's patrol
        self.assertIn(("4306,4293", "9", "1822.84", "1120.48"), pulls[0])         # group 9, three units
        self.assertIn(("61971", "6", "1833.29", "1121.23"), pulls[0])             # group 10, the two jailers
        self.assertIn(("4283", "10", "1841.84", "1136.73"), pulls[0])             # the Sentry's patrol pull
        self.assertIn(("4293,4306,61971", "56.8", "1871.88", "1110.15"), pulls[3])  # the crypt pack, Duke leg
        self.assertIn(("4306,4293", "14", "1859.23", "1158.89"), pulls[3])        # the crypt room of 1.4 after the census
        # Boss order: Vishas, Thalnos, Duke, and Scorn's boss alone as the plan's last step.
        self.assertEqual([r["leg"] for r in rows if r["kind"] == "boss"], ["0", "2", "3", "4"])
        self.assertEqual([(r["entry"], r["entries"]) for r in rows if r["kind"] == "boss"],
                         [("3983", "4293"), ("4543", ""), ("61972", ""), ("14693", "6426")])
        self.assertEqual((rows[-1]["leg"], rows[-1]["kind"], rows[-1]["entry"]), ("4", "boss", "14693"))
        for row in rows:
            if row["kind"] == "pull":
                self.assertNotIn("6547", row["entries"].split(","), "the friendly Suffering Victim is still a pull member")
                self.assertNotIn("4075", row["entries"].split(","), "a Rat is still a pull member")
        # The boss table's DELETE is scoped to this wing's four entries, never all of map 189 (four plans share the map).
        self.assertIn("DELETE FROM `ai_dungeon_boss` WHERE `map` IN (189) AND `entry` IN (3983, 14693, 4543, 61972);", text)

    def step_rows(self, text):
        # Each ai_dungeon_step row of a migration as a dict of its columns, in file order.
        for block in re.findall(r"INSERT INTO `ai_dungeon_step` \(([^)]*)\) VALUES(.*?);\n", text, re.S):
            cols = [c.strip(" `") for c in block[0].split(",")]
            for row in re.findall(r"^\((.*)\)[,]?$", block[1], re.M):
                # A quoted value may hold commas (`entries`); the space after each comma is skipped before the match.
                values = [v.strip() for v in re.findall(r"\s*('(?:[^']|'')*'|[^,]+)", row)]
                yield dict(zip(cols, [v.strip("'") for v in values]))

    def test_stormwind_stockade_is_the_plan_with_the_pack_fixes(self):
        # 20261022210100 (turtle-agent 1bb93ed, 2026-10-22): the Stormwind Stockade draft with the pack fixes of PREP-STOCKADE-2026-10-22.md, no row
        # hand-set, with the boss rows of map 34. 5 legs, 95 steps (14 + 8 + 9 + 3 + 0 pulls), bosses in leg order, no door or event step. The plan row
        # has the unit lists; the packs that stand with a boss are that boss row's entries, not pulls; Bruegal 1720 is in a pull, not skipped.
        name, text = newest_plan_text("stormwind_stockade")
        self.assertEqual(name, "20261022210100_world.sql")
        self.assertIn("('stormwind_stockade', 34, '1711,1715,1706', '1711', '1717', 'turtle-agent 1bb93ed dungeons/stormwind_stockade.json', 'plan v1, draft (not mapped, not live-checked)');", text)
        self.assertEqual(len(leg_rows(text, "stormwind_stockade")), 5)
        rows = [r for r in self.step_rows(text) if r["plan"] == "stormwind_stockade"]
        self.assertEqual(len(rows), 95)
        self.assertEqual([len([r for r in leg_steps(rows, leg) if r["kind"] == "pull"]) for leg in range(5)], [14, 8, 9, 3, 0])
        self.assertEqual([r["entry"] for r in rows if r["kind"] == "boss"], ["1696", "1663", "1666", "1717", "1716"])
        self.assertEqual({r["kind"] for r in rows}, {"move", "pull", "boss"})
        # The last step of every leg is its boss; the boss rows' entries are the packs that stand with them (Hamhock's guards are a creature_group
        # and come with him: his row names none, since the 30 yd of an entries list would hold the Insurgent of Bazil's pair).
        for leg in range(5):
            self.assertEqual(leg_steps(rows, leg)[-1]["kind"], "boss", leg)
        self.assertEqual([r["entries"] for r in rows if r["kind"] == "boss"], ["1706,1707", "1706,1708", "1715,1708", "", "1715,1711"])
        pulls = [r for r in rows if r["kind"] == "pull"]
        self.assertEqual([r["entries"] for r in pulls if "1720" in r["entries"].split(",")], ["1708,1706,1720"])
        # No pull of leg 4 (the boss row is the fight), and the Inmates in front of Dextren are the last pull of leg 1 with a radius that stops short of him.
        self.assertEqual(leg_steps(rows, 4)[0]["kind"], "move")
        last = [r for r in leg_steps(rows, 1) if r["kind"] == "pull"][-1]
        self.assertEqual((last["entries"], float(last["radius"])), ("1708", 7.0))
        self.assertEqual(re.findall(r"(?m)^DELETE FROM `ai_dungeon_step` WHERE `plan` = '(\w+)';", text), ["stormwind_stockade"])
        self.assertIn("DELETE FROM `ai_dungeon_boss` WHERE `map` IN (34);", text)

    def test_stormwind_vault_is_the_plan_with_the_leg0_split(self):
        # 20261024151700 (turtle-agent 33dcce5, 2026-10-24): 20261024010200 again, with leg-0 pull group 0 split in
        # three: the census shows the two 60597 and the 60596 unlinked (every map-35 group_leader is 0, no
        # creature_linking row for any map-35 spawn), so each is one single pull in route order (60597 at 1.86 58.70,
        # 60597 at -4.20 59.17, then the 60596 alone at -1.50 67.34), each from the corridor stand (-1.0 47.0
        # z -27.55, 11.8-12.6 yd from its unit, 25+ yd from the next pack). 6 legs, 105 steps
        # (9 + 2 + 10 + 10 + 24 + 0 pulls), bosses in leg order, no door, event or patrol step. The plan row carries kill_first
        # for the four AoE/root casters (cc_priority and cc_always stay empty: no humanoid worth a Polymorph; the lone
        # 60596 keeps the plan-level kill_first for mixed pulls). No pull row reaches
        # a neighbouring spawn (every radius <= 8.5 yd), and no pull is the boss guard the boss row now carries.
        name, text = newest_plan_text("stormwind_vault")
        self.assertEqual(name, "20261024151700_world.sql")
        self.assertIn("('stormwind_vault', 35, '', '', '60602,60596,60601,60603', 'turtle-agent 33dcce5 dungeons/stormwind_vault.json', 'plan v1, draft (not mapped, not live-checked)');", text)
        self.assertEqual(leg_rows(text, "stormwind_vault"),
                         {0: ("volkan_cruelblade", ""), 1: ("damian", ""), 2: ("aszosh_grimflame", ""),
                          3: ("black_bride", ""), 4: ("tham'grarr", ""), 5: ("arc'tiras", "")})
        rows = [r for r in self.step_rows(text) if r["plan"] == "stormwind_vault"]
        self.assertEqual(len(rows), 105)
        self.assertEqual({leg: len(leg_steps(rows, leg)) for leg in range(6)}, {0: 14, 1: 4, 2: 13, 3: 23, 4: 44, 5: 7})
        self.assertEqual([len([r for r in leg_steps(rows, leg) if r["kind"] == "pull"]) for leg in range(6)], [9, 2, 10, 10, 24, 0])
        self.assertEqual([r["entry"] for r in rows if r["kind"] == "boss"], ["80851", "80854", "80853", "80850", "80852", "93107"])
        self.assertEqual([r["entries"] for r in rows if r["kind"] == "boss"], ["", "", "60597", "60604", "", ""])
        self.assertEqual({r["kind"] for r in rows}, {"move", "pull", "boss"})
        for leg in range(6):
            self.assertEqual(leg_steps(rows, leg)[-1]["kind"], "boss", leg)
        pulls = [r for r in rows if r["kind"] == "pull"]
        self.assertLessEqual(max(float(r["radius"]) for r in pulls), 8.5)
        self.assertGreaterEqual(min(float(r["radius"]) for r in pulls), 4.9)
        # The three single pulls open leg 0 in route order, each from the corridor stand.
        leg0 = leg_steps(rows, 0)
        self.assertEqual([(r["kind"], r["entries"]) for r in leg0[:7]],
                         [("move", ""), ("move", ""), ("pull", "60597"), ("move", ""), ("pull", "60597"),
                          ("move", ""), ("pull", "60596")])
        self.assertEqual([(float(r["x"]), float(r["y"])) for r in leg0[1:7:2]], [(-1.0, 47.0)] * 3)
        # The two guard positions are gone as pulls (the boss rows carry their entries); no pull centre sits on them.
        for x, y in ((-5.7097, 181.434), (3.5689, 192.759), (65.0286, 116.118)):
            self.assertFalse([r for r in pulls if math.hypot(float(r["x"]) - x, float(r["y"]) - y) < 1.0], (x, y))
        # Arc'tiras is the last leg with no pull of its own (the west hall is cleared on the way to Tham'Grarr).
        self.assertEqual(leg_steps(rows, 5)[0]["kind"], "move")
        self.assertEqual(re.findall(r"(?m)^DELETE FROM `ai_dungeon_step` WHERE `plan` = '(\w+)';", text), ["stormwind_vault"])
        self.assertIn("DELETE FROM `ai_dungeon_boss` WHERE `map` IN (35) AND `entry` IN (80851, 80854, 80853, 80850, 80852, 93107);", text)

    def test_sunken_temple_boss_order_puts_jammalan_before_the_drakes(self):
        # 20261025000300 (turtle-agent ad23bbb, 2026-10-25): 20261024151900 again, with the r38 slotD polish
        # (RUN-LESSONS.md r38 sunken_temple slotD, PLAN DONE 2472/2472). (1) The island approach: the pause at
        # step 236 ('three moves in a row had no walkable path', lead -448.0 103.2 -191.0) came from the cut
        # row -448.0 103.2 -187.83, which is 3.2 yd above the pool floor the bot stands on (server path query
        # from the lead's real z: NOPATH), so leg 0 group 9 now carries four approach moves up the east ramp
        # (-448 96 -188.9, -440 96 -184.6, -432 96.5 -179.6, -424 99 -174.8), every hop server path-checked
        # from -455.0 95.8 -191.0 (type 0x1 ok). (2) The deferred 5714 pack (r38 step 2194, -467.2 26.3 -67.0,
        # 'never within reach: skipped, not cleared'): its platform is reached only by the ramp from the
        # -53.9 hall (-512 26.4 -53.83, -490.67 21.33 -61.83, -484.53 18.13 -64.93, the stand 19.3 yd from the
        # pack), so the group moves after the hall group (-481 1.4 -53.9) and gains those three approach moves.
        # (3) The wasted-time round trip (r38 steps 1981-2103: the spiral down to the z -148.7 statue ring for
        # old pull groups 9-13 and back up, 1774 yd of rows for 191 yd of ring): those five packs (8311/8384 at
        # -546.35 151.0, -555.54 131.46, -564.03 105.16, -533.0 115.37, -522.8 92.9) move into the atal'alarion
        # leg after its last statue-ring group (-508.5 183.0), 49.6 yd from it and 7.6 yd from the first statue
        # stand; dreamscythe walks group 8 -> group 9 (-555.5 270.6 -90.8 -> -542.9 94.3 -90.9) on the ring in
        # 190.6 yd. Pull rows keep their units and radii (they move nobody); every reordered hop is server
        # path-checked. 20261024151900's own story keeps its pins below.
        # 20261024151900 (turtle-agent a76e004, 2026-10-24): 20261024151400 again, with the Jammal'an-leg
        # corridor split (RUN-LESSONS.md r33s2 slotS steps 901-903, z -90.8): old pull groups 1 (-446.6 -53.7,
        # 5263+5271) and 2 (-462.9 -76.9, 5271+5273+5263) chained (3 wipes; joiners 5263, 5273 and 8317, the
        # spirit a dying 5271 summons, so no census pack to pre-pull). From the map-109 census: group 1 ->
        # 2 pulls (5271 pair radius 6, 5263 single radius 10), group 2 -> 3 pulls (5271 east pair radius 5,
        # 5271+5273 middle radius 5, 5263+5271 west triple radius 8); group 6 moves before them (fought on
        # arrival, no approach row) so no walk crosses an unpulled pack; groups 3/4/5 keep members and radii
        # and gain stands. Every new stand is 15-25 yd from its own pull point on the z -90.8 floor
        # (mesh-checked with plan-check --mmaps), outside every other unpulled unit's aggro (20 yd - level
        # diff, party 52) + 5 yd. 20261024151400's own story stays: one server-path approach waypoint on each
        # of the two long dreamscythe approach hops the live server refuses (2048-node search answers
        # 'unreachable'): leg-3 group-2 approach 1 (-381.6 102.4 -90.83 -> -536.0 152.0 -69.33, 880.7 yd
        # offline) gains -354.13 160.27 -109.03 (halves 448.7 + 437.3 yd, both server type 0x1 ok), and
        # group-3 approach 1 (-568.53 175.47 -69.33 -> -544.18 154.95 -53.32) gains -361.33 28.27 -106.43
        # (halves 476.9 + 411.7 yd, both type 0x1 ok). The r30 run then stopped at the Weaver: alive at 100 %,
        # not pullable, 'took more than 300 s out of combat' twice. The instance script keeps Weaver 5720 and
        # Dreamscythe 5721 VISIBILITY_OFF / NOT_SELECTABLE / IMMUNE_TO_NPC (patrol idle) while m_auiEncounter[2] !=
        # DONE (TYPE_JAMMALAN), and reveals them only in SetData(TYPE_JAMMALAN, DONE)
        # (instance_sunken_temple.cpp:384-408; Shade of Eranikus 5709 is gated the same way, sleeping). The six
        # Atal'ai statues 148830-148835 (guids 27898, 27899, 28111-28114) are used in that exact entry order
        # (a wrong statue triggers a trap and does not advance the counter); the right one sets
        # GO_FLAG_NO_INTERACT on itself and respawns the green light 148883 within INTERACTION_DISTANCE of it.
        # Census unit lists, radii (but for the five new corridor pulls), pulls (but for the corridor split) and
        # boss rows are unchanged. 6 legs, 2472 steps (11 + 37 + 25 + 2 + 1 pulls), boss order 5710, 5720, 5721,
        # 5719, 5709; 6 open_proof rows and 1 hold row in leg 0.
        name, text = newest_plan_text("sunken_temple")
        self.assertEqual(name, "20261025000300_world.sql")
        self.assertIn("('sunken_temple', 109, '5259,5273,5269,5226', '5259', '5271,5280,5291', 'turtle-agent ad23bbb dungeons/sunken_temple.json', 'plan v1, draft (not mapped, not live-checked)');", text)
        self.assertEqual(len(re.findall(r"(?m)^\('sunken_temple', \d+, '(?:[^']|'')*', '(?:[^']|'')*'\)[,;]$", text)), 6)
        self.assertEqual([n.replace("''", "'") for n in re.findall(r"(?m)^\('sunken_temple', \d+, '((?:[^']|'')*)', '(?:[^']|'')*'\)[,;]$", text)],
                         ["atal'alarion", "jammal'an_the_prophet", "weaver", "dreamscythe", "morphaz", "shade_of_eranikus"])
        rows = [r for r in self.step_rows(text) if r["plan"] == "sunken_temple"]
        self.assertEqual(len(rows), 2349)
        self.assertEqual([len([r for r in leg_steps(rows, leg) if r["kind"] == "pull"]) for leg in range(6)], [21, 11, 37, 20, 2, 1])
        self.assertEqual([r["entry"] for r in rows if r["kind"] == "boss"], ["5710", "5720", "5721", "5719", "5709"])
        self.assertEqual({r["kind"] for r in rows}, {"move", "pull", "boss", "open_proof", "hold"})
        self.assertEqual(leg_steps(rows, 0)[-1]["kind"], "hold")
        for leg in range(1, 6):
            self.assertEqual(leg_steps(rows, leg)[-1]["kind"], "boss", leg)
        # The six statue rows are open_proof at the same points as before, radius 10, no item/param/flags/timeout,
        # entries 148883, in the same order.
        leg0 = leg_steps(rows, 0)
        proofs = [r for r in leg0 if r["kind"] == "open_proof"]
        self.assertEqual([(r["entry"], r["x"], r["y"], r["z"]) for r in proofs],
                         [("148830", "-515.553", "95.2582", "-148.74"),
                          ("148831", "-419.849", "94.4837", "-148.74"),
                          ("148832", "-491.4", "135.97", "-148.74"),
                          ("148833", "-491.491", "53.4818", "-148.74"),
                          ("148834", "-443.855", "136.101", "-148.74"),
                          ("148835", "-443.417", "53.8312", "-148.74")])
        for r in proofs:
            self.assertEqual((r["radius"], r["item"], r["param"], r["flags"], r["timeout_s"], r["entries"]),
                             ("10", "0", "0", "0", "0", "148883"), r["seq"])
        # No hold row between the last statue and the boss approach (the old entry-4 flags-40 row is gone);
        # leg 0's last row is the units hold.
        hold = [r for r in leg0 if r["kind"] == "hold"]
        self.assertEqual(len(hold), 1)
        self.assertEqual((hold[0]["entry"], hold[0]["x"], hold[0]["y"], hold[0]["z"]), ("0", "-460", "96.8", "-189.58"))
        self.assertEqual((hold[0]["radius"], hold[0]["param"], hold[0]["flags"], hold[0]["timeout_s"], hold[0]["entries"]),
                         ("30", "0", "0", "300", "8580"))
        self.assertEqual(leg0[-1]["seq"], hold[0]["seq"])
        self.assertIn("8580 counts while still friendly", hold[0]["comment"])
        # The boss approach is the three walkway points (the fourth, the hold spot, is dropped by hold());
        # the hold is the leg's last row, so nothing follows the approach but the hold.
        approach = [r for r in leg0 if r["comment"] == "boss approach"]
        self.assertEqual([(float(r["x"]), float(r["y"]), float(r["z"])) for r in approach],
                         [(-349.6, 99.4, -172.86), (-400.0, 98.2, -172.93), (-435.0, 97.4, -181.49)])
        self.assertLess(leg0.index(proofs[-1]), leg0.index(approach[0]))
        # THE Z FIX: the interpolated rows of old seq 764, 765, 767, 768 (z -177.55, -182.97, -182.09, -174.60 at
        # x -349.7, -399.4, -410.6, -361.2) are gone: nothing is cut between -300 100.5 and the first approach
        # point, or after the hold. No leg-0 move row with x in [-420, -345] stands below z -173.5 (the lessons
        # floor is -172.9 there).
        for r in leg0:
            if r["kind"] == "move" and -420 <= float(r["x"]) <= -345:
                self.assertGreaterEqual(float(r["z"]), -173.5, (r["seq"], r["x"], r["z"]))
        # The walkway via moved with the reorder: Jammal'an's leg (leg 1) now walks it (atal'alarion hold spot
        # back up to -300 100.5 -172.13); the weaver leg starts at Jammal'an's boss with no via rows.
        via = [r for r in leg_steps(rows, 1) if r["comment"] == "via"]
        self.assertEqual([(float(r["x"]), float(r["y"]), float(r["z"])) for r in via],
                         [(-460.0, 96.8, -189.58), (-435.0, 97.4, -181.49), (-400.0, 98.2, -172.93),
                          (-349.6, 99.4, -172.86), (-300.0, 100.5333, -172.13)])
        self.assertEqual([r for r in leg_steps(rows, 2) if r["comment"] == "via"], [])
        # Pull group 8's stand is unchanged: on the pack floor, 15-25 yd from the pack, same z band.
        pg8 = [r for r in leg0 if r["kind"] == "pull" and r["comment"] == "pull group 8"][0]
        stand8 = [r for r in leg0 if r["kind"] == "move" and r["comment"] == "pull group 8 approach"]
        self.assertEqual(len(stand8), 1)
        self.assertEqual([(float(r["x"]), float(r["y"]), float(r["z"])) for r in stand8], [(-455.0, 95.8, -189.55)])
        self.assertLessEqual(math.dist((float(stand8[0]["x"]), float(stand8[0]["y"])),
                                       (float(pg8["x"]), float(pg8["y"]))), 25.0)
        self.assertGreaterEqual(math.dist((float(stand8[0]["x"]), float(stand8[0]["y"])),
                                          (float(pg8["x"]), float(pg8["y"]))), 15.0)
        self.assertLessEqual(abs(float(stand8[0]["z"]) - float(pg8["z"])), 4.0)
        # Jammal'an's Ogom 5711 and Morphaz's Hazzas 5722 are the boss rows' `first` (their creature groups aggro together); the other
        # three have no linked add (no boss row of 8580: leg 0 ends in the units hold).
        self.assertEqual([r["entries"] for r in rows if r["kind"] == "boss"], ["5711", "", "", "5722", ""])
        pulls = [r for r in rows if r["kind"] == "pull"]
        # The three radii that reached the next pack (same entries 14.4-26.8 yd out) and the seven 5291 pulls with its 15 yd wander.
        self.assertEqual(float([r for r in leg_steps(rows, 0) if r["kind"] == "pull"][5]["radius"]), 17.5)
        # The 23 detour pulls have two approach moves each, plus the new single-stand pull group 8, plus the
        # two server-path waypoints (one extra approach move each on dreamscythe groups 2 and 3), plus the ten
        # Jammal'an corridor stands (two dogleg waypoints on the split groups 2 and 4); the last stands on
        # the pack's own floor, 30 yd or less from the pull row (a cut path's move rows may lie between the two
        # approach points and the pull, and the statue walks put open_proof rows between, so find the last approach
        # move scanning back over those kinds).
        approaches = [r for r in rows if r["kind"] == "move" and r["comment"].startswith("pull group ") and r["comment"].endswith(" approach")]
        self.assertEqual(len(approaches), 66)
        self.assertEqual(len({(r["leg"], r["comment"].split()[2]) for r in approaches}), 34)
        # The server-path waypoints: one inserted approach move on each of the two long dreamscythe hops.
        leg3 = leg_steps(rows, 3)
        self.assertIn(("move", "-354.13", "160.27", "-109.03", "pull group 2 approach"),
                      [(r["kind"], r["x"], r["y"], r["z"], r["comment"]) for r in leg3])
        self.assertIn(("move", "-361.33", "28.27", "-106.43", "pull group 3 approach"),
                      [(r["kind"], r["x"], r["y"], r["z"], r["comment"]) for r in leg3])
        # 20261025000300's three fixes. (1) The island approach: group 9's four approach rows climb the east
        # ramp from the pool (-448 96 -188.9, -432 96.5 -179.6, -424 99 -174.8) and end at a ring stand 20 yd
        # from its pull row; the old cut row (-448 103.2 -187.83, 3.2 yd above the pool floor) is gone.
        g9approach = [(r["x"], r["y"], r["z"]) for r in leg0
                      if r["kind"] == "move" and r["comment"] == "pull group 9 approach"]
        self.assertEqual(g9approach,
                         [("-448", "96", "-188.9"), ("-432", "96.5", "-179.6"),
                          ("-424", "99", "-174.8"), ("-499", "136", "-172.8")])
        pg9 = [r for r in leg0 if r["kind"] == "pull" and r["comment"] == "pull group 9"][0]
        stand9 = math.dist((float(g9approach[-1][0]), float(g9approach[-1][1]), float(g9approach[-1][2])),
                           (float(pg9["x"]), float(pg9["y"]), float(pg9["z"])))
        self.assertGreaterEqual(stand9, 15.0)
        self.assertLessEqual(stand9, 25.0)
        self.assertNotIn(("move", "-448.0", "103.2", "-187.83", "approach path"),
                         [(r["kind"], r["x"], r["y"], r["z"], r["comment"]) for r in leg0])
        # The four ramp/ring rows come before the pull, and the ramp rows rise toward the ring (z from -188.9 to -174.8).
        idx = [i for i, r in enumerate(leg0) if r["kind"] == "move" and r["comment"] == "pull group 9 approach"]
        self.assertLess(idx[-1], leg0.index(pg9))
        zs = [float(leg0[i]["z"]) for i in idx[:3]]
        self.assertEqual(zs, sorted(zs))
        # (3) The statue-ring packs now sit in leg 0 after its last statue-ring group (group 15, -508.5 183):
        # four 8311/8384 pulls at the west arc, in the census order, with their radii.
        leg0pulls = [r for r in leg0 if r["kind"] == "pull"]
        self.assertEqual([(r["entries"], r["radius"]) for r in leg0pulls[16:]],
                         [("8311", "10"), ("8311", "10"), ("8384,8311", "16.1"),
                          ("8311", "10"), ("8311,8384", "17.6")])
        self.assertEqual([(r["x"], r["y"], r["z"]) for r in leg0pulls[16:]],
                         [("-546.35", "151", "-147.53"), ("-555.54", "131.46", "-148.8"),
                          ("-564.03", "105.16", "-148.54"), ("-533.01", "115.37", "-148.8"),
                          ("-522.8", "92.88", "-148.74")])
        # The five moved pulls come after the statue-ring group and before the six statue rows.
        self.assertLess(leg0.index(leg0pulls[16]), leg0.index(proofs[0]))
        # Dreamscythe keeps no pull on the statue ring until its group 18 (the old group 23): the round trip to
        # the bottom floor for groups 9-13 is gone, so group 8 (z -90.8) -> group 9 (z -90.9) walks the ring in
        # at most 8 rows, and no leg-3 pull before group 18 stands below z -145.
        leg3pulls = [r for r in leg3 if r["kind"] == "pull"]
        self.assertEqual([(r["x"], r["y"], r["z"]) for r in leg3pulls[8:10]],
                         [("-555.54", "270.64", "-90.83"), ("-542.93", "94.31", "-90.87")])
        between = leg3[leg3.index(leg3pulls[8]) + 1:leg3.index(leg3pulls[9])]
        self.assertLessEqual(len(between), 8)
        for r in leg3pulls[:18]:
            self.assertGreater(float(r["z"]), -145.0, r["comment"])
        # (2) The 5714 platform pull moves after the hall group (-481 1.4 -53.9) and gains the ramp approach:
        # three rows, the last 19.3 yd from the pack.
        self.assertEqual([(r["x"], r["y"], r["z"]) for r in leg3
                          if r["kind"] == "move" and r["comment"] == "pull group 17 approach"],
                         [("-512", "26.4", "-53.83"), ("-490.67", "21.33", "-61.83"),
                          ("-484.53", "18.13", "-64.93")])
        p5714 = [r for r in leg3 if r["kind"] == "pull" and r["entries"] == "5714"][0]
        self.assertEqual(p5714["comment"], "pull group 17")
        self.assertEqual(leg3pulls[16]["entries"], "5259,5270,8318")
        self.assertEqual(leg3pulls[17]["entries"], "5714")
        ramp = [(float(r["x"]), float(r["y"]), float(r["z"])) for r in leg3
                if r["kind"] == "move" and r["comment"] == "pull group 17 approach"]
        d5714 = math.dist(ramp[-1], (float(p5714["x"]), float(p5714["y"]), float(p5714["z"])))
        self.assertGreaterEqual(d5714, 15.0)
        self.assertLessEqual(d5714, 25.0)
        self.assertLessEqual(abs(ramp[-1][2] - float(p5714["z"])), 2.5)
        # The corridor split: leg 1 holds 11 pulls (group 6 first, fought on arrival with no approach row, then
        # the 5271 pair, the 5263 single, the three group-2 splits, groups 3/4/5 with new stands, group 7).
        # 8317 is the spirit a dying 5271 summons (no census pack, so no pull carries it: every pull's entries
        # are census units only).
        leg1 = leg_steps(rows, 1)
        leg1pulls = [r for r in leg1 if r["kind"] == "pull"]
        self.assertEqual([(r["entries"], r["radius"]) for r in leg1pulls],
                         [("8318,5256,5259", "16.1"),
                          ("5263,5271", "14.7"), ("5271", "6"), ("5263", "10"), ("5271", "5"),
                          ("5271,5273", "5"), ("5263,5271", "8"), ("5271,5273,5263,5269", "15.6"),
                          ("5263,5271", "13.3"), ("5271", "12.8"), ("5267,8318,5270,5259", "18.6")])
        for r in leg1pulls:
            self.assertNotIn("8317", r["entries"].split(","))
        # The new stands: dogleg waypoints on split groups 2 and 4, single stands elsewhere; each stand is
        # 15-25 yd from its own pull point on the corridor floor (group 4's stand rides the east rim at -90.11).
        leg1stands = [(r["x"], r["y"], r["z"]) for r in leg1
                       if r["kind"] == "move" and r["comment"].startswith("pull group ") and r["comment"].endswith(" approach")]
        self.assertEqual(leg1stands,
                         [("-485", "-50", "-90.73"), ("-467", "-49.6", "-90.73"), ("-469", "-50", "-90.73"),
                          ("-460", "-49.6", "-90.73"), ("-438", "-58", "-90.11"), ("-462", "-53.5", "-90.73"),
                          ("-475", "-57", "-90.73"), ("-452.5", "-73", "-90.73"), ("-467.5", "-100", "-90.73"),
                          ("-458", "-108", "-90.73")])
        # Pair each pull with its last stand (dogleg first waypoints pair with nothing): g1a/g2e take stands
        # 1/4, the singles take theirs in order; g6 (arrival fight) and group 0 have no stand.
        for (sx, sy, sz), pull in zip([leg1stands[1], leg1stands[2], leg1stands[4], leg1stands[5],
                                       leg1stands[6], leg1stands[7], leg1stands[8], leg1stands[9]],
                                      leg1pulls[2:10]):
            d = math.dist((float(sx), float(sy)), (float(pull["x"]), float(pull["y"])))
            self.assertGreaterEqual(d, 15.0, (pull["comment"], pull["entries"]))
            self.assertLessEqual(d, 25.0, (pull["comment"], pull["entries"]))
        for leg in range(6):
            steps = leg_steps(rows, leg)
            for i, pull in enumerate(steps):
                if pull["kind"] != "pull":
                    continue
                stand = None
                for j in range(i - 1, -1, -1):
                    if steps[j]["kind"] not in ("move", "stand"):
                        break
                    if steps[j]["kind"] == "move" and steps[j]["comment"].endswith(" approach"):
                        stand = steps[j]
                        break
                if stand is None:
                    continue
                d = math.dist((float(stand["x"]), float(stand["y"]), float(stand["z"])),
                              (float(pull["x"]), float(pull["y"]), float(pull["z"])))
                self.assertLessEqual(d, 30.0, (leg, pull["comment"]))
        self.assertEqual(re.findall(r"(?m)^DELETE FROM `ai_dungeon_step` WHERE `plan` = '(\w+)';", text), ["sunken_temple"])
        # Entry-scoped on the shared map: only this plan's six bosses are deleted and replaced (the SM-wing rule on map 189).
        # (the boss rows stay with 20261024030000, which 20261024060100 stacks on)

    def test_razorfen_downs_is_the_plan_with_the_census_fixes(self):
        # 20261023190000 (turtle-agent rfd-fix1 7931a7c, 2026-10-23, after live run r25 slot 5 stopped at step 251): the plan of
        # 20261023130000 again, with an approach list on the seven leg-3 pulls whose pack stands on another layer. The generated
        # Razorfen Downs draft with the census fixes of PREP-RAZORFEN-DOWNS-2026-10-23.md, no row hand-set, with the boss rows of
        # map 129 scoped to its own four entries. 4 legs, 510 steps (10 + 30 + 19 + 20 pulls; 24 of the moves are approach rows),
        # bosses in leg order, no boss.first. The plan row carries cc_priority and kill_first; the critters 2110/4076 are out of
        # every pull, Ragglesnout 7354 is in leg 3's pack, Rakameg's four Worshippers are the last pull of leg 2, and every pull
        # radius stays short of the next pack (<= 16 yd).
        name, text = newest_plan_text("razorfen_downs")
        self.assertEqual(name, "20261023190000_world.sql")
        self.assertIn("('razorfen_downs', 129, '62676,7337,7335,62677', '', '7354,7342,7353,7341', 'turtle-agent 7931a7c dungeons/razorfen_downs.json', 'plan v1, draft (not mapped, not live-checked)');", text)
        legs = leg_rows(text, "razorfen_downs")
        self.assertEqual([legs[i][0] for i in range(4)],
                         ["mordresh_fire_eye", "glutton", "death_prophet_rakameg", "amnennar_the_coldbringer"])
        rows = [r for r in self.step_rows(text) if r["plan"] == "razorfen_downs"]
        self.assertEqual(len(rows), 510)
        self.assertEqual([len([r for r in leg_steps(rows, leg) if r["kind"] == "pull"]) for leg in range(4)], [10, 30, 19, 20])
        self.assertEqual([r["entry"] for r in rows if r["kind"] == "boss"], ["7357", "8567", "62679", "7358"])
        self.assertEqual({r["kind"] for r in rows}, {"move", "pull", "boss"})
        for leg in range(4):
            self.assertEqual(leg_steps(rows, leg)[-1]["kind"], "boss", leg)
        self.assertEqual([r["entries"] for r in rows if r["kind"] == "boss"], ["", "", "", ""])
        pulls = [r for r in rows if r["kind"] == "pull"]
        self.assertFalse([r["entries"] for r in pulls if "2110" in r["entries"].split(",") or "4076" in r["entries"].split(",")])
        self.assertEqual([(r["entries"], float(r["radius"])) for r in pulls if "7354" in r["entries"].split(",")],
                         [("7347,7346,7342,7344,7354", 10.6)])
        self.assertEqual([r for r in leg_steps(rows, 2) if r["kind"] == "pull"][-1]["entries"], "62676")
        self.assertLessEqual(max(float(r["radius"]) for r in pulls), 16.0)
        # 20261023190000: the seven leg-3 approach lists (run r25 slot 5: the party stood on the lower floor while the packs of
        # pull groups 3, 4 and 7 stand on the shelf above, z 39-43). Each list ends on the pack's own layer, within 30 yd of the
        # pull row; the stands are pinned so a re-export that moves one is caught. The rows of a list lie between the pull before
        # it and its own pull; the exporter cuts a hop over 60 yd into "path" rows between them.
        steps3 = leg_steps(rows, 3)
        pos = {id(r): i for i, r in enumerate(steps3)}
        approach = {}
        for r in steps3:
            if r["kind"] == "move" and r["comment"].endswith(" approach"):
                approach.setdefault(r["comment"], []).append(r)
        self.assertEqual(sorted(approach), ["pull group 10 approach", "pull group 13 approach", "pull group 14 approach",
                                            "pull group 2 approach", "pull group 3 approach", "pull group 4 approach",
                                            "pull group 6 approach"])
        order = sorted(approach, key=lambda c: int(c.split()[2]))
        self.assertEqual([len(approach[c]) for c in order], [3, 1, 4, 4, 4, 4, 4])
        stands = {c: (float(approach[c][-1]["x"]), float(approach[c][-1]["y"]), float(approach[c][-1]["z"])) for c in order}
        self.assertEqual(stands, {
            "pull group 2 approach": (2364.35, 991.682, 39.6645),
            "pull group 3 approach": (2350.0, 954.0, 42.8389),
            "pull group 4 approach": (2340.0, 945.0, 28.3757),
            "pull group 6 approach": (2380.0, 936.0, 42.899),
            "pull group 10 approach": (2424.0, 1000.0, 49.5571),
            "pull group 13 approach": (2398.0, 1010.0, 38.3457),
            "pull group 14 approach": (2352.0, 1010.0, 26.4749),
        })
        pulls3 = [r for r in steps3 if r["kind"] == "pull"]
        for c in order:
            n = int(c.split()[2])
            pull = pulls3[n]
            self.assertEqual(steps3[pos[id(pull)] - 1], approach[c][-1], c)
            self.assertLessEqual(math.dist(stands[c], (float(pull["x"]), float(pull["y"]), float(pull["z"]))), 30.0, c)
            lo = pos[id(pulls3[n - 1])] if n else -1
            hi = pos[id(pull)]
            self.assertTrue(all(lo < i < hi for i in (pos[id(r)] for r in approach[c])), c)
        self.assertEqual(re.findall(r"(?m)^DELETE FROM `ai_dungeon_step` WHERE `plan` = '(\w+)';", text), ["razorfen_downs"])
        self.assertIn("DELETE FROM `ai_dungeon_boss` WHERE `map` IN (129) AND `entry` IN (7357, 8567, 62679, 7358);", text)

    def test_maraudon_purple_is_the_split_and_stood_wipe_leg(self):
        # 20261024060500 (turtle-agent mp-fix1 6f182afa1ed5ea3c4639f0f3cf397eeb821989db, the r28b wipe-leg fix on top of
        # the 20261024010000 census fixes of PREP-MARAUDON-PURPLE-2026-10-24.md): leg 0 groups 35-39 (the 2-slimer patrol
        # pair, the 2 Sisters, the 4-sludge pack, the lone lurker and the 5-unit satyr mix the run fought as one 6-8 unit
        # pack on the approach walk around steps 70-74) are 4 pulls in route order, radius 5, one approach stand each on
        # the pack's floor (the run leader's party stands there to pull). 6 legs, 1660 steps (71 + 15 + 17 + 21 + 7 + 10
        # pulls), bosses in leg order, no boss.first. The plan row carries the humanoid casters and kill_first; Meshlok
        # the Harvester 12237 (rank 2) is in the skip list, not a pull.
        name, text = newest_plan_text("maraudon_purple")
        self.assertEqual(name, "20261024060500_world.sql")
        self.assertIn("('maraudon_purple', 349, '11793,11794', '', '12224,13142,12220,12217', 'turtle-agent 6f182afa1ed5ea3c4639f0f3cf397eeb821989db dungeons/m', 'plan v1, draft (not mapped, not live-checked)');", text)
        self.assertEqual(leg_rows(text, "maraudon_purple"),
                         {0: ("lord_vyletongue", ""), 1: ("celebras_the_cursed", ""), 2: ("landslide", ""),
                          3: ("tinkerer_gizlock", ""), 4: ("rotgrip", ""), 5: ("princess_theradras", "")})
        rows = [r for r in self.step_rows(text) if r["plan"] == "maraudon_purple"]
        self.assertEqual(len(rows), 1660)
        self.assertEqual([r["entry"] for r in rows if r["kind"] == "boss"],
                         ["12236", "12225", "12203", "13601", "13596", "12201"])
        self.assertEqual({r["kind"] for r in rows}, {"move", "pull", "boss"})
        self.assertEqual([len([r for r in leg_steps(rows, leg) if r["kind"] == "pull"]) for leg in range(6)],
                         [71, 15, 17, 21, 7, 10])
        for leg in range(6):
            self.assertEqual(leg_steps(rows, leg)[-1]["kind"], "boss", leg)
        pulls = [r for r in rows if r["kind"] == "pull"]
        critters = {"2914", "4076", "6145", "13321"}
        self.assertFalse([r["entries"] for r in pulls if critters & set(r["entries"].split(","))])
        self.assertFalse([r["entries"] for r in pulls if "12237" in r["entries"].split(",")])
        self.assertFalse([r["entries"] for r in pulls if "13743" in r["entries"].split(",")])
        self.assertLessEqual(max(float(r["radius"]) for r in pulls), 8.0)
        # The two Cavern Lurkers 9.1 yd apart are one pull (leg 0 group 39), the rare Meshlok's spawn point among them.
        self.assertEqual([(float(r["x"]), float(r["y"]), float(r["radius"])) for r in pulls
                          if r["entries"] == "12223" and abs(float(r["x"]) - 735.68) < 0.1],
                         [(735.68, -79.53, 5.0)])
        # The wipe leg: the slime pair, the Sisters, the sludge and the lurker folded into the satyr mix, each with
        # its approach stand (the run leader's party stands there to pull).
        by_comment = {r["comment"]: r for r in rows}
        self.assertEqual([(by_comment["pull group %d approach" % g]["x"],
                           by_comment["pull group %d approach" % g]["y"],
                           by_comment["pull group %d approach" % g]["z"]) for g in (35, 36, 37, 38)],
                         [("775", "-170", "-56.6"), ("812.5", "-152.5", "-74.9"),
                          ("775", "-162.5", "-56.3"), ("765", "-135", "-56.3")])
        self.assertEqual([by_comment["pull group %d" % g]["entries"] for g in (35, 36, 37, 38)],
                         ["12221", "11794", "12222", "12216,11791,12217,12223"])
        for g in (35, 36, 37, 38):
            self.assertEqual(by_comment["pull group %d" % g]["radius"], "5", g)
        # Leg 3 group 6: two linked packs interleaved within 8 yd, one 10-unit pull; the exporter lists each entry once.
        ten = [r for r in pulls if r["entries"] == "11789,11783,11784"]
        self.assertEqual(len(ten), 1)
        self.assertEqual(float(ten[0]["radius"]), 5.0)
        self.assertIn("DELETE FROM `ai_dungeon_boss` WHERE `map` IN (349) AND `entry` IN (12236,12225,12203,13601,13596,12201);", text)

    def test_dire_maul_east_is_the_plan_with_the_census_fixes(self):
        # 20261024020300 (turtle-agent 11d68ad, 2026-10-24): the first Dire Maul East export, from the generated draft with the census
        # fixes of PREP-DIRE-MAUL-EAST-2026-10-24.md. 4 legs, 318 steps (52 + 30 + 1 + 6 pulls), bosses in leg order, and the plan row's
        # kill_first 11461,13196,11457 (the Guardian healer, the Phase Lasher volleys, the Hellcaller's Rain of Fire); cc_priority is
        # empty (every trash unit is a demon or an elemental). The draft's five-floor jumble is gone: leg 0 is the courtyard, the south pods and
        # the basin corridor on the walk to Zevrim, leg 1 the rest of the basin, leg 2 Lethtendris' balcony, leg 3 the cave. The Gordok Commons pulls of the back door (11441, 11444,
        # 13036), the Isalien trigger/pair (16045, 16097) and Pimgib (14349) are out of every pull; Pimgib is Lethtendris' first. No pull
        # row stands within 20 yd of a boss, and every radius is the pack's own spread (5-21.5 yd).
        name, text = newest_plan_text("dire_maul_east")
        self.assertEqual(name, "20261024020300_world.sql")
        self.assertIn("('dire_maul_east', 429, '', '', '11461,13196,11457', 'turtle-agent 11d68ad dungeons/dire_maul_east.json', 'plan v1, draft (not mapped, not live-checked)');", text)
        self.assertEqual(leg_rows(text, "dire_maul_east"),
                         {0: ("zevrim_thornhoof", ""), 1: ("hydrospawn", ""), 2: ("lethtendris", ""),
                          3: ("alzzin_the_wildshaper", "")})
        rows = [r for r in self.step_rows(text) if r["plan"] == "dire_maul_east"]
        self.assertEqual(len(rows), 318)
        self.assertEqual({leg: len(leg_steps(rows, leg)) for leg in range(4)}, {0: 147, 1: 65, 2: 30, 3: 76})
        pulls = [r for r in rows if r["kind"] == "pull"]
        self.assertEqual([len([r for r in leg_steps(rows, leg) if r["kind"] == "pull"]) for leg in range(4)], [52, 30, 1, 6])
        self.assertEqual([r["entry"] for r in rows if r["kind"] == "boss"], ["11490", "13280", "14327", "11492"])
        self.assertEqual([r["entries"] for r in rows if r["kind"] == "boss"], ["", "", "14349", ""])
        self.assertEqual({r["kind"] for r in rows}, {"move", "pull", "boss"})
        for leg in range(4):
            self.assertEqual(leg_steps(rows, leg)[-1]["kind"], "boss", leg)
        # the wing's own trash only: no Gordok Commons pack, no Isalien event, no boss pet as a pull
        for e in ("11441", "11444", "13036", "16045", "16097", "14349"):
            self.assertFalse([r for r in pulls if e in r["entries"].split(",")], e)
        self.assertEqual([(r["entries"], float(r["radius"])) for r in pulls if "11456" in r["entries"].split(",") and r["z"] == "16.58"],
                         [("11456", 5.0)])
        self.assertLessEqual(max(float(r["radius"]) for r in pulls), 22.0)
        self.assertGreaterEqual(min(float(r["radius"]) for r in pulls), 5.0)
        # no pull in a boss's sight (20 yd): the closest is the balcony pack at 21 yd from Lethtendris
        bosses = [(float(r["x"]), float(r["y"]), float(r["z"])) for r in rows if r["kind"] == "boss"]
        for r in pulls:
            for bx, by, bz in bosses:
                self.assertGreaterEqual(math.dist((float(r["x"]), float(r["y"]), float(r["z"])), (bx, by, bz)), 20.0,
                                        (r["leg"], r["seq"]))
        self.assertIn("DELETE FROM `ai_dungeon_boss` WHERE `map` IN (429) AND `entry` IN (11490, 13280, 14327, 11492);", text)
        self.assertIn("(429, 11492, 'Alzzin the Wildshaper');", text)

    def test_dire_maul_north_is_the_plan_with_the_census_fixes(self):
        # 20261024150900 (turtle-agent 107dead, 2026-10-24): the first plan of Dire Maul North, rebuilt from the map-429 spawn census of
        # PREP-DIRE-MAUL-NORTH-2026-10-24.md. 5 legs in door order (Mol'dar, the great hall + Fengus, Slip'kik, the Kromcrush tier, the King),
        # 149 steps (19 + 28 + 10 + 10 + 4 pulls, 73 moves, the 5 bosses), no row hand-set. The skeleton's 13 Gordok Spirit pulls (11446,
        # non-attackable) are gone, and the Gordok ogres the generator dropped for their gossip flag (114 rows, 99 spawned) are pull members. The plan row
        # carries cc_priority for the three casters; no fleer and no trash healer in the wing, so kill_first is empty. Cho'Rush 14324 is
        # the King's first, not a pull and not a boss of the table. Leg 0 pull group 2 carries the four approach stands down the g1-g2
        # corridor on the mesh floor (r28c stopped at step 6: the exporter's cut joined the straight corners at z -3.8 while the floor
        # drops to z -25, so 361.7/411.7 floated 21 yd above the mesh); every hop plans complete at 47-49 yd, the last 48 yd from the pack.
        # Leg 2 pulls Slip'kik (14323, stand 545 560) and his guard pack (2 x 11448, stand 575 565) right after the swarmers,
        # before the north-loop walk (r31 wiped at old step 115: the 552.8 563.5 move stood 15-17 yd off Slip'kik's spawn on his
        # 465 yd patrol loop); the old group 8 at the leg's end is gone and the boss step takes him already dead.
        name, text = newest_plan_text("dire_maul_north")
        self.assertEqual(name, "20261024150900_world.sql")
        self.assertIn("('dire_maul_north', 429, '11448,11444,11445', '', '', 'turtle-agent 107dead dungeons/dire_maul_north.json', 'plan v1, draft (not mapped, not live-checked)');", text)
        legs = leg_rows(text, "dire_maul_north")
        self.assertEqual([legs[i][0] for i in range(5)],
                         ["guard_mol_dar", "guard_fengus", "guard_slip_kik", "captain_kromcrush", "king_gordok"])
        rows = [r for r in self.step_rows(text) if r["plan"] == "dire_maul_north"]
        self.assertEqual(len(rows), 149)
        self.assertEqual([len([r for r in leg_steps(rows, leg) if r["kind"] == "pull"]) for leg in range(5)], [19, 28, 10, 10, 4])
        self.assertEqual([r["entry"] for r in rows if r["kind"] == "boss"], ["14326", "14321", "14323", "14325", "11501"])
        self.assertEqual({r["kind"] for r in rows}, {"move", "pull", "boss"})
        for leg in range(5):
            self.assertEqual(leg_steps(rows, leg)[-1]["kind"], "boss", leg)
        self.assertEqual([r["entries"] for r in rows if r["kind"] == "boss"], ["", "", "", "", "14324"])
        pulls = [r for r in rows if r["kind"] == "pull"]
        self.assertFalse([r for r in pulls if {"11446", "14338", "14322"} & set(r["entries"].split(","))])
        # The courtyard's First Pack (pool-of-pools 4294, one variant live): the four variant entries at one centre, radius 13.6.
        self.assertEqual((pulls[0]["entries"], float(pulls[0]["radius"])), ("11441,11444,13036", 13.6))
        # Leg 0 pull group 2 (the east courtyard pack) is walked to down the g1-g2 corridor on the mesh floor: the four approach
        # stands replace the exporter's floating cut (311.73/361.73/411.73/456.27 at z -3.8; the middle two 21 yd above the mesh).
        moves = [r for r in leg_steps(rows, 0) if r["kind"] == "move"]
        self.assertEqual([(r["x"], r["y"], r["z"]) for r in moves[1:5]],
                         [("319.73", "10.93", "-8.64"), ("363.73", "10.93", "-25.28"),
                          ("411.73", "10.93", "-24.85"), ("456.27", "10.93", "-3.82")])
        self.assertTrue(all(r["comment"] == "pull group 2 approach" for r in moves[1:5]))
        # Leg 2 pulls Slip'kik and his guard pack before the north-loop walk: the stands sit 15-25 yd off their own
        # pack on the mesh floor and outside the other's reach, and the old end-of-leg guard pull is gone.
        leg2 = leg_steps(rows, 2)
        leg2pulls = [r for r in leg2 if r["kind"] == "pull"]
        self.assertEqual([(r["entries"], r["comment"]) for r in leg2pulls[4:6]],
                         [("14323", "pull group 4"), ("11448", "pull group 5")])
        leg2moves = [r for r in leg2 if r["kind"] == "move"]
        self.assertEqual([(r["x"], r["y"], r["z"], r["comment"]) for r in leg2moves[6:8]],
                         [("545", "560", "-25.3", "pull group 4 approach"),
                          ("575", "565", "-25.3", "pull group 5 approach")])
        self.assertEqual([r["comment"] for r in leg2pulls],
                         ["pull group %d" % i for i in (0, 1, 2, 3, 4, 5, 6, 7, 8, 9)])
        self.assertGreaterEqual(min(float(r["radius"]) for r in pulls), 5.0)
        self.assertLessEqual(max(float(r["radius"]) for r in pulls), 22.0)
        self.assertEqual(re.findall(r"(?m)^DELETE FROM `ai_dungeon_step` WHERE `plan` = '(\w+)';", text), ["dire_maul_north"])
        self.assertIn("DELETE FROM `ai_dungeon_boss` WHERE `map` IN (429) AND `entry` IN (14326, 14321, 14323, 14325, 11501);", text)
        for entry, boss in ((14326, "Guard Mol'dar"), (14321, "Guard Fengus"), (14323, "Guard Slip'kik"),
                            (14325, "Captain Kromcrush"), (11501, "King Gordok")):
            self.assertIn("(429, %d, '%s')" % (entry, boss.replace("'", "''")), text)

    def test_dire_maul_west_is_the_hall_split_and_the_crystal_event(self):
        # 20261024153600 (turtle-agent 940fc091): the draft with the census fixes, the two r33 rounds and the r36 crystal-event
        # fix. The five CrystalGenerator game objects (177259, 177257, 177258, 179504, 179505) are each used when the four Mana
        # Remnants 11483 within 20 yd of it die (instance_dire_maul.cpp SetData64 TYPE_CRISTAL_EVENT, DoSortCristalsEventMobs);
        # the last one opens the force field 179503 and makes Immol'thar selectable. Each crystal has one four-remnant pull from
        # a stand 15-25 yd off the pack followed by a hold on the crystal's object state (entry the crystal, param 1 active,
        # flags 16 done-by-object, radius 40, timeout 120, entries 11483): the hold fights a remnant the pull left and ends only
        # when the crystal reads active, so a missed guardian pauses at the crystal and not at Immol'thar (r36 step 465 sat
        # 300 s on the boss, unattackable). 20261025001200 (turtle-agent d55bda0, r39s2 slotC) is the correction of the done
        # sign: the crystals spawn GO_STATE_READY (their spawns' DB state 1 is GO_STATE_READY, GameObject.h:603-605) and
        # DoUseDoorOrButton switches a crystal to GO_STATE_ACTIVE (0) when its guard list empties, where it stays (a button
        # with no auto-close), so the done param is 1 (active); 0 (ready) is the state the crystal leaves, and round 6 read
        # the DB 1 as ACTIVE and waited for it (r39s2 waited for 0 for 4 x 120 s after the crystal went 1). The run leader finds
        # the object only within kEntryPointRadius = 30 yd of the hold spot (RemoteCommanderPolicy.h:1181, EntryGameObjectCheck
        # in BotCommands.cpp:7067-7082) and reads its live state in ReadHoldDone (BotCommands.cpp:11174-11196); 179505's stand
        # stays at -132 686.5 (26.5 yd from the crystal, the move of 20261025000800) and the plan is 616 steps.
        # 20261024154700 (turtle-agent 56de96ff, r38 slotB step 29): the z 28.6 upper-west
        # room is left out and r38's lower-hall step-29 fight is four leg-0 pulls (groups 13-16 = W1-W4) with stands 25.9-30.3
        # yd off the pack centres; steps 27/28 move onto the lower floor. 20261025000200 (turtle-agent 4b64803d, r38 slotC2
        # steps 441-442 and 233): the Immol'thar hall is worked from its mouth instead of along the Residual Monstrosities'
        # ring. 11484 (18 spawns) walk ONE 18-point ring around the pit (radius 109-118 yd from -40.45 811.14; the script
        # npc_residual_montruosity keeps them 32-36 yd apart), are not linked (no creature_groups or linking row on the map),
        # knock the tank back with Arcane Blast 22940 and summon 7 Mana Bursts (14397, 60 s) when they die, so they are pulled
        # in parts: six rows at the north ring point -17.6352 697.856 from the hall mouth -18 668 (29.9 yd) and four at the
        # south-east ring point 46.989 881.837 from 85 895 (40.2 yd), radius 30 each, each its own step (its own rest gate). The
        # two Arcane Torrent formations that wander the hall (314853 at 105.188 724.409, 346789 at -38.5111 631.679; 14399 +
        # three 14400, groups flags 3) are pulled FIRST, each its own step, radius 45, from stands off the ring (95 700, 24.6
        # yd; -70 650, 36.4 yd): r38 hop-pulled them mid-walk ("a new unit joined"). The two hall crystals' stands and the three
        # older Torrent pulls' stands are off the ring too (55, 41, 63, 37 and 33 yd from it; the old ones stood 1.5-22 yd from
        # it). Step 233 (leg 2 seq 77, the 11473 patrol on the hall floor) gets a four-point approach that walks the balcony
        # east and down the stairs to its own floor. No CC is added: every hall unit is creature type 4 (elemental) and the run
        # leader's CC set has no spell for one without a warlock (Banish), and cc_always would make the agent's own driver
        # Polymorph an immune unit. 6 legs, 618 steps (34 + 2 + 18 + 13 + 17 + 4 pulls, plus 5 holds), bosses in leg order. The
        # plan row's kill_first is the two healers/regrowthers (14303, 11473), the Polymorphing Sorcerer (11470) and the
        # summoner (11484); cc_priority and cc_always stay empty. The 29 Shen'dralar Wisps (14361), the 16 neutral Highborne
        # Summoners (11466) and the courtyard's Gordok mobs are out of every pull; Revanchion 14690 has a leg of his own;
        # Ferra 14308 is Illyanna's boss.first; Tsu'zee 11467 (rare) is a member of the Kalendris hall's pull.
        name, text = newest_plan_text("dire_maul_west")
        self.assertEqual(name, "20261025001200_world.sql")
        self.assertIn("('dire_maul_west', 429, '', '', '14303,11473,11470,11484', 'turtle-agent d55bda0 dungeons/dire_maul_west.json', 'plan v1, draft (census 2026-10-24, not mapped, not live-checked)');", text)
        legs = leg_rows(text, "dire_maul_west")
        self.assertEqual([legs[i][0] for i in range(6)],
                         ["tendris_warpwood", "revanchion", "illyanna_ravenoak", "magister_kalendris", "immol'thar",
                          "prince_tortheldrin"])
        rows = [r for r in self.step_rows(text) if r["plan"] == "dire_maul_west"]
        self.assertEqual(len(rows), 616)
        self.assertEqual([len(leg_steps(rows, leg)) for leg in range(6)], [133, 22, 181, 93, 105, 82])
        self.assertEqual([len([r for r in leg_steps(rows, leg) if r["kind"] == "pull"]) for leg in range(6)],
                         [34, 2, 18, 13, 17, 4])
        self.assertEqual([r["entry"] for r in rows if r["kind"] == "boss"], ["11489", "14690", "11488", "11487", "11496", "11486"])
        self.assertEqual({r["kind"] for r in rows}, {"move", "pull", "boss", "hold"})
        for leg in range(6):
            self.assertEqual(leg_steps(rows, leg)[-1]["kind"], "boss", leg)
        # The five crystal holds: the object entry, its done state (param 1 active: the crystal spawns GO_STATE_READY and the
        # script switches it to GO_STATE_ACTIVE when its four remnants are down) and the done-by-object flag 16, at the stand of
        # the pull group right before each, with the remnant entry as their only units. The two hall holds stand off the ring
        # (179505's -132 686.5 is 46 yd from its ring path and 26.5 yd from the crystal, inside the run leader's 30 yd object
        # search; 95 880 is 41 yd).
        holds = [r for r in rows if r["kind"] == "hold"]
        self.assertEqual([(r["leg"], r["seq"], r["entry"], float(r["x"]), float(r["y"]), float(r["z"]), r["param"], r["flags"],
                           r["timeout_s"], float(r["radius"]), r["entries"]) for r in holds],
                         [("0", "13", "177259", 32.0, 300.0, -7.5, "1", "16", "120", 40.0, "11483"),
                          ("0", "91", "177257", 130.6, 454.9, 29.2, "1", "16", "120", 40.0, "11483"),
                          ("2", "70", "177258", -102.7, 426.9, 29.2, "1", "16", "120", 40.0, "11483"),
                          ("4", "59", "179505", -132.0, 686.5, -24.52, "1", "16", "120", 40.0, "11483"),
                          ("4", "93", "179504", 95.0, 880.0, -24.19, "1", "16", "120", 40.0, "11483")])
        for r in holds:
            self.assertIn("is active", r["comment"])
        # The stand is within the run leader's 30 yd object search of its crystal (kEntryPointRadius), which 20261025000200's
        # -150 690 was not (30.17 yd, so the hold read no object and could never end: r39 slot6 step 491).
        crystals = {"177259": (16.7865, 277.154, -8.4683), "177257": (111.851, 436.077, 28.6016),
                    "177258": (-85.1898, 444.665, 28.6016), "179504": (71.097, 874.454, -24.5794),
                    "179505": (-130.415, 712.951, -24.579)}
        for r in holds:
            self.assertLessEqual(math.dist(crystals[r["entry"]], (float(r["x"]), float(r["y"]), float(r["z"]))), 30.0,
                                 r["entry"])
        # The r38 step-29 fight split into four pulls (leg 0 groups 13-16) with their stands, and 11442 out of every list.
        l0 = leg_steps(rows, 0)
        self.assertEqual([(r["entries"], float(r["radius"]), r["comment"]) for r in l0 if r["kind"] == "pull" and r["comment"] in
                          ("pull group 13", "pull group 14", "pull group 15", "pull group 16")],
                         [("11475,11472,11471,11473", 23.0, "pull group 13"),
                          ("11471,11472,11475,11473", 19.0, "pull group 14"),
                          ("11472,11471,11475", 18.0, "pull group 15"),
                          ("11469,11470,11476,14398", 6.0, "pull group 16")])
        self.assertEqual([(r["x"], r["y"], r["z"], r["comment"]) for r in l0 if r["comment"] in
                          ("pull group 13 approach", "pull group 14 approach", "pull group 15 approach", "pull group 16 approach")],
                         [("-50", "455", "-3", "pull group 13 approach"),
                          ("-50", "470", "-3", "pull group 13 approach"),
                          ("-65", "480", "-3.8", "pull group 13 approach"),
                          ("-114", "520", "-3.8", "pull group 14 approach"),
                          ("-56", "524", "-3.3", "pull group 15 approach"),
                          ("-48", "490", "-3", "pull group 16 approach")])
        # Steps 27/28 (leg 0 seq 26/27) are the lower-floor walk points the old z 28.68 cut points became.
        self.assertEqual((l0[26]["kind"], float(l0[26]["x"]), float(l0[26]["y"]), float(l0[26]["z"])),
                         ("move", -74.66678, 370.3999, -3.4181242))
        self.assertEqual((l0[27]["kind"], float(l0[27]["x"]), float(l0[27]["y"]), float(l0[27]["z"])),
                         ("move", -55.733448, 370.3999, -3.4181242))
        self.assertFalse([r for r in rows if r["kind"] == "pull" and "11442" in r["entries"].split(",")])
        # The five crystal pulls: four 11483 at radius 18 (the 20 remnants in five groups), each before its own hold.
        crystal_pulls = [r for r in rows if r["kind"] == "pull" and r["entries"] == "11483"]
        self.assertEqual([(r["leg"], float(r["x"]), float(r["y"]), float(r["radius"])) for r in crystal_pulls],
                         [("0", 17.1, 277.3, 18.0), ("0", 111.5, 435.8, 18.0), ("2", -85.3, 445.0, 18.0),
                          ("4", -130.2, 712.8, 18.0), ("4", 70.7, 874.6, 18.0)])
        # Ferra is Illyanna's boss.first (the row's entries); Immol'thar's row names the boss alone (entries empty: the step's
        # own entry is 11496, and the plan refuses the boss in boss.first), and no pull lists the neutral NPCs.
        self.assertEqual([r["entries"] for r in rows if r["kind"] == "boss"], ["", "", "14308", "", "", ""])
        pulls = [r for r in rows if r["kind"] == "pull"]
        self.assertFalse([r["entries"] for r in pulls
                          if {"14361", "11466", "14308", "13036", "14690", "11442"} & set(r["entries"].split(","))])
        # Leg 4 (the r38 slotC2 hall): the leg opens at the hall mouth -18 668 (the old `to` was on the ring), carries its
        # 30-point route from the Kalendris leg as via moves, and pulls the two wandering Arcane Torrent formations first, each
        # its own step with radius 45 and a stand in the north corridor (24.6 and 36.4 yd from the spawn).
        l4 = leg_steps(rows, 4)
        self.assertEqual((l4[0]["kind"], float(l4[0]["x"]), float(l4[0]["y"]), float(l4[0]["z"]), l4[0]["comment"]),
                         ("move", 73.6985, 539.6372, -3.6072, "via"))
        self.assertEqual((float(l4[29]["x"]), float(l4[29]["y"]), float(l4[29]["z"]), l4[29]["comment"]),
                         (-27.4668, 627.7333, -24.6181, "via"))
        self.assertEqual((l4[30]["kind"], float(l4[30]["x"]), float(l4[30]["y"]), float(l4[30]["z"]), l4[30]["comment"]),
                         ("move", -18.0, 668.0, -24.571, "to"))
        self.assertEqual([(r["seq"], float(r["x"]), float(r["y"]), float(r["radius"]), r["entries"]) for r in l4
                          if r["kind"] == "pull" and float(r["radius"]) == 45.0],
                         [("36", 105.19, 724.41, 45.0, "14400,14399"),
                          ("41", -38.51, 631.68, 45.0, "14400,14399")])
        self.assertEqual([(r["x"], r["y"], r["comment"]) for r in l4
                          if r["comment"] in ("pull group 0 approach", "pull group 1 approach")],
                         [("95", "700", "pull group 0 approach"), ("-70", "650", "pull group 1 approach")])
        # 11484 in parts: six pulls at the north ring point from the mouth, four at the south-east one from 85 895, radius 30,
        # each straight after its own stand (and so its own rest gate). The two Arcane Torrent pulls come before the first.
        parts = [r for r in l4 if r["kind"] == "pull" and r["entries"] == "11484"]
        self.assertEqual([(r["seq"], float(r["x"]), float(r["y"]), float(r["z"]), float(r["radius"])) for r in parts],
                         [("43", -17.64, 697.86, -24.58, 30.0), ("45", -17.64, 697.86, -24.58, 30.0),
                          ("47", -17.64, 697.86, -24.58, 30.0), ("49", -17.64, 697.86, -24.58, 30.0),
                          ("51", -17.64, 697.86, -24.58, 30.0), ("53", -17.64, 697.86, -24.58, 30.0),
                          ("95", 46.99, 881.84, -24.58, 30.0), ("97", 46.99, 881.84, -24.58, 30.0),
                          ("99", 46.99, 881.84, -24.58, 30.0), ("101", 46.99, 881.84, -24.58, 30.0)])
        order = {int(r["seq"]): i for i, r in enumerate(l4)}
        stands = {seq: ("-18", "668") for seq in ("43", "45", "47", "49", "51", "53")}
        stands.update({seq: ("85", "895") for seq in ("95", "97", "99", "101")})
        for p in parts:
            prev = l4[order[int(p["seq"])] - 1]
            self.assertEqual(prev["kind"], "move", p["seq"])
            self.assertTrue(prev["comment"].endswith("approach"), p["seq"])
            self.assertEqual((prev["x"], prev["y"]), stands[p["seq"]], p["seq"])
            # the stand is off the ring by the distance to the pull centre (29.9 and 40.2 yd) and the pull centre is on it
            self.assertLessEqual(dist(prev, float(p["x"]), float(p["y"])), 45.0, p["seq"])
        self.assertLess(order[int(parts[0]["seq"])], order[int([r for r in l4 if r["comment"] == "pull group 8"][0]["seq"])])
        # The three Torrent pulls the plan already had get stands off the ring (they stood 1.7, 22 and 19.7 yd from it).
        self.assertEqual([(r["x"], r["y"], r["comment"]) for r in l4
                          if r["comment"] in ("pull group 10 approach", "pull group 11 approach", "pull group 12 approach",
                                              "pull group 8 approach", "pull group 13 approach")],
                         [("-90", "650", "pull group 8 approach"), ("-132", "686.5", "pull group 8 approach"),
                          ("-180", "710", "pull group 10 approach"), ("-160", "895", "pull group 11 approach"),
                          ("-130", "930", "pull group 12 approach"), ("-40", "958", "pull group 12 approach"),
                          ("20", "948", "pull group 12 approach"), ("75", "900", "pull group 12 approach"),
                          ("95", "880", "pull group 13 approach")])
        # The boss approach is on the mesh (the second point used to stand 4 yd above the pit slope) and both points are the
        # r36 ones; the boss row is Immol'thar alone.
        self.assertEqual([(r["x"], r["y"], r["z"], r["comment"]) for r in l4 if r["comment"] == "boss approach"],
                         [("25.07", "891.66", "-24.25", "boss approach"), ("-7.05", "853.36", "-30.95", "boss approach")])
        self.assertEqual((l4[-1]["kind"], l4[-1]["entry"]), ("boss", "11496"))
        # Step 233: leg 2 group 14 (the 11473 patrol at -101.39 570.07 -4.39) walks down to its own floor first: east along the
        # balcony (29.18 there), the stairs at 125.07 483.2, then the hall floor to -57.07 570.67 (44 yd from the pack).
        il = leg_steps(rows, 2)
        first = [i for i, r in enumerate(il) if r["comment"] == "pull group 14 approach"][0]
        pull14 = [i for i, r in enumerate(il) if r["comment"] == "pull group 14"][0]
        self.assertEqual([(r["x"], r["y"], r["z"], r["comment"]) for r in il[first:pull14 + 1]],
                         [("-61", "551.4", "29.18", "pull group 14 approach"),
                          ("-11.422455", "544.92474", "29.052597", "path"),
                          ("5.866665", "542.6666", "29.181877", "path"),
                          ("7.199998", "542.1333", "29.181877", "path"),
                          ("11.73333", "539.19995", "28.681877", "path"),
                          ("61.73333", "539.19995", "28.681877", "path"),
                          ("111.73333", "539.2", "28.681877", "path"),
                          ("124.8", "539.2", "28.68", "pull group 14 approach"),
                          ("125.06663", "536.8", "28.181877", "path"),
                          ("125.06936", "493.34302", "3.453537", "path"),
                          ("125.07", "483.2", "-2.32", "pull group 14 approach"),
                          ("124.799965", "480.79987", "-3.818124", "path"),
                          ("121.066635", "480.79987", "-3.518124", "path"),
                          ("90.07631", "520.0376", "-3.518124", "path"),
                          ("87.99998", "522.6665", "-3.518124", "path"),
                          ("42.926495", "544.3019", "-4.059006", "path"),
                          ("21.333328", "554.6666", "-4.3181243", "path"),
                          ("-27.656956", "564.66425", "-4.3181243", "path"),
                          ("-57.07", "570.67", "-4.32", "pull group 14 approach"),
                          ("-101.39", "570.07", "-4.39", "pull group 14")])
        self.assertEqual([(r["seq"], r["entries"], float(r["radius"])) for r in il if r["comment"] == "pull group 14"],
                         [("96", "11473", 5.5)])
        # Tsu'zee 11467 is the rare of the Kalendris hall: a member of the pull nearest him (leg 3, pull group 6, radius 8.6).
        self.assertEqual([(r["entries"], float(r["radius"])) for r in pulls if "11467" in r["entries"].split(",")],
                         [("11467,11471", 8.6)])
        # Revanchion's leg pulls the two hall packs the old walk died on (r33 step 128) before his boss row.
        rev = leg_steps(rows, 1)
        self.assertEqual([(r["kind"], r["entries"], r["comment"]) for r in rev if r["kind"] != "move"],
                         [("pull", "11471,11472", "pull group 0"), ("pull", "11472,11471,11475", "pull group 1"),
                          ("boss", "", "boss")])
        # The pit climb of leg 2 group 3 and the east-stairs climb plus the crystal-3 stand of group 7 (the z 28.6 balcony's
        # only walkable link from the hall), and the Tendris boss approach on the mesh (the cutter's 50 yd split put a point
        # in mid air before).
        self.assertEqual([(r["x"], r["y"], r["z"], r["comment"]) for r in il
                          if r["comment"] in ("pull group 3 approach", "pull group 7 approach")],
                         [("21.33", "547.47", "-23.22", "pull group 3 approach"),
                          ("24.8", "450.93", "-22.12", "pull group 3 approach"),
                          ("5.07", "371.47", "-3.02", "pull group 3 approach"),
                          ("-61.33", "515", "-3.52", "pull group 3 approach"),
                          ("-64", "493.6", "-4.32", "pull group 7 approach"),
                          ("125.07", "483.2", "-2.32", "pull group 7 approach"),
                          ("124.8", "539.2", "28.68", "pull group 7 approach"),
                          ("-102.7", "426.9", "29.2", "pull group 7 approach")])
        self.assertEqual([(r["x"], r["y"], r["z"], r["comment"]) for r in leg_steps(rows, 0)
                          if r["comment"] == "boss approach"],
                         [("21.6", "420", "-4.4", "boss approach"), ("21.33", "448", "-20.5", "boss approach")])
        # Radii: the two patrol pulls need 45 yd (a 4-unit formation inside 45 yd of its spawn; the run leader waits up to 60 s
        # for the patroller), the 11484 parts 30 (one unit at the 32-36 yd spacing); every other pull stays at or under 23.
        self.assertLessEqual(max(float(r["radius"]) for r in pulls), 45.0)
        self.assertLessEqual(max(float(r["radius"]) for r in pulls if r["entries"] not in ("14400,14399", "11484")), 23.0)
        self.assertLessEqual(max(len(r["entries"].split(",")) for r in pulls), 5)
        self.assertEqual(re.findall(r"(?m)^DELETE FROM `ai_dungeon_step` WHERE `plan` = '(\w+)';", text), ["dire_maul_west"])
        # The six bosses' ai_dungeon_boss rows are unchanged (seeded by 20261024020500; test_boss_table_lists_every_plan_boss
        # holds them to the boss steps), so this migration re-seeds none.
        self.assertNotIn("`ai_dungeon_boss`", text)

    def test_stratholme_live_is_the_live_side_with_the_census_fixes(self):
        # 20261024060700 (turtle-agent ef83bc5, 2026-10-24): re-export of 20261024020100 (the census of
        # PREP-STRATHOLME-LIVE-2026-10-24.md, no row hand-set) with the Dathrohan fix: Dathrohan (10812) UpdateEntry-transforms
        # into Balnazzar (10813) below 40 % on the same creature, so the boss step's entry is 10813 (a corpse of 10813 ends
        # it; 10812 never leaves one) with 10812 in first, and ai_dungeon_boss seeds 10813. The boss rows of map 329 stay
        # scoped to this plan's boss-step entries, so stratholme_undead's five keep theirs. 3 legs, 195 steps (141 moves,
        # 51 pulls: 42 + 0 + 9; 3 bosses), 199 rows. The 16 neutral citizens (faction 7) are out of every pull and the
        # legs skip 10384/10385/10558, Balzaphon 14684 (event 81, never spawned) is gone, Skul 10393 is listed in the pack he stands in,
        # every radius is the members' spread (<= 18.7 yd), Jarien/Sothos are Dathrohan's first (his leg is a move and the boss) and the
        # Galford hall packs are leg 0's last pulls, before the boss.
        name, text = newest_plan_text("stratholme_live")
        self.assertEqual(name, "20261024060700_world.sql")
        self.assertIn("('stratholme_live', 329, '10419,10425,10426,10422', '', '10423,10421,10420,11032', 'turtle-agent ef83bc5 dungeons/stratholme_live.json', 'plan v1, draft (census-fixed, not mapped, not live-checked)');", text)
        legs = leg_rows(text, "stratholme_live")
        self.assertEqual([legs[i][0] for i in range(3)],
                         ["archivist_galford", "grand_crusader_dathrohan", "cannon_master_willey"])
        rows = [r for r in self.step_rows(text) if r["plan"] == "stratholme_live"]
        self.assertEqual(len(rows), 195)
        self.assertEqual([len([r for r in leg_steps(rows, leg) if r["kind"] == "pull"]) for leg in range(3)], [42, 0, 9])
        self.assertEqual({r["kind"] for r in rows}, {"move", "pull", "boss"})
        self.assertEqual([r["entry"] for r in rows if r["kind"] == "boss"], ["10811", "10813", "10997"])
        self.assertEqual([r["entries"] for r in rows if r["kind"] == "boss"], ["", "16101,16102,10812", ""])
        for leg in range(3):
            self.assertEqual(leg_steps(rows, leg)[-1]["kind"], "boss", leg)
        self.assertEqual([r["kind"] for r in leg_steps(rows, 1)], ["move", "boss"])
        pulls = [r for r in rows if r["kind"] == "pull"]
        self.assertFalse([r["entries"] for r in pulls
                          if {"10384", "10385", "14684"} & set(r["entries"].split(","))])
        self.assertEqual([r["entries"] for r in pulls if "10393" in r["entries"].split(",")],
                         ["10382,10390,10391,10393"])
        self.assertLessEqual(max(float(r["radius"]) for r in pulls), 18.7)
        self.assertEqual(len([r for r in pulls if r["entries"] == "10424,10425"]), 2)
        last0 = leg_steps(rows, 0)[-2]
        self.assertEqual((last0["kind"], round(dist(last0, 3439.0, -3090.0), 1)), ("pull", 0.4))
        self.assertLessEqual(dist(leg_steps(rows, 0)[-1], 3455.96, -3103.41), 0.1)
        self.assertEqual(re.findall(r"(?m)^DELETE FROM `ai_dungeon_step` WHERE `plan` = '(\w+)';", text), ["stratholme_live"])
        self.assertIn("DELETE FROM `ai_dungeon_boss` WHERE `map` IN (329) AND `entry` IN (10811, 10813, 10997);", text)
        self.assertIn("(329, 10811, 'Archivist Galford'),\n(329, 10813, 'Balnazzar'),\n(329, 10997, 'Cannon Master Willey');", text)

    def test_stratholme_undead_has_the_rivendare_room_leg(self):
        # 20261024152300 (turtle-agent prep/stratholme_undead 47c914f, 2026-10-24): 20261024150200 again with the
        # Rivendare pull for RUN-LESSONS r32 slotD (round 1's hold worked through Slaughter Square: every abomination
        # pull passed, the hold ended '13 of 13 down', the Black Guards fought, Ramstein met; but the run ended
        # 'plan done' with Baron Rivendare 10440 ALIVE at 100 %, ~85 yd from the hold spot, never pulled: the plan
        # format cannot hold a boss row after a hold in the same leg, so the boss is a new last leg 5
        # 'baron_rivendare_room'. Its 'to' walks from the hold spot to Rivendare's room (through the gate the event
        # opens) and its boss row pulls 10440 at his spawn (the normal boss row shape, entries ''). Legs 0-4
        # byte-identical to 20261024150200. 6 legs, 253 steps (15 + 20 + 22 + 13 + 13 + 0 pulls; 164 moves;
        # 5 boss rows + 1 hold); the new leg's 10 rows are the only step rows added. Boss table unchanged (all five);
        # the ai_boss_tactic row (329, 10440, kill_first) is NOT carried here: 20261024150200 already wrote it with
        # the same values, so 152300 carries no ai_boss_tactic statement at all (a second DELETE+INSERT of the same
        # key fails the unique gate; 20261024152000's plain re-INSERT failed the same way, replaced here).
        name, text = newest_plan_text("stratholme_undead")
        self.assertEqual(name, "20261024152300_world.sql")
        self.assertIn("('stratholme_undead', 329, '', '', '10393,10411,10400,10398', 'turtle-agent 47c914f dungeons/stratholme_undead.json', 'plan v1, draft (not mapped, not live-checked)');", text)
        legs = leg_rows(text, "stratholme_undead")
        self.assertEqual([legs[i][0] for i in range(6)],
                         ["magistrate_barthilas", "nerub'enkan", "maleki_the_pallid",
                          "baroness_anastari", "baron_rivendare", "baron_rivendare_room"])
        rows = [r for r in self.step_rows(text) if r["plan"] == "stratholme_undead"]
        self.assertEqual(len(rows), 253)
        self.assertEqual({r["kind"] for r in rows}, {"move", "pull", "boss", "hold"})
        self.assertEqual(len([r for r in rows if r["kind"] == "move"]), 164)
        self.assertEqual([len([r for r in leg_steps(rows, leg) if r["kind"] == "pull"]) for leg in range(6)],
                         [15, 20, 22, 13, 13, 0])
        bosses = [r for r in rows if r["kind"] == "boss"]
        self.assertEqual([r["entry"] for r in bosses], ["10435", "10437", "10438", "10436", "10440"])
        self.assertEqual([r["entries"] for r in bosses], ["", "", "10398,10400", "", ""])
        for leg in range(4):
            self.assertEqual(leg_steps(rows, leg)[-1]["kind"], "boss", leg)
        # leg 4 still ends in the whole-square units hold (round 1, unchanged)
        leg4 = leg_steps(rows, 4)
        self.assertEqual(leg4[-1]["kind"], "hold")
        hold = [r for r in leg4 if r["kind"] == "hold"]
        self.assertEqual(len(hold), 1)
        self.assertEqual((hold[0]["entry"], hold[0]["x"], hold[0]["y"], hold[0]["z"]), ("0", "4040.01", "-3413.15", "117.77"))
        self.assertEqual((hold[0]["radius"], hold[0]["param"], hold[0]["flags"], hold[0]["timeout_s"], hold[0]["entries"]),
                         ("68", "0", "0", "300", "10440,10416,10417"))
        self.assertIn("10440 counts while still friendly", hold[0]["comment"])
        self.assertEqual(leg4[-1]["seq"], hold[0]["seq"])
        # the 13 Slaughter Square pulls still cover their wander area (radius 18 = spread 0 + 10 over wander 8)
        pulls4 = [r for r in leg4 if r["kind"] == "pull"]
        self.assertEqual(len(pulls4), 13)
        self.assertEqual({r["entries"] for r in pulls4}, {"10416", "10417"})
        for r in pulls4:
            self.assertEqual(float(r["radius"]), 18.0, r["comment"])
        # leg 5 walks from the hold spot to Rivendare's room and pulls him at his spawn
        leg5 = leg_steps(rows, 5)
        self.assertEqual(len(leg5), 10)
        self.assertEqual([r["kind"] for r in leg5], ["move"] * 9 + ["boss"])
        self.assertEqual((leg5[0]["x"], leg5[0]["y"]), ("4036.5332", "-3398.6667"))
        self.assertEqual((leg5[-2]["x"], leg5[-2]["y"], leg5[-2]["z"]), ("4032.79", "-3333.02", "115.061"))
        self.assertEqual(leg5[-2]["comment"], "to")
        boss5 = leg5[-1]
        self.assertEqual((boss5["entry"], boss5["x"], boss5["y"], boss5["z"]), ("10440", "4032.79", "-3333.02", "115.061"))
        self.assertEqual((boss5["entries"], boss5["comment"]), ("", "boss"))
        pulls = [r for r in rows if r["kind"] == "pull"]
        # the neutral units of faction 7 and the crystals are in no pull
        listed = {e for r in pulls for e in r["entries"].split(",") if e}
        self.assertFalse(listed & {"10384", "10385", "10558", "10415"})
        # the two hostile rares are explicit, each in one pack, and are kill_first entries
        self.assertEqual([(r["leg"], float(r["radius"])) for r in pulls if "10393" in r["entries"].split(",")],
                         [("0", 12.5)])
        self.assertEqual([(r["leg"], float(r["radius"])) for r in pulls if "10809" in r["entries"].split(",")],
                         [("2", 5.0)])
        # leg 2 opens with the two corridor packs the draft had at the end of leg 1 (the exporter writes a leg's boss last)
        pulls2 = [r for r in leg_steps(rows, 2) if r["kind"] == "pull"]
        self.assertEqual([(r["entries"], round(dist(r, 3954.9, -3565.8))) for r in pulls2[:1]], [("10398,10400", 0)])
        self.assertLessEqual(dist(pulls2[1], 3970.6, -3546.6), 0.1)
        # the spec's bbox cut this formation in half: the pull at 3543 -3421 is the whole formation plus the two ghouls in it
        cadaver = [r for r in pulls if abs(float(r["x"]) - 3542.65) < 0.1 and abs(float(r["y"]) + 3420.89) < 0.1]
        self.assertEqual([(r["entries"], float(r["radius"])) for r in cadaver],
                         [("10390,10391,10381,10382,10405", 8.9)])
        self.assertEqual(re.findall(r"(?m)^DELETE FROM `ai_dungeon_step` WHERE `plan` = '(\w+)';", text), ["stratholme_undead"])
        self.assertIn("DELETE FROM `ai_dungeon_boss` WHERE `map` IN (329) AND `entry` IN (10435, 10437, 10438, 10436, 10440);", text)
        self.assertEqual([(m, e) for m, e, _n in self.boss_rows(text)],
                         [(329, 10435), (329, 10437), (329, 10438), (329, 10436), (329, 10440)])
        # Rivendare's tactic (329, 10440, kill_first, 0, 0) ships in 20261024150200 with the same values, so this
        # migration carries no ai_boss_tactic statement (DELETE, INSERT and REPLACE all name the table in backticks;
        # the header comment mentions it without backticks and stays).
        self.assertNotIn("`ai_boss_tactic`", text)
        old = dict(migrations())["20261024150200_world.sql"]
        self.assertIn("(329, 10440, 'kill_first', 0, 0, 1, 0, 'Rivendare: engage him the moment he is in combat with the party (r28d slotD)');", old)

    def test_stormwrought_ruins_is_the_census_fixed_draft(self):
        # 20261025002100 (turtle-agent 050c168, 2026-10-25): SW-FIX7. RUN-LESSONS rf6 stormwrought_ruins slot6 (FULL
        # 823/823) paused 3 times at step 249 (leg 5 deathlord_tidebane): the lead stood in the lake at -6271.7
        # -4055.6 216.5, 6.3 yd from -6265.6 -4056 218.2, and a GM move to that point fixed it. The first move after
        # the group-3 pull was -6265.6 -4056 218.189; the lead's post-pull stand -6278.9 -4055.4 218.0 is EAST of it,
        # so the walk went west along y -4055.4, where the client floor is 215.8-217.3 (WMO 215.97-217.19, water
        # 217.81) under a navmesh shelf at 218.7. Group 4 now has two approach rows, -6276 -4059 218.189 (the dry
        # shelf, WMO 218.14) and -6265.6 -4056 218.189 (the old step), so the first move after the pull is the dry via
        # and the exporter's cut rows follow from -6265.6. Every new hop is server-exact 0x1 (0.2-0.5 yd short); the
        # real lead stand -6278.9 -> -6276 is 0x1, 4.7 yd. 824 steps (+1), was 823.
        # 20261025001700 (turtle-agent a2a91b3, 2026-10-25): SW-FIX6. RUN-LESSONS r41 stormwrought_ruins slot6 (FULL
        # 821/821) entered the lake-shore island again at leg 5: the hop from the group-11 approach row -6330 -3942
        # 219.9 to -6340 -3950 218.6 crosses the 8 yd mesh hole at x -6341..-6333, y -3945..-3947 (Floors empty at
        # 210-230; the WMO floor under it 217.3, below the water 217.81; the server's own path point -6333 -3947.2
        # 219.8 is inside it). The lead landed at -6353.8 -3947.3 215.8 (3.1 yd off the mesh), steps 274-277 failed
        # "no walkable path" and only a GM move to -6324.5 -3882 222.5 helped; r37 fell at the same spot. SW-FIX4 had
        # only replaced the RETURN cut. Group 11's approach now crosses y -3946 at x -6329.5 (3.5 yd east of the hole)
        # and stays on the shelf at -6337 -3949.5; groups 12 and 13 get one approach row each on that shelf (-6354
        # -3958, -6366 -3948.5: 24.7 and 23.4 yd from their pack points), so the pack of old step 273 (now 275) has a
        # stand and the fall is gone. Leg 10 (the post-kill points) is unchanged: no point of the pool can be reached
        # from both L and the pool bed (155 ring points queried: never 0x1 from both; the bed is its own island), so a
        # party that chases Eldermaw into the water still leaves by the run leader's rescue. 823 steps.
        # 20261025001300 (turtle-agent b679582, 2026-10-25): SW-ELDERMAW, the plan data of specs/sw-eldermaw.md Spec B.
        # Leg 9's Eldermaw 62661 swims a 440 yd loop in WMO water (surface 202.20, floor 191.2-195.8) whose only navmesh
        # is the pool bed, on its own island: r37 slot6 waited out the search and paused on "alive at ...". Probe P1 (r38
        # eldermaw_p1 slotC3, server 2, 19z54e) killed him from the west lip L = -5842.0 -3807.0 200.7 (124 s of combat,
        # no evade, no deaths, the 50 % regeneration showed) and showed that `cmd move` refuses deep-water points, so the
        # boss row moves to his search centre P = wp3 (-5820.0 -3797.19 201.241) with radius 95 (wp7, his farthest
        # waypoint, is 90.1 yd out), needs_walk, stand_pull and the approach to L, plus skip_unreachable as the fallback:
        # flags 12288 (4096 | 8192). The move to L is the row right before the boss with no approach-path row between
        # them, and the next move (leg 10's `to`) is cut from L, not from P (the party fought at the stand and P is in
        # deep water). 12 legs, 821 steps (224 pulls; 834 rows): leg 9 gains 10 rows (9 cut rows to L and the approach
        # move), leg 10 gains 30 (the cut from L).
        # 20261025000900 (turtle-agent 7e3d371, 2026-10-25): SW-FIX5 adds leg 4 group 7's approach down to the floor of
        # groups 8-11 and a step-31 approach in leg 1. RUN-LESSONS r39s2 stormwrought_ruins slotB stopped at step 183
        # (leg 4 chieftain_stormsong, pull group 11): the exporter's cut for group 7 ended at -6048.5 -4167.5 220.0, a
        # stacked point (Floors 222.79 and 219.99, no link), the party landed on the 222.3-223.3 shelf (live lead
        # -6058.5 -4166.2 224.5), the packs stand at z217.6-218.4 and every pullback refused "no safe stand
        # 15.0-25.0 yd". r37 slot6 walked the same rows and cleared the leg: its party rested at -6057.9 -4136.5 217.5
        # and pulled groups 8-11 from there, so that spot is the group-7 approach's stand (26.7/41.0/22.4/50.9 yd from
        # the four packs). Step 31's pack 62540 (-5765.28 -4278.17 235.33) is 235.2 yd by the only route (the ledge and
        # the pit are cut by a 12 yd void); the r39 party was left 104 yd out on an off-mesh rock (-5868.7 -4267.7
        # 239.7, no floor). Group 2 now carries the four-point corridor route to the pit rim -5795.73 -4268.27 239.66.
        # 12 legs, 781 steps (224 pulls; 794 rows).
        # 20261025000100 (turtle-agent 6cf1575, 2026-10-25): SW-FIX4 re-exports the 20261024154300 plan with leg 5
        # group 14's authored approach. RUN-LESSONS r37 stormwrought_ruins slot6 had stopped at the lake shore: the
        # exporter's cut from group 13 (pull -6389.38 -3947.89 218.72) to group 14 walked y -3947 across the 8 yd
        # mesh hole at x -6341..-6333 (no floor at any height; the lake below 3.4 yd) and put a cut point in it
        # (-6333.07 -3947.2). The lead fell to -6339.4 -3947.3 215.8, steps 264-266 were "no walkable path", and the
        # rescue teleported it back onto the water. Four authored rows now follow the solid shore at y -3950 and the
        # ramp up to the live hand-move point (-6324.5 -3882 222.69), replacing five cut rows: 770 steps.
        # 20261024154300 (turtle-agent bd6ecb2, 2026-10-24): SW-FIX3, the back-half seam bridged after
        # RUN-LESSONS r36 stormwrought_ruins slotJ stopped at step 553. The party stood on the z215 island at
        # -6045.1 -3784.5 215.2-215.9 and the pack 62650/62645 (at -5990.5 -3783.86 219.37) is on the east
        # shelf: the server's path query is NOPATH between them (nearest reachable -6045.1 -3784.5 215.9,
        # 54.7 yd short), the floors are there (215.43 launch, 215.88 mid-gap, 216.88 landing). Leg 7 is split:
        # mycellakos_west keeps the route and pull groups 0-9; leg 8 mycellakos starts with the exact move to
        # the island edge and the 6.3 yd move_hop_exact to the east shelf, then the packs 10-18 (its groups 0-8)
        # and the boss. 12 legs, 781 steps (224 pulls; the seam's three rows, then SW-FIX4's four approach rows in
        # place of five cut rows, SW-FIX5's three for leg 4 group 7 in place of eight and four for leg 1 group 2),
        # bosses in leg order, no boss.first, no
        # door or event step. Every other row is 20261024151200's, shifted (legs 8-10 are 9-11). Leg 5 keeps the
        # SW-FIX1 south-shore order and the SW-FIX2 ledge approach rows (groups 10-11); the 12 water-only
        # z207.06 Razormaw singles, leg 6's 62660 single, leg 7's eight 62655 singles and leg 8's six 62648
        # groups are gone. The nine packs inside a boss's 18 yd detection are still their leg's last pull, and
        # Remains of the Innocent 62670 (boss-grade trash) is still in leg 11 and in kill_first.
        name, text = newest_plan_text("stormwrought_ruins")
        self.assertEqual(name, "20261025002100_world.sql")
        self.assertIn("('stormwrought_ruins', 818, '62535,62540,62541,62543,62651,62659,62663,62536', '', '62650,62647,62539,62669,62670', 'turtle-agent 050c168 dungeons/stormwrought_ruins.json', 'plan v1, draft (not mapped, not live-checked)');", text)
        self.assertEqual(leg_rows(text, "stormwrought_ruins"),
                         {0: ("dagar_the_glutton", ""), 1: ("oronok_torn-heart", ""),
                          2: ("librarian_theodorus", ""), 3: ("duke_balor_the_iv", ""),
                          4: ("chieftain_stormsong", ""), 5: ("deathlord_tidebane", ""),
                          6: ("subjugator_halthas_shadecrest", ""), 7: ("mycellakos_west", ""),
                          8: ("mycellakos", ""), 9: ("eldermaw_the_primordial", ""),
                          10: ("lady_drazare", ""), 11: ("ighal'for", "")})
        rows = [r for r in self.step_rows(text) if r["plan"] == "stormwrought_ruins"]
        self.assertEqual(len(rows), 824)
        self.assertEqual([len([r for r in leg_steps(rows, leg) if r["kind"] == "pull"]) for leg in range(12)],
                         [11, 28, 18, 19, 19, 31, 27, 10, 9, 10, 30, 12])
        self.assertEqual([r["entry"] for r in rows if r["kind"] == "boss"],
                         ["62547", "62548", "62552", "62549", "62551", "62550", "62652", "62664", "62661", "62665", "62671"])
        self.assertEqual({r["kind"] for r in rows}, {"move", "move_hop_exact", "pull", "boss"})
        for leg in list(range(7)) + list(range(8, 12)):
            self.assertEqual(leg_steps(rows, leg)[-1]["kind"], "boss", leg)
        self.assertEqual([r["entries"] for r in rows if r["kind"] == "boss"], [""] * 11)
        pulls = [r for r in rows if r["kind"] == "pull"]
        self.assertLessEqual(max(float(r["radius"]) for r in pulls), 17.7)
        self.assertGreaterEqual(min(float(r["radius"]) for r in pulls), 4.0)
        # SW-FIX2: no water-only pull survives (every pull stands on the ground mesh: no z207.06 Razormaw single,
        # no z192-204 62655 single, no z195-200 62648 group).
        self.assertFalse([r for r in pulls if float(r["z"]) < 205])
        self.assertFalse([r for r in pulls if "62655" in r["entries"].split(",")])
        self.assertFalse([r for r in pulls if "62648" in r["entries"].split(",")])
        # The packs inside a boss's 18 yd detection are the last pull of their leg (the boss joins by design;
        # mycellakos is now leg 8 and lady_drazare leg 10, and mycellakos_west has no boss).
        self.assertEqual([leg_steps(rows, leg)[-2]["entries"] for leg in (1, 3, 5, 8, 10)],
                         ["62540", "62541,62543", "62542", "62646", "62667,62647,62663"])
        # SW-FIX1 kept: leg 5's south-shore packs come right after g1 (old g9-g13 first); leg 4 group 3 radius 4.
        self.assertEqual([(r["entries"], r["radius"]) for r in leg_steps(rows, 5) if r["kind"] == "pull"][:7],
                         [("62544,62540,62545", "13.4"), ("62545,62544,62540", "10.1"),
                          ("62546,62545,62544", "8"), ("62542,62545,62544", "8"),
                          ("62544,62546,62545", "10.5"), ("62546", "8.4"), ("62546", "5")])
        self.assertEqual([(r["entries"], r["radius"]) for r in leg_steps(rows, 4) if r["kind"] == "pull"][3:5],
                         [("62537,62539", "4"), ("62539,62537,62536", "5")])
        self.assertIn("62670", [r["entries"] for r in leg_steps(rows, 11) if r["kind"] == "pull"])
        # SW-FIX2: leg 5 groups 10-11 carry the ledge approach rows (the export writes them as move rows right
        # before their pull); the stands are pinned so a re-export that moves one is caught.
        steps5 = leg_steps(rows, 5)
        pos = {id(r): i for i, r in enumerate(steps5)}
        approach = {}
        for r in steps5:
            if r["kind"] == "move" and r["comment"].endswith(" approach"):
                approach.setdefault(r["comment"], []).append(r)
        self.assertEqual(sorted(approach), ["pull group 10 approach", "pull group 11 approach",
                                            "pull group 12 approach", "pull group 13 approach",
                                            "pull group 14 approach", "pull group 4 approach"])
        order = sorted(approach, key=lambda c: int(c.split()[2]))
        self.assertEqual([len(approach[c]) for c in order], [2, 3, 4, 1, 1, 4])
        # SW-FIX7's group-4 approach is the dry-shore via, not a stand: its last point is the old step 249, 116 yd
        # from group 5's pack, and the exporter cuts the hop from it. The SW-FIX2/FIX4/FIX6 stands are groups 10-14.
        order = [c for c in order if c != "pull group 4 approach"]
        stands = {c: (float(approach[c][-1]["x"]), float(approach[c][-1]["y"]), float(approach[c][-1]["z"])) for c in order}
        self.assertEqual(stands, {
            "pull group 10 approach": (-6325.5, -3920.0, 222.5),
            "pull group 11 approach": (-6337.0, -3949.5, 218.29),
            "pull group 12 approach": (-6354.0, -3958.0, 218.29),
            "pull group 13 approach": (-6366.0, -3948.5, 218.24),
            "pull group 14 approach": (-6324.5, -3882.0, 222.689),
        })
        pulls5 = [r for r in steps5 if r["kind"] == "pull"]
        for c in order:
            n = int(c.split()[2])
            pull = pulls5[n]
            self.assertEqual(steps5[pos[id(pull)] - 1], approach[c][-1], c)
            # groups 10-13 stand 23.2-24.7 yd from their pack (the pull ring 15-25); group 14's is 49.7 (SW-FIX4).
            self.assertLessEqual(math.dist(stands[c], (float(pull["x"]), float(pull["y"]), float(pull["z"]))),
                                 30.0 if n < 14 else 50.0, c)
            lo = pos[id(pulls5[n - 1])] if n else -1
            hi = pos[id(pull)]
            self.assertTrue(all(lo < i < hi for i in (pos[id(r)] for r in approach[c])), c)
        # SW-FIX7: the dry-shore via after the group-3 pull (step 248). r6 slot6 paused 3 times at step 249 with the
        # lead in the lake at -6271.7 -4055.6 216.5: the first move after the pull was -6265.6 -4056 218.189, and
        # the lead's post-pull stand -6278.9 -4055.4 218.0 is EAST of it, so the walk went west along y -4055.4,
        # where the client floor is 215.8-217.3 (WMO 215.97-217.19, water 217.81) under a navmesh shelf at 218.7.
        # Group 4 now gets -6276 -4059 218.189 (the dry shelf, WMO 218.14) then the old point; the first move after
        # the pull is the dry via, and the exporter's cut rows follow from -6265.6.
        a4 = approach["pull group 4 approach"]
        self.assertEqual([(r["kind"], r["x"], r["y"], r["z"]) for r in a4],
                         [("move", "-6276", "-4059", "218.189"),
                          ("move", "-6265.6", "-4056", "218.189")])
        p3 = pulls5[3]
        self.assertEqual(steps5[pos[id(p3)] + 1], a4[0])
        self.assertEqual(steps5[pos[id(p3)] + 2], a4[1])
        self.assertEqual((steps5[pos[id(p3)] + 3]["x"], steps5[pos[id(p3)] + 3]["y"],
                          steps5[pos[id(p3)] + 3]["z"]), ("-6266.6665", "-4055.7332", "218.189"))
        self.assertEqual((steps5[pos[id(p3)] + 4]["x"], steps5[pos[id(p3)] + 4]["y"],
                          steps5[pos[id(p3)] + 4]["z"]), ("-6312.2666", "-4062.6665", "218.189"))
        self.assertEqual(steps5[pos[id(p3)] + 5], pulls5[4])
        # SW-FIX4: group 14's approach keeps to the solid shore strip at y -3950 and the ramp (2-6 yd south of the
        # 8 yd mesh hole at x -6341..-6333, y -3947), ending on the ledge at the point the r37 hand move used.
        a14 = approach["pull group 14 approach"]
        self.assertEqual([(r["kind"], r["x"], r["y"], r["z"]) for r in a14],
                         [("move", "-6344", "-3950", "218.289"),
                          ("move", "-6331", "-3950", "218.34"),
                          ("move", "-6328", "-3928", "221.717"),
                          ("move", "-6324.5", "-3882", "222.689")])
        self.assertEqual(steps5[pos[id(pulls5[14])] - 1], a14[-1])
        self.assertLessEqual(math.dist(stands["pull group 14 approach"],
                                       (float(pulls5[14]["x"]), float(pulls5[14]["y"]), float(pulls5[14]["z"]))), 50.0)
        self.assertFalse([r for r in rows if -6341 < float(r["x"]) < -6333 and abs(float(r["y"]) + 3947.2) < 2.0],
                         "a step stands in the lake mesh hole (r37's cut point -6333.07 -3947.2)")
        # SW-FIX6: the outbound crossing. Group 11's approach walks from the ramp down to the shelf at x -6329.5
        # (3.5 yd east of the hole's edge -6333) and then west along y -3949.5; the old row -6340 -3950 218.6, which
        # crossed the hole and dropped the lead to 215.8 in r37 and r41, is gone. No row stands over the hole's
        # footprint at the shelf's z (the hole: x -6341..-6333, y -3945..-3947, Floors empty at 210-230).
        self.assertFalse([r for r in steps5 if (round(float(r["x"]), 3), round(float(r["y"]), 3)) == (-6340.0, -3950.0)],
                         "the group-11 approach still crosses the mesh hole at -6340 -3950")
        self.assertFalse([r for r in rows if -6341 < float(r["x"]) < -6333 and -3947.5 < float(r["y"]) < -3944.5
                          and float(r["z"]) < 219.0],
                         "a step stands over the lake mesh hole below its shelf")
        self.assertEqual([(r["x"], r["y"], r["z"]) for r in approach["pull group 11 approach"]],
                         [("-6328.5", "-3935", "220.5"), ("-6330", "-3942", "219.9"),
                          ("-6329.5", "-3948.5", "218.4"), ("-6337", "-3949.5", "218.29")])
        # SW-FIX6: the pack of the deferred step 273 (now step 275) gets stands on its own shelf.
        for c, d in (("pull group 12 approach", 24.7), ("pull group 13 approach", 23.4)):
            n = int(c.split()[2])
            pack = (float(pulls5[n]["x"]), float(pulls5[n]["y"]), float(pulls5[n]["z"]))
            self.assertLessEqual(abs(math.dist(stands[c], pack) - d), 0.2, c)
        # SW-FIX3: the seam hop. Leg 7 (mycellakos_west) ends with its group-9 pull on the z215 island; leg 8
        # starts with the exact move to the island edge (flags 64), its move_hop_exact 6.3 yd east (the seam),
        # and the move to the east shelf, then the step-553 pack and its neighbour. The exact/hop pair is the
        # shape the exporter and the run leader require (DungeonRunPolicy.h ExactWalkTo), so it is pinned here.
        steps7, steps8 = leg_steps(rows, 7), leg_steps(rows, 8)
        self.assertEqual((steps7[-1]["kind"], steps7[-1]["entries"]), ("pull", "62646"))
        self.assertEqual((float(steps7[-1]["x"]), float(steps7[-1]["y"]), float(steps7[-1]["z"])),
                         (-6054.58, -3784.35, 213.87))
        self.assertEqual([(r["kind"], r["x"], r["y"], r["z"], r["flags"]) for r in steps8[:3]],
                         [("move", "-6043.7", "-3790.6", "215.4", "64"),
                          ("move_hop_exact", "-6038", "-3788", "216.9", "0"),
                          ("move", "-6036", "-3792", "217.1", "0")])
        self.assertEqual([(r["kind"], r["x"], r["y"], r["z"], r["entries"]) for r in steps8[3:5]],
                         [("pull", "-6012.28", "-3784.58", "218.45", "62645,62650"),
                          ("pull", "-5990.5", "-3783.86", "219.37", "62650,62645")])
        # SW-FIX5: leg 4 group 7's approach ends on the floor of groups 8-11 and they are pulled from it with no move row
        # between (r39s2: the party stood on the 222.3-223.3 shelf and each pullback refused). The stand is the r37
        # slot6 rest spot (-6057.9 -4136.5 217.5), 26.7/41.0/22.4/50.9 yd from groups 8/9/10/11.
        steps4 = leg_steps(rows, 4)
        pulls4 = [r for r in steps4 if r["kind"] == "pull"]
        i7 = steps4.index(pulls4[7])
        self.assertEqual([(r["kind"], r["x"], r["y"], r["z"], r["comment"]) for r in steps4[i7 - 3:i7]],
                         [("move", "-6056.2666", "-4184.8", "222.789", "pull group 7 approach"),
                          ("move", "-6055.7334", "-4179.4663", "220.189", "pull group 7 approach"),
                          ("move", "-6057.9", "-4136.5", "217.5", "pull group 7 approach")])
        self.assertEqual([r["kind"] for r in steps4[i7:i7 + 5]],
                         ["pull"] * 5, "groups 7-11 pulled from the approach's stand with no move between")
        stand4 = (float(steps4[i7 - 1]["x"]), float(steps4[i7 - 1]["y"]), float(steps4[i7 - 1]["z"]))
        self.assertEqual(stand4, (-6057.9, -4136.5, 217.5))
        for n in range(8, 12):
            p = pulls4[n]
            self.assertLessEqual(math.dist(stand4, (float(p["x"]), float(p["y"]), float(p["z"]))), 60.0, n)
        # SW-FIX5: leg 1 group 2 (step 31's pack, 62540 at -5765.28 -4278.17 235.33) walks the corridor route to the pit
        # rim, 32.4 yd from the pack, instead of the run leader's 8 hops (r39s2 stopped 104 yd out on an off-mesh rock).
        steps1 = leg_steps(rows, 1)
        approach1 = [r for r in steps1 if r["comment"] == "pull group 2 approach"]
        self.assertEqual([(r["kind"], r["x"], r["y"], r["z"]) for r in approach1],
                         [("move", "-5869.87", "-4303.2", "234.99"),
                          ("move", "-5874.93", "-4283.47", "236.39"),
                          ("move", "-5873.07", "-4268.8", "239.19"),
                          ("move", "-5795.73", "-4268.27", "239.66")])
        pull2 = [r for r in steps1 if r["kind"] == "pull"][2]
        self.assertEqual(steps1[steps1.index(pull2) - 1], approach1[-1], "the rim is the last move before the pull")
        self.assertLessEqual(math.dist((float(approach1[-1]["x"]), float(approach1[-1]["y"]), float(approach1[-1]["z"])),
                                       (float(pull2["x"]), float(pull2["y"]), float(pull2["z"]))), 60.0)
        self.assertLessEqual(math.dist((float(approach1[-1]["x"]), float(approach1[-1]["y"]), float(approach1[-1]["z"])),
                                       (float(pull2["x"]), float(pull2["y"]), float(pull2["z"]))), 32.5)
        # the route's corners the exporter cut into move rows: all on the corridor floor (z 234.96-239.69), none on the
        # 239.7 rock the r39 lead was left on (no navmesh floor there).
        between = steps1[steps1.index([r for r in steps1 if r["kind"] == "pull"][1]) + 1:steps1.index(pull2)]
        self.assertTrue(between[0]["comment"] == "path")
        self.assertLessEqual(max(float(r["z"]) for r in between), 241.3)
        self.assertFalse([r for r in between if -5870 < float(r["x"]) < -5866 and abs(float(r["y"]) + 4267.7) < 2.5])
        # SW-ELDERMAW: the leg-9 boss row is a stand pull from the west lip L = -5842.0 -3807.0 200.7 (specs/sw-eldermaw.md
        # Spec B). Its point is his search centre P = wp3, its radius 95 (wp7, his farthest waypoint, is 90.1 yd out; the
        # loader caps a row's radius at 100), and its flags 12288: 4096 kPlanFlagSkipUnreachable | 8192 kPlanFlagStandPull.
        # The row right before it in the same leg is the move to L, with no approach-path row between the two (the
        # exporter writes none before a stand-pull boss), and L is 24.1 yd from P (inside the run leader's 60 yd see).
        steps9, steps10 = leg_steps(rows, 9), leg_steps(rows, 10)
        boss9 = steps9[-1]
        self.assertEqual((boss9["kind"], boss9["entry"], boss9["x"], boss9["y"], boss9["z"], boss9["radius"], boss9["flags"]),
                         ("boss", "62661", "-5820", "-3797.19", "201.241", "95", "12288"))
        self.assertEqual(int(boss9["flags"]) & 4096, 4096, "skip_unreachable (4096) on the boss row")
        self.assertEqual(int(boss9["flags"]) & 8192, 8192, "stand_pull (8192) on the boss row")
        self.assertLessEqual(int(float(boss9["radius"])), 100)
        self.assertEqual(boss9["comment"],
                         "boss; out of reach: swims a pool (WMO water 202.2, bed 191-196); bed navmesh cut off from the "
                         "shore; specs/sw-eldermaw.md")
        stand9 = steps9[-2]
        self.assertEqual((stand9["kind"], stand9["x"], stand9["y"], stand9["z"], stand9["comment"]),
                         ("move", "-5842", "-3807", "200.7", "boss approach"))
        last_pull9 = max(i for i, r in enumerate(steps9) if r["kind"] == "pull")
        self.assertFalse([r for r in steps9[last_pull9 + 1:] if r["comment"] == "approach path"],
                         "a stand-pull boss gets no approach-path row between its stand and its row")
        stand = (float(stand9["x"]), float(stand9["y"]), float(stand9["z"]))
        boss = (float(boss9["x"]), float(boss9["y"]), float(boss9["z"]))
        self.assertAlmostEqual(math.dist(stand, boss), 24.1, delta=0.1)
        self.assertEqual(len(steps9), 59)
        # The 9 cut rows from group 9's pull down to the lip end on L.
        self.assertEqual([(r["kind"], r["comment"]) for r in steps9[last_pull9 + 1:last_pull9 + 10]],
                         [("move", "path")] * 9)
        self.assertEqual(steps9[last_pull9 + 10:], [stand9, boss9])
        # The next move (leg 10's `to`) is cut from L, not from P: its 30 path rows start 5.7 yd from the stand, and
        # the `to` follows them. Measured from P the first hop would have been a partial (0x4) from the pool.
        self.assertEqual(len(steps10), 78)
        self.assertEqual([r["comment"] for r in steps10[:30]], ["path"] * 30)
        self.assertEqual(steps10[30]["comment"], "to")
        first10 = steps10[0]
        self.assertLessEqual(math.dist(stand, (float(first10["x"]), float(first10["y"]), float(first10["z"]))), 6.0)
        self.assertGreater(math.dist(boss, (float(first10["x"]), float(first10["y"]), float(first10["z"]))), 20.0)
        self.assertEqual(re.findall(r"(?m)^DELETE FROM `ai_dungeon_step` WHERE `plan` = '(\w+)';", text), ["stormwrought_ruins"])
        self.assertEqual(re.findall(r"(?m)^DELETE FROM `ai_dungeon_leg` WHERE `plan` = '(\w+)';", text), ["stormwrought_ruins"])
        self.assertEqual(re.findall(r"(?m)^DELETE FROM `ai_dungeon_plan` WHERE `plan` = '(\w+)';", text), ["stormwrought_ruins"])
        self.assertIn("DELETE FROM `ai_dungeon_boss` WHERE `map` IN (818) AND `entry` IN (62547, 62548, 62552, 62549, 62551, 62550, 62652, 62664, 62661, 62665, 62671);", text)
        self.assertIn("(818, 62671, 'Ighal''for');", text)

    def test_karazhan_crypt_is_the_plan_with_the_shaft_dive(self):
        # 20261028220000 (turtle-agent prep/kc-corpsemuncher fee876a, 2026-10-28) replaces 20261025002600: same plan, with
        # Corpsemuncher 91917 as leg 6 (specs/kc-corpsemuncher.md option A, the shaft climb). From Hivaxxis the party walks to
        # the shaft floor F1 (-11211 -1522 -45.9), swims one row 42.6 yd up to S1 (-11211 -1516 -3.0), one more open-water row
        # to S2 (-11205 -1514.5 -0.5) and walks the north ramp's first dry polygon D1 (-11211 -1492 3.6) into his dry wing; the
        # T0 probe of 2026-10-28 passed F1->S1 and S1->D1. The walk
        # reaches F1 through the corridor point (-11211 -1689 -28.5) leg 4 already walks (kc-cm-fix1: the exporter's cut at
        # -11166.4 -1686.7 -30.2 stood 1.7 yd under the corridor floor and the run refused it twice live). Legs 0-5 are
        # row for row the old file (the Hivaxxis leg reordered with a stand for every pack, the lower-crypt pools on dry stands,
        # see test_karazhan_crypt_hivaxxis_leg_walks_the_room_east_to_west and
        # test_karazhan_crypt_lower_crypt_pools_have_dry_stands). The reorder itself came from r40 (RUN-LESSONS
        # "r40 karazhan_crypt slot5", the first full run: 0 deaths, the dive worked for all 5 in 12 s, the Drowned Sinners and the
        # Spectre pulls worked, Archlich Enkhraz, Marrowspike and Hivaxxis down, STOP at step 113). The old leg 3 sent the party
        # from the lower crypt back up to the z 4.3 walkway; the only way up is the dive link in reverse, from the water, and the
        # lead bot never made it (it stood on the shaft floor and made no way toward the next point), so the plan does every upper
        # floor first, the dive next and the lower crypt last, and the run ends down there: 7 legs, 316 steps (154 pulls,
        # 156 moves, 6 bosses), 324 rows. The critters 14881, the never-waking Remains 91924 and the ledge Crypt Fearfeaster
        # 91922 stay out as in 20261025001100.
        name, text = newest_plan_text("karazhan_crypt")
        self.assertEqual(name, "20261028220000_world.sql")
        self.assertIn("('karazhan_crypt', 800, '', '', '91918,91913,91911,91925', 'turtle-agent fee876a dungeons/karazhan_crypt.json', 'plan v1, draft (reordered 2026-10-25: upper floors, shaft dive, lower crypt last; Corpsemuncher leg 6 by the shaft climb (swim hand-off), probe T0 2026-10-28; not live-checked; hivaxxis reordered after r40 slot5b; lower-crypt pools dry 2026-10-26)');", text)
        self.assertEqual(leg_rows(text, "karazhan_crypt"),
                         {0: ("archlich_enkhraz", ""), 1: ("guard_captain_gort", ""), 2: ("commander_andreon", ""),
                          3: ("marrowspike_dive", ""), 4: ("marrowspike", ""), 5: ("hivaxxis", ""),
                          6: ("corpsemuncher", "")})
        rows = [r for r in self.step_rows(text) if r["plan"] == "karazhan_crypt"]
        self.assertEqual(len(rows), 316)
        self.assertEqual({leg: len(leg_steps(rows, leg)) for leg in range(7)},
                         {0: 51, 1: 55, 2: 78, 3: 27, 4: 41, 5: 14, 6: 50})
        self.assertEqual([len([r for r in leg_steps(rows, leg) if r["kind"] == "pull"]) for leg in range(7)],
                         [23, 41, 43, 5, 25, 4, 13])
        self.assertEqual([len([r for r in leg_steps(rows, leg) if r["kind"] == "move"]) for leg in range(7)],
                         [27, 13, 34, 22, 15, 9, 36])
        self.assertEqual([r["entry"] for r in rows if r["kind"] == "boss"], ["91916", "92935", "91919", "91920", "91929", "91917"])
        self.assertEqual({r["kind"] for r in rows}, {"move", "pull", "boss"})
        for leg in (0, 1, 2, 4, 5, 6):
            self.assertEqual(leg_steps(rows, leg)[-1]["kind"], "boss", leg)
        self.assertEqual(leg_steps(rows, 3)[-1]["kind"], "pull", "the dive leg ends on the slope pack; it has no boss")
        self.assertEqual([r["entries"] for r in rows if r["kind"] == "boss"], ["91927", "", "", "", "", ""])
        pulls = [r for r in rows if r["kind"] == "pull"]
        entries = [e for r in pulls for e in r["entries"].split(",")]
        self.assertNotIn("14881", entries)      # the 19 critters of the spider room
        self.assertNotIn("91927", entries)      # Enkhraz's cat is his boss.first
        self.assertNotIn("91931", entries)      # the GM-only Alarus trigger
        self.assertNotIn("91922", entries)      # the unmeshed ledge's Fearfeaster
        self.assertNotIn("91917", entries, "Corpsemuncher is the leg's boss, not a pull")
        boss6 = [r for r in rows if r["entry"] == "91917"]
        self.assertEqual([(r["leg"], r["kind"]) for r in boss6], [("6", "boss")])
        # Leg 6's climb: the walk ends on the shaft floor F1, one swim row up to open water S1, then the `to` D1 on the
        # north ramp's first dry polygon (RUN-LESSONS "kc corpsemuncher probe", 2026-10-28: Q1 F1->S1 and Q2 S1->D1 PASS).
        # The swim is a single row: under the exporter's 60 yd move-gap cut, so the run leader sends it as one `cmd move`.
        leg6moves = [r for r in leg_steps(rows, 6) if r["kind"] == "move"]
        f1 = [r for r in leg6moves
              if math.dist([float(r[c]) for c in ("x", "y", "z")], [-11211.0, -1522.0, -45.9]) < 0.3]
        s1 = [r for r in leg6moves
              if math.dist([float(r[c]) for c in ("x", "y", "z")], [-11211.0, -1516.0, -3.0]) < 0.3]
        self.assertEqual(len(f1), 1, "one move row on the shaft floor F1")
        self.assertEqual(len(s1), 1, "one swim row up to open water S1")
        self.assertEqual([f1[0]["comment"], s1[0]["comment"]], ["via", "via"])
        self.assertEqual(int(s1[0]["seq"]) - int(f1[0]["seq"]), 1, "the swim leaves from F1")
        d1to = [r for r in leg6moves if r["comment"] == "to" and
                math.dist([float(r[c]) for c in ("x", "y", "z")], [-11211.0, -1492.0, 3.6]) < 0.3]
        self.assertEqual(len(d1to), 1, "the `to` D1 on the north ramp's first dry polygon")
        # kc-cm-fix2 (RUN-LESSONS "combat live 1236b" run A, step 292): a Drowned Sinner engaged on the arrival at S1, its
        # fight ran under the D1 row and left the lead bot 34.2 yd from D1 in open water, where the core has no polygon in its
        # 5 yd start search and answers the dry D1 with its mid-water shortcut 0x11, which the swim hand-off refuses for a dry
        # end: "the lead bot is stuck". S2 is a second open-water row right after S1, so a fight on the arrival at S1 runs under
        # a water row (the hand-off swims water to water from anywhere in the open shaft); S2 lies in the band north of
        # y -1518.5 where the core paths to D1 (0x1), and more than the run leader's stalled-arrival 6 yd from S1, so the
        # arrival at S1 never counts as the arrival at S2.
        s2 = [r for r in leg6moves
              if math.dist([float(r[c]) for c in ("x", "y", "z")], [-11205.0, -1514.5, -0.5]) < 0.3]
        self.assertEqual(len(s2), 1, "one open-water row S2 after S1")
        self.assertEqual(s2[0]["comment"], "via")
        self.assertEqual(int(s2[0]["seq"]) - int(s1[0]["seq"]), 1, "S2 comes right after the swim to S1")
        self.assertEqual(int(d1to[0]["seq"]) - int(s2[0]["seq"]), 1, "D1 is sent from S2")
        self.assertGreater(float(s2[0]["y"]), -1518.5, "S2 is in the band where the core paths to D1")
        self.assertGreater(math.dist([float(s1[0][c]) for c in ("x", "y", "z")],
                                     [float(s2[0][c]) for c in ("x", "y", "z")]), 6.0,
                           "S2 is not reached by standing on S1 (kStalledArriveYd)")
        self.assertGreater(float(s2[0]["z"]), -4.2, "S2 is above the ramp's end, not under it")
        self.assertLess(float(s2[0]["z"]), 3.0, "S2 is under the water surface (the hand-off's both-ends-in-water)")
        self.assertLess(math.dist([float(f1[0][c]) for c in ("x", "y", "z")],
                                  [float(s1[0][c]) for c in ("x", "y", "z")]), 60.0, "one cmd move row")
        # kc-cm-fix1 (RUN-LESSONS "bosses 1236 live"): the exporter's old cut row at -11166.4 -1686.7 -30.2 stood
        # 1.7 yd under the corridor floor and the live run refused it twice. The walk now goes through the corridor
        # point (-11211 -1689 -28.5) leg 4 already walks (leg 6's first via), and every row of that walk is on the
        # -28.47 floor.
        self.assertFalse([r for r in leg6moves
                          if abs(float(r["x"]) + 11166.399) < 0.5 and abs(float(r["y"]) + 1686.6667) < 0.5],
                         "the refused cut point must never come back")
        via0 = [r for r in leg6moves if r["comment"] == "via" and
                math.dist([float(r[c]) for c in ("x", "y", "z")], [-11211.0, -1689.0, -28.5]) < 0.3]
        self.assertEqual(len(via0), 1, "leg 6's first via is leg 4's corridor point")
        self.assertLess(int(via0[0]["seq"]), int(f1[0]["seq"]), "the corridor via comes before F1")
        corridor = [r for r in leg6moves
                    if int(r["seq"]) >= 7 and int(r["seq"]) <= int(via0[0]["seq"])]
        self.assertEqual([int(r["seq"]) for r in corridor], [7, 8, 9, 10, 11, 12, 13, 14, 15])
        for r in corridor:
            self.assertAlmostEqual(float(r["z"]), -28.47348, delta=0.6,
                                   msg="the corridor walk stays on the -28.47 floor")
        # the south wing (x -11270..-11170, y -1485..-1395) holds leg 6's rows only
        wing = [r for r in rows if -11270 <= float(r["x"]) <= -11170 and -1485 <= float(r["y"]) <= -1395]
        self.assertTrue(wing, "Corpsemuncher's wing has rows now")
        self.assertEqual({r["leg"] for r in wing}, {"6"})
        self.assertEqual(len([r for r in pulls if "91930" in r["entries"].split(",")]), 3)   # the 7 Tomb Creepers, three pulls
        # 4 Drowned Sinners at the shaft floor (leg 3), the one on the south ramp (leg 1, z -1.5) and the ramp-foot one of
        # leg 6 (z -1.46, pulled from D1)
        niners = [r for r in pulls if r["entries"] == "91932"]
        self.assertEqual(len(niners), 6)
        self.assertEqual([r["leg"] for r in niners].count("3"), 4)
        self.assertEqual([r["leg"] for r in niners].count("1"), 1)
        self.assertEqual([r["leg"] for r in niners].count("6"), 1)
        self.assertLessEqual(max(float(r["radius"]) for r in pulls), 20.0)   # leg 6's wing packs are the widest (16.5..20)
        self.assertGreaterEqual(min(float(r["radius"]) for r in pulls), 5.0)
        self.assertEqual(re.findall(r"(?m)^DELETE FROM `ai_dungeon_step` WHERE `plan` = '(\w+)';", text), ["karazhan_crypt"])
        # The map 800 ai_dungeon_boss seed stays in 20261024021100 (the six bosses are unchanged): this file must not seed it again.
        self.assertNotRegex(text, r"(?m)^(?:INSERT|REPLACE) INTO `ai_dungeon_boss`")
        seeds = [(n, t) for n, t in migrations()
                 if "DELETE FROM `ai_dungeon_boss` WHERE `map` IN (800)" in t]
        self.assertEqual([n for n, _t in seeds], ["20261024021100_world.sql"])
        self.assertIn("DELETE FROM `ai_dungeon_boss` WHERE `map` IN (800) AND `entry` IN (91916, 91917, 91920, 91929, 91919, 92935);",
                      seeds[0][1])

    def test_karazhan_crypt_dives_once_and_never_climbs_back(self):
        # The dive route and the floor order (KARAZHAN-CRYPT-NAVMESH.md, probe tests/r39kc/; the r40 run in RUN-LESSONS). Leg 3
        # (marrowspike_dive) leaves the walkway for a DRY point on the south ramp above the waterline (surface 3.046), one move row
        # through the off-mesh dive lands on the shaft floor (-11221.3 -1560 -45.9); the dive is 56.8 yd straight, inside the run
        # leader's 60 yd sight distance, and it must start dry (water-to-water is refused, BFD C2/C3). The four Drowned Sinners 91932
        # that swim at the floor are pulled from that floor stand before anything else - they killed a bot parked there in 32 s - and
        # the pack of two Risen Crypt Guards and the Shadowblade Spectre 91918 is pulled from a stand on the lower-crypt slope BEFORE
        # the walk past it. Legs 0-2 are the upper floors, legs 4-5 the lower crypt, and the link is crossed once, downwards.
        _name, text = newest_plan_text("karazhan_crypt")
        rows = [r for r in self.step_rows(text) if r["plan"] == "karazhan_crypt"]
        leg3 = leg_steps(rows, 3)
        to_at = [i for i, r in enumerate(leg3) if r["comment"] == "to"]
        self.assertEqual(len(to_at), 1)
        self.assertTrue(all(r["kind"] == "move" for r in leg3[:to_at[0] + 1]))
        dry_row = leg3[to_at[0]]
        dry = [float(dry_row[c]) for c in ("x", "y", "z")]
        self.assertAlmostEqual(dry[0], -11219.96, places=2)
        self.assertAlmostEqual(dry[1], -1586.82, places=2)
        self.assertGreater(dry[2], 4.0, "the dive must start on the dry ramp, above the 3.046 water surface")
        dive = leg3[to_at[0] + 1]
        self.assertEqual(dive["kind"], "move")
        self.assertEqual(dive["comment"], "pull group 0 approach")
        landed = [float(dive[c]) for c in ("x", "y", "z")]
        self.assertEqual([round(v, 1) for v in landed], [-11221.3, -1560.0, -45.9])
        self.assertAlmostEqual(math.dist(dry, landed), 56.8, places=1)
        self.assertLess(math.dist(dry, landed), 60.0, "one cmd move row: a cut would put a move row in mid-water")
        pulls = [r for r in leg3 if r["kind"] == "pull"]
        self.assertEqual([r["entries"] for r in pulls[:4]], ["91932"] * 4)
        # The Sinners of the floor: ambush (stealthed), and on the floor (z -41.2 to -44.7), not in the water column.
        self.assertTrue(all(int(r["flags"]) & 1 for r in pulls[:4]))
        self.assertTrue(all(-45.0 < float(r["z"]) < -41.0 for r in pulls[:4]))
        # The slope pack: the only pull with the Spectre, 9.2 yd from a stand row of its own, the leg's last row.
        spectre = [i for i, r in enumerate(pulls) if "91918" in r["entries"].split(",")]
        self.assertEqual(spectre, [4])
        self.assertEqual(pulls[4]["entries"], "91925,91918")   # two Risen Crypt Guards and the Spectre, one row
        stand = [r for r in leg3 if r["kind"] == "move" and r["comment"] == "pull group 4 approach"]
        self.assertEqual(len(stand), 1)
        self.assertAlmostEqual(math.dist([float(stand[0][c]) for c in ("x", "y", "z")],
                                         [float(pulls[4][c]) for c in ("x", "y", "z")]), 9.2, places=1)
        # r40's refused step 67: the exporter's cut row at -11200.0 -1749.3 -29.4 (unreachable) is replaced by the point the run
        # walked to, -11194.8 -1752.3 -28.3, as a via row of the marrowspike leg; the leg's `to` is r40's next row (step 68).
        leg4 = leg_steps(rows, 4)
        vias = [r for r in leg4 if r["kind"] == "move" and r["comment"] == "via"]
        self.assertEqual([[round(float(r[c]), 3) for c in ("x", "y", "z")] for r in vias],
                         [[-11211.0, -1689.0, -28.5], [-11202.032, -1738.184, -29.205], [-11194.8, -1752.3, -28.3]])
        to4 = [r for r in leg4 if r["comment"] == "to"][0]
        self.assertEqual([round(float(to4[c]), 3) for c in ("x", "y", "z")], [-11199.199, -1770.667, -29.273])
        self.assertFalse([r for r in rows if abs(float(r["x"]) + 11200.0) < 0.3 and abs(float(r["y"]) + 1749.3) < 0.3
                          and abs(float(r["z"]) + 29.4) < 0.3],
                         "the refused cut point must be gone")
        # The walk's hops are r40's own moves, all under the run leader's 60 yd.
        walk = [leg3[-1]] + vias + [to4]
        for a, b in zip(walk, walk[1:]):
            self.assertLess(math.dist([float(a[c]) for c in ("x", "y", "z")],
                                      [float(b[c]) for c in ("x", "y", "z")]), 60.0)
        # commander_andreon's `to` is on the upper hall floor, 21.1 yd from its first pull (it was the terrace z 5.87, 23.7 yd
        # straight below the pack but 297.6 yd away on the navmesh: the run leader would have pulled through the ceiling).
        leg2 = leg_steps(rows, 2)
        to2 = [r for r in leg2 if r["comment"] == "to"][0]
        self.assertAlmostEqual(float(to2["x"]), -11096.95, places=2)
        self.assertAlmostEqual(float(to2["y"]), -1642.28, places=2)
        self.assertAlmostEqual(float(to2["z"]), 28.8001, places=3)
        self.assertAlmostEqual(math.dist([float(to2["x"]), float(to2["y"]), float(to2["z"])],
                                         [float(leg2[17]["x"]), float(leg2[17]["y"]), float(leg2[17]["z"])]), 21.1, places=1)
        # The order by floor: legs 0-2 hold no point below z -1.5 (the lowest is the south-ramp Sinner of leg 1), legs 4-5 stand
        # in the lower crypt only, and no move row starts below the waterline and ends on the walkway (the climb r40 could not make).
        # Leg 5's stands are points of the spider room's own floor, z -14.78..-15.17 (the creature spawns stand at -15.4..-15.7):
        # -14.7 keeps it off the walkway (z 4.3) and off the off-mesh z -13.5 ledge the run leader searched in r40 slot5b.
        for leg in (0, 1, 2):
            self.assertTrue(all(float(r["z"]) >= -1.5 for r in leg_steps(rows, leg)), leg)
        for leg in (4, 5):
            self.assertTrue(all(float(r["z"]) <= -14.7 for r in leg_steps(rows, leg)), leg)
        lead = None
        for r in rows:
            if r["leg"] == "6":
                continue   # leg 6 climbs the shaft by the one swim row (the Corpsemuncher pins)
            if r["kind"] == "move" and lead is not None and lead < 0.0 and float(r["z"]) > 3.0:
                self.fail("a move row climbs back up: leg %s seq %s to %s %s %s" % (r["leg"], r["seq"], r["x"], r["y"], r["z"]))
            lead = float(r["z"])

    def test_karazhan_crypt_hivaxxis_leg_walks_the_room_east_to_west(self):
        # r40 slot5b (RUN-LESSONS) ran the reordered plan to the end with 0 deaths, 0 wipes and Enkhraz, Gort, Andreon and
        # Marrowspike down, then stopped in the Hivaxxis leg and never fought him: step 256, the Unseen Stalker 91923 at
        # -11173.3 -1741.94, was deferred ("not reachable from this floor", nearest 72.6 yd after 8 hops); step 257
        # (-11222.385 -1751.4604 -15.287915) found "no dry stand-in within 25 yd"; step 258 (-11230.666 -1753.0667
        # -15.273479) was "refused: unreachable". The lead sat at -11237.9 -1746.7 -13.5, 17 yd from Hivaxxis.
        # The two moves were the offline exporter's 50 yd cut of the hop from the stalker to the boss: its straight path
        # sags to z -15.27/-15.29, 0.4-0.5 yd under the room's floor (-14.78..-15.17 on the server's own path), and the
        # server answers a path to them 0x8 from the west. The leg also pulled the Tomb Creeper pair first, which walks
        # the party in at the room's EAST end and drags it west into the pair; the stand the leader then searched for its
        # next pull was the off-mesh pocket at -11245.7 -1747.3 -15.0, whose only walkable neighbour is the z -13.5 ledge.
        # The leg now walks the room the way the party enters it, east to west, with the stand given for every pack and the
        # boss, so the leader never searches one: 14 rows, the last boss 91929. Every move row is on the server's floor.
        _name, text = newest_plan_text("karazhan_crypt")
        rows = [r for r in self.step_rows(text) if r["plan"] == "karazhan_crypt"]
        leg5 = leg_steps(rows, 5)
        got = [(int(r["seq"]), r["kind"], (round(float(r["x"]), 2), round(float(r["y"]), 2), round(float(r["z"]), 2)),
                r["entries"], r["comment"]) for r in leg5]
        self.assertEqual(got, [
            (0, "move", (-11249.0, -1769.26, -28.47), "", "to"),
            (1, "move", (-11233.0, -1755.0, -28.4), "", "pull group 0 approach"),
            (2, "move", (-11233.0, -1706.0, -28.3), "", "pull group 0 approach"),
            (3, "move", (-11191.09, -1668.78, -27.97), "", "pull group 0 approach"),
            (4, "move", (-11164.57, -1698.66, -27.97), "", "pull group 0 approach"),
            (5, "pull", (-11173.3, -1741.94, -15.44), "91923", "pull group 0"),
            (6, "move", (-11196.86, -1746.51, -15.08), "", "pull group 1 approach"),
            (7, "pull", (-11206.6, -1743.9, -15.57), "91930", "pull group 1"),
            (8, "move", (-11212.57, -1749.56, -14.91), "", "pull group 2 approach"),
            (9, "pull", (-11229.1, -1746.86, -15.39), "91930", "pull group 2"),
            (10, "move", (-11230.24, -1752.98, -14.78), "", "pull group 3 approach"),
            (11, "pull", (-11242.65, -1758.72, -15.7), "91930", "pull group 3"),
            (12, "move", (-11243.99, -1751.02, -15.17), "", "boss approach"),
            (13, "boss", (-11255.0, -1744.53, -15.69), "", "boss"),
        ])
        # The walk order: every pull stands east of the next one, and the boss is west of all four (the room is entered in
        # the east). The stalker sits behind the Tomb of the Unrepentant door (-11177.8 -1742), so it comes first.
        pulls = [r for r in leg5 if r["kind"] == "pull"]
        self.assertEqual([r["entries"] for r in pulls], ["91923", "91930", "91930", "91930"])
        self.assertEqual([float(r["x"]) for r in pulls], sorted([float(r["x"]) for r in pulls], reverse=True))
        self.assertLess(float(leg5[-1]["x"]), min(float(r["x"]) for r in pulls))
        self.assertEqual(leg5[-1]["kind"], "boss")
        self.assertEqual(leg5[-1]["entry"], "91929")
        # Every pull and the boss has a stand row right before it, and the pack is 10.1-17.0 yd from it (the run leader's
        # own stand search, 15-25 yd, is what walked into the pocket; a given stand is walked to as it is).
        for i, r in enumerate(leg5):
            if r["kind"] in ("pull", "boss"):
                self.assertEqual(leg5[i - 1]["kind"], "move", (r["leg"], r["seq"]))
                self.assertTrue(leg5[i - 1]["comment"].endswith(" approach"), leg5[i - 1]["comment"])
                stand = [float(leg5[i - 1][c]) for c in ("x", "y", "z")]
                pack = [float(r[c]) for c in ("x", "y", "z")]
                self.assertLess(math.dist(stand, pack), 50.0)
                if r["kind"] == "pull":
                    self.assertGreaterEqual(math.dist(stand, pack), 10.0, r["comment"])
        # The two cut points that failed live are gone (the exporter adds no point of its own in this leg), and no leg-5
        # move sits in the 0.2 yd sag band under the room's floor (-14.78..-15.17) that made them unreachable.
        for x, y, z in ((-11222.385, -1751.4604, -15.287915), (-11230.666, -1753.0667, -15.273479)):
            self.assertFalse([r for r in rows if abs(float(r["x"]) - x) < 0.2 and abs(float(r["y"]) - y) < 0.2],
                             "the refused cut point %s %s is still a row" % (x, y))
        moves = [r for r in leg5 if r["kind"] == "move"]
        self.assertEqual([r["comment"] for r in moves].count("approach path"), 0)
        self.assertEqual([r["comment"] for r in leg5 if r["comment"].startswith("path")], [])
        self.assertFalse([r for r in moves if -15.35 < float(r["z"]) < -15.2], "a move row under the room's floor")
        # The boss stand is on the room floor, 12.8 yd straight from Hivaxxis (pathdump: type 0x1, 12.9 yd of path).
        stand = [float(leg5[-2][c]) for c in ("x", "y", "z")]
        self.assertEqual([round(v, 2) for v in stand], [-11243.99, -1751.02, -15.17])
        self.assertAlmostEqual(math.dist(stand, [float(leg5[-1][c]) for c in ("x", "y", "z")]), 12.8, places=1)


    def test_karazhan_crypt_lower_crypt_pools_have_dry_stands(self):
        # proof20d2 (RUN-LESSONS "proof20d2 karazhan_crypt") paused twice on `cmd move ... refused: unreachable` after the
        # dive: step 248 (leg marrowspike) to -11138.4 -1733.6 -31.4 and step 254 (leg hivaxxis) to -11215.8 -1743.7 -31.4,
        # the second with the lead already on the point. Both are water polygons of the lower crypt (server-exact tile
        # 8005334 / 6a8168b1: floors with flag 0x9 water, z -29.6 and -29.74; the lead sank to -31.4). 20261025002600
        # (turtle-agent c49eb6c) replaces them: pull group 23 walks around the first pool's west and south rim, the
        # Hivaxxis leg's first hop around the basin's west and north rim. Every row below is on a dry 0x1 floor; the
        # strict row replay from the run's real positions is 0 refused. The old water rows must never come back.
        _name, text = newest_plan_text("karazhan_crypt")
        rows = [r for r in self.step_rows(text) if r["plan"] == "karazhan_crypt"]
        leg4 = leg_steps(rows, 4)
        approach = [(int(r["seq"]), tuple(round(float(r[c]), 2) for c in ("x", "y", "z")))
                    for r in leg4 if r["comment"] == "pull group 23 approach"]
        self.assertEqual(approach, [(34, (-11152.0, -1710.0, -28.0)),
                                    (35, (-11152.0, -1758.0, -29.0)),
                                    (36, (-11154.0, -1790.0, -28.5)),
                                    (37, (-11150.0, -1800.0, -28.4))])
        pull23 = [r for r in leg4 if r["kind"] == "pull" and r["comment"] == "pull group 23"][0]
        stand = [float(leg4[37][c]) for c in ("x", "y", "z")]
        pack = [float(pull23[c]) for c in ("x", "y", "z")]
        self.assertGreaterEqual(math.dist(stand, pack), 10.0, "the pool stand is a pull stand, 10-17 yd out")
        self.assertLess(math.dist(stand, pack), 17.0, "the pool stand is a pull stand, 10-17 yd out")
        leg5 = leg_steps(rows, 5)
        first = [(int(r["seq"]), tuple(round(float(r[c]), 2) for c in ("x", "y", "z")))
                 for r in leg5[:3]]
        self.assertEqual(first, [(0, (-11249.0, -1769.26, -28.47)),
                                 (1, (-11233.0, -1755.0, -28.4)),
                                 (2, (-11233.0, -1706.0, -28.3))])
        # Neither of the refused water points is a row any more (the exporter's cut at the first pool, the leg's old
        # first approach in the basin).
        for x, y in ((-11138.4, -1741.73), (-11215.79, -1743.7)):
            self.assertFalse([r for r in rows if abs(float(r["x"]) - x) < 0.5 and abs(float(r["y"]) - y) < 0.5],
                             "the water point %s %s is still a row" % (x, y))

    def test_lower_blackrock_spire_is_the_plan_with_the_census_fixes(self):
        # 20261024153100 (turtle-agent ca779b3, RUN-LESSONS r35 slot5, steps 217/222/223 = leg 1): the r33 draft after the
        # census and the server-path route fixes, no row hand-set, with the boss rows scoped to this plan's seven entries
        # (map 229 is shared with the upper_blackrock_spire raid plan). 7 legs, 491 steps (113 pulls: 22 + 23 + 32 + 5 + 4
        # + 22 + 5; 371 moves; 7 bosses). The draft's 11 UBRS-level packs (z 106-111, x 8-29) are out, the four static rares
        # are pack members, Mor Grayhoof and his trigger are out, no pull lists more than six units and every radius covers
        # its own pack (<= 16 yd). Leg 0 walks from the z 31 pit floor a refused or skipped pull leaves the party on (pull
        # group 14's approach starts at -45.3 -404 31.5, pull group 20's at -35.2 -395.2 31.63). Leg 1's z 24 shelf is
        # reached from the z -7 ledge, not from the z 11 spider floor: the z -7 pack (pull group 11) comes before the shelf
        # pull (pull group 12, the 6-unit 9239/9240 pack at the shelf's east end -81.07 -482.4 24.4), Vosh'gajin's two
        # linked 9240 guards are the boss row's entries (not pulls), and the 10374 patroller (pull group 8) is pulled from
        # the east spur -104.8 -520.4 10.8, 60 yd from Mother Smolderweb (r35's step 217 wipe).
        name, text = newest_plan_text("lower_blackrock_spire")
        self.assertEqual(name, "20261024153100_world.sql")
        self.assertIn("('lower_blackrock_spire', 229, '9240,9257,9261,9262,9269', '', '9217,9045,9198,9239,9717,9266', "
                      "'turtle-agent ca779b3 dungeons/lower_blackrock_spire.json', 'plan v1, draft (not mapped, not live-checked)');", text)
        legs = leg_rows(text, "lower_blackrock_spire")
        self.assertEqual([legs[i][0] for i in range(7)],
                         ["highlord_omokk", "shadow_hunter_vosh'gajin", "war_master_voone", "mother_smolderweb",
                          "quartermaster_zigris", "halycon", "overlord_wyrmthalak"])
        rows = [r for r in self.step_rows(text) if r["plan"] == "lower_blackrock_spire"]
        self.assertEqual(len(rows), 491)
        self.assertEqual([len([r for r in leg_steps(rows, leg) if r["kind"] == "pull"]) for leg in range(7)],
                         [22, 23, 32, 5, 4, 22, 5])
        self.assertEqual([r["entry"] for r in rows if r["kind"] == "boss"],
                         ["9196", "9236", "9237", "10596", "9736", "10220", "9568"])
        self.assertEqual({r["kind"] for r in rows}, {"move", "pull", "boss"})
        for leg in range(7):
            self.assertEqual(leg_steps(rows, leg)[-1]["kind"], "boss", leg)
        pulls = [r for r in rows if r["kind"] == "pull"]
        self.assertLessEqual(max(len([e for e in r["entries"].split(",") if e]) for r in pulls), 6)
        self.assertLessEqual(max(float(r["radius"]) for r in pulls), 16.0)
        self.assertLessEqual(max(float(r["z"]) for r in rows if r["kind"] == "pull"), 91.0)
        for rare in ("9217", "9218", "9718", "10376"):
            self.assertEqual(len([r for r in pulls if rare in r["entries"].split(",")]), 1, rare)
        for out in ("16080", "16044"):
            self.assertFalse([r for r in pulls if out in r["entries"].split(",")], out)
        # 20261024153100: leg 1's fixes. The two linked 9240 guards are the boss row's entries, not pulls (they stand 9-10 yd
        # from Vosh'gajin and cannot be pulled without him); the 6-unit pack keeps its pull from the shelf's east end, and the
        # 10374 patroller pulls from the east spur. Every approach chain ends within 50 yd of its pull and its hops are <= 60 yd
        # (the exporter's cut threshold), so a re-export that drops a chain is caught here.
        steps1 = leg_steps(rows, 1)
        pos = {id(r): i for i, r in enumerate(steps1)}
        boss1 = [r for r in steps1 if r["kind"] == "boss"][0]
        self.assertEqual(boss1["entry"], "9236")
        self.assertEqual(boss1["entries"], "9240")
        self.assertFalse([r for r in pulls if r["leg"] == "1" and r["entries"] == "9240"])
        z7 = [r for r in steps1 if r["kind"] == "pull" and r["comment"] == "pull group 11"][0]
        six = [r for r in steps1 if r["kind"] == "pull" and r["comment"] == "pull group 12"][0]
        self.assertLess(pos[id(z7)], pos[id(six)])
        self.assertAlmostEqual(math.dist((-75.69, -517.37, -7.08), tuple(float(z7[k]) for k in "xyz")), 0.0, delta=0.2)
        self.assertAlmostEqual(math.dist((-99.1, -481.68, 24.43), tuple(float(six[k]) for k in "xyz")), 0.0, delta=1)
        six_approach = [r for r in steps1 if r["kind"] == "move" and r["comment"] == "pull group 12 approach"]
        self.assertEqual([(float(r["x"]), float(r["y"]), float(r["z"])) for r in six_approach],
                         [(-45.8, -512.9, 5.0), (-45.6, -494.13, 17.53), (-46.13, -491.73, 16.23), (-81.07, -482.4, 24.4)])
        self.assertLess(pos[id(six_approach[-1])], pos[id(six)])
        self.assertAlmostEqual(math.dist((-81.07, -482.4, 24.4), tuple(float(six[k]) for k in "xyz")), 17.8, delta=0.5)
        patrol = [r for r in steps1 if r["kind"] == "pull" and r["comment"] == "pull group 8"][0]
        patrol_approach = [r for r in steps1 if r["kind"] == "move" and r["comment"] == "pull group 8 approach"]
        self.assertEqual([(float(r["x"]), float(r["y"]), float(r["z"])) for r in patrol_approach],
                         [(-141.87, -510.13, -18.37), (-116.53, -535.73, -2.77), (-122.4, -560.0, 9.73), (-104.8, -520.4, 10.8)])
        self.assertLess(pos[id(patrol_approach[-1])], pos[id(patrol)])
        self.assertAlmostEqual(math.dist((-104.8, -520.4, 10.8), tuple(float(patrol[k]) for k in "xyz")), 22.5, delta=0.5)
        z29 = [r for r in steps1 if r["kind"] == "pull" and r["comment"] == "pull group 13"][0]
        z29_approach = [r for r in steps1 if r["kind"] == "move" and r["comment"] == "pull group 13 approach"]
        self.assertEqual([(float(r["x"]), float(r["y"]), float(r["z"])) for r in z29_approach],
                         [(-46.4, -479.2, 16.5), (-3.7, -492.3, 30.0), (-21.3, -500.3, 29.7), (-42.7, -512.0, 29.7)])
        self.assertLess(pos[id(z29_approach[-1])], pos[id(z29)])
        group7 = [r for r in steps1 if r["kind"] == "pull" and r["comment"] == "pull group 7"][0]
        for anchor, pull, approach in ((group7, patrol, patrol_approach), (z7, six, six_approach),
                                       (six, z29, z29_approach)):
            prev = tuple(float(anchor[k]) for k in "xyz")
            first = (float(approach[0]["x"]), float(approach[0]["y"]), float(approach[0]["z"]))
            self.assertLessEqual(math.dist(prev, first), 60)
            for a, b in zip(approach, approach[1:]):
                self.assertLessEqual(math.dist((float(a["x"]), float(a["y"]), float(a["z"])),
                                               (float(b["x"]), float(b["y"]), float(b["z"]))), 60)
            last = (float(approach[-1]["x"]), float(approach[-1]["y"]), float(approach[-1]["z"]))
            self.assertLessEqual(math.dist(last, (float(pull["x"]), float(pull["y"]), float(pull["z"]))), 50)
        self.assertEqual(re.findall(r"(?m)^DELETE FROM `ai_dungeon_step` WHERE `plan` = '(\w+)';", text), ["lower_blackrock_spire"])
        self.assertIn("DELETE FROM `ai_dungeon_boss` WHERE `map` IN (229) AND `entry` IN (9196, 9236, 9237, 10596, 9736, 10220, 9568);", text)

    def test_lower_blackrock_spire_rows_pass_the_loader_checks(self):
        # Every step row goes through the loader's own code (DungeonPlanPolicy.h), compiled from tools/test_dungeon_plan_rows.cpp:
        # a row the loader refuses would be skipped at startup and the run pauses before it.
        compiler = shutil.which("g++")
        if compiler is None:
            self.skipTest("g++ is not installed")
        _name, text = newest_plan_text("lower_blackrock_spire")
        rows = [r for r in self.step_rows(text) if r["plan"] == "lower_blackrock_spire"]
        lines = ["P\tlower_blackrock_spire\t%d" % BOSS_PLANS["lower_blackrock_spire"]]
        for r in rows:
            lines.append("\t".join(["S", r["plan"], r["leg"], r["seq"], r["kind"], r["entry"], r["x"], r["y"], r["z"], r["radius"], r["item"],
                                    r["param"], r["flags"], r["timeout_s"], r["entries"]]))
        with tempfile.TemporaryDirectory() as tmp:
            binary = os.path.join(tmp, "rows")
            build = subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", os.path.join(ROOT, "tools/test_dungeon_plan_rows.cpp"), "-o", binary],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([binary], input="\n".join(lines) + "\n", capture_output=True, text=True)
        self.assertEqual(run.stdout.strip().splitlines()[-1], "rows %d refused 0" % len(rows), run.stdout)
        self.assertEqual(run.returncode, 0, run.stdout)

    def test_crescent_grove_is_the_census_fixed_draft(self):
        # 20261023210000 (turtle-agent cg-fix1 479dc0d) re-exports the 20261023130100 draft (prep/crescent_grove ba00e7c, the census
        # fixes of PREP-CRESCENT-GROVE-2026-10-23.md) with pull group 49's approach on dry ground: the exporter's cut from group 48
        # went through a lake (water-only polygons) and the lead bot cannot make its own move to a water target, so run 1 (r25 slot 6)
        # stalled at step 367. 5 legs, 559 steps (59 + 101 + 53 + 59 + 27 pulls; leg 2 loses two moves), bosses in leg order, no door or
        # event step. The plan row has the unit lists; the 14 Deer 883 (critters) are out of every group, the Elders 92105/92106 are
        # Grovetender Engryss' first (they stood 11 yd from him), and the two pulls at the patrolling Roaming Felguard 2566320 are one
        # group of four. No pull row stands within 25 yd of a boss.
        name, text = newest_plan_text("crescent_grove")
        self.assertEqual(name, "20261023210000_world.sql")
        self.assertIn("('crescent_grove', 802, '92101', '92101', '92124,92123,92119,92125,92126,92114,92121', 'turtle-agent 479dc0d dungeons/crescent_grove.json', 'plan v1, draft (not mapped, not live-checked)');", text)
        self.assertEqual(leg_rows(text, "crescent_grove"),
                         {0: ("grovetender_engryss", ""), 1: ("keeper_ranathos", ""), 2: ("high_priestess_a'lathea", ""),
                          3: ("fenektis_the_deceiver", ""), 4: ("master_raxxieth", "")})
        rows = [r for r in self.step_rows(text) if r["plan"] == "crescent_grove"]
        self.assertEqual(len(rows), 559)
        self.assertEqual({leg: len(leg_steps(rows, leg)) for leg in range(5)}, {0: 103, 1: 190, 2: 94, 3: 110, 4: 62})
        # The walk to pull group 49: three approach moves on the dry shore (the exporter would otherwise cut the hop through the
        # lake), each hop of at most 50 yd (the cut threshold), then the pull row.
        leg2 = leg_steps(rows, 2)
        self.assertEqual([(r["kind"], r["x"], r["y"], r["z"], r["comment"]) for r in leg2[73:77]],
                         [("move", "-240", "-75", "275.39", "pull group 49 approach"),
                          ("move", "-284", "-95", "273.79", "pull group 49 approach"),
                          ("move", "-312", "-135", "280.11", "pull group 49 approach"),
                          ("pull", "-335.39", "-176.91", "292.37", "pull group 49")])
        hops = [leg2[72]] + leg2[73:77]
        for a, b in zip(hops, hops[1:]):
            self.assertLessEqual(math.dist([float(a["x"]), float(a["y"]), float(a["z"])],
                                           [float(b["x"]), float(b["y"]), float(b["z"])]), 50.0, (a["seq"], b["seq"]))
        pulls = [r for r in rows if r["kind"] == "pull"]
        self.assertEqual([len([r for r in leg_steps(rows, leg) if r["kind"] == "pull"]) for leg in range(5)], [59, 101, 53, 59, 27])
        self.assertEqual([r["entry"] for r in rows if r["kind"] == "boss"], ["92107", "92109", "92108", "92111", "92110"])
        self.assertEqual([r["entries"] for r in rows if r["kind"] == "boss"], ["92105,92106", "", "", "", ""])
        for leg in range(5):
            self.assertEqual(leg_steps(rows, leg)[-1]["kind"], "boss", leg)
        # The critter and the pack sizes: no 883 anywhere, the merged patrol pack as one row, and the radii cut to the own members.
        self.assertFalse([r for r in pulls if "883" in r["entries"].split(",")])
        self.assertEqual(max(len(r["entries"].split(",")) for r in pulls), 3)
        radii = {(r["leg"], r["x"], r["y"]): r["radius"] for r in pulls}
        for leg, x, y, want in [("0", "506.37", "-98.07", "10"), ("0", "511", "-124.74", "10"), ("0", "528.34", "-125", "10"),
                                ("0", "308.02", "-34.71", "10"), ("0", "220.36", "-49.59", "10"),
                                ("1", "296.43", "59.62", "13"), ("1", "312.96", "257.41", "10"), ("1", "199.99", "202.65", "16"),
                                ("3", "-501.08", "-64.54", "14")]:
            self.assertEqual(radii.get((leg, x, y)), want, "%s %s %s" % (leg, x, y))
        self.assertIn(("2", "92120,92122", "10", "-405.38", "-186.68"),
                      [(r["leg"], r["entries"], r["radius"], r["x"], r["y"]) for r in pulls])
        bosses = [(261.576, -148.21), (122.596, 67.9047), (-308.922, -307.698), (-510.094, 62.3177), (-659.29, -390.521)]
        for r in pulls:
            for x, y in bosses:
                self.assertGreaterEqual(dist(r, x, y), 25.0, r["comment"])
        self.assertIn("DELETE FROM `ai_dungeon_boss` WHERE `map` IN (802) AND `entry` IN (92107, 92109, 92108, 92111, 92110);", text)

    def test_maraudon_orange_is_the_census_fixed_wing(self):
        # 20261024010100 (turtle-agent d59b259, 2026-10-24): the orange wing's draft with the census fixes of
        # PREP-MARAUDON-ORANGE-2026-10-24.md, no row hand-set, with the boss rows of map 349 scoped to its own six entries.
        # 6 legs, 995 steps (28 + 10 + 45 + 22 + 7 + 8 pulls), bosses in leg order, no boss.first, no door or event step.
        # The plan row carries cc_priority and kill_first; the 50 critter spawns (2914/13321/6145/4076) are out of every pull,
        # every radius is the farthest member plus its wander (<= 19.5 yd, no draft's farthest + 10), and the two pulls that
        # stood nearer another leg's packs are in leg 2 now.
        name, text = newest_plan_text("maraudon_orange")
        self.assertEqual(name, "20261024010100_world.sql")
        self.assertIn("('maraudon_orange', 349, '11793,11794', '', '12217,12219,12222', 'turtle-agent d59b259 dungeons/maraudon_orange.json', 'plan v1, draft (not mapped, not live-checked)');", text)
        self.assertEqual(leg_rows(text, "maraudon_orange"),
                         {0: ("noxxion", ""), 1: ("razorlash", ""), 2: ("landslide", ""), 3: ("tinkerer_gizlock", ""),
                          4: ("rotgrip", ""), 5: ("princess_theradras", "")})
        rows = [r for r in self.step_rows(text) if r["plan"] == "maraudon_orange"]
        self.assertEqual(len(rows), 995)
        self.assertEqual({leg: len(leg_steps(rows, leg)) for leg in range(6)}, {0: 36, 1: 28, 2: 427, 3: 215, 4: 176, 5: 113})
        pulls = [r for r in rows if r["kind"] == "pull"]
        self.assertEqual([len([r for r in leg_steps(rows, leg) if r["kind"] == "pull"]) for leg in range(6)],
                         [28, 10, 45, 22, 7, 8])
        self.assertEqual([r["entry"] for r in rows if r["kind"] == "boss"],
                         ["13282", "12258", "12203", "13601", "13596", "12201"])
        self.assertEqual([r["entries"] for r in rows if r["kind"] == "boss"], ["", "", "", "", "", ""])
        self.assertEqual({r["kind"] for r in rows}, {"move", "pull", "boss"})
        for leg in range(6):
            self.assertEqual(leg_steps(rows, leg)[-1]["kind"], "boss", leg)
        # The critters are gone: not a pull lists 2914, 13321, 6145 or 4076.
        self.assertFalse([r for r in pulls if set(r["entries"].split(",")) & {"2914", "13321", "6145", "4076"}])
        # The radii are the pack's own spread: the three 8-Vile-Larva pulls (one linked creature_groups pack each) are the widest,
        # and the pull that took a Spewed Larva with its eight larvae is one group.
        self.assertLessEqual(max(float(r["radius"]) for r in pulls), 19.5)
        self.assertEqual([(r["entries"], r["radius"]) for r in pulls if float(r["radius"]) >= 17.0],
                         [("12218", "19.5"), ("12218", "18.1"), ("12218", "19")])
        self.assertEqual([(r["entries"], r["radius"]) for r in pulls if "13533" in r["entries"].split(",")],
                         [("12218,13533", "16.6")])
        # No pull row stands within the aggro range of a boss (its own detection is 18-20 yd; the nearest is 33.6 yd).
        bosses = [(1130.4, -191.342), (978.887, -10.152), (356.681, -185.455), (134.902, -313.731),
                  (42.0826, -65.9511), (27.8981, 83.1932)]
        for r in pulls:
            for x, y in bosses:
                self.assertGreaterEqual(dist(r, x, y), 30.0, r["comment"])
        # Leg 5's two stray groups (309.6 97.9 -131.8 and 351.2 205.0 -102.3) are leg 2's pulls now.
        self.assertIn(("12207", "2", "5", "309.63", "97.9"),
                      [(r["entries"], r["leg"], r["radius"], r["x"], r["y"]) for r in pulls])
        self.assertIn("DELETE FROM `ai_dungeon_boss` WHERE `map` IN (349) AND `entry` IN (13282, 12258, 12203, 13601, 13596, 12201);", text)

    def test_blackrock_depths_is_the_plan_with_the_angerforge_hall_first(self):
        # 20261025002200 (turtle-agent prep/brd-92-floor ff95dac, 2026-10-25, after the refast run stopped at step 92
        # with 4 pauses and steps 816/818 were still open from r39): leg 1's g9 approach walks down the ring and the ramp
        # from the z-43 ledge the g7/g8 fight leaves the party on (the 432.2 yd route to the old first point 522 -241 -65
        # is 256 smooth points, MAX_POINT_PATH_LENGTH, so the server answers NOPATH from the ledge at 536.7 -246.6 -42.5;
        # from r39's end 549.9 -274.8 -43.1 the same route is 94 points and NORMAL), and leg 6's g12/g13 carry the ridge
        # approach points on their floor (the cutter's 50 yd hops stood 6.33 and 5.26 yd under it and had no poly).
        # The rest is 20261025000500's: leg 5's gallery pulls are gone and the Incendius boss has the only served descent;
        # leg 3 clears the z-45 hall (8911/8898) and Angerforge before g3, g3 opens leg 4 from the boss's hall, g45
        # descends to its own z-72 floor, and the Angerforge approach stands 25.0 yd from the boss (the four linked 8901
        # guards cannot be split: creature_linking flag 1031).
        # No row hand-set, with the boss rows of map 230. 10 legs, 1164 steps (30 + 60 + 6 + 11 + 30 + 23 + 14 + 8
        # + 67 + 8 pulls), bosses in leg order. The lava points are out of the pulls, leg 7's start is on
        # Flamelash's platform, the packs that stand at five bosses are those boss rows' entries, the four rares
        # are pulls of their own, and no pull reaches beyond its own spread.
        name, text = newest_plan_text("blackrock_depths")
        self.assertEqual(name, "20261025002200_world.sql")
        self.assertIn("('blackrock_depths', 230, '8903,8892,8890,8891', '8903', '8894,8898,8896,8904,8900', 'turtle-agent ff95dac dungeons/blackrock_depths.json', 'plan v1, draft (not mapped, not live-checked)');", text)
        self.assertEqual(len(leg_rows(text, "blackrock_depths")), 10)
        rows = [r for r in self.step_rows(text) if r["plan"] == "blackrock_depths"]
        self.assertEqual(len(rows), 1164)
        pulls = [r for r in rows if r["kind"] == "pull"]
        self.assertEqual([len([r for r in leg_steps(rows, leg) if r["kind"] == "pull"]) for leg in range(10)],
                         [30, 60, 6, 11, 30, 23, 14, 8, 67, 8])
        self.assertEqual([r["entry"] for r in rows if r["kind"] == "boss"],
                         ["9018", "9319", "9016", "9033", "8983", "9017", "9056", "9156", "9938", "9019"])
        self.assertEqual([r["entries"] for r in rows if r["kind"] == "boss"],
                         ["8912,8890,8921", "8921", "", "8901", "8906,8907", "", "8897,8905", "", "", ""])
        for leg in range(10):
            self.assertEqual(leg_steps(rows, leg)[-1]["kind"], "boss", leg)
        # The step-395 split: leg 3 pull group 7 is the lone 8911 (radius 2) with its own stand, pull group 8 the
        # two 8898 (radius 2) with theirs; leg 1 runs g21 before g20/g19 now. The angerforge hall is before g3 now.
        leg3 = leg_steps(rows, 3)
        g8a = [r for r in leg3 if r["comment"] == "pull group 7"]
        g8b = [r for r in leg3 if r["comment"] == "pull group 8"]
        self.assertEqual(len(g8a), 1)
        self.assertEqual(len(g8b), 1)
        self.assertEqual(g8a[0]["entries"], "8911")
        self.assertEqual(g8a[0]["radius"], "2")
        self.assertEqual(g8b[0]["entries"], "8898")
        self.assertEqual(g8b[0]["radius"], "2")
        stands8 = [r for r in leg3 if r["comment"] in ("pull group 7 approach", "pull group 8 approach")]
        self.assertEqual([(r["x"], r["y"], r["z"]) for r in stands8],
                         [("719", "40", "-45"), ("719", "35", "-45")])
        # The step-377 fix: the z-45 hall is pulled before g3 and g3 opens leg 4 from Angerforge's hall. The boss
        # approach stands 25.0 yd from 652.4 21.4 -60.0 on the z-60 floor, outside his 20 yd aggro with the party's
        # 3 yd spread (the four linked 8901 guards cannot be split: creature_linking flag 1031).
        self.assertFalse([r for r in leg3 if r["entries"] == "8898,8899"])
        self.assertEqual([(r["x"], r["y"], r["z"]) for r in leg3 if r["comment"] == "boss approach"],
                         [("668.4", "40.6", "-59.4")])
        leg4 = leg_steps(rows, 4)
        self.assertEqual([r["entries"] for r in leg4 if r["comment"] == "pull group 0"], ["8898,8899"])
        self.assertEqual([(r["x"], r["y"], r["z"]) for r in leg4 if r["comment"] == "pull group 0 approach"],
                         [("682", "12", "-59.5")])
        # Steps 93 and 240: the g9 approach starts on the party's real z-65 stand (522 -241 -65, 13.4 yd from
        # 535 -244 -68) and the g45 descent walks the only served route down to its own z-72 floor
        # (565 -53 -60, 583 17 -69, 676 -94 -71.5); every hop was path-checked from the lead's real position
        # (523.6 -237.8 -64.4 and 624.5 -11.8 -72.7). Warder Stilgiss 16079 (no fight in the run) is gone.
        leg1 = leg_steps(rows, 1)
        # The refast step-92 stop: the g7/g8 fight leaves the party on the z-43 ledge, where 522 -241 -65 is a 432.2 yd
        # route (84 polys, 256 smooth points -> the server's MAX_POINT_PATH_LENGTH -> NOPATH) and 535 -244 -68 the same.
        # The approach anchors on the ramp top and then the pull stand: 270.9 yd / 73 points from the refast ledge
        # (536.7 -246.6 -42.5) and 246.3 yd / 63 points from r39's end (549.9 -274.8 -43.1), then 165.6 yd / 41 points down.
        g9 = [r for r in leg1 if r["comment"] == "pull group 9 approach"]
        self.assertEqual([(r["x"], r["y"], r["z"]) for r in g9],
                         [("585.6", "-103.4668", "-45.4225"), ("535", "-244", "-68")])
        self.assertNotIn("522", [r["x"] for r in g9])
        self.assertEqual([(r["x"], r["y"], r["z"]) for r in leg1 if r["comment"] == "pull group 45 approach"],
                         [("565", "-53", "-60"), ("583", "17", "-69"), ("676", "-94", "-71.5")])
        self.assertEqual([(r["x"], r["y"], r["z"]) for r in leg1 if r["comment"] == "pull group 37 approach"],
                         [("589.9", "-117.9", "-39.7"), ("584.2", "-124.2", "-49.7"),
                          ("583.6", "-143.7", "-53.2"), ("582.8", "-164.4", "-83.9")])
        self.assertNotIn("16079", [r["entries"] for r in pulls])
        leg1pulls = [r["entries"] for r in leg_steps(rows, 1) if r["kind"] == "pull"]
        self.assertEqual(leg1pulls[19:22], ["8892", "8892,8891", "8892,8891"])
        # The 19 Fireguard Destroyer points on the lava and in the two lava pockets are gone (the navmesh flags them SLIME, and no run
        # leader route reaches them); the two Fireguard Destroyer pulls left in leg 6 stand on the main network at z -103.4 and -102.0.
        lava = [(849.381, -243.453), (851.683, -281.675), (886.74, -327.905), (932.497, -250.361),
                (926.193, -352.366), (941.022, -310.532), (1022.1, -303.398), (1118.19, -358.505),
                (983.66, -278.923), (1067.93, -264.695), (1139.25, -241.24),
                (1012.2, -239.4), (1009.7, -235.9), (1027.7, -246.0),
                (1154.0, -313.6), (1161.9, -315.6), (1150.9, -323.9), (1165.1, -327.2)]
        for r in pulls:
            self.assertGreater(min(dist(r, x, y) for x, y in lava), 1.0, (r["leg"], r["seq"], r["x"], r["y"]))
            self.assertGreater(float(r["z"]), -110.0, (r["leg"], r["seq"]))
        self.assertLessEqual(max(float(r["radius"]) for r in pulls), 18.0)
        # The rares are pull rows now, not the draft's skip list (which is gone): Loregrain 9024, Stilgiss 9041 + Verek 9042 together,
        # Panzor 8923 and Roccor 9025.
        rarity = [r["entries"] for r in pulls if set(r["entries"].split(",")) & {"9024", "9041", "9042", "8923", "9025"}]
        self.assertIn("9024", rarity)
        self.assertIn("9042,9041", rarity)
        self.assertIn("8923", rarity)
        self.assertIn("9025", rarity)
        # The r38 stall: the gallery above the Incendius pit (leg 5 groups 23-25, 18 spawns at 898-940 -227..-292
        # z -49.85) is a walkable island no route of the leg reaches (server path-check: 1030-1310 yd and 16-47 yd short
        # from every floor the leg holds), so the three pulls are gone and the leg ends with g22, then the boss descent
        # the plan could not walk before: the boss stands 22.0 yd below the ring and the direct hop refuses (1215.2 yd,
        # 33.0 yd short); the approach walks the arena's south-east door, every hop server path-checked from the stand
        # the party really holds (884 -301 -48.8), and stops 25.6 yd from the boss on the arena floor.
        leg5 = leg_steps(rows, 5)
        self.assertFalse([r for r in leg5 if 890 <= float(r["x"]) <= 950 and -300 <= float(r["y"]) <= -220
                          and abs(float(r["z"]) + 49.85) < 4])
        self.assertEqual([r["comment"] for r in leg5[-2:]], ["boss approach", "boss"])
        self.assertEqual([(r["x"], r["y"], r["z"]) for r in leg5 if r["comment"] == "boss approach"],
                         [("900", "-325", "-49.9"), ("911.7", "-383.2", "-49.8"),
                          ("976.3", "-370.4", "-64.7"), ("905", "-290", "-71.8")])
        # No other pull changes floor without a move row to walk it: between two adjacent pulls the only jump over 10 yd
        # in legs 5 and 6 is leg 5's group 6 -> group 7 (+21.8 yd from the arena floor up to the ring), which r38 walked
        # on the 399 yd ring loop and cleared from 853 -252 -49.5 (steps 646-654); leg 6's biggest adjacent-pull rise is
        # 6.48 yd (group 0 to group 1) and the 54.0 yd drop to its lava-lake groups is 108 move rows.
        def floor_jumps(leg):
            steps = leg_steps(rows, leg)
            return [round(abs(float(a["z"]) - float(b["z"])), 2)
                    for a, b in zip(steps, steps[1:]) if a["kind"] == "pull" and b["kind"] == "pull"
                    and abs(float(a["z"]) - float(b["z"])) > 10]
        self.assertEqual(floor_jumps(5), [21.81])
        self.assertEqual(floor_jumps(6), [])
        self.assertEqual(re.findall(r"(?m)^DELETE FROM `ai_dungeon_step` WHERE `plan` = '(\w+)';", text), ["blackrock_depths"])
        self.assertIn("DELETE FROM `ai_dungeon_boss` WHERE `map` IN (230) AND `entry` IN (9018, 9319, 9016, 9033, 8983, 9017, 9056, 9156, 9938, 9019);", text)
        # The steps 816/818 fix: the cutter's 50 yd hops (1180.5664 -397.7208 -97.98146 and 1171.39 -410.61612 -98.72122)
        # stood 6.33 and 5.26 yd under the ridge floor and had no poly within the finder's reach; the two groups carry the
        # ridge points on their floor instead, and the old rows are gone.
        leg6 = leg_steps(rows, 6)
        self.assertEqual([(r["x"], r["y"], r["z"]) for r in leg6 if r["comment"] == "pull group 12 approach"],
                         [("1180.5664", "-397.7208", "-91.6458")])
        self.assertEqual([(r["x"], r["y"], r["z"]) for r in leg6 if r["comment"] == "pull group 13 approach"],
                         [("1171.39", "-410.6161", "-93.4582")])
        self.assertFalse([r for r in rows if r["x"] == "1180.5664" and r["z"] == "-97.98146"])
        self.assertFalse([r for r in rows if r["x"] == "1171.39" and r["z"] == "-98.72122"])
        # The g12/g13 chain walks at most 50.6 yd between its rows (the ridge point to g12's pack centre).
        g12 = next(i for i, r in enumerate(leg6) if r["comment"] == "pull group 12 approach")
        self.assertEqual(leg6[g12 + 1]["x"], "1173.3334")
        self.assertEqual(leg6[g12 + 1]["z"], "-92.52249")

    def test_dragonmaw_retreat_is_the_plan_with_the_census_fixes(self):
        # 20261025001900 (turtle-agent 1116b84, 2026-10-25, after live run r41s2 slotA stopped at step 755 with 4 pauses): the
        # pull of 62073 at -6100.5 -3971.4 (leg zuluhed_floor group 10) had no approach, so the run leader walked the lead from
        # the group-9 pull (-6103 -4004.5) over the thin floor at -6122.6..-6129 -4000.5..-3988.7, where the core's
        # GetWaterOrGroundLevel answers the lava level 72.25 - 2 = 70.25; the lead sank into the pocket under the sheet (z 70.3,
        # dry but 3 yd under the floor) and its own stay walk failed ('stay walk resting after 3 failed walks'). Group 10 now
        # has three approach rows -6130 -4005, -6135 -3990 and the stand -6119 -3966 72.6: the stand is 19.2 yd from the pull
        # (inside the 24.5 yd shot rule), class S, the clamp keeps 72.6, it sees the pull, and every hop is a complete server
        # path with no sinking point (sinkgate.py/standpath.py over specs/dmr-step453-tools). The same scan found one sinking
        # point at -6275.9 -3967.4 on the group-16 -> group-17 walk (62035 at -6290.9 -4016.7); group 17's two approach rows
        # -6284 -3968, -6290 -3996 72.6 (20.7 yd, in sight) route round it. The 5 exporter cut rows those replace were on the
        # sinking line. sinkgate.py over all 10 legs: 0 rows sink below their floor.
        # 20261025001800 (turtle-agent 5779343, 2026-10-25, after r39s2/r41s2 lost 34 bots in the halgan hall with no mob in
        # combat, 33 of them on a thin floor over WMO magma): 20261025001600 again, with every stand of the hall on a solid column.
        # The core's GetWaterOrGroundLevel answers the lava level 72.25 - 2 = 70.25 for a point on the 72.6 floor (its liquid
        # lookup casts from ground_z and so finds the lava bed below the plane), and DispatchMovement keeps the 2.35 yd clamp, so
        # the plan's own stand -6328 -3861.5 sank the lead into the magma (rowcheck.py: class D, clamp 70.25, LAVA - the only such
        # row of the plan). (6) Group 16's last approach is -6322 -3880 72.6: on solid floor, the nearest non-solid cell 9.6 yd
        # off, 16.9 yd (at most ~22 with the wander) from the bridge 62035 and with sight, inside the 24.5 yd shot rule, and the
        # -6341.7 -3892.5 hop to it is a complete server path with no non-solid sample. (7) Group 17 got an approach round the
        # open pool: the turn -6326 -3912, then the stand -6281 -3915.5 72.6 (17.0 and 21.2 yd from its two members, sight to
        # both, nothing non-solid within 10 yd). (8) The leg-8 pack 62035/62036/62046 at -6260.6 -3936 (r41s2 fought it on
        # halgan_approach's own walk at steps 456-458) moved into leg 4 right after group 17 with the stand -6264 -3914 72.6
        # (15.2-22.2 yd off, sight to all three); leg 8 keeps its other 22 packs.
        # 20261025001600 (turtle-agent 529ab6d, 2026-10-25, after live run r39s2 slotA2 stopped at step 614): 20261025001000 again,
        # with (4) the ramp up to the z 290-303 ledge as the approach rows of the two ledge pulls, and (5) the hall_clear landing as
        # a plain direct step (no gm_cross, so plan-export takes the plan without --gm-cross-direct).
        # 20261025001000 (turtle-agent be41b1c, 2026-10-25, after live run r39s2 slotA stopped at step 453): 20261025000700 again,
        # with the corridor camp pulled BEFORE the walk crosses it and the halgan rest stand off the magma sheet.
        # (1) The corridor camp the leg-2 walk crossed (r27 slot5 step 388: 21 units in one fight, 2 wipes; r39s2 steps 226-243: 8
        # deaths, three tries on step 228 and four more at 236) is leg 2 now, hall_clear: its `to` is the corridor bottom
        # (-6314.6665 -4074.6665 215.089), the 12 waves climb the corridor in walk order (reversed from the descent, each group's
        # approach list reversed too, 1-6 units a wave), and searistrasz follows with the z 322 hall and the boss. The walk back
        # down the cleared corridor is halgan_approach's own route. (2) r39s2 step 453 (the rest/gather stand -6332.93 -3863.11):
        # no unit attacked the party (the addon view held 0 hostiles, every trace mob had Target=0 and cb=false, and the two
        # suspects - the 62035 at -6360.97 -3838.82 73.17 and the ledge 62036 at -6357.21 -3867.19 106.973 - never engaged it);
        # the hp fell 605-610 a second, the core's lava tick, while the party stood in the magma sheet under the bridge that the
        # navmesh calls ground. The stand moved to -6328.0 -3861.5 73.2 (dry in the trace), reached by a 4-point approach chain
        # along the south shore (-6400 -3880, -6370 -3890, -6341.7 -3892.5); the two packs that stand in the lava are left out; a
        # second avoid box covers the sheet. (3) The four walks that no single hop could carry keep their `via` chains of the
        # server path's own points (halgan_redbrand's climb starts on the east rim at -6387.4668 -3977.3333 72.689), the plan's
        # needs_walk pack (62046 at -6190.56 -3919.91) stays `leave`d, and plan-check is ok on all 591 points.
        # (4) r39s2 slotA2 step 614: the four packs on the z 290-303 ledge above the corridor's south end were deferred ('not
        # reachable from this floor') and the next row's point (-6283.5 -4392.5 302.7) had 'no walkable path' from the lead at
        # -6282.7 -4325.9 266.2 - the plan had no route up. The ramp the party walks down in leg 4 and climbs in leg 3 is now
        # pull group 19's approach (4 server-path points, ending on the shelf stand -6330 -4380 290.4) and the second ramp is
        # group 22's (3 points, ending on the terrace stand -6283.47 -4392.53 302.69); the three z 290 packs pull from the shelf
        # stand 25.8-29.6 yd off, the z 303 golem from the terrace stand 22.4 yd off, and no point of the way up is within 20 yd
        # of a pack not pulled yet (the nearest is 24.6 yd).
        # (5) The hall_clear landing (-6190.1 -3962.4 215.8) is a plain direct step with a check_skip now: 7.4 yd across and
        # 5.1 yd down over a 9.0 yd navmesh gap (ServerPath answers no route across it), inside the direct limits, and the run
        # walked the same move_direct live at step 200 (from -6191.8 -3954.9 221.6 to -6186.7 -3964.3 215.2, no death).
        # 10 legs, 827 steps (28+6+12+60+23+17+25+10+22+3 pulls), bosses in leg order, no door or event step.
        name, text = newest_plan_text("dragonmaw_retreat")
        self.assertEqual(name, "20261025001900_world.sql")
        self.assertIn("('dragonmaw_retreat', 816, '62077,62036,62047,62046', '62077', '62053,62039,62071,62070,8853', 'turtle-agent 1116b84 dungeons/dragonmaw_retreat.json', 'plan v1, draft (not mapped, not live-checked)');", text)
        legs = leg_rows(text, "dragonmaw_retreat")
        self.assertEqual(sorted(legs), list(range(10)))
        self.assertEqual([legs[i][0] for i in range(10)],
                         ["bogpaw_truthsay", "gowlfang", "hall_clear", "searistrasz", "halgan_approach", "halgan_redbrand",
                          "zuluhed_approach", "zuluhed_mid", "zuluhed_floor", "zuluhed_the_whacked"])
        self.assertIn("leave (not read by the run leader yet): 62046 at -6190.56 -3919.91", legs[7][1])
        rows = [r for r in self.step_rows(text) if r["plan"] == "dragonmaw_retreat"]
        self.assertEqual(len(rows), 827)
        self.assertEqual([len([r for r in leg_steps(rows, leg) if r["kind"] == "pull"]) for leg in range(10)],
                         [28, 6, 12, 60, 23, 17, 25, 10, 22, 3])
        self.assertEqual([(int(r["leg"]), int(r["entry"])) for r in rows if r["kind"] == "boss"],
                         [(0, 62056), (1, 62057), (3, 62072), (5, 62069), (9, 62037)])
        # The landing of leg 2 is the one straight walk of the plan (the run leader has no GM move, and no gm_cross is left in
        # the plan: plan-export takes it without --gm-cross-direct), with its own comment;
        # and the server-path `via` of leg 3 (after the corridor camp is clear).
        self.assertEqual([(r["x"], r["y"], r["z"], r["comment"]) for r in rows if r["kind"] == "move_direct"],
                         [("-6190.1", "-3962.4", "215.8", "via")])
        # r39s2 slotA2 step 614: the ramp up to the z 290-303 ledge, as pull group 19's and group 22's approach rows, in walk
        # order, the last point of each on the floor its packs stand on.
        ledge = [(r["kind"], r["x"], r["y"], r["z"], r["comment"]) for r in leg_steps(rows, 6)
                 if r["comment"] in ("pull group 19 approach", "pull group 22 approach")]
        self.assertEqual(ledge, [
            ("move", "-6293.33", "-4349.07", "274.39", "pull group 19 approach"),
            ("move", "-6329.07", "-4349.33", "278.69", "pull group 19 approach"),
            ("move", "-6329.6", "-4369.6", "290.69", "pull group 19 approach"),
            ("move", "-6330", "-4380", "290.4", "pull group 19 approach"),
            ("move", "-6293.33", "-4380", "294.49", "pull group 22 approach"),
            ("move", "-6288.5", "-4386", "297.46", "pull group 22 approach"),
            ("move", "-6283.47", "-4392.53", "302.69", "pull group 22 approach"),
        ])
        # Each of the four ledge packs is pulled from a stand 21-30 yd away on its own floor (+/- 2 yd of z), and the approach
        # row right before it is that stand: the run leader pulls what it sees within 60 yd, so a stand on the ledge is what
        # makes these pulls reachable (on the corridor they were deferred as 'not reachable from this floor').
        import math as _math
        zuluhed = leg_steps(rows, 6)
        for pack, entries in ((("-6300.44", "-4380.81", "290.84"), "62078"), (("-6305.22", "-4372.68", "290.16"), "62039"),
                              (("-6304.99", "-4390.02", "290.16"), "62051"), (("-6266.44", "-4407.07", "303.03"), "62078")):
            i = next(j for j, r in enumerate(zuluhed)
                     if r["kind"] == "pull" and (r["x"], r["y"], r["z"]) == pack and r["entries"] == entries)
            stands = [r for r in zuluhed[:i] if r["comment"].startswith("pull group")
                      and r["comment"].endswith("approach")]
            self.assertTrue(stands, "the pull at %s has no approach row" % (pack,))
            stand = stands[-1]
            d = _math.dist((float(stand["x"]), float(stand["y"]), float(stand["z"])),
                           tuple(float(v) for v in pack))
            self.assertTrue(21.0 <= d <= 30.0, "the stand of the pull at %s is %.1f yd off" % (pack, d))
            self.assertLess(abs(float(stand["z"]) - float(pack[2])), 2.0, "the stand of the pull at %s is not on its floor" % (pack,))
        # The way up keeps 20 yd clear of every pack pulled after the pull it leads to (the pulls run in row order; a pull's
        # point is its pack): the nearest such distance on the ascent rows is 24.6 yd, so climbing aggros nothing new.
        pos = [(j, r) for j, r in enumerate(zuluhed) if r["comment"] in ("pull group 19 approach", "pull group 22 approach")]
        for j, r in pos:
            p = tuple(float(r[k]) for k in ("x", "y", "z"))
            target = next(j2 for j2 in range(j + 1, len(zuluhed)) if zuluhed[j2]["kind"] == "pull")
            later = [tuple(float(q[k]) for k in ("x", "y", "z")) for q in zuluhed[target + 1:] if q["kind"] == "pull"]
            near = min(_math.dist(p, q) for q in later)
            self.assertGreater(near, 20.0, "the ascent row %s passes %.1f yd from a pack still alive" % (p, near))
        leg3 = leg_steps(rows, 3)
        self.assertIn(("move", "-6293.33", "-4373.33", "294.49", "via"),
                      [(r["kind"], r["x"], r["y"], r["z"], r["comment"]) for r in leg3])
        # The hall camp: leg 2, walked up: 12 waves, each a pull with 1-4 approach rows ending in a stand 21-30 yd from the wave;
        # no boss; the first pull is the z 239.7 pack by the corridor's north ramp, the last the z 278 pack at the top.
        hall = leg_steps(rows, 2)
        self.assertEqual([r["kind"] for r in hall if r["kind"] not in ("move", "move_direct")], ["pull"] * 12)
        self.assertFalse([r for r in hall if r["kind"] == "boss"], "hall_clear must not carry a boss")
        waves = [r for r in hall if r["kind"] == "pull"]
        self.assertEqual((waves[0]["x"], waves[0]["y"], waves[0]["z"]), ("-6258.83", "-4069.58", "239.74"))
        self.assertEqual((waves[-1]["x"], waves[-1]["y"], waves[-1]["z"]), ("-6338.42", "-4336.78", "278.16"))
        self.assertEqual([float(w["radius"]) for w in waves][:3], [4.7, 9.7, 3.2])
        self.assertTrue(all(len(r["entries"].split(",")) <= 6 for r in waves), "a wave is over 6 units")
        # One stand per wave, on its north side (the walk's own direction): the party stops before each pack, never past it.
        approach = [r for r in hall if r["comment"].endswith("approach")]
        self.assertEqual(len(approach), 12, "the waves' approach rows")
        self.assertEqual([(r["x"], r["y"], r["z"]) for r in approach][:3],
                         [("-6258.138", "-4093.273", "247.18"), ("-6270.069", "-4096.311", "248.96"),
                          ("-6269.621", "-4114.897", "260.37")])
        self.assertEqual((approach[-1]["x"], approach[-1]["y"], approach[-1]["z"]), ("-6312.194", "-4330.296", "278.76"))
        # Each stand is 21-45 yd from its wave's pull row (the plan check holds the 21-30 yd to the nearest member; a row names
        # one member only). The exact stands are pinned above.
        import math as _math
        for i, a in enumerate(approach):
            p = waves[i]
            d = _math.dist((float(a["x"]), float(a["y"]), float(a["z"])),
                           (float(p["x"]), float(p["y"]), float(p["z"])))
            self.assertTrue(21.0 <= d <= 45.0, "wave %d stand is %.1f yd from the pull row's pack" % (i, d))
        # The walk round the magma pool (r35 slot6b step 475, 11 deaths at the old dip -6404.53 -3950.93 70.94): no row of the
        # plan stands in the avoid box, and halgan_redbrand's climb starts on the east rim at the fix2 line.
        self.assertFalse([r for r in rows
                          if -6416 <= float(r["x"]) <= -6378 and -3970 <= float(r["y"]) <= -3946
                          and 70.7 <= float(r["z"]) <= 72.6], "a row stands in the avoid box (the magma pool)")
        # The hall's thin floor over the WMO magma (specs/dmr-step453.md): a stand on it is clamped to the lava level - 2 (70.25,
        # the lava tick), so every stand is now on a solid column. The group-16 stand is -6322 -3880 72.6 (the nearest non-solid
        # cell 9.6 yd off), group 17's is -6281 -3915.5 72.6 and the moved leg-8 pack's is -6264 -3914 72.6; the old stands
        # -6328 -3861.5 (fix 4) and -6332.93 -3863.11 (fix 3) are gone for good: they are what killed the party.
        self.assertFalse([r for r in rows
                          if -6385 <= float(r["x"]) <= -6330.5 and -3870 <= float(r["y"]) <= -3830
                          and 69.9 <= float(r["z"]) <= 73.3], "a row stands in the avoid box (the hall lava sheet)")
        for old in ((-6328.0, -3861.5), (-6332.93, -3863.11)):
            self.assertFalse([r for r in rows
                              if abs(float(r["x"]) - old[0]) < 0.01 and abs(float(r["y"]) - old[1]) < 0.01],
                             "the stand %s over the magma is back" % (old,))
        halgan = leg_steps(rows, 4)
        stand = [r for r in halgan if r["kind"] == "pull" and abs(float(r["x"]) + 6308.27) < 0.01]
        self.assertEqual(len(stand), 1, "the group-16 62035 pull is missing")
        i = halgan.index(stand[0])
        self.assertEqual([(r["x"], r["y"], r["z"]) for r in halgan[i - 4:i]],
                         [("-6400", "-3880", "73.2"), ("-6370", "-3890", "73.2"), ("-6341.7", "-3892.5", "72.7"),
                          ("-6322", "-3880", "72.6")], "the south-shore approach chain or the solid stand is missing")
        # Group 17's approach round the open pool, then the leg-8 pack right after it: each stand is on solid floor and 15-22 yd
        # from its pack, inside the run leader's 24.5 yd shot rule (so it shoots from the stand, never walks onto the bridge).
        g17 = [r for r in halgan if r["kind"] == "pull" and abs(float(r["x"]) + 6273.79) < 0.01]
        self.assertEqual(len(g17), 1, "the group-17 pull is missing")
        j = halgan.index(g17[0])
        self.assertEqual([(r["x"], r["y"], r["z"], r["comment"]) for r in halgan[j - 2:j]],
                         [("-6326", "-3912", "72.6", "pull group 17 approach"),
                          ("-6281", "-3915.5", "72.6", "pull group 17 approach")], "the group-17 approach is missing")
        moved = [r for r in halgan if r["kind"] == "pull" and (r["x"], r["y"], r["z"]) == ("-6261.17", "-3932.36", "72.6")]
        self.assertEqual(len(moved), 1, "the moved 62035/62036/62046 pull is missing from leg 4")
        self.assertEqual((moved[0]["entries"], moved[0]["radius"]), ("62035,62036,62046", "13.7"))
        m = halgan.index(moved[0])
        self.assertEqual([(r["x"], r["y"], r["z"], r["comment"]) for r in halgan[m - 1:m]],
                         [("-6264", "-3914", "72.6", "pull group 18 approach")], "the moved pack's stand is missing")
        self.assertFalse([r for r in leg_steps(rows, 8)
                          if (r["x"], r["y"], r["z"]) == ("-6261.17", "-3932.36", "72.6")],
                         "the 62035/62036/62046 pack is still pulled in leg 8")
        # r41s2 slotA step 755: the group-10 pull of 62073 has its own approach now, and the group-17 pull's approach replaces
        # the exporter's 5 cut rows (they lay on the sinking line over the thin floor). Each stand is on solid floor, 19-21 yd
        # from the pack (inside the 24.5 yd shot rule), and every hop of both chains is a complete server path with no sinking
        # point (sinkgate.py, standpath.py). Remove a row and this fails: it pins the only stands that keep the lead out of
        # the pocket.
        floor = leg_steps(rows, 8)
        for pack, group, chain in (
                (("-6100.52", "-3971.36", "73.22"), "10",
                 [("-6130", "-4005", "72.6"), ("-6135", "-3990", "72.6"), ("-6119", "-3966", "72.6")]),
                (("-6290.9", "-4016.66", "73.19"), "17",
                 [("-6284", "-3968", "72.6"), ("-6290", "-3996", "72.6")])):
            i = next(j for j, r in enumerate(floor) if r["kind"] == "pull" and (r["x"], r["y"], r["z"]) == pack)
            self.assertEqual([(r["x"], r["y"], r["z"], r["comment"]) for r in floor[i - len(chain):i]],
                             [(x, y, z, "pull group %s approach" % group) for x, y, z in chain],
                             "the solid-floor approach of the pull at %s is missing" % (pack,))
        self.assertFalse([r for r in halgan if r["kind"] == "pull" and abs(float(r["x"]) + 6360.97) < 0.01],
                         "the 62035 that stands in the lava sheet is still a pull")
        self.assertFalse([r for r in halgan if r["kind"] == "pull" and abs(float(r["x"]) + 6357.21) < 0.01
                          and 106.9 < float(r["z"]) < 107.1], "the ledge 62036 with no dry stand is still a pull")
        self.assertFalse([r for r in rows if abs(float(r["x"]) + 6404.5337) < 0.01 and abs(float(r["y"]) + 3950.9333) < 0.01],
                         "the old step 475 dip point is still exported")
        self.assertIn(("move", "-6387.47", "-3977.33", "72.69"),
                      [(r["kind"], r["x"], r["y"], r["z"]) for r in leg_steps(rows, 5)],
                      "the east-rim row of the halgan climb is missing")
        # The long walks are `via` chains, one row per ~45 yd of the server's own path (the refused-hop fix).
        self.assertGreaterEqual(len([r for r in leg_steps(rows, 5) if r["comment"] == "via"]), 19)
        self.assertGreaterEqual(len([r for r in leg_steps(rows, 9) if r["comment"] == "via"]), 44)
        self.assertEqual([r["kind"] for r in leg_steps(rows, 9)][-1], "boss")
        pulls = [r for r in rows if r["kind"] == "pull"]
        self.assertFalse({e for r in pulls for e in r["entries"].split(",")} & {"4075", "14881"}, "a critter is still a pull member")
        self.assertEqual((pulls[0]["entries"], float(pulls[0]["radius"])), ("62052", 5.0))
        # The Bruiser that stood inside Bogpaw's and Gowlfang's 20 yd detection is not a pull row any more.
        self.assertFalse([r for r in pulls if abs(float(r["x"]) + 5985.32) < 0.5 and abs(float(r["y"]) + 3626.44) < 0.5])
        # The map's boss rows stay in the migration that introduced them (20261024151500): this one re-exports the same plan on
        # the same map, so it must not delete or re-seed ai_dungeon_boss (the exporter writes no boss rows).
        earlier = dict(migrations())["20261024151500_world.sql"]
        self.assertIn("DELETE FROM `ai_dungeon_boss` WHERE `map` IN (816) AND `entry` IN (62056,62057,62072,62069,62037);", earlier)
        self.assertIn("REPLACE INTO `ai_dungeon_boss` (`map`, `entry`, `name`) VALUES\n(816, 62056, 'Bogpaw Truthsay'),\n(816, 62057, 'Gowlfang'),\n(816, 62072, 'Searistrasz'),\n(816, 62069, 'Halgan Redbrand'),\n(816, 62037, 'Zuluhed the Whacked');", earlier)
        self.assertNotIn("ai_dungeon_boss", text, "the re-export must leave the map's boss rows alone")
        self.assertIn("DELETE FROM `ai_dungeon_step` WHERE `plan` = 'dragonmaw_retreat';", text)
        self.assertEqual(re.findall(r"(?m)^DELETE FROM `ai_dungeon_step` WHERE `plan` = '(\w+)';", text), ["dragonmaw_retreat"])

    def test_black_morass_is_the_plan_with_the_census_fixes(self):
        # 20261024060800 (turtle-agent 7b712c15, 2026-10-24 bm-fix1): The Black Morass draft with the census fixes of
        # PREP-BLACK-MORASS-2026-10-24.md plus the r28d step-1 fix (69 plain leg-0 via points from the landing to the
        # to along the 2,678 yd live mesh path), no row hand-set, with the boss rows of map 269. 4 legs, 591 steps
        # (596 rows: 335 move (69 via + 266), 252 pull, 4 boss; 131 + 23 + 66 + 32 pulls), bosses in leg order, no
        # boss.first and no door/event step. The 28 Snakes (2914, a level 1 critter) are out of every pull; the two
        # split packs are merged; Epochronos' guard is the last pull of leg 2; every radius (< 16 yd) reaches its own
        # group from the row point and no further. Medivh 50105 is faction 35 (friendly) and stays as a destination row.
        name, text = newest_plan_text("black_morass")
        self.assertEqual(name, "20261024060800_world.sql")
        self.assertIn("('black_morass', 269, '', '', '65124,65102,65105,61575', 'turtle-agent 7b712c15 dungeons/black_morass.json', 'plan v1, draft (not mapped, not live-checked)');", text)
        legs = leg_rows(text, "black_morass")
        self.assertEqual([legs[i][0] for i in range(4)],
                         ["antnormi", "medivh", "time-lord_epochronos", "chronar"])
        rows = [r for r in self.step_rows(text) if r["plan"] == "black_morass"]
        self.assertEqual(len(rows), 591)
        self.assertEqual({r["kind"] for r in rows}, {"move", "pull", "boss"})
        self.assertEqual([len([r for r in leg_steps(rows, leg) if r["kind"] == "pull"]) for leg in range(4)],
                         [131, 23, 66, 32])
        self.assertEqual([r["entry"] for r in rows if r["kind"] == "boss"], ["65125", "50105", "65116", "65113"])
        for leg in range(4):
            self.assertEqual(leg_steps(rows, leg)[-1]["kind"], "boss", leg)
        # Leg 0 opens with the 69 via rows from the landing walk (the r28d step-1 fix), then the to row, then pulls.
        steps0 = leg_steps(rows, 0)
        self.assertEqual([r["kind"] for r in steps0[:69]], ["move"] * 69)
        self.assertEqual([(r["x"], r["y"]) for r in steps0[:2]],
                         [("-1950.93", "6581.6"), ("-1936", "6595.73")])
        self.assertEqual((steps0[69]["kind"], steps0[69]["x"], steps0[69]["y"], steps0[69]["comment"]),
                         ("move", "-1961.5", "6797", "to"))
        self.assertEqual((steps0[70]["kind"], steps0[70]["x"], steps0[70]["y"], steps0[70]["z"], steps0[70]["entries"]),
                         ("pull", "-1961.5", "6797", "26.53", "65112"))
        self.assertEqual([r["entries"] for r in rows if r["kind"] == "boss"], ["", "", "", ""])
        pulls = [r for r in rows if r["kind"] == "pull"]
        self.assertFalse([r for r in pulls if "2914" in r["entries"].split(",")], "a snake is still a pull member")
        # The Riftguard + four Dragonspawn pack at y 6720 is one 5-unit row now (the exporter lists the distinct entries);
        # its radius covers the farthest member from the row's centre point.
        self.assertEqual([(r["x"], r["y"], r["z"], float(r["radius"])) for r in pulls
                          if r["entries"] == "65101,65100" and abs(float(r["x"]) + 1633.65) < 0.5],
                         [("-1633.65", "6726.27", "-222.43", 12.1)])
        # The Time Anomaly 50106 at 18.9 yd from Epochronos (detection 20) is leg 2's last pull, right before the boss row.
        steps2 = leg_steps(rows, 2)
        self.assertEqual((steps2[-2]["kind"], steps2[-2]["x"], steps2[-2]["y"], steps2[-2]["z"], steps2[-2]["entries"]),
                         ("pull", "-1450.1", "6967.23", "-14.31", "50106"))
        self.assertLessEqual(max(float(r["radius"]) for r in pulls), 16.0)
        self.assertGreaterEqual(min(float(r["radius"]) for r in pulls), 2.0)
        self.assertIn("DELETE FROM `ai_dungeon_boss` WHERE `map` IN (269) AND `entry` IN (65125, 50105, 65116, 65113);", text)
        self.assertEqual(re.findall(r"(?m)^DELETE FROM `ai_dungeon_step` WHERE `plan` = '(\w+)';", text), ["black_morass"])

    def test_razorfen_kraul_is_the_plan_with_the_pack_fixes(self):
        # 20261025003100 (turtle-agent e8ee268, 2026-10-26, after live run proof20d4 slotD: the Ramtusk leg's six pit packs
        # (steps 322-327) have no firing stand from the leg's z 80-83 floor: step 322 x3 "no firing stand found", step 323
        # "pack skipped, not cleared: 2 units out of reach", steps 324-326 deferred "not reachable from this floor", 4 packs left
        # alive; step 1's "cmd move 1951.0 1570.0 83.3 refused: unreachable" is the run's only pause). The six lower-floor packs
        # (4511 at 2122.66 1738.86, 2138.32 1744.36, 2167.05 1701.62 and 2166 1686.35, and the [4511 4514] pair at 2149.5
        # 1674-1686, z 52-59) moved to charlga_pit, the leg that walks that floor, in walk order from its south mouth, each with a
        # stand on the pit floor 20.0-22.2 yd from every spawn of its pack and a clear mesh ray; each hop between stands is a
        # complete server path (0x1: 62.0, 20.1, 11.7, 26.3, 46.6, 38.7 yd). The Ramtusk leg's pull group 4 (the 4623 pair) got a
        # stand 19-22.6 yd away (step 319 walked in blind and body-pulled 4534/4532/4518: the first wipe; group 5 has no stand
        # the clearance rule allows). Leg 0's opening walk is one via point 1954.5 1582.3 82.61, 39.9 yd from the entrance (the
        # spot the live party reached), so the run leader's blind 96-yd approach no longer cuts a hop at 1951 1570 83.3.
        # 9 legs, 836 steps, 129 pulls (31 + 20 + 8 + 8 + 12 + 10 + 6 + 23 + 4), no row hand-set. Server-exact plan-check: ok,
        # 287 points (279), 5 warnings (6), 62 hop notices (69). Replays from the run's real lead positions refuse nothing and
        # leave no pack without a stand: leg 3 0/0 (was 0 refusals, 6 packs without a stand), leg 7 0/0 (23 pulls), leg 8 0/0.
        #
        # before it: 20261025002500 (turtle-agent 470f316, 2026-10-25, after live run r39 slotD: 'no firing stand found' at steps 10 and 25
        # x5 (packs 4511/4512 of the Roogug leg: 43 min, 14 deaths) and at step 315 (rotthorn: 2 of the 3 wipes and the only
        # pause), and 'pack skipped, not cleared: 1 units out of reach after 3 pulls that killed nothing' for the pair above the
        # step-315 pack): the floors, not the ranges. The leg-0 lower pocket (z 62-65) is under the upper walkway and its only
        # walkable route is the 262-yd ramp (2016 1554 -> 2049.6 1549.1 -> 2058.85 1576.5); the tank at 2016.6 1659.3 82.7 got
        # no stand for the 4511 at 2065.89 1633.15 64.3 (the path drops below the 6-yd stand ledge cut; its nearest in-sight
        # point is 53.8 yd away) and body-pulled 100+ yd through 4435/4436/4514. The four pocket packs now get one 4-point walk
        # down (pull group 6 approach) and are pulled in the order the party meets them at the ramp foot (4514, the pair, 4511,
        # 4541): each next pack is in sight from the one before (16.5 -> 8.9 -> 9.5 -> 5.8 yd). The [4512 4511] pair at 2069.06
        # 1701.43 57.604 (step 25) has a 182.9-yd server route from the party that loops east through the Ramtusk leg's packs, so
        # it moved to charlga_pit (its floor) and is pulled 17.7 yd from the stand 2050.06 1710.43 59.58. The five rotthorn pit
        # packs (4514 2080.08 1784.59 54.77, 4511 2046.61 1765.31 59.46, the pair at 2034.15/2033.7 1758-1762, 4514 2032.57
        # 1727.98 60.63, 4514 2044.24 1709.91 59.72) are 20-23 yd below the spiral: the server path from the spiral corner
        # 2102.67 1728 78.19 to the 4511 is the 454.3-yd incomplete one ending 23 yd above it, and only the pit stand 2095 1845
        # 50.8 reaches it. They moved to charlga_pit after its 11 groups, each with a stand 17.5-24.4 yd away on the pit floor
        # (2094.36 1794.12, 2065.61 1779.31, 2053.15 1774.91, 2039.57 1750.98, 2036.24 1732.91, and the leg-0 pair's 2050.06
        # 1710.43). NOT changed: steps 82-84's room (the 4532/4518/4532 at 2138.38 1630.94, 2128-2133 1613-1620, 2146.03 1614.74)
        # is a navmesh island (the corridor 2110 1600 82.1 reaches 2116.53 1613.07 82.69; 2116.53 -> 2124 1614 is type 0x8 and a
        # 32-yd grid finds 0 stand points within 24.5 yd a complete server path reaches). The live run cleared them by reach pulls
        # (30 s, 2 min, 1 min, no deaths). 9 legs, 819 steps, 122 pulls (31 + 20 + 8 + 14 + 12 + 10 + 6 + 17 + 4), no row
        # hand-set. Server-exact plan-check: ok, 279 points, 69 hop notices, 6 warnings. The 'pulls move nobody' replay refuses
        # no new row: leg 0's one refusal (the 4440 pull from the cut end 2176 1557.3) and leg 4's 65 (its pre-existing cut walk,
        # same coordinates) are 20261024154200's, and legs 7-8 refuse 0; the six moved pulls are type 0x1 from their stands.
        # before it: 20261024154200 (turtle-agent 600ea009, 2026-10-24, after live run r35 slot7b, the best run so far: 6 of 7 bosses,
        # stopped at step 655): 20261024040000's plan again, except the charlga leg, split in three, and the rotthorn leg.
        # r35's step 655 was the first pit stand (2095 1845 50.8): the whole party walked from the den floor (2157 1941 z65) up
        # the z76 dead-end ledge over the pit, 28.1 yd from and 25 yd above the stand ("no stand-in on dry ground within 25 yd",
        # "stuck 28.1 yd ... cmd move path type 0x4"), the ledge of r28. The server's own path from the den to the stand ends on
        # that ledge (type 0x4, 2 polys, 2104.5 1844.0 78.8) and the only walkable route is the 1200-yd loop round the instance
        # (the offline route is 1347.3 yd: the pit has no link up to the ledge, and the plan's 49.1 yd straight move was below the
        # exporter's 50/60 yd cut, so no row walked it). A pull group's 'approach' holds at most 4 points and every cmd move must
        # stay under the run leader's 150 yd ReactDistance, so the walk is a leg's 'via' chain: charlga_den keeps the den corridor
        # packs, charlga_pit walks the 31 via points of the loop down (every hop 8.2-48.6 yd, all server-normal) and pulls the
        # pit, and charlga_razorflank walks the 26 points back up to the route's own corner 2093.1 1767.5 79.8 for the pit-rim,
        # upper-ledge and corridor packs and Charlga. The pit stand stays 2095 1845 50.8 (15.9 yd from the pack it pulls, line of
        # sight on the open pit floor); it was never the trouble. Server-exact plan-check: ok, 269 points (212 before), and a
        # replay of the rows (a pull moves nobody; 3 legs per move; 150 yd) passes every hop of the last legs where 17 rows of
        # legs 5-6 were refused before.
        # r35's step 313 (the old rotthorn leg's 'to', 3 wipes) stood on the first Quilguard Champion's own spawn (4.1 yd on
        # arrival): the party walked into the 2-Champion pack and the Stalker 6035, 25.9 yd from that point, joined, and each wipe
        # stranded the lead at the entrance (1945.3 1550.2 82.2, teleported back). The 'to' is the spiral's own corner
        # 2102.67 1728 78.19 (25.7 yd from the pack; nothing within 20 yd of it in the map census), and the two Quilguard
        # Champions 87430/87431 (4 yd apart, the only patrolling spawns of the spiral: creature_movement, a 176 yd round trip up
        # to the pit rim and back) are one pull of two carrying that patrol; the charlga leg no longer books 87430 again.
        # 9 legs, 759 steps (630 moves, 122 pulls, 7 bosses: 32 + 20 + 8 + 14 + 17 + 10 + 6 + 11 + 4 pulls), no row hand-set.
        # before it: 20261024040000 (turtle-agent rfk-fix8 41172e39, 2026-10-24, after live run r27 slot8 run 7 stopped at step 657 in the
        # charlga leg): the same plan with leg 6 whole; pull groups 6, 7 and 8 got pit-floor stands (2095 1845, 2100 1855,
        # 2120 1840) and group 17 (the z80 ledge pack, 32 yd above its anchor) the corner 2093.1 1767.5; 706 steps.
        # before it: 20261023230100 (turtle-agent rfk-fix7 788c2ea5, 2026-10-23, after live run r26 slot8 run 6 stopped at step 618 in the
        # agathelos leg): 20261023160000's plan again, except leg 5. The leg's 'to' (step 612, where the walk back returns to) is the
        # den entry 2090.7 1922.9 66.3 on the route's last corner (was 2056.44 1920.13 69.47, under the z 99 ledge the lead bot
        # idled on), and pull groups 0, 1, 3, 4 and 5 have an approach stand on the den's own floor (group 2 shares group 1's);
        # five more rows, 702 steps. Group 5 (the pack that wiped the party; its joiners belong to group 6, 20+ yd away) is pulled
        # from 2078 1961, 37 yd from group 6.
        # before it: 20261023160000 (turtle-agent rfk-fix6 0a72713f, 2026-10-23, after live run r24 slot8 run 5 stopped at step 343 in the
        # rotthorn leg): the plan of 20261023120000 again, except the approach to leg rotthorn's pull group 14. Run 5's lead bot
        # climbed to the plateau's lip at 1995.7 1802.1 78.6 (the cut corner 1995.7333 1802.1333) and every move 331-342 was refused
        # "no walkable path": the first descent hop was the cut straight off the lip, 1970.9332 1797.0667 73.087, 25 yd long. The
        # z 73 -> 61.6 descent is the spiral ramp around the pit, and run 5's frames show the party walk it up from the lower floor.
        # Pull group 14 now has four approach points on that ramp (1989.7 1801.5 77.78, 1983.1 1800 76.2, 1976.4 1798.4 74.69,
        # 1970.9 1797.1 73.09), so the cut starts on the ramp top; the lip corners are gone and the rows after 1970.9 are
        # 20261023120000's again. The Ramtusk boss approach stays (no boss-approach point is off the mesh; run 5's step-299 pause
        # was the post-wipe walk back). No row hand-set, with the boss rows of map 47 left in 20261022210300. 7 legs, 697 steps
        # (32 + 20 + 8 + 14 + 17 + 10 + 22 pulls), bosses in leg order, no door or event step. The plan row has the unit lists; the
        # packs that stand with a boss are that boss row's entries, not pulls; Halmgar 4842 and the Blind Hunter 4425 are pull
        # members.
        name, text = newest_plan_text("razorfen_kraul")
        self.assertEqual(name, "20261025003100_world.sql")
        self.assertIn("('razorfen_kraul', 47, '4427,4515,4522,4440', '4427', '', 'turtle-agent e8ee268 dungeons/razorfen_kraul.json', 'plan v1, draft (not mapped, not live-checked)');", text)
        self.assertEqual(len(leg_rows(text, "razorfen_kraul")), 9)
        rows = [r for r in self.step_rows(text) if r["plan"] == "razorfen_kraul"]
        self.assertEqual(len(rows), 836)
        # The opening stand is the lower-floor pocket 1994 1615 (the old 1987.53 1600.03 has the upper walkway overhead); the five
        # corridor groups all have radius 10 (group 0 was 25 and owned the next group's Quilguards; group 5's default was 19.3), and
        # seq 5 is the explicit corridor regroup move before pull group 4 (r19-19z37: the two ranged bots climbed the west ramp while
        # idle).
        first = leg_steps(rows, 0)
        # pf-rfk-ramtusk: the opening walk starts on the via point the live party reached (proof20d4 step 1's refused hop)
        self.assertEqual((first[0]["kind"], first[0]["x"], first[0]["y"], first[0]["z"], first[0]["comment"]),
                         ("move", "1954.5", "1582.3", "82.61", "via"))
        self.assertEqual((first[1]["kind"], first[1]["x"], first[1]["y"], first[1]["z"]), ("move", "1994", "1615", "83.2"))
        self.assertEqual((first[2]["kind"], first[2]["entries"], first[2]["radius"]), ("pull", "4530", "10"))
        self.assertEqual([r["radius"] for r in first[3:6]], ["10", "10", "10"])
        self.assertEqual((first[6]["kind"], first[6]["x"], first[6]["y"], first[6]["z"], first[6]["comment"]),
                         ("move", "2000", "1635", "81", "pull group 4 approach"))
        # The two stands (r20-19z39: pulls 39 and 42 walked the long way; r22 slot5: the room's 2180 1604 was refused): 2110 1600 82.1
        # before the 4532 pull and the same 2110 1600 82.1 before the 4440 pull, whose radius is the explicit 10.
        stands = [(r["x"], r["y"], r["z"], r["comment"]) for r in leg_steps(rows, 0)
                  if r["kind"] == "move" and r["comment"] in ("pull group 20 approach", "pull group 23 approach")]
        self.assertEqual(stands, [("2110", "1600", "82.1", "pull group 20 approach"),
                                  ("2110", "1600", "82.1", "pull group 23 approach")])
        self.assertEqual([r["radius"] for r in leg_steps(rows, 0) if r["kind"] == "pull" and r["entries"] == "4440"], ["10"])
        # Step 20 (r23 slot8 paused once, "the lead bot is stuck 28.6 yd from 2048.0 1578.7 64.6"): the r24 slot8 run showed the
        # approach moves that replaced the corner led the party onto the navmesh island at 2076.5 1601.5 66.0, so the corner is back:
        # pull 17's hop from pull group 16 is the run-3 cut, 2048.0 1578.7 64.6 first (one resume there is accepted).
        paths0 = [(r["x"], r["y"], r["z"]) for r in leg_steps(rows, 0) if r["comment"] == "approach path"]
        cut17 = paths0.index(("2047.9999", "1578.6666", "64.60514"))
        self.assertEqual(paths0[cut17:cut17 + 3],
                         [("2047.9999", "1578.6666", "64.60514"),
                          ("2050.1333", "1581.8666", "64.50514"),
                          ("2061.3333", "1623.2", "64.28699")])
        self.assertFalse([r for r in leg_steps(rows, 0) if r["comment"] == "pull group 17 approach"],
                         "pull 17's approach points are reverted")
        self.assertEqual([len([r for r in leg_steps(rows, leg) if r["kind"] == "pull"]) for leg in range(9)], [31, 20, 8, 8, 12, 10, 6, 23, 4])
        # Step 289 (r23 slot8: `cmd move 2140.0 1724.5 53.0 refused: unreachable`): the last lower-floor pull's hop to pull group 13
        # is anchored on the lead's floor, 2129.3 1716 80.9, and its cut walks the upper floor only.
        ramtusk = leg_steps(rows, 3)
        # pf-rfk-ramtusk: the 4623 pair (pull group 4) is shot from the upper-floor stand 2136.98 1638.69 82.2 (19-22.6 yd), and the
        # leg keeps no pull on the pit floor: the six lower packs are charlga_pit's now.
        g4 = next(i for i, r in enumerate(ramtusk) if r["comment"] == "pull group 4")
        self.assertEqual((ramtusk[g4 - 1]["kind"], ramtusk[g4 - 1]["x"], ramtusk[g4 - 1]["y"], ramtusk[g4 - 1]["z"],
                          ramtusk[g4 - 1]["comment"]),
                         ("move", "2136.98", "1638.69", "82.2", "pull group 4 approach"))
        self.assertFalse([r for r in ramtusk if r["kind"] == "pull" and float(r["z"]) < 70],
                         "the Ramtusk leg pulls a pit-floor pack again")
        after7 = next(i for i, r in enumerate(ramtusk) if r["comment"] == "pull group 7 approach")
        self.assertEqual([(r["kind"], r["x"], r["y"], r["z"], r["comment"]) for r in ramtusk[after7 - 1:after7 + 2]],
                         [("pull", "2128.7", "1704.86", "80.48", "pull group 6"),
                          ("move", "2129.3", "1716", "80.9", "pull group 7 approach"),
                          ("move", "2129.5999", "1689.0667", "82.18699", "approach path")])
        self.assertTrue(all(float(r["z"]) > 80 for r in ramtusk[after7:] if r["kind"] == "move"),
                        "the Ramtusk approach cut walks the lower floor again")
        # Step 331 (r24 slot8 run 5: the lead stood on the plateau's lip at 1995.7 1802.1 78.6 and every move 331-342 was refused
        # "no walkable path"): the cut into rotthorn's spiral descent starts on the ramp the party walked, four approach moves to
        # pull group 14; the old lip corners 1995.7333 1802.1333 and 1994.6665 1802.6666 are gone.
        rotthorn = leg_steps(rows, 4)
        after13 = next(i for i, r in enumerate(rotthorn) if r["comment"] == "pull group 8") + 1
        self.assertEqual([(r["kind"], r["x"], r["y"], r["z"], r["comment"]) for r in rotthorn[after13:after13 + 4]],
                         [("move", "1989.7", "1801.5", "77.78", "pull group 9 approach"),
                          ("move", "1983.1", "1800", "76.2", "pull group 9 approach"),
                          ("move", "1976.4", "1798.4", "74.69", "pull group 9 approach"),
                          ("move", "1970.9", "1797.1", "73.09", "pull group 9 approach")])
        self.assertFalse([r for r in rotthorn if r["x"] in ("1995.7333", "1994.6665")], "the plateau-lip corners are back")
        # Step 612 (r26 slot8 run 6: "the point of step 612 refused (unreachable)", then "no walkable path toward the pack at 2077.0
        # 1981.5": the walk back ended on the z 99-134 ledges over the den): the leg's 'to' is the den entry on the route, and the
        # den pulls have approach stands on the z 63-68 floor, each a short complete path from the one before.
        agathelos = leg_steps(rows, 5)
        to_idx = next(i for i, r in enumerate(agathelos) if r["comment"] == "to")
        self.assertEqual((agathelos[to_idx]["x"], agathelos[to_idx]["y"], agathelos[to_idx]["z"]), ("2090.7", "1922.9", "66.3"))
        stands5 = [(r["x"], r["y"], r["z"], r["comment"]) for r in agathelos if r["comment"].endswith(" approach")]
        self.assertEqual(stands5, [("2074", "1921", "68.24", "pull group 0 approach"),
                                   ("2074", "1927", "66.21", "pull group 1 approach"),
                                   ("2076", "1945", "64.6", "pull group 3 approach"),
                                   ("2078", "1957", "63.23", "pull group 4 approach"),
                                   ("2078", "1961", "63.11", "pull group 5 approach")])
        self.assertTrue(all(float(r["z"]) < 75 for r in agathelos[to_idx:] if r["kind"] == "move"),
                        "a den hop climbs to a ledge")
        self.assertEqual([r["entry"] for r in rows if r["kind"] == "boss"], ["6168", "4424", "4428", "4420", "62503", "4422", "4421"])
        self.assertEqual({r["kind"] for r in rows}, {"move", "pull", "boss"})
        # The last step of every leg with a boss is its boss; the two walk legs of the charlga split end on their last pull. The boss
        # rows' entries are the packs that stand with them (Ramtusk's Spearhides, 5-6 yd from him; Aggem and the two animal legs
        # hold none).
        for leg in (0, 1, 2, 3, 4, 5, 8):
            self.assertEqual(leg_steps(rows, leg)[-1]["kind"], "boss", leg)
        for leg in (6, 7):
            self.assertFalse([r for r in leg_steps(rows, leg) if r["kind"] == "boss"], leg)
        self.assertEqual([r["entries"] for r in rows if r["kind"] == "boss"], ["4516,4442", "", "4515,4523", "4438", "", "", "4517"])
        # No pull still owns Halmgar as an uncounted extra: his pull row names him, and leg 3 has no skip left.
        pulls = [r for r in rows if r["kind"] == "pull"]
        self.assertEqual([r["entries"] for r in pulls if "4842" in r["entries"].split(",")], ["4525,4842"])
        self.assertEqual([r["entries"] for r in pulls if "4425" in r["entries"].split(",")], ["4539,4425"])
        # No pull of the charlga legs holds a Death's Head Priest under the den (the boss is 2 yd 2-D from the old priest pull): the
        # boss row is the fight; the one priest pull left (charlga_pit) stands 40 yd from her, with the Warden.
        self.assertEqual([(r["entries"], round(float(r["x"])), round(float(r["y"]))) for r in pulls if r["leg"] == "7" and "4517" in r["entries"].split(",")], [("4517,4437", 2189, 1832)])
        # Step 657 (r27 slot8 run 7: 4x "the tank could not start moving to a firing position for the pull", lead on the z74.1
        # ledge 25 yd above the pack): groups 6, 7 and 8 are pulled from approach stands on the z49 pit floor, and group 17
        # (anchor 32 yd below its z80 pack) from the route's own ledge corner.
        pit = leg_steps(rows, 7)
        self.assertEqual([(r["x"], r["y"], r["z"], r["comment"]) for r in pit if r["comment"] == "to"],
                         [("2095", "1845", "50.8", "to")])
        self.assertEqual([(r["x"], r["y"], r["z"]) for r in pit if r["comment"] == "via"][:1], [("2112", "1877.3", "72.8")])
        self.assertEqual([(r["x"], r["y"], r["z"]) for r in pit if r["comment"] == "via"][-1:], [("2126.9", "1824", "47.8")])
        self.assertEqual(len([r for r in pit if r["comment"] == "via"]), 31)
        stands6 = [(r["x"], r["y"], r["z"], r["comment"]) for r in pit if r["comment"].endswith(" approach")]
        self.assertEqual(stands6, [("2100", "1855", "49.7", "pull group 1 approach"),
                                   ("2120", "1840", "48.8", "pull group 2 approach"),
                                   ("2094.36", "1794.12", "51.52", "pull group 11 approach"),
                                   ("2065.61", "1779.31", "57.78", "pull group 12 approach"),
                                   ("2053.15", "1774.91", "59.18", "pull group 13 approach"),
                                   ("2039.57", "1750.98", "60.36", "pull group 14 approach"),
                                   ("2036.24", "1732.91", "60.75", "pull group 15 approach"),
                                   ("2050.06", "1710.43", "59.58", "pull group 16 approach"),
                                   ("2106.66", "1724.86", "54.26", "pull group 17 approach"),
                                   ("2126.32", "1728.36", "52.18", "pull group 18 approach"),
                                   ("2136.54", "1734.03", "51.88", "pull group 19 approach"),
                                   ("2149.73", "1711.63", "55.01", "pull group 20 approach"),
                                   ("2133.51", "1673.73", "58.83", "pull group 21 approach"),
                                   ("2155", "1705.35", "55.37", "pull group 22 approach")])
        # pf-rfk-stands: the six packs that used to be pulled from the spiral/walkway above now stand on the pit floor, each with
        # its stand 15-24.5 yd from the pack it pulls (the stand is the row before the pull row).
        for i, r in enumerate(pit):
            if r["comment"] in ("pull group 11 approach", "pull group 12 approach", "pull group 13 approach",
                                "pull group 14 approach", "pull group 15 approach", "pull group 16 approach",
                                "pull group 17 approach", "pull group 18 approach", "pull group 19 approach",
                                "pull group 20 approach", "pull group 21 approach", "pull group 22 approach"):
                pull = pit[i + 1]
                self.assertEqual(pull["kind"], "pull", r["comment"])
                d = ((float(r["x"]) - float(pull["x"])) ** 2 + (float(r["y"]) - float(pull["y"])) ** 2 +
                     (float(r["z"]) - float(pull["z"])) ** 2) ** 0.5
                self.assertTrue(15.0 <= d <= 24.5, "%s: %.2f yd from its pack, want 15-24.5" % (r["comment"], d))
                self.assertTrue(50.0 <= float(r["z"]) <= 61.0, "%s: z %s is not the pit floor" % (r["comment"], r["z"]))
        up = leg_steps(rows, 8)
        self.assertEqual([(r["x"], r["y"], r["z"], r["comment"]) for r in up if r["comment"] == "to"],
                         [("2093.1", "1767.5", "79.8", "to")])
        self.assertEqual([(r["x"], r["y"], r["z"]) for r in up if r["comment"] == "via"][:1], [("2117.1", "1807.5", "48")])
        self.assertEqual([(r["x"], r["y"], r["z"]) for r in up if r["comment"] == "via"][-1:], [("2102.7", "1728", "78.2")])
        self.assertEqual(len([r for r in up if r["comment"] == "via"]), 26)
        self.assertFalse([r for r in up if r["comment"].endswith(" approach")],
                         "the leg's own 'to' is the corner the ledge pack is pulled from")
        # r35's step 313: the rotthorn 'to' is off its first pack, and the patrolling Champion pair is one pull of two.
        self.assertEqual([(r["x"], r["y"], r["z"]) for r in leg_steps(rows, 4) if r["comment"] == "to"],
                         [("2102.67", "1728", "78.19")])
        self.assertEqual([(r["entries"], r["x"], r["y"], r["radius"]) for r in leg_steps(rows, 4)
                          if r["comment"] == "pull group 1"], [("4623", "2092.61", "1774.1", "6")])
        self.assertFalse([r for r in up if r["kind"] == "pull" and abs(float(r["x"]) - 2094.39) < 0.5
                          and abs(float(r["y"]) - 1773.56) < 0.5], "87430 is booked twice")
        self.assertEqual(re.findall(r"(?m)^DELETE FROM `ai_dungeon_step` WHERE `plan` = '(\w+)';", text), ["razorfen_kraul"])
        # The ai_dungeon_boss rows stay in 20261022210300 (the boss seed is per map and a repeat would trip test_boss_table_is_the_seed);
        # this file replaces the plan rows only.

    def test_gnomeregan_is_the_plan_with_the_pack_fixes(self):
        # 20261024152900 (turtle-agent d04ea67, 2026-10-24): r34 slotF stopped at step 424 of 628 because the lead stood
        # on the -677.7 416.1 z -271 navmesh seam and the next hop was refused. The full skipped-pull server replay over
        # the exported rows fixed that seam stop and four more refused/under-floor cut rows (leg 0 pull groups 70/71,
        # leg 2 pull group 8, leg 3's -683.9 to -725.33 ramp hop and pull group 18); 20261024151000 stays applied and
        # its plan is carried over except the rows named here. The plan row's unit lists are unchanged; the counts are
        # 5 legs, 627 steps, bosses in leg order (7800 last).
        name, text = newest_plan_text("gnomeregan")
        self.assertEqual(name, "20261024152900_world.sql")
        self.assertIn("('gnomeregan', 90, '6392,6329', '6392', '6220,6226,6227,7849,6223', 'turtle-agent d04ea67 dungeons/gnomeregan.json', 'plan v1, draft (not mapped, not live-checked)');", text)
        self.assertEqual(len(leg_rows(text, "gnomeregan")), 5)
        rows = [r for r in self.step_rows(text) if r["plan"] == "gnomeregan"]
        self.assertEqual(len(rows), 627)
        self.assertEqual([len([r for r in leg_steps(rows, leg) if r["kind"] == "pull"]) for leg in range(5)], [87, 37, 17, 23, 4])
        self.assertEqual([r["entry"] for r in rows if r["kind"] == "boss"], ["7079", "6235", "6229", "7800"])
        self.assertEqual((rows[-1]["leg"], rows[-1]["kind"], rows[-1]["entry"]), ("4", "boss", "7800"))
        self.assertFalse([r for r in rows if r["entry"] == "6228"], "the Ambassador's boss step is back")
        # Step 376 (1-based) is the Electrocutioner's boss row; the failed exporter cut is gone, and its two approach moves stand before it.
        self.assertEqual((rows[375]["leg"], rows[375]["kind"], rows[375]["entry"]), ("1", "boss", "6235"))
        leg1 = leg_steps(rows, 1)
        self.assertEqual([(r["kind"], r["comment"]) for r in leg1[-3:]],
                         [("move", "boss approach"), ("move", "boss approach"), ("boss", "boss")])
        self.assertEqual([(float(r["x"]), float(r["y"]), float(r["z"])) for r in leg1[-3:-1]],
                         [(-524.0, 582.7, -230.56), (-541.58, 534.7, -216.68)])
        self.assertFalse([r for r in leg1 if abs(float(r["x"]) + 541.31885) < 0.001 and abs(float(r["y"]) - 535.48035) < 0.001],
                         "the off-mesh exporter cut is back")
        # Leg 2: the via on the Electrocutioner ramp floor and the z -312 pull. Its approach now ends at -672.8 417.3
        # after the -677.7 416.1 seam instead of standing on it: no move row of the leg is within 2 yd of the seam, and
        # -704 426.6665 -273.06 is the explicit hop into the seam's far side. The last move before the pull is the
        # 32.1 yd stand the cut generates; the old -623.2 and -822.7 approach rows are now path rows.
        leg2 = leg_steps(rows, 2)
        self.assertEqual((leg2[0]["kind"], leg2[0]["x"], leg2[0]["y"], leg2[0]["z"], leg2[0]["comment"]),
                         ("move", "-544.9", "551.8", "-219.18", "via"))
        k = [j for j, r in enumerate(leg2) if r["kind"] == "pull" and r["comment"] == "pull group 2"][0]
        self.assertEqual((int(leg2[k]["seq"]), leg2[k]["entries"], float(leg2[k]["radius"])), (71, "6226", 10.0))
        approach = [r for r in leg2[:k] if r["kind"] == "move" and r["comment"] == "pull group 2 approach"]
        self.assertEqual([(r["x"], r["y"], r["z"]) for r in approach],
                         [("-704", "426.6665", "-273.06"), ("-672.8", "417.3", "-273.06"),
                          ("-671.10754", "524.2145", "-272.87"), ("-751.2637", "548.34406", "-290.81")])
        self.assertEqual([int(r["seq"]) for r in approach], [44, 45, 54, 57])
        seam = [(-677.7, 416.1, -271.0)]
        self.assertGreater(min(math.dist([float(r[c]) for c in ("x", "y", "z")], seam[0])
                               for r in leg2 if r["kind"] == "move"), 2.0,
                           "a move row stands on the -677.7 416.1 navmesh seam again")
        # Pull group 8's approach replaces the under-floor cut row -656.09 520.05 -276.72 (3.86 yd under the floor)
        # and the two seam rows the old cut wrote:
        self.assertEqual([(r["x"], r["y"], r["z"]) for r in leg2
                          if r["kind"] == "move" and r["comment"] == "pull group 8 approach"],
                         [("-656.0925", "520.0519", "-272.87"), ("-672.8", "417.3", "-273.06"),
                          ("-700", "426.6665", "-273.06")])
        self.assertFalse([r for r in leg2 if abs(float(r["x"]) + 677.6001) < 0.001 and abs(float(r["y"]) - 416) < 0.001],
                         "the seam cut row is back")
        start = [j for j, r in enumerate(leg2) if r["kind"] == "pull" and r["comment"] == "pull group 1"][0] + 1
        walk = leg2[start:k]
        self.assertEqual(len(walk), 35, "the walk from pull group 1 to the z -312 pull is 35 move rows")
        self.assertTrue(all(r["kind"] == "move" for r in walk))
        self.assertAlmostEqual(math.dist([float(leg2[k - 1][c]) for c in ("x", "y", "z")],
                                         [float(leg2[k][c]) for c in ("x", "y", "z")]), 32.1, places=1)
        # Leg 0's ring loop: pull group 70's -536.32 183.47 -193.66 and pull group 71's -509 128 -154.52 and
        # -520 210 -188.9 are the floor points that replace the refused -533.33 192 and -512 128 cut hops (r34's
        # step-210 refusal went to -509 128). The -512 128 row may not come back.
        leg0 = leg_steps(rows, 0)
        self.assertEqual([(r["x"], r["y"], r["z"]) for r in leg0
                          if r["comment"] in ("pull group 70 approach", "pull group 71 approach")],
                         [("-536.32", "183.47", "-193.66"), ("-509", "128", "-154.52"),
                          ("-520", "210", "-188.9")])
        self.assertFalse([r for r in leg0 if r["kind"] == "move" and r["x"] == "-512" and r["y"] == "128"],
                         "the refused -512 128 cut row is back")
        # Leg 3: the via chain crosses the seam at -704 and -672.8 and breaks the refused -683.9 to -725.33 ramp
        # hop at -690 526 -276.4; pull group 18's approach is the floor point under the old -324.05 cut row.
        leg3 = leg_steps(rows, 3)
        self.assertEqual([(r["x"], r["y"], r["z"]) for r in leg3 if r["comment"] == "via"],
                         [("-704", "426.6665", "-273.06"), ("-672.8", "417.3", "-273.06"),
                          ("-656", "437.5", "-273"), ("-683.9", "524.3", "-273.93"),
                          ("-690", "526", "-276.4")])
        self.assertEqual([(r["x"], r["y"], r["z"]) for r in leg3 if r["comment"] == "pull group 18 approach"],
                         [("-732.04", "671.15", "-328.66")])
        self.assertEqual(re.findall(r"(?m)^DELETE FROM `ai_dungeon_step` WHERE `plan` = '(\w+)';", text), ["gnomeregan"])
        # The map's ai_dungeon_boss seed stays in 20261022210500 (all five bosses incl. 6228, unchanged): this file
        # must not seed it again (the boss seed test asserts every (map, entry) is seeded once).
        self.assertNotRegex(text, r"(?m)^(?:INSERT|REPLACE) INTO `ai_dungeon_boss`")

    def test_gilneas_city_is_the_census_fixed_draft(self):
        # 20261024020700 (turtle-agent 198727f, 2026-10-24): the generated draft rebuilt from the map-815 spawn census
        # (PREP-GILNEAS-CITY-2026-10-24.md), no row hand-set. The generated draft had 171 pull groups, 49 of them nothing but
        # critters (Rat 4075, Raven 61643: faction 31, flags_extra 2, 8 hp) and 19 more with critters as members; the same pack was
        # split over 2-4 rows and the boss guards stood inside their boss's detection as pulls of their own. Now: 7 legs, 276 steps
        # (10 + 20 + 21 + 21 + 25 + 21 + 4 pulls; the 7 bosses in leg order, `first` on Ragetooth, Magnus and Mortimer), the plan
        # row's cc_priority 61426,61390,61417 and kill_first 61391,61425, every pull radius <= 10.2 yd, at most 5 units a pull, no
        # critter in any group, and the boss rows of map 815 scoped to its own seven entries.
        name, text = newest_plan_text("gilneas_city")
        self.assertEqual(name, "20261024020700_world.sql")
        self.assertIn("('gilneas_city', 815, '61426,61390,61417', '', '61391,61425', 'turtle-agent 198727f dungeons/gilneas_city.json', 'plan v1, draft (not mapped, not live-checked)');", text)
        legs = leg_rows(text, "gilneas_city")
        self.assertEqual([legs[i][0] for i in range(7)],
                         ["matthias_holtz", "packmaster_ragetooth", "judge_sutherland", "dustivan_blackcowl",
                          "marshal_magnus_greystone", "regent-lord_mortimer_harlow", "genn_greymane"])
        rows = [r for r in self.step_rows(text) if r["plan"] == "gilneas_city"]
        self.assertEqual(len(rows), 276)
        self.assertEqual({leg: len(leg_steps(rows, leg)) for leg in range(7)},
                         {0: 17, 1: 65, 2: 37, 3: 52, 4: 63, 5: 36, 6: 6})
        self.assertEqual([len([r for r in leg_steps(rows, leg) if r["kind"] == "pull"]) for leg in range(7)],
                         [10, 20, 21, 21, 25, 21, 4])
        self.assertEqual([r["entry"] for r in rows if r["kind"] == "boss"],
                         ["61419", "61420", "61421", "61422", "61423", "61264", "61418"])
        self.assertEqual([r["entries"] for r in rows if r["kind"] == "boss"],
                         ["", "61425", "", "", "61417", "61263", ""])
        self.assertEqual({r["kind"] for r in rows}, {"move", "pull", "boss"})
        for leg in range(7):
            self.assertEqual(leg_steps(rows, leg)[-1]["kind"], "boss", leg)
        pulls = [r for r in rows if r["kind"] == "pull"]
        self.assertFalse([r["entries"] for r in pulls
                          if "4075" in r["entries"].split(",") or "61643" in r["entries"].split(",")],
                         "a critter (Rat 4075, Raven 61643) is in a pull")
        self.assertLessEqual(max(float(r["radius"]) for r in pulls), 10.2)
        self.assertLessEqual(max(len(r["entries"].split(",")) for r in pulls), 5)
        # Every pull position is a spawn row of the map-815 export (the 5-unit Cleric pack at -1468.5 2380.3 183.7 is the widest).
        self.assertEqual([(r["entries"], float(r["radius"])) for r in pulls
                          if r["entries"] == "61364,61389,61394,61391"], [("61364,61389,61394,61391", 10.2)])
        # Each leg starts where the previous leg's boss died (leg 0 at the entrance, leg 6 at the Harlow stair foot).
        self.assertEqual([(r["x"], r["y"], r["z"]) for r in rows if r["kind"] == "move" and r["comment"] == "to"],
                         [("-1100", "2237.75", "182.86"), ("-1100.35", "2332.63", "182.737"),
                          ("-1288.2", "2221.51", "183.195"), ("-1475.25", "2321.64", "185.343"),
                          ("-1450.66", "2567.06", "182.67"), ("-1061.79", "2805.39", "182.25"),
                          ("-934.875", "2971.25", "182.954")])
        self.assertEqual(re.findall(r"(?m)^DELETE FROM `ai_dungeon_step` WHERE `plan` = '(\w+)';", text), ["gilneas_city"])
        self.assertIn("DELETE FROM `ai_dungeon_boss` WHERE `map` IN (815) AND `entry` IN (61419, 61420, 61421, 61422, 61423, 61264, 61418);", text)

    def test_blackfathom_deeps_is_the_plan_with_the_kelris_fix(self):
        # 20261024150300 (turtle-agent 3ec9ef9, 2026-10-24): 20261024060400 again, with leg twilight_lord_kelris
        # de-pooled for the r29 slot6 stall (step 580 pulled 4827 at -818.6 -54.69 z -38.27, 9 yd below the
        # hall floor z -29.6: 4 pauses 'no walkable path toward the pack', lead in water path 0x81). The 8
        # pulls whose census members stand in the pool below the hall floor (z more than 5 yd below z -29.6,
        # or water-only floor in map 48 mmaps) are gone: 4823 at -761.47 -34.66 z -40.33, 4827 at -818.6
        # -54.69 z -38.27, 4827 at -816.93 -88.56 z -40.27, 4823 at -844.07 -70.65 z -35.84, 4827 at
        # -843.47 -94.06 z -37.24, 4827 at -778.37 -94.49 z -40.14, 4823 at -808.04 -116.28 z -36.87 and
        # 4827 at -795.99 -156.21 z -43.10. The step-574 walk (-785 -58 to -816 -59, z -29) wiped twice
        # straight through the un-pulled 4811/4809/4811 pack at -816.2 -55.4: it gets the explicit pull
        # it never had, from the stand (-806 -58 -29.64, on the hall floor mesh, 10.5 yd off the pack and
        # 21.1 yd clear of the 4812 pair south) before the west walk, in route order. Route order, the
        # boss, the fires, the waves and the portal are unchanged. 7 legs, 627 steps
        # (44 + 8 + 47 + 11 + 17 + 20 + 3 pulls), bosses in leg order. The plan row keeps the unit lists;
        # the packs that stand with a boss are that boss row's entries; 9 groups carry patrol blocks.
        name, text = newest_plan_text("blackfathom_deeps")
        self.assertEqual(name, "20261024150300_world.sql")
        self.assertIn("('blackfathom_deeps', 48, '4820,4809,4811,4799,4814', '4820,4809', '4809,4813', 'turtle-agent 3ec9ef94237cda3a3a912640452ec8078471a78d dungeons/b', 'plan v1, draft (not mapped, not live-checked)');", text)

        # leg_rows() matches [A-Za-z0-9_] leg names only; ghamoo-ra, old_serra'kis and aku'mai are not
        # words, so the legs are counted from the step rows (each leg's boss step proves its rows are there).
        rows = [r for r in self.step_rows(text) if r["plan"] == "blackfathom_deeps"]
        self.assertEqual(len(rows), 627)
        self.assertEqual([len([r for r in leg_steps(rows, leg) if r["kind"] == "pull"]) for leg in range(7)], [44, 8, 47, 11, 17, 20, 3])
        self.assertEqual([r["entry"] for r in rows if r["kind"] == "boss"], ["4887", "4831", "6243", "62530", "4830", "4832", "4829"])
        self.assertEqual({r["kind"] for r in rows}, {"move", "pull", "boss", "open", "wait_clear", "wait_object"})
        # The last step of every leg is its boss, except leg 5 (its after_boss fires and waits follow the Kelris row).
        for leg in range(7):
            if leg == 5:
                continue
            self.assertEqual(leg_steps(rows, leg)[-1]["kind"], "boss", leg)
        self.assertEqual([r["entries"] for r in rows if r["kind"] == "boss"], ["", "4807", "4819,4820", "", "", "", ""])
        # Leg 5 in route order: the hall packs first, the Kelris-adjacent pack last of the pre-boss packs (the
        # four 4825 packs south of Kelris follow it, then the boss, then the fires). The pool pulls are gone,
        # so group 3 (the 4811/4809/4811 pack at -819.39 -56.72 the step-574 walk wiped on) pulls from its new
        # hall-floor stand (-806 -58) before the west walk; the platform caster split (groups 5-6) is unchanged.
        leg5 = [r for r in leg_steps(rows, 5) if r["kind"] == "pull"]
        self.assertEqual([(r["entries"], r["radius"]) for r in leg5[5:7]], [("4813", "5"), ("4813,4814", "8")])
        self.assertEqual([(r["x"], r["y"]) for r in leg_steps(rows, 5) if r["kind"] == "move" and r["comment"] in ("pull group 3 approach", "pull group 5 approach", "pull group 6 approach")], [("-806", "-58"), ("-797.6", "-107.2"), ("-797.6", "-107.2")])
        self.assertEqual([(r["entries"], r["x"], r["y"]) for r in leg5[:3]], [("4809,4810", "-764.81", "-57.77"), ("4811", "-779.2", "-58.67"), ("4810,4811,4812", "-798.1", "-58.09")])
        self.assertEqual([(r["x"], r["y"], r["entries"]) for r in leg5[14:16]], [("-789.71", "-140.38", "4814"), ("-784.06", "-161.77", "4813")])
        boss5 = [r for r in leg_steps(rows, 5) if r["kind"] == "boss"][0]
        after5 = [r for r in leg_steps(rows, 5) if int(r["seq"]) > int(boss5["seq"])]
        self.assertEqual([r["kind"] for r in after5], ["move", "open", "move", "open", "move", "open", "move", "open", "wait_clear", "wait_object"])
        self.assertEqual([r["entry"] for r in after5 if r["kind"] == "open"], ["21118", "21119", "21120", "21121"])
        self.assertEqual([(r["entry"], r["x"], r["y"]) for r in after5 if r["kind"] == "wait_object"], [("21117", "-818.361", "-200.647")])
        self.assertEqual(re.findall(r"(?m)^DELETE FROM `ai_dungeon_step` WHERE `plan` = '(\w+)';", text), ["blackfathom_deeps"])
        # The map 48 ai_dungeon_boss rows stay in 20261022210800 (the seed test holds them to one seeding).

    def test_windhorn_canyon_is_the_plan_with_the_pack_fixes(self):
        # 20261024151600 (turtle-agent a39dc40, 2026-10-24) re-exports the Windhorn Canyon draft that 20261023150100 (0afaeeb) first
        # put in, with the pack fixes of PREP-WINDHORN-CANYON-2026-10-23.md, the terrace fix of run 1 (approach stands on the
        # z 241 terrace of leg 0, reached by the ring route; the leg-0 pull order changed so that no walk passes a live pack),
        # the leg-1 floor-stand fix of run 2 (leg 1 group 16 stands on the z 219 shelf, which the navmesh does not join to the z 198
        # floor: floor approach stands for group 16 and 17) and the leg-2 start fix of run 3 (leg 2 walks the z 197 floor first:
        # its to moves onto the party's floor and its groups reorder z 197, z 220, z 220, z 241 x3, z 239 x2, z 283 x9, with
        # approach stands on walked floors), no row hand-set. 20261023230000 was the second export, 20261023230200 the
        # third, 20261024060200 the fourth, 20261024150400 the fifth. The sixth adds the server-path chains of
        # SERVER-PATHCHECK-AUDIT-2026-10-24.md: leg 0's climb to the z 241 terrace is cut into three stands, leg 2 groups 3, 6, 7
        # and 8 get one chain stand each, and leg 5's to gets four via stands, so that every leg passes the server's 2048-node
        # path query (16 server refusals before, 0 after). The map-820 boss rows stay in 20261023150100. 7 legs, 2214 steps
        # (188 pulls (46 + 32 + 17 + 14 + 36 + 9 + 34), the 7 boss rows, approach moves and the cut path rows), bosses in leg
        # order, no door or event step. The plan row has
        # empty unit lists (the world data has no AI/spell rows for these entries); the packs that stand with a
        # boss are that boss row's entries; co-located packs are merged; radii are cut to the groups' members.
        # The seventh export (20261024151800, turtle-agent 7bf2dec) adds the chains a skipped pull leaves behind:
        # live r33s2 stood at the z 197 floor and step 730's approach to -7616 -3648 z 219.686 was refused
        # because pull group 1's pack was already gone, so its `pack cleared` moved nobody. 7 legs, 2985 steps.
        # The eighth export (20261024152400, turtle-agent 9e28646) splits leg-2 pull group 6 (3 elites at once wiped
        # a level-31 party 3 times live, r34 step 1025) into the Bonespeaker 62774 alone (new group 6, radius 2) and
        # the Avenger + Shaman (new group 7, radius 2), with stands 15-25 yd from their units and outside every other
        # unpulled unit's aggro + 5 yd, mesh-checked and server path-checked from the previous MOVE point; the joiner
        # is the z239 Behemoth (Stormhoof's boss.first add, not a pull); the plan CCs the Shaman (cc_always 62773).
        # 7 legs, 2906 steps. Pinned below.
        # The ninth export (20261024153200, turtle-agent 11ddb77) drops Rotag 62785 from Narlgom's boss entries:
        # the body is a feign-death spawn (unit_flags 33024 IMMUNE_TO_PLAYER, flags_extra 134217728 APPEAR_DEAD,
        # aura 51187), Narlgom's idle channel and npc_champion_rotag keep it stand-state dead and passive, so it can
        # be neither attacked nor killed; live r36 step 1177 waited on it 14+ min after Narlgom died. Narlgom summons
        # the Spirit of Champion Rotag 61410 by spell 42004 at 50%, and that one fights; the boss row is 62780 alone.
        # The rows are unchanged (2906 steps); only the boss row's entries cell is now empty.
        # The tenth export (20261024154100, turtle-agent 0a33c0c) is the legs 4-6 strict server replay: live r36 slotG
        # stopped at step 1854 (leg 4 old group 34, 62760 at -7374.29 -3641.85 z260.04, lead -7371.5 -3592.9 z242.7,
        # 49 yd away and 17 yd below: "no firing stand found"/"firing stands used up (2)"), and step 1179 refused a
        # 215.5 yd approach hop to -7617.3 -3650.0 282.9. Three terraces have no navmesh link (each server query ends
        # 51.9-119.4 yd short and the nearest reachable point is outside the 25 yd firing range), so their pulls are
        # dropped: leg 4 old groups 34/35 (two + one 62760 on z 260/261), leg 5 old groups 0-5 (six 62760 on z 259-288)
        # and leg 6 old groups 6-10 (five 62761 on z 238-241); the old leg-6 group 33 (62761 at -7135.47 -3568.04
        # z239.169) is an island too and is dropped, its descent chain moved to Ahgk'tos's boss approach. Leg-3 pull
        # group 0 gets the live firing stand -7617.3 -3650.0 282.9 as its approach. The exporter now snaps every cut via
        # point to the nearest floor (the z242 corner -7635.5576 -3477.8813 242.086 was 5.1 yd off the server mesh), so
        # the cut rows pass. 7 legs, 2906 steps, 175 pulls (46 + 32 + 18 + 14 + 34 + 3 + 28); the strict replay over
        # legs 4-6 has 0 refused hops (153 before).
        # The eleventh export (20261025000400, turtle-agent d8edf74) is the r38 slotE wasted-time audit (WHC-WASTE-2026-10-25.md):
        # the live run walked 41,994 yd (24 miles) over 2065 steps and two stretches were plan order - steps 1645-1854 (12.6 min,
        # 3156 yd) climbed back up the leg-4 ramps it had come down (the whole z 149 floor, then the z 221 groups 21-23, then the
        # z 286 terrace 24-33), and leg 6 descended to the z 148 floor, climbed 3170 yd to the z 286 terrace and descended again.
        # Legs 4 and 6 now pull their floors in one descending pass - leg 4: to/group 0 (z 220), the z 243 pair 4-5, the z 286
        # terrace 24-33, the z 213-222 groups 1-3/6/21-23, the z 149 floor 13-20/7-12, boss; leg 6: the z 244 floor 0-5, the 415-yd
        # ramp to the z 286 terrace 24-27/16-23, one descent to the z 148 floor 6-15, boss. Leg 4 group 19 (62763 at -7546.11
        # -3441.46 z 222.195) gets the stand -7531.44 -3449.56 221.49, so the hop after its pull is 68 yd and not the 175.6 yd the
        # party's stand left (over the 150 yd ReactDistance). The two boss approach chains that were those descents (leg 4's
        # z 230.69/z 197.59 and leg 6's z 199.29) are trimmed to the last stand on the boss's floor: with the low floor last they
        # walked the party back up and down for nothing. 7 legs, 2396 steps, 175 pulls (46 + 32 + 18 + 14 + 34 + 3 + 28); row
        # chains 8957 + 3817 + 7364 + 4910 + 6209 + 3498 + 5635 = 40,390 yd against 49,225 (leg 4 9659 -> 6209, leg 6 11,021 ->
        # 5635: 8835 yd less, ~35 min of walking at the run's 250 yd/min). Legs 0, 1, 2, 3 and 5 are unchanged. The walk-past
        # check (each hop's offline polyline against every pack still unpulled, 20 yd) goes 27 -> 19 close passes in leg 4 and
        # 24 -> 22 in leg 6; the strict replay over legs 4 and 6 has 0 refused hops, farthest 128.3 yd. The zig-zag of live steps
        # 1885-2033 is terrain, not plan order: -7257.07 -3560.53 244.47 to the boss is 200 yd in a line and 3007.3 yd walkable
        # (the plan's rows 3016.3 yd). Pinned below.
        name, text = newest_plan_text("windhorn_canyon")
        self.assertEqual(name, "20261025000400_world.sql")
        rows = [r for r in self.step_rows(text) if r["plan"] == "windhorn_canyon"]
        # leg_rows() matches [A-Za-z0-9_] leg names only; ahgk'tos_the_pure is not a word, so the legs are
        # counted from the step rows (each leg's boss step proves its rows are there).
        self.assertEqual({r["leg"] for r in rows}, {"0", "1", "2", "3", "4", "5", "6"})
        self.assertEqual(len(rows), 2396)
        self.assertEqual([len([r for r in leg_steps(rows, leg) if r["kind"] == "pull"]) for leg in range(7)], [46, 32, 18, 14, 34, 3, 28])
        self.assertIn("('windhorn_canyon', 820, '62773,62774', '62773', '', 'turtle-agent d8edf74 dungeons/windhorn_canyon.json', 'plan v1, draft (not mapped, not live-checked)');", text)
        self.assertEqual([r["entry"] for r in rows if r["kind"] == "boss"], ["62784", "62783", "62780", "62781", "62779", "62782", "62778"])
        self.assertEqual({r["kind"] for r in rows}, {"move", "pull", "boss"})
        # The last step of every leg is its boss; the boss rows' entries are the packs that stand with them
        # (Walgan's Bloodguards, Stormhoof's Behemoth, Ahgk'tos's Cloudstalkers; Narlgom's is empty, see the ninth export).
        for leg in range(7):
            self.assertEqual(leg_steps(rows, leg)[-1]["kind"], "boss", leg)
        self.assertEqual([r["entries"] for r in rows if r["kind"] == "boss"], ["62771", "", "", "62985", "", "", "62762"])
        self.assertEqual(re.findall(r"(?m)^DELETE FROM `ai_dungeon_step` WHERE `plan` = '(\w+)';", text), ["windhorn_canyon"])
        # The terrace fix (third export): leg 0 pulls the plateau packs 23/24 first, then the terrace in one visit - the ramp-top trio (25),
        # the Behemoth (26), the Earthkeeper trio (27), the Avenger pair (28), the east pair and Avenger (29, 30) - then the floor packs at the
        # ramp foot (31-33), the second visit (34-41) and the exit pack 45; each terrace pack reached from another layer has a
        # "pull group N approach" stand row on the z 241 floor before its pull.
        leg0 = leg_steps(rows, 0)
        pulls0 = [r for r in leg0 if r["kind"] == "pull"]
        self.assertEqual([p["entries"] for p in pulls0[23:34]],
                         ["62767,62766,62765", "62766", "62772,62766,62769", "62985", "62770,62774,62772", "62777,62774", "62865,62777", "62777",
                          "62767,62766,62765", "62760", "62763"])
        self.assertEqual([(r["comment"], r["x"], r["y"], r["z"]) for r in leg0 if r["comment"].endswith(" approach") and r["z"].startswith("24")],
                         [("pull group 25 approach", "-7786.4", "-3493.8", "240.8"), ("pull group 26 approach", "-7787.9", "-3459.4", "241.4"), ("pull group 27 approach", "-7723.1", "-3423.4", "241.7"), ("pull group 28 approach", "-7668.5", "-3405.1", "241.9"), ("pull group 29 approach", "-7660.3", "-3404.4", "241.9"), ("pull group 34 approach", "-7794.8", "-3450.1", "241"), ("pull group 35 approach", "-7816", "-3446.9", "240.9"), ("pull group 41 approach", "-7870.2", "-3445.1", "241.6"), ("pull group 45 approach", "-7761.1", "-3449.3", "241.5")])
        # The sixth export's server-path chain up to the terrace: group 25's stand is the last of three (the other two on the z 198 and
        # z 222 landings), so the climb from the z 149 floor is made of legs the server's 2048-node query answers complete.
        self.assertEqual([(r["comment"], r["x"], r["y"], r["z"]) for r in leg0 if r["comment"] == "pull group 25 approach"],
                         [("pull group 25 approach", "-7676", "-3684.27", "197.59"),
                          ("pull group 25 approach", "-7601.33", "-3418.13", "221.59"),
                          ("pull group 25 approach", "-7786.4", "-3493.8", "240.8")])
        # The leg-1 floor-stand fix (fourth export): leg 1 group 16 (the z 219 shelf pack) is pulled from its floor approach stand,
        # and group 17 from its own on the way north; the two shelf walk rows (server steps 504/505 of the third export) are gone.
        leg1 = leg_steps(rows, 1)
        self.assertEqual([(r["comment"], r["x"], r["y"], r["z"]) for r in leg1 if r["comment"].endswith(" approach")],
                         [("pull group 16 approach", "-7680", "-3605.3", "198"), ("pull group 17 approach", "-7680", "-3584", "198.4")])
        # The leg-2 start fix (fifth export): leg 2 walks the z 197 floor first - its to is on the party's floor where
        # leg 1 ends, and its pull groups reorder so the z 197 pack and the z 220 shelf pack come before the climb; the
        # upper-layer groups are pulled from approach stands on walked floors.
        leg2 = leg_steps(rows, 2)
        self.assertEqual(leg2[0]["comment"], "to")
        self.assertEqual((leg2[0]["x"], leg2[0]["y"], leg2[0]["z"]), ("-7640", "-3636", "197.4"))
        self.assertEqual([p["entries"] for p in [r for r in leg2 if r["kind"] == "pull"]],
                         ["62763,62767", "62772", "62772,62865", "62777", "62774", "62985,62773", "62774",
                          "62777,62773", "62773,62774", "62760", "62985,62760", "62760", "62764", "62760",
                          "62774", "62768", "62760", "62764"])
        # The eighth export's split (r34 step 1025): the Bonespeaker alone is group 6 (radius 2: the Avenger 5.1 yd
        # and the Shaman 5.2 yd away stay outside radius + 3), the Avenger + Shaman group 7 (radius 2); each stand is
        # 15-25 yd from its units, outside every other unpulled unit's aggro + 5 yd, mesh-checked and server-checked.
        self.assertEqual([(p["entries"], p["radius"]) for p in [r for r in leg2 if r["kind"] == "pull"]][6:8],
                         [("62774", "2"), ("62777,62773", "2")])
        self.assertEqual([(r["comment"], r["x"], r["y"], r["z"]) for r in leg2 if r["comment"].endswith(" approach")],
                         [("pull group 1 approach", "-7489.33", "-3584", "214.09"), ("pull group 1 approach", "-7617.36", "-3650.12", "219.74"),
                          ("pull group 3 approach", "-7488", "-3663.73", "215.49"), ("pull group 3 approach", "-7628", "-3570", "241"),
                          ("pull group 4 approach", "-7628", "-3570", "241"),
                          ("pull group 5 approach", "-7628", "-3570", "241"),
                          ("pull group 6 approach", "-7490.4", "-3675.2", "219.39"), ("pull group 6 approach", "-7633.6", "-3601.9", "239.8"),
                          ("pull group 7 approach", "-7633.6", "-3644", "239.3"),
                          ("pull group 8 approach", "-7490.4", "-3675.2", "219.39"), ("pull group 8 approach", "-7600", "-3625", "220"),
                          ("pull group 9 approach", "-7274.67", "-3562.67", "281.37"), ("pull group 9 approach", "-7620", "-3585", "282.3")])
        # The sixth export's server-path chains: each refused leg-2 hop gets one chain stand before its old stand, so no single server
        # query is asked for the whole climb.
        leg5 = leg_steps(rows, 5)
        self.assertEqual([(r["comment"], r["x"], r["y"], r["z"]) for r in leg5 if r["comment"] == "via"],
                         [("via", "-7834.67", "-3615.2", "172.99"), ("via", "-7677.6", "-3588.53", "198.29"),
                          ("via", "-7509.33", "-3690.67", "220.19"), ("via", "-7274.67", "-3598.13", "254.17")])
        to5 = [r for r in leg5 if r["comment"] == "to"][0]
        self.assertEqual((to5["x"], to5["y"], to5["z"]), ("-7544.51", "-3761.01", "282.885"))
        # The seventh export's chains: the party leaves the last move row, not each pull's stand, so these stands climb from it.
        # Leg 3 group 4 (z 283.18) climbs from the leg's z 239.79 `to`; the leg-3 boss approach then walks the boss's own z 239 floor
        # so leg 4's cut rows have an anchor the party is really at; the leg-4 boss approach descends from the z 286 shelf to z 149.39;
        # leg 6 group 11 climbs from the z 244.29 `to`, and groups 32/33 descend from the z 286 shelf to Ahgk'tos at z 151.365.
        leg3 = leg_steps(rows, 3)
        self.assertEqual([(r["comment"], r["x"], r["y"], r["z"]) for r in leg3 if r["comment"].endswith("approach")],
                         [("pull group 0 approach", "-7617.3", "-3650", "282.9"),
                          ("pull group 4 approach", "-7635.2", "-3676", "282.59"), ("pull group 4 approach", "-7628.77", "-3750.06", "283.31"),
                          ("boss approach", "-7489.33", "-3584", "214.09")])
        # The eleventh export: the z 286 terrace comes first (on the one ascent) and the z 149 floor last, so the boss approach
        # chain that used to BE the descent from the shelf is one stand on the boss's floor; group 19's stand keeps the hop after
        # the three z 213-222 pulls under the run leader's 150 yd ReactDistance.
        leg4 = leg_steps(rows, 4)
        self.assertEqual([(r["comment"], r["x"], r["y"], r["z"]) for r in leg4 if r["comment"].endswith("approach")],
                         [("pull group 19 approach", "-7531.44", "-3449.56", "221.49"),
                          ("boss approach", "-7569.07", "-3551.47", "149.39")])
        # The eleventh export: leg 6 climbs to the z 286 terrace by the 415-yd ramp right after the z 244 floor and descends once
        # to the boss's own z 148 floor; the group that carried the z 199.29 -> z 149.07 descent chain is now group 18, and the
        # boss keeps only that last stand (its z 199.29 point walked the party 50 yd back up after the low floor).
        leg6 = leg_steps(rows, 6)
        self.assertEqual([(r["comment"], r["x"], r["y"], r["z"]) for r in leg6 if r["comment"].endswith("approach")],
                         [("pull group 18 approach", "-7490.4", "-3675.2", "219.39"), ("pull group 18 approach", "-7688.8", "-3676", "198.29"),
                          ("pull group 18 approach", "-7448.53", "-3536.8", "149.07"), ("pull group 18 approach", "-7239", "-3445.36", "149.95"),
                          ("boss approach", "-7448.53", "-3536.8", "149.07")])
        # The map-820 boss DELETE is scoped to this plan's own entries (it shares no map with another plan); it stays in the first file.
        first = dict(migrations())["20261023150100_world.sql"]
        self.assertIn("DELETE FROM `ai_dungeon_boss` WHERE `map` IN (820) AND `entry` IN (62778, 62779, 62780, 62781, 62782, 62783, 62784);", first)
        self.assertNotIn("ai_dungeon_boss", text)

    def test_zulfarrak_names_its_area_trigger_on_the_walk_in(self):
        # The move into at_zumrah (962) names the trigger in `entry`, so the run's bots fire it without
        # AiPlayerbot.BotAreaTriggers (ai::IsBotAreaTriggerArmed). The 20261025002900 export moved the row from
        # leg 10 to leg 11 (the gahzrilla leg); its own UPDATE re-applies the entry and comment after the INSERT,
        # because 20261024000000's UPDATE still names the old leg 10 (the courtyard now).
        name, text, rows = newest_zulfarrak_rows()
        walk_in = [r for r in rows if r["leg"] == "11" and r["seq"] == "101"]
        self.assertEqual(len(walk_in), 1)
        self.assertEqual(walk_in[0]["kind"], "move")
        self.assertIn("area trigger 962", walk_in[0]["comment"])
        update = text
        statements = [s.strip() for s in re.split(r";\s*$", re.sub(r"(?m)^--.*$", "", update), flags=re.M) if s.strip()]
        ours = [s for s in statements if s.startswith("UPDATE `ai_dungeon_step` SET `entry` = 962")]
        self.assertEqual(len(ours), 1, name)
        self.assertTrue(ours[0].endswith("\nWHERE `plan` = 'zulfarrak' AND `leg` = 11 AND `seq` = 101 AND `kind` = 'move'"), name)
        # The row's comment no longer claims the allowlist is needed.
        comment = re.search(r"`comment` = '((?:[^']|'')*)'", ours[0]).group(1)
        self.assertNotIn("needs AiPlayerbot.BotAreaTriggers", comment)
        self.assertIn("no AiPlayerbot.BotAreaTriggers needed", comment)
        # Nothing newer re-inserts the zulfarrak steps (that would drop the entry again).
        later = [n for n, body in migrations() if n > name and
                 re.search(r"INSERT INTO `ai_dungeon_step`.*?\('zulfarrak',", body, re.S)]
        self.assertEqual(later, [])

    def test_hateforge_quarry_is_the_plan_with_the_census_fixes(self):
        # 20261025000600 (turtle-agent 5d111ba, 2026-10-25): the r37 slot8b waste audit. The full 479-step plan ran
        # (124.8 min, 1 death, 0 wipes) but leg 4 looped: it entered the upper shelf at group 0 (892 yd) instead of the
        # lower floor (307 yd), cleared the lower floor, then climbed 514 yd back to the east shelf, and took the
        # north spur 59-61 (131+ yd) before returning to the boss room. The leg now enters at lower group 51, clears
        # 41-54 in one shortest-path visit ending at the group-54 ramp (664 yd), keeps the upper order 0-40, 55-58, 62,
        # boss, and drops 59-61 (no boss/door needs them, 230+ yd from the boss). Leg-4 server path 5007.7 -> 3734.8 yd
        # (~8 min with the dropped fights). The hfq-fix8 step-273 magma pair is intact, now rows 300-302 (group 21);
        # 416 steps, 60 leg-4 pulls.
        # 20261024154800 (turtle-agent 6bc4887, 2026-10-24): the hfq-fix8 magma fix. r38 slotC ran the 478-step plan
        # (0 deaths) and stopped at step 273 the same way: the point -8069 -3442 z164.04 is IN the ADT magma (ground
        # 163.84 under a flat magma surface at 164.00, maps/8084738.map) and MoveTo2 refuses the END point before any
        # path is built (path=0x0); hfq-fix7 read only the navmesh polygon flags and could not see the liquid. The
        # stand becomes two via points round the south end of the channel: -8076 -3484 z169.7 then -8063 -3449
        # z169.1 (both dry, path-checked clean), 479 steps.
        # 20261024154500 (turtle-agent 0ab2802, 2026-10-24): the hfq-fix7 magma fix. r37 slot4 ran the 478-step plan
        # (Bargul, the terrace, Corrosis, Har'gesh; 1 death) and stopped at step 273, leg 4 pull group 7's approach
        # stand -8073.12 -3441.07 z162.52: 4 pauses 'the lead bot is stuck 11.4 yd from -8073.1 -3441.1 162.5', the
        # lead at -8072.5 -3452 z165.8, the server's lead log 'why=magma path=0x0'. The point sits exactly on the shared
        # edge of a ground polygon (0x01) and a magma polygon (0x02): floors at it list only the ground polygon, but it
        # is 0.00 yd from the magma polygon (0.5 yd west z162.32, 0.5 yd north z162.34 and 0.5 yd east z162.71: magma,
        # magma, ground) and the server's snap
        # of the goal can land on the magma side. The stand moves to -8069 -3442 z164.04 (3.83 yd from the magma, 14.25
        # yd from the pack centroid; server path type 0x1 ok from step 272's stop and the r37 stall); no other plan point
        # of the 478 is within 25.4 yd of a magma or slime polygon. The plan file is back in Marshal's canonical form.
        # 20261024153700 (turtle-agent a1ae53f, 2026-10-24): the hfq-fix6 leg-0 low-floor approach chains.
        # r35 slot4c stopped at step 15 of the 469-step plan: step 14's pack (-8234.2 -3371.8 132.6) was
        # deferred and the lead stood at -8225.7 -3303.2 163.6 (3.9 yd under the west-wall floor, a dead
        # island: every server query from it is NOPATH), so step 15 (-8254.9 -3377.5 139.4) had no stand
        # 15-25 yd with a path and paused 4x; the step-16 pack (-8278.3 -3366.7 168.4) is 29 yd above.
        # Leg 0 groups 10-14 now have approach chains that descend the east wall once (group 10: -8200.0
        # -3300.0 165.59 -> -8200.0 -3320.0 139.32 -> -8208.1 -3324.23 140.71) and pull the low floor
        # from stands 15.4-22.3 yd away (g11 -8211.95 -3331.25 140.73, g12 -8227.35 -3359.3 138.4, g13
        # -8234.23 -3371.84 133.15, g14 -8260.0 -3380.0 140.61 then -8280.0 -3384.0 166.75), so the west
        # wall that stranded the lead is never walked. Leg 0 group 50 gets four approach rows (through
        # -8311.76 -3338.32 173.56, -8300.24 -3327.22 171.57, -8288.72 -3316.11 176.03, -8282.97 -3310.56
        # 172.91) replacing the cut chord through the off-mesh -8293.0 -3301.9 169.2, and the Corrosis boss
        # gets four (-8216.73 -3514.3 135.58, -8218.23 -3490.35 137.07, -8219.73 -3466.4 139.24, -8221.23
        # -3442.45 135.72) replacing the cut chord through the off-mesh -8219.9 -3462.9 135.0.
        # 20261024153400 (turtle-agent bcb3d45, 2026-10-24): the hfq-fix5 stands for the leg-4 pulls. r35
        # slot4b stopped at step 273: steps 264/265 refused ('the tank could not start moving to a firing
        # position': 60720's stand 2 at 38.4 yd; 60721's stand-1 walk stalled), step 272's pack
        # (-8054.4 -3401.8 170.5) was deferred from a z162.0 stand it could not walk, and step 273's pack
        # (-8073.8 -3385.2 173.0) had no reachable stand 15-25 yd from the lead at -8063 -3461.6 172.7.
        # Five leg-4 groups get approach stands (each a move row before the pull): group 7 -8073.12 -3441.07
        # z162.52 (14.5 yd), group 8 -8069.9 -3459.9 z167.78 (15.1 yd), group 10 the east-slope floor link
        # -8049.48 -3436.5 z168.37 then -8041.87 -3433.57 z170.31, group 11 -8035.89 -3428.47 z173.15 then
        # -8044.42 -3414.46 z167.67 (16.4 yd), group 12 -8058.8 -3385.2 z171.27 (15.1 yd). Every hop is a
        # server-exact path-check type 0x1 ok from the lead's real position; the leg-5 group-6 stand of
        # hfq-fix4 is unchanged.
        # 6 legs, 416 steps (155 pulls: 54 + 0 + 29 + 5 + 60 + 7, the 5 bosses), bosses in
        # leg order; every leg with a boss ends on it (leg 0 is the trash clear, its boss moved to the terrace leg).
        # The plan row carries the census unit lists; Broody 60715 is Engineer
        # Figgles' boss.first (his lone pull is gone); the boss room is one 7-entry pull, the leg's last, and every pull
        # radius is 2.5 yd off the group's own spawns. The ramp-patrolling Overseer 60722 is pulled at group 22 of leg 0,
        # before the walk up; the terrace leg walks to the exact launch, hops to the ramp top (on the terrace
        # island), then to the stand 18.8 yd from the boss (ray clear) and the boss approach; corrosis starts with the
        # reverse hop down the same seam (exact launch at the ramp top), then the server-path waypoint.
        name, text = newest_plan_text("hateforge_quarry")
        self.assertEqual(name, "20261025000600_world.sql")
        self.assertIn("('hateforge_quarry', 808, '60717,60720,60719,60725', '', '60718,60726,60723,60712,60738', "
                      "'turtle-agent 5d111ba dungeons/hateforge_quarry.json', 'plan v1, draft (not mapped, not live-checked)');", text)
        legs = leg_rows(text, "hateforge_quarry")
        self.assertEqual([legs[i][0] for i in range(6)],
                         ["high_foreman_bargul_blackhammer", "bargul_terrace", "corrosis", "har'gesh_doomcaller",
                          "engineer_figgles", "hatereaver_annihilator"])
        rows = [r for r in self.step_rows(text) if r["plan"] == "hateforge_quarry"]
        self.assertEqual(len(rows), 416)
        self.assertEqual([len([r for r in leg_steps(rows, leg) if r["kind"] == "pull"]) for leg in range(6)], [54, 0, 29, 5, 60, 7])
        for leg in range(6):
            # Leg 0 is the trash clear (its boss moved to the terrace leg); every other leg ends on its boss.
            self.assertEqual(leg_steps(rows, leg)[-1]["kind"], "boss" if leg else "pull", leg)
        # The terrace leg: exact launch move (flags 64), the hop onto the ramp top, the landing, the stand, then the boss.
        terrace = leg_steps(rows, 1)
        self.assertEqual([(r["kind"], r["x"], r["y"]) for r in terrace if r["kind"] in ("move_hop_exact", "boss")],
                         [("move_hop_exact", "-8323.71", "-3452.3"), ("boss", "-8304.88", "-3442.24")])
        self.assertEqual([(r["kind"], r["flags"]) for r in terrace if r["kind"] == "move" and r["flags"] == "64"],
                         [("move", "64")])
        self.assertIn(("move", "-8309.45", "-3460.42"), [(r["kind"], r["x"], r["y"]) for r in terrace])
        # The corrosis leg starts with the reverse hop down the same seam, then the server-path waypoint.
        corrosis = leg_steps(rows, 2)
        self.assertEqual([(r["kind"], r["x"], r["y"]) for r in corrosis[:2]],
                         [("move", "-8323.71", "-3452.3"), ("move_hop_exact", "-8320", "-3447.5")])
        self.assertIn(("move", "-8192", "-3349.33"), [(r["kind"], r["x"], r["y"]) for r in corrosis])
        # The hfq-fix6 leg-0 approaches: group 10's east-wall descent (top, bottom, stand), one stand each for
        # groups 11-13, group 14's ramp bottom and top, then group 50's four rows in place of the dead chord.
        leg0_approaches = [(r["comment"], r["x"], r["y"], r["z"]) for r in leg_steps(rows, 0)
                           if r["kind"] == "move" and r["comment"].endswith(" approach")]
        self.assertEqual([r[1:] for r in leg0_approaches],
                         [("-8200", "-3300", "165.59"), ("-8200", "-3320", "139.32"), ("-8208.1", "-3324.23", "140.71"),
                          ("-8211.95", "-3331.25", "140.73"), ("-8227.35", "-3359.3", "138.4"),
                          ("-8234.23", "-3371.84", "133.15"), ("-8260", "-3380", "140.61"), ("-8280", "-3384", "166.75"),
                          ("-8311.76", "-3338.32", "173.56"), ("-8300.24", "-3327.22", "171.57"),
                          ("-8288.72", "-3316.11", "176.03"), ("-8282.97", "-3310.56", "172.91")])
        self.assertEqual([r[0] for r in leg0_approaches],
                         ["pull group 10 approach"] * 3 + ["pull group 11 approach", "pull group 12 approach",
                                                           "pull group 13 approach"] + ["pull group 14 approach"] * 2
                         + ["pull group 50 approach"] * 4)
        # The hfq-fix6 Corrosis approach: four rows; the off-mesh chord point is gone.
        self.assertEqual([(r["x"], r["y"], r["z"]) for r in corrosis if r["kind"] == "move" and r["comment"] == "boss approach"],
                         [("-8216.73", "-3514.3", "135.58"), ("-8218.23", "-3490.35", "137.07"),
                          ("-8219.73", "-3466.4", "139.24"), ("-8221.23", "-3442.45", "135.72")])
        # The r37/r38 step-273 magma-seam and magma stands are gone, with the hfq-fix6 points before them.
        for bad in (("-8225.7", "-3303.2"), ("-8293.01", "-3301.8716"), ("-8219.884", "-3462.8818"),
                    ("-8073.12", "-3441.07"), ("-8069", "-3442")):
            self.assertFalse([r for r in rows if (r["x"], r["y"]) == bad], bad)
        # The hatereaver leg carries the two server-path waypoints (the audit halfway point, then the
        # route midpoint), the group-6 approach stand 18.2 yd from the last pack, and still ends on its boss.
        hatereaver = leg_steps(rows, 5)
        self.assertEqual([(r["x"], r["y"]) for r in hatereaver if r["kind"] == "move" and (r["x"], r["y"]) in [("-8192", "-3349.33"), ("-8220.8", "-3445.07")]],
                         [("-8192", "-3349.33"), ("-8220.8", "-3445.07")])
        self.assertIn(("move", "-7995.97", "-3621.1"), [(r["kind"], r["x"], r["y"]) for r in hatereaver])
        self.assertEqual([(r["kind"], r["x"], r["y"]) for r in hatereaver[-3:]],
                         [("move", "-7995.97", "-3621.1"), ("pull", "-7980.74", "-3611.51"), ("boss", "-7990.59", "-3581.73")])
        # Figgles' row carries Broody as its first; no other boss does, and no pull lists a boss.first entry or a summoned unit.
        self.assertEqual([(r["entry"], r["entries"]) for r in rows if r["kind"] == "boss" and r["entries"]],
                         [("60736", "60715")])
        pulls = [r for r in rows if r["kind"] == "pull"]
        for gone in ("60715", "10116", "60738", "60735", "60829", "60737", "60736", "60734"):
            self.assertFalse([r for r in pulls if gone in r["entries"].split(",")], gone)
        # The boss room is one row (the plan's 7 spawns, deduplicated to its 4 entries) and the leg's last pull.
        self.assertEqual([r["entries"] for r in leg_steps(rows, 4) if r["kind"] == "pull"][-1], "60719,60720,60712,60722")
        # The waste audit enters leg 4 on the lower floor, not the upper shelf: group 0 is the old group 51 (60720).
        self.assertEqual([r["entries"] for r in leg_steps(rows, 4) if r["kind"] == "pull"][:3],
                         ["60720", "60724", "60719,60718,60722"])
        # The leg-4 approach stands: group 21 is the hfq-fix8 magma pair (it was group 7 at -8073.12 -3441.07 z162.52,
        # then -8069 -3442 z164.04, both in the ADT liquid; now -8076 -3484 z169.7 then -8063 -3449 z169.1, south round
        # the channel, both dry); group 22 one, groups 24 and 25 two each, group 26 one.
        self.assertEqual([(r["comment"], r["x"], r["y"], r["z"]) for r in leg_steps(rows, 4) if r["kind"] == "move" and r["comment"].endswith(" approach")],
                         [("pull group 21 approach", "-8076", "-3484", "169.7"),
                          ("pull group 21 approach", "-8063", "-3449", "169.1"),
                          ("pull group 22 approach", "-8069.9", "-3459.9", "167.78"),
                          ("pull group 24 approach", "-8049.48", "-3436.5", "168.37"),
                          ("pull group 24 approach", "-8041.87", "-3433.57", "170.31"),
                          ("pull group 25 approach", "-8035.89", "-3428.47", "173.15"),
                          ("pull group 25 approach", "-8044.42", "-3414.46", "167.67"),
                          ("pull group 26 approach", "-8058.8", "-3385.2", "171.27")])
        self.assertEqual({float(r["radius"]) for r in pulls}, {2.5})
        # The ramp Overseer is pulled at group 22 of leg 0, before the walk up (census order).
        self.assertEqual([r["entries"] for r in leg_steps(rows, 0) if r["kind"] == "pull"][22], "60722")
        self.assertEqual(re.findall(r"(?m)^DELETE FROM `ai_dungeon_step` WHERE `plan` = '(\w+)';", text), ["hateforge_quarry"])
        # The map-808 boss DELETE is scoped to this plan's own entries (it is the first plan on the map).
        self.assertIn("DELETE FROM `ai_dungeon_boss` WHERE `map` IN (808) AND `entry` IN (60735, 60829, 60737, 60736, 60734);", text)

    def test_zulfarrak_and_uldaman_final_counts(self):
        # Each plan's newest INSERT follows the DELETE of its old steps and legs, so its rows are the plan after all migrations.
        name, text = newest_plan_text("zulfarrak")
        self.assertEqual(name, "20261025002900_world.sql")
        self.assertEqual(len(leg_rows(text, "zulfarrak")), 18)
        self.assertEqual(len([r for r in self.step_rows(text) if r["plan"] == "zulfarrak"]), 247)
        name, text = newest_plan_text("uldaman")
        self.assertEqual(name, "20261022170000_world.sql")
        self.assertEqual(len(leg_rows(text, "uldaman")), 14)
        self.assertEqual(len([r for r in self.step_rows(text) if r["plan"] == "uldaman"]), 139)

    def test_frostmane_hollow_plan_is_whole(self):
        # 20261022210030 (turtle-agent b8d3f62, 2026-10-22): the first rows of the draft plan, exported with --allow-draft --gm-cross-direct
        # --approach-hops, with the census fixes (the units the core links to a boss are the boss row's entries). The plan file has no event, door or gossip step, so its 4 legs are only moves, pulls and one boss each. Every pin
        # below fails when a leg, a pack or a boss row is cut.
        name, text = newest_plan_text("frostmane_hollow")
        self.assertEqual(name, "20261022210030_world.sql")
        self.assertIn("('frostmane_hollow', 822, '63137,63139', '', '63136,63132,36519,82', 'turtle-agent b8d3f62 dungeons/frostmane_hollow.json', "
                      "'plan v1, draft (not mapped, not live-checked)');", text)
        legs = re.findall(r"^\('frostmane_hollow', (\d+), '((?:[^']|'')+)', ''\)[,;]$", text, re.M)
        self.assertEqual([(int(n), leg.replace("''", "'")) for n, leg in legs],
                         [(0, "battlemaster_ubukaz"), (1, "kan'za_the_seer"), (2, "tan'sha_the_sleek"), (3, "hailar_the_frigid")])
        rows = [r for r in self.step_rows(text) if r["plan"] == "frostmane_hollow"]
        self.assertEqual(len(rows), 413)
        self.assertEqual({k: len([r for r in rows if r["kind"] == k]) for k in {r["kind"] for r in rows}}, {"move": 326, "pull": 83, "boss": 4})
        self.assertEqual({leg: len(leg_steps(rows, leg)) for leg in range(4)}, {0: 53, 1: 205, 2: 45, 3: 110})
        self.assertEqual({leg: len([r for r in rows if r["leg"] == str(leg) and r["kind"] == "pull"]) for leg in range(4)}, {0: 16, 1: 49, 2: 15, 3: 3})
        # One boss per leg, last in the leg, in the plan's order Ubukaz, Kan'za, Tan'sha, Hailar; Hailar's five Ritualists 36519 (each
        # linked to him in the core, channelling Frostmane Ritual on him) are no pull any more: the boss row lists 36519, as it lists Kan'za's two
        # Snowcallers 63137 and Handler Oboka 63132 (both linked to their boss); the lone linked units are not pulls.
        self.assertEqual([r["entry"] for r in rows if r["kind"] == "boss"], ["63131", "63129", "63133", "63130"])
        for leg in range(4):
            self.assertEqual(leg_steps(rows, leg)[-1]["kind"], "boss", leg)
            self.assertEqual(leg_steps(rows, leg)[0]["kind"], "move", leg)
            self.assertEqual(len([r for r in leg_steps(rows, leg) if r["comment"] == "to"]), 1, leg)
        self.assertEqual([r["entries"] for r in rows if r["kind"] == "boss"], ["", "63137", "63132", "36519"])
        self.assertEqual([r["entries"] for r in leg_steps(rows, 3) if r["kind"] == "pull"].count("36519"), 0)
        self.assertNotIn("63132", [r["entries"] for r in leg_steps(rows, 2) if r["kind"] == "pull"])
        for r in rows:
            self.assertEqual((r["item"], r["param"], r["timeout_s"]), ("0", "0", "0"), r)

    def test_frostmane_hollow_rows_pass_the_loader_checks(self):
        # Every step row goes through the loader's own code (DungeonPlanPolicy.h, compiled from tools/test_dungeon_plan_rows.cpp): a row
        # the loader refuses is skipped at startup and the run pauses before it.
        compiler = shutil.which("g++")
        if compiler is None:
            self.skipTest("g++ is not installed")
        _name, text = newest_plan_text("frostmane_hollow")
        rows = [r for r in self.step_rows(text) if r["plan"] == "frostmane_hollow"]
        lines = ["P\tfrostmane_hollow\t822"]
        for r in rows:
            lines.append("\t".join(["S", r["plan"], r["leg"], r["seq"], r["kind"], r["entry"], r["x"], r["y"], r["z"], r["radius"], r["item"],
                                    r["param"], r["flags"], r["timeout_s"], r["entries"]]))
        with tempfile.TemporaryDirectory() as tmp:
            binary = os.path.join(tmp, "rows")
            build = subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", os.path.join(ROOT, "tools/test_dungeon_plan_rows.cpp"), "-o", binary],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([binary], input="\n".join(lines) + "\n", capture_output=True, text=True)
        self.assertEqual(run.stdout.strip().splitlines()[-1], "rows %d refused 0" % len(rows), run.stdout)
        self.assertEqual(run.returncode, 0, run.stdout)

    def test_exact_flag_only_on_a_move_before_its_direct_step(self):
        # flags 64 (kPlanFlagExact, DungeonPlanPolicy.h): the loader refuses it on any kind but move, and it marks the launch
        # point of the move_direct right after it in the same leg (the run leader sends that hop only after the exact proof).
        for name, text in migrations():
            rows = list(self.step_rows(text))
            for i, row in enumerate(rows):
                if int(row["flags"]) & 64 == 0:
                    continue
                self.assertEqual(row["kind"], "move", "%s: the exact flag on a %s row" % (name, row["kind"]))
                self.assertLess(i + 1, len(rows), "%s: an exact move is the last row" % name)
                after = rows[i + 1]
                self.assertEqual((after["plan"], after["leg"], after["kind"]), (row["plan"], row["leg"], "move_hop_exact"),
                                 "%s: the exact move %s/%s/%s is not followed by its move_hop_exact" % (name, row["plan"], row["leg"], row["seq"]))

    def test_hop_exact_follows_its_exact_move(self):
        # move_hop_exact (DungeonPlanPolicy.h kPlanKindMoveHopExact) is the hop launched from the flag 64 move right before it in
        # its leg; a module without exact waypoints skips the unknown kind (no direct order for the hop).
        for name, text in migrations():
            rows = list(self.step_rows(text))
            for i, row in enumerate(rows):
                if row["kind"] != "move_hop_exact":
                    continue
                self.assertGreater(i, 0, "%s: a move_hop_exact is the first row" % name)
                before = rows[i - 1]
                self.assertEqual((before["plan"], before["leg"], before["kind"]), (row["plan"], row["leg"], "move"), name)
                self.assertTrue(int(before["flags"]) & 64, "%s: the row before the move_hop_exact %s/%s/%s is not an exact move" %
                                (name, row["plan"], row["leg"], row["seq"]))

    def test_inserted_kinds_fit_the_column(self):
        # ai_dungeon_step.kind is a varchar(16) (20261014150000): a longer kind fails the INSERT in strict mode, or is cut.
        for name, text in migrations():
            for row in self.step_rows(text):
                self.assertLessEqual(len(row["kind"]), 16, "%s: kind %r is longer than the column" % (name, row["kind"]))
        with open(os.path.join(ROOT, "ai/playerbot/DungeonPlanPolicy.h"), encoding="utf-8") as handle:
            policy = handle.read()
        for kind in re.findall(r'\{"(\w+)", PlanStepKind::', policy) + re.findall(r'kPlanKind\w+ = "(\w+)"', policy):
            self.assertLessEqual(len(kind), 16, "kind %r is longer than ai_dungeon_step.kind" % kind)

    def test_wailing_caverns_serpentis_foot_is_exact(self):
        # The newest migration that inserts the wailing_caverns steps carries the live-proven hop (tests/wc-hop-probe-2): the foot
        # -293.3 0.5 -58 as an exact move (flags 64), then the move_direct to the landing -287.5 5 -63.6 (pjerra/TortoiseBots
        # feat/exact-waypoint). A re-export from an exporter that writes no exact flag would launch the hop from 3-6 yd short.
        newest = None
        for name, text in migrations():
            if re.search(r"INSERT INTO `ai_dungeon_step`.*?\('wailing_caverns',", text, re.S):
                newest = (name, text)
        self.assertIsNotNone(newest)
        name, text = newest
        rows = [r for r in self.step_rows(text) if r["plan"] == "wailing_caverns"]
        exact = [i for i, r in enumerate(rows) if int(r["flags"]) & 64]
        self.assertEqual(len(exact), 1, "%s: the wailing_caverns rows need exactly one exact move" % name)
        foot, hop = rows[exact[0]], rows[exact[0] + 1]
        self.assertEqual((foot["kind"], foot["x"], foot["y"], foot["z"]), ("move", "-293.3", "0.5", "-58"), name)
        self.assertEqual((hop["kind"], hop["x"], hop["y"], hop["z"]), ("move_hop_exact", "-287.5", "5", "-63.6"), name)

    def test_wailing_caverns_kresh_is_a_boss_leg(self):
        # 20261028180000 (turtle-agent 8a7f7d0, 2026-10-28) re-exports the whole wailing_caverns plan on top of 20261025002700. Kresh 3653
        # (faction 7: neutral to every player, so only a step that makes the party attack him fights him) is no longer a pull member: he
        # is the boss of a new leg `kresh` between cobrahn_a and cobrahn_b, at the middle of his 525.9 yd river beat (his third waypoint
        # -3.64081 229.268), where he passes within the row's 30 yd radius at most 96 s apart. The row's timeout_s is 300 (the plan's
        # wait_s): DungeonRunPolicy.h BossAwayWaitMs makes it the away-boss wait, so the 210 s round is not missed (the 60 s default
        # caught him 10% of the time and said 'pack cleared' in 1 s otherwise). The six river groups that followed his old pull are
        # cobrahn_b's now, so no leg pulls him. Lady Anacondra stays a member of her four spot packs (a boss row per spot would pause the
        # run 'boss not found' three times in four).
        name, text = newest_plan_text("wailing_caverns")
        self.assertEqual(name, "20261028180000_world.sql")
        self.assertIn("'turtle-agent 8a7f7d0 dungeons/wailing_caverns.json'", text)
        rows = [r for r in self.step_rows(text) if r["plan"] == "wailing_caverns"]
        kinds = {k: len([r for r in rows if r["kind"] == k]) for k in KINDS}
        self.assertEqual((len(rows), kinds["move"], kinds["move_hop_exact"], kinds["pull"], kinds["boss"]), (323, 214, 1, 100, 8), name)
        self.assertEqual(len([r for r in rows if r["kind"] == "pull" and "3653" in r["entries"].split(",")]), 0, name)
        kresh = [r for r in rows if r["leg"] == "3"]
        self.assertEqual([(r["seq"], r["kind"], r["x"], r["y"], r["z"], r["radius"], r["timeout_s"], r["entries"]) for r in kresh],
                         [("0", "move", "4", "254", "-105.5", "0", "0", ""),
                          ("1", "boss", "-3.64081", "229.268", "-106.189", "0", "300", "")], name)
        self.assertEqual(len([r for r in rows if r["kind"] == "pull" and "3671" in r["entries"].split(",")]), 4, name)
        self.assertEqual([r["entry"] for r in rows if r["kind"] == "boss"], ["3653", "3669", "3670", "61968", "3674", "61965", "3673", "5775"], name)
        self.assertEqual(len([r for r in rows if r["kind"] == "boss" and r["entry"] in ("3671", "3654")]), 0, name)

    def test_wailing_caverns_vangros_and_zandara_legs(self):
        # The two Turtle-added bosses (20261025002700, turtle-agent 16a864d): Vangros 61965 in the west wing and Zandara Windhoof 61968 in
        # the east spur. Both are always spawned (one creature row each on map 43, 7-day respawn, movement_type 0, faction 17/130 = hostile
        # to every player) and their ai_dungeon_boss rows already exist (20261021114931). The legs are the rows' proof: leg 9 zandara sits
        # between pythas_c and skum (the spur is a dead end off the cave mouth corridor), leg 12 vangros between west_column and serpentis
        # (the wing is reached from the west column's north loop only); the later legs keep their rows and close up to 10/11/13/14 (the
        # kresh leg of 20261028180000 is leg 3, before them).
        name, text = newest_plan_text("wailing_caverns")
        self.assertEqual(name, "20261028180000_world.sql")
        self.assertIn("('wailing_caverns', 43, '3840', '', '3671,61966', 'turtle-agent 8a7f7d0 dungeons/wailing_caverns.json', 'plan v1, mapped (not live-checked)');", text)
        legs = leg_rows(text, "wailing_caverns")
        self.assertEqual(len(legs), 15)
        self.assertEqual([legs[i][0] for i in (8, 9, 10, 11, 12, 13, 14)],
                         ["pythas_c", "zandara", "skum", "west_column", "vangros", "serpentis", "verdan"])
        rows = [r for r in self.step_rows(text) if r["plan"] == "wailing_caverns"]
        z = [r for r in rows if r["leg"] == "9"]
        self.assertEqual(len(z), 22)
        self.assertEqual([(r["seq"], r["kind"], r["x"], r["y"], r["z"], r["radius"], r["entries"]) for r in z if r["kind"] in ("pull", "boss")],
                         [("13", "pull", "-3.63", "-57.53", "-66.28", "10", "3840,5761"),
                          ("15", "pull", "23.14", "-29.18", "-64.88", "15", "61967,61966"),
                          ("17", "pull", "66.34", "-30.52", "-66.55", "6", "61966"),
                          ("19", "pull", "69.51", "1.15", "-66.6", "12", "61966"),
                          ("21", "boss", "81.88", "27.03", "-66.84", "0", "61966,61967")], name)
        self.assertEqual((z[12]["kind"], z[12]["x"], z[12]["y"], z[12]["z"]), ("move", "-12", "-92", "-64.6"), name)
        self.assertEqual((z[20]["comment"], z[20]["x"], z[20]["y"], z[20]["z"]), ("boss approach", "67", "2", "-66.5"), name)
        v = [r for r in rows if r["leg"] == "12"]
        self.assertEqual(len(v), 29)
        self.assertEqual([(r["seq"], r["kind"], r["x"], r["y"], r["z"], r["radius"], r["entries"]) for r in v if r["kind"] in ("pull", "boss")],
                         [("1", "pull", "-380.6", "-99.2", "-64.51", "10", "5056,3640"),
                          ("3", "pull", "-388", "-65.4", "-65.13", "6", "3640,5761"),
                          ("17", "pull", "-531.6", "-53.7", "-68.4", "6", "5761"),
                          ("19", "pull", "-527.44", "-84.56", "-67.35", "16", "5055,61964,5761"),
                          ("21", "pull", "-524.45", "-119.2", "-71.69", "8", "3641"),
                          ("23", "pull", "-525.9", "-140.4", "-66.7", "6", "61964"),
                          ("24", "pull", "-498.5", "-155.25", "-66.15", "10", "5761,61964"),
                          ("26", "pull", "-468.44", "-177.98", "-65", "19", "5055,61964"),
                          ("28", "boss", "-419.6", "-218", "-65.6", "0", "5053,5056,5756")], name)
        self.assertEqual((v[0]["kind"], v[0]["x"], v[0]["y"], v[0]["z"]), ("move", "-355.5", "-80.8", "-65.3"), name)
        self.assertEqual((v[27]["comment"], v[27]["x"], v[27]["y"], v[27]["z"]), ("boss approach", "-455", "-190", "-65.5"), name)

    def test_gossip_rows_fit_the_loader(self):
        # NormalizePlanStep (DungeonPlanPolicy.h): a gossip row needs its NPC entry, an option of 1-15 (the number `.bot cmd gossip` takes,
        # counted from 1: it refuses 0) and no flag but kPlanFlagDeadNpcIsDone (512; 20261022210000 sets it on Bly's row only, Weegli's
        # End Door needs him alive); the timeout is at most 600. A row the loader refuses is skipped with a log line and the
        # run then pauses at the step before it, so a migration must never carry one.
        seen = 0
        for name, text in migrations():
            for row in self.step_rows(text):
                if row["kind"] != "gossip":
                    continue
                seen += 1
                where = "%s: gossip %s/%s/%s" % (name, row["plan"], row["leg"], row["seq"])
                self.assertGreater(int(row["entry"]), 0, where + ": no NPC entry")
                self.assertTrue(1 <= int(row["param"]) <= 15, where + ": the option is outside 1-15 (counted from 1)")
                flags = int(row["flags"])
                self.assertIn(flags, (0, 512), where + ": a gossip carries no flag but 512 (dead NPC is done)")
                if flags == 512:
                    self.assertEqual(row["entry"], "7604", where + ": only Sergeant Bly's gossip counts a dead NPC as done")
                self.assertLessEqual(int(row["timeout_s"]), 600, where)
        self.assertGreaterEqual(seen, 1)

    def test_zulfarrak_end_door_is_a_hold_a_gossip_and_a_door_wait(self):
        # Weegli Blastfuse (7607) blows the End Door (146084) only once the pyramid phase (instance data 1) reads 8 (PYRAMID_KILLED_ALL_TROLLS,
        # tortoise-wow zulfarrak.h:40) and his menu then offers its one line (zulfarrak.cpp:497: index 0, so `cmd gossip` option 1).
        name, _text, zf = newest_zulfarrak_rows()
        rows = [r for r in zf if r["leg"] == "15"]
        self.assertEqual([r["kind"] for r in rows], ["move", "hold", "gossip", "wait_object"])
        hold, gossip, door = rows[1], rows[2], rows[3]
        # flag 8 alone: the phase is a script value, not an encounter state (no 32), and param 8 is above the encounter states 0-4
        self.assertEqual((hold["entry"], hold["param"], hold["flags"], hold["timeout_s"]), ("1", "8", "8", "600"))
        self.assertEqual((gossip["entry"], gossip["param"], gossip["flags"], gossip["timeout_s"]), ("7607", "1", "0", "0"))
        self.assertEqual((gossip["x"], gossip["y"], gossip["z"]), ("1877.52", "1199.63", "8.87"))
        # the door wait: object 146084 in state 2 (active_alt): Weegli's blast opens the door with the alternative state
        # (zulfarrak.cpp:423 DoUseDoorOrButton(EndDoorGUID, 0, true)), and wait_object compares exactly: the r8 run paused twice at
        # a state-1 wait on an open door. The order of the rows is the point.
        self.assertEqual((door["entry"], door["param"]), ("146084", "2"))
        # Sergeant Bly (leg 16): his gossip, then a hold done by its units (no done flag) with Bly first, so he counts while still
        # friendly; a boss row would leave the friendly Bly alone at once (DungeonRunPolicy.h FightStep).
        bly = [r for r in zf if r["leg"] == "16"]
        self.assertEqual([r["kind"] for r in bly], ["move", "gossip", "hold"], name)
        # flags 512 = kPlanFlagDeadNpcIsDone (20261022210000): his crew dies in the pyramid event's troll waves in some runs (r12, r15-19z33)
        # and a dead Bly then counts the gossip as done instead of pausing the run; Weegli's gossip (leg 15, above) keeps flags 0.
        self.assertEqual((bly[1]["entry"], bly[1]["param"], bly[1]["flags"]), ("7604", "1", "512"), name)
        hold = bly[2]
        self.assertEqual((hold["entry"], hold["param"], hold["flags"], hold["radius"], hold["timeout_s"], hold["entries"]),
                         ("0", "0", "0", "25", "300", "7604,7608,7606,7605"), name)
        self.assertEqual((hold["x"], hold["y"], hold["z"]), ("1884", "1212", "9"), name)
        # the Ukorz leg follows with the plain kinds, after Bly
        ukorz = [r for r in zf if r["leg"] == "17"]
        self.assertEqual(max(int(r["leg"]) for r in zf), 17, name + ": Ukorz is the last leg")
        self.assertEqual([r["kind"] for r in ukorz], ["move", "move", "pull", "pull", "pull", "pull", "pull", "boss"])
        self.assertEqual((ukorz[-1]["entry"], ukorz[-1]["entries"]), ("7267", "7797"))

    def test_zulfarrak_dead_bly_migration_changes_one_flag(self):
        # 20261022210000 is 20261021174716 with ONE row changed: Sergeant Bly's gossip (leg 15 seq 1) carries flags 512
        # (kPlanFlagDeadNpcIsDone). Every other step row, the legs and the plan row (but its source text) are equal.
        texts = dict(migrations())
        old_name, new_name = "20261021174716_world.sql", "20261022210000_world.sql"
        self.assertIn(old_name, texts)
        self.assertIn(new_name, texts)
        old = [r for r in self.step_rows(texts[old_name]) if r["plan"] == "zulfarrak"]
        new = [r for r in self.step_rows(texts[new_name]) if r["plan"] == "zulfarrak"]
        self.assertEqual(len(old), len(new))
        changed = [(o, n) for o, n in zip(old, new) if o != n]
        self.assertEqual(len(changed), 1)
        o, n = changed[0]
        self.assertEqual((n["leg"], n["seq"], n["kind"], n["entry"]), ("15", "1", "gossip", "7604"))
        self.assertEqual((o["flags"], n["flags"]), ("0", "512"))
        self.assertEqual(dict(o, flags="512"), n)
        self.assertEqual(leg_rows(texts[old_name], "zulfarrak"), leg_rows(texts[new_name], "zulfarrak"))

    def test_gossip_kind_is_in_the_policy_tables(self):
        # The kind text the exporter writes is the one the loader parses and the status line prints.
        with open(os.path.join(ROOT, "ai/playerbot/DungeonPlanPolicy.h"), encoding="utf-8") as handle:
            policy = handle.read()
        self.assertIn('{"gossip", PlanStepKind::Gossip}', policy)
        self.assertIn('case PlanStepKind::Gossip: return "gossip";', policy)

    def test_hold_rows_follow_the_loader_rules(self):
        # NormalizePlanStep (DungeonPlanPolicy.h) for a hold: at most one done flag (8 data, 16 object); the encounter flag 32 only
        # with 8 and then a state 0-4; by object a state 0-2 and an entry; with neither flag, entries. A plain script value (flag 8
        # without 32: Zul'Farrak's pyramid phase) may be any number.
        for name, text in migrations():
            for row in self.step_rows(text):
                if row["kind"] != "hold":
                    continue
                flags, param, where = int(row["flags"]), int(row["param"]), "%s: hold %s/%s/%s" % (name, row["plan"], row["leg"], row["seq"])
                self.assertFalse(flags & 8 and flags & 16, where + " has both done flags")
                if flags & 32:
                    self.assertTrue(flags & 8, where + ": the encounter flag without the data flag")
                    self.assertLessEqual(param, 4, where + ": no encounter state")
                if flags & 16:
                    self.assertNotEqual(row["entry"], "0", where)
                    self.assertLessEqual(param, 2, where)
                if not flags & 24:
                    self.assertTrue(row["entries"], where + ": done by its units without entries")

    def test_zulfarrak_runs_the_stairs_event(self):
        # The newest migration that inserts the zulfarrak steps (20261021174713, the graves on 20261021115557, 20261021095914 before; turtle-agent plan-export of the plan with the stairs
        # holds, Weegli's door and Ukorz) carries Zum'rah, the Executioner's key to the cage, the three holds on the pyramid phase
        # (instance data 1: 3 = WAVE_1, 7 = WAVE_3 on the stairs top, 8 = PYRAMID_KILLED_ALL_TROLLS at the foot, script values with
        # flag 8 alone), Nekrum before Sezz'ziz at the end of leg 14 and Ukorz as the last row; a re-export from an exporter without hold values
        # would cut the plan at the stairs again (or write them as encounter states, which the loader refuses above 4).
        name, text, rows = newest_zulfarrak_rows()
        self.assertEqual([r["entry"] for r in rows if r["kind"] == "boss"], ["8127", "7272", "7795", "7273", "7271", "7274", "7275", "7267"], name)
        cage = [i for i, r in enumerate(rows) if r["kind"] == "open"]
        self.assertEqual(len(cage), 1, name)
        i = cage[0]
        self.assertEqual((rows[i]["entry"], rows[i]["item"]), ("141073", "8444"), name)
        loot = [r for r in rows[:i] if r["kind"] == "loot"]
        self.assertEqual((loot[-1]["entry"], loot[-1]["item"]), ("7274", "8444"), name + ": the key is looted before the cage")
        holds = [r for r in rows if r["kind"] == "hold"]
        self.assertEqual([(r["entry"], r["param"], r["flags"], r["timeout_s"]) for r in holds],
                         [("1", "3", "8", "90"), ("1", "7", "8", "600"), ("1", "8", "8", "600"), ("0", "0", "0", "300")], name)
        self.assertEqual(rows[i + 1]["kind"], "hold", name + ": the first hold right after the cage (the event's start)")
        for r in holds[:2]:
            self.assertEqual((r["x"], r["y"], r["z"], r["entries"]), ("1887", "1274", "42", "7787,7788,7789,8876,8877"), name)
        self.assertEqual((holds[2]["leg"], holds[2]["x"], holds[2]["y"], holds[2]["z"], holds[2]["entries"]),
                         ("15", "1877.5", "1199.6", "8.9", "7787,7788,7789,8876,8877"), name)
        leg14 = [r for r in rows if r["leg"] == "14"]
        self.assertEqual(leg14[-2]["entries"], "7796", name + ": Nekrum before Sezz'ziz")
        self.assertEqual(leg14[-1]["kind"], "wait_clear", name)
        self.assertEqual((rows[-1]["kind"], rows[-1]["leg"]), ("boss", "17"), name + ": Ukorz ends the plan")
        self.assertIn("area trigger 962", text, name + ": the header and UPDATE name the trigger Zum'rah needs")

    def test_zulfarrak_antusul_pulls_the_pair_before_the_camp_from_stands(self):
        # r2-19z19 step 26: with the camp before the pair and no stand, the run leader stopped 30 yd short of the camp centre, 15.4 yd from the
        # pair's Shadowcaster, and the walk began the fight. The pair (5648,7247) is pulled first, from the stand 1620 880; the camp
        # (5648,5650) after it, from 1630 878; the north pack last. Each pull is right behind its stand move.
        name, _text, rows = newest_zulfarrak_rows()
        leg = [r for r in rows if r["leg"] == "3"]
        pulls = [(i, r) for i, r in enumerate(leg) if r["kind"] == "pull"]
        order = [r["entries"] for _i, r in pulls]
        self.assertEqual(order[-3:], ["5648,7247", "5648,5650", "7246,5648,5649"], name + ": the pair, the camp, the north pack")
        for (i, r), stand in ((pulls[-3], ("1620", "880")), (pulls[-2], ("1630", "878"))):
            move = leg[i - 1]
            self.assertEqual((move["kind"], move["x"], move["y"]), ("move", stand[0], stand[1]), name + ": a stand before " + r["entries"])
        # the stands keep 20 yd from every later pack and 15 yd from the patrol path (the plan check of turtle-agent measures it); here the
        # pair's stand is west of 1625 and the camp's stand west of 1635, so neither is near the Shadowcaster camp at 1660 901
        for stand, limit in ((leg[pulls[-3][0] - 1], 1625), (leg[pulls[-2][0] - 1], 1635)):
            self.assertLess(float(stand["x"]), limit, name)
        self.assertEqual(max(int(r["leg"]) for r in rows), 17, name)

    def test_shipped_zulfarrak_has_the_gahzrilla_leg(self):
        # 2026-10-26: the audit counts Gahz'rilla 7273 (EVERY-BOSS-2026-10-26) and the leg was played live in zf-r5, so the shipped
        # plan carries the leg now. The Mallet is still the operator's (no bot can get it): the header says a GM `.additem 9240`
        # is needed before the run, else the run leader pauses at the use_on and `.bot run skip` passes the leg.
        name, text, rows = newest_zulfarrak_rows()
        self.assertIn("gahzrilla", [n for n, _c in leg_rows(text, "zulfarrak").values()], name)
        self.assertEqual([r["entry"] for r in rows if r["kind"] == "use_on"], ["141832"], name)
        self.assertIn("7273", [r["entry"] for r in rows if r["kind"] == "boss"], name)
        self.assertIn(".additem 9240", text, name)

    def test_zulfarrak_gahzrilla_opt_in_matches_the_shipped_plan(self):
        # tools/optional-plans/zulfarrak-gahzrilla.sql was the export of a93caba with --with-optional gahzrilla; 20261025002900
        # ports its rows into the shipped plan. The file is kept as provenance, so its step and leg rows must stay identical to
        # the shipped ones; only the plan row's `source` text names the newer revision (74bbea2), and the shipped file's own
        # UPDATE names the area trigger on the renumbered row (an UPDATE, not a step row).
        name, text, rows = newest_zulfarrak_rows()
        opt = OPT_IN.read_text()
        opt_rows = [r for r in self.step_rows(opt) if r["plan"] == "zulfarrak"]
        self.assertEqual(opt_rows, rows, name + ": the shipped steps are the opt-in export's rows")
        self.assertEqual(leg_rows(opt, "zulfarrak"), leg_rows(text, "zulfarrak"), name + ": the same legs")
        plan_row = lambda t: re.search(r"^\('zulfarrak', 209, .*\);$", t, re.M).group(0)
        shipped = plan_row(text)
        ported = plan_row(opt)
        self.assertEqual(re.sub(r"'turtle-agent \w+ ", "'turtle-agent ", shipped),
                         re.sub(r"'turtle-agent \w+ ", "'turtle-agent ", ported), name + ": only the source revision differs")
        self.assertIn("SUPERSEDED 2026-10-26", opt.split("DELETE FROM")[0])
        self.assertIn("NOT A MIGRATION", opt.split("DELETE FROM")[0])
        update = "UPDATE `ai_dungeon_plan` SET `cc_priority` = '7246,7247,5648' WHERE `plan` = 'zulfarrak';"
        self.assertIn(update, text)
        self.assertIn(update, opt)

    def test_zulfarrak_gahzrilla_rings_the_gong_with_the_mallet(self):
        # 20261025002900: the gahzrilla leg is in the shipped rows (turtle-agent plan-export --with-optional gahzrilla): the pool-ring
        # pulls, the stand 1652.84 1168.99 10.67, then ONE use_on of the gong 141832 at its point with the Mallet of Zul'Farrak 9240 (the
        # order `cmd use 9240 <carrier> entry:141832@1650.9,1171.9,10.9`; a plain open of the gong does nothing), then Gahz'rilla 7273
        # where his walk-out ends. The Mallet is GM-added to the lead bot before the run (no bot can get it); the header says so.
        # The boss rows: ai_dungeon_boss (209, 7273) and the two interruptible casts from core data (spell_template interruptFlags 15).
        name, text, rows = newest_zulfarrak_rows()
        legs = leg_rows(text, "zulfarrak")
        self.assertEqual(legs[9][0], "gahzrilla", name)
        self.assertEqual(max(int(r["leg"]) for r in rows), 17, name + ": Ukorz is leg 17 with the gahzrilla leg")
        leg = [r for r in rows if r["leg"] == "9"]
        uses = [i for i, r in enumerate(leg) if r["kind"] == "use_on"]
        self.assertEqual(len(uses), 1, name + ": the gong is rung once (a second ring summons a new Gahz'rilla that dies at once, an invisible corpse at the pit)")
        i = uses[0]
        use = leg[i]
        self.assertEqual((use["entry"], use["item"], use["x"], use["y"], use["z"], use["param"], use["flags"]),
                         ("141832", "9240", "1650.9", "1171.9", "10.9", "0", "0"), name)
        self.assertEqual((leg[i - 1]["kind"], leg[i - 1]["x"], leg[i - 1]["y"]), ("move", "1652.84", "1168.99"), name + ": the stand first")
        self.assertEqual((leg[i + 1]["kind"], leg[i + 1]["entry"], leg[i + 1]["x"]), ("boss", "7273", "1702.58"), name)
        self.assertEqual(sum(1 for r in leg if r["kind"] == "pull"), 8, name + ": the pool ring is cleared first")
        self.assertIn(".additem 9240", text, name + ": the header names the Mallet's GM setup")
        self.assertIn("7604", text.split("DELETE FROM")[0], name + ": the header names Bly's BotGossipNpcs entry")
        self.assertIn("REPLACE INTO `ai_dungeon_boss` (`map`, `entry`, `name`) VALUES\n(209, 7273, 'Gahz''rilla');", text, name)
        self.assertIn("(209, 7273, 'interrupt', 0, 11836,", text, name + ": Freeze Solid is interruptible")
        self.assertIn("(209, 7273, 'interrupt', 0, 11131,", text, name + ": Icicle is interruptible")

    def test_zulfarrak_graves_before_the_trigger(self):
        # Zum'rah's 31 Shallow Graves (128403) are opened one at a time before the trigger 962 (turtle-agent plan-export
        # --carrier-chests): each is a move to its stand (4 yd or less from the grave), the loot (gone-is-done flag 256, item 0)
        # and the sweep pull of 7286,7276 at the grave's point (radius 15). The walk keeps ZUMRAH_GRAVES' order. 128308 (the
        # script graves, not Zum'rah's) is never a row. The graves are read from the newest zulfarrak migration.
        name, _text, rows = newest_zulfarrak_rows()
        leg = next(r["leg"] for r in rows if r["kind"] == "boss" and r["entry"] == "7271")
        steps = [r for r in rows if r["leg"] == leg]
        self.assertEqual([int(r["seq"]) for r in steps], list(range(len(steps))), name + ": the zumrah seq is not 0..n-1")
        self.assertEqual([r for r in rows if r["entry"] == "128308"], [], name + ": a 128308 row (script graves, not Zum'rah's)")
        loots = [i for i, r in enumerate(steps) if r["kind"] == "loot"]
        self.assertEqual(len(loots), len(ZUMRAH_GRAVES), name + ": the number of grave loot rows")
        for i, (gx, gy) in zip(loots, ZUMRAH_GRAVES):
            where = "%s: grave at seq %s" % (name, steps[i]["seq"])
            self.assertEqual((steps[i]["entry"], steps[i]["flags"], steps[i]["item"]), ("128403", "256", "0"), where)
            self.assertLessEqual(math.hypot(float(steps[i]["x"]) - gx, float(steps[i]["y"]) - gy), 0.1, where + ": not the plan's point")
            move, pull = steps[i - 1], steps[i + 1]
            self.assertEqual((move["kind"], move["comment"]), ("move", "stand"), where + ": not preceded by its stand")
            self.assertLessEqual(math.hypot(float(move["x"]) - gx, float(move["y"]) - gy), 4, where + ": the stand is more than 4 yd off")
            self.assertEqual((pull["kind"], pull["entries"], pull["radius"]), ("pull", "7286,7276", "15"), where + ": not followed by the sweep")
            self.assertLessEqual(math.hypot(float(pull["x"]) - float(steps[i]["x"]), float(pull["y"]) - float(steps[i]["y"])), 0.1,
                                 where + ": the sweep is not at the grave")
        trigger = [i for i, r in enumerate(steps) if (r["kind"], r["x"], r["y"], r["z"]) == ("move", "1903", "1014", "11.8")]
        boss = [i for i, r in enumerate(steps) if r["kind"] == "boss"]
        self.assertEqual((len(trigger), len(boss)), (1, 1), name + ": the trigger 962 and the boss 7271 are each one row")
        self.assertLess(loots[-1] + 1, trigger[0], name + ": the last grave's sweep is not before the trigger")
        self.assertLess(trigger[0], boss[0], name + ": the trigger is not before the boss")
        self.assertEqual(boss[0], len(steps) - 1, name + ": the boss does not end the zumrah leg")

    def test_zulfarrak_velratha_approach_stays_off_the_island_shadowhunter(self):
        # turtle-agent#286: the lone Shadowhunter 81610 stands on a navmesh island at 1591 1047.7 (no path leaves it; a fight there
        # takes the tank onto the island, zf-r6-19z24). Pull group 4's approach points were 1584 1050 and 1603 1043, 7.4 and 12.9 yd
        # from it; they are 1594 1011, 1598 1028 and 1606 1040 now, and the leg's comment carries the plan's `leave` list as text
        # (the run leader does not read it yet).
        name, text, rows = newest_zulfarrak_rows()
        legs = leg_rows(text, "zulfarrak")
        leg = next(k for k, (leg_name, _c) in legs.items() if leg_name == "velratha")
        steps = leg_steps(rows, leg)
        approach = [(float(r["x"]), float(r["y"])) for r in steps if r["kind"] == "move" and r["comment"] == "pull group 4 approach"]
        self.assertEqual(approach, [(1594.0, 1011.0), (1598.0, 1028.0), (1606.0, 1040.0)], name)
        for old in ((1584.0, 1050.0), (1603.0, 1043.0)):
            self.assertEqual([r for r in steps if (float(r["x"]), float(r["y"])) == old], [], "%s: the old approach point %s is back" % (name, old))
        for r in steps:
            if r["kind"] == "move":
                self.assertGreaterEqual(dist(r, 1591.0, 1047.7), 15, "%s: velratha move %s is within 15 yd of the island Shadowhunter" % (name, r["seq"]))
        # the approach is right before its pull, which still fetches the Shadowhunters and the Witch Doctor of the group
        after = steps[[r["comment"] for r in steps].index("pull group 4")]
        self.assertEqual((after["kind"], after["x"], after["y"]), ("pull", "1583", "1086.76"), name)
        self.assertEqual(int(after["seq"]) - 1, int(next(r for r in steps if r["x"] == "1606")["seq"]), name + ": the last approach is not right before its pull")
        comment = legs[leg][1]
        self.assertIn("leave (not read by the run leader yet)", comment, name)
        self.assertIn("81610", comment, name)
        self.assertIn("1591 1047.7", comment, name)
        # the graves of 20261021115557 stay: 31 loots of 128403 with the gone-is-done flag, none of 128308
        graves = [r for r in rows if r["kind"] == "loot" and r["entry"] == "128403"]
        self.assertEqual(len(graves), 31, name)
        self.assertEqual({r["flags"] for r in graves}, {"256"}, name)

    def test_uldaman_pulls_the_keepers_neighbours_before_the_boss(self):
        # PLAN-ULDAMAN-GRIMLOK #2 (turtle-agent#287): the boss 7206 was pulled with the Stone Steward 4860 (a patrol that walks to 13.6 yd
        # from it), the Sculptors and the Rocksmasher of the galgann leg alive 20-35 yd north, and 5-6 mobs joined (r4 slot1, r6 slot5).
        # They are pulled first, each from an approach move, in the Keeper's own leg; nothing of them is left in the galgann leg.
        name, text, rows = newest_uldaman_rows()
        legs = leg_rows(text, "uldaman")
        keeper = next(k for k, (leg_name, _c) in legs.items() if leg_name == "ancient_stone_keeper")
        galgann = next(k for k, (leg_name, _c) in legs.items() if leg_name == "galgann")
        steps = leg_steps(rows, keeper)
        boss = [i for i, r in enumerate(steps) if r["kind"] == "boss" and r["entry"] == "7206"]
        self.assertEqual(boss, [len(steps) - 1], name + ": the Stone Keeper is the one boss row and ends its leg")
        for entries, x, y in (("4860", -39.6, 272.8), ("7012,7011", -43.03, 263.93), ("7011", -46.4, 202.9)):
            pulls = [i for i, r in enumerate(steps) if r["kind"] == "pull" and r["entries"] == entries and dist(r, x, y) < 0.1]
            self.assertEqual(len(pulls), 1, "%s: the pull of %s at %s %s is missing or doubled" % (name, entries, x, y))
            self.assertLess(pulls[0], boss[0], "%s: the pull of %s comes after the Stone Keeper" % (name, entries))
            self.assertEqual(steps[pulls[0] - 1]["kind"], "move", name + ": the pull of %s has no approach move" % entries)
            self.assertIn("approach", steps[pulls[0] - 1]["comment"], name)
        # they are the last three pulls of the leg, the Steward (the patrol's ground) first; the boss approach (-73.3 240.2) is the row before the boss
        self.assertEqual([r["entries"] for r in steps if r["kind"] == "pull"][-3:], ["4860", "7012,7011", "7011"], name)
        self.assertEqual((steps[-2]["kind"], steps[-2]["x"], steps[-2]["y"]), ("move", "-73.3", "240.2"), name)
        for r in rows:
            if r["leg"] == str(galgann) and r["kind"] == "pull":
                self.assertNotEqual(r["entries"], "4860", name + ": the Steward is pulled in the galgann leg again")
                self.assertGreater(dist(r, -43.03, 263.93), 1, name + ": the Sculptor pull is in the galgann leg again")

    def test_uldaman_scorpid_pull_is_approached_from_the_ramp_top(self):
        # r10 regression (PLAN-ULDAMAN-R10-REGRESSION.txt, F2): the approach of the ten scorpids (pull group 3) is the ramp top where r9 pulled
        # cleanly; from -17.5 292.5 the healer and the mage stood 27-30 yd behind the tank when the scorpids engaged at step 79.
        name, text, rows = newest_uldaman_rows()
        legs = leg_rows(text, "uldaman")
        galgann = leg_steps(rows, next(k for k, (leg_name, _c) in legs.items() if leg_name == "galgann"))
        pulls = [i for i, r in enumerate(galgann) if r["kind"] == "pull" and r["entries"] == "7405"]
        self.assertEqual(len(pulls), 1, name + ": the scorpid pull is missing or doubled")
        approach = galgann[pulls[0] - 1]
        self.assertEqual((approach["kind"], approach["comment"], approach["x"], approach["y"], approach["z"]),
                         ("move", "pull group 3 approach", "-20", "321", "-40"), name + ": the scorpid approach is not the ramp top")

    def test_uldaman_grimlok_corridor_is_pulled_one_group_at_a_time(self):
        # PLAN-ULDAMAN-GRIMLOK #2: Brawler 30105 (40.6 359.9) joined pull group 0 in 8 of 8 runs, so group 0 lists it (radius 16 at
        # 35.77 370.57: 11.7 yd to it, the Brawler at 55.4 379 stays out). The first groups of the keepers_door leg are pulled next
        # from stands at 28 360 and 33 354, the patrol and its trio are one group (radius 18), and the pair at 38-40 439-445 is pulled from 48 419.
        name, text, rows = newest_uldaman_rows()
        legs = leg_rows(text, "uldaman")
        by_name = {leg_name: k for k, (leg_name, _c) in legs.items()}
        grimlok = leg_steps(rows, by_name["grimlok"])
        pulls = [r for r in grimlok if r["kind"] == "pull"]
        first = pulls[0]
        self.assertEqual((first["comment"], first["radius"], first["entries"]), ("pull group 0", "16", "4853,4855"), name)
        self.assertLessEqual(dist(first, 40.6, 359.9), float(first["radius"]), name + ": the Brawler 30105 at 40.6 359.9 is outside group 0")
        self.assertGreater(dist(first, 55.4, 379.0), float(first["radius"]), name + ": the patrol-side Brawler at 55.4 379 is inside group 0")
        # the keepers_door packs now pulled from the grimlok leg, before the patrol and the east groups
        order = [(float(r["x"]), float(r["y"])) for r in pulls]
        def at(x, y):
            found = [i for i, (px, py) in enumerate(order) if math.hypot(px - x, py - y) < 0.1]
            self.assertEqual(len(found), 1, "%s: the grimlok leg has %d pulls at %s %s" % (name, len(found), x, y))
            return found[0]
        pair, brawlers, merged = at(49.1, 343.45), at(39.55, 327.5), at(58.63, 374.4)
        self.assertLess(0, pair, name)
        self.assertLess(pair, brawlers, name)
        self.assertLess(brawlers, merged, name + ": the keepers_door packs are not pulled before the patrol")
        for index, stand in ((pair, (28.0, 360.0)), (brawlers, (33.0, 354.0))):
            move = grimlok[grimlok.index(pulls[index]) - 1]
            self.assertEqual((move["kind"], float(move["x"]), float(move["y"])), ("move", stand[0], stand[1]), name)
        # the patrol and the trio are one group of radius 18 (pull groups 1 and 2 of the old order were never alone)
        self.assertEqual((pulls[merged]["radius"], pulls[merged]["entries"]), ("18", "4855,4853"), name)
        self.assertEqual([i for i, (px, py) in enumerate(order) if math.hypot(px - 57.9, py - 374.0) < 3 and i != merged], [], name + ": a second pull of the patrol")
        # the southern pair from 48 419, right before its pull, then Grimlok
        south = at(38.85, 442.0)
        move = grimlok[grimlok.index(pulls[south]) - 1]
        self.assertEqual((move["kind"], float(move["x"]), float(move["y"])), ("move", 48.0, 419.0), name)
        self.assertEqual(grimlok[-1]["kind"], "boss", name)
        self.assertEqual(grimlok[-1]["entry"], "4854", name)
        # none of the three packs is left to the keepers_door leg
        for r in rows:
            if r["leg"] == str(by_name["keepers_door"]) and r["kind"] == "pull":
                for x, y in ((49.1, 343.45), (40.6, 359.9), (39.55, 327.5)):
                    self.assertGreater(dist(r, x, y), 1, "%s: keepers_door pulls the pack at %s %s again" % (name, x, y))

    def test_uldaman_grimlok_patrol_is_pulled_from_the_lower_floor(self):
        # PLAN-ULDAMAN-GRIMLOK-ISLAND D1 (turtle-agent fix/uldaman-grimlok-pit): the tank shot pull group 3 from the crest above the 6x6
        # pit at 39.7-46.1 370.7-376.8 and fell in (r13 slot1, slot5). Two approach moves now come before the pull: 19.1 366.4 -40.6, then
        # 37.5 363.5 -42.5 on the lower floor, 21.4 yd from the nearest live Brawler (the 20 yd clearance rule of PR #281). The pull and the
        # step 96 approach (50.8 379.3 -38.6) after it keep their rows, one seq later than before the second move.
        name, text, rows = newest_uldaman_rows()
        legs = leg_rows(text, "uldaman")
        grimlok = leg_steps(rows, next(k for k, (leg_name, _c) in legs.items() if leg_name == "grimlok"))
        merged = next(i for i, r in enumerate(grimlok) if r["kind"] == "pull" and r["comment"] == "pull group 3")
        self.assertEqual([(r["kind"], float(r["x"]), float(r["y"]), float(r["z"]), r["comment"]) for r in grimlok[merged - 2:merged]],
                         [("move", 19.1, 366.4, -40.6, "pull group 3 approach"), ("move", 37.5, 363.5, -42.5, "pull group 3 approach")], name)
        self.assertEqual(int(grimlok[merged]["seq"]), int(grimlok[merged - 1]["seq"]) + 1, name)
        after = grimlok[merged + 1]
        self.assertEqual((after["kind"], float(after["x"]), float(after["y"]), float(after["z"]), after["comment"]),
                         ("move", 50.8, 379.3, -38.6, "pull group 4 approach"), name)
        self.assertEqual(int(grimlok[-1]["seq"]), len(grimlok) - 1 + int(grimlok[0]["seq"]), name + ": the leg's seq has a gap")

    def test_uldaman_descent_ends_out_of_the_door_squad(self):
        # r9-19z26: the descent leg ended at 141 290, 7 yd from the door squad; the tank engaged it on arrival, its fight moves took it
        # onto the raised rubble at 132-139 / 296-304 (mesh at z -23.8 no path leaves) and the run paused at step 127. The leg ends 20 yd
        # or more from every squad member now (the pull's radius 20.3 round its centre does not reach it) and 12 yd or more off the rubble.
        name, text, rows = newest_uldaman_rows()
        legs = leg_rows(text, "uldaman")
        descent = leg_steps(rows, next(k for k, (leg_name, _c) in legs.items() if leg_name == "descent"))
        end, squad = descent[1], descent[2]
        self.assertEqual((end["kind"], end["comment"], squad["kind"], squad["comment"]), ("move", "to", "pull", "pull group 0"), name)
        self.assertGreater(dist(end, float(squad["x"]), float(squad["y"])), float(squad["radius"]), name + ": the leg ends inside the squad's pull")
        for x, y in ((148.1, 287.9), (144.4, 299.0), (152.7, 288.4), (153.6, 298.3), (144.5, 306.5), (151.2, 303.8)):
            self.assertGreaterEqual(dist(end, x, y), 20, "%s: the leg ends %.1f yd from the squad member at %s %s" % (name, dist(end, x, y), x, y))
        ex, ey = float(end["x"]), float(end["y"])
        self.assertGreaterEqual(math.hypot(max(132 - ex, ex - 139, 0), max(296 - ey, ey - 304, 0)), 12, name + ": the leg ends by the rubble")

    def test_zulfarrak_and_uldaman_rows_pass_the_loader_checks(self):
        # Every step row of the newest zulfarrak and uldaman migrations goes through the loader's own code (DungeonPlanPolicy.h:
        # ParsePlanStepRowKind, ParsePlanEntries, DungeonPlanTable::AddStep with NormalizePlanStep and the (plan, leg, seq) key),
        # compiled from tools/test_dungeon_plan_rows.cpp: a row the loader refuses is skipped at startup and the run pauses before it.
        compiler = shutil.which("g++")
        if compiler is None:
            self.skipTest("g++ is not installed")
        lines, counts = [], {}
        for plan, rows in (("zulfarrak", newest_zulfarrak_rows()[2]), ("uldaman", newest_uldaman_rows()[2])):
            lines.append("P\t%s\t%d" % (plan, BOSS_PLANS.get(plan, 209)))
            counts[plan] = len(rows)
            for r in rows:
                lines.append("\t".join(["S", r["plan"], r["leg"], r["seq"], r["kind"], r["entry"], r["x"], r["y"], r["z"], r["radius"], r["item"],
                                        r["param"], r["flags"], r["timeout_s"], r["entries"]]))
        self.assertGreater(counts["zulfarrak"], 200)
        self.assertGreater(counts["uldaman"], 130)
        self.assertEqual(counts["zulfarrak"], 247, "the gahzrilla leg is in the shipped plan")
        with tempfile.TemporaryDirectory() as tmp:
            binary = os.path.join(tmp, "rows")
            build = subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", os.path.join(ROOT, "tools/test_dungeon_plan_rows.cpp"), "-o", binary],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([binary], input="\n".join(lines) + "\n", capture_output=True, text=True)
        self.assertEqual(run.stdout.strip().splitlines()[-1], "rows %d refused 0" % sum(counts.values()), run.stdout)
        self.assertEqual(run.returncode, 0, run.stdout)

    def test_scarlet_cathedral_rows_pass_the_loader_checks(self):
        # The row the whole fix rests on (the pair's hold, entry 1 param 4 flags 8) goes through the loader's own code with the
        # rest of the plan: NormalizePlanStep takes param 4 as a script value (flag 8 without 32), keeps radius 35 and 600 s,
        # and a refused row would leave the run pausing before Mograine and Whitemane.
        compiler = shutil.which("g++")
        if compiler is None:
            self.skipTest("g++ is not installed")
        _name, text = newest_plan_text("scarlet_cathedral")
        rows = [r for r in self.step_rows(text) if r["plan"] == "scarlet_cathedral"]
        lines = ["P\tscarlet_cathedral\t%d" % BOSS_PLANS["scarlet_cathedral"]]
        for r in rows:
            lines.append("\t".join(["S", r["plan"], r["leg"], r["seq"], r["kind"], r["entry"], r["x"], r["y"], r["z"], r["radius"], r["item"],
                                    r["param"], r["flags"], r["timeout_s"], r["entries"]]))
        with tempfile.TemporaryDirectory() as tmp:
            binary = os.path.join(tmp, "rows")
            build = subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", os.path.join(ROOT, "tools/test_dungeon_plan_rows.cpp"), "-o", binary],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([binary], input="\n".join(lines) + "\n", capture_output=True, text=True)
        self.assertEqual(run.stdout.strip().splitlines()[-1], "rows %d refused 0" % len(rows), run.stdout)

    def test_karazhan_crypt_rows_pass_the_loader_checks(self):
        # All 259 rows of the reordered export go through the loader's own code (DungeonPlanPolicy.h: ParsePlanStepRowKind,
        # ParsePlanEntries, DungeonPlanTable::AddStep with NormalizePlanStep and the (plan, leg, seq) key), compiled from
        # tools/test_dungeon_plan_rows.cpp: a row it refuses is skipped at startup and the run pauses before it. The rows the
        # dive adds are the ones a refusal would hurt most - a 56.8 yd move through an off-mesh link and four ambush pulls
        # (flag 1) whose stand is a move row, all of them inside the run leader's reach.
        compiler = shutil.which("g++")
        if compiler is None:
            self.skipTest("g++ is not installed")
        _name, text = newest_plan_text("karazhan_crypt")
        rows = [r for r in self.step_rows(text) if r["plan"] == "karazhan_crypt"]
        lines = ["P\tkarazhan_crypt\t%d" % BOSS_PLANS["karazhan_crypt"]]
        for r in rows:
            lines.append("\t".join(["S", r["plan"], r["leg"], r["seq"], r["kind"], r["entry"], r["x"], r["y"], r["z"], r["radius"], r["item"],
                                    r["param"], r["flags"], r["timeout_s"], r["entries"]]))
        with tempfile.TemporaryDirectory() as tmp:
            binary = os.path.join(tmp, "rows")
            build = subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", os.path.join(ROOT, "tools/test_dungeon_plan_rows.cpp"), "-o", binary],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([binary], input="\n".join(lines) + "\n", capture_output=True, text=True)
        self.assertEqual(run.stdout.strip().splitlines()[-1], "rows %d refused 0" % len(rows), run.stdout)

    def test_inserted_step_kinds_are_known(self):
        for name, text in migrations():
            for block in re.findall(r"INSERT INTO `ai_dungeon_step` \(([^)]*)\) VALUES(.*?);", text, re.S):
                cols = [c.strip(" `") for c in block[0].split(",")]
                kind_at = cols.index("kind")
                for row in re.findall(r"^\((.*)\)[,]?$", block[1], re.M):
                    values = [v.strip() for v in re.findall(r"'[^']*'|[^,]+", row)]
                    self.assertIn(values[kind_at].strip("'"), KINDS, "%s: unknown step kind in %s" % (name, row))

    def boss_rows(self, text):
        # The ai_dungeon_boss rows of one migration as (map, entry, name), in file order.
        rows = []
        for block in re.findall(r"(?:INSERT|REPLACE) INTO `ai_dungeon_boss` \(`map`, `entry`, `name`\) VALUES(.*?);\n", text, re.S):
            for row in re.findall(r"^\((\d+), (\d+), '((?:[^']|'')*)'\)[,]?$", block, re.M):
                rows.append((int(row[0]), int(row[1]), row[2].replace("''", "'")))
        return rows

    def test_boss_table_is_the_seed(self):
        # A map's seed is the last migration that seeds it: each such file DELETEs the map and REPLACEs its whole set
        # (test_boss_table_is_created_and_seeded_idempotently), so a re-export that re-seeds a map supersedes the
        # earlier file's rows. Within one file a (map, entry) twice is still an error.
        seed = {}
        for name, text in migrations():
            if "`ai_dungeon_boss`" not in text:
                continue
            rows = self.boss_rows(text)
            keys = [(m, e) for m, e, _n in rows]
            self.assertEqual(len(keys), len(set(keys)), "%s: a (map, entry) is seeded twice" % name)
            for m, e, n in rows:
                seed[(m, e)] = n
        keys = list(seed)
        self.assertEqual(seed, SEEDED_BOSSES)
        self.assertFalse({e for _m, e in keys} & NOT_BOSSES, "an add, an event unit or a rare is seeded as a boss")

    def test_boss_table_lists_every_plan_boss(self):
        # The newest migration that inserts a plan's boss steps holds its bosses (later ones replace single legs).
        seeded = set(SEEDED_BOSSES)
        for plan, map_id in BOSS_PLANS.items():
            newest = None
            for name, text in migrations():
                if re.search(r"^\('%s', \d+, \d+, 'boss'," % plan, text, re.M):
                    newest = text
            self.assertIsNotNone(newest, plan)
            entries = [int(r["entry"]) for r in self.step_rows(newest) if r["plan"] == plan and r["kind"] == "boss"]
            self.assertTrue(entries, plan)
            for entry in entries:
                self.assertIn((map_id, entry), seeded, "%s: boss step entry %d is not in ai_dungeon_boss" % (plan, entry))

    def test_loader_adds_the_boss_rows_before_the_seal(self):
        # AddBoss is refused after the seal, so a row read after it would be dropped without a word.
        with open(os.path.join(ROOT, "ai/playerbot/DungeonPlanMgr.cpp"), encoding="utf-8") as handle:
            loader = handle.read()
        add = loader.find("table.AddBoss(fields[0].GetUInt32(), fields[1].GetUInt32())")
        self.assertNotEqual(add, -1, "the loader does not add the ai_dungeon_boss rows")
        self.assertLess(add, loader.index("table.Seal();"))

    def test_boss_table_is_created_and_seeded_idempotently(self):
        for name, text in migrations():
            if "`ai_dungeon_boss`" not in text:
                continue
            self.assertNotRegex(text, r"CREATE TABLE `ai_dungeon_boss`", name)  # only IF NOT EXISTS
            self.assertNotRegex(text, r"(?m)^INSERT INTO `ai_dungeon_boss`", name)  # REPLACE, so a re-run keeps one row
            # The DELETE before the rows is scoped to exactly the maps the file seeds: a seeded map it leaves out keeps a
            # row a later file drops; a map it names beyond them loses rows it does not put back. A map shared by several
            # plans (189, the four SM wings) scopes the DELETE to its own boss entries too.
            seeded = {int(m) for m in re.findall(r"(?m)^\((\d+), \d+, '", text)}
            seeded_entries = {(int(m), int(e)) for m, e in re.findall(r"(?m)^\((\d+), (\d+), '", text)}
            scoped = re.findall(r"DELETE FROM `ai_dungeon_boss` WHERE `map` IN \(([\d, ]+)\)(?: AND `entry` IN \(([\d, ]+)\))?;", text)
            self.assertEqual(len(scoped), 1, name)
            if scoped[0][1]:
                self.assertEqual({int(e) for e in scoped[0][1].split(",")}, {e for _m, e in seeded_entries}, name)
            self.assertEqual({int(m) for m in scoped[0][0].split(",")}, seeded, name)

    def test_boss_tactic_inserts_delete_their_own_keys_first(self):
        # ai_boss_tactic's key is (map, boss_entry, kind, target_entry, spell_id): a migration that re-INSERTs a
        # tactic row an earlier migration already wrote, without DELETEing it, fails on a boot that runs both
        # (20261024152000 re-INSERTed 20261024150200's (329, 10440) kill_first row). The first migration that
        # INSERTs a (map, boss_entry) key needs no DELETE (fresh table); every later migration that INSERTs the
        # same key first DELETEs it: a whole-map DELETE, or a DELETE scoped to its own boss entries (the shape
        # the other plan migrations use for their boss rows).
        seen = set()
        for name, text in migrations():
            if "INSERT INTO `ai_boss_tactic`" not in text:
                continue
            inserted = set()
            for block in re.findall(r"INSERT INTO `ai_boss_tactic` \(([^)]*)\) VALUES(.*?);\n", text, re.S):
                cols = [c.strip(" `") for c in block[0].split(",")]
                map_at, entry_at = cols.index("map"), cols.index("boss_entry")
                for row in re.findall(r"(?m)^\((\d+), (\d+), '", block[1]):
                    inserted.add((row[0], row[1]))
            whole = set(re.findall(r"DELETE FROM `ai_boss_tactic` WHERE `map` = (\d+);", text))
            scoped = {}
            for map_id, entries in re.findall(r"DELETE FROM `ai_boss_tactic` WHERE `map` = (\d+) AND `boss_entry` IN \(([\d, ]+)\);", text):
                scoped.setdefault(map_id, set()).update(e.strip() for e in entries.split(","))
            missing = sorted((m, e) for m, e in (inserted & seen) if not (m in whole or e in scoped.get(m, set())))
            self.assertFalse(missing, "%s: tactic INSERTs with no DELETE of the same (map, boss_entry): %s" % (name, missing))
            seen |= inserted


    def test_scholomance_is_the_census_fixed_draft(self):
        # 20261025002400 (turtle-agent 1a544f5, 2026-10-26): the Scholomance draft with the census fixes of
        # PREP-SCHOLOMANCE-2026-10-24.md, leg 0's route clear after the r28c slotC first run, the step-11 reorder after the
        # r29 slot8 run (RUN-LESSONS.md), the hall-stand fix after the r30 slotC run (spec scholo-leg0-kill-order.md section 7),
        # the Vectus fix after the r34 slotA run and the Vectus-room clear after the r35 slot8b run (spec
        # scholo-vectus-fight.md): the r35 slot8b FULL run chained the Marduk pull into Vectus and all 32 students and wiped
        # 12 times, so leg vectus clears the 32 students 10475 first in ten pulls (groups 18-27) from stands 26+ yd from
        # every later student and 31+ yd from Vectus before the boss row (Marduk 10433 first, then Vectus):
        # the landing-to-Malicia walk is 554 yd and passes 40 census packs within 30 yd, so leg 0 is those packs with their
        # approach rows instead of one move row (45 pulls since the proof20d2 splits, see the pin after this one). The r30 run
        # wiped on the step-16 stands (215 22, 218 30, 220 24: 11-20 yd from
        # the unpulled 48845 pack and the 91341/91350 pair), so the 48849 pack is pulled from the stairs (224.2 48.1), the lone
        # 10471 and the 48844 pack from the stair foot (229.6 45.9), the southern 48841 pair from 220 38. No row hand-set, and the
        # boss rows of map 289 scoped to this plan's ten entries. 10 legs, 591 steps (167 pulls, 10 bosses, 414 moves), bosses in
        # leg order. The plan row carries cc_priority, the undead no_cc is not a column and kill_first; the 55 Rats 4075 and the
        # Black Rat 2110 are out of every pull, as are the disabled Kormok 16118 and his trigger 16047. Every pull is at most six
        # members and its radius (5-13.1 yd) covers its own positions; Alexei's linked Risen Guards 10489 and Rattlegore's linked
        # Risen Construct 10488 are the boss rows' first.
        name, text = newest_plan_text("scholomance")
        self.assertEqual(name, "20261025003000_world.sql")
        self.assertIn("('scholomance', 289, '10470,10472,10476,11257,10469,10475', '', '10471,11582,10477', 'turtle-agent 15e042b", text)
        legs = leg_rows(text, "scholomance")
        self.assertEqual([legs[i][0] for i in range(10)],
                         ["instructor_malicia", "the_ravenian", "lorekeeper_polkelt", "lady_illucia_barov",
                          "jandice_barov", "rattlegore", "vectus", "ras_frostwhisper",
                          "doctor_theolen_krastinov", "lord_alexei_barov"])
        rows = [r for r in self.step_rows(text) if r["plan"] == "scholomance"]
        self.assertEqual(len(rows), 589)
        self.assertEqual([len([r for r in leg_steps(rows, leg) if r["kind"] == "pull"]) for leg in range(10)],
                         [45, 7, 4, 14, 36, 25, 26, 3, 2, 3])
        self.assertEqual([r["entry"] for r in rows if r["kind"] == "boss"],
                         ["10505", "10507", "10901", "10502", "10503", "11622", "10432", "10508", "11261", "10504"])
        self.assertEqual({r["kind"] for r in rows}, {"move", "pull", "boss"})
        for leg in range(10):
            self.assertEqual(leg_steps(rows, leg)[-1]["kind"], "boss", leg)
        self.assertEqual(leg_steps(rows, 0)[0]["kind"], "move")
        self.assertEqual([r["entries"] for r in rows if r["kind"] == "boss"],
                         ["", "", "", "", "", "10488", "10433,10475", "", "", "10489"])
        pulls = [r for r in rows if r["kind"] == "pull"]
        self.assertEqual(len(pulls), 165)
        # Leg 0 is the whole route: every pull is preceded by its approach move row, and the pulls are the stair guards, the
        # 115.8 hall, the 109.7 ring, the 104.8 hall and Malicia's room, in that order.
        leg0 = leg_steps(rows, 0)
        self.assertEqual(len(leg0), 97)
        self.assertEqual([r["kind"] for r in leg0].count("move"), 51)
        # 50 approach rows for 45 pulls: the new ramp stands split five approaches into hops (the exporter cuts an
        # approach longer than its hop length).
        self.assertEqual(len([r for r in leg0 if r["kind"] == "move" and r["comment"].endswith(" approach")]), 50)
        pulls0 = [r for r in leg0 if r["kind"] == "pull"]
        self.assertEqual(len(pulls0), 45)
        for i, r in enumerate(leg0):
            if r["kind"] == "pull":
                self.assertEqual(leg0[i - 1]["kind"], "move")
                self.assertTrue(leg0[i - 1]["comment"].endswith(" approach"), leg0[i - 1])
        self.assertEqual(leg0[0]["comment"], "to")
        self.assertEqual((leg0[0]["x"], leg0[0]["y"], leg0[0]["z"]), ("190.819", "126.329", "137.227"))
        self.assertEqual(leg0[-1]["kind"], "boss")
        self.assertEqual(pulls0[0]["entries"], "10489")
        # The z 115.8 hall cluster: the 48849 pack (four single pulls since proof20d2), the lone 10471, the 48844 pack and
        # the southern 48841 pair come in that order.
        entries0 = [r["entries"] for r in pulls0[:11]]
        self.assertEqual(entries0[4:11],
                         ["10471", "10470", "10471", "10499", "10471", "10470,10471", "10471,10470"])
        # The r34 Vectus fix: the boss row walks to its stand (128 105 102.4, 16.7 yd from Vectus, 25.1 yd from Marduk)
        # before the boss, and the ras leg leaves the room north (via 124 116, 120 115, 110 110, all 25.9+ yd from Vectus
        # and 40 yd from the nearest hostile-233 pack) instead of the through-room path at 9.2 yd from him. Legs 0-5, 8
        # and 9 are byte-identical to the 150800 rows. The r35 slot8b Vectus-room clear: leg 6 rows 19-38 alternate the
        # ten approach moves and the ten student pulls (the exporter lists each entry once, so every pull row's entries
        # is the single 10475; the group sizes [3,5,3,3,4,3,5,1,3,2] show in the pull radii [5.0,8.8,5.0,5.0,5.7,5.0,7.4,
        # 5.0,5.0,5.0]), then the boss approach and the boss.
        leg6 = leg_steps(rows, 6)
        self.assertEqual(len(leg6), 39)
        student_pulls = [i for i, r in enumerate(leg6) if r["kind"] == "pull" and r["entries"] == "10475"]
        self.assertEqual(len(student_pulls), 8)
        self.assertEqual([float(leg6[i]["radius"]) for i in student_pulls],
                         [5.0, 5.0, 5.0, 5.7, 5.0, 7.4, 5.0, 5.0])
        for i in student_pulls:
            self.assertEqual(leg6[i - 1]["kind"], "move")
            self.assertTrue(leg6[i - 1]["comment"].endswith(" approach"))
        # The first and the last student pull carry a second approach row: the walk back's staging points 174 70 (42.2 yd
        # from Vectus) and 95 88 (50.2 yd), each outside the boss row's 30 + 10 yd reach.
        self.assertEqual((leg6[19]["x"], leg6[19]["y"], leg6[19]["z"]), ("174", "70", "104.85"))
        self.assertEqual((leg6[20]["x"], leg6[20]["y"], leg6[20]["z"]), ("165", "74", "102.96"))
        self.assertEqual((leg6[34]["x"], leg6[34]["y"], leg6[34]["z"]), ("95", "88", "97.686"))
        self.assertEqual((leg6[35]["x"], leg6[35]["y"], leg6[35]["z"]), ("113", "92", "99.37"))
        self.assertTrue(leg6[19]["comment"].endswith(" approach") and leg6[34]["comment"].endswith(" approach"))
        self.assertEqual(leg6[-2]["kind"], "move")
        self.assertEqual(leg6[-2]["comment"], "boss approach")
        self.assertEqual((leg6[-2]["x"], leg6[-2]["y"], leg6[-2]["z"]), ("128", "105", "102.4"))
        self.assertEqual(leg6[-1]["kind"], "boss")
        self.assertEqual(leg6[-1]["entry"], "10432")
        self.assertEqual(leg6[-1]["entries"], "10433,10475")
        leg7 = leg_steps(rows, 7)
        self.assertEqual(len(leg7), 15)
        self.assertEqual([(r["x"], r["y"], r["z"]) for r in leg7[:3]],
                         [("124", "116", "101.9"), ("120", "115", "101.1"), ("110", "110", "99.5")])
        self.assertTrue(all(r["comment"] == "via" for r in leg7[:3]))
        self.assertEqual(leg7[10]["kind"], "move")
        self.assertEqual(leg7[10]["comment"], "to")
        self.assertFalse([r["entries"] for r in pulls
                          if {"2110", "4075", "16047", "16118"} & set(r["entries"].split(","))])
        self.assertLessEqual(max(len(r["entries"].split(",")) for r in pulls), 6)
        self.assertEqual((min(float(r["radius"]) for r in pulls), max(float(r["radius"]) for r in pulls)), (5.0, 13.1))
        self.assertEqual(re.findall(r"(?m)^DELETE FROM `ai_dungeon_step` WHERE `plan` = '(\w+)';", text), ["scholomance"])
        self.assertIn("DELETE FROM `ai_dungeon_boss` WHERE `map` IN (289) AND `entry` IN (10505,10507,10901,10502,10503,11622,10432,10508,11261,10504);", text)
        self.assertEqual(len(re.findall(r"(?m)^\(289, \d+, '", text)), 10)

    # Spec item-leg-skip F22-24: the ai_dungeon_leg_need table and its rows.
    EXPECTED_LEG_NEEDS = {
        # plan: {(leg, item): (mode, pick, object)}; 1 optional, 2 door.
        "zulfarrak": {(9, 9240): (1, 0, 0)},
        # Review round 2 (Opus 3): one row per door, on the first leg whose route crosses it (DOOR_SPAWNS below).
        "blackrock_depths": {(1, 11000): (2, 250, 170560), (4, 11000): (2, 250, 170559), (8, 11000): (2, 250, 170558)},
        "scarlet_cathedral": {(0, 7146): (2, 175, 104591)},
        "scarlet_armory": {(1, 7146): (2, 175, 101854)},
        "stratholme_undead": {(1, 12382): (2, 300, 175357)},
        "scholomance": {(1, 13873): (2, 0, 175167)},
        "dire_maul_west": {(2, 18249): (2, 300, 300601)},
        "dire_maul_north": {(2, 18266): (2, 0, 177219), (3, 18268): (2, 200, 177217)},
        "gnomeregan": {(2, 6893): (2, 150, 90858)},
        "stormwrought_ruins": {(4, 41913): (2, 185, 2020233)},
        "dragonmaw_retreat": {(3, 41876): (2, 160, 2020232)},
    }

    def need_rows(self, text):
        for block in re.findall(r"INSERT INTO `ai_dungeon_leg_need` \(([^)]*)\) VALUES(.*?);", text, re.S):
            cols = [c.strip(" `") for c in block[0].split(",")]
            for row in re.findall(r"(?m)^\((.*)\)[,;]?\s*$", block[1]):
                values = [v.strip() for v in re.findall(r"\s*('(?:[^']|'')*'|[^,]+)", row)]
                plain = [v[1:-1] if len(v) >= 2 and v[0] == "'" and v[-1] == "'" else v for v in values]
                yield dict(zip(cols, plain))

    def test_leg_need_table_is_created_idempotently(self):
        seen = [name for name, text in migrations() if "CREATE TABLE IF NOT EXISTS `ai_dungeon_leg_need`" in text]
        self.assertEqual(len(seen), 1, seen)
        self.assertEqual(seen[0], "20261026130000_world.sql", seen)

    def test_leg_need_rows_match_the_plans(self):
        full = "\n".join(text for _name, text in migrations())
        rows = {}
        for row in self.need_rows(full):
            rows.setdefault(row["plan"], {})[(int(row["leg"]), int(row["item"]))] = (
                int(row["mode"]), int(row["pick"]), int(row["object"]))
        for plan, want in self.EXPECTED_LEG_NEEDS.items():
            self.assertEqual(rows.get(plan, {}), want, plan)
        self.assertEqual(set(rows), set(self.EXPECTED_LEG_NEEDS))
        # Every need leg has a step with that leg in the newest plan migration; the loader refuses the rest.
        for plan, want in self.EXPECTED_LEG_NEEDS.items():
            _name, text = newest_plan_text(plan)
            have = {int(m.group(1)) for m in re.finditer(r"\('%s', (\d+)," % plan, text)}
            for leg, _item in want:
                self.assertIn(leg, have, "%s leg %d" % (plan, leg))

    # The door objects' spawns on the plans' maps (Tortoise V2 world `gameobject`, the 2026-10-26 audit's source): the
    # door hold looks for the door near the lead bot and stops only within kDoorNearYd (25) of it
    # (runtime/DungeonRunPolicy.h DoorNear), so a door row sits on the first leg whose route crosses it.
    DOOR_SPAWNS = {
        170560: (569.589, -9.36154, -70.2566), 170559: (496.397, 16.0126, -70.2566),
        170558: (1311.48, -343.072, -92.0093), 104591: (1069.95, 1399.14, 30.7956),
        101854: (1933.69, -431.654, 18.671), 175357: (3727.29, -3599.08, 142.22),
        175167: (174.378, 77.9398, 104.802), 300601: (50.5672, 501.383, -23.3041),
        177219: (385.208, 373.802, -1.15898), 177217: (491.255, 518.678, 29.463),
        90858: (-758.538, 260.386, -273.049), 2020233: (-6068.09, -4098.55, 218.912),
        2020232: (-6120.35, -4402.09, 301.998),
    }

    def test_leg_need_door_rows_sit_on_the_leg_that_crosses_the_door(self):
        # Review round 2 (Opus 3): each door row is on the first leg with a step within 6.5 yd of the door (the route
        # crosses it there), or, where no step comes that close (Dire Maul North's courtyard, crossed between leg 1's
        # boss and leg 2), the first leg with a step within the hold's 25 yd; and that leg has a step within 25 yd.
        full = "\n".join(text for _name, text in migrations())
        doors = [(n["plan"], int(n["leg"]), int(n["object"])) for n in self.need_rows(full) if n["mode"] == "2"]
        self.assertTrue(doors)
        for plan, leg, obj in doors:
            _name, text = newest_plan_text(plan)
            rows = [r for r in self.step_rows(text) if r["plan"] == plan]
            door = self.DOOR_SPAWNS[obj]
            near = {}
            for r in rows:
                d = math.dist((float(r["x"]), float(r["y"]), float(r["z"])), door)
                near[int(r["leg"])] = min(near.get(int(r["leg"]), 1e9), d)
            crossing = [l for l in sorted(near) if near[l] <= 6.5] or [l for l in sorted(near) if near[l] <= 25.0]
            self.assertTrue(crossing, "%s door %d: no leg comes near it" % (plan, obj))
            self.assertEqual(leg, crossing[0], "%s door %d" % (plan, obj))
            self.assertLessEqual(near[leg], 25.0, "%s door %d" % (plan, obj))

    def test_leg_need_zulfarrak_leg_is_gahzrilla(self):
        full = "\n".join(text for _name, text in migrations())
        zf = [r for r in self.need_rows(full) if r["plan"] == "zulfarrak"]
        self.assertEqual(len(zf), 1, zf)
        row = zf[0]
        self.assertEqual((row["leg"], row["item"], row["mode"]), ("9", "9240", "1"))

    def test_leg_need_gahz_pins_stay(self):
        name, text, rows = newest_zulfarrak_rows()
        self.assertIn("7273", [r["entry"] for r in rows if r["kind"] == "boss"], name)

    def test_scholomance_vectus_room_is_the_boss_rows_first_adds(self):
        # 20261025003000 (turtle-agent 7474973, proof20d4/proof20d2 scholomance, 2026-10-26): step 470 (d4; 460 in d2), the
        # 5-student pack at 130.28 83.56, drew all 24 living students on the first attempt of both runs (d4: the rogue's
        # pre-pull Sap reached 18.6 yd from Vectus, 7 wipes; d2: the hunter stepped to 19.1 yd) because a melee fight with
        # students 13.9-22.9 yd from Vectus cannot stay out of his 20 yd aggro, and the pulled unit's 10 yd assistance ring
        # makes the pack indivisible. The two packs (5 + 3) are the boss row's first adds now (10433 before 10475: Marduk's
        # party-wide aura and volley kill the 4.3k hp priest first, E1); the other 24 stay in eight pulls, and the last pull's
        # approach carries a staging point at 95 88 (50.2 yd from Vectus, outside the boss row's 30 + 10 yd walk-back reach)
        # so a wipe's walk back replays the room's own rows, not the leg's 448 entry. Every pin fails when its rows are cut.
        name, text = newest_plan_text("scholomance")
        self.assertEqual(name, "20261025003000_world.sql")
        rows = [r for r in self.step_rows(text) if r["plan"] == "scholomance"]
        leg6 = leg_steps(rows, 6)
        self.assertEqual(len(leg6), 39)
        student_pulls = [(r["entries"], r["x"], r["y"]) for r in leg6 if r["kind"] == "pull" and r["entries"] == "10475"]
        self.assertEqual(student_pulls, [
            ("10475", "142.4", "67.28"), ("10475", "111.42", "67.52"), ("10475", "96.04", "75.03"),
            ("10475", "109.77", "86.73"), ("10475", "97.97", "95.53"), ("10475", "109.13", "103.61"),
            ("10475", "105.33", "110.35"), ("10475", "118.24", "114.61")])
        self.assertEqual(len(student_pulls), 8)
        # The two packs inside Vectus' aggro are gone as pulls, rows and all.
        self.assertNotIn(("10475", "130.28", "83.56"), [(r["entries"], r["x"], r["y"]) for r in leg6])
        self.assertNotIn(("10475", "122.68", "97.43"), [(r["entries"], r["x"], r["y"]) for r in leg6])
        self.assertFalse([r for r in leg6 if float(r["radius"]) == 8.8])
        # The boss row: the students before Marduk, then Vectus, from the unchanged 128 105 102.4 approach.
        boss = leg6[-1]
        self.assertEqual((boss["kind"], boss["entry"], boss["entries"]), ("boss", "10432", "10433,10475"))
        self.assertEqual((leg6[-2]["kind"], leg6[-2]["comment"]), ("move", "boss approach"))
        self.assertEqual((leg6[-2]["x"], leg6[-2]["y"], leg6[-2]["z"]), ("128", "105", "102.4"))
        self.assertGreater(len(boss["entries"].split(",")), 1)
        # The staging point before the last pull's stand: 95 88 (the walk back's safe point), then the 113 92 stand.
        i = next(k for k, r in enumerate(leg6) if r["kind"] == "pull" and r["x"] == "118.24")
        self.assertEqual((leg6[i - 1]["x"], leg6[i - 1]["y"], leg6[i - 1]["z"]), ("113", "92", "99.37"))
        self.assertEqual((leg6[i - 2]["x"], leg6[i - 2]["y"], leg6[i - 2]["z"]), ("95", "88", "97.686"))
        self.assertEqual(leg6[i - 2]["comment"], leg6[i - 1]["comment"])
        # No other leg pulls a student, and leg 6 pulls exactly the eight rows above.
        self.assertEqual([r["entry"] for r in rows if r["kind"] == "boss"],
                         ["10505", "10507", "10901", "10502", "10503", "11622", "10432", "10508", "11261", "10504"])
        self.assertFalse([r for r in rows if r["kind"] == "pull" and r["leg"] != "6" and "10475" in r["entries"].split(",")])
        self.assertEqual(sum(1 for r in leg6 if r["kind"] == "pull"), 26)

if __name__ == "__main__":
    unittest.main()
