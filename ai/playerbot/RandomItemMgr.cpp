
#include "playerbot/playerbot.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "RandomItemMgr.h"
#include "Maps/Map.h"
#include "Database/DatabaseEnv.h"

#include "playerbot/ServerFacade.h"
#include "strategy/values/LootValues.h"

#include "ItemEnchantmentMgr.h"

#include "strategy/values/SharedValueContext.h"

char * strstri (const char* str1, const char* str2);

namespace
{
    bool IsRandomGearCandidate(ItemPrototype const* proto, uint8 clazz)
    {
        if (!proto)
            return false;

        if (proto->Flags & (ITEM_FLAG_CONJURED | ITEM_FLAG_DEPRECATED | ITEM_FLAG_WRAPPER))
            return false;

        if (proto->StartQuest || proto->RequiredSkill || proto->RequiredSkillRank || proto->RequiredSpell ||
            proto->RequiredHonorRank || proto->RequiredCityRank || proto->RequiredReputationFaction ||
            proto->RequiredReputationRank)
            return false;

        if (proto->AllowableClass && (proto->AllowableClass & (1u << (clazz - 1))) == 0)
            return false;

        return proto->Name1.find("(Test)") == std::string::npos &&
            proto->Name1.find("(TEST)") == std::string::npos &&
            proto->Name1.find("(test)") == std::string::npos &&
            proto->Name1.find("Test") == std::string::npos &&
            proto->Name1.find("TEST") == std::string::npos &&
            proto->Name1.find("Deprecated") == std::string::npos &&
            proto->Name1.find("Unused") == std::string::npos &&
            proto->Name1.find("Monster ") == std::string::npos &&
            proto->Name1.find("[PH]") == std::string::npos &&
            proto->Name1.find("(OLD)") == std::string::npos &&
            proto->Name1.find("zzz") == std::string::npos &&
            proto->Name1.find("ZZZ") == std::string::npos;
    }
}

uint64 BotEquipKey::GetKey()
{
    return level + 100 * clazz + 10000 * spec + 1000000 * slot + 100000000 * quality;
}

RandomItemMgr::RandomItemMgr()
{
    viableSlots[EQUIPMENT_SLOT_HEAD].insert(INVTYPE_HEAD);
    viableSlots[EQUIPMENT_SLOT_NECK].insert(INVTYPE_NECK);
    viableSlots[EQUIPMENT_SLOT_SHOULDERS].insert(INVTYPE_SHOULDERS);
    viableSlots[EQUIPMENT_SLOT_BODY].insert(INVTYPE_BODY);
    viableSlots[EQUIPMENT_SLOT_CHEST].insert(INVTYPE_CHEST);
    viableSlots[EQUIPMENT_SLOT_CHEST].insert(INVTYPE_ROBE);
    viableSlots[EQUIPMENT_SLOT_WAIST].insert(INVTYPE_WAIST);
    viableSlots[EQUIPMENT_SLOT_LEGS].insert(INVTYPE_LEGS);
    viableSlots[EQUIPMENT_SLOT_FEET].insert(INVTYPE_FEET);
    viableSlots[EQUIPMENT_SLOT_WRISTS].insert(INVTYPE_WRISTS);
    viableSlots[EQUIPMENT_SLOT_HANDS].insert(INVTYPE_HANDS);
    viableSlots[EQUIPMENT_SLOT_FINGER1].insert(INVTYPE_FINGER);
    viableSlots[EQUIPMENT_SLOT_FINGER2].insert(INVTYPE_FINGER);
    viableSlots[EQUIPMENT_SLOT_TRINKET1].insert(INVTYPE_TRINKET);
    viableSlots[EQUIPMENT_SLOT_TRINKET2].insert(INVTYPE_TRINKET);
    viableSlots[EQUIPMENT_SLOT_MAINHAND].insert(INVTYPE_WEAPON);
    viableSlots[EQUIPMENT_SLOT_MAINHAND].insert(INVTYPE_2HWEAPON);
    viableSlots[EQUIPMENT_SLOT_MAINHAND].insert(INVTYPE_WEAPONMAINHAND);
    viableSlots[EQUIPMENT_SLOT_OFFHAND].insert(INVTYPE_WEAPON);
    viableSlots[EQUIPMENT_SLOT_OFFHAND].insert(INVTYPE_2HWEAPON);
    viableSlots[EQUIPMENT_SLOT_OFFHAND].insert(INVTYPE_SHIELD);
    viableSlots[EQUIPMENT_SLOT_OFFHAND].insert(INVTYPE_WEAPONOFFHAND);
    viableSlots[EQUIPMENT_SLOT_OFFHAND].insert(INVTYPE_HOLDABLE);
    viableSlots[EQUIPMENT_SLOT_RANGED].insert(INVTYPE_RANGED);
    viableSlots[EQUIPMENT_SLOT_RANGED].insert(INVTYPE_THROWN);
    viableSlots[EQUIPMENT_SLOT_RANGED].insert(INVTYPE_RANGEDRIGHT);
    viableSlots[EQUIPMENT_SLOT_RANGED].insert(INVTYPE_RELIC);
    viableSlots[EQUIPMENT_SLOT_TABARD].insert(INVTYPE_TABARD);
    viableSlots[EQUIPMENT_SLOT_BACK].insert(INVTYPE_CLOAK);

    weightStatLink["sta"] = ITEM_MOD_STAMINA;
    weightStatLink["str"] = ITEM_MOD_STRENGTH;
    weightStatLink["agi"] = ITEM_MOD_AGILITY;
    weightStatLink["int"] = ITEM_MOD_INTELLECT;
    weightStatLink["spi"] = ITEM_MOD_SPIRIT;

    ItemStatLink[STAT_STAMINA] = "sta";
    ItemStatLink[STAT_STRENGTH] = "str";
    ItemStatLink[STAT_AGILITY] = "agi";
    ItemStatLink[STAT_INTELLECT] = "int";
    ItemStatLink[STAT_SPIRIT] = "spi";


}

void RandomItemMgr::Init()
{
    BuildItemInfoCache();
    BuildEquipCache();
    BuildAmmoCache();
    BuildPotionCache();
    BuildFoodCache();
    BuildTradeCache();
    LoadRandomEnchantments();
    LoadBotEnchantCandidates();
    BuildRandomItemCache();
}

RandomItemMgr::~RandomItemMgr()
{
    for (std::map<RandomItemType, RandomItemPredicate*>::iterator i = predicates.begin(); i != predicates.end(); ++i)
        delete i->second;

    for (auto& [itemId, info] : itemInfoCache)
        delete info;

    predicates.clear();
}

bool RandomItemMgr::HandleConsoleCommand(ChatHandler* handler, char const* args)
{
    if (!args || !*args)
    {
        sLog.outError( "Usage: rnditem");
        return false;
    }

    return false;
}

RandomItemList RandomItemMgr::Query(uint32 level, RandomItemType type, RandomItemPredicate* predicate)
{
    RandomItemList &list = randomItemCache[(level - 1) / 10][type];

    RandomItemList result;
    for (RandomItemList::iterator i = list.begin(); i != list.end(); ++i)
    {
        uint32 itemId = *i;
        ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
        if (!proto)
            continue;

        if (predicate && !predicate->Apply(proto))
            continue;

        result.push_back(itemId);
    }

    return result;
}

void RandomItemMgr::BuildRandomItemCache()
{
    randomItemCache.clear();
    auto results = CharacterDatabase.PQuery("select lvl, type, item from ai_playerbot_rnditem_cache");
    if (results)
    {
        sLog.outString("Loading random item cache");
        int count = 0;
        do
        {
            Field* fields = results->Fetch();
            uint32 level = fields[0].GetUInt32();
            uint32 type = fields[1].GetUInt32();
            uint32 itemId = fields[2].GetUInt32();

            RandomItemType rit = (RandomItemType)type;
            randomItemCache[level][rit].push_back(itemId);
            count++;

        } while (results->NextRow());
        sLog.outString("Equipment cache loaded from %d records", count);
    }
    else
    {
        // MaNGOS returns an empty result set as a failed query, so what is left
        // here is either an absent table or an empty one. The native module ships
        // the schema only, so a fresh install has an empty cache and reaches this
        // path; it is generated once and loaded from the database afterwards.
        auto cacheCount = CharacterDatabase.PQuery("SELECT COUNT(*) FROM ai_playerbot_rnditem_cache");
        if (!cacheCount)
        {
            sLog.outErrorDb("TortoiseBots: ai_playerbot_rnditem_cache is missing; skipping optional cache generation");
            return;
        }

        if (!sPlayerbotAIConfig.generateItemCaches)
        {
            sLog.outString("Random item cache is empty and AiPlayerbot.GenerateItemCaches is disabled; skipping optional cache generation");
            return;
        }

        sLog.outString("Building random item cache from %u items", sItemStorage.GetMaxEntry());

        // One INSERT per entry would be ~94k round trips on the world thread, so
        // rows are collected into multi-row statements. PExecute formats into a
        // 32KB fixed buffer and does not report a truncated statement, so a flush
        // stays well below it.
        std::string rows;
        rows.reserve(MAX_QUERY_LEN / 2);
        auto flushRows = [&rows]()
        {
            if (rows.empty())
                return;

            CharacterDatabase.PExecute("insert into ai_playerbot_rnditem_cache (lvl, type, item) values %s", rows.c_str());
            rows.clear();
        };

        CharacterDatabase.BeginTransaction();

        for (uint32 itemId = 0; itemId < sItemStorage.GetMaxEntry(); ++itemId)
        {
            ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
            if (!proto)
                continue;

            if (proto->Duration & 0x80000000)
                continue;

            if (strstri(proto->Name1, "qa") || strstri(proto->Name1, "test") || strstri(proto->Name1, "deprecated"))
                continue;

            if (!proto->ItemLevel)
                continue;

            if (!proto->SellPrice)
                continue;

            uint32 level = proto->ItemLevel;
            for (uint32 type = RANDOM_ITEM_GUILD_TASK; type <= RANDOM_ITEM_GUILD_TASK_REWARD_TRADE_RARE; type++)
            {
                RandomItemType rit = (RandomItemType)type;
                if (predicates[rit] && !predicates[rit]->Apply(proto))
                    continue;

                char row[48];
                snprintf(row, sizeof(row), "%s(%u, %u, %u)", rows.empty() ? "" : ", ", level / 10, type, itemId);
                rows += row;

                if (rows.size() >= 24 * 1024)
                    flushRows();

                randomItemCache[level / 10][rit].push_back(itemId);
            }
        }

        flushRows();
        CharacterDatabase.CommitTransaction();

        uint32 maxLevel = sPlayerbotAIConfig.randomBotMaxLevel;
        if (maxLevel > sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL))
            maxLevel = sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL);
        for (uint32 level = 0; level < (maxLevel / 10); level++)
        {
            for (uint32 type = RANDOM_ITEM_GUILD_TASK; type <= RANDOM_ITEM_GUILD_TASK_REWARD_TRADE_RARE; type++)
            {
                RandomItemList list = randomItemCache[level][(RandomItemType)type];
                sLog.outDetail("    Level %d..%d Type %d - %zu random items cached",
                        level * 10, level * 10 + 9,
                        type,
                        list.size());
                for (RandomItemList::iterator i = list.begin(); i != list.end(); ++i)
                {
                    uint32 itemId = *i;
                    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
                    if (!proto)
                        continue;

                    sLog.outDetail("        [%d] %s", itemId, proto->Name1);
                }
            }
        }
    }
}

uint32 RandomItemMgr::GetRandomItem(uint32 level, RandomItemType type, RandomItemPredicate* predicate)
{
    RandomItemList const& list = Query(level, type, predicate);
    if (list.empty())
        return 0;

    uint32 index = urand(0, list.size() - 1);
    uint32 itemId = list[index];

    return itemId;
}

bool RandomItemMgr::CanEquipItemNew(ItemPrototype const* proto)
{
    if (proto->Duration & 0x80000000)
        return false;

    if (proto->Bonding == BIND_QUEST_ITEM || proto->Bonding == BIND_WHEN_USE)
        return false;

    if (proto->Class == ITEM_CLASS_CONTAINER)
        return false;

    bool properSlot = false;
    for (std::map<EquipmentSlots, std::set<InventoryType> >::iterator i = viableSlots.begin(); i != viableSlots.end(); ++i)
    {
        std::set<InventoryType> slots = viableSlots[(EquipmentSlots)i->first];
        if (slots.find((InventoryType)proto->InventoryType) != slots.end())
            properSlot = true;
    }

    return properSlot;
}

void RandomItemMgr::AddItemStats(uint32 mod, uint8 &sp, uint8 &ap, uint8 &tank)
{
    switch (mod)
    {
    case ITEM_MOD_HEALTH:
    case ITEM_MOD_STAMINA:
    case ITEM_MOD_MANA:
    case ITEM_MOD_INTELLECT:
    case ITEM_MOD_SPIRIT:
        sp++;
        break;
    }

    switch (mod)
    {
    case ITEM_MOD_AGILITY:
    case ITEM_MOD_STRENGTH:
    case ITEM_MOD_HEALTH:
    case ITEM_MOD_STAMINA:
        tank++;
        break;
    }

    switch (mod)
    {
    case ITEM_MOD_HEALTH:
    case ITEM_MOD_STAMINA:
    case ITEM_MOD_AGILITY:
    case ITEM_MOD_STRENGTH:
        ap++;
        break;
    }
}

bool RandomItemMgr::CheckItemStats(uint8 clazz, uint8 sp, uint8 ap, uint8 tank)
{
    switch (clazz)
    {
    case CLASS_PRIEST:
    case CLASS_MAGE:
    case CLASS_WARLOCK:
        if (!sp || ap > sp || tank > sp)
            return false;
        break;
    case CLASS_PALADIN:
    case CLASS_WARRIOR:
        if ((!ap && !tank) || sp > ap || sp > tank)
            return false;
        break;
    case CLASS_HUNTER:
    case CLASS_ROGUE:
        if (!ap || sp > ap || sp > tank)
            return false;
        break;
    }

    return sp || ap || tank;
}

bool RandomItemMgr::ShouldEquipArmorForSpec(uint8 playerclass, uint8 spec, ItemPrototype const* proto)
{
    if (proto->InventoryType == INVTYPE_TABARD)
        return true;

    if (!spec)
        spec = static_cast<uint8>(GetFallbackSpecId(playerclass));

    if (!m_weightScales[spec].info.id)
        return false;

    std::unordered_set<uint32> resultArmorSubClass = { ITEM_SUBCLASS_ARMOR_CLOTH };

    switch (playerclass)
    {
    case CLASS_WARRIOR:
    {
        if (proto->InventoryType == INVTYPE_HOLDABLE)
            return false;

        if (m_weightScales[spec].info.name == "arms" || m_weightScales[spec].info.name == "fury")
        {
            resultArmorSubClass = { ITEM_SUBCLASS_ARMOR_LEATHER, ITEM_SUBCLASS_ARMOR_MAIL, ITEM_SUBCLASS_ARMOR_PLATE };
        }
        else
            resultArmorSubClass = { ITEM_SUBCLASS_ARMOR_MAIL, ITEM_SUBCLASS_ARMOR_PLATE };
        break;
    }
    case CLASS_PALADIN:
    {
        if (m_weightScales[spec].info.name != "holy" && proto->InventoryType == INVTYPE_HOLDABLE)
            return false;

        if (m_weightScales[spec].info.name != "holy")
            resultArmorSubClass = { ITEM_SUBCLASS_ARMOR_MAIL, ITEM_SUBCLASS_ARMOR_PLATE , ITEM_SUBCLASS_ARMOR_LIBRAM };
        else
            resultArmorSubClass = { ITEM_SUBCLASS_ARMOR_CLOTH, ITEM_SUBCLASS_ARMOR_LEATHER, ITEM_SUBCLASS_ARMOR_MAIL, ITEM_SUBCLASS_ARMOR_PLATE, ITEM_SUBCLASS_ARMOR_LIBRAM };
        break;
    }
    case CLASS_HUNTER:
    {
        if (proto->InventoryType == INVTYPE_HOLDABLE)
            return false;

        resultArmorSubClass = { ITEM_SUBCLASS_ARMOR_CLOTH, ITEM_SUBCLASS_ARMOR_LEATHER, ITEM_SUBCLASS_ARMOR_MAIL };
        break;
    }
    case CLASS_ROGUE:
    {
        if (proto->InventoryType == INVTYPE_HOLDABLE)
            return false;

        resultArmorSubClass = { ITEM_SUBCLASS_ARMOR_CLOTH, ITEM_SUBCLASS_ARMOR_LEATHER };
        break;
    }
    case CLASS_PRIEST:
    {
        resultArmorSubClass = { ITEM_SUBCLASS_ARMOR_CLOTH };
        break;
    }
    case CLASS_SHAMAN:
    {
        if (m_weightScales[spec].info.name == "enhance" && proto->InventoryType == INVTYPE_HOLDABLE)
            return false;

        if (m_weightScales[spec].info.name == "enhance")
            resultArmorSubClass = { ITEM_SUBCLASS_ARMOR_TOTEM, ITEM_SUBCLASS_ARMOR_LEATHER, ITEM_SUBCLASS_ARMOR_MAIL };
        else
            resultArmorSubClass = { ITEM_SUBCLASS_ARMOR_TOTEM, ITEM_SUBCLASS_ARMOR_CLOTH, ITEM_SUBCLASS_ARMOR_LEATHER, ITEM_SUBCLASS_ARMOR_MAIL };
        break;
    }
    case CLASS_MAGE:
    case CLASS_WARLOCK:
    {
        resultArmorSubClass = { ITEM_SUBCLASS_ARMOR_CLOTH };
        break;
    }
    case CLASS_DRUID:
    {
        if ((m_weightScales[spec].info.name == "feraltank" || m_weightScales[spec].info.name == "feraldps") && proto->InventoryType == INVTYPE_HOLDABLE)
            return false;

        if (m_weightScales[spec].info.name == "feraltank" || m_weightScales[spec].info.name == "feraldps")
            resultArmorSubClass = { ITEM_SUBCLASS_ARMOR_IDOL, ITEM_SUBCLASS_ARMOR_LEATHER };
        else
            resultArmorSubClass = { ITEM_SUBCLASS_ARMOR_IDOL, ITEM_SUBCLASS_ARMOR_CLOTH, ITEM_SUBCLASS_ARMOR_LEATHER };

        break;
    }
    }

    return resultArmorSubClass.find(proto->SubClass) != resultArmorSubClass.end();
}

bool RandomItemMgr::CanEquipArmor(uint8 clazz, uint8 spec, uint32 level, ItemPrototype const* proto)
{
    if (proto->InventoryType == INVTYPE_TABARD)
        return true;

    if ((clazz == CLASS_WARRIOR || clazz == CLASS_PALADIN || clazz == CLASS_SHAMAN)
            && proto->SubClass == ITEM_SUBCLASS_ARMOR_SHIELD)
        return true;

    if ((clazz == CLASS_WARRIOR || (clazz == CLASS_PALADIN && spec != 4)) && level >= 40)
    {
        if (proto->SubClass != ITEM_SUBCLASS_ARMOR_PLATE && proto->InventoryType != INVTYPE_CLOAK)
            return false;
    }

    if (((clazz == CLASS_WARRIOR || (clazz == CLASS_PALADIN && spec != 4)) && level < 40) ||
            ((clazz == CLASS_HUNTER || (clazz == CLASS_SHAMAN && spec != 21)) && level >= 40))
    {
        if (proto->SubClass != ITEM_SUBCLASS_ARMOR_MAIL && proto->InventoryType != INVTYPE_CLOAK)
        {
            if (spec == 22 && (proto->SubClass == ITEM_SUBCLASS_ARMOR_LEATHER || proto->SubClass == ITEM_SUBCLASS_ARMOR_CLOTH))
                return true;

            return false;
        }
    }

    if (((clazz == CLASS_HUNTER || clazz == CLASS_SHAMAN) && level < 40) ||
            (((clazz == CLASS_DRUID && !(spec == 29 || spec == 31)))  || clazz == CLASS_ROGUE))
    {
        if (proto->SubClass != ITEM_SUBCLASS_ARMOR_LEATHER && proto->InventoryType != INVTYPE_CLOAK)
            return false;
    }

    if (proto->Quality <= ITEM_QUALITY_NORMAL)
        return true;

    return true;

    //uint8 sp = 0, ap = 0, tank = 0;
    //for (int j = 0; j < MAX_ITEM_PROTO_STATS; ++j)
    //{
    //    // for ItemStatValue != 0
    //    if(!proto->ItemStat[j].ItemStatValue)
    //        continue;

    //    AddItemStats(proto->ItemStat[j].ItemStatType, sp, ap, tank);
    //}

    //return CheckItemStats(clazz, sp, ap, tank);
}

