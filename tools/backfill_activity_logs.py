#!/usr/bin/env python3
"""
Backfill historical bot activity metrics (kills, loot, money, deaths, vendor visits,
giveups, trainer visits, AH) from server log archives and database into activity-state.json
for the observability dashboard.
"""

import json
import os
import re
import subprocess
import time
from collections import defaultdict

STATE_FILE = "/mnt/pny-ssd/Tortoise WoW Projects/tortoise-docker/data/icons/activity-state.json"

CLASS_MAP = {
    1: "warrior",
    2: "paladin",
    3: "hunter",
    4: "rogue",
    5: "priest",
    7: "shaman",
    8: "mage",
    9: "warlock",
    11: "druid"
}

def run_db_query(db_name, query):
    cmd = [
        "docker", "compose", "exec", "-T", "db",
        "mariadb", "-umangos", "-pmangos", db_name,
        "-N", "-B", "-e", query
    ]
    cwd = "/mnt/pny-ssd/Tortoise WoW Projects/tortoise-docker"
    res = subprocess.run(cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=True)
    return res.stdout

def load_item_templates():
    print("Loading item templates from tw_world...")
    raw = run_db_query("tw_world", "SELECT entry, quality, sell_price, buy_price FROM item_template;")
    items = {}
    for line in raw.splitlines():
        parts = line.split('\t')
        if len(parts) >= 4:
            try:
                entry = int(parts[0])
                quality = int(parts[1])
                sell_price = int(parts[2])
                buy_price = int(parts[3])
                items[entry] = (quality, sell_price, buy_price)
            except ValueError:
                continue
    print(f"Loaded {len(items)} item templates.")
    return items

def load_characters():
    print("Loading characters from tw_char...")
    raw = run_db_query("tw_char", "SELECT guid, name, race, class, level, money FROM characters;")
    chars = {}
    name_to_guid = {}
    for line in raw.splitlines():
        parts = line.split('\t')
        if len(parts) >= 6:
            try:
                guid = int(parts[0])
                name = parts[1]
                cls_id = int(parts[3])
                lvl = int(parts[4])
                money = int(parts[5])
                cls_name = CLASS_MAP.get(cls_id, "unknown")
                chars[guid] = {
                    "guid": guid,
                    "name": name,
                    "class": cls_name,
                    "level": lvl,
                    "money": money
                }
                name_to_guid[name.lower()] = guid
            except ValueError:
                continue
    print(f"Loaded {len(chars)} characters.")
    return chars, name_to_guid

def load_quest_rewards(bot_stats):
    print("Loading quest cash rewards from DB...")
    query = """
    SELECT c.guid, sum(q.RewOrReqMoney)
    FROM tw_char.character_queststatus c
    JOIN tw_world.quest_template q ON c.quest = q.entry
    WHERE c.status = 1 AND q.RewOrReqMoney > 0
    GROUP BY c.guid;
    """
    raw = run_db_query("tw_char", query)
    total_reward = 0
    for line in raw.splitlines():
        parts = line.split('\t')
        if len(parts) >= 2:
            try:
                guid = int(parts[0])
                money = int(parts[1])
                if guid in bot_stats:
                    bot_stats[guid]["money_earned"] += money
                    total_reward += money
            except ValueError:
                continue
    print(f"Added {total_reward:,} copper ({total_reward/10000:.2f}g) in quest rewards.")

