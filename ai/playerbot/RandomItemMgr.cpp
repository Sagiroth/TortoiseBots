
#include "playerbot/playerbot.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "RandomItemMgr.h"
#include "playerbot/PlayerbotAI.h"

#include "Database/DBCStore.h"
#include "Database/DatabaseEnv.h"
#include "PlayerbotAI.h"

#include "playerbot/ServerFacade.h"
#include "strategy/values/LootValues.h"

#include "ItemEnchantmentMgr.h"

#include "strategy/values/SharedValueContext.h"

char * strstri (const char* str1, const char* str2);

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
    BuildRandomItemCache();
    //BuildRarityCache();
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
        // The native module ships the cache schema separately from the large
        // optional dataset. MaNGOS returns an empty result as nullptr here;
        // distinguish that from a genuinely absent/legacy cache so startup
        // does not synchronously issue one INSERT per item on the world
        // thread. A populated cache still follows the mature load path.
        auto cacheCount = CharacterDatabase.PQuery("SELECT COUNT(*) FROM ai_playerbot_rnditem_cache");
        if (cacheCount && cacheCount->Fetch()->GetUInt32() == 0)
        {
            sLog.outString("Random item cache is present but empty; skipping optional cache generation");
            return;
        }

        sLog.outString("Building random item cache from %u items", sItemStorage.GetMaxEntry());
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

                randomItemCache[level / 10][rit].push_back(itemId);
                CharacterDatabase.PExecute("insert into ai_playerbot_rnditem_cache (lvl, type, item) values (%u, %u, %u)",
                        level / 10, type, itemId);
            }
        }

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