bool RandomItemMgr::ShouldEquipWeaponForSpec(uint8 playerclass, uint8 spec, ItemPrototype const* proto)
{
    EquipmentSlots slot_mh = EQUIPMENT_SLOT_START;
    EquipmentSlots slot_oh = EQUIPMENT_SLOT_START;
    EquipmentSlots slot_rh = EQUIPMENT_SLOT_START;
    for (std::map<EquipmentSlots, std::set<InventoryType> >::iterator i = viableSlots.begin(); i != viableSlots.end(); ++i)
    {
        std::set<InventoryType> slots = viableSlots[(EquipmentSlots)i->first];
        if (slots.find((InventoryType)proto->InventoryType) != slots.end())
        {
            if (i->first == EQUIPMENT_SLOT_MAINHAND)
                slot_mh = i->first;
            if (i->first == EQUIPMENT_SLOT_OFFHAND)
                slot_oh = i->first;
            if (i->first == EQUIPMENT_SLOT_RANGED)
                slot_rh = i->first;
        }
    }

    if (slot_mh == EQUIPMENT_SLOT_START && slot_oh == EQUIPMENT_SLOT_START && slot_rh == EQUIPMENT_SLOT_START)
        return false;

    if (!spec)
        spec = static_cast<uint8>(GetFallbackSpecId(playerclass));

    if (!m_weightScales[spec].info.id)
        return false;

    std::unordered_set<uint32> mh_weapons;
    std::unordered_set<uint32> oh_weapons;
    std::unordered_set<uint32> r_weapons;

    switch (playerclass)
    {
    case CLASS_WARRIOR:
    {
        if (m_weightScales[spec].info.name == "prot")
        {
            mh_weapons = { ITEM_SUBCLASS_WEAPON_SWORD, ITEM_SUBCLASS_WEAPON_AXE, ITEM_SUBCLASS_WEAPON_MACE, ITEM_SUBCLASS_WEAPON_DAGGER, ITEM_SUBCLASS_WEAPON_FIST };
            oh_weapons = { ITEM_SUBCLASS_ARMOR_SHIELD };
            r_weapons = { ITEM_SUBCLASS_WEAPON_BOW, ITEM_SUBCLASS_WEAPON_CROSSBOW, ITEM_SUBCLASS_WEAPON_GUN };
        }
        else if (m_weightScales[spec].info.name == "arms")
        {
            mh_weapons = { ITEM_SUBCLASS_WEAPON_SWORD2, ITEM_SUBCLASS_WEAPON_AXE2, ITEM_SUBCLASS_WEAPON_MACE2, ITEM_SUBCLASS_WEAPON_POLEARM };
            r_weapons = { ITEM_SUBCLASS_WEAPON_BOW, ITEM_SUBCLASS_WEAPON_CROSSBOW, ITEM_SUBCLASS_WEAPON_GUN };
        }
        else
        {
            mh_weapons = { ITEM_SUBCLASS_WEAPON_SWORD, ITEM_SUBCLASS_WEAPON_AXE, ITEM_SUBCLASS_WEAPON_MACE, ITEM_SUBCLASS_WEAPON_DAGGER, ITEM_SUBCLASS_WEAPON_FIST };
            oh_weapons = { ITEM_SUBCLASS_WEAPON_SWORD, ITEM_SUBCLASS_WEAPON_AXE, ITEM_SUBCLASS_WEAPON_MACE, ITEM_SUBCLASS_WEAPON_DAGGER, ITEM_SUBCLASS_WEAPON_FIST };
            r_weapons = { ITEM_SUBCLASS_WEAPON_BOW, ITEM_SUBCLASS_WEAPON_CROSSBOW, ITEM_SUBCLASS_WEAPON_GUN };
        }
        break;
    }
    case CLASS_PALADIN:
    {
        if (m_weightScales[spec].info.name == "prot")
        {
            mh_weapons = { ITEM_SUBCLASS_WEAPON_SWORD, ITEM_SUBCLASS_WEAPON_AXE, ITEM_SUBCLASS_WEAPON_MACE };
            oh_weapons = { ITEM_SUBCLASS_ARMOR_SHIELD };
            r_weapons = { ITEM_SUBCLASS_ARMOR_LIBRAM };
        }
        else if (m_weightScales[spec].info.name == "holy")
        {
            mh_weapons = { ITEM_SUBCLASS_WEAPON_SWORD, ITEM_SUBCLASS_WEAPON_AXE, ITEM_SUBCLASS_WEAPON_MACE };
            oh_weapons = { ITEM_SUBCLASS_ARMOR_SHIELD, ITEM_SUBCLASS_ARMOR_MISC };
            r_weapons = { ITEM_SUBCLASS_ARMOR_LIBRAM };
        }
        else
        {
            mh_weapons = { ITEM_SUBCLASS_WEAPON_SWORD2, ITEM_SUBCLASS_WEAPON_AXE2, ITEM_SUBCLASS_WEAPON_MACE2, ITEM_SUBCLASS_WEAPON_POLEARM };
            r_weapons = { ITEM_SUBCLASS_ARMOR_LIBRAM };
        }
        break;
    }
    case CLASS_HUNTER:
    {
        mh_weapons = { ITEM_SUBCLASS_WEAPON_FIST, ITEM_SUBCLASS_WEAPON_DAGGER, ITEM_SUBCLASS_WEAPON_SWORD, ITEM_SUBCLASS_WEAPON_AXE, ITEM_SUBCLASS_WEAPON_SWORD2, ITEM_SUBCLASS_WEAPON_AXE2, ITEM_SUBCLASS_WEAPON_POLEARM, ITEM_SUBCLASS_WEAPON_STAFF };
        r_weapons = { ITEM_SUBCLASS_WEAPON_BOW, ITEM_SUBCLASS_WEAPON_CROSSBOW, ITEM_SUBCLASS_WEAPON_GUN };
        break;
    }
    case CLASS_ROGUE:
    {
        if (m_weightScales[spec].info.name == "assas")
        {
            mh_weapons = { ITEM_SUBCLASS_WEAPON_DAGGER };
            oh_weapons = { ITEM_SUBCLASS_WEAPON_DAGGER };
        }
        else if (m_weightScales[spec].info.name == "combat")
        {
            // Rogues are taught fist skill; combat uses swords/maces + fists.
            mh_weapons = { ITEM_SUBCLASS_WEAPON_SWORD, ITEM_SUBCLASS_WEAPON_MACE, ITEM_SUBCLASS_WEAPON_FIST };
            oh_weapons = { ITEM_SUBCLASS_WEAPON_SWORD, ITEM_SUBCLASS_WEAPON_MACE, ITEM_SUBCLASS_WEAPON_FIST };
        }
        else
        {
            mh_weapons = { ITEM_SUBCLASS_WEAPON_DAGGER, ITEM_SUBCLASS_WEAPON_SWORD, ITEM_SUBCLASS_WEAPON_MACE, ITEM_SUBCLASS_WEAPON_FIST };
            oh_weapons = { ITEM_SUBCLASS_WEAPON_DAGGER, ITEM_SUBCLASS_WEAPON_SWORD, ITEM_SUBCLASS_WEAPON_MACE, ITEM_SUBCLASS_WEAPON_FIST };
        }

        r_weapons = { ITEM_SUBCLASS_WEAPON_THROWN, ITEM_SUBCLASS_WEAPON_BOW, ITEM_SUBCLASS_WEAPON_CROSSBOW, ITEM_SUBCLASS_WEAPON_GUN };
        break;
    }
    case CLASS_PRIEST:
    {
        mh_weapons = { ITEM_SUBCLASS_WEAPON_STAFF, ITEM_SUBCLASS_WEAPON_DAGGER, ITEM_SUBCLASS_WEAPON_MACE };
        oh_weapons = { ITEM_SUBCLASS_ARMOR_MISC };
        r_weapons = { ITEM_SUBCLASS_WEAPON_WAND };
        break;
    }
    case CLASS_SHAMAN:
    {
        if (m_weightScales[spec].info.name == "resto")
        {
            mh_weapons = { ITEM_SUBCLASS_WEAPON_STAFF, ITEM_SUBCLASS_WEAPON_DAGGER, ITEM_SUBCLASS_WEAPON_AXE, ITEM_SUBCLASS_WEAPON_MACE, ITEM_SUBCLASS_WEAPON_FIST };
            oh_weapons = { ITEM_SUBCLASS_ARMOR_MISC, ITEM_SUBCLASS_ARMOR_SHIELD };
            r_weapons = { ITEM_SUBCLASS_ARMOR_TOTEM };
        }
        else if (m_weightScales[spec].info.name == "enhance")
        {
            mh_weapons = { ITEM_SUBCLASS_WEAPON_MACE2, ITEM_SUBCLASS_WEAPON_AXE2 };
            r_weapons = { ITEM_SUBCLASS_ARMOR_TOTEM };
        }
        else
        {
            mh_weapons = { ITEM_SUBCLASS_WEAPON_STAFF, ITEM_SUBCLASS_WEAPON_DAGGER, ITEM_SUBCLASS_WEAPON_AXE, ITEM_SUBCLASS_WEAPON_MACE, ITEM_SUBCLASS_WEAPON_FIST };
            oh_weapons = { ITEM_SUBCLASS_ARMOR_MISC, ITEM_SUBCLASS_ARMOR_SHIELD };
            r_weapons = { ITEM_SUBCLASS_ARMOR_TOTEM };
        }
        break;
    }
    case CLASS_MAGE:
    case CLASS_WARLOCK:
    {
        mh_weapons = { ITEM_SUBCLASS_WEAPON_STAFF, ITEM_SUBCLASS_WEAPON_DAGGER, ITEM_SUBCLASS_WEAPON_SWORD };
        oh_weapons = { ITEM_SUBCLASS_ARMOR_MISC };
        r_weapons = { ITEM_SUBCLASS_WEAPON_WAND };
        break;
    }
    case CLASS_DRUID:
    {
        if (m_weightScales[spec].info.name == "feraltank")
        {
            mh_weapons = { ITEM_SUBCLASS_WEAPON_STAFF, ITEM_SUBCLASS_WEAPON_MACE2, ITEM_SUBCLASS_WEAPON_DAGGER, ITEM_SUBCLASS_WEAPON_MACE };
            oh_weapons = { ITEM_SUBCLASS_ARMOR_MISC };
            r_weapons = { ITEM_SUBCLASS_ARMOR_IDOL };
        }
        else if (m_weightScales[spec].info.name == "resto")
        {
            mh_weapons = { ITEM_SUBCLASS_WEAPON_STAFF, ITEM_SUBCLASS_WEAPON_DAGGER, ITEM_SUBCLASS_WEAPON_MACE, ITEM_SUBCLASS_WEAPON_MACE2 };
            oh_weapons = { ITEM_SUBCLASS_ARMOR_MISC };
            r_weapons = { ITEM_SUBCLASS_ARMOR_IDOL };
        }
        else if (m_weightScales[spec].info.name == "feraldps")
        {
            mh_weapons = { ITEM_SUBCLASS_WEAPON_STAFF, ITEM_SUBCLASS_WEAPON_MACE2, ITEM_SUBCLASS_WEAPON_MACE };
            oh_weapons = { ITEM_SUBCLASS_ARMOR_MISC };
            r_weapons = { ITEM_SUBCLASS_ARMOR_IDOL };
        }
        else
        {
            mh_weapons = { ITEM_SUBCLASS_WEAPON_DAGGER, ITEM_SUBCLASS_WEAPON_MACE, ITEM_SUBCLASS_WEAPON_STAFF, ITEM_SUBCLASS_WEAPON_MACE2 };
            oh_weapons = { ITEM_SUBCLASS_ARMOR_MISC };
            r_weapons = { ITEM_SUBCLASS_ARMOR_IDOL };
        }
        break;
    }
    }

    if (slot_mh == EQUIPMENT_SLOT_MAINHAND)
    {
        return mh_weapons.find(proto->SubClass) != mh_weapons.end();
    }
    if (slot_oh == EQUIPMENT_SLOT_OFFHAND)
    {
        return oh_weapons.find(proto->SubClass) != oh_weapons.end();
    }
    if (slot_rh == EQUIPMENT_SLOT_RANGED)
    {
        return r_weapons.find(proto->SubClass) != r_weapons.end();
    }

    return false;
}

bool RandomItemMgr::CanEquipWeapon(uint8 clazz, ItemPrototype const* proto)
{
    switch (clazz)
    {
    case CLASS_PRIEST:
        if (proto->SubClass != ITEM_SUBCLASS_WEAPON_STAFF &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_WAND &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_MACE)
            return false;
        break;
    case CLASS_MAGE:
    case CLASS_WARLOCK:
        if (proto->SubClass != ITEM_SUBCLASS_WEAPON_STAFF &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_WAND &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_SWORD)
            return false;
        break;
    case CLASS_WARRIOR:
        if (proto->SubClass != ITEM_SUBCLASS_WEAPON_MACE2 &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_SWORD2 &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_MACE &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_SWORD &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_GUN &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_CROSSBOW &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_BOW &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_AXE &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_AXE2 &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_THROWN)
            return false;
        break;
    case CLASS_PALADIN:
        if (proto->SubClass != ITEM_SUBCLASS_WEAPON_MACE2 &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_SWORD2 &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_MACE &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_SWORD)
            return false;
        break;
    case CLASS_SHAMAN:
        if (proto->SubClass != ITEM_SUBCLASS_WEAPON_MACE &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_MACE2 &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_STAFF)
            return false;
        break;
    case CLASS_DRUID:
        if (proto->SubClass != ITEM_SUBCLASS_WEAPON_MACE &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_MACE2 &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_DAGGER &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_STAFF)
            return false;
        break;
    case CLASS_HUNTER:
        if (proto->SubClass != ITEM_SUBCLASS_WEAPON_AXE2 &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_SWORD2 &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_GUN &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_CROSSBOW &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_BOW)
            return false;
        break;
    case CLASS_ROGUE:
        if (proto->SubClass != ITEM_SUBCLASS_WEAPON_DAGGER &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_SWORD &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_MACE &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_GUN &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_CROSSBOW &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_BOW &&
                proto->SubClass != ITEM_SUBCLASS_WEAPON_THROWN)
            return false;
        break;
    }

    return true;
}