def process_loot_log(items_db, chars, name_to_guid, bot_stats):
    print("Streaming /opt/turtle/logs/loot.log...")
    cmd = ["docker", "compose", "exec", "-T", "mangosd", "cat", "/opt/turtle/logs/loot.log"]
    cwd = "/mnt/pny-ssd/Tortoise WoW Projects/tortoise-docker"
    proc = subprocess.Popen(cmd, cwd=cwd, stdout=subprocess.PIPE, text=True, bufsize=2097152)

    item_re = re.compile(r'^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2} ([^:]+):(\d+) (?:\[[^\]]*\] )?(?:loots|wins (?:need|greed) roll for) (\d+)x(\d+)')
    money_re = re.compile(r'^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2} ([^:]+):(\d+) (?:\[[^\]]*\] )?gets (?:(\d+)g)?(?:(\d+)s)?(?:(\d+)c)?')
    creature_re = re.compile(r'\[loot from Creature \(Entry: (\d+) Guid: (\d+)\)\]')

    # To ensure kills are distinct per creature
    creature_kills = defaultdict(set)

    total_lines = 0
    matched_items = 0
    matched_money = 0

    for line in proc.stdout:
        total_lines += 1
        line = line.strip()
        if not line:
            continue

        m_item = item_re.match(line)
        if m_item:
            b_name = m_item.group(1)
            b_guid = int(m_item.group(2))
            count = int(m_item.group(3))
            item_id = int(m_item.group(4))

            if b_guid not in chars:
                b_guid = name_to_guid.get(b_name.lower(), b_guid)

            if b_guid not in bot_stats:
                continue

            matched_items += 1
            stats = bot_stats[b_guid]
            stats["loot_items"] += count
            stats["events"] += 1

            # Track creature kill
            m_creat = creature_re.search(line)
            if m_creat:
                c_guid = int(m_creat.group(2))
                if c_guid not in creature_kills[b_guid]:
                    creature_kills[b_guid].add(c_guid)
                    stats["kills"] += 1

            q, sp, bp = items_db.get(item_id, (0, 0, 0))
            if q >= 2:
                stats["notable_loot"] += count
            item_val = sp if sp > 0 else bp
            stats["loot_value"] += count * item_val

            if "skin" in line.lower():
                stats["skinning"] += count
            elif "gameobject" in line.lower():
                stats["gathering"] += count
            continue

        m_money = money_re.match(line)
        if m_money:
            b_name = m_money.group(1)
            b_guid = int(m_money.group(2))
            g = int(m_money.group(3) or 0)
            s = int(m_money.group(4) or 0)
            c = int(m_money.group(5) or 0)
            copper = g * 10000 + s * 100 + c

            if b_guid not in chars:
                b_guid = name_to_guid.get(b_name.lower(), b_guid)

            if b_guid not in bot_stats:
                continue

            matched_money += 1
            stats = bot_stats[b_guid]
            stats["money_earned"] += copper
            stats["events"] += 1

            m_creat = creature_re.search(line)
            if m_creat:
                c_guid = int(m_creat.group(2))
                if c_guid not in creature_kills[b_guid]:
                    creature_kills[b_guid].add(c_guid)
                    stats["kills"] += 1
            continue

    proc.wait()
    total_kills = sum(len(s) for s in creature_kills.values())
    print(f"Processed loot.log: {total_lines} lines ({matched_items} item loots, {matched_money} money pickups, {total_kills:,} creature kills).")

def process_deaths_csv(chars, name_to_guid, bot_stats):
    print("Streaming /opt/turtle/logs/deaths.csv...")
    cmd = ["docker", "compose", "exec", "-T", "mangosd", "cat", "/opt/turtle/logs/deaths.csv"]
    cwd = "/mnt/pny-ssd/Tortoise WoW Projects/tortoise-docker"
    proc = subprocess.Popen(cmd, cwd=cwd, stdout=subprocess.PIPE, text=True, bufsize=1048576)

    total_deaths = 0
    matched_deaths = 0

    for line in proc.stdout:
        line = line.strip()
        if not line:
            continue
        parts = line.split(',', 6)
        if len(parts) >= 2:
            total_deaths += 1
            bot_name = parts[1]
            guid = name_to_guid.get(bot_name.lower())
            if guid and guid in bot_stats:
                matched_deaths += 1
                bot_stats[guid]["deaths"] += 1
                bot_stats[guid]["events"] += 1

    proc.wait()
    print(f"Processed deaths.csv: {total_deaths} deaths ({matched_deaths} matched current bots).")