bool RandomItemMgr::CanEquipItem(BotEquipKey key, ItemPrototype const* proto)
{
    if (proto->Duration & 0x80000000)
        return false;

    if (proto->Quality != key.quality)
        return false;

    if (proto->Bonding == BIND_QUEST_ITEM || proto->Bonding == BIND_WHEN_USE)
        return false;

    if (proto->Class == ITEM_CLASS_CONTAINER)
        return true;

    std::set<InventoryType> slots = viableSlots[(EquipmentSlots)key.slot];
    if (slots.find((InventoryType)proto->InventoryType) == slots.end())
        return false;

    uint32 requiredLevel = proto->RequiredLevel;

    if (!requiredLevel)
    {
        requiredLevel = GetMinLevelFromCache(proto->ItemId);
    }
    if (!requiredLevel)
        return false;

    return true;
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
            mh_weapons = { ITEM_SUBCLASS_WEAPON_SWORD, ITEM_SUBCLASS_WEAPON_MACE };
            oh_weapons = { ITEM_SUBCLASS_WEAPON_SWORD, ITEM_SUBCLASS_WEAPON_MACE };
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

                FactionTemplateEntry const* factionEntry = sFactionTemplateStore.LookupEntry(cInfo->Faction);
                if (PlayerbotAI::friendToAlliance(factionEntry))
                    isAlly = true;
                if (PlayerbotAI::friendToHorde(factionEntry))
                    isHorde = true;

                // check faction conditions
                VendorItemData const* vItems = sObjectMgr.GetNpcVendorItemList(vendor);
                VendorItemData const* tItems = sObjectMgr.GetNpcVendorTemplateItemList(cInfo->VendorTemplateId);

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
            "scale_16, scale_17, scale_18, scale_19, scale_20, scale_21, scale_22, scale_23, scale_24, scale_25, scale_26, scale_27, scale_28, scale_29, scale_30, scale_31, scale_32)"
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");

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

    uint32 socketBonus = 0;

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

    // if stat value consists of only socket bonuses - skip
    if (socketBonus && !(statWeight || basicStatsWeight))
        return 0;

    statWeight += socketBonus;

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
        case 1: //Proc //TODO add proc values?
            break;
        case 2: //Damage
            if (!pEnchant->amount[s])
                continue;
            weight += CalculateSingleStatWeight(playerclass, spec, "mledps", pEnchant->amount[s]);
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
        case 8: //Prismatic socket
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

uint32 RandomItemMgr::CalculateGemWeight(uint8 playerclass, uint8 spec, uint32 gemId)
{
    return 0;
}

uint32 RandomItemMgr::CalculateSocketWeight(uint8 playerclass, ItemQualifier& qualifier, uint8 spec)
{
    return 0;
}


uint32 RandomItemMgr::ItemStatWeight(Player* player, ItemQualifier& qualifier)
{
    ItemSpecType itSpec;
    uint32 weight = CalculateStatWeight(player->GetClass(), GetPlayerSpecId(player), qualifier.GetProto(), itSpec);
    if(qualifier.GetEnchantId())
        weight += CalculateEnchantWeight(player->GetClass(), GetPlayerSpecId(player), qualifier.GetEnchantId());
    if (qualifier.GetRandomPropertyId())
        weight += CalculateRandomPropertyWeight(player->GetClass(), GetPlayerSpecId(player), qualifier.GetRandomPropertyId());
    if(qualifier.GetGem1())
        weight += CalculateGemWeight(player->GetClass(), GetPlayerSpecId(player), qualifier.GetGem1());
    if (qualifier.GetGem2())
        weight += CalculateGemWeight(player->GetClass(), GetPlayerSpecId(player), qualifier.GetGem2());
    if (qualifier.GetGem3())
        weight += CalculateGemWeight(player->GetClass(), GetPlayerSpecId(player), qualifier.GetGem3());
    if (qualifier.GetGem4())
        weight += CalculateGemWeight(player->GetClass(), GetPlayerSpecId(player), qualifier.GetGem4());

    weight += CalculateSocketWeight(player->GetClass(), qualifier, GetPlayerSpecId(player));

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
    return questIds;
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

    for (uint32 specNum = 1; specNum < 5; ++specNum)
    {
        if (!m_weightScales[specNum].info.id)
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

        // skip no stats trinkets
        if (info->weights[specId] == 1 &&
            info->slot == EQUIPMENT_SLOT_NECK ||
            info->slot == EQUIPMENT_SLOT_TRINKET1 ||
            info->slot == EQUIPMENT_SLOT_TRINKET2 ||
            info->slot == EQUIPMENT_SLOT_FINGER1 ||
            info->slot == EQUIPMENT_SLOT_FINGER2)
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

        // skip no stats trinkets
        if (info->weights[specId] < 2 && (
            info->slot == EQUIPMENT_SLOT_NECK ||
            info->slot == EQUIPMENT_SLOT_TRINKET1 ||
            info->slot == EQUIPMENT_SLOT_TRINKET2 ||
            info->slot == EQUIPMENT_SLOT_FINGER1 ||
            info->slot == EQUIPMENT_SLOT_FINGER2))
            continue;

        // skip pvp items
        if (info->source == ITEM_SOURCE_PVP)
        {
            if (!player->GetHonorRankInfo().rank)
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
    VendorItemData const* tItems = sObjectMgr.GetNpcVendorTemplateItemList(cInfo->VendorTemplateId);

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
                    ReputationRank(pProto->RequiredReputationRank) > player->GetReputationRank(sFactionTemplateStore.LookupEntry(cInfo->Faction)->faction))
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
    if (info->pvpRank && player->GetHonorHighestRankInfo().rank < info->pvpRank)
        return 0;
    if (info->pvpRank && info->pvpRank < 16 && player->GetHonorHighestRankInfo().rank == 18)
        return 0;

    // skip non pvp items for some specs
    if (info->pvpRank < 16 && player->GetHonorHighestRankInfo().rank == 18)
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

    // skip no stats trinkets
    if (info->weights[specId] == 1 && (
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
        auto cacheCount = CharacterDatabase.PQuery("SELECT COUNT(*) FROM ai_playerbot_equip_cache");
        if (cacheCount && cacheCount->Fetch()->GetUInt32() == 0)
        {
            sLog.outString("Equipment cache is present but empty; skipping optional cache generation");
            return;
        }

        uint64 total = uint64(MAX_CLASSES * 3 * maxLevel * EQUIPMENT_SLOT_END * ITEM_QUALITY_ARTIFACT);
        sLog.outString("Building equipment cache for %d classes, %d specs, %d levels, %d slots, %d quality from %d items (%zu total)",
                MAX_CLASSES, MAX_STAT_SCALES, maxLevel, EQUIPMENT_SLOT_END, ITEM_QUALITY_ARTIFACT, sItemStorage.GetMaxEntry(), total);

        BarGoLink bar(total);
        // Tracks how many items were cached for each stat-weight spec, so we can
        // emit a visible progress line per (class, spec) as the build advances.
        std::map<uint32, uint64> specItemCounts;
        RandomItemList tabardsList;
        RandomItemList shirtsList;
        BotEquipKey tabardKey(60, 1, 1, EQUIPMENT_SLOT_TABARD, 1);
        BotEquipKey shirtKey(60, 1, 1, EQUIPMENT_SLOT_BODY, 1);

        // TO DO: Replace this with a db query
        static std::unordered_set<uint32> unavailableItemIDs = { 128, 997, 1024, 1027, 1217, 1255, 23720, 2929, 7725, 15141, 13460, 5013, 6376, 6345, 6343, 6222, 21857, 22728, 22729, 17967, 1041, 1133, 1134, 2413, 2415, 5663, 5874, 5875, 8583, 8589, 8590, 8627, 8630, 8633, 16339, 20221, 13323, 13324, 14062, 23193, 21044, 2932, 41, 42, 46, 50, 54, 58, 77, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 97, 98, 99, 100, 101, 102, 103, 104, 105, 113, 114, 115, 119, 122, 123, 124, 125, 126, 130, 131, 132, 133, 134, 135, 136, 137, 138, 141, 146, 149, 150, 151, 152, 155, 156, 157, 184, 527, 734, 741, 746, 751, 761, 784, 786, 788, 806, 807, 808, 813, 823, 836, 842, 855, 875, 876, 877, 883, 894, 898, 900, 901, 902, 903, 905, 906, 907, 908, 909, 913, 917, 930, 931, 941, 945, 948, 951, 956, 958, 960, 964, 965, 966, 967, 968, 973, 974, 975, 976, 980, 985, 986, 989, 992, 994, 996, 1002, 1004, 1014, 1016, 1018, 1020, 1021, 1022, 1023, 1025, 1026, 1028, 1029, 1030, 1031, 1032, 1033, 1034, 1035, 1036, 1037, 1038, 1042, 1043, 1044, 1046, 1047, 1048, 1049, 1052, 1053, 1057, 1058, 1061, 1063, 1072, 1078, 1084, 1085, 1086, 1087, 1088, 1089, 1090, 1091, 1092, 1093, 1095, 1096, 1099, 1100, 1101, 1102, 1105, 1108, 1109, 1111, 1112, 1115, 1119, 1122, 1123, 1124, 1125, 1128, 1136, 1138, 1139, 1141, 1144, 1146, 1149, 1150, 1151, 1157, 1162, 1163, 1164, 1165, 1170, 1174, 1176, 1184, 1186, 1192, 1199, 1216, 1222, 1224, 1228, 1229, 1231, 1232, 1238, 1239, 1243, 1244, 1245, 1246, 1250, 1253, 1258, 1259, 1266, 1267, 1268, 1269, 1272, 1279, 1281, 1298, 1311, 1312, 1313, 1321, 1323, 1324, 1328, 1332, 1334, 1335, 1339, 1341, 1350, 1352, 1354, 1356, 1363, 1371, 1379, 1385, 1392, 1397, 1398, 1400, 1402, 1403, 1424, 1432, 1435, 1444, 1450, 1472, 1492, 1500, 1508, 1527, 1533, 1534, 1535, 1536, 1544, 1545, 1554, 1559, 1567, 1568, 1571, 1574, 1588, 1589, 1591, 1597, 1599, 1603, 1612, 1619, 1622, 1623, 1638, 1641, 1648, 1649, 1651, 1654, 1655, 1657, 1658, 1663, 1672, 1676, 1681, 1684, 1689, 1690, 1691, 1692, 1693, 1694, 1695, 1698, 1699, 1700, 1704, 1719, 1724, 1736, 1851, 1854, 1877, 1878, 1880, 1882, 1886, 1912, 1914, 1915, 1918, 1924, 1940, 1948, 1950, 1960, 1963, 1969, 1977, 1995, 1999, 2002, 2003, 2012, 2016, 2038, 2045, 2050, 2056, 2060, 2071, 2103, 2104, 2106, 2107, 2115, 2128, 2170, 2189, 2191, 2206, 2255, 2273, 2275, 2305, 2306, 2322, 2323, 2363, 2404, 2405, 2410, 2412, 2460, 2461, 2462, 2478, 2481, 2482, 2483, 2484, 2485, 2486, 2487, 2496, 2497, 2498, 2500, 2501, 2502, 2503, 2513, 2514, 2517, 2518, 2554, 2573, 2574, 2588, 2599, 2600, 2602, 2638, 2647, 2655, 2664, 2668, 2669, 2688, 2693, 2755, 2789, 2790, 2791, 2792, 2793, 2803, 2804, 2808, 2811, 2812, 2814, 2826, 2867, 2887, 2890, 2891, 2895, 2896, 2918, 2919, 2920, 2921, 2922, 2923, 2927, 2945, 2948, 2993, 2994, 2995, 3001, 3002, 3003, 3004, 3005, 3006, 3007, 3015, 3028, 3029, 3031, 3032, 3044, 3046, 3050, 3051, 3052, 3054, 3059, 3060, 3061, 3062, 3063, 3064, 3068, 3077, 3088, 3089, 3090, 3091, 3092, 3093, 3094, 3095, 3096, 3097, 3098, 3099, 3100, 3101, 3102, 3109, 3113, 3114, 3115, 3116, 3118, 3119, 3120, 3121, 3122, 3123, 3124, 3125, 3126, 3127, 3128, 3129, 3130, 3132, 3133, 3134, 3136, 3138, 3139, 3140, 3141, 3142, 3143, 3144, 3145, 3146, 3147, 3149, 3150, 3159, 3168, 3215, 3219, 3221, 3222, 3226, 3232, 3242, 3243, 3244, 3245, 3246, 3247, 3249, 3259, 3271, 3278, 3298, 3316, 3320, 3333, 3338, 3410, 3436, 3438, 3441, 3459, 3500, 3501, 3503, 3504, 3507, 3513, 3519, 3522, 3523, 3524, 3525, 3526, 3527, 3528, 3529, 3532, 3533, 3534, 3535, 3536, 3537, 3538, 3539, 3540, 3541, 3542, 3543, 3544, 3545, 3546, 3547, 3548, 3549, 3557, 3568, 3580, 3584, 3620, 3624, 3646, 3648, 3675, 3677, 3686, 3687, 3705, 3707, 3738, 3744, 3746, 3762, 3768, 3773, 3788, 3789, 3790, 3791, 3861, 3865, 3878, 3881, 3883, 3884, 3885, 3886, 3887, 3888, 3895, 3929, 3933, 3934, 3952, 3953, 3954, 3955, 3956, 3957, 3958, 3959, 3977, 3978, 3979, 3980, 3981, 3982, 3983, 3984, 3988, 3991, 4008, 4009, 4010, 4011, 4012, 4013, 4014, 4015, 4030, 4031, 4032, 4033, 4095, 4141, 4142, 4143, 4144, 4145, 4146, 4147, 4148, 4149, 4150, 4151, 4152, 4153, 4154, 4155, 4156, 4157, 4158, 4159, 4160, 4161, 4162, 4163, 4164, 4165, 4166, 4167, 4168, 4169, 4170, 4171, 4172, 4173, 4174, 4175, 4176, 4177, 4178, 4179, 4180, 4181, 4182, 4183, 4184, 4185, 4186, 4187, 4188, 4189, 4190, 4191, 4192, 4193, 4194, 4195, 4196, 4198, 4199, 4200, 4201, 4202, 4203, 4204, 4205, 4206, 4207, 4208, 4209, 4210, 4211, 4212, 4214, 4215, 4216, 4217, 4218, 4219, 4220, 4221, 4222, 4223, 4224, 4225, 4226, 4227, 4228, 4229, 4230, 4266, 4267, 4268, 4269, 4270, 4271, 4272, 4273, 4274, 4275, 4276, 4277, 4279, 4280, 4281, 4282, 4283, 4284, 4285, 4286, 4287, 4288, 4295, 4418, 4427, 4431, 4442, 4451, 4452, 4475, 4486, 4501, 4523, 4524, 4559, 4573, 4578, 4579, 4620, 4657, 4664, 4667, 4670, 4673, 4679, 4682, 4685, 4688, 4691, 4704, 4728, 4730, 4747, 4748, 4749, 4750, 4754, 4756, 4760, 4761, 4762, 4763, 4764, 4773, 4774, 4811, 4812, 4815, 4839, 4842, 4853, 4855, 4856, 4857, 4858, 4868, 4884, 4885, 4889, 4899, 4900, 4901, 4902, 4912, 4927, 4930, 4934, 4943, 4950, 4955, 4956, 4959, 4965, 4966, 4981, 4985, 4988, 4989, 4990, 4996, 4997, 5000, 5004, 5008, 5010, 5014, 5015, 5024, 5041, 5045, 5046, 5047, 5049, 5053, 5090, 5091, 5106, 5108, 5126, 5127, 5129, 5130, 5131, 5132, 5139, 5141, 5142, 5144, 5145, 5146, 5147, 5148, 5149, 5150, 5151, 5152, 5153, 5154, 5155, 5156, 5157, 5158, 5159, 5160, 5161, 5162, 5163, 5171, 5172, 5174, 5222, 5223, 5226, 5227, 5228, 5230, 5231, 5235, 5255, 5264, 5265, 5282, 5283, 5294, 5295, 5296, 5297, 5298, 5307, 5308, 5330, 5331, 5333, 5353, 5358, 5365, 5372, 5378, 5380, 5381, 5384, 5400, 5401, 5402, 5403, 5406, 5407, 5408, 5409, 5410, 5434, 5436, 5438, 5449, 5450, 5453, 5454, 5515, 5531, 5545, 5546, 5548, 5549, 5550, 5551, 5552, 5553, 5554, 5555, 5556, 5557, 5558, 5559, 5560, 5561, 5562, 5563, 5564, 5577, 5603, 5607, 5625, 5632, 5641, 5644, 5647, 5648, 5649, 5650, 5651, 5652, 5653, 5654, 5657, 5658, 5660, 5661, 5662, 5666, 5667, 5670, 5671, 5672, 5673, 5674, 5676, 5677, 5678, 5679, 5680, 5682, 5683, 5684, 5685, 5688, 5696, 5697, 5698, 5699, 5700, 5701, 5702, 5703, 5704, 5705, 5706, 5707, 5708, 5709, 5710, 5711, 5712, 5713, 5714, 5715, 5716, 5719, 5720, 5721, 5722, 5723, 5724, 5725, 5726, 5727, 5728, 5729, 5730, 5742, 5743, 5748, 5768, 5769, 5821, 5822, 5823, 5828, 5845, 5857, 5858, 5859, 5878, 5896, 5916, 5937, 5949, 5953, 5954, 5968, 6036, 6090, 6130, 6131, 6132, 6133, 6174, 6192, 6207, 6208, 6209, 6210, 6213, 6216, 6243, 6244, 6255, 6262, 6273, 6276, 6277, 6278, 6279, 6280, 6374, 6437, 6478, 6489, 6490, 6491, 6492, 6493, 6494, 6495, 6496, 6497, 6498, 6499, 6500, 6501, 6516, 6544, 6589, 6619, 6620, 6621, 6623, 6638, 6639, 6644, 6646, 6648, 6649, 6650, 6673, 6674, 6683, 6698, 6707, 6708, 6711, 6724, 6728, 6730, 6733, 6734, 6736, 6754, 6777, 6778, 6779, 6837, 6850, 6852, 6891, 6896, 6897, 6899, 6946, 6988, 7007, 7093, 7170, 7171, 7187, 7188, 7248, 7275, 7299, 7347, 7388, 7425, 7426, 7427, 7466, 7467, 7497, 7547, 7548, 7550, 7681, 7707, 7716, 7868, 7869, 7872, 7925, 7948, 7949, 7950, 7951, 7952, 7953, 7977, 7986, 7987, 7988, 7994, 8195, 8243, 8388, 8493, 8502, 8503, 8504, 8505, 8506, 8507, 8543, 8546, 8547, 8688, 8743, 8744, 8745, 8756, 8757, 8758, 8759, 8760, 8761, 8762, 8763, 8764, 8765, 8768, 8769, 8770, 8771, 8772, 8773, 8774, 8775, 8776, 8777, 8778, 8779, 8780, 8781, 8782, 8783, 8784, 8785, 8786, 8787, 8788, 8789, 8790, 8791, 8792, 8793, 8794, 8795, 8796, 8797, 8798, 8799, 8800, 8801, 8802, 8803, 8804, 8805, 8806, 8807, 8808, 8809, 8810, 8811, 8812, 8813, 8814, 8815, 8816, 8818, 8819, 8820, 8821, 8822, 8823, 8824, 8825, 8826, 8828, 8829, 8830, 8832, 8833, 8834, 8835, 8837, 8840, 8841, 8842, 8843, 8844, 8847, 8848, 8849, 8850, 8851, 8852, 8853, 8854, 8855, 8856, 8857, 8858, 8859, 8860, 8861, 8862, 8863, 8864, 8865, 8866, 8867, 8868, 8869, 8870, 8871, 8872, 8873, 8874, 8875, 8876, 8877, 8878, 8879, 8880, 8881, 8882, 8883, 8884, 8885, 8886, 8887, 8888, 8889, 8890, 8891, 8892, 8893, 8894, 8895, 8896, 8897, 8898, 8899, 8900, 8901, 8902, 8903, 8904, 8905, 8906, 8907, 8909, 8910, 8911, 8912, 8913, 8914, 8915, 8916, 8917, 8918, 8919, 8920, 8921, 8922, 8929, 8930, 8931, 8933, 8934, 8935, 8936, 8937, 8938, 8939, 8940, 8941, 8942, 8943, 8944, 8945, 8947, 8954, 8955, 8958, 8960, 8961, 8962, 8963, 8965, 8966, 8967, 8968, 8969, 8971, 8972, 8974, 8975, 8976, 8977, 8978, 8980, 8981, 8983, 8986, 8987, 8988, 8989, 8990, 8991, 8992, 8993, 8994, 8995, 8996, 8997, 8998, 8999, 9000, 9001, 9002, 9003, 9004, 9005, 9006, 9007, 9008, 9009, 9010, 9011, 9012, 9013, 9014, 9015, 9016, 9017, 9018, 9019, 9020, 9021, 9022, 9023, 9024, 9025, 9026, 9027, 9028, 9029, 9031, 9032, 9033, 9034, 9035, 9037, 9039, 9040, 9041, 9043, 9044, 9046, 9047, 9048, 9049, 9050, 9051, 9052, 9053, 9054, 9055, 9056, 9057, 9058, 9059, 9062, 9063, 9064, 9065, 9066, 9067, 9068, 9069, 9070, 9071, 9072, 9073, 9074, 9075, 9076, 9077, 9078, 9079, 9080, 9081, 9082, 9083, 9084, 9085, 9086, 9087, 9089, 9090, 9091, 9092, 9093, 9094, 9095, 9096, 9097, 9098, 9099, 9100, 9101, 9102, 9103, 9104, 9105, 9123, 9124, 9125, 9126, 9127, 9128, 9129, 9130, 9131, 9132, 9133, 9134, 9135, 9136, 9137, 9138, 9139, 9140, 9141, 9142, 9143, 9145, 9146, 9147, 9148, 9150, 9151, 9152, 9156, 9157, 9158, 9159, 9160, 9161, 9162, 9164, 9165, 9166, 9167, 9168, 9169, 9170, 9171, 9174, 9175, 9176, 9177, 9178, 9180, 9181, 9182, 9183, 9184, 9185, 9188, 9190, 9191, 9192, 9193, 9194, 9195, 9198, 9199, 9200, 9201, 9202, 9203, 9204, 9205, 9207, 9208, 9209, 9211, 9212, 9215, 9216, 9217, 9218, 9219, 9221, 9222, 9223, 9225, 9226, 9227, 9228, 9229, 9230, 9231, 9325, 9380, 9417, 9443, 9464, 9529, 9532, 9537, 9685, 9888, 10010, 10011, 10020, 10032, 10038, 10039, 10049, 10303, 10304, 10313, 10319, 10322, 10324, 10478, 10555, 10579, 10580, 10585, 10594, 10595, 10596, 10650, 10651, 10683, 10719, 10723, 11085, 11099, 11111, 11115, 11170, 11171, 11198, 11199, 11200, 11201, 11228, 11442, 11443, 11473, 11505, 11609, 11613, 11616, 11663, 11664, 11666, 11667, 11670, 11671, 11672, 11673, 11676, 11683, 11838, 11903, 12104, 12105, 12106, 12107, 12143, 12186, 12187, 12188, 12189, 12211, 12244, 12245, 12258, 12369, 12385, 12440, 12442, 12443, 12468, 12469, 12526, 12585, 12615, 12616, 12617, 12729, 12762, 12763, 12764, 12769, 12778, 12779, 12789, 12795, 12802, 12805, 12816, 12817, 12818, 12826, 12831, 12832, 12853, 12904, 12931, 12961, 12962, 12970, 12971, 12972, 12986, 13080, 13090, 13092, 13149, 13151, 13152, 13153, 13154, 13214, 13223, 13242, 13247, 13291, 13318, 13330, 13338, 13342, 13343, 13500, 13503, 13517, 13543, 13586, 13608, 13642, 13643, 13644, 13645, 13646, 13647, 13648, 13649, 13650, 13651, 13652, 13653, 13654, 13655, 13656, 13657, 13658, 13659, 13660, 13661, 13662, 13663, 13664, 13665, 13666, 13667, 13668, 13669, 13670, 13671, 13672, 13673, 13674, 13675, 13676, 13677, 13678, 13679, 13680, 13681, 13682, 13683, 13684, 13685, 13686, 13687, 13688, 13689, 13690, 13691, 13692, 13693, 13694, 13695, 13696, 13697, 13710, 13711, 13712, 13713, 13714, 13715, 13716, 13717, 13726, 13727, 13728, 13729, 13730, 13731, 13732, 13733, 13734, 13735, 13736, 13737, 13738, 13739, 13740, 13741, 13742, 13743, 13744, 13745, 13746, 13747, 13748, 13749, 13762, 13763, 13764, 13765, 13766, 13767, 13768, 13769, 13770, 13771, 13772, 13773, 13774, 13775, 13776, 13777, 13778, 13779, 13780, 13781, 13782, 13783, 13784, 13785, 13786, 13787, 13788, 13789, 13790, 13791, 13792, 13793, 13794, 13795, 13796, 13797, 13798, 13799, 13800, 13801, 13802, 13803, 13804, 13805, 13806, 13807, 13808, 13809, 13811, 13812, 13842, 13843, 13844, 13845, 13846, 13847, 13848, 13849, 13862, 13923, 13936, 14083, 14363, 14382, 14383, 14384, 14385, 14386, 14387, 14388, 14389, 14390, 14391, 14392, 14393, 14394, 14524, 14550, 14597, 14609, 14691, 14696, 14818, 14822, 14871, 14876, 14878, 14883, 14884, 14885, 14886, 14887, 14888, 14889, 14890, 14891, 14892, 15446, 15586, 15688, 15769, 15780, 15888, 15889, 16024, 16025, 16026, 16027, 16028, 16029, 16030, 16031, 16033, 16034, 16035, 16036, 16037, 16038, 16061, 16062, 16063, 16064, 16065, 16066, 16067, 16068, 16069, 16070, 16071, 16073, 16074, 16075, 16076, 16077, 16078, 16079, 16080, 16081, 16082, 16085, 16086, 16102, 16103, 16104, 16105, 16106, 16107, 16108, 16109, 16116, 16117, 16118, 16119, 16120, 16121, 16122, 16123, 16124, 16125, 16126, 16127, 16129, 16131, 16132, 16134, 16135, 16136, 16137, 16138, 16139, 16140, 16141, 16142, 16143, 16144, 16145, 16146, 16147, 16148, 16149, 16150, 16151, 16152, 16153, 16154, 16155, 16156, 16157, 16158, 16159, 16160, 16161, 16162, 16163, 16164, 16172, 16173, 16174, 16175, 16176, 16177, 16178, 16179, 16180, 16181, 16182, 16183, 16184, 16185, 16186, 16187, 16188, 16191, 16211, 16212, 16213, 16308, 16315, 16336, 16337, 16338, 16343, 16344, 16367, 16370, 16394, 16395, 16398, 16399, 16400, 16402, 16404, 16407, 16411, 16412, 16438, 16439, 16445, 16447, 16458, 16460, 16461, 16464, 16469, 16470, 16481, 16482, 16488, 16493, 16495, 16500, 16511, 16512, 16517, 16520, 16529, 16537, 16538, 16546, 16547, 16553, 16556, 16557, 16559, 16570, 16572, 16575, 16576, 16664, 16792, 17000, 17024, 17027, 17040, 17041, 17108, 17115, 17116, 17122, 17142, 17162, 17163, 17199, 17342, 17343, 17347, 17354, 17409, 17412, 17563, 17565, 17574, 17575, 17582, 17585, 17587, 17589, 17595, 17597, 17606, 17609, 17614, 17615, 17619, 17621, 17731, 17769, 17783, 17802, 17824, 17825, 17826, 17827, 17828, 17829, 17830, 17831, 17832, 17833, 17834, 17835, 17836, 17837, 17838, 17839, 17840, 17841, 17842, 17843, 17844, 17845, 17846, 17847, 17848, 17851, 17852, 17853, 17854, 17855, 17856, 17857, 17858, 17859, 17860, 17861, 17862, 17882, 17883, 17884, 17885, 17886, 17887, 17888, 17889, 17890, 17891, 17892, 17893, 17894, 17895, 17896, 17897, 17898, 17899, 17910, 17911, 18023, 18063, 18105, 18106, 18153, 18155, 18156, 18157, 18158, 18159, 18161, 18162, 18163, 18164, 18165, 18209, 18235, 18303, 18304, 18316, 18320, 18341, 18342, 18355, 18438, 18589, 18593, 18595, 18599, 18627, 18630, 18666, 18667, 18668, 18669, 18685, 18747, 18763, 18764, 18765, 18800, 18801, 18881, 18882, 18942, 18963, 18964, 18965, 18966, 18967, 18968, 18971, 18982, 19065, 19082, 19122, 19129, 19158, 19184, 19185, 19186, 19187, 19188, 19189, 19190, 19191, 19192, 19193, 19194, 19195, 19196, 19197, 19198, 19199, 19200, 19201, 19226, 19286, 19313, 19314, 19427, 19428, 19455, 19456, 19457, 19482, 19486, 19487, 19488, 19489, 19490, 19502, 19503, 19504, 19622, 19642, 19662, 19742, 19743, 19804, 19809, 19810, 19811, 19837, 19844, 19847, 19868, 19926, 19932, 19966, 19983, 19986, 19987, 19989, 20003, 20005, 20020, 20024, 20026, 20084, 20135, 20136, 20137, 20138, 20139, 20140, 20141, 20142, 20143, 20144, 20145, 20146, 20149, 20178, 20179, 20180, 20182, 20183, 20185, 20238, 20239, 20240, 20241, 20242, 20245, 20246, 20247, 20248, 20249, 20250, 20251, 20252, 20267, 20268, 20269, 20270, 20271, 20272, 20273, 20274, 20275, 20276, 20277, 20278, 20279, 20280, 20281, 20282, 20283, 20284, 20285, 20286, 20287, 20288, 20289, 20290, 20291, 20292, 20297, 20298, 20299, 20300, 20301, 20302, 20303, 20304, 20305, 20306, 20307, 20308, 20309, 20311, 20312, 20313, 20314, 20315, 20316, 20317, 20318, 20319, 20320, 20321, 20322, 20323, 20324, 20325, 20326, 20327, 20328, 20329, 20330, 20331, 20332, 20333, 20334, 20335, 20336, 20337, 20338, 20339, 20340, 20341, 20342, 20343, 20344, 20345, 20346, 20347, 20348, 20349, 20350, 20351, 20352, 20353, 20354, 20355, 20356, 20357, 20358, 20359, 20360, 20361, 20362, 20363, 20364, 20367, 20368, 20370, 20372, 20423, 20445, 20446, 20460, 20462, 20475, 20485, 20489, 20502, 20522, 20524, 20525, 20583, 20584, 20585, 20586, 20587, 20588, 20589, 20590, 20591, 20592, 20593, 20594, 20595, 20596, 20597, 20598, 20609, 20651, 20737, 20739, 20740, 20814, 20834, 20883, 20887, 20908, 20936, 20937, 20946, 21043, 21101, 21102, 21124, 21125, 21127, 21135, 21141, 21152, 21159, 21163, 21168, 21173, 21193, 21194, 21195, 21236, 21238, 21240, 21246, 21247, 21274, 21276, 21313, 21339, 21369, 21419, 21420, 21421, 21422, 21423, 21424, 21425, 21426, 21427, 21428, 21429, 21430, 21431, 21432, 21433, 21434, 21435, 21437, 21439, 21440, 21441, 21442, 21443, 21444, 21445, 21446, 21447, 21448, 21449, 21450, 21451, 21516, 21518, 21550, 21560, 21575, 21577, 21578, 21584, 21588, 21591, 21594, 21612, 21613, 21614, 21628, 21629, 21630, 21631, 21632, 21633, 21634, 21636, 21637, 21638, 21641, 21642, 21643, 21644, 21646, 21649, 21653, 21655, 21656, 21657, 21658, 21659, 21660, 21661, 21662, 21717, 21719, 21720, 21736, 21739, 21782, 21795, 21811, 21890, 21923, 21930, 21962, 21963, 21964, 22020, 22021, 22022, 22023, 22024, 22025, 22026, 22027, 22028, 22029, 22030, 22031, 22032, 22033, 22034, 22035, 22036, 22037, 22038, 22039, 22040, 22041, 22042, 22043, 22045, 22114, 22130, 22151, 22152, 22230, 22233, 22258, 22273, 22316, 22346, 22386, 22387, 22391, 22485, 22486, 22584, 22585, 22586, 22587, 22588, 22619, 22625, 22626, 22684, 22685, 22686, 22687, 22692, 22694, 22695, 22696, 22697, 22698, 22703, 22704, 22705, 22765, 22780, 22781, 22805, 22814, 22817, 22933, 23034, 23058, 23072, 23086, 23162, 23163, 23164, 23172, 23175, 23176, 23215, 23224, 23227, 23245, 23271, 23325, 23360, 23418, 23567, 23656, 23683, 23684, 23689, 23690, 23696, 23698, 23699, 23700, 23701, 23712, 23713, 23714, 23715, 23716, 23718, 23719, 23721, 23722, 23725, 23727, 23728, 23794, 23795, 23796, 24071, 24358, 2556, 17, 192, 2248, 2301, 2543, 2952, 3038, 3043, 3104, 3105, 3106, 3359, 3398, 3479, 3579, 3896, 4081, 4574, 4642, 5031, 5032, 5033, 5034, 5035, 5036, 5037, 5039, 5070, 5290, 6128, 6141, 6142, 6143, 6606, 7066, 7167, 7169, 7677, 7940, 9042, 9239, 9376, 9377, 10006, 10037, 10422, 11182, 11183, 11344, 11345, 12221, 12222, 12246, 12333, 12407, 12413, 12423, 12686, 12767, 12948, 13256, 13294, 13472, 13516, 13919, 14689, 14690, 14692, 14693, 14694, 14695, 14697, 14698, 14699, 14700, 14701, 14702, 14703, 14704, 14705, 14819, 15089, 16128, 16130, 16133, 16334, 16340, 18439, 18732, 18733, 19285, 19294, 19359, 19985, 20133, 20294, 20386, 20523, 20529, 20880, 20905, 21169, 21170, 21172, 21797, 21798, 21799, 21832, 22751 };

        for (uint8 clazz = CLASS_WARRIOR; clazz < MAX_CLASSES; ++clazz)
        {
            // skip nonexistent classes
            if (!((1 << (clazz - 1)) & CLASSMASK_ALL_PLAYABLE) || !sChrClassesStore.LookupEntry(clazz))
                continue;

            for (uint32 level = 1; level <= maxLevel; ++level)
            {
                for (uint8 slot = 0; slot < EQUIPMENT_SLOT_END; ++slot)
                {
                    for (uint32 spec = 1; spec <= MAX_STAT_SCALES; ++spec)
                    {
                        if (!m_weightScales[spec].info.id)
                            continue;

                        if (m_weightScales[spec].info.classId != clazz)
                            continue;

                        for (uint32 quality = ITEM_QUALITY_POOR; quality <= ITEM_QUALITY_ARTIFACT; ++quality)
                        {
                            BotEquipKey key(level, clazz, spec, slot, quality);

                            RandomItemList items;
                            for (uint32 itemId = 0; itemId < sItemStorage.GetMaxEntry(); ++itemId)
                            {
                                ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
                                if (!proto)
                                    continue;

                                if (proto->Quality != key.quality)
                                    continue;

                                // skip not available items
                                // TO DO: Replace this with a db query
                                if (unavailableItemIDs.find(itemId) != unavailableItemIDs.end())
                                    continue;

                                if ((slot == EQUIPMENT_SLOT_BODY || slot == EQUIPMENT_SLOT_TABARD))
                                {
                                    std::set<InventoryType> slots = viableSlots[(EquipmentSlots)key.slot];
                                    if (slots.find((InventoryType)proto->InventoryType) == slots.end())
                                        continue;

                                    if (slot == EQUIPMENT_SLOT_BODY && std::find(shirtsList.begin(), shirtsList.end(), itemId) == shirtsList.end())
                                        shirtsList.push_back(itemId);
                                    if (slot == EQUIPMENT_SLOT_TABARD && std::find(tabardsList.begin(), tabardsList.end(), itemId) == tabardsList.end())
                                        tabardsList.push_back(itemId);

                                    CharacterDatabase.PExecute("replace into ai_playerbot_equip_cache (id, clazz, spec, lvl, slot, quality, item) values (%u, %u, %u, %u, %u, %u, %u)",
                                        2000000 + itemId, 1, 1, 60, slot, 1, itemId);

                                    continue;
                                }

                                // check stat weight
                                uint32 statWeight = GetStatWeight(itemId, spec);
                                if (statWeight <= 0)
                                    continue;

                                // only accept "useless" items if bot level <= 30
                                if (statWeight == 1 && level > 30 && !proto->RandomProperty)
                                    continue;

                                uint32 minLevel = GetMinLevelFromCache(itemId);
                                // skip higher level (e.g. quest rewards)
                                if (minLevel > level)
                                    continue;

                                if (abs((int)minLevel - (int)level) > 20)
                                    continue;

                                /*if (proto->Class == ITEM_CLASS_WEAPON && abs((int)minLevel - (int)level) > 10)
                                    continue;*/

                                if (proto->Class != ITEM_CLASS_WEAPON &&
                                    proto->Class != ITEM_CLASS_ARMOR &&
                                    proto->Class != ITEM_CLASS_CONTAINER &&
                                    proto->Class != ITEM_CLASS_PROJECTILE)
                                    continue;

                                if (!CanEquipItem(key, proto))
                                    continue;

                                if (proto->Class == ITEM_CLASS_ARMOR && (
                                    slot == EQUIPMENT_SLOT_HEAD ||
                                    slot == EQUIPMENT_SLOT_SHOULDERS ||
                                    slot == EQUIPMENT_SLOT_CHEST ||
                                    slot == EQUIPMENT_SLOT_WAIST ||
                                    slot == EQUIPMENT_SLOT_LEGS ||
                                    slot == EQUIPMENT_SLOT_FEET ||
                                    slot == EQUIPMENT_SLOT_WRISTS ||
                                    slot == EQUIPMENT_SLOT_HANDS) && !CanEquipArmor(key.clazz, key.spec, key.level, proto))
                                    continue;

                                //if (proto->Class == ITEM_CLASS_WEAPON && !CanEquipWeapon(key.clazz, proto))
                                //    continue;

                                if (slot == EQUIPMENT_SLOT_OFFHAND && key.clazz == CLASS_ROGUE && proto->Class != ITEM_CLASS_WEAPON)
                                    continue;

                                items.push_back(itemId);

                                CharacterDatabase.PExecute("insert into ai_playerbot_equip_cache (clazz, spec, lvl, slot, quality, item) values (%u, %u, %u, %u, %u, %u)",
                                    clazz, spec, level, slot, quality, itemId);
                            }

                            equipCache[key] = items;
                            specItemCounts[spec] += items.size();
                            bar.step();
                            sLog.outDetail("Equipment cache for class: %d, level %d, slot %d, quality %d: %zu items",
                                clazz, level, slot, quality, items.size());
                        }
                    }
                }
            }

            // The class loop is outermost, so once we leave a class body every spec
            // belonging to that class is fully built. Emit one visible line per spec.
            for (uint32 spec = 1; spec <= MAX_STAT_SCALES; ++spec)
            {
                if (!m_weightScales[spec].info.id || m_weightScales[spec].info.classId != clazz)
                    continue;

                sLog.outBasic("[GearCache] class %u spec %u (%s): cached %llu items",
                    clazz, spec, m_weightScales[spec].info.name.c_str(),
                    (unsigned long long)specItemCounts[spec]);
            }
        }
        equipCache[tabardKey] = tabardsList;
        equipCache[shirtKey] = shirtsList;
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
	sLog.outString("Cached %d types of ammo", counter1); // TEST
}

uint32 RandomItemMgr::GetAmmo(uint32 level, uint32 subClass)
{
    return ammoCache[(level - 1) / 10][subClass];
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

std::vector<uint32> RandomItemMgr::GetGemsList()
{
    std::vector<uint32>_gems;

    return _gems;
}

void RandomItemMgr::BuildRarityCache()
{
    auto results = CharacterDatabase.PQuery("select item, rarity from ai_playerbot_rarity_cache");
    if (results)
    {
        sLog.outBasic("Loading item rarity cache");
        int count = 0;
        do
        {
            Field* fields = results->Fetch();
            uint32 itemId = fields[0].GetUInt32();
            float rarity = fields[1].GetFloat();

            rarityCache[itemId] = rarity;
            count++;

        } while (results->NextRow());
        sLog.outString("Item rarity cache loaded from %d records", count);
    }
    else
    {
        sLog.outBasic("Building item rarity cache from %u items", sItemStorage.GetMaxEntry());
        BarGoLink bar(sItemStorage.GetMaxEntry());
        for (uint32 itemId = 0; itemId < sItemStorage.GetMaxEntry(); ++itemId)
        {
            bar.step();
            ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
            if (!proto)
                continue;

            if (proto->Duration & 0x80000000)
                continue;

            if (proto->Quality == ITEM_QUALITY_POOR)
                continue;

            if (strstri(proto->Name1, "qa") || strstri(proto->Name1, "test") || strstri(proto->Name1, "deprecated"))
                continue;

            if (!proto->ItemLevel)
                continue;
            auto results = WorldDatabase.PQuery(
                    "select max(q.chance) from ( "
                    // "-- Creature "
                    "select  "
                    "avg ( "
                    "   case  "
                    "    when lt.groupid = 0 then lt.ChanceOrQuestChance  "
                    "    when lt.ChanceOrQuestChance > 0 then lt.ChanceOrQuestChance "
                    "    else   "
                    "    ifnull(100 - (select sum(ChanceOrQuestChance) from creature_loot_template lt1 where lt1.groupid = lt.groupid and lt1.entry = lt.entry and lt1.ChanceOrQuestChance > 0), 100) "
                    "    / (select count(*) from creature_loot_template lt1 where lt1.groupid = lt.groupid and lt1.entry = lt.entry and lt1.ChanceOrQuestChance = 0) "
                    "    end "
                    ") chance, 'creature' type "
                    "from creature_loot_template lt "
                    "join creature_template ct on ct.loot_id = lt.entry "
                    "join creature c on c.id = ct.entry "
                    "where lt.item = '%u' "
                    "union all "
                    // "-- Gameobject "
                    "select  "
                    "avg ( "
                    "   case  "
                    "    when lt.groupid = 0 then lt.ChanceOrQuestChance  "
                    "    when lt.ChanceOrQuestChance > 0 then lt.ChanceOrQuestChance "
                    "    else   "
                    "    ifnull(100 - (select sum(ChanceOrQuestChance) from gameobject_loot_template lt1 where lt1.groupid = lt.groupid and lt1.entry = lt.entry and lt1.ChanceOrQuestChance > 0), 100) "
                    "    / (select count(*) from gameobject_loot_template lt1 where lt1.groupid = lt.groupid and lt1.entry = lt.entry and lt1.ChanceOrQuestChance = 0) "
                    "    end "
                    ") chance, 'gameobject' type "
                    "from gameobject_loot_template lt "
                    "join gameobject_template ct on ct.data1 = lt.entry "
                    "join gameobject c on c.id = ct.entry "
                    "where lt.item = '%u' "
                    "union all "
                    // "-- Disenchant "
                    "select  "
                    "avg ( "
                    "   case  "
                    "    when lt.groupid = 0 then lt.ChanceOrQuestChance  "
                    "    when lt.ChanceOrQuestChance > 0 then lt.ChanceOrQuestChance "
                    "    else   "
                    "    ifnull(100 - (select sum(ChanceOrQuestChance) from disenchant_loot_template lt1 where lt1.groupid = lt.groupid and lt1.entry = lt.entry and lt1.ChanceOrQuestChance > 0), 100) "
                    "    / (select count(*) from disenchant_loot_template lt1 where lt1.groupid = lt.groupid and lt1.entry = lt.entry and lt1.ChanceOrQuestChance = 0) "
                    "    end "
                    ") chance, 'disenchant' type "
                    "from disenchant_loot_template lt "
                    "join item_template ct on ct.DisenchantID = lt.entry "
                    "where lt.item = '%u' "
                    "union all "
                    // "-- Fishing "
                    "select  "
                    "avg ( "
                    "   case  "
                    "    when lt.groupid = 0 then lt.ChanceOrQuestChance  "
                    "    when lt.ChanceOrQuestChance > 0 then lt.ChanceOrQuestChance "
                    "    else   "
                    "    ifnull(100 - (select sum(ChanceOrQuestChance) from fishing_loot_template lt1 where lt1.groupid = lt.groupid and lt1.entry = lt.entry and lt1.ChanceOrQuestChance > 0), 100) "
                    "    / (select count(*) from fishing_loot_template lt1 where lt1.groupid = lt.groupid and lt1.entry = lt.entry and lt1.ChanceOrQuestChance = 0) "
                    "    end "
                    ") chance, 'fishing' type "
                    "from fishing_loot_template lt "
                    "where lt.item = '%u' "
                    "union all "
                    // "-- Skinning "
                    "select  "
                    "avg ( "
                    "   case  "
                    "    when lt.groupid = 0 then lt.ChanceOrQuestChance  "
                    "    when lt.ChanceOrQuestChance > 0 then lt.ChanceOrQuestChance  "
                    "    else   "
                    "    ifnull(100 - (select sum(ChanceOrQuestChance) from skinning_loot_template lt1 where lt1.groupid = lt.groupid and lt1.entry = lt.entry and lt1.ChanceOrQuestChance > 0), 100) "
                    "    * ifnull((select 1/count(*) from skinning_loot_template lt1 where lt1.groupid = lt.groupid and lt1.entry = lt.entry and lt1.ChanceOrQuestChance = 0), 1) "
                    "    end "
                    ") chance, 'skinning' type "
                    "from skinning_loot_template lt "
                    "join creature_template ct on ct.skinning_loot_id = lt.entry "
                    "join creature c on c.id = ct.entry "
                    "where lt.item = '%u' "
                    ") q; ",
                             itemId,itemId,itemId,itemId,itemId);

            if (results)
            {
                Field* fields = results->Fetch();
                float rarity = fields[0].GetFloat();
                if (rarity > 0.01)
                {
                    rarityCache[itemId] = rarity;

                    CharacterDatabase.PExecute("insert into ai_playerbot_rarity_cache (item, rarity) values (%u, %f)",
                            itemId, rarity);
                }
            }
        }
        sLog.outString("Item rarity cache built from %u items", sItemStorage.GetMaxEntry());
    }
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

float RandomItemMgr::GetItemRarity(uint32 itemId)
{
    return rarityCache[itemId];
}