void RandomItemMgr::BuildItemInfoCache()
{
    for (auto& [key, itemInfo] : itemInfoCache)
        if (itemInfo)
            delete itemInfo;

    uint32 maxLevel = sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL);

    for (uint32 i = 0; i <= MAX_STAT_SCALES; ++i)
    {
        WeightScale scale;
        scale.info.id = 0;
        scale.info.name = "";
        scale.info.classId = 0;
        m_weightScales[i] = scale;
    }

    // load weightscales
    sLog.outString("Loading weightscales info");
    auto results = WorldDatabase.PQuery("select id, name, class from ai_playerbot_weightscales");

    if (results)
    {
        int totalcount = 0;
        int statcount = 0;
        int curClass = CLASS_WARRIOR;

        do
        {
            Field* fields = results->Fetch();
            uint32 id = fields[0].GetUInt32();
            std::string name = fields[1].GetString();
            uint32 clazz = fields[2].GetUInt32();

            WeightScale scale;
            scale.info.id = id;
            scale.info.name = name;
            scale.info.classId = clazz;
            m_weightScales[id] = scale;
            totalcount++;

        } while (results->NextRow());

        sLog.outString("Loaded %d weightscale class specs", totalcount);

        auto result = WorldDatabase.PQuery("select id, field, val from ai_playerbot_weightscale_data");
        if (result)
        {
            do
            {
                Field* fields = result->Fetch();
                uint32 id = fields[0].GetUInt32();
                std::string field = fields[1].GetString();
                uint32 weight = fields[2].GetUInt32();

                WeightScaleStat stat;
                stat.stat = field;
                stat.weight = weight;

                m_weightScales[id].stats.push_back(stat);
                statcount++;

            } while (result->NextRow());
        }

        sLog.outString("Loaded %d weightscale stat weights", statcount);
    }

    if (m_weightScales[1].stats.empty())
    {
        sLog.outInfo("TortoiseBots: optional item weight scales are empty; random gear scoring is unavailable");
        return;
    }

    // vendor items
    sLog.outString("Loading vendor item list...");
    std::vector<uint32> vendorItems;
    std::vector<uint32> allianceItems;
    std::vector<uint32> hordeItems;
    vendorItems.clear();
    if (auto result = WorldDatabase.PQuery("%s", "SELECT item, entry FROM npc_vendor"))
    {
        BarGoLink bar(result->GetRowCount());
        do
        {
            bar.step();
            Field* fields = result->Fetch();
            uint32 entry = fields[0].GetUInt32();
            if (!entry)
                continue;
            vendorItems.push_back(fields[0].GetUInt32());

            uint32 vendorId = fields[1].GetUInt32();
            if (vendorId)
            {
                if (vendorId == 12782 || vendorId == 12777 || vendorId == 12785)
                    allianceItems.push_back(entry);
                if (vendorId == 14581 || vendorId == 12792 || vendorId == 12794)
                    hordeItems.push_back(entry);
            }
        } while (result->NextRow());
    }
    sLog.outString("Loaded %d vendor items...", (uint32)vendorItems.size());
    sLog.outString("Loaded %d alliance only vendor items...", (uint32)allianceItems.size());
    sLog.outString("Loaded %d horde only vendor items...", (uint32)hordeItems.size());

    // calculate drop source
    sLog.outString("Loading loot templates...");
    DropMap* dropMap = new DropMap;

    int32 sEntry;

    for (uint32 entry = 0; entry < sCreatureStorage.GetMaxEntry(); entry++)
    {
        sEntry = entry;

        LootTemplateAccess const* lTemplateA = DropMapValue::GetLootTemplate(ObjectGuid(HIGHGUID_UNIT, entry, uint32(1)), LOOT_CORPSE);

        if (lTemplateA)
            for (LootStoreItem const& lItem : lTemplateA->Entries)
                dropMap->insert(std::make_pair(lItem.itemid, sEntry));
    }

    for (uint32 entry = 0; entry < sGOStorage.GetMaxEntry(); entry++)
    {
        sEntry = entry;

        LootTemplateAccess const* lTemplateA = DropMapValue::GetLootTemplate(ObjectGuid(HIGHGUID_GAMEOBJECT, entry, uint32(1)), LOOT_CORPSE);

        if (lTemplateA)
            for (LootStoreItem const& lItem : lTemplateA->Entries)
                dropMap->insert(std::make_pair(lItem.itemid, -sEntry));
    }

    sLog.outString("Loaded %d loot templates...", (uint32)dropMap->size());

    sLog.outString("Calculating stat weights for %d items...", sItemStorage.GetMaxEntry());
    BarGoLink bar(sItemStorage.GetMaxEntry());

    CharacterDatabase.BeginTransaction();

    // generate stat weights for classes/specs
    for (uint32 itemId = 0; itemId < sItemStorage.GetMaxEntry(); ++itemId)
    {
        bar.step();

        ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
        if (!proto)
            continue;

        // skip non armor/weapon
        if (proto->Class != ITEM_CLASS_WEAPON &&
            proto->Class != ITEM_CLASS_ARMOR &&
            proto->Class != ITEM_CLASS_CONTAINER &&
            proto->Class != ITEM_CLASS_PROJECTILE)
            continue;

        if (!CanEquipItemNew(proto))
            continue;

        // skip test items
        if (strstr(proto->Name1, "(Test)") ||
            strstr(proto->Name1, "(TEST)") ||
            strstr(proto->Name1, "(test)") ||
            strstr(proto->Name1, "(JEFFTEST)") ||
            strstr(proto->Name1, "Test ") ||
            strstr(proto->Name1, "Test") ||
            strstr(proto->Name1, "TEST") ||
            strstr(proto->Name1, "TEST ") ||
            strstr(proto->Name1, " TEST") ||
            strstr(proto->Name1, "2200 ") ||
            strstr(proto->Name1, "Deprecated ") ||
            strstr(proto->Name1, "Unused ") ||
            strstr(proto->Name1, "Monster ") ||
            strstr(proto->Name1, "[PH]") ||
            strstr(proto->Name1, "(OLD)") ||
            strstr(proto->Name1, "zzz") ||
            strstr(proto->Name1, "ZZZ")
            )
            continue;

        // skip items with rank/rep requirements
        /*if (proto->RequiredHonorRank > 0 ||
            proto->RequiredSkillRank > 0 ||
            proto->RequiredCityRank > 0 ||
            proto->RequiredReputationRank > 0)
            continue;*/

        /*if (proto->RequiredHonorRank > 0 ||
            proto->RequiredSkillRank > 0 ||
            proto->RequiredCityRank > 0)
            continue;*/


        // check possible equip slots
        EquipmentSlots slot = EQUIPMENT_SLOT_END;
        for (std::map<EquipmentSlots, std::set<InventoryType> >::iterator i = viableSlots.begin(); i != viableSlots.end(); ++i)
        {
            std::set<InventoryType> slots = viableSlots[(EquipmentSlots)i->first];
            if (slots.find((InventoryType)proto->InventoryType) != slots.end())
                slot = i->first;
        }

        if (slot == EQUIPMENT_SLOT_END)
            continue;

        // Init Item cache
        ItemInfoEntry* cacheInfo = new ItemInfoEntry;
        uint32 itemSpec = ITEM_SPEC_NONE;

        // check faction
        if (!cacheInfo->team && proto->AllowableRace > 1 && proto->AllowableRace < 8388607)
        {
            if (FactionEntry const* faction = sFactionStore.LookupEntry(HORDE))
                if ((proto->AllowableRace & faction->BaseRepRaceMask[0]) != 0)
                    cacheInfo->team = HORDE;

            if (FactionEntry const* faction = sFactionStore.LookupEntry(ALLIANCE))
                if ((proto->AllowableRace & faction->BaseRepRaceMask[0]) != 0)
                    cacheInfo->team = ALLIANCE;
        }

        // PvP vendors
        if (std::find(allianceItems.begin(), allianceItems.end(), proto->ItemId) != allianceItems.end())
            cacheInfo->team = ALLIANCE;
        if (std::find(hordeItems.begin(), hordeItems.end(), proto->ItemId) != hordeItems.end())
            cacheInfo->team = HORDE;

        // check min level
        if (proto->RequiredLevel)
            cacheInfo->minLevel = proto->RequiredLevel;

        // check item source

        if (proto->Flags & ITEM_FLAG_NO_DISENCHANT)
        {
            cacheInfo->source = ITEM_SOURCE_PVP;
            sLog.outDetail("Item: %d, source: PvP Reward", proto->ItemId);
        }

        // check quests
        if (cacheInfo->source == ITEM_SOURCE_NONE || cacheInfo->source == ITEM_SOURCE_PVP)
        {
            std::vector<uint32> questIds = GetQuestIdsForItem(proto->ItemId);
            if (questIds.size())
            {
                bool isAlly = false;
                bool isHorde = false;
                for (std::vector<uint32>::iterator i = questIds.begin(); i != questIds.end(); ++i)
                {
                    Quest const* quest = sObjectMgr.GetQuestTemplate(*i);
                    if (quest)
                    {
                        cacheInfo->source = ITEM_SOURCE_QUEST;
                        cacheInfo->sourceIds.push_back(*i);
                        if (!cacheInfo->minLevel)
                            cacheInfo->minLevel = quest->GetQuestLevel();

                        // check quest team
                        if (!cacheInfo->team)
                        {
                            uint32 reqRace = quest->GetRequiredRaces();
                            if (reqRace)
                            {
                                if ((reqRace & RACEMASK_ALLIANCE) != 0)
                                    isAlly = true;
                                if ((reqRace & RACEMASK_HORDE) != 0)
                                    isHorde = true;
                            }
                        }

                        if (quest->GetRequiredMinRepFaction())
                        {
                            cacheInfo->repFaction = quest->GetRequiredMinRepFaction();
                            int r = 0;
                            for (; r < MAX_REPUTATION_RANK; ++r)
                            {
                                if (quest->GetRequiredMinRepValue() == ReputationMgr::PointsInRank[r])
                                    cacheInfo->repRank = uint32(r);
                            }
                            if (FactionEntry const* faction = sFactionStore.LookupEntry(quest->GetRequiredMinRepFaction()))
                            {
                                if (faction->team == ALLIANCE)
                                    cacheInfo->team = ALLIANCE;
                                if (faction->team == HORDE)
                                    cacheInfo->team = HORDE;
                            }
                        }
                    }
                }

                if (!cacheInfo->team)
                {
                    if (isAlly && isHorde)
                        cacheInfo->team = TEAM_BOTH_ALLOWED;
                    else if (isAlly)
                        cacheInfo->team = ALLIANCE;
                    else if (isHorde)
                        cacheInfo->team = HORDE;
                }

                sLog.outDetail("Item: %d, team (quest): %s", proto->ItemId, cacheInfo->team == ALLIANCE ? "Alliance" : cacheInfo->team == HORDE ? "Horde" : "Both");
                sLog.outDetail("Item: %d, source: quest %d, minlevel: %d", proto->ItemId, cacheInfo->sourceIds.front(), cacheInfo->minLevel);
            }
        }

        if (cacheInfo->minLevel)
            sLog.outDetail("Item: %d, minlevel: %d", proto->ItemId, cacheInfo->minLevel);

        // check vendors
        if (cacheInfo->source == ITEM_SOURCE_NONE || cacheInfo->source == ITEM_SOURCE_PVP)
        {
            bool isAlly = false;
            bool isHorde = false;
            for (auto& vendor : GAI_VALUE2(std::list<int32>, "item vendor list", itemId))
            {
                CreatureInfo const* cInfo = sObjectMgr.GetCreatureTemplate(vendor);
                if (!cInfo)
                    continue;

                cacheInfo->source = ITEM_SOURCE_VENDOR;
                cacheInfo->sourceIds.push_back(vendor);

                FactionTemplateEntry const* factionEntry = sFactionTemplateStore.LookupEntry(cInfo->faction);
                if (PlayerbotAI::friendToAlliance(factionEntry))
                    isAlly = true;
                if (PlayerbotAI::friendToHorde(factionEntry))
                    isHorde = true;

                // check faction conditions
                VendorItemData const* vItems = sObjectMgr.GetNpcVendorItemList(vendor);
                VendorItemData const* tItems = sObjectMgr.GetNpcVendorTemplateItemList(cInfo->vendor_id);

                if (vItems || tItems)
                {
                    uint8 customitems = vItems ? vItems->GetItemCount() : 0;
                    uint8 numitems = customitems + (tItems ? tItems->GetItemCount() : 0);
                    for (int i = 0; i < numitems; ++i)
                    {
                        VendorItem const* crItem = i < customitems ? vItems->GetItem(i) : tItems->GetItem(i - customitems);
                        if (!crItem || !crItem->conditionId)
                            continue;

                        if (auto result = WorldDatabase.PQuery("SELECT type, value1, value2 FROM conditions WHERE condition_entry = '%u'", crItem->conditionId))
                        {
                            do
                            {
                                Field *fields = result->Fetch();
                                uint32 m_type = fields[0].GetUInt32();
                                if (m_type != CONDITION_REPUTATION_RANK_MIN)
                                    continue;

                                uint32 m_value1 = fields[1].GetUInt32();
                                uint32 m_value2 = fields[2].GetUInt32();

                                if (FactionEntry const* faction = sFactionStore.LookupEntry(m_value1))
                                {
                                    cacheInfo->repFaction = m_value1;
                                    cacheInfo->repRank = m_value2;
                                }
                            } while (result->NextRow());
                        }
                    }
                }
            }

            if (!cacheInfo->team)
            {
                if (isAlly && isHorde)
                    cacheInfo->team = TEAM_BOTH_ALLOWED;
                else if (isAlly)
                    cacheInfo->team = ALLIANCE;
                else if (isHorde)
                    cacheInfo->team = HORDE;
            }

            if (cacheInfo->source == ITEM_SOURCE_VENDOR)
                sLog.outDetail("Item: %d, source: vendor", proto->ItemId);
        }

        if (cacheInfo->team)
            sLog.outDetail("Item: %d, team (item): %s", proto->ItemId, cacheInfo->team == ALLIANCE ? "Alliance" : "Horde");

        // check drops
        std::list<int32> creatures;
        std::list<int32> gameobjects;

        auto range = dropMap->equal_range(itemId);

        for (auto itr = range.first; itr != range.second; ++itr)
        {
            if (itr->second > 0)
                creatures.push_back(itr->second);
            else
                gameobjects.push_back(abs(itr->second));
        }

        // check creature drop
        if (cacheInfo->source == ITEM_SOURCE_NONE)
        {
            if (creatures.size())
            {
                if (creatures.size() == 1)
                {
                    cacheInfo->source = ITEM_SOURCE_DROP;
                    cacheInfo->sourceIds.push_back(creatures.front());
                    sLog.outDetail("Item: %d, source: creature drop, ID: %d", proto->ItemId, creatures.front());
                }
                else
                {
                    cacheInfo->source = ITEM_SOURCE_DROP;
                    sLog.outDetail("Item: %d, source: creatures drop, number: %d", proto->ItemId, (uint32)creatures.size());
                }
            }
        }

        // check gameobject drop
        if (cacheInfo->source == ITEM_SOURCE_NONE || (cacheInfo->source == ITEM_SOURCE_DROP && cacheInfo->sourceIds.empty()))
        {
            if (gameobjects.size())
            {
                if (gameobjects.size() == 1)
                {
                    cacheInfo->source = ITEM_SOURCE_DROP;
                    cacheInfo->sourceIds.push_back(gameobjects.front());
                    sLog.outDetail("Item: %d, source: gameobject, ID: %d", proto->ItemId, gameobjects.front());
                }
                else
                {
                    cacheInfo->source = ITEM_SOURCE_DROP;
                    sLog.outDetail("Item: %d, source: gameobjects, number: %d", proto->ItemId, (uint32)gameobjects.size());
                }
            }
        }

        // check faction
        if (proto->RequiredReputationFaction > 0 && proto->RequiredReputationFaction != 35 && proto->RequiredReputationRank < 15)
        {
            cacheInfo->repFaction = proto->RequiredReputationFaction;
            cacheInfo->repRank = proto->RequiredReputationRank;
        }

        // check honor rank
        if (proto->RequiredHonorRank > 0)
        {
            cacheInfo->pvpRank = proto->RequiredHonorRank;
        }

        // check skill
        if (proto->RequiredSkill > 0)
        {
            cacheInfo->reqSkill = proto->RequiredSkill;
            cacheInfo->reqSkillRank = proto->RequiredSkillRank;
        }

        cacheInfo->quality = proto->Quality;
        cacheInfo->itemId = proto->ItemId;
        cacheInfo->slot = slot;
        cacheInfo->itemLevel = proto->ItemLevel;

        // Owner-rule source tier (lowest-tier source wins) + REP/PVP flags.
        ClassifySourceTier(proto, cacheInfo);

        // calculate stat weights
        for (uint8 clazz = CLASS_WARRIOR; clazz < MAX_CLASSES; ++clazz)
        {
            // skip nonexistent classes
            if (!((1 << (clazz - 1)) & CLASSMASK_ALL_PLAYABLE) || !sChrClassesStore.LookupEntry(clazz))
                continue;

            // skip wrong classes
            if ((proto->AllowableClass & (1 << (clazz - 1))) == 0)
                continue;

            for (uint32 spec = 1; spec <= MAX_STAT_SCALES; ++spec)
            {
                if (!m_weightScales[spec].info.id)
                    continue;

                if (m_weightScales[spec].info.classId != clazz)
                    continue;

                // check possible armor for spec
                if (proto->Class == ITEM_CLASS_ARMOR && (
                    slot == EQUIPMENT_SLOT_HEAD ||
                    slot == EQUIPMENT_SLOT_SHOULDERS ||
                    slot == EQUIPMENT_SLOT_CHEST ||
                    slot == EQUIPMENT_SLOT_WAIST ||
                    slot == EQUIPMENT_SLOT_LEGS ||
                    slot == EQUIPMENT_SLOT_FEET ||
                    slot == EQUIPMENT_SLOT_WRISTS ||
                    slot == EQUIPMENT_SLOT_HANDS) &&
                    !ShouldEquipArmorForSpec(clazz, spec, proto))
                    continue;

                // check possible weapon for spec
                if ((proto->Class == ITEM_CLASS_WEAPON || (proto->SubClass == ITEM_SUBCLASS_ARMOR_SHIELD || (proto->SubClass == ITEM_SUBCLASS_ARMOR_MISC && proto->InventoryType == INVTYPE_HOLDABLE))) &&
                    !ShouldEquipWeaponForSpec(clazz, spec, proto))
                    continue;

                //StatWeight statWeight;
                //statWeight.id = m_weightScales[spec].info.id;
                ItemSpecType tempSpec = ITEM_SPEC_NONE;
                uint32 statW = CalculateStatWeight(clazz, spec, proto, tempSpec);
                itemSpec |= tempSpec;
                if (!statW /*&& tempSpec == ITEM_SPEC_NONE*/ && cacheInfo->quality < ITEM_QUALITY_UNCOMMON && cacheInfo->minLevel <= 10)
                    statW = 1;

                // legendary stat x2
                if (proto->Quality >= ITEM_QUALITY_LEGENDARY && statW > 1)
                    statW *= 2;

                if (slot == EQUIPMENT_SLOT_BODY && statW <= 0)
                    statW = 1;

                if (slot == EQUIPMENT_SLOT_TABARD && statW <= 0)
                    statW = 1;

                // warriors only plate >= 40 lvl
                if (proto->SubClass == ITEM_SUBCLASS_ARMOR_MAIL && cacheInfo->minLevel >= 40 && clazz == CLASS_WARRIOR)
                    statW = 0;

                // paladin tank/dps only plate >= 40 lvl
                if (proto->SubClass == ITEM_SUBCLASS_ARMOR_MAIL && cacheInfo->minLevel >= 40 && clazz == CLASS_PALADIN && spec != 4)
                    statW = 0;

                // some trinkets have no stats
                if (cacheInfo->slot == EQUIPMENT_SLOT_TRINKET1 ||
                    cacheInfo->slot == EQUIPMENT_SLOT_TRINKET2)
                {
                    if (statW == 0 && proto->AllowableClass == uint32(clazz) && proto->Spells[0].SpellId)
                    {
                        statW = (uint32)(proto->Quality + proto->ItemLevel);
                    }
                }

                // Make wand useful
                if (!statW && cacheInfo->slot == EQUIPMENT_SLOT_RANGED && proto->SubClass == ITEM_SUBCLASS_WEAPON_WAND && (clazz == CLASS_PRIEST || clazz == CLASS_MAGE || clazz == CLASS_WARLOCK))
                    statW = 1;

                // Random properties
                if (!statW && proto->RandomProperty)
                    statW = 1;

                // set stat weight = 1 for items that can be equipped but have no proper stats
                //statWeight.weight = statW;
                // save item statWeight into ItemCache
                cacheInfo->weights[spec] = statW;
                sLog.outDetail("Item: %d, weight: %d, class: %d, spec: %s", proto->ItemId, statW, clazz, m_weightScales[spec].info.name.c_str());
            }
        }

        cacheInfo->itemSpec = (ItemSpecType)itemSpec;

        // save cache
        static SqlStatementID delCache;
        static SqlStatementID insertCache;

        SqlStatement stmt = CharacterDatabase.CreateStatement(delCache, "DELETE FROM ai_playerbot_item_info_cache WHERE id = ?");
        stmt.PExecute(proto->ItemId);

        stmt = CharacterDatabase.CreateStatement(insertCache, "INSERT INTO ai_playerbot_item_info_cache (id, quality, slot, source, sourceId, team, faction, factionRepRank, minLevel, "
            "scale_1, scale_2, scale_3, scale_4, scale_5, scale_6, scale_7, scale_8, scale_9, scale_10, scale_11, scale_12, scale_13, scale_14, scale_15, "
            "scale_16, scale_17, scale_18, scale_19, scale_20, scale_21, scale_22, scale_23, scale_24, scale_25, scale_26, scale_27, scale_28, scale_29, scale_30, scale_31, scale_32, "
            "source_tier, source_flags, world_epic)"
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");

        stmt.addUInt32(cacheInfo->itemId);
        stmt.addUInt32(cacheInfo->quality);
        stmt.addUInt32(cacheInfo->slot);
        stmt.addUInt32(cacheInfo->source);
        stmt.addUInt32(cacheInfo->sourceIds.empty() ? 0 : cacheInfo->sourceIds.front());
        stmt.addUInt32(cacheInfo->team);
        stmt.addUInt32(cacheInfo->repFaction);
        stmt.addUInt32(cacheInfo->repRank);
        stmt.addUInt32(cacheInfo->minLevel);

        for (int i = 1; i <= MAX_STAT_SCALES; ++i)
        {
            // Safety net: scale_* are signed MEDIUMINT (max 8388607). Clamp so a single
            // pathological weight can never overflow the column and abort the build.
            uint32 w = cacheInfo->weights[i] > 8388607u ? 8388607u : cacheInfo->weights[i];
            stmt.addUInt32(w);
        }

        stmt.addUInt32((uint32)cacheInfo->sourceTier);
        stmt.addUInt32((uint32)cacheInfo->sourceFlags);
        stmt.addUInt32(cacheInfo->worldEpic ? 1u : 0u);
        stmt.Execute();

        itemInfoCache[cacheInfo->itemId] = cacheInfo;
    }

    CharacterDatabase.CommitTransaction();
    delete dropMap;
}