def process_trades_log(chars, name_to_guid, bot_stats):
    print("Streaming /opt/turtle/logs/trades.log...")
    cmd = ["docker", "compose", "exec", "-T", "mangosd", "cat", "/opt/turtle/logs/trades.log"]
    cwd = "/mnt/pny-ssd/Tortoise WoW Projects/tortoise-docker"
    proc = subprocess.Popen(cmd, cwd=cwd, stdout=subprocess.PIPE, text=True, bufsize=1048576)

    trade_re = re.compile(r'^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2} (?:\[([^\]]+)\]:? )?(?:Player )?([^:]+):(\d+)')
    money_val_re = re.compile(r'gets (-?\d+)c')

    total_trades = 0
    matched = 0

    for line in proc.stdout:
        line = line.strip()
        if not line:
            continue
        total_trades += 1
        m = trade_re.match(line)
        if not m:
            continue
        tag = m.group(1) or ""
        bot_name = m.group(2)
        bot_guid = int(m.group(3))

        if bot_guid not in chars:
            bot_guid = name_to_guid.get(bot_name.lower(), bot_guid)

        if bot_guid not in bot_stats:
            continue

        matched += 1
        stats = bot_stats[bot_guid]
        stats["events"] += 1

        mm = money_val_re.search(line)
        copper = int(mm.group(1)) if mm else 0

        if tag == "SellItem":
            stats["items_sold"] += 1
            stats["sold_value"] += copper
            stats["money_earned"] += copper
            stats["vendor_visits"] += 1
        elif tag == "BuyItem":
            stats["items_bought"] += 1
            cost = abs(copper)
            stats["bought_value"] += cost
            stats["money_spent"] += cost
            stats["vendor_visits"] += 1
        elif tag == "AuctionBuyout":
            stats["ah_bids"] += 1
            cost = abs(copper)
            stats["money_spent"] += cost
        elif tag == "AuctionHouse":
            stats["ah_listings"] += 1
        elif tag == "MailAuction":
            stats["money_earned"] += copper
        elif tag in ("Quest", "QuestMaxLevel"):
            if copper > 0:
                stats["money_earned"] += copper
            elif copper < 0:
                stats["money_spent"] += abs(copper)

    proc.wait()
    print(f"Processed trades.log: {total_trades} trades ({matched} matched current bots).")

def process_bot_events_csv(name_to_guid, bot_stats):
    print("Streaming /opt/turtle/logs/bot_events.csv...")
    cmd = ["docker", "compose", "exec", "-T", "mangosd", "cat", "/opt/turtle/logs/bot_events.csv"]
    cwd = "/mnt/pny-ssd/Tortoise WoW Projects/tortoise-docker"
    proc = subprocess.Popen(cmd, cwd=cwd, stdout=subprocess.PIPE, text=True, bufsize=1048576)

    giveup_events = {
        'ReachGiveUp', 'TravelTargetDropped', 'QuestDropped',
        'QuestObjectiveStalled', 'TravelMoveFailed', 'SellErrandFailed',
        'TrainerNoMoney', 'LongStuckFallback', 'NoPathTrapRescue'
    }

    total_events = 0
    matched_events = 0

    for line in proc.stdout:
        line = line.strip()
        if not line:
            continue
        total_events += 1
        parts = line.split(',')
        if len(parts) >= 3:
            b_name = parts[1].lower()
            guid = name_to_guid.get(b_name)
            if not guid or guid not in bot_stats:
                continue

            matched_events += 1
            stats = bot_stats[guid]
            evt = parts[2]

            if evt in giveup_events:
                stats["giveups"] += 1
            elif evt in ('SellAction', 'BuyAction'):
                stats["vendor_visits"] += 1
            elif evt == 'NearbyService' and len(parts) >= 8:
                service = parts[7].replace('"', '').strip()
                if service == 'sell':
                    stats["vendor_visits"] += 1
                elif service == 'trainer':
                    stats["trainer_visits"] += 1
            elif evt == 'TrainerAction':
                stats["trainer_visits"] += 1
            elif evt == 'RepairAllAction':
                stats["repairs"] += 1
                if len(parts) >= 9:
                    try:
                        cost = int(parts[8].replace('"', '').strip())
                        stats["repair_cost"] += cost
                        stats["money_spent"] += cost
                    except ValueError:
                        pass
            elif evt == 'AhAction':
                stats["ah_listings"] += 1
            elif evt == 'AhBidAction':
                stats["ah_bids"] += 1

    proc.wait()
    print(f"Processed bot_events.csv: {total_events} events ({matched_events} matched current bots).")