uint32 RandomItemMgr::CalculateStatWeight(uint8 playerclass, uint8 spec, ItemPrototype const* proto, ItemSpecType& itSpec)
{
    uint32 specType = ITEM_SPEC_NONE;
    uint32 statWeight = 0;
    uint32 spellPower = 0;
    uint32 spellHeal = 0;
    uint32 attackPower = 0;
    bool isCasterItem = false;
    bool isAttackItem = false;
    bool isDpsItem = false;
    bool isTankItem = false;
    bool isHealingItem = false;
    bool isSpellDamageItem = false;
    bool hasInt = false;
    bool noCaster = (Classes)playerclass == CLASS_WARRIOR || (Classes)playerclass == CLASS_ROGUE || (Classes)playerclass == CLASS_HUNTER || spec == 30 || spec == 32 || spec == 21 || spec == 6;
    bool hasMana = !((Classes)playerclass == CLASS_WARRIOR || (Classes)playerclass == CLASS_ROGUE);

    if ((Classes)playerclass == CLASS_HUNTER && proto->SubClass == ITEM_SUBCLASS_WEAPON_THROWN)
        return (uint32)proto->ItemLevel;

    //check relicts
    if (proto->InventoryType == INVTYPE_RELIC)
    {
        if (playerclass == CLASS_PALADIN && proto->SubClass != ITEM_SUBCLASS_ARMOR_LIBRAM)
            return 0;

        if (playerclass == CLASS_DRUID && proto->SubClass != ITEM_SUBCLASS_ARMOR_IDOL)
            return 0;

        if (playerclass == CLASS_SHAMAN && proto->SubClass != ITEM_SUBCLASS_ARMOR_TOTEM)
            return 0;

        if (playerclass == CLASS_WARRIOR
            || playerclass == CLASS_HUNTER
            || playerclass == CLASS_ROGUE
            || playerclass == CLASS_PRIEST
            || playerclass == CLASS_MAGE
            || playerclass == CLASS_WARLOCK)
            return 0;

        return proto->Quality + proto->ItemLevel;
    }

    bool isWhitelist = false;
    // whitelist
    if (std::find(sPlayerbotAIConfig.randomGearWhitelist.begin(), sPlayerbotAIConfig.randomGearWhitelist.end(), proto->ItemId) != sPlayerbotAIConfig.randomGearWhitelist.end())
        isWhitelist = true;

    // whitelist pvp items, as thei have wierd stats
    if (proto->RequiredHonorRank)
        isWhitelist = true;

    // whitelist the ONLY feral Off Hand in vanilla
    if ((spec == 30 || spec == 32) && proto->ItemId == 13385)
        isWhitelist = true;

    // whitelist atiesh
    if (playerclass == CLASS_MAGE && proto->ItemId == 22589)
        isWhitelist = true;
    if (playerclass == CLASS_WARLOCK && proto->ItemId == 22630)
        isWhitelist = true;
    if (playerclass == CLASS_PRIEST && proto->ItemId == 22631)
        isWhitelist = true;
    if (playerclass == CLASS_DRUID && proto->ItemId == 22632)
        isWhitelist = true;

    // check basic item stats
    int32 basicStatsWeight = 0;
    for (int j = 0; j < MAX_ITEM_PROTO_STATS; ++j)
    {
        uint32 statType = 0;
        int32 val = 0;
        std::string weightName = "";

        //if (j >= proto->StatsCount)
        //    continue;

        statType = proto->ItemStat[j].ItemStatType;
        val = proto->ItemStat[j].ItemStatValue;

        if (val == 0)
            continue;

        for (std::map<std::string, uint32 >::iterator i = weightStatLink.begin(); i != weightStatLink.end(); ++i)
        {
            uint32 modd = i->second;
            if (modd == statType)
            {
                weightName = i->first;


                break;
            }
        }

        if (weightName.empty())
            continue;

        uint32 singleStat = CalculateSingleStatWeight(playerclass, spec, weightName, val);
        basicStatsWeight += singleStat;

        if (val)
        {
            if (weightName == "int" && !noCaster)
                isCasterItem = true;

            if (weightName == "int")
                hasInt = true;

            if (weightName == "splpwr")
                isCasterItem = true;

            if (weightName == "str")
                isAttackItem = true;

            if (weightName == "agi")
                isAttackItem = true;

            if (weightName == "atkpwr")
                isAttackItem = true;
        }
    }

    // check defensive stats
    uint32 defenseStats = 0;
    defenseStats += CalculateSingleStatWeight(playerclass, spec, "block", proto->Block);
    defenseStats += CalculateSingleStatWeight(playerclass, spec, "armor", proto->Armor);

    // check weapon dps
    if (proto->IsWeapon())
    {
        WeaponAttackType attType = BASE_ATTACK;

        uint32 dps = 0;

        for (int i = 0; i < MAX_ITEM_PROTO_DAMAGES; i++)
        {
            if (proto->Damage[i].DamageMax == 0)
                break;

            dps = (proto->Damage[i].DamageMin + proto->Damage[i].DamageMax) / (float)(proto->Delay / 1000.0f) / 2;
            if (dps)
            {
                if (proto->IsRangedWeapon())
                    statWeight += CalculateSingleStatWeight(playerclass, spec, "rgddps", dps);
                else
                    statWeight += CalculateSingleStatWeight(playerclass, spec, "mledps", dps);
            }
        }
    }

    // check item spells
    uint32 spellDamage = 0;
    uint32 spellHealing = 0;
    uint32 auraStatWeight = 0;
    uint32 auraApStatWeight = 0;
    uint32 auraHealStatWeight = 0;
    uint32 auraDamageStatWeight = 0;
    bool isFeral = false;
    for (const auto& spellData : proto->Spells)
    {
        // no spell
        if (!spellData.SpellId)
            continue;

        // apply only at-equip spells for weapons, on use/hit for armor
        if (!(spellData.SpellTrigger == ITEM_SPELLTRIGGER_ON_EQUIP || (!proto->IsWeapon() && proto->InventoryType != INVTYPE_HOLDABLE && (spellData.SpellTrigger == ITEM_SPELLTRIGGER_ON_USE || spellData.SpellTrigger == ITEM_SPELLTRIGGER_CHANCE_ON_HIT))))
            continue;

        // check if it is valid spell
        SpellEntry const* spellproto = sSpellTemplate.LookupEntry<SpellEntry>(spellData.SpellId);
        if (!spellproto)
            continue;

        bool hasAP = false;

        uint32 effectAuraStatWeight = 0;
        uint32 effectAuraApStatWeight = 0;
        uint32 effectAuraHealStatWeight = 0;
        uint32 effectAuraDamageStatWeight = 0;

        for (uint8 j = 0; j < MAX_EFFECT_INDEX; ++j)
        {
            if ((spellproto->Effect[j] == SPELL_EFFECT_APPLY_AURA) &&
                (spellproto->EffectBasePoints[j] >= 0))
            {
                // spell damage
                // SPELL_AURA_MOD_DAMAGE_DONE
                if (spellproto->EffectApplyAuraName[j] == SPELL_AURA_MOD_DAMAGE_DONE)
                {
                    spellDamage = spellproto->EffectBasePoints[j] + 1;
                    isSpellDamageItem = true;
                    // generic spell damage
                    if (spellproto->EffectMiscValue[j] == SPELL_SCHOOL_MASK_MAGIC)
                    {
                        effectAuraDamageStatWeight += CalculateSingleStatWeight(playerclass, spec, "splpwr", spellDamage);
                    }
                    else
                    {
                        uint32 specialDamage = 0;
                        if ((spellproto->EffectMiscValue[j] & SPELL_SCHOOL_MASK_ARCANE) != 0)
                            specialDamage += CalculateSingleStatWeight(playerclass, spec, "arcsplpwr", spellDamage);

                        if ((spellproto->EffectMiscValue[j] & SPELL_SCHOOL_MASK_FROST) != 0)
                            specialDamage += CalculateSingleStatWeight(playerclass, spec, "frosplpwr", spellDamage);

                        if ((spellproto->EffectMiscValue[j] & SPELL_SCHOOL_MASK_FIRE) != 0)
                            specialDamage += CalculateSingleStatWeight(playerclass, spec, "firsplpwr", spellDamage);

                        if ((spellproto->EffectMiscValue[j] & SPELL_SCHOOL_MASK_SHADOW) != 0)
                            specialDamage += CalculateSingleStatWeight(playerclass, spec, "shasplpwr", spellDamage);

                        if ((spellproto->EffectMiscValue[j] & SPELL_SCHOOL_MASK_NATURE) != 0)
                            specialDamage += CalculateSingleStatWeight(playerclass, spec, "natsplpwr", spellDamage);

                        effectAuraDamageStatWeight += specialDamage;
                    }
                }
                // spell healing
                // SPELL_AURA_MOD_HEALING_DONE
                if (spellproto->EffectApplyAuraName[j] == SPELL_AURA_MOD_HEALING_DONE)
                {
                    isHealingItem = true;
                    spellHealing = spellproto->EffectBasePoints[j] + 1;
                    effectAuraHealStatWeight += CalculateSingleStatWeight(playerclass, spec, "splheal", spellproto->EffectBasePoints[j] + 1);
                }

                // Vanilla spell hit rating
                // SPELL_AURA_MOD_SPELL_HIT_CHANCE
                if (spellproto->EffectApplyAuraName[j] == SPELL_AURA_MOD_SPELL_HIT_CHANCE)
                {
                    isCasterItem = true;
                    effectAuraStatWeight += CalculateSingleStatWeight(playerclass, spec, "spellhitrtng", spellproto->EffectBasePoints[j] + 1);
                }

                // Vanilla spell crit rating
                // SPELL_AURA_MOD_SPELL_CRIT_CHANCE, SPELL_AURA_MOD_SPELL_CRIT_CHANCE_SCHOOL
                if (spellproto->EffectApplyAuraName[j] == SPELL_AURA_MOD_SPELL_CRIT_CHANCE || spellproto->EffectApplyAuraName[j] == SPELL_AURA_MOD_SPELL_CRIT_CHANCE_SCHOOL)
                {
                    isCasterItem = true;
                    effectAuraStatWeight += CalculateSingleStatWeight(playerclass, spec, "spellcritstrkrtng", spellproto->EffectBasePoints[j] + 1);
                }

                // spell penetration
                // SPELL_AURA_MOD_TARGET_RESISTANCE
                if (spellproto->EffectApplyAuraName[j] == SPELL_AURA_MOD_TARGET_RESISTANCE)
                {
                    // check if magic type
                    if (spellproto->EffectMiscValue[j] == SPELL_SCHOOL_MASK_SPELL)
                        effectAuraStatWeight += CalculateSingleStatWeight(playerclass, spec, "spellpenrtng", abs(spellproto->EffectBasePoints[j] + 1));
                }

                // check attack power
                if (!hasAP && spellproto->EffectApplyAuraName[j] == SPELL_AURA_MOD_ATTACK_POWER)
                {
                    hasAP = true;
                    isAttackItem = true;
                    std::string SpellName = spellproto->SpellName[0];
                    if (SpellName.find("Attack Power - Feral") != std::string::npos)
                        isFeral = true;
                    if (!isWhitelist && isFeral && (playerclass != CLASS_DRUID && playerclass != CLASS_WARRIOR && playerclass != CLASS_PALADIN && proto->IsWeapon()))
                        return 0;

                    effectAuraApStatWeight += CalculateSingleStatWeight(playerclass, spec, isFeral ? "feratkpwr" : "atkpwr", spellproto->EffectBasePoints[j] + 1);
                }

                // check ranged ap
                // SPELL_AURA_MOD_RANGED_ATTACK_POWER
                if (!hasAP && spellproto->EffectApplyAuraName[j] == SPELL_AURA_MOD_RANGED_ATTACK_POWER)
                {
                    // filter non ranged classes
                    if (playerclass == CLASS_SHAMAN || (!proto->IsRangedWeapon() && playerclass != CLASS_HUNTER))
                        return 0;

                    hasAP = true;
                    isAttackItem = true;
                    effectAuraApStatWeight += CalculateSingleStatWeight(playerclass, spec, "atkpwr", spellproto->EffectBasePoints[j] + 1);
                }

                // check block
                if (spellproto->EffectApplyAuraName[j] == SPELL_AURA_MOD_SHIELD_BLOCKVALUE)
                {
                    isTankItem = true;
                    effectAuraStatWeight += CalculateSingleStatWeight(playerclass, spec, "block", spellproto->EffectBasePoints[j] + 1);
                }

                if (spellproto->EffectApplyAuraName[j] == SPELL_AURA_MOD_PARRY_PERCENT)
                {
                    isTankItem = true;
                    effectAuraStatWeight += CalculateSingleStatWeight(playerclass, spec, "parryrtng", spellproto->EffectBasePoints[j] + 1);
                }

                if (spellproto->EffectApplyAuraName[j] == SPELL_AURA_MOD_DODGE_PERCENT)
                {
                    isTankItem = true;
                    effectAuraStatWeight += CalculateSingleStatWeight(playerclass, spec, "dodgertng", spellproto->EffectBasePoints[j] + 1);
                }

                // block chance
                // SPELL_AURA_MOD_BLOCK_PERCENT
                if (spellproto->EffectApplyAuraName[j] == SPELL_AURA_MOD_BLOCK_PERCENT)
                {
                    isTankItem = true;
                    effectAuraStatWeight += CalculateSingleStatWeight(playerclass, spec, "blockrtng", spellproto->EffectBasePoints[j] + 1);
                }

                // armor penetration
                // SPELL_AURA_MOD_TARGET_RESISTANCE
                if (spellproto->EffectApplyAuraName[j] == SPELL_AURA_MOD_TARGET_RESISTANCE)
                {
                    // check if physical type
                    if (spellproto->EffectMiscValue[j] == SPELL_SCHOOL_MASK_NORMAL)
                        effectAuraStatWeight += CalculateSingleStatWeight(playerclass, spec, "armorpenrtng", abs(spellproto->EffectBasePoints[j] + 1));
                }

                // Vanilla hit rating
                // SPELL_AURA_MOD_HIT_CHANCE
                if (spellproto->EffectApplyAuraName[j] == SPELL_AURA_MOD_HIT_CHANCE)
                {
                    isAttackItem = true;
                    effectAuraStatWeight += CalculateSingleStatWeight(playerclass, spec, "hitrtng", spellproto->EffectBasePoints[j] + 1);
                }

                // Vanilla crit rating
                // SPELL_AURA_MOD_HIT_CHANCE
                if (spellproto->EffectApplyAuraName[j] == SPELL_AURA_MOD_CRIT_PERCENT)
                {
                    isAttackItem = true;
                    effectAuraStatWeight += CalculateSingleStatWeight(playerclass, spec, "critstrkrtng", spellproto->EffectBasePoints[j] + 1);
                }

                //check defense SPELL_AURA_MOD_SKILL
                if (spellproto->EffectApplyAuraName[j] == SPELL_AURA_MOD_SKILL)
                {
                    if (spellproto->EffectMiscValue[j] == SKILL_DEFENSE)
                    {
                        isTankItem = true;
                        effectAuraStatWeight += CalculateSingleStatWeight(playerclass, spec, "defrtng", spellproto->EffectBasePoints[j] + 1);
                    }
                }


                // mana regen
                // SPELL_AURA_MOD_POWER_REGEN
                if (spellproto->EffectApplyAuraName[j] == SPELL_AURA_MOD_POWER_REGEN)
                {
                    isCasterItem = true;
                    effectAuraStatWeight += CalculateSingleStatWeight(playerclass, spec, "manargn", spellproto->EffectBasePoints[j] + 1);
                }
            }
        }

        // different stat weight based on trigger
        float coverage = 1;

        if (spellData.SpellTrigger == ITEM_SPELLTRIGGER_ON_USE)
        {
            if (spellData.SpellCooldown != 0)
            {
                int32 spellDuration = GetSpellDuration(spellproto);
                coverage = static_cast<float>(spellDuration) / spellData.SpellCooldown;
            }
            else
            {
                coverage = 0.17f; //Most often trinkets have 20 seconds buff with 2 minute cooldown which means ~17%
            }
        }
        else if (spellData.SpellTrigger == ITEM_SPELLTRIGGER_CHANCE_ON_HIT)
        {
            float averageItemDelay = 2.43f;
            coverage = (static_cast<float>(spellproto->procChance) / 100) * (static_cast<float>(GetSpellDuration(spellproto)) / averageItemDelay);

            if (coverage > 0.9f)
                coverage = 0.9f;
        }

        effectAuraStatWeight *= coverage;
        effectAuraHealStatWeight *= coverage;
        effectAuraApStatWeight *= coverage;
        effectAuraDamageStatWeight *= coverage;

        auraStatWeight += effectAuraStatWeight;
        auraHealStatWeight += effectAuraHealStatWeight;
        auraApStatWeight += effectAuraApStatWeight;
        auraDamageStatWeight += effectAuraDamageStatWeight;
    }

    // skip all 1h Maces for feral druids if they have no feral AP
    if (!isWhitelist && !isFeral && playerclass == CLASS_DRUID && proto->IsWeapon() && proto->SubClass == ITEM_SUBCLASS_WEAPON_MACE && (spec == 30 || spec == 32))
        return 0;

    statWeight += auraStatWeight;
    spellHeal += auraHealStatWeight;
    spellPower += auraDamageStatWeight;
    attackPower += auraApStatWeight;

    if (spellHeal > spellPower || isHealingItem)
        specType |= ITEM_SPEC_SPELL_HEALING;

    if (spellPower >= spellHeal)
        specType |= ITEM_SPEC_SPELL_DAMAGE;

    if (isTankItem && (noCaster || !hasMana || !spellHeal || (!isHealingItem && !isSpellDamageItem)))
        specType |= ITEM_SPEC_TANK;

    if (isAttackItem)
        specType |= ITEM_SPEC_ATTACK;

    if (!noCaster && (isCasterItem || hasInt || isSpellDamageItem))
        specType |= ITEM_SPEC_CASTER;


    // limit speed for tank weapons
    if (!isWhitelist && spec == 3 && proto->IsWeapon() && proto->Delay > 2300)
        return 0;

    if (!isWhitelist && spec == 5 && proto->IsWeapon() && proto->Delay > 2400)
        return 0;

    // check for caster item
    if (isCasterItem || hasInt || spellHeal || spellPower || isSpellDamageItem || isHealingItem)
    {
        if (!isWhitelist && (!hasMana || (noCaster && !(spec == 6 || spec == 30 || spec == 32 || spec == 21))) && (spellHeal || isHealingItem || isSpellDamageItem || spellPower))
            return 0;

        if (!isWhitelist && !hasMana && hasInt)
            return 0;

        if (!isWhitelist && !hasMana && noCaster && (spellPower > attackPower || spellHeal > attackPower))
            return 0;

        if (!isWhitelist && (spec != 6 && spec != 21) && !spellPower && !spellHeal && isSpellDamageItem)
            return 0;

        if (!isWhitelist && /*(spec != 6 && spec != 21) && */!spellHeal && isHealingItem && !isSpellDamageItem)
            return 0;

        if (!isWhitelist && (spec != 6 && spec != 21) && !noCaster && isSpellDamageItem && !spellPower && !(spellDamage && spellHealing && proto->IsWeapon() && proto->InventoryType == INVTYPE_WEAPONMAINHAND))
            return 0;

        bool playerCaster = false;
        for (std::vector<WeightScaleStat>::iterator i = m_weightScales[spec].stats.begin(); i != m_weightScales[spec].stats.end(); ++i)
        {
            if (i->stat == "splpwr" || i->stat == "int" || i->stat == "manargn" || i->stat == "splheal" || i->stat == "spellcritstrkrtng" || i->stat == "spellhitrtng")
            {
                playerCaster = true;
            }
        }

        if (!isWhitelist && (spec != 6 && spec != 21 && playerclass != CLASS_HUNTER) && !playerCaster)
            return 0;
    }

    // check for caster item
    if (isAttackItem)
    {
        if (!isWhitelist && hasMana && !noCaster && !(hasInt || spellPower || spellHeal || isHealingItem || isSpellDamageItem))
            return 0;

        bool playerAttacker = false;
        for (std::vector<WeightScaleStat>::iterator i = m_weightScales[spec].stats.begin(); i != m_weightScales[spec].stats.end(); ++i)
        {
            if (i->stat == "str" || i->stat == "agi" || i->stat == "atkpwr" || i->stat == "mledps" || i->stat == "rgddps" || i->stat == "hitrtng" || i->stat == "critstrkrtng")
            {
                playerAttacker = true;
            }
        }

        if (!isWhitelist && !playerAttacker)
            return 0;
    }


    itSpec = (ItemSpecType)specType;

    statWeight += spellPower;
    statWeight += spellHeal;
    statWeight += attackPower;
    statWeight += defenseStats;

    // handle negative stats
    if (basicStatsWeight < 0 && ((uint32)(abs(basicStatsWeight)) >= statWeight))
        statWeight = 0;
    else
        statWeight += basicStatsWeight;

    return statWeight;
}

uint32 RandomItemMgr::CalculateRandomEnchantId(uint8 playerclass, uint8 spec, ItemPrototype const* proto)
{
    if (!proto)
        return 0;

    // Random Property case
    if (proto->RandomProperty)
    {
        uint32 randomPropId = GetItemEnchantMod(proto->RandomProperty);
        ItemRandomPropertiesEntry const* random_id = sItemRandomPropertiesStore.LookupEntry(randomPropId);
        if (!random_id)
        {
            sLog.outErrorDb("Enchantment id #%u used but it doesn't have records in 'ItemRandomProperties.dbc'", randomPropId);
            return 0;
        }

        // check stats
        if (CalculateEnchantWeight(playerclass, spec, random_id->ID))
            return random_id->ID;
    }

    return 0;
}

uint32 RandomItemMgr::CalculateBestRandomEnchantId(uint8 playerclass, uint8 spec, uint32 itemId)
{
    if (!itemId)
        return 0;

    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
    if (!proto)
        return 0;

    std::map<uint32, std::vector<uint32> >::const_iterator tab = randomEnchantsCache.find(proto->RandomProperty);
    if (tab == randomEnchantsCache.end())
        return 0;

    uint32 bestScore = 0;
    uint32 bestId = 0;

    const std::vector<uint32> propList = tab->second;
    for (auto propId : propList)
    {
        ItemRandomPropertiesEntry const* random_id = sItemRandomPropertiesStore.LookupEntry(propId);
        if (!random_id)
            continue;

        uint32 currScore = 0;
        for (uint32 i = PROP_ENCHANTMENT_SLOT_0; i < PROP_ENCHANTMENT_SLOT_0 + 3; ++i)
        {
            uint32 enchantId = random_id->enchant_id[i - PROP_ENCHANTMENT_SLOT_0];
            currScore += CalculateEnchantWeight(playerclass, spec, enchantId);
        }

        if (currScore > bestScore)
        {
            bestScore = currScore;
            bestId = random_id->ID;;
        }
    }

    return bestId;
}

uint32 RandomItemMgr::CalculateEnchantWeight(uint8 playerclass, uint8 spec, uint32 enchantId)
{
    if (!enchantId)
        return 0;

    SpellItemEnchantmentEntry const* pEnchant = sSpellItemEnchantmentStore.LookupEntry(enchantId);

    if (!pEnchant)
        return 0;

    uint32 weight = 0;

    for (int s = 0; s < 3; ++s)
    {
        switch (pEnchant->type[s])
        {
        case 2: //Damage
            if (!pEnchant->amount[s])
                continue;
            // Weapon-damage enchants (incl. scopes): melee specs score them
            // on "mledps", ranged specs on "rgddps". One of the two always
            // exists, so hunter scopes rank above zero and march up the
            // ladder via the tie-break instead of returning "no enchant".
            weight += CalculateSingleStatWeight(playerclass, spec, "mledps", pEnchant->amount[s]);
            weight += CalculateSingleStatWeight(playerclass, spec, "rgddps", pEnchant->amount[s]);
            break;
        case 3:
        {
            if (!pEnchant->spellid[s])
                continue;

            SpellEntry const* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(pEnchant->spellid[s]);

            if (!spellInfo)
                continue;

            for (uint32 j = 0; j < MAX_EFFECT_INDEX; ++j)
            {
                if (spellInfo->Effect[j] != SPELL_EFFECT_APPLY_AURA)
                    continue;

                if (spellInfo->EffectApplyAuraName[j] == SPELL_AURA_MOD_STAT)
                {
                    uint32 stat = spellInfo->EffectMiscValue[j];
                    uint32 value = spellInfo->EffectBasePoints[j] + 1;

                    if (!value)
                        continue;

                    if (ItemStatLink.find(stat) == ItemStatLink.end())
                        continue;

                    weight += CalculateSingleStatWeight(playerclass, spec, ItemStatLink[stat], value);
                }
                // spell damage
                // SPELL_AURA_MOD_DAMAGE_DONE
                if (spellInfo->EffectApplyAuraName[j] == SPELL_AURA_MOD_DAMAGE_DONE)
                {
                    uint32 spellDamage = spellInfo->EffectBasePoints[j] + 1;
                    // generic spell damage
                    if (spellInfo->EffectMiscValue[j] == SPELL_SCHOOL_MASK_MAGIC)
                    {
                        weight += CalculateSingleStatWeight(playerclass, spec, "splpwr", spellDamage);
                    }
                    else
                    {
                        uint32 specialDamage = 0;
                        if ((spellInfo->EffectMiscValue[j] & SPELL_SCHOOL_MASK_ARCANE) != 0)
                            specialDamage += CalculateSingleStatWeight(playerclass, spec, "arcsplpwr", spellDamage);

                        if ((spellInfo->EffectMiscValue[j] & SPELL_SCHOOL_MASK_FROST) != 0)
                            specialDamage += CalculateSingleStatWeight(playerclass, spec, "frosplpwr", spellDamage);

                        if ((spellInfo->EffectMiscValue[j] & SPELL_SCHOOL_MASK_FIRE) != 0)
                            specialDamage += CalculateSingleStatWeight(playerclass, spec, "firsplpwr", spellDamage);

                        if ((spellInfo->EffectMiscValue[j] & SPELL_SCHOOL_MASK_SHADOW) != 0)
                            specialDamage += CalculateSingleStatWeight(playerclass, spec, "shasplpwr", spellDamage);

                        if ((spellInfo->EffectMiscValue[j] & SPELL_SCHOOL_MASK_NATURE) != 0)
                            specialDamage += CalculateSingleStatWeight(playerclass, spec, "natsplpwr", spellDamage);

                        weight += specialDamage;
                    }
                }
                // spell healing
                // SPELL_AURA_MOD_HEALING_DONE
                if (spellInfo->EffectApplyAuraName[j] == SPELL_AURA_MOD_HEALING_DONE)
                {
                    weight += CalculateSingleStatWeight(playerclass, spec, "splheal", spellInfo->EffectBasePoints[j] + 1);
                }
                // Flat health/mana from chest/mana enchants: stamina and
                // intellect are the closest weight scales (aura 34/35 is a
                // flat pool, not a percent — the +1 mirrors the other
                // branches' EffectBasePoints+1 convention).
                if (spellInfo->EffectApplyAuraName[j] == SPELL_AURA_MOD_INCREASE_HEALTH)
                {
                    weight += CalculateSingleStatWeight(playerclass, spec, "sta", spellInfo->EffectBasePoints[j] + 1);
                }
                if (spellInfo->EffectApplyAuraName[j] == SPELL_AURA_MOD_INCREASE_ENERGY)
                {
                    weight += CalculateSingleStatWeight(playerclass, spec, "int", spellInfo->EffectBasePoints[j] + 1);
                }
            }
            break;
        }
        case 4: //Armor
            if (!pEnchant->amount[s])
                continue;
            weight += CalculateSingleStatWeight(playerclass, spec, "armor", pEnchant->amount[s]);
            break;
        case 5: //Stat
        {
            for (auto& statLink : weightStatLink)
            {
                if (statLink.second != pEnchant->spellid[s])
                    continue;

                weight += CalculateSingleStatWeight(playerclass, spec, statLink.first, pEnchant->amount[s]);
            }
            break;
        }
        case 6: //Totem
            break;
        case 7: //Use Spell
            break;
        }
    }

    return weight;
}


uint32 RandomItemMgr::CalculateRandomPropertyWeight(uint8 playerclass, uint8 spec, int32 randomPropertyId)
{
    uint32 weight = 0;
    if (randomPropertyId)
    {
        ItemRandomPropertiesEntry const* item_rand = sItemRandomPropertiesStore.LookupEntry(abs(randomPropertyId));
        if (item_rand)
        {
            for (uint32 i = PROP_ENCHANTMENT_SLOT_0; i < PROP_ENCHANTMENT_SLOT_0 + 3; ++i)
            {
                uint32 enchantId = item_rand->enchant_id[i - PROP_ENCHANTMENT_SLOT_0];

                weight += CalculateEnchantWeight(playerclass, spec, enchantId);
            }
        }
    }
    return weight;
}

uint32 RandomItemMgr::ItemStatWeight(Player* player, ItemQualifier& qualifier)
{
    ItemSpecType itSpec;
    uint32 weight = CalculateStatWeight(player->GetClass(), GetPlayerSpecId(player), qualifier.GetProto(), itSpec);
    if(qualifier.GetEnchantId())
        weight += CalculateEnchantWeight(player->GetClass(), GetPlayerSpecId(player), qualifier.GetEnchantId());
    if (qualifier.GetRandomPropertyId())
        weight += CalculateRandomPropertyWeight(player->GetClass(), GetPlayerSpecId(player), qualifier.GetRandomPropertyId());
    return weight;
}

uint32 RandomItemMgr::ItemStatWeight(Player* player, Item* item)
{
    ItemQualifier itemQualifier(item);
    return ItemStatWeight(player, itemQualifier);
}

uint32 RandomItemMgr::CalculateSingleStatWeight(uint8 playerclass, uint8 spec, std::string stat, int32 value)
{
    uint32 statWeight = 0;
    for (std::vector<WeightScaleStat>::iterator i = m_weightScales[spec].stats.begin(); i != m_weightScales[spec].stats.end(); ++i)
    {
        if (stat == i->stat)
        {
            // Compute in 64-bit and ignore non-positive contributions. A negative stat
            // value (item stat penalties, or negative aura EffectBasePoints) must not wrap
            // to a huge uint32 here — that both inflates the item's score and overflows the
            // signed MEDIUMINT scale_* cache columns, aborting the cache build on first boot.
            int64 weighted = (int64)i->weight * (int64)value;
            if (weighted <= 0)
                return 0;
            statWeight = (uint32)weighted;
            sLog.outDetail("stat: %s, val: %d, weight: %d, total: %d, class: %d, spec: %s", stat.c_str(), value, i->weight, statWeight, playerclass, m_weightScales[spec].info.name.c_str());
            return statWeight;
        }
    }

    return statWeight;
}

bool CheckItemSpec(uint8 spec, ItemSpecType itSpec)
{
    return false;
}

uint32 RandomItemMgr::GetQuestIdForItem(uint32 itemId)
{
    bool isQuest = false;
    uint32 questId = 0;
    ObjectMgr::QuestMap const& questTemplates = sObjectMgr.GetQuestTemplates();
    for (ObjectMgr::QuestMap::const_iterator i = questTemplates.begin(); i != questTemplates.end(); ++i)
    {
        Quest const* quest = i->second.get();

        uint32 rewItemCount = quest->GetRewItemsCount();
        for (uint32 i = 0; i < rewItemCount; ++i)
        {
            if (!quest->RewItemId[i])
                continue;

            if (quest->RewItemId[i] == itemId)
            {
                isQuest = true;
                questId = quest->GetQuestId();
                break;
            }
        }

        uint32 rewChocieItemCount = quest->GetRewChoiceItemsCount();
        for (uint32 i = 0; i < rewChocieItemCount; ++i)
        {
            if (!quest->RewChoiceItemId[i])
                continue;

            if (quest->RewChoiceItemId[i] == itemId)
            {
                isQuest = true;
                questId = quest->GetQuestId();
                break;
            }
        }
        if (isQuest)
            break;
    }
    return questId;
}

std::vector<uint32> RandomItemMgr::GetQuestIdsForItem(uint32 itemId)
{
    // Quest templates never change after load, so the reverse lookup is
    // memoized: the seed gate otherwise rescans every quest per candidate.
    auto memoized = questIdsMemo.find(itemId);
    if (memoized != questIdsMemo.end())
        return memoized->second;

    std::vector<uint32> questIds;
    ObjectMgr::QuestMap const& questTemplates = sObjectMgr.GetQuestTemplates();
    for (ObjectMgr::QuestMap::const_iterator i = questTemplates.begin(); i != questTemplates.end(); ++i)
    {
        Quest const* quest = i->second.get();

        uint32 rewItemCount = quest->GetRewItemsCount();
        for (uint32 i = 0; i < rewItemCount; ++i)
        {
            if (!quest->RewItemId[i])
                continue;

            if (quest->RewItemId[i] == itemId)
            {
                questIds.push_back(quest->GetQuestId());
                break;
            }
        }

        uint32 rewChocieItemCount = quest->GetRewChoiceItemsCount();
        for (uint32 i = 0; i < rewChocieItemCount; ++i)
        {
            if (!quest->RewChoiceItemId[i])
                continue;

            if (quest->RewChoiceItemId[i] == itemId)
            {
                questIds.push_back(quest->GetQuestId());
                break;
            }
        }
    }
    return questIdsMemo[itemId] = questIds;
}
// Walks one loot table through the same access seam the world uses at loot
// time (DropMapValue::GetLootTemplate, LootValues.h) and marks every item it
// can yield: ungrouped Entries, reference entries one level deep (the world
// resolves only one indirection per template), and all grouped chances —
// Groups carry the explicitly/grouped chances; the ungrouped Entries list
// alone misses raid drops.
static void MarkLootItems(LootTemplateAccess const* access, std::set<uint32>& items)
{
    if (!access)
        return;

    for (LootStoreItem const& lootEntry : access->Entries)
    {
        items.insert(lootEntry.itemid);
        if (lootEntry.mincountOrRef < 0)
        {
            if (LootTemplate const* ref = LootTemplates_Reference.GetLootFor((uint32)-lootEntry.mincountOrRef))
            {
                LootTemplateAccess const* refAccess = reinterpret_cast<LootTemplateAccess const*>(ref);
                for (LootStoreItem const& refEntry : refAccess->Entries)
                    items.insert(refEntry.itemid);
            }
        }
    }
    for (LootLootGroupAccess const& group : access->Groups)
    {
        for (LootStoreItem const& lootEntry : group.ExplicitlyChanced)
            items.insert(lootEntry.itemid);
        for (LootStoreItem const& lootEntry : group.EqualChanced)
            items.insert(lootEntry.itemid);
    }
}

// Records every item a loot table can yield (same seam and one-reference
// rule as MarkLootItems) at the given tier: itemLootTier[item] keeps the
// minimum tier over all tables. When directOnly, entries reached only
// through a reference row still lower itemLootTier but are not added to
// itemDirectBaseLoot — world-epic attestation needs a direct row.
static void MarkLootItemsTier(LootTemplateAccess const* access, ItemSourceTier tier,
    std::map<uint32, ItemSourceTier>& itemLootTier, std::set<uint32>& itemDirectBaseLoot, bool directOnly)
{
    if (!access)
        return;

    auto record = [&](uint32 itemId, bool direct)
    {
        if (!itemId)
            return;
        auto known = itemLootTier.find(itemId);
        if (known == itemLootTier.end() || tier < known->second)
            itemLootTier[itemId] = tier;
        if (direct && directOnly && tier == ITEM_SOURCE_TIER_BASE)
            itemDirectBaseLoot.insert(itemId);
    };

    for (LootStoreItem const& lootEntry : access->Entries)
    {
        record(lootEntry.itemid, true);
        if (lootEntry.mincountOrRef < 0)
        {
            if (LootTemplate const* ref = LootTemplates_Reference.GetLootFor((uint32)-lootEntry.mincountOrRef))
            {
                LootTemplateAccess const* refAccess = reinterpret_cast<LootTemplateAccess const*>(ref);
                for (LootStoreItem const& refEntry : refAccess->Entries)
                    record(refEntry.itemid, false);
            }
        }
    }
    for (LootLootGroupAccess const& group : access->Groups)
    {
        for (LootStoreItem const& lootEntry : group.ExplicitlyChanced)
            record(lootEntry.itemid, true);
        for (LootStoreItem const& lootEntry : group.EqualChanced)
            record(lootEntry.itemid, true);
    }
}

bool RandomItemMgr::IsRaidSourcedItem(uint32 itemId)
{
    if (!itemId)
        return false;

    if (!raidSourceIndexed)
        BuildRaidSourceIndex();

    return raidSourceItems.find(itemId) != raidSourceItems.end();
}

// Source-tier computation for the owner gear rules (roadmap #289).
// Lowest-tier source wins: world/vendor/quest/allowed-recipe counts as base
// even when the same item also drops in a raid. Runs once inside
// BuildItemInfoCache (which already walks every prototype), so the per-item
// cost stays in the startup cache build, never on the gear-roll path.
// End-game dungeon maps whose own loot tables are tier 1: 229/289/329/800.
// Every other dungeon (Deadmines/WC/SM low-level sets, BRD, Dire Maul,
// Hateforge, Gilneas, Dragonmaw, Stormwrought, Windhorn, ...) stays base.

// One-time raid provenance index for the fresh-seed gate. The per-item
// per dropping creature per candidate — thousands of synchronous queries in
// a single seeding burst, despite the header claiming a bounded scan. This
// builds the inverse item set once: two spawn-table reads (map ids classified
// through MapEntry::IsRaid, so custom raid maps are included), world-boss
// rank templates from the in-memory storage, then one loot walk per
// qualifying entry across corpse/pickpocket/skinning and gameobject chest
// tables. Entries with no spawn rows simply never qualify — the same
// fail-closed-to-non-raid decision as before, now made once instead of per
// candidate. Token turn-ins and quest rewards have no loot row and stay out
// of the set (they are gated by IsRaidQuestItem / the quest-level check).
void RandomItemMgr::BuildRaidSourceIndex()
{
    raidSourceIndexed = true;

    // Classify every spawn-table map through the DBC once.
    std::set<uint32> raidMaps;
    {
        std::unique_ptr<QueryResult> rows(WorldDatabase.Query("SELECT DISTINCT map FROM creature UNION SELECT DISTINCT map FROM gameobject"));
        if (rows)
        {
            do
            {
                uint32 mapId = rows->Fetch()[0].GetUInt32();
                if (MapEntry const* mapEntry = sMapStorage.LookupEntry<MapEntry>(mapId))
                    if (mapEntry->IsRaid())
                        raidMaps.insert(mapId);
            } while (rows->NextRow());
        }
    }

    // Entries qualifying as raid-sourced: any spawn on a raid map.
    std::set<uint32> raidCreatures;
    std::set<uint32> raidGameobjects;
    if (!raidMaps.empty())
    {
        std::string mapList;
        for (uint32 mapId : raidMaps)
            mapList += (mapList.empty() ? "" : ",") + std::to_string(mapId);

        auto collect = [&mapList](char const* table, std::set<uint32>& out)
        {
            std::unique_ptr<QueryResult> rows(WorldDatabase.PQuery(
                "SELECT DISTINCT id FROM %s WHERE map IN (%s)", table, mapList.c_str()));
            if (!rows)
                return;
            do
            {
                out.insert(rows->Fetch()[0].GetUInt32());
            } while (rows->NextRow());
        };
        collect("creature", raidCreatures);
        collect("gameobject", raidGameobjects);
    }

    // World bosses qualify wherever they walk.
    uint32 maxEntry = sCreatureStorage.GetMaxEntry();
    for (uint32 entry = 0; entry < maxEntry; ++entry)
    {
        CreatureInfo const* cInfo = sObjectMgr.GetCreatureTemplate(entry);
        if (cInfo && cInfo->rank == CREATURE_ELITE_WORLDBOSS)
            raidCreatures.insert(entry);
    }

    for (uint32 entry : raidCreatures)
    {
        LootType const lootTypes[3] = { LOOT_CORPSE, LOOT_PICKPOCKETING, LOOT_SKINNING };
        for (LootType lootType : lootTypes)
        {
            MarkLootItems(DropMapValue::GetLootTemplate(
                ObjectGuid(HIGHGUID_UNIT, entry, uint32(1)), lootType), raidSourceItems);
        }
    }
    for (uint32 entry : raidGameobjects)
    {
        MarkLootItems(DropMapValue::GetLootTemplate(
            ObjectGuid(HIGHGUID_GAMEOBJECT, entry, uint32(1)), LOOT_CORPSE), raidSourceItems);
    }

    sLog.outDetail("RandomItemMgr: raid provenance index holds %u items from %zu raid creatures, %zu raid gameobjects, %zu raid maps",
        (uint32)raidSourceItems.size(), raidCreatures.size(), raidGameobjects.size(), raidMaps.size());
}

// Source-tier index: one inverted item->tier map plus the recipe reverse
// map, computed once per process. Steps on the world DB (two spawn-table
// reads, no joins), then one loot walk per owning template — O(total loot
// rows), a few hundred ms. Lowest-tier source wins: a shared generic table
// dropping in both a raid and the open world records base for its items.
void RandomItemMgr::BuildSourceTierIndex()
{
    sourceTierIndexed = true;
    creatureSpawnTier.clear();
    gameObjectSpawnTier.clear();
    itemLootTier.clear();
    itemDirectBaseLoot.clear();
    recipeTier.clear();
    repRecipeItems.clear();

    auto mapTier = [](uint32 mapId) -> ItemSourceTier
    {
        if (MapEntry const* mapEntry = sMapStorage.LookupEntry<MapEntry>(mapId))
            if (mapEntry->IsRaid())
                return ITEM_SOURCE_TIER_RAID;
        if (mapId == 229 || mapId == 289 || mapId == 329 || mapId == 800)
            return ITEM_SOURCE_TIER_DUNGEON;
        return ITEM_SOURCE_TIER_BASE;
    };

    // Creature template -> min tier over its spawns (id..id4 columns).
    {
        std::unique_ptr<QueryResult> rows(WorldDatabase.Query(
            "SELECT id, id2, id3, id4, map FROM creature"));
        if (rows)
        {
            do
            {
                Field* fields = rows->Fetch();
                uint32 ids[4] = { fields[0].GetUInt32(), fields[1].GetUInt32(), fields[2].GetUInt32(), fields[3].GetUInt32() };
                ItemSourceTier tier = mapTier(fields[4].GetUInt32());
                for (uint32 id : ids)
                {
                    if (!id)
                        continue;
                    auto found = creatureSpawnTier.find(id);
                    if (found == creatureSpawnTier.end() || tier < found->second)
                        creatureSpawnTier[id] = tier;
                }
            } while (rows->NextRow());
        }
    }

    // GameObject template -> min tier over its spawns.
    {
        std::unique_ptr<QueryResult> rows(WorldDatabase.Query("SELECT id, map FROM gameobject"));
        if (rows)
        {
            do
            {
                Field* fields = rows->Fetch();
                uint32 id = fields[0].GetUInt32();
                ItemSourceTier tier = mapTier(fields[1].GetUInt32());
                auto found = gameObjectSpawnTier.find(id);
                if (found == gameObjectSpawnTier.end() || tier < found->second)
                    gameObjectSpawnTier[id] = tier;
            } while (rows->NextRow());
        }
    }

    // Walk every owning template once and record each yielded item at the
    // owner's min spawn tier: creature corpse/pickpocket/skinning tables
    // (world bosses always raid) plus gameobject chest tables. O(total loot
    // rows); every per-item lookup below is O(1).
    LootType const creatureLootTypes[3] = { LOOT_CORPSE, LOOT_PICKPOCKETING, LOOT_SKINNING };
    uint32 maxCreature = sCreatureStorage.GetMaxEntry();
    for (uint32 entry = 0; entry < maxCreature; ++entry)
    {
        CreatureInfo const* cInfo = sObjectMgr.GetCreatureTemplate(entry);
        if (!cInfo)
            continue;
        auto found = creatureSpawnTier.find(entry);
        if (found == creatureSpawnTier.end())
            continue;
        ItemSourceTier tier = found->second;
        if (cInfo->rank == CREATURE_ELITE_WORLDBOSS)
            tier = ITEM_SOURCE_TIER_RAID;
        ObjectGuid guid(HIGHGUID_UNIT, entry, uint32(1));
        for (LootType lootType : creatureLootTypes)
            MarkLootItemsTier(DropMapValue::GetLootTemplate(guid, lootType), tier,
                itemLootTier, itemDirectBaseLoot, lootType == LOOT_CORPSE);
    }
    uint32 maxGO = sGOStorage.GetMaxEntry();
    for (uint32 entry = 0; entry < maxGO; ++entry)
    {
        auto found = gameObjectSpawnTier.find(entry);
        if (found == gameObjectSpawnTier.end())
            continue;
        MarkLootItemsTier(DropMapValue::GetLootTemplate(
            ObjectGuid(HIGHGUID_GAMEOBJECT, entry, uint32(1)), LOOT_CORPSE),
            found->second, itemLootTier, itemDirectBaseLoot, false);
    }
    // Recipe reverse map: crafted wearable -> tier of its recipe source.
    // Chain (§7): recipe item (class 9) -> spellid_1 LEARN spell
    // (Effect[0]=36 SPELL_EFFECT_LEARN_SPELL) -> EffectTriggerSpell[0] (craft
    // spell) -> Effect[e]=24 CREATE_ITEM -> EffectItemType = wearable.
    // The in-memory SpellEntry store carries the merged DBC+DB rows, so the
    // chain walks without world-DB spell queries.
    {
        std::unique_ptr<QueryResult> recipes(WorldDatabase.Query(
            "SELECT entry, spellid_1 FROM item_template WHERE class = 9 AND spellid_1 <> 0"));
        if (recipes)
        {
            do
            {
                Field* fields = recipes->Fetch();
                uint32 recipeItem = fields[0].GetUInt32();
                uint32 learnSpell = fields[1].GetUInt32();
                SpellEntry const* learn = sServerFacade.LookupSpellInfo(learnSpell);
                if (!learn || learn->Effect[0] != SPELL_EFFECT_LEARN_SPELL)
                    continue;
                uint32 craftSpell = learn->EffectTriggerSpell[0];
                SpellEntry const* craft = sServerFacade.LookupSpellInfo(craftSpell);
                if (!craft)
                    continue;
                bool createsItem = false;
                for (uint8 e = 0; e < MAX_EFFECT_INDEX; ++e)
                    if (craft->Effect[e] == SPELL_EFFECT_CREATE_ITEM)
                        createsItem = true;
                if (!createsItem)
                    continue;

                // Recipe source tier: rep-gated recipe excludes (REP flag is
                // set separately); raid / end-game-dungeon loot recipe takes
                // the loot tier; trainer / vendor / world loot stays base.
                ItemPrototype const* recipeProto = sObjectMgr.GetItemPrototype(recipeItem);
                ItemSourceTier tier = ITEM_SOURCE_TIER_BASE;
                if (recipeProto && (recipeProto->RequiredReputationFaction || recipeProto->RequiredReputationRank))
                {
                    tier = ITEM_SOURCE_TIER_RAID;
                    repRecipeItems.insert(recipeItem);
                }
                else
                {
                    tier = RecipeLootTier(recipeItem);
                }

                for (uint8 e = 0; e < MAX_EFFECT_INDEX; ++e)
                {
                    if (craft->Effect[e] != SPELL_EFFECT_CREATE_ITEM)
                        continue;
                    uint32 product = craft->EffectItemType[e];
                    if (!product)
                        continue;
                    auto known = recipeTier.find(product);
                    if (known == recipeTier.end() || tier < known->second)
                        recipeTier[product] = tier;
                }
            } while (recipes->NextRow());
        }
    }

    sLog.outDetail("RandomItemMgr: source-tier index holds %zu creature spawns, %zu gameobject spawns, %zu loot items (%zu direct base), %zu recipe tiers (%zu rep-gated)",
        creatureSpawnTier.size(), gameObjectSpawnTier.size(), itemLootTier.size(), itemDirectBaseLoot.size(), recipeTier.size(), repRecipeItems.size());
}