def main():
    items_db = load_item_templates()
    chars, name_to_guid = load_characters()

    # Initialize counters for all characters
    bot_stats = {}
    for guid in chars:
        bot_stats[guid] = {
            "loot_items": 0,
            "loot_value": 0,
            "notable_loot": 0,
            "items_sold": 0,
            "sold_value": 0,
            "items_bought": 0,
            "bought_value": 0,
            "money_earned": 0,
            "money_spent": 0,
            "kills": 0,
            "deaths": 0,
            "skinning": 0,
            "gathering": 0,
            "ah_listings": 0,
            "ah_bids": 0,
            "vendor_visits": 0,
            "trainer_visits": 0,
            "repairs": 0,
            "repair_cost": 0,
            "giveups": 0,
            "events": 0
        }

    load_quest_rewards(bot_stats)
    process_loot_log(items_db, chars, name_to_guid, bot_stats)
    process_deaths_csv(chars, name_to_guid, bot_stats)
    process_trades_log(chars, name_to_guid, bot_stats)
    process_bot_events_csv(name_to_guid, bot_stats)

    # Now load current activity-state.json
    print(f"Loading state file {STATE_FILE}...")
    with open(STATE_FILE, 'r') as f:
        state_data = json.load(f)

    # Existing bots map
    existing_bots = {b["guid"]: b for b in state_data.get("bots", []) if "guid" in b}

    def estimate_spell_cost(level):
        if level <= 5:
            return level * 100
        elif level <= 10:
            return 500 + (level - 5) * 300
        elif level <= 15:
            return 2000 + (level - 10) * 800
        elif level <= 20:
            return 6000 + (level - 15) * 1500
        elif level <= 30:
            return 13500 + (level - 20) * 2500
        elif level <= 40:
            return 38500 + (level - 30) * 4500
        elif level <= 50:
            return 83500 + (level - 40) * 8000
        else:
            return 163500 + (level - 50) * 15000

    # Merge stats
    for guid, s in bot_stats.items():
        if guid not in chars:
            continue
        c_info = chars[guid]
        wallet = c_info["money"]
        lvl = c_info["level"]
        
        # For non-admin bots, reconcile money spent on training and total earned to cover current wallet
        if guid != 501 and guid != 930025:
            est_spent = estimate_spell_cost(lvl)
            total_spent = max(s["money_spent"], est_spent + s["money_spent"])
            total_earned = max(s["money_earned"], wallet + total_spent)
            s["money_spent"] = total_spent
            s["money_earned"] = total_earned

        if guid not in existing_bots:
            existing_bots[guid] = {
                "guid": guid,
                "name": c_info["name"],
                "class": c_info["class"],
                "level": c_info["level"],
                "counters": {
                    "quests_rewarded": 0,
                    "quests_accepted": 0,
                    "quests_completed": 0,
                    "quest_handins": 0,
                    "open_quests": 0,
                    "loot_items": s["loot_items"],
                    "loot_value": s["loot_value"],
                    "notable_loot": s["notable_loot"],
                    "items_sold": s["items_sold"],
                    "sold_value": s["sold_value"],
                    "items_bought": s["items_bought"],
                    "bought_value": s["bought_value"],
                    "money_earned": s["money_earned"],
                    "money_spent": s["money_spent"],
                    "kills": s["kills"],
                    "deaths": s["deaths"],
                    "ghost_seconds": 0,
                    "trainer_visits": s["trainer_visits"],
                    "spells_learned": 0,
                    "vendor_visits": s["vendor_visits"],
                    "repairs": s["repairs"],
                    "repair_cost": s["repair_cost"],
                    "giveups": s["giveups"],
                    "skinning": s["skinning"],
                    "gathering": s["gathering"],
                    "skillups": 0,
                    "ah_listings": s["ah_listings"],
                    "ah_bids": s["ah_bids"],
                    "events": s["events"],
                    "levels_gained": 0,
                    "first_seen": int(time.time()),
                    "last_event": int(time.time()),
                    "levels": [{"level": c_info["level"], "at": int(time.time())}]
                },
                "last_money": c_info["money"],
                "has_money": True,
                "last_level": c_info["level"],
                "open_quests": []
            }
        else:
            # Merge counters by taking max of existing and backfilled stats
            b = existing_bots[guid]
            cnt = b.setdefault("counters", {})
            for k in ("loot_items", "loot_value", "notable_loot", "items_sold", "sold_value",
                      "items_bought", "bought_value",
                      "kills", "deaths", "skinning", "gathering", "ah_listings", "ah_bids",
                      "vendor_visits", "trainer_visits", "repairs", "repair_cost", "giveups"):
                cnt[k] = max(cnt.get(k, 0), s.get(k, 0))
            cnt["money_spent"] = max(cnt.get("money_spent", 0), s["money_spent"])
            cnt["money_earned"] = max(cnt.get("money_earned", 0), s["money_earned"])
            cnt["events"] = max(cnt.get("events", 0), s.get("events", 0))

    state_data["bots"] = list(existing_bots.values())

    # Write atomic update
    tmp_file = STATE_FILE + ".tmp"
    with open(tmp_file, 'w') as f:
        json.dump(state_data, f, indent=2)
    os.replace(tmp_file, STATE_FILE)
    print(f"Successfully backfilled activity state to {STATE_FILE} for {len(state_data['bots'])} bots.")

if __name__ == "__main__":
    main()