// O(1) recipe-source lookup on the inverted index. Fail-open base when the
// recipe has no loot row (quest reward, deprecated).
ItemSourceTier RandomItemMgr::RecipeLootTier(uint32 recipeItemId)
{
    if (!recipeItemId)
        return ITEM_SOURCE_TIER_BASE;

    auto known = itemLootTier.find(recipeItemId);
    return known == itemLootTier.end() ? ITEM_SOURCE_TIER_BASE : known->second;
}


bool RandomItemMgr::IsRaidQuest(Quest const* quest)
{
    if (!quest)
        return false;
    // Explicit raid quest type.
    if (quest->GetType() == QUEST_TYPE_RAID)
        return true;
    // Raid-scale group content.
    if (quest->GetSuggestedPlayers() > 5)
        return true;
    // Raid map completion: ZoneOrSort > 0 is an area id whose map is the
    // raid; negative ZoneOrSort is a QuestSort.dbc sort id, not a map.
    int32 zoneOrSort = quest->GetZoneOrSort();
    if (zoneOrSort > 0)
    {
        AreaEntry const* area = AreaEntry::GetById((uint32)zoneOrSort);
        if (area)
        {
            if (MapEntry const* mapEntry = sMapStorage.LookupEntry<MapEntry>(area->MapId))
            {
                if (mapEntry->IsRaid())
                    return true;
            }
        }
    }
    return false;
}

bool RandomItemMgr::IsRaidQuestItem(uint32 itemId)
{
    if (!itemId)
        return false;

    std::vector<uint32> questIds = GetQuestIdsForItem(itemId);
    for (uint32 questId : questIds)
    {
        if (IsRaidQuest(sObjectMgr.GetQuestTemplate(questId)))
            return true;
    }
    return false;
}

// Rare world epic attestation (§6): membership in the direct-base-loot set
// built by the index walk. Reference-table-only and dungeon/raid-map rows
// never enter the set.
bool RandomItemMgr::IsWorldDropEpic(uint32 itemId)
{
    if (!itemId)
        return false;

    return itemDirectBaseLoot.count(itemId) != 0;
}

// Source-tier cache accessors. Fail-open defaults (base / no flags / not an
// epic) for uncached items: custom items and sparse DBC rows must never
// block gear.
ItemSourceTier RandomItemMgr::GetSourceTier(uint32 itemId)
{
    auto found = itemInfoCache.find(itemId);
    if (found == itemInfoCache.end() || !found->second)
        return ITEM_SOURCE_TIER_BASE;
    return found->second->sourceTier;
}

uint8 RandomItemMgr::GetSourceFlags(uint32 itemId)
{
    auto found = itemInfoCache.find(itemId);
    if (found == itemInfoCache.end() || !found->second)
        return ITEM_SOURCE_FLAG_NONE;
    return found->second->sourceFlags;
}

bool RandomItemMgr::IsWorldEpic(uint32 itemId)
{
    auto found = itemInfoCache.find(itemId);
    if (found == itemInfoCache.end() || !found->second)
        return false;
    return found->second->worldEpic;
}

// Seed/hire source-tier gate (owner rules): tier cap, REP flag, PVP flag.
// Replaces the ad-hoc raid/PvP/rep checks in the seed path with one
// classification — no double logic.
bool RandomItemMgr::PassesSourceTier(uint32 itemId)
{
    auto found = itemInfoCache.find(itemId);
    if (found == itemInfoCache.end() || !found->second)
        return true;
    ItemInfoEntry* info = found->second;
    if ((uint32)info->sourceTier > sPlayerbotAIConfig.randomGearMaxSourceTier)
        return false;
    if ((info->sourceFlags & ITEM_SOURCE_FLAG_REP) && !sPlayerbotAIConfig.randomGearAllowReputation)
        return false;
    if ((info->sourceFlags & ITEM_SOURCE_FLAG_PVP) && !sPlayerbotAIConfig.randomGearAllowPvP)
        return false;
    return true;
}


// Per-item owner-rule classification. Lowest-tier source wins across every
// known source; REP/PVP are orthogonal flags, not tiers.
void RandomItemMgr::ClassifySourceTier(ItemPrototype const* proto, ItemInfoEntry* cacheInfo)
{
    if (!proto || !cacheInfo)
        return;

    if (!sourceTierIndexed)
        BuildSourceTierIndex();

    cacheInfo->sourceTier = ITEM_SOURCE_TIER_BASE;
    cacheInfo->sourceFlags = ITEM_SOURCE_FLAG_NONE;
    cacheInfo->worldEpic = false;

    auto lowerTier = [&](ItemSourceTier tier)
    {
        if (tier < cacheInfo->sourceTier)
            cacheInfo->sourceTier = tier;
    };
    auto raiseTier = [&](ItemSourceTier tier)
    {
        if (tier > cacheInfo->sourceTier)
            cacheInfo->sourceTier = tier;
    };

    // REP flag: item row, quest requirement (already in cacheInfo), vendor
    // condition (already in cacheInfo), or rep-gated recipe.
    if (proto->RequiredReputationFaction || proto->RequiredReputationRank ||
        cacheInfo->repFaction || repRecipeItems.count(proto->ItemId))
        cacheInfo->sourceFlags |= ITEM_SOURCE_FLAG_REP;

    // PVP flag: NO_DISENCHANT reward blues or honor rank gear.
    if ((proto->Flags & ITEM_FLAG_NO_DISENCHANT) || proto->RequiredHonorRank)
        cacheInfo->sourceFlags |= ITEM_SOURCE_FLAG_PVP;

    // Rare world epic marker (§6): BoE epic, no set, weapon/armor, attested
    // by a direct world-map (0/1) creature-loot row.
    if (proto->Quality == ITEM_QUALITY_EPIC && proto->Bonding == BIND_WHEN_EQUIPPED &&
        !proto->ItemSet && (proto->Class == ITEM_CLASS_WEAPON || proto->Class == ITEM_CLASS_ARMOR))
        cacheInfo->worldEpic = IsWorldDropEpic(proto->ItemId);

    // Crafted product: the recipe source decides. A base recipe (trainer,
    // plain vendor, world loot) keeps the product at base even when the
    // product itself also drops in a raid — lowest-tier wins, so only
    // raise, never lower, from here.
    auto recipe = recipeTier.find(proto->ItemId);
    if (recipe != recipeTier.end())
    {
        raiseTier(recipe->second);
        if (repRecipeItems.count(proto->ItemId))
            cacheInfo->sourceFlags |= ITEM_SOURCE_FLAG_REP;
        if (recipe->second == ITEM_SOURCE_TIER_BASE)
            return;
    }

    // Loot sources: O(1) on the inverted index. Any base table keeps the
    // item at base; unknown (vendor-only/quest-only) items skip this.
    auto loot = itemLootTier.find(proto->ItemId);
    if (loot != itemLootTier.end())
        raiseTier(loot->second);

    // Quest reward source: raid quest (type 62 / >5 players / raid map)
    // raises; any other quest is a base source and keeps base. Vendor and
    // plain world sources are base by definition — nothing to do.
    std::vector<uint32> questIds = GetQuestIdsForItem(proto->ItemId);
    bool hasBaseQuest = false;
    for (uint32 questId : questIds)
    {
        Quest const* quest = sObjectMgr.GetQuestTemplate(questId);
        if (!quest)
            continue;
        if (IsRaidQuest(quest))
            raiseTier(ITEM_SOURCE_TIER_RAID);
        else
            hasBaseQuest = true;
        if (quest->GetRequiredMinRepFaction())
            cacheInfo->sourceFlags |= ITEM_SOURCE_FLAG_REP;
    }
    if (hasBaseQuest)
        lowerTier(ITEM_SOURCE_TIER_BASE);
}



std::string RandomItemMgr::GetPlayerSpecName(Player* player)
{
    std::string specName;
    int tab = AiFactory::GetPlayerSpecTab(player);
    switch (player->GetClass())
    {
    case CLASS_PRIEST:
        if (tab == 2)
            specName = "shadow";
        else if (tab == 1)
            specName = "holy";
        else
            specName = "disc";
        ;        break;
    case CLASS_SHAMAN:
        if (tab == 2)
            specName = "resto";
        else if (tab == 1)
            specName = "enhance";
        else
            specName = "elem";
        break;
    case CLASS_WARRIOR:
        if (tab == 2)
            specName = "prot";
        else if (tab == 1)
            specName = "fury";
        else
            specName = "arms";
        break;
    case CLASS_PALADIN:
        if (tab == 0)
            specName = "holy";
        else if (tab == 1)
            specName = "prot";
        else if (tab == 2)
            specName = "retrib";
        break;
    case CLASS_DRUID:
        if (tab == 0)
            specName = "balance";
        else if (tab == 1)
        {
            specName = "feraltank";
            if (player->GetLevel() > 19 && urand(0, 100) > 50)
                specName = "feraldps";
        }
        else if (tab == 2)
            specName = "resto";
        break;
    case CLASS_ROGUE:
        if (tab == 0)
            specName = "assas";
        else if (tab == 1)
            specName = "combat";
        else if (tab == 2)
            specName = "subtle";
        break;
    case CLASS_HUNTER:
        if (tab == 0)
            specName = "beast";
        else if (tab == 1)
            specName = "marks";
        else if (tab == 2)
            specName = "surv";
        break;
    case CLASS_MAGE:
        if (tab == 0)
            specName = "arcane";
        else if (tab == 1)
            specName = "fire";
        else if (tab == 2)
            specName = "frost";
        break;
    case CLASS_WARLOCK:
        if (tab == 0)
            specName = "afflic";
        else if (tab == 1)
            specName = "demo";
        else if (tab == 2)
            specName = "destro";
        break;
    default:
        break;
    }
    return specName;
}

uint32 RandomItemMgr::GetPlayerSpecId(Player* player)
{
    std::string specName = GetPlayerSpecName(player);
    if (specName.empty())
        return 0;

    for (auto itr : m_weightScales)
    {
        if (itr.second.info.name == specName && itr.second.info.classId == player->GetClass())
            return itr.second.info.id;
    }
    return 0;
}

uint32 RandomItemMgr::GetFallbackSpecId(uint8 playerclass)
{
    // First weight scale registered for the class: generic enough for
    // pre-10 bots with no spent talents, filtered by weapon rules downstream.
    for (auto const& itr : m_weightScales)
        if (itr.second.info.classId == playerclass && itr.second.info.id)
            return itr.second.info.id;
    return 0;
}

uint32 RandomItemMgr::GetUpgrade(Player* player, std::string spec, uint8 slot, uint32 quality, uint32 itemId)
{
    if (!player)
        return 0;

    // get old item statWeight
    uint32 oldStatWeight = 0;
    uint32 specId = 0;
    uint32 closestUpgrade = 0;
    uint32 closestUpgradeWeight = 0;
    std::vector<uint32> classspecs;

    for (uint32 specNum = 1; specNum <= MAX_STAT_SCALES; ++specNum)
    {
        if (!m_weightScales[specNum].info.id)
            continue;

        // Only the player's own class specs may be compared against each other;
        // scanning ids 1..4 limited this to the warrior and paladin specs.
        if (m_weightScales[specNum].info.classId != player->GetClass())
            continue;

        classspecs.push_back(m_weightScales[specNum].info.id);

        if (m_weightScales[specNum].info.name == spec)
            specId = m_weightScales[specNum].info.id;
    }
    if (!specId)
        return 0;

    if (itemId && itemInfoCache[itemId])
    {
        oldStatWeight = itemInfoCache[itemId]->weights[specId];

        if (oldStatWeight)
            sLog.outString("Old Item: %d, weight: %d", itemId, oldStatWeight);
        else
            sLog.outString("Old item has no stat weight");
    }

    for (std::map<uint32, ItemInfoEntry*>::iterator i = itemInfoCache.begin(); i != itemInfoCache.end(); ++i)
    {
        ItemInfoEntry* info = i->second;
        if (!info)
            continue;

        // skip useless items
        if (info->weights[specId] == 0)
            continue;

        // owner-configured exclusion list: never suggest these items
        if (std::find(sPlayerbotAIConfig.randomGearBlacklist.begin(), sPlayerbotAIConfig.randomGearBlacklist.end(), info->itemId) != sPlayerbotAIConfig.randomGearBlacklist.end())
            continue;

        // skip higher lvl
        if (info->minLevel > player->GetLevel())
            continue;

        // skip too low level
        if (player->GetLevel() > 10 && info->minLevel < (player->GetLevel() - 10))
            continue;

        // skip wrong team
        if (info->team && info->team != player->GetTeam())
            continue;

        // skip wrong slot
        if ((EquipmentSlots)info->slot != (EquipmentSlots)slot)
            continue;

        // skip higher quality
        if (quality && info->quality != quality)
            continue;

        // skip worse items
        if (info->weights[specId] <= oldStatWeight)
            continue;

        // skip items that only fit in slot, but not stats
        if (!itemId && info->weights[specId] == 1 && player->GetLevel() > 40)
            continue;

        // skip quest items
        if (info->source == ITEM_SOURCE_QUEST)
        {
            bool hasQuestDone = false;
            for (const auto& source : info->sourceIds)
            {
                if (player->GetQuestRewardStatus(source) == QUEST_STATUS_COMPLETE)
                    hasQuestDone = true;
            }
            if (!hasQuestDone)
                continue;
        }

        // No-stat jewellery below 30: a weight-1 neck/ring/trinket beats an
        // empty slot at 10-29 (owner spec: prefer usable over empty). Above
        // 30 the pools have scored alternatives, so the gate stays.
        if (info->weights[specId] == 1 && player->GetLevel() >= 30 &&
            (info->slot == EQUIPMENT_SLOT_NECK ||
            info->slot == EQUIPMENT_SLOT_TRINKET1 ||
            info->slot == EQUIPMENT_SLOT_TRINKET2 ||
            info->slot == EQUIPMENT_SLOT_FINGER1 ||
            info->slot == EQUIPMENT_SLOT_FINGER2))
            continue;

        // check if item stat score is the best among class specs
        uint32 bestSpecId = 0;
        uint32 bestSpecScore = 0;
        for (std::vector<uint32>::iterator i = classspecs.begin(); i != classspecs.end(); ++i)
        {
            if (info->weights[*i] > bestSpecScore)
            {
                bestSpecId = *i;
                bestSpecScore = info->weights[specId];
            }
        }

        if (bestSpecId && bestSpecId != specId && player->GetLevel() > 40)
            return 0;

        if (!closestUpgrade)
        {
            closestUpgrade = info->itemId;
            closestUpgradeWeight = info->weights[specId];
        }

        // pick closest upgrade
        if (info->weights[specId] < closestUpgradeWeight)
        {
            closestUpgrade = info->itemId;
            closestUpgradeWeight = info->weights[specId];
        }
    }

    if (closestUpgrade)
        sLog.outString("New Item: %d, weight: %d", closestUpgrade, closestUpgradeWeight);

    return closestUpgrade;
}

std::vector<uint32> RandomItemMgr::GetUpgradeList(Player* player, uint32 specId, uint8 slot, uint32 quality, uint32 itemId, uint32 amount)
{
    std::vector<uint32> listItems;
    if (!player)
        return listItems;

    // get old item statWeight
    uint32 oldStatWeight = 0;
    uint32 closestUpgrade = 0;
    uint32 closestUpgradeWeight = 0;
    std::vector<uint32> classspecs;

    if (itemId && itemInfoCache[itemId])
    {
        oldStatWeight = itemInfoCache[itemId]->weights[specId];

        if (oldStatWeight)
            sLog.outString("Old Item: %d, weight: %d", itemId, oldStatWeight);
        else
            sLog.outString("Old item has no stat weight");
    }

    for (std::map<uint32, ItemInfoEntry*>::iterator i = itemInfoCache.begin(); i != itemInfoCache.end(); ++i)
    {
        ItemInfoEntry* info = i->second;
        if (!info)
            continue;

        // skip useless items
        if (info->weights[specId] == 0)
            continue;

        // owner-configured exclusion list: never suggest these items
        if (std::find(sPlayerbotAIConfig.randomGearBlacklist.begin(), sPlayerbotAIConfig.randomGearBlacklist.end(), info->itemId) != sPlayerbotAIConfig.randomGearBlacklist.end())
            continue;

        // skip higher lvl
        if (info->minLevel > player->GetLevel())
            continue;

        // skip too low level
        if (player->GetLevel() > 20 && (int32)info->minLevel < (int32)(player->GetLevel() - 20))
            continue;

        // skip wrong team
        if (info->team && info->team != player->GetTeam())
            continue;

        // skip wrong slot
        if ((EquipmentSlots)info->slot != (EquipmentSlots)slot)
            continue;

        // skip higher quality
        if (quality && info->quality != quality)
            continue;

        // skip worse items
        if (info->weights[specId] <= oldStatWeight)
            continue;

        // skip items that only fit in slot, but not stats
        if (!itemId && info->weights[specId] == 1 && player->GetLevel() > 20)
            continue;

        // skip quest items
        if (info->source == ITEM_SOURCE_QUEST)
        {
            bool hasQuestDone = false;
            for (const auto& source : info->sourceIds)
            {
                if (player->GetQuestRewardStatus(source) == QUEST_STATUS_COMPLETE)
                    hasQuestDone = true;
            }
            if (!hasQuestDone)
                continue;
        }

        // No-stat jewellery below 30: a weight-1 neck/ring/trinket beats an
        // empty slot at 10-29 (owner spec: prefer usable over empty). Above
        // 30 the pools have scored alternatives, so the gate stays.
        if (info->weights[specId] < 2 && player->GetLevel() >= 30 && (
            info->slot == EQUIPMENT_SLOT_NECK ||
            info->slot == EQUIPMENT_SLOT_TRINKET1 ||
            info->slot == EQUIPMENT_SLOT_TRINKET2 ||
            info->slot == EQUIPMENT_SLOT_FINGER1 ||
            info->slot == EQUIPMENT_SLOT_FINGER2))
            continue;

        // skip pvp items
        if (info->source == ITEM_SOURCE_PVP)
        {
            if (!player->GetHonorMgr().GetRank().rank)
                continue;
        }

        //if (player->GetLevel() >= 40)
        //{
        //    // check if item stat score is the best among class specs
        //    uint32 bestSpecId = 0;
        //    uint32 bestSpecScore = 0;
        //    for (std::vector<uint32>::iterator i = classspecs.begin(); i != classspecs.end(); ++i)
        //    {
        //        if (info->weights[*i] > bestSpecScore)
        //        {
        //            bestSpecId = *i;
        //            bestSpecScore = info->weights[specId];
        //        }
        //    }

        //    if (bestSpecId && bestSpecId != specId)
        //        continue;
        //}

        listItems.push_back(info->itemId);
        //continue;

        // pick closest upgrade
        if (info->weights[specId] > closestUpgradeWeight)
        {
            closestUpgrade = info->itemId;
            closestUpgradeWeight = info->weights[specId];
        }
    }

    if (listItems.size())
        sLog.outString("New Items: %zu, Old item:%d, New items max: %d", listItems.size(), oldStatWeight, closestUpgradeWeight);

    // sort by stat weight
    std::sort(listItems.begin(), listItems.end(), [specId](int a, int b) { return sRandomItemMgr.GetStatWeight(a, specId) <= sRandomItemMgr.GetStatWeight(b, specId); });

    return listItems;
}

bool RandomItemMgr::HasStatWeight(uint32 itemId)
{
    return itemInfoCache[itemId] != nullptr;
}

bool RandomItemMgr::CanBuyFromVendor(Player *player, uint32 itemId, uint32 creatureId)
{
    CreatureInfo const* cInfo = sObjectMgr.GetCreatureTemplate(creatureId);
    if (!cInfo)
        return false;

    VendorItemList vendorItems;
    VendorItemData const* vItems = sObjectMgr.GetNpcVendorItemList(creatureId);
    VendorItemData const* tItems = sObjectMgr.GetNpcVendorTemplateItemList(cInfo->vendor_id);

    if (!vItems && !tItems)
    {
        return false;
    }

    uint8 customitems = vItems ? vItems->GetItemCount() : 0;
    uint8 numitems = customitems + (tItems ? tItems->GetItemCount() : 0);

    for (int i = 0; i < numitems; ++i)
    {
        VendorItem const* crItem = i < customitems ? vItems->GetItem(i) : tItems->GetItem(i - customitems);

        if (crItem && crItem->item == itemId)
        {
            ItemPrototype const* pProto = sObjectMgr.GetItemPrototype(itemId);
            if (pProto)
            {
                // when no faction required but rank > 0 will be used faction id from the vendor faction template to compare the rank
                if (!pProto->RequiredReputationFaction && pProto->RequiredReputationRank > 0 &&
                    ReputationRank(pProto->RequiredReputationRank) > player->GetReputationRank(sFactionTemplateStore.LookupEntry(cInfo->faction)->faction))
                    return false;

                if (crItem->conditionId && !sObjectMgr.IsConditionSatisfied(crItem->conditionId, player, player->GetMap(), nullptr, CONDITION_FROM_VENDOR))
                    return false;
            }
            return true;
        }
    }
    return false;
}

bool RandomItemMgr::HasSameQuestRewards(Player *player, uint32 itemId)
{
    ItemInfoEntry* info = itemInfoCache[itemId];
    if (!info)
        return false;
    if (info->source != ITEM_SOURCE_QUEST)
        return false;

    for (auto& questId : info->sourceIds)
    {
        Quest const* quest = sObjectMgr.GetQuestTemplate(questId);
        if (!quest)
            continue;

        uint32 rewItemCount = quest->GetRewItemsCount();
        for (uint32 i = 0; i < rewItemCount; ++i)
        {
            if (!quest->RewItemId[i])
                continue;

            if (player->HasItemCount(quest->RewItemId[i], 1, true))
                return true;
        }

        uint32 rewChoiceItemCount = quest->GetRewChoiceItemsCount();
        for (uint32 i = 0; i < rewChoiceItemCount; ++i)
        {
            if (!quest->RewChoiceItemId[i])
                continue;

            if (player->HasItemCount(quest->RewChoiceItemId[i], 1, true))
                return true;
        }
    }
    return false;
}

uint32 RandomItemMgr::GetMinLevelFromCache(uint32 itemId)
{
    ItemInfoEntry* info = itemInfoCache[itemId];
    if (!info)
        return 0;

    return info->minLevel;
}

uint32 RandomItemMgr::GetStatWeight(Player* player, uint32 itemId)
{
    if (!player || !itemId)
        return 0;

    if (!itemInfoCache[itemId])
        return 0;

    uint32 statWeight = 0;
    uint32 specId = GetPlayerSpecId(player);
    std::vector<uint32> classspecs;

    if (specId == 0)
        specId = GetFallbackSpecId(player->GetClass());
    if (specId == 0)
        return 0;

    if (!m_weightScales[specId].info.id)
        return 0;

    std::map<uint32, ItemInfoEntry*>::iterator itr = itemInfoCache.find(itemId);
    if (itr != itemInfoCache.end())
    {
        statWeight = itr->second->weights[specId];
    }

    return statWeight;
}

uint32 RandomItemMgr::GetStatWeight(uint32 itemId, uint32 specId)
{
    if (!specId || !itemId)
        return 0;

    if (!itemInfoCache[itemId])
        return 0;

    uint32 statWeight = 0;
    std::vector<uint32> classspecs;

    if (!m_weightScales[specId].info.id)
        return 0;

    std::map<uint32, ItemInfoEntry*>::iterator itr = itemInfoCache.find(itemId);
    if (itr != itemInfoCache.end())
    {
        statWeight = itr->second->weights[specId];
    }

    return statWeight;
}

uint32 RandomItemMgr::GetBestRandomEnchantStatWeight(uint32 itemId, uint32 specId)
{
    if (!specId || !itemId)
        return 0;

    if (!itemInfoCache[itemId])
        return 0;

    if (!m_weightScales[specId].info.id)
        return 0;

    uint8 plrClass = 0;
    uint32 statWeight = 0;

    for (auto itr : m_weightScales)
    {
        if (itr.second.info.id == specId)
            plrClass = itr.second.info.classId;
    }

    if (!plrClass)
        return 0;

    std::map<uint32, ItemInfoEntry*>::iterator itr = itemInfoCache.find(itemId);
    if (itr != itemInfoCache.end())
    {
        uint32 bestEnch = CalculateBestRandomEnchantId(plrClass, specId, itemId);
        if (bestEnch)
        {
            statWeight = CalculateEnchantWeight(plrClass, specId, bestEnch);
        }
    }

    return statWeight;
}

uint32 RandomItemMgr::GetLiveStatWeight(Player* player, uint32 itemId, uint32 specId)
{
    if (!player || !itemId)
        return 0;

    if (!itemInfoCache[itemId])
        return 0;

    uint32 statWeight = 0;
    specId = specId ? specId : GetPlayerSpecId(player);
    if (specId == 0 && player)
        specId = GetFallbackSpecId(player->GetClass());
    if (specId == 0)
        return 0;

    if (!m_weightScales[specId].info.id)
        return 0;

    ItemInfoEntry* info = itemInfoCache[itemId];
    if (!info)
        return 0;

    statWeight = info->weights[specId];

    // skip higher lvl
    if (info->minLevel > player->GetLevel())
        return 0;

    // skip too low level
    //if ((int32)info->minLevel < (int32)(player->GetLevel() - 20))
    //    return 0;

    // skip wrong team
    if (info->team && (Team)info->team != player->GetTeam())
        return 0;

    // skip quest items
    if (info->source == ITEM_SOURCE_QUEST && !info->sourceIds.empty())
    {
        bool canDoQuest = false;
        for (const auto& source : info->sourceIds)
        {
            Quest const* quest = sObjectMgr.GetQuestTemplate(source);
            if (quest)
            {
                // only class quests player could do
                if (player->SatisfyQuestClass(quest, false) && player->SatisfyQuestRace(quest, false) && player->SatisfyQuestLevel(quest, false))
                    canDoQuest = true;

                // check if quest is inactive (if linked to a not running game event)
                if (!quest->IsActive())
                    canDoQuest = false;

                // can be rewarded
                if (canDoQuest)
                    break;
            }
        }
        if (!canDoQuest)
            return 0;
    }

    // skip pvp items
    /*if (info->source == ITEM_SOURCE_PVP)
    {
        if (!player->GetHonorRankInfo().rank)
            return 0;
    }*/

    // skip missing reputation
    if (info->repFaction && uint32(player->GetReputationRank(info->repFaction)) < info->repRank)
        return 0;

    // skip missing pvp ranks
    if (info->pvpRank && player->GetHonorMgr().GetHighestRank().rank < info->pvpRank)
        return 0;
    if (info->pvpRank && info->pvpRank < 16 && player->GetHonorMgr().GetHighestRank().rank == 18)
        return 0;

    // skip non pvp items for some specs
    if (info->pvpRank < 16 && player->GetHonorMgr().GetHighestRank().rank == 18)
    {
        ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
        if (proto && !(
            info->slot == EQUIPMENT_SLOT_WAIST ||
            info->slot == EQUIPMENT_SLOT_WRISTS ||
            info->slot == EQUIPMENT_SLOT_BACK ||
            info->slot == EQUIPMENT_SLOT_NECK ||
            info->slot == EQUIPMENT_SLOT_TRINKET1 ||
            info->slot == EQUIPMENT_SLOT_TRINKET2 ||
            info->slot == EQUIPMENT_SLOT_FINGER1 ||
            info->slot == EQUIPMENT_SLOT_FINGER2 ||
            proto->SubClass == ITEM_SUBCLASS_ARMOR_TOTEM ||
            proto->SubClass == ITEM_SUBCLASS_ARMOR_LIBRAM ||
            proto->SubClass == ITEM_SUBCLASS_ARMOR_IDOL)
            && !(specId == 3 || specId == 4 || specId == 5 || specId == 14 || specId == 22 || specId == 31))
            return 0;
    }

    // skip missing skills
    if (info->reqSkill && player->GetSkillValue(info->reqSkill) < info->reqSkillRank)
        return 0;

    // No-stat jewellery below 30: a weight-1 neck/ring/trinket beats an
    // empty slot at 10-29 (owner spec: prefer usable over empty). The seed
    // loop filters weight-0 only, so this gate is what emptied the slots.
    if (info->weights[specId] == 1 && player->GetLevel() >= 30 && (
        info->slot == EQUIPMENT_SLOT_NECK ||
        info->slot == EQUIPMENT_SLOT_TRINKET1 ||
        info->slot == EQUIPMENT_SLOT_TRINKET2 ||
        info->slot == EQUIPMENT_SLOT_FINGER1 ||
        info->slot == EQUIPMENT_SLOT_FINGER2))
        return 0;

    // check if item stat score is the best among class specs
    /*uint32 bestSpecId = 0;
    uint32 bestSpecScore = 0;
    for (uint32 spec = 1; spec < MAX_STAT_SCALES; ++spec)
    {
        if (!m_weightScales[spec].info.id)
            continue;

        if (m_weightScales[spec].info.classId != player->GetClass())
            continue;

        if (info->weights[spec] > bestSpecScore && info->weights[spec] > 1)
        {
            bestSpecId = spec;
            bestSpecScore = info->weights[spec];
        }
    }*/

    // TODO test
    /*if (bestSpecId && bestSpecId != specId && player->GetLevel() >= 60)
        return 0;*/

    // increase stat weights for pvp items
    if (info->pvpRank)
        return statWeight * 5;

    return statWeight;
}

void RandomItemMgr::BuildEquipCache()
{
    uint32 maxLevel = DEFAULT_MAX_LEVEL;

    equipCache.clear();

    auto results = CharacterDatabase.PQuery("select clazz, spec, lvl, slot, quality, item from ai_playerbot_equip_cache");
    if (results)
    {
        sLog.outString("Loading equipment cache for %d classes, %d levels, %d slots, %d quality from %d items",
                MAX_CLASSES, maxLevel, EQUIPMENT_SLOT_END, ITEM_QUALITY_ARTIFACT, sItemStorage.GetMaxEntry());
        int count = 0;
        do
        {
            Field* fields = results->Fetch();
            uint32 clazz = fields[0].GetUInt32();
            uint32 spec = fields[1].GetUInt32();
            uint32 level = fields[2].GetUInt32();
            uint32 slot = fields[3].GetUInt32();
            uint32 quality = fields[4].GetUInt32();
            uint32 itemId = fields[5].GetUInt32();

            BotEquipKey key(level, clazz, spec, slot, quality);
            equipCache[key].push_back(itemId);
            count++;

        } while (results->NextRow());
        sLog.outString("Equipment cache loaded from %d records", count);
    }
    else
    {
        // MaNGOS returns an empty result set as a failed query, so what is left
        // here is either an absent table or an empty one. The native module ships
        // the schema only, so a fresh install has an empty cache and reaches this
        // path; it is generated once and loaded from the database afterwards.
        auto cacheCount = CharacterDatabase.PQuery("SELECT COUNT(*) FROM ai_playerbot_equip_cache");
        if (!cacheCount)
        {
            sLog.outErrorDb("TortoiseBots: ai_playerbot_equip_cache is missing; skipping optional cache generation");
            return;
        }

        if (!sPlayerbotAIConfig.generateItemCaches)
        {
            sLog.outString("Equipment cache is empty and AiPlayerbot.GenerateItemCaches is disabled; skipping optional cache generation");
            return;
        }

        // BuildItemInfoCache() bails out early while the world weight scales are
        // empty, and a cache generated without them would hold nothing but the
        // shirt/tabard rows and then load as "populated" forever. The warning the
        // scale load already logs is the actionable part.
        if (itemInfoCache.empty())
        {
            sLog.outError("Equipment cache is empty and no item stat weights are available; skipping optional cache generation");
            return;
        }

        sLog.outString("Building equipment cache for %d classes, %d specs, %d levels, %d slots, %d quality from %d items",
                MAX_CLASSES, MAX_STAT_SCALES, maxLevel, EQUIPMENT_SLOT_END, ITEM_QUALITY_ARTIFACT, sItemStorage.GetMaxEntry());

        // Tracks how many items were cached for each stat-weight spec, reported
        // once per spec after the walk.
        std::map<uint32, uint64> specItemCounts;
        RandomItemList tabardsList;
        RandomItemList shirtsList;
        BotEquipKey tabardKey(60, 1, 1, EQUIPMENT_SLOT_TABARD, 1);
        BotEquipKey shirtKey(60, 1, 1, EQUIPMENT_SLOT_BODY, 1);

        // The cache is built from the loaded ItemPrototype rows in a single pass.
        // Walking the whole item id space (up to ~2M ids on Tortoise) once per
        // (class, spec, level, slot, quality) key is what made the first startup
        // take tens of minutes on the world thread: ~11s for ~2.9M rows here, of
        // which the prototype walk itself is under a second. Rows are written as
        // multi-row statements inside one transaction, because one INSERT per
        // cached item would be ~2.9M round trips. PExecute formats into a 32KB
        // fixed buffer and does not report a truncated statement, so a flush stays
        // well below it.
        std::string rows;
        rows.reserve(MAX_QUERY_LEN / 2);
        auto flushRows = [&rows]()
        {
            if (rows.empty())
                return;

            CharacterDatabase.PExecute("insert into ai_playerbot_equip_cache (clazz, spec, lvl, slot, quality, item) values %s", rows.c_str());
            rows.clear();
        };

        auto addItem = [&](BotEquipKey const& key, uint32 itemId)
        {
            equipCache[key].push_back(itemId);

            char row[80];
            snprintf(row, sizeof(row), "%s(%u, %u, %u, %u, %u, %u)", rows.empty() ? "" : ", ",
                (unsigned)key.clazz, (unsigned)key.spec, key.level, (unsigned)key.slot, key.quality, itemId);
            rows += row;

            if (rows.size() >= 24 * 1024)
                flushRows();
        };

        CharacterDatabase.BeginTransaction();

        for (uint32 itemId = 0; itemId < sItemStorage.GetMaxEntry(); ++itemId)
        {
            ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
            if (!proto)
                continue;

            // Reject everything no key could ever accept, cheapest test first.
            if (proto->Quality > ITEM_QUALITY_ARTIFACT)
                continue;

            // Shirts and tabards are not cached per class, spec and level: the
            // donor build collected them under the fixed (60, 1, 1, slot, 1) key,
            // and never checked their bind state or required level.
            if (proto->InventoryType == INVTYPE_BODY || proto->InventoryType == INVTYPE_TABARD)
            {
                bool bodySlot = proto->InventoryType == INVTYPE_BODY;

                for (uint8 clazz = CLASS_WARRIOR; clazz < MAX_CLASSES; ++clazz)
                {
                    // skip nonexistent classes
                    if (!((1 << (clazz - 1)) & CLASSMASK_ALL_PLAYABLE) || !sChrClassesStore.LookupEntry(clazz))
                        continue;

                    if (!IsRandomGearCandidate(proto, clazz))
                        continue;

                    if (bodySlot)
                        shirtsList.push_back(itemId);
                    else
                        tabardsList.push_back(itemId);
                    break;
                }

                continue;
            }

            if (proto->Duration & 0x80000000)
                continue;

            if (proto->Bonding == BIND_QUEST_ITEM || proto->Bonding == BIND_WHEN_USE)
                continue;

            if (proto->Class != ITEM_CLASS_WEAPON &&
                proto->Class != ITEM_CLASS_ARMOR &&
                proto->Class != ITEM_CLASS_CONTAINER &&
                proto->Class != ITEM_CLASS_PROJECTILE)
                continue;

            auto infoItr = itemInfoCache.find(itemId);
            if (infoItr == itemInfoCache.end() || !infoItr->second)
                continue;

            ItemInfoEntry* info = infoItr->second;

            // Without a required level there is no level bracket to cache for;
            // CanEquipItem() rejects such an item as well.
            uint32 minLevel = info->minLevel;
            if (!proto->RequiredLevel && !minLevel)
                continue;

            // Slots this prototype can occupy. CanEquipItem() resolved this per
            // key, but it only depends on the inventory type.
            std::vector<uint8> protoSlots;
            for (uint8 slot = 0; slot < EQUIPMENT_SLOT_END; ++slot)
            {
                std::set<InventoryType> const& slots = viableSlots[(EquipmentSlots)slot];
                if (slots.find((InventoryType)proto->InventoryType) != slots.end())
                    protoSlots.push_back(slot);
            }

            if (protoSlots.empty())
                continue;

            // The donor key held an item for every level within 20 levels of its
            // own required level, never below it and never above the cache range.
            uint32 levelLow = minLevel > 20 ? minLevel - 20 : 1;
            uint32 levelHigh = std::min(maxLevel, minLevel + 20);

            for (uint8 clazz = CLASS_WARRIOR; clazz < MAX_CLASSES; ++clazz)
            {
                // skip nonexistent classes
                if (!((1 << (clazz - 1)) & CLASSMASK_ALL_PLAYABLE) || !sChrClassesStore.LookupEntry(clazz))
                    continue;

                if (!IsRandomGearCandidate(proto, clazz))
                    continue;

                for (uint32 spec = 1; spec <= MAX_STAT_SCALES; ++spec)
                {
                    if (!m_weightScales[spec].info.id)
                        continue;

                    if (m_weightScales[spec].info.classId != clazz)
                        continue;

                    // check stat weight
                    uint32 statWeight = info->weights[spec];

                    // only accept "useless" items if bot level <= 30
                    uint32 specLevelHigh = (statWeight == 1 && !proto->RandomProperty) ? std::min(levelHigh, 30u) : levelHigh;

                    if (statWeight <= 0 || levelLow > specLevelHigh)
                        continue;

                    for (std::vector<uint8>::iterator i = protoSlots.begin(); i != protoSlots.end(); ++i)
                    {
                        uint8 slot = *i;

                        if (slot == EQUIPMENT_SLOT_OFFHAND && clazz == CLASS_ROGUE && proto->Class != ITEM_CLASS_WEAPON)
                            continue;

                        // Warriors and hunters cannot use caster off-hands
                        // ("held in off-hand"): keep them out of the cache so
                        // gear rolls never offer one (Issue #219).
                        if (slot == EQUIPMENT_SLOT_OFFHAND && proto->InventoryType == INVTYPE_HOLDABLE &&
                            (clazz == CLASS_WARRIOR || clazz == CLASS_HUNTER))
                            continue;

                        bool armorSlot = proto->Class == ITEM_CLASS_ARMOR && (
                            slot == EQUIPMENT_SLOT_HEAD ||
                            slot == EQUIPMENT_SLOT_SHOULDERS ||
                            slot == EQUIPMENT_SLOT_CHEST ||
                            slot == EQUIPMENT_SLOT_WAIST ||
                            slot == EQUIPMENT_SLOT_LEGS ||
                            slot == EQUIPMENT_SLOT_FEET ||
                            slot == EQUIPMENT_SLOT_WRISTS ||
                            slot == EQUIPMENT_SLOT_HANDS);

                        // CanEquipArmor() only ever switches on the level 40 armor
                        // proficiency step, so it is evaluated once per band.
                        bool armorBelow40 = !armorSlot || CanEquipArmor(clazz, (uint8)spec, 39, proto);
                        bool armorAt40Plus = !armorSlot || CanEquipArmor(clazz, (uint8)spec, 40, proto);

                        for (uint32 level = levelLow; level <= specLevelHigh; ++level)
                        {
                            if (!(level < 40 ? armorBelow40 : armorAt40Plus))
                                continue;

                            addItem(BotEquipKey(level, clazz, (uint8)spec, slot, proto->Quality), itemId);
                            specItemCounts[spec]++;
                        }
                    }
                }
            }
        }

        flushRows();

        // The fixed shirt/tabard keys are what the mature gear code queries, and
        // their rows carry the item id in the row id so a rebuild replaces them
        // instead of accumulating duplicates.
        equipCache[tabardKey] = tabardsList;
        equipCache[shirtKey] = shirtsList;

        for (RandomItemList::iterator i = shirtsList.begin(); i != shirtsList.end(); ++i)
            CharacterDatabase.PExecute("replace into ai_playerbot_equip_cache (id, clazz, spec, lvl, slot, quality, item) values (%u, %u, %u, %u, %u, %u, %u)",
                2000000 + *i, 1, 1, 60, EQUIPMENT_SLOT_BODY, 1, *i);

        for (RandomItemList::iterator i = tabardsList.begin(); i != tabardsList.end(); ++i)
            CharacterDatabase.PExecute("replace into ai_playerbot_equip_cache (id, clazz, spec, lvl, slot, quality, item) values (%u, %u, %u, %u, %u, %u, %u)",
                2000000 + *i, 1, 1, 60, EQUIPMENT_SLOT_TABARD, 1, *i);

        CharacterDatabase.CommitTransaction();

        for (uint32 spec = 1; spec <= MAX_STAT_SCALES; ++spec)
        {
            if (!m_weightScales[spec].info.id)
                continue;

            sLog.outBasic("[GearCache] class %u spec %u (%s): cached %llu items",
                m_weightScales[spec].info.classId, spec, m_weightScales[spec].info.name.c_str(),
                (unsigned long long)specItemCounts[spec]);
        }

        sLog.outString("Equipment cache saved to DB");
    }
}

RandomItemList RandomItemMgr::Query(uint32 level, uint8 clazz, uint8 spec, uint8 slot, uint32 quality)
{
    BotEquipKey key(level, clazz, spec, slot, quality);
    return equipCache[key];
}

void RandomItemMgr::BuildAmmoCache()
{
    uint32 maxLevel = sPlayerbotAIConfig.randomBotMaxLevel;
    if (maxLevel > sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL))
        maxLevel = sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL);

    sLog.outBasic("Building ammo cache for %d levels", maxLevel);
	int counter1 = 0;
    for (uint32 level = 1; level <= maxLevel+1; level+=10)
    {
        for (uint32 subClass = ITEM_SUBCLASS_ARROW; subClass <= ITEM_SUBCLASS_BULLET; subClass++)
        {
            auto results = WorldDatabase.PQuery(
                    "select entry, required_level from item_template where class = '%u' and subclass = '%u' and required_level <= '%u' and quality = '%u' order by required_level desc",
                    ITEM_CLASS_PROJECTILE, subClass, level, ITEM_QUALITY_NORMAL);
            if (!results)
                return;

            Field* fields = results->Fetch();
            if (fields)
            {
                uint32 entry = fields[0].GetUInt32();
                ammoCache[level / 10][subClass] = entry;
				counter1++;
            }
        }

        auto results = WorldDatabase.PQuery(
            "select entry, required_level from item_template where class = '%u' and subclass = '%u' and required_level <= '%u' and quality = '%u' order by required_level desc",
            ITEM_CLASS_WEAPON, ITEM_SUBCLASS_WEAPON_THROWN, level, ITEM_QUALITY_NORMAL);
        if (!results)
            return;

        Field* fields = results->Fetch();
        if (fields)
        {
            uint32 entry = fields[0].GetUInt32();
            ammoCache[level / 10][ITEM_SUBCLASS_WEAPON_THROWN] = entry;
            counter1++;
        }
    }
	sLog.outBasic("Building quiver cache");
    // Best vendor-sold quiver/ammo pouch per level bucket (owner spec: the
    // ammo container must be adequate to the level). The npc_vendor join
    // makes drop/raid containers impossible by construction, and
    // required_level DESC picks the highest tier the level allows
    // (Light Quiver 1 -> Medium 10 -> Heavy 30 -> ...).
    for (uint32 level = 1; level <= maxLevel + 1; level += 10)
    {
        auto results = WorldDatabase.PQuery(
                "SELECT it.entry FROM item_template it JOIN npc_vendor v ON v.item = it.entry"
                " WHERE it.class = '%u' AND it.required_level <= '%u'"
                " ORDER BY it.required_level DESC LIMIT 1",
                ITEM_CLASS_QUIVER, level);
        if (results)
            quiverCache[level / 10] = results->Fetch()[0].GetUInt32();
    }

	sLog.outString("Cached %d types of ammo", counter1); // TEST
}

uint32 RandomItemMgr::GetAmmo(uint32 level, uint32 subClass)
{
    return ammoCache[(level - 1) / 10][subClass];
}

uint32 RandomItemMgr::GetQuiver(uint32 level)
{
    if (level < 1)
        level = 1;
    return quiverCache[(level - 1) / 10];
}


void RandomItemMgr::BuildPotionCache()
{
    uint32 maxLevel = sPlayerbotAIConfig.randomBotMaxLevel;
    if (maxLevel > sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL))
        maxLevel = sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL);

    sLog.outBasic("Building potion cache for %d levels", maxLevel);
	int counter2 = 0;
    for (uint32 level = 1; level <= maxLevel+1; level+=10)
    {
        uint32 effects[] = { SPELL_EFFECT_HEAL, SPELL_EFFECT_ENERGIZE };
        for (int i = 0; i < 2; ++i)
        {
            uint32 effect = effects[i];

            for (uint32 itemId = 0; itemId < sItemStorage.GetMaxEntry(); ++itemId)
            {
                ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
                if (!proto)
                    continue;

                if (proto->Class != ITEM_CLASS_CONSUMABLE ||
                    (proto->SubClass != ITEM_SUBCLASS_POTION && proto->SubClass != ITEM_SUBCLASS_FLASK) ||
                    proto->Bonding != NO_BIND)
                    continue;

                if (proto->RequiredLevel && (proto->RequiredLevel > level || (level > 10 && proto->RequiredLevel < level - 10)))
                    continue;

                if (proto->RequiredSkill)
                    continue;

                if (proto->Area || proto->Map || proto->RequiredCityRank || proto->RequiredHonorRank)
                    continue;

                if (proto->Duration & 0x80000000)
                    continue;

                for (int j = 0; j < MAX_ITEM_PROTO_SPELLS; j++)
                {
                    const SpellEntry* const spellInfo = sServerFacade.LookupSpellInfo(proto->Spells[j].SpellId);
                    if (!spellInfo)
                        continue;

                    for (int i = 0 ; i < 3; i++)
                    {
                        if (spellInfo->Effect[i] == effect)
                        {
                            potionCache[level / 10][effect].push_back(itemId);
                            break;
                        }
                    }
                }
            }
        }
    }

    for (uint32 level = 1; level <= maxLevel+1; level+=10)
    {
        uint32 effects[] = { SPELL_EFFECT_HEAL, SPELL_EFFECT_ENERGIZE };
        for (int i = 0; i < 2; ++i)
        {
            uint32 effect = effects[i];
            uint32 size = potionCache[level / 10][effect].size();
			counter2++;
            sLog.outDetail("Potion cache for level=%d, effect=%d: %d items", level, effect, size);
        }
    }
	sLog.outString("Cached %d types of potions", counter2); // TEST
}

void RandomItemMgr::BuildFoodCache()
{
    uint32 maxLevel = sPlayerbotAIConfig.randomBotMaxLevel;
    if (maxLevel > sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL))
        maxLevel = sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL);

    sLog.outBasic("Building food cache for %d levels", maxLevel);
	int counter3 = 0;
    for (uint32 level = 1; level <= maxLevel+1; level+=10)
    {
        uint32 categories[] = { 11, 59 };
        for (int i = 0; i < 2; ++i)
        {
            uint32 category = categories[i];

            for (uint32 itemId = 0; itemId < sItemStorage.GetMaxEntry(); ++itemId)
            {
                ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
                if (!proto)
                    continue;

                if (proto->Class != ITEM_CLASS_CONSUMABLE ||
                    (proto->SubClass != ITEM_SUBCLASS_FOOD && proto->SubClass != ITEM_SUBCLASS_CONSUMABLE) ||
                    (proto->Spells[0].SpellCategory != category) ||
                    proto->Bonding != NO_BIND)
                    continue;

                if (proto->RequiredLevel && (proto->RequiredLevel > level || (level > 10 && proto->RequiredLevel < level - 10)))
                    continue;

                if (proto->RequiredSkill)
                    continue;

                if (proto->Area || proto->Map || proto->RequiredCityRank || proto->RequiredHonorRank)
                    continue;

                if (proto->Duration & 0x80000000)
                    continue;

                foodCache[level / 10][category].push_back(itemId);
            }
        }
    }

    for (uint32 level = 1; level <= maxLevel+1; level+=10)
    {
        uint32 categories[] = { 11, 59 };
        for (int i = 0; i < 2; ++i)
        {
            uint32 category = categories[i];
            uint32 size = foodCache[level / 10][category].size();
			counter3++;
            sLog.outDetail("Food cache for level=%d, category=%d: %d items", level, category, size);
        }
    }
	sLog.outString("Cached %d types of food", counter3);
}

uint32 RandomItemMgr::GetRandomPotion(uint32 level, uint32 effect)
{
    std::vector<uint32> potions = potionCache[(level - 1) / 10][effect];
    if (potions.empty()) return 0;
    return potions[urand(0, potions.size() - 1)];
}

uint32 RandomItemMgr::GetFood(uint32 level, uint32 category)
{
    std::vector<uint32> items;
    if (category == 11)
    {
        if (level < 5)
            items = { 787, 117, 4540, 2680 };
        else if (level < 15)
            items = { 2287, 4592, 4541, 21072 };
        else if (level < 25)
            items = { 3770, 16170, 4542, 20074 };
        else if (level < 35)
            items = { 4594, 3771, 1707, 4457 };
        else if (level < 45)
            items = { 4599, 4601, 21552, 17222 /*21030, 16168 */ };
        else
            items = { 8950, 8952, 8957, 21023 /*21033, 21031 */ };
    }

    if (category == 59)
    {
        if (level < 5)
            items = { 159, 117 };
        else if (level < 15)
            items = { 1179, 21072 };
        else if (level < 25)
            items = { 1205 };
        else if (level < 35)
            items = { 1708 };
        else if (level < 45)
            items = { 1645 };
        else
            items = { 8766 };
    }

    if (items.empty()) return 0;
    return items[urand(0, items.size() - 1)];
}

uint32 RandomItemMgr::GetRandomFood(uint32 level, uint32 category)
{
    std::vector<uint32> food = foodCache[(level - 1) / 10][category];
    if (food.empty()) return 0;
    return food[urand(0, food.size() - 1)];
}

void RandomItemMgr::BuildTradeCache()
{
    tradeCache.clear();

    uint32 maxLevel = sPlayerbotAIConfig.randomBotMaxLevel;
    if (maxLevel > sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL))
        maxLevel = sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL);

    sLog.outBasic("Building trade cache for %d levels", maxLevel);
	int counter4 = 0;
    for (uint32 level = 1; level <= maxLevel+1; level+=10)
    {
        for (uint32 itemId = 0; itemId < sItemStorage.GetMaxEntry(); ++itemId)
        {
            ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
            if (!proto)
                continue;

            if (proto->Class != ITEM_CLASS_TRADE_GOODS || proto->Bonding != NO_BIND)
                continue;

            if (proto->ItemLevel < level)
                continue;

            if (proto->RequiredLevel && (proto->RequiredLevel > level || (level > 10 && proto->RequiredLevel < level - 10)))
                continue;

            if (proto->RequiredSkill)
                continue;

            tradeCache[level / 10].push_back(itemId);
        }
    }

    for (uint32 level = 1; level <= maxLevel+1; level+=10)
    {
        uint32 size = tradeCache[level / 10].size();
        sLog.outDetail("Trade cache for level=%d: %d items", level, size);
		counter4++;
    }
	sLog.outString("Cached %d trade categories", counter4); // TEST
}

uint32 RandomItemMgr::GetRandomTrade(uint32 level)
{
    std::vector<uint32> trade = tradeCache[(level - 1) / 10];
    if (trade.empty()) return 0;
    return trade[urand(0, trade.size() - 1)];
}

void RandomItemMgr::LoadRandomEnchantments()
{
    randomEnchantsCache.clear();

    uint32 count = 0;
    auto queryResult = WorldDatabase.Query("SELECT entry, ench, chance FROM item_enchantment_template");

    if (queryResult)
    {
        do
        {
            Field* fields = queryResult->Fetch();
            uint32 entry = fields[0].GetUInt32();
            uint32 ench = fields[1].GetUInt32();
            float chance = fields[2].GetFloat();

            if (chance > 0.000001f && chance <= 100.0f)
                randomEnchantsCache[entry].push_back(ench);

            ++count;
        } while (queryResult->NextRow());

        sLog.outString(">> Loaded %u Item Enchantment definitions", count);
    }
    else
        sLog.outErrorDb(">> Loaded 0 Item Enchantment definitions. DB table `item_enchantment_template` is empty.");

    sLog.outString();
}

void RandomItemMgr::LoadBotEnchantCandidates()
{
    botEnchantCandidates.clear();
    botEnchantsLoaded = true;

    uint32 count = 0;
    auto queryResult = WorldDatabase.Query(
        "SELECT spellid, slotid, min_level, tier, rep, premium FROM ai_playerbot_enchant_candidates");
    if (queryResult)
    {
        do
        {
            Field* fields = queryResult->Fetch();
            BotEnchantCandidate candidate;
            candidate.spellId = fields[0].GetUInt32();
            candidate.slotId = fields[1].GetUInt8();
            candidate.minLevel = fields[2].GetUInt8();
            candidate.tier = fields[3].GetUInt8();
            candidate.rep = fields[4].GetUInt8();
            candidate.premium = fields[5].GetUInt8();
            botEnchantCandidates.push_back(candidate);
            ++count;
        } while (queryResult->NextRow());

        sLog.outString(">> Loaded %u bot enchant candidates", count);
    }
    else
        sLog.outErrorDb(">> Loaded 0 bot enchant candidates. DB table `ai_playerbot_enchant_candidates` is empty.");

    sLog.outString();
}

uint32 RandomItemMgr::CalculateProcEnchantWeight(uint8 playerclass, uint8 spec, uint32 enchantId)
{
    if (!enchantId)
        return 0;

    SpellItemEnchantmentEntry const* pEnchant = sSpellItemEnchantmentStore.LookupEntry(enchantId);
    if (!pEnchant)
        return 0;

    // Expected-damage model for ITEM_ENCHANTMENT_TYPE_COMBAT_SPELL procs.
    // Per-swing proc chance follows the core's own formula
    // (Player::CastItemCombatSpell): table PPM from spell_proc_item_enchant
    // when present, else the DBC amount as a percent, else 1 PPM. PPM turns
    // into chance via the standard 60s-normalized swing rate
    // (chance = PPM * weaponDelay / 60000); a 2.0s weapon stands in for the
    // unknown piece so candidate ranking never depends on the equipped item.
    // Expected damage per swing ~= chance * spell damage, converted to the
    // spec's melee-dps scale ("mledps" is damage per point) and scaled by
    // buff uptime (duration window capped at 90%, same convention as the
    // trinket scorer in CalculateStatWeight). Non-damage procs (slows,
    // debuffs) score 0.
    static const float kReferenceWeaponDelayMs = 2000.0f;
    uint32 weight = 0;
    for (int s = 0; s < 3; ++s)
    {
        if (pEnchant->type[s] != ITEM_ENCHANTMENT_TYPE_COMBAT_SPELL)
            continue;
        uint32 procSpellId = pEnchant->spellid[s];
        if (!procSpellId)
            continue;
        SpellEntry const* procInfo = sSpellTemplate.LookupEntry<SpellEntry>(procSpellId);
        if (!procInfo)
            continue;

        float chancePerSwing = 0.0f;
        float ppmRate = sSpellMgr.GetItemEnchantProcChance(procSpellId);
        if (ppmRate > 0.0f)
            chancePerSwing = ppmRate * kReferenceWeaponDelayMs / 60000.0f;
        else if (pEnchant->amount[s] != 0)
            chancePerSwing = static_cast<float>(pEnchant->amount[s]) / 100.0f;
        else
            chancePerSwing = 1.0f * kReferenceWeaponDelayMs / 60000.0f;

        for (uint32 j = 0; j < MAX_EFFECT_INDEX; ++j)
        {
            uint32 effect = procInfo->Effect[j];
            int32 basePoints = procInfo->EffectBasePoints[j] + 1;
            if (basePoints <= 0)
                continue;
            if (effect == SPELL_EFFECT_HEALTH_LEECH)
            {
                // Lifestealing-style proc: direct damage plus a heal.
                // Score the damage through both dps scales (see the type-2
                // comment above); the heal rides along for free.
                float expected = chancePerSwing * static_cast<float>(basePoints);
                weight += CalculateSingleStatWeight(playerclass, spec, "mledps", static_cast<int32>(expected));
                weight += CalculateSingleStatWeight(playerclass, spec, "rgddps", static_cast<int32>(expected));
            }
            else if (effect == SPELL_EFFECT_SCHOOL_DAMAGE)
            {
                // Direct damage proc: expected damage per swing (both scales).
                float expected = chancePerSwing * static_cast<float>(basePoints);
                weight += CalculateSingleStatWeight(playerclass, spec, "mledps", static_cast<int32>(expected));
                weight += CalculateSingleStatWeight(playerclass, spec, "rgddps", static_cast<int32>(expected));
            }
            else if (effect == SPELL_EFFECT_APPLY_AURA)
            {
                uint32 auraName = procInfo->EffectApplyAuraName[j];
                if (auraName == SPELL_AURA_MOD_STAT)
                {
                    uint32 stat = procInfo->EffectMiscValue[j];
                    if (ItemStatLink.find(stat) == ItemStatLink.end())
                        continue;
                    float coverage = chancePerSwing;
                    int32 duration = GetSpellDuration(procInfo);
                    if (duration > 0)
                    {
                        // Buff proc: uptime ~= procs-per-duration-window.
                        coverage = chancePerSwing * static_cast<float>(duration) / kReferenceWeaponDelayMs;
                        if (coverage > 0.9f)
                            coverage = 0.9f;
                    }
                    float expected = coverage * static_cast<float>(basePoints);
                    weight += CalculateSingleStatWeight(playerclass, spec, ItemStatLink[stat], static_cast<int32>(expected));
                }
                else if (auraName == SPELL_AURA_MOD_DAMAGE_DONE)
                {
                    float coverage = chancePerSwing;
                    int32 duration = GetSpellDuration(procInfo);
                    if (duration > 0)
                    {
                        coverage = chancePerSwing * static_cast<float>(duration) / kReferenceWeaponDelayMs;
                        if (coverage > 0.9f)
                            coverage = 0.9f;
                    }
                    float expected = coverage * static_cast<float>(basePoints);
                    if (procInfo->EffectMiscValue[j] == SPELL_SCHOOL_MASK_MAGIC)
                        weight += CalculateSingleStatWeight(playerclass, spec, "splpwr", static_cast<int32>(expected));
                }
            }
        }
    }

    return weight;
}

uint32 RandomItemMgr::CalculateBestBotEnchantId(Player* bot, uint32 specId, Item* item)
{
    if (!bot || !item)
        return 0;
    if (!botEnchantsLoaded)
        LoadBotEnchantCandidates();
    if (botEnchantCandidates.empty())
        return 0;

    ItemPrototype const* proto = item->GetProto();
    if (!proto)
        return 0;

    uint32 level = bot->GetLevel();
    uint32 quality = proto->Quality;
    // Owner quality ceiling: grey gear stays unenchanted; whites take cheap
    // enchants only (min level at least 10 below the bot), greens at least 5
    // below; blues take anything non-premium up to the bot's level; epics+
    // take premium too.
    if (quality < ITEM_QUALITY_NORMAL)
        return 0;
    bool isEpic = quality >= ITEM_QUALITY_EPIC;
    uint32 levelMargin = quality == ITEM_QUALITY_NORMAL ? 10 : quality == ITEM_QUALITY_UNCOMMON ? 5 : 0;

    uint8 equipSlot = item->GetSlot();
    uint8 playerclass = bot->GetClass();
    if (!specId)
        specId = GetFallbackSpecId(playerclass);
    if (!specId || !m_weightScales[specId].info.id)
        return 0;

    uint32 bestSpell = 0;
    uint32 bestScore = 0;
    for (auto const& candidate : botEnchantCandidates)
    {
        if (candidate.slotId != equipSlot)
            continue;
        // Source-tier cap with rep gate (same rules as the gear pool:
        // default tier 0, rep off; roadmap issue #289 unlocks higher).
        if (candidate.tier > 0 || candidate.rep > 0)
            continue;
        if (candidate.minLevel > level)
            continue;
        if (candidate.premium && !isEpic)
            continue;
        if (candidate.minLevel + levelMargin > level)
            continue;

        SpellEntry const* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(candidate.spellId);
        if (!spellInfo || spellInfo->Effect[0] != SPELL_EFFECT_ENCHANT_ITEM)
            continue;
        uint32 enchantId = spellInfo->EffectMiscValue[0];
        if (!enchantId)
            continue;
        // Slot/item-type mask fit: mirrors EnchantItemT's own check so
        // mismatches are skipped silently instead of spamming error logs
        // (e.g. Fiery Weapon onto a shield, scope onto a thrown weapon).
        if (!((1 << proto->SubClass) & spellInfo->EquippedItemSubClassMask) &&
            !((1 << proto->InventoryType) & spellInfo->EquippedItemInventoryTypeMask))
            continue;

        uint32 score = CalculateEnchantWeight(playerclass, specId, enchantId);
        if (!score)
            score = CalculateProcEnchantWeight(playerclass, specId, enchantId);
        // Deterministic tie-break: two candidates can weigh the same
        // (e.g. scopes on a spec with no dps scale, or zero-weight auras).
        // Prefer the higher-min-level row so picks march up the ladder with
        // the bot instead of sticking on the first tier forever.
        if (score > bestScore || (score && bestSpell && score == bestScore &&
            candidate.minLevel > GetBotEnchantMinLevel(bestSpell, equipSlot)))
        {
            bestScore = score;
            bestSpell = candidate.spellId;
        }
    }

    return bestScore ? bestSpell : 0;
}

uint8 RandomItemMgr::GetBotEnchantMinLevel(uint32 spellId, uint8 slotId)
{
    for (auto const& candidate : botEnchantCandidates)
        if (candidate.spellId == spellId && candidate.slotId == slotId)
            return candidate.minLevel;
    return 0;
}
