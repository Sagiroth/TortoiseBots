
#include "playerbot/playerbot.h"
#include "ItemUsageValue.h"
#include "CraftValues.h"
#include "playerbot/EquipThresholdPolicy.h"
#include "MountValues.h"
#include "AmmoCheatPolicy.h"
#include "BudgetValues.h"
#include "GuildValues.h"

#include "playerbot/RandomItemMgr.h"
#include "playerbot/AiFactory.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/QuestLogPolicy.h"
#include "../../../../runtime/ClassConsumablePolicy.h"

using namespace ai;

namespace
{
uint32 GetAuctionItemCount(AuctionEntry const& auction)
{
    Item* item = sAuctionMgr.GetAItem(auction.itemGuidLow);
    return item ? item->GetCount() : 0;
}

// What a piece changes on the character sheet when it carries no stat the
// bot's weight scale knows: armour and block for armour, DPS for weapons.
// ItemStatWeight scores such a piece 0 for every spec but protection paladin
// and feral tank (only those weight "armor" in ai_playerbot_weightscale_data),
// so two low-level pieces tie at 0 and the winner used to come from quality +
// item level - which let the white ilvl-1 starter kit outrank grey drops with
// ten times the armour. Used only to break an exact tie of the weighted
// scores, so a piece with a real stat always outranks a bigger stat-less one.
float ItemSheetValue(ItemPrototype const* proto)
{
    if (proto->IsWeapon())
    {
        if (proto->Delay <= 0)
            return 0;

        float best = 0;
        for (int i = 0; i < MAX_ITEM_PROTO_DAMAGES; ++i)
        {
            if (proto->Damage[i].DamageMax == 0)
                continue;

            best = std::max(best, (proto->Damage[i].DamageMin + proto->Damage[i].DamageMax) / 2.0f / (proto->Delay / 1000.0f));
        }

        return best;
    }

    return static_cast<float>(proto->Armor) + static_cast<float>(proto->Block);
}
}

std::unordered_map<uint32, std::unordered_set<uint32>> ItemUsageValue::m_reagentItemIdsForCraftingSkills;
std::unordered_set<uint32> ItemUsageValue::m_allReagentItemIdsForCraftingSkills;
std::vector<uint32> ItemUsageValue::m_allReagentItemIdsForCraftingSkillsVector;

std::unordered_map<uint32, std::vector<std::pair<uint32, uint32>>> ItemUsageValue::m_craftingReagentItemIdsForCraftableItem;

std::unordered_set<uint32> ItemUsageValue::m_allItemIdsSoldByAnyVendors;
std::unordered_set<uint32> ItemUsageValue::m_itemIdsSoldByAnyVendorsWithLimitedMaxCount;

ItemQualifier::ItemQualifier(std::string qualifier) : itemId(0), enchantId(0), randomPropertyId(0), proto(nullptr) {
    std::vector<std::string> numbers = Qualified::getMultiQualifiers(qualifier, ":");

    if (numbers.empty())
        return;

    if (!std::all_of(numbers[0].begin(), numbers[0].end(), ::isdigit))
        return;

    itemId = stoi(numbers[0]);

    if (numbers.size() > 1 && !numbers[1].empty())
        enchantId = stoi(numbers[1]);

    if (numbers.size() > 2 && !numbers[2].empty())
        randomPropertyId = stoi(numbers[2]);

}

ItemUsage ItemUsageValue::Calculate()
{
    ItemQualifier itemQualifier(qualifier);
    uint32 itemId = itemQualifier.GetId();
    if (!itemId)
        return ItemUsage::ITEM_USAGE_NONE;

    const ItemPrototype* proto = sObjectMgr.GetItemPrototype(itemId);
    if (!proto)
        return ItemUsage::ITEM_USAGE_NONE;

    //FORCE
    ForceItemUsage forceUsage = AI_VALUE2_EXISTS(ForceItemUsage, "force item usage", proto->ItemId, ForceItemUsage::FORCE_USAGE_NONE);

    if (forceUsage == ForceItemUsage::FORCE_USAGE_GREED)
        return ItemUsage::ITEM_USAGE_FORCE_GREED;

    if (forceUsage == ForceItemUsage::FORCE_USAGE_NEED)
        return ItemUsage::ITEM_USAGE_FORCE_NEED;

    if (forceUsage == ForceItemUsage::FORCE_USAGE_KEEP)
    {
        ItemUsage equip = QueryItemUsageForEquip(itemQualifier, bot);
        if (equip == ItemUsage::ITEM_USAGE_EQUIP || equip == ItemUsage::ITEM_USAGE_BAD_EQUIP || equip == ItemUsage::ITEM_USAGE_BROKEN_EQUIP)
            return equip;
        return ItemUsage::ITEM_USAGE_KEEP;
    }

    if (forceUsage == ForceItemUsage::FORCE_USAGE_EQUIP)
    {
        ItemUsage equip = QueryItemUsageForEquip(itemQualifier, bot);
        if (equip == ItemUsage::ITEM_USAGE_EQUIP || equip == ItemUsage::ITEM_USAGE_BAD_EQUIP || equip == ItemUsage::ITEM_USAGE_BROKEN_EQUIP)
            return equip;
        if (bot->CanUseItem(proto) == EQUIP_ERR_OK && proto->InventoryType != INVTYPE_NON_EQUIP)
            return ItemUsage::ITEM_USAGE_EQUIP;
        return ItemUsage::ITEM_USAGE_KEEP;
    }

    if (forceUsage == ForceItemUsage::FORCE_USAGE_BAG)
        return ItemUsage::ITEM_USAGE_KEEP;

    if (bot->GetGuildId())
    {
        std::vector<GuildShareItemEntry> shareList = AI_VALUE(std::vector<GuildShareItemEntry>, "guild share list");
        if (!shareList.empty())
        {
            for (const auto& entry : shareList)
            {
                if (entry.itemId == itemId)
                    return ItemUsage::ITEM_USAGE_KEEP;

                ItemPrototype const* craftProto = sObjectMgr.GetItemPrototype(entry.itemId);
                if (craftProto)
                {
                    std::vector<std::pair<uint32, uint32>> reagents = GetAllReagentItemIdsForCraftingItem(craftProto);
                    for (const auto& [reagentId, reagentCount] : reagents)
                    {
                        if (reagentId == itemId)
                            return ItemUsage::ITEM_USAGE_KEEP;
                    }
                }
            }
        }
    }

    // Keep the Tortoise hearthstone.
    if (proto->ItemId == 6948)
        return ItemUsage::ITEM_USAGE_KEEP;

    // A fishing pole is a tool: the fish action equips it for fishing and nothing else does.
    // With a stat on it (Strong Fishing Pole, +5 fishing) it counted as a weapon upgrade, so
    // "equip upgrades" - which runs whenever a bot holds a pole and is not casting - took it
    // off and put it right back, 5,000 swaps per minute and bot. The best pole the bot owns
    // is kept, a lesser spare is junk (one bot carried four spare poles).
    if (proto->Class == ITEM_CLASS_WEAPON && proto->SubClass == ITEM_SUBCLASS_WEAPON_FISHING_POLE)
    {
        if (!ai->HasSkill(SKILL_FISHING))
            return ItemUsage::ITEM_USAGE_NONE;
        std::list<Item*> poles = AI_VALUE2(std::list<Item*>, "inventory items", "fishing pole");
        for (Item* pole : poles)
            if (pole->GetProto()->ItemLevel > proto->ItemLevel)
                return ItemUsage::ITEM_USAGE_NONE;
        return ItemUsage::ITEM_USAGE_KEEP;
    }

    //WARLOCKS GOT TO KEEP SOULSHARDS (keep at most 5; excess is destroyed out
    //of combat by the "too many soul shards" trigger)
    if (bot->GetClass() == CLASS_WARLOCK && proto->ItemId == 6265 && CurrentStacks(ai, proto) <= 5)
        return ItemUsage::ITEM_USAGE_KEEP;

    // Class consumables upkeep (c-cons, r-poisons finding 14): poisons the
    // bot's class is masked for, and stones/oils its class actually uses,
    // are never vendor trash or AH stock while level-appropriate - the seed
    // side (AddConsumables/TopUpConsumableFamily, refreshed on ding) plants
    // exactly these entries, so selling them just re-buys the same stack.
    // KEEP also shields them from the full-bag destroy list, like food/ammo.
    // Rules live in runtime/ClassConsumablePolicy.h (unit-tested).
    if (proto->Class == ITEM_CLASS_TRADE_GOODS || proto->Class == ITEM_CLASS_CONSUMABLE)
    {
        uint32_t botClass = bot->GetClass();
        uint32_t botLevel = bot->GetLevel();
        if (TortoiseBots::IsStoneEntry(proto->ItemId))
        {
            if (TortoiseBots::ShouldKeepStone(botClass, proto->RequiredLevel, botLevel))
                return ItemUsage::ITEM_USAGE_KEEP;
        }
        else if (TortoiseBots::IsOilEntry(proto->ItemId))
        {
            if (TortoiseBots::ShouldKeepOil(botClass, proto->RequiredLevel, botLevel))
                return ItemUsage::ITEM_USAGE_KEEP;
        }
        else if (proto->Class == ITEM_CLASS_CONSUMABLE &&
                 TortoiseBots::ShouldKeepClassMaskedConsumable(botClass, proto->AllowableClass, proto->RequiredLevel, botLevel))
        {
            return ItemUsage::ITEM_USAGE_KEEP;
        }
    }

    //SKILL
    if (ai->HasActivePlayerMaster())
    {
        if (IsItemUsefulForSkill(proto))
            return ItemUsage::ITEM_USAGE_SKILL;

        if (IsItemNeededForSkill(proto))
        {
            float stacks = CurrentStacks(ai, proto);
            if (stacks < 1)
                return ItemUsage::ITEM_USAGE_SKILL; //Buy more.
            else if (stacks == 1)
                return ItemUsage::ITEM_USAGE_KEEP; //Keep in inventory.
        }
    }
    else
    {
        bool needItem = false;

        if (IsItemNeededForSkill(proto))
        {
            float stacks = CurrentStacks(ai, proto);
            if (stacks < 1)
                return ItemUsage::ITEM_USAGE_SKILL; //Buy more.
            else if (stacks == 1)
                return ItemUsage::ITEM_USAGE_KEEP; //Keep in inventory.
        }
        else
        {
            bool lowBagSpace = AI_VALUE(uint8, "bag space") > 50;

            if (proto->Class == ITEM_CLASS_TRADE_GOODS || proto->Class == ITEM_CLASS_MISC || proto->Class == ITEM_CLASS_REAGENT)
                needItem =!ai->HasCheat(BotCheatMask::item) && IsItemNeededForUsefullCraft(proto, lowBagSpace);
            else if (proto->Class == ITEM_CLASS_RECIPE)
            {
                if (bot->HasSpell(GetRecipeSpell(proto)))
                    needItem = false;
                else
                    needItem = bot->CanUseItem(proto) == EQUIP_ERR_OK;
            }
        }

        if (needItem)
        {
            float stacks = CurrentStacks(ai, proto);

            if (proto->Class == ITEM_CLASS_RECIPE && stacks > 0) //Only buy one recipe.
                return ItemUsage::ITEM_USAGE_KEEP;

            if (stacks < 1)
                return ItemUsage::ITEM_USAGE_SKILL; //Buy more.
            else if (stacks == 1)
                return ItemUsage::ITEM_USAGE_KEEP; //Do not buy more.
        }
    }

    //USE
    if (proto->Class == ITEM_CLASS_KEY)
        return ItemUsage::ITEM_USAGE_USE;

    if (proto->Class == ITEM_CLASS_CONSUMABLE)
    {
        std::string foodType = "";

        if (IsHpFoodOrDrink(proto))
        {
            foodType = "food";
        }
        else if (IsManaFoodOrDrink(proto))
        {
            foodType = "drink";
        }
        else if (IsHealingPotion(proto))
        {
            foodType = "healing potion";
        }
        else if (IsManaPotion(proto))
        {
            foodType = "mana potion";
        }
        else if (IsBandage(proto))
        {
            foodType = "bandage";
        }

        bool botHasHealingSpells = bot->GetClass() == CLASS_PALADIN || bot->GetClass() == CLASS_PRIEST || bot->GetClass() == CLASS_DRUID || bot->GetClass() == CLASS_SHAMAN;

        //itemlevel 1 consumables are mostly level-independent
        bool isAppropriateConsumableLevel = proto->RequiredLevel <= bot->GetLevel()
            && (proto->ItemLevel == 1 || proto->ItemLevel >= bot->GetLevel());

        bool isAppropriateConsumable = isAppropriateConsumableLevel
            && (IsHpFoodOrDrink(proto) || IsHealingPotion(proto) || (IsBandage(proto) && !botHasHealingSpells) || (bot->GetPowerType() == POWER_MANA && (IsManaFoodOrDrink(proto) || IsManaPotion(proto))));

        if (isAppropriateConsumable && bot->CanUseItem(proto) == EQUIP_ERR_OK)
        {
            //A consumable the bot can actually use is never vendor trash - with
            //or without the item cheat. The random pool runs with
            //AiPlayerbot.RndBotCheats = repair,breath,item, and the old
            //`&& !HasCheat(BotCheatMask::item)` on this block skipped the whole
            //decision for every pool bot: their food and drink fell through to
            //the VENDOR branch, so every sell path handed them over (live cycle
            //4: 1,286 of the 4,394 sale rows were the bot's own food and drink)
            //and that same VENDOR answer is what armed the 2,300 vendor errands.
            //
            //The cheat only makes the "buy more" signal moot - the bot never has
            //to shop - it is not a licence to sell rations.
            if (ai->HasCheat(BotCheatMask::item))
                return ItemUsage::ITEM_USAGE_KEEP;

            float stacks = BetterStacks(proto, foodType);

            if (stacks < 1)
            {
                stacks += CurrentStacks(ai, proto);

                if (stacks < 1)
                    return ItemUsage::ITEM_USAGE_USE; //Buy some to get to 1 stack
            }

            return ItemUsage::ITEM_USAGE_KEEP; //Never sell what the bot eats, drinks or bandages with.
        }
    }

    if (proto->Class == ITEM_CLASS_REAGENT && SpellsUsingItem(proto->ItemId, bot).size())
    {
        //A reagent one of the bot's own spells consumes is never vendor trash,
        //however many stacks it holds.
        if (CurrentStacks(ai, proto) < 1)
            return ItemUsage::ITEM_USAGE_USE;

        return ItemUsage::ITEM_USAGE_KEEP;
    }

    //EQUIP (bot-aware speed: dynamic mounts such as the 0-static-speed
    // turtle 30174 score at the rider's real speed).
    if (MountValue::GetMountSpell(itemId) && bot->CanUseItem(proto) == EQUIP_ERR_OK)
    {
        uint32 newSpell = MountValue::GetMountSpell(itemId);
        uint32 riderSpeed = MountValue::GetRiderMountSpeed(bot);
        uint32 newSpeed = MountValue(newSpell).GetSpeedFor(bot, riderSpeed);
        if (newSpeed)
        {
            std::vector<MountValue> mounts = AI_VALUE(std::vector<MountValue>, "mount list");

            if (mounts.empty())
                return ItemUsage::ITEM_USAGE_EQUIP;

            bool hasBetterMount = false, hasSameMount = false;

            for (auto& mount : mounts)
            {
                uint32 currentSpeed = mount.GetSpeedFor(bot, riderSpeed);
                if (currentSpeed > newSpeed)
                    hasBetterMount = true;
                else if (currentSpeed == newSpeed)
                    hasSameMount = true;

                if (hasBetterMount)
                    break;
            }

            if (!hasBetterMount)
                return hasSameMount ? ItemUsage::ITEM_USAGE_KEEP : ItemUsage::ITEM_USAGE_EQUIP;
        }
    }

    // Profession tools (Mining Pick, Blacksmith Hammer, Skinning Knife, Arclight
    // Spanner, Woodcutting Axe) and a few quest items are weapon-class "misc"
    // weapons: no weapon skill is needed to wield them, so a bot with an empty
    // hand scored one as a fill-the-slot weapon and announced "Equipping ...".
    // The copy a bot needs was already kept above and quest items are handled
    // below; none of them is gear.
    const bool miscWeapon = proto->Class == ITEM_CLASS_WEAPON && proto->SubClass == ITEM_SUBCLASS_WEAPON_MISC;
    ItemUsage equip = miscWeapon ? ItemUsage::ITEM_USAGE_NONE : QueryItemUsageForEquip(itemQualifier, bot);
    if (equip != ItemUsage::ITEM_USAGE_NONE)
        return equip;


    if (forceUsage == ForceItemUsage::FORCE_USAGE_EQUIP)
        return ItemUsage::ITEM_USAGE_KEEP;

    //DISENCHANT
    if ((proto->Class == ITEM_CLASS_ARMOR || proto->Class == ITEM_CLASS_WEAPON) && proto->Bonding != BIND_WHEN_PICKED_UP &&
        ai->HasSkill(SKILL_ENCHANTING) && proto->Quality >= ITEM_QUALITY_UNCOMMON)
    {
        if (proto->DisenchantID)
        {

                Item* item = CurrentItem(proto, bot);

                //Bot has budget to replace the item it wants to disenchant.
                if (!item || !sRandomBotFacade.IsRandomBot(bot) || AI_VALUE2(uint32, "free money for", (uint32)NeedMoneyFor::tradeskill) > proto->BuyPrice)
                    return ItemUsage::ITEM_USAGE_DISENCHANT;

        }
    }

    //QUEST
    if (!ai->GetMaster() || !sPlayerbotAIConfig.syncQuestWithPlayer || !IsNeededForQuest(ai->GetMaster(), itemId))
    {
        if (IsNeededForQuest(bot, itemId))
            return ItemUsage::ITEM_USAGE_QUEST;
        else if (IsNeededForQuest(bot, itemId, true) && CurrentStacks(ai, proto) < 2) //Do not sell quest items unless selling a full stack will stil keep enough in inventory.
            return ItemUsage::ITEM_USAGE_KEEP;
    }

    // Bone Chew Toy (item 51751): the only quest needing it is the inactive
    // 40298, so it is junk, not something to keep. Keyed off the item id, so
    // quest starters (Free Ticket Voucher 19338 etc.) keep their KEEP verdict
    // below and keep looting normally.
    if (itemId == ai::kBoneChewToyItemId)
        return ItemUsage::ITEM_USAGE_NONE;

    //A quest item the bot carries is never vendor trash, whether or not it holds
    //the quest that needs it right now.
    if (proto->Class == ItemClass::ITEM_CLASS_QUEST)
        return ItemUsage::ITEM_USAGE_KEEP;

    // AMMO
if ((proto->Class == ITEM_CLASS_PROJECTILE ||
     (proto->Class == ITEM_CLASS_WEAPON && proto->SubClass == ITEM_SUBCLASS_WEAPON_THROWN)) &&
    bot->CanUseItem(proto) == EQUIP_ERR_OK)
{
    if ((bot->GetClass() == CLASS_HUNTER && proto->Class != ITEM_CLASS_WEAPON) ||
        bot->GetClass() == CLASS_ROGUE ||
        bot->GetClass() == CLASS_WARRIOR)
    {
        Item* const pItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);
        if (pItem)
        {
            uint32 ammoClass = ITEM_CLASS_PROJECTILE;
            uint32 subClass = 0;

            switch (pItem->GetProto()->SubClass)
            {
                case ITEM_SUBCLASS_WEAPON_GUN:
                    subClass = ITEM_SUBCLASS_BULLET;
                    break;
                case ITEM_SUBCLASS_WEAPON_BOW:
                case ITEM_SUBCLASS_WEAPON_CROSSBOW:
                    subClass = ITEM_SUBCLASS_ARROW;
                    break;
                case ITEM_SUBCLASS_WEAPON_THROWN:
                    ammoClass = ITEM_CLASS_WEAPON;
                    subClass = ITEM_SUBCLASS_WEAPON_THROWN;
                    break;
            }

            if (proto->Class == ammoClass && proto->SubClass == subClass)
            {
                uint32 currentAmmoId = bot->GetUInt32Value(PLAYER_AMMO_ID);
                // A thrown weapon is its own ammo and never sets PLAYER_AMMO_ID, so "no ammo
                // equipped" was always true for it: a second stack of the same axes in the bag
                // came back as "equip", equipping it swapped the two stacks and the next tick
                // swapped them back (28,000 swaps in half an hour for one rogue). The stack in
                // the ranged slot counts as the equipped ammo.
                if (pItem->GetProto()->SubClass == ITEM_SUBCLASS_WEAPON_THROWN)
                    currentAmmoId = pItem->GetEntry();
                const ItemPrototype* currentAmmoProto = nullptr;
                if (currentAmmoId)
                    currentAmmoProto = sObjectMgr.GetItemPrototype(currentAmmoId);

                // Pool item cheat: the per-tick refill tops the equipped stack
                // back up, so firing never consumes anything and vendor ammo
                // is never a restock - it only burns the trainer purse (live
                // pool: same-arrow batches up to 10 per visit). The equip
                // checks below still run (empty slot / better ammo classify
                // EQUIP); only the restock demand is gated, via needAmmo = 0
                // so the AMMO return below can never fire.
                float betterAmmoStacks = BetterStacks(proto, "ammo"); // how much better ammo we have
                float needAmmo = (bot->GetClass() == CLASS_HUNTER) ? 8 : 2;
                if (ai::SuppressAmmoBuy(ai->HasCheat(BotCheatMask::item)))
                    needAmmo = 0;

                                    // fallback: equip any ammo if no ammo equipped
                if (!currentAmmoId)
                {
                                    // check if this proto exists in bags
                if (ai->HasItemInInventory(proto->ItemId))
                return ItemUsage::ITEM_USAGE_EQUIP;
                }

                // If no better ammo exists
                if (betterAmmoStacks <= 0)
                {
                    // Equip this ammo if not already equipped
                    if (currentAmmoId != proto->ItemId)
                        return ItemUsage::ITEM_USAGE_EQUIP;
                }

                // If this ammo is already equipped or after equipping
                float totalStacks = betterAmmoStacks + CurrentStacks(ai, proto);

                if (totalStacks < needAmmo)            // Not enough ammo, buy more
                    return ItemUsage::ITEM_USAGE_AMMO;

                return ItemUsage::ITEM_USAGE_KEEP;     //Ammo for the equipped ranged weapon is never vendor trash.
            }
        }
    }
}

    //KEEP
    if (proto->Quality >= ITEM_QUALITY_EPIC && sPlayerbotAIConfig.botsSaveEpics && !sRandomBotFacade.IsRandomBot(bot))
        return ItemUsage::ITEM_USAGE_KEEP;

    uint32 ahPrice = 0;

    //VENDOR/AH
    if (proto->SellPrice > 0 || AI_VALUE2_EXISTS(int, "manual int", "expected ah sell price for " + std::to_string(itemId),-1) != 0)
    {
        ItemUsage sellUsage = ItemUsage::ITEM_USAGE_VENDOR;

        if (!ai->HasActivePlayerMaster())
        {
            uint32 maxSellPrice = proto->SellPrice;

            if (proto->Stackable)
                maxSellPrice *= proto->Stackable;

            uint32 minimumSellPrice = bot->GetMoney() / 1000;

            if (maxSellPrice < minimumSellPrice) //Do not loot items less than 0.1% of bot's gold per stack.
                sellUsage = ItemUsage::ITEM_USAGE_NONE;
        }


        //if item value is significantly higher than its vendor sell price and we actually have money to place the item on ah.
        uint32 ahMoney = AI_VALUE2(uint32, "free money for", (uint32)NeedMoneyFor::ah);

        if(!ahMoney && AI_VALUE(uint8, "bag space") > 80)
            return sellUsage;

        if (!IsMoreProfitableToSellToAHThanToVendor(proto, bot))
            return sellUsage;

        Item* item = CurrentItem(proto, bot);
        uint32 count = item ? item->GetCount() : 1;

        uint32 sellPrice = proto->SellPrice * count;

        uint32 depositCost = GetAhDepositCost(proto, count);

        uint32 ahPrice = ItemUsageValue::GetBotSellPrice(proto, bot) * count;

        if(proto->SellPrice == 0)
            SET_AI_VALUE2(int, "manual int", "expected ah sell price for " + std::to_string(itemId), ahPrice);

        if (ahPrice < depositCost)
            return sellUsage; //The AH desposit is higher than the money gained.

        if (ahPrice - depositCost < sellPrice)
            return sellUsage; //It costs more to AH then sell.

        if (ahPrice - depositCost - sellPrice < bot->GetMoney() / 500)
            return sellUsage; //Do not move to AH for items with less than 0.2% of bots gold markup.

        if(depositCost > ahMoney && AI_VALUE(uint8, "bag space") > 80)
            return sellUsage; //We simply do not have the money to put this on AH.

        if(!item)
            return ItemUsage::ITEM_USAGE_AH;   //We can't determine if this item is soulboud (yet) or broken so we assume we can AH this.

        bool soulBound = (proto->Bonding == BIND_WHEN_EQUIPPED) && item->IsSoulBound();

        if (soulBound)
            return sellUsage; //Item is soulbound so can't AH.

        uint32 repairCost = RepairCostValue::RepairCost(item);

        if (ahPrice < proto->SellPrice + repairCost)
            return sellUsage;  //Repairing costs more than the AH profit.

        if (repairCost > 0)
            return ItemUsage::ITEM_USAGE_BROKEN_AH; //Keep until repaired so we can AH later.

        return ItemUsage::ITEM_USAGE_AH;
    }

    //NONE
    return ItemUsage::ITEM_USAGE_NONE;
}

std::vector<uint8> ItemUsageValue::GetEquipSlotCandidates(Player* bot, Item* item, ItemPrototype const* proto)
{
    std::vector<uint8> slotOrder;
    switch (proto->InventoryType)
    {
        case INVTYPE_FINGER:
            slotOrder = { EQUIPMENT_SLOT_FINGER1, EQUIPMENT_SLOT_FINGER2 };
            break;
        case INVTYPE_TRINKET:
            slotOrder = { EQUIPMENT_SLOT_TRINKET1, EQUIPMENT_SLOT_TRINKET2 };
            break;
        case INVTYPE_WEAPON:
            slotOrder = { EQUIPMENT_SLOT_MAINHAND };
            if (bot->CanDualWield())
                slotOrder.push_back(EQUIPMENT_SLOT_OFFHAND);
            break;
        default:
            slotOrder = { NULL_SLOT }; // core resolves the single eligible slot
            break;
    }

    std::vector<uint8> candidates;
    for (uint8 slot : slotOrder)
    {
        uint16 dest;
        // not_loading must stay at the core default (true): passing false makes
        // core's 2H branch skip the off-hand unequip/store checks and return
        // EQUIP_ERR_ITEMS_CANT_BE_SWAPPED whenever any off-hand item is held,
        // and it also skips the alive/level/honor-rank checks this probe wants.
        InventoryResult result = item
            ? bot->CanEquipItem(slot, dest, item, true)
            : RandomBotFacade::CanEquipUnseenItem(bot, slot, dest, proto->ItemId);

        if (result != EQUIP_ERR_OK)
            continue;

        uint8 resolved = dest & 0xFF;
        if (std::find(candidates.begin(), candidates.end(), resolved) == candidates.end())
            candidates.push_back(resolved);
    }

    return candidates;
}

uint8 ItemUsageValue::GetPreferredEquipSlot(Player* bot, Item* item, ItemPrototype const* proto)
{
    std::vector<uint8> candidates = GetEquipSlotCandidates(bot, item, proto);
    if (candidates.empty())
        return NULL_SLOT;

    uint32 specId = sRandomItemMgr.GetPlayerSpecId(bot);
    if (!specId)
        specId = sRandomItemMgr.GetFallbackSpecId(bot->GetClass());

    bool const canDualWield = bot->CanDualWield();

    // A spec-allowed weapon should take over a hand that still holds a weapon
    // the spec forbids (the spec transition), and that hand can be the off one.
    // Slot-aware on purpose: the plain gate is any-slot, so a 1H weapon reads
    // as spec-legal for protection even in the off hand - and would then
    // target the shield it must never replace. Only an item legal FOR the
    // hand it would take starts a transition.
    bool const newWeaponForSpec = proto->Class == ITEM_CLASS_WEAPON &&
        sRandomItemMgr.ShouldEquipWeaponForSpec(bot->GetClass(), specId, proto, canDualWield);

    uint8 emptySlot = NULL_SLOT;
    uint8 best = NULL_SLOT;
    uint32 bestWeight = 0;

    for (uint8 slot : candidates)
    {
        // A hand the spec forbids for this weapon is no candidate at all: a
        // dual-wield-capable protection warrior would otherwise compare a
        // main-hand upgrade against its (lighter) shield, pick the off hand,
        // and then reject the item there - never upgrading the main hand.
        if (proto->Class == ITEM_CLASS_WEAPON &&
            !sRandomItemMgr.ShouldEquipWeaponForSlot(bot->GetClass(), specId, proto, slot, canDualWield))
            continue;

        Item* equipped = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!equipped)
        {
            if (emptySlot == NULL_SLOT)
                emptySlot = slot; // fill an empty slot, unless a spec transition wins
            continue;
        }

        if (newWeaponForSpec && equipped->GetProto()->Class == ITEM_CLASS_WEAPON &&
            !sRandomItemMgr.ShouldEquipWeaponForSpec(bot->GetClass(), specId, equipped->GetProto(), canDualWield) &&
            sRandomItemMgr.ShouldEquipWeaponForSlot(bot->GetClass(), specId, proto, slot, canDualWield))
        {
            // An empty main hand is worse than an off-spec off hand: without a
            // main hand the bot cannot auto-attack or use main-hand abilities
            // at all, so filling it wins over replacing the off hand.
            if (emptySlot == EQUIPMENT_SLOT_MAINHAND)
                return emptySlot;
            return slot; // off-spec weapon here: this is the slot to replace
        }

        uint32 weight = sRandomItemMgr.ItemStatWeight(bot, equipped);
        if (best == NULL_SLOT || weight < bestWeight)
        {
            best = slot;
            bestWeight = weight;
        }
    }

    return emptySlot != NULL_SLOT ? emptySlot : best;
}

// A shield is only worth keeping for a bot that can still end up in a
// one-hander-and-shield setup: a spec that lists the shield as its off-hand
// (protection warrior/paladin, holy paladin) or the tank role the roster gave
// the bot. Two-hander specs (arms, fury, retribution) never get back to it, so
// for them a shield stays ordinary loot.
static bool BotCanReturnToShield(Player* bot, uint32 specId, ItemPrototype const* proto)
{
    if (sRandomItemMgr.ShouldEquipWeaponForSpec(bot->GetClass(), specId, proto, bot->CanDualWield()))
        return true;

    return (AiFactory::GetPlayerRoles(bot) & BOT_ROLE_TANK) != 0;
}

// True when nothing else the bot carries is a better shield: the off-hand and
// every bag are scanned, and ties are broken by quality and then by item guid so
// exactly one of two otherwise identical shields wins.
static bool IsBestOwnedShield(Player* bot, Item* myself, ItemPrototype const* proto)
{
    bool better = false;
    auto consider = [&](Item* item)
    {
        if (!item || item == myself)
            return;

        ItemPrototype const* other = item->GetProto();
        if (!other || other->Class != ITEM_CLASS_ARMOR || other->SubClass != ITEM_SUBCLASS_ARMOR_SHIELD)
            return;

        // A shield the bot cannot wear must not outrank one it can keep.
        if (bot->CanUseItem(item) != EQUIP_ERR_OK)
            return;

        if (other->ItemLevel > proto->ItemLevel ||
            (other->ItemLevel == proto->ItemLevel &&
             (other->Quality > proto->Quality ||
              (other->Quality == proto->Quality && item->GetGUIDLow() > (myself ? myself->GetGUIDLow() : 0)))))
            better = true;
    };

    consider(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND));
    for (uint8 slotPos = INVENTORY_SLOT_ITEM_START; slotPos < INVENTORY_SLOT_ITEM_END; ++slotPos)
        consider(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slotPos));
    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
    {
        Bag const* pBag = (Bag const*)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag);
        uint32 size = pBag ? pBag->GetBagSize() : 0;
        for (uint32 slotPos = 0; slotPos < size; ++slotPos)
            consider(bot->GetItemByPos(bag, static_cast<uint8>(slotPos)));
    }

    return !better;
}

ItemUsage ItemUsageValue::QueryItemUsageForEquip(ItemQualifier& itemQualifier, Player* bot)
{
    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    AiObjectContext* context = ai->GetAiObjectContext();
    ChatHelper* chat = ai->GetChatHelper();
    ItemPrototype const* itemProto = itemQualifier.GetProto();

    if (bot->CanUseItem(itemProto) != EQUIP_ERR_OK)
        return ItemUsage::ITEM_USAGE_NONE;

    if (itemProto->InventoryType == INVTYPE_NON_EQUIP)
        return ItemUsage::ITEM_USAGE_NONE;

    // Pick the slot to compare against. Core only reports the primary slot
    // once a ring/trinket pair or a dual-wield hand is full, which deadlocks
    // the secondary slot; the helper falls back to the weaker equipped item.
    std::list<Item*> items = AI_VALUE2(std::list<Item*>, "inventory items", chat->formatItem(itemQualifier));
    Item* bagItem = items.empty() ? nullptr : items.front();

    // Quiver/pouch upgrades are decided by the hunter's own ilvl/quality
    // comparison below, not by free-slot accounting: a full row of plain
    // bags must not collapse the usage to NONE first (the equip path then
    // picks the quiver slot or a free slot, or fails explicitly).
    bool const isQuiverUpgradeCandidate = (itemProto->Class == ITEM_CLASS_QUIVER &&
        bot->GetClass() == CLASS_HUNTER &&
        bot->GetWeaponForAttack(WeaponAttackType::RANGED_ATTACK, false, false) != nullptr);

    uint32 specId = sRandomItemMgr.GetPlayerSpecId(bot);
    if (!specId)
        specId = sRandomItemMgr.GetFallbackSpecId(bot->GetClass());

    bool const canDualWield = bot->CanDualWield();

    uint8 slot = ItemUsageValue::GetPreferredEquipSlot(bot, bagItem, itemProto);
    if (slot == NULL_SLOT && !isQuiverUpgradeCandidate)
    {
        // A two-hander in the main hand makes core resolve no off-hand slot, so
        // every shield the bot could wear reads as useless here and the next
        // vendor visit sells it (Turtle keeps buyback rows in the DB, which is
        // where the pool's "shield still in the bag" actually sat). Keep it
        // instead: equipping stays a no-op while the two-hander is worn, and the
        // next audit puts the shield on as soon as a one-hander takes the main
        // hand. Only a bot that can return to a one-hander and shield keeps one,
        // and only the best shield it owns - the rest stay ordinary loot, so the
        // bags of an arms/fury/retribution bot do not fill up with spares.
        if (itemProto->Class == ITEM_CLASS_ARMOR &&
            itemProto->SubClass == ITEM_SUBCLASS_ARMOR_SHIELD &&
            bot->IsTwoHandUsed() &&
            BotCanReturnToShield(bot, specId, itemProto) &&
            IsBestOwnedShield(bot, bagItem, itemProto))
            return ItemUsage::ITEM_USAGE_EQUIP;

        return ItemUsage::ITEM_USAGE_NONE;
    }

    uint16 dest = ((INVENTORY_SLOT_BAG_0 << 8) | slot);

    // The quiver branch below already returns NONE for non-hunters (no separate
    // gate needed): the observed priest/mage vendor quiver batches came through
    // the AH-flip path, which BuyAction gates for item-cheat bots.

    if (itemProto->Class == ITEM_CLASS_QUIVER)
    {
        Item* equippedRangedWeapon = bot->GetWeaponForAttack(WeaponAttackType::RANGED_ATTACK, false, false);

        if (bot->GetClass() == CLASS_HUNTER && equippedRangedWeapon)
        {
            ItemPrototype const* rangedWeaponItemProto = equippedRangedWeapon->GetProto();
            if (!rangedWeaponItemProto)
                return ItemUsage::ITEM_USAGE_NONE;

            bool isCorrectQuiverTypeForCurrentWeapon = false;

            if (itemProto->SubClass == ITEM_SUBCLASS_AMMO_POUCH)
            {
                if (rangedWeaponItemProto->SubClass == ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_GUN)
                {
                    isCorrectQuiverTypeForCurrentWeapon = true;
                }
            }
            else {
                if (rangedWeaponItemProto->SubClass == ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_BOW
                    || rangedWeaponItemProto->SubClass == ItemSubclassWeapon::ITEM_SUBCLASS_WEAPON_CROSSBOW)
                {
                    isCorrectQuiverTypeForCurrentWeapon = true;
                }
            }

            if (!isCorrectQuiverTypeForCurrentWeapon)
                return ItemUsage::ITEM_USAGE_NONE;

            std::vector<Bag*> equippedQuivers = PlayerbotAIStorage::Instance().GetAI(bot)->GetEquippedQuivers();

            if (equippedQuivers.empty())
            {
                // Hunter with no quiver yet: any quiver matching the
                // ranged weapon is an upgrade (free slot or quiver slot
                // picked by GetSmallestBagSlot; core vetoes a second one).
                return ItemUsage::ITEM_USAGE_EQUIP;
            }
            for (auto quiver : equippedQuivers)
            {
                if (quiver->GetProto()->ItemLevel < itemProto->ItemLevel)
                {
                    return ItemUsage::ITEM_USAGE_EQUIP;
                }

                if (quiver->GetProto()->ItemLevel == itemProto->ItemLevel && quiver->GetProto()->Quality < itemProto->Quality)
                {
                    return ItemUsage::ITEM_USAGE_EQUIP;
                }

                //no need to check for quiver container slots size, higher ilvl/quality checks is enough
            }
        }

        return ItemUsage::ITEM_USAGE_NONE;
    }

    if (itemProto->Class == ITEM_CLASS_CONTAINER)
    {
        // Soul bags (and other profession containers) are only useful to
        // the class that fills them: a warlock treats a soul bag as an
        // equip upgrade when it holds more shards than the smallest
        // equipped soul bag. First soul bag equips even when its slots <
        // plain bag size (shards would otherwise sit in plain slots).
        // No slot reserved blindly: GetSmallestBagSlot only replaces a
        // soul bag or takes an empty slot, never a plain bag.
        if (itemProto->SubClass != ITEM_SUBCLASS_CONTAINER)
        {
            if (itemProto->SubClass == ITEM_SUBCLASS_SOUL_CONTAINER && bot->GetClass() == CLASS_WARLOCK)
            {
                uint32 smallestSoul = 0;
                bool haveSoulBag = false;
                for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
                {
                    Item* bagItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag);
                    if (bagItem && bagItem->GetProto() && bagItem->GetProto()->Class == ITEM_CLASS_CONTAINER &&
                        bagItem->GetProto()->SubClass == ITEM_SUBCLASS_SOUL_CONTAINER)
                    {
                        haveSoulBag = true;
                        uint32 slots = ((Bag*)bagItem)->GetBagSize();
                        if (!smallestSoul || slots < smallestSoul)
                            smallestSoul = slots;
                    }
                }
                if (haveSoulBag && smallestSoul >= itemProto->ContainerSlots)
                    return ItemUsage::ITEM_USAGE_NONE;
                return ItemUsage::ITEM_USAGE_EQUIP;
            }
            return ItemUsage::ITEM_USAGE_NONE;
        }

        if (GetSmallestBagSize(bot) >= itemProto->ContainerSlots)
            return ItemUsage::ITEM_USAGE_NONE;

        return ItemUsage::ITEM_USAGE_EQUIP;
    }

    bool shouldEquip = false;
    bool armorForSpec = true;

    uint32 statWeight = sRandomItemMgr.ItemStatWeight(bot, itemQualifier);
    if (statWeight)
        shouldEquip = true;

    // Slot-aware on purpose: the plain gate answers "legal in ANY slot", so a
    // 1H weapon reads as spec-legal for protection (main-hand set) even when
    // compared against the shield. The OH slot of a shield spec admits
    // shields only - a weapon there is never an upgrade, which is the
    // reverse guard for the shield-transition below (no ping-pong).
    if (itemProto->Class == ITEM_CLASS_WEAPON && !sRandomItemMgr.ShouldEquipWeaponForSlot(bot->GetClass(), specId, itemProto, slot, canDualWield))
        shouldEquip = false;
    if (itemProto->Class == ITEM_CLASS_ARMOR)
    {
        armorForSpec = sRandomItemMgr.ShouldEquipArmorForSpec(bot->GetClass(), specId, itemProto);
        if (!armorForSpec)
            shouldEquip = false;
    }

    Item* oldItem = bot->GetItemByPos(dest);

    ai->TellDebug(ai->GetMaster(), "Checking equip: " + chat->formatItem(itemProto) + " to " + chat->formatSlot(slot) + " vs " + (oldItem ? chat->formatItem(oldItem->GetProto()) : "empty"), "debug equip");

    if (itemProto->Class == ITEM_CLASS_WEAPON &&
        !sRandomItemMgr.ShouldEquipWeaponForSlot(bot->GetClass(), specId, itemProto, slot, canDualWield))
    {
        if (oldItem)
            return ItemUsage::ITEM_USAGE_NONE;
    }

    // An item with no stats, no armour, no weapon damage and no spell does
    // nothing for the bot. Without this it falls through to BAD_EQUIP below,
    // which bots without a real player master happily put on - that is how 74
    // of them ended up wearing a Forever-Lovely Rose on their head and another
    // 34 a rabbit headband or a carnival mask. Shirt and tabard are cosmetic
    // slots and stay exempt.
    if (!statWeight && slot != EQUIPMENT_SLOT_BODY && slot != EQUIPMENT_SLOT_TABARD)
    {
        bool const contributes = itemProto->Armor > 0
            || itemProto->Block > 0
            || itemProto->Damage[0].DamageMax > 0.0f
            || itemProto->Spells[0].SpellId > 0
            // A random-property item carries its stats in the random suffix,
            // not the base prototype: a Beaded Orb (15969, +228) scores 0 from
            // the base stats alone, so into an empty slot it still counts as
            // contributing rather than being rejected as a stat-less item.
            || (!oldItem && itemQualifier.GetRandomPropertyId() != 0);

        if (!contributes)
            return ItemUsage::ITEM_USAGE_NONE;
    }

    // Caster off-hand on a melee class with an empty off-hand slot: the
    // armor-class fallthrough below would EQUIP it unconditionally. Reject
    // first so warriors/rogues/hunters never pick it up (Issue #219).
    if (!oldItem && slot == EQUIPMENT_SLOT_OFFHAND && itemProto->InventoryType == INVTYPE_HOLDABLE &&
        (bot->GetClass() == CLASS_WARRIOR || bot->GetClass() == CLASS_ROGUE || bot->GetClass() == CLASS_HUNTER))
        return ItemUsage::ITEM_USAGE_NONE;

    //No item equiped
    if (!oldItem)
    {
        if (shouldEquip || itemProto->Class == ITEM_CLASS_ARMOR)
            return ItemUsage::ITEM_USAGE_EQUIP;
        else
            return ItemUsage::ITEM_USAGE_BAD_EQUIP;
    }

    const ItemPrototype* oldItemProto = oldItem->GetProto();

    if (itemProto->Class == ITEM_CLASS_ARMOR && itemProto->InventoryType == INVTYPE_TABARD)
    {
        uint32 currentStacks = CurrentStacks(ai, itemProto);

        if (currentStacks > 0)
        {
            if (itemProto->ItemId != oldItemProto->ItemId && urand(1, 100) <= 10) //Not equiped. Random 10% equip it.
                return ItemUsage::ITEM_USAGE_EQUIP;

            return ItemUsage::ITEM_USAGE_KEEP;
        }

        return ItemUsage::ITEM_USAGE_EQUIP; //Do not have it yet. Buy/get it.
    }

    if (AI_VALUE2_EXISTS(ForceItemUsage, "force item usage", oldItemProto->ItemId, ForceItemUsage::FORCE_USAGE_NONE) == ForceItemUsage::FORCE_USAGE_EQUIP) //Current equip is forced. Do not unequip.
    {
        if (AI_VALUE2_EXISTS(ForceItemUsage, "force item usage", itemProto->ItemId, ForceItemUsage::FORCE_USAGE_NONE) == ForceItemUsage::FORCE_USAGE_EQUIP)
            return ItemUsage::ITEM_USAGE_KEEP;
        else
            return ItemUsage::ITEM_USAGE_NONE;
    }

    uint32 oldStatWeight = sRandomItemMgr.ItemStatWeight(bot, oldItem);

    // When the weighted scores tie - the common case for low-level gear, since
    // armour-only pieces score 0 for nearly every spec - the sheet value breaks
    // the tie before quality and item level do. A real stat item still outranks
    // a bigger stat-less one, because only an exact tie reaches it.
    float const sheetValue = ItemSheetValue(itemProto);
    float const oldSheetValue = ItemSheetValue(oldItemProto);
    bool const weightsTied = statWeight == oldStatWeight;

    if (!weightsTied)
    {
        shouldEquip = statWeight >= oldStatWeight;
    }
    else if (sheetValue != oldSheetValue)
    {
        shouldEquip = sheetValue > oldSheetValue;
    }
    else
    {
        shouldEquip = itemProto->Quality >= oldItemProto->Quality && itemProto->ItemLevel > oldItemProto->ItemLevel;
    }

    if (AI_VALUE2_EXISTS(ForceItemUsage, "force item usage", itemProto->ItemId, ForceItemUsage::FORCE_USAGE_NONE) == ForceItemUsage::FORCE_USAGE_EQUIP) //New item is forced. Always equip it.
        return ItemUsage::ITEM_USAGE_EQUIP;

    // Wrong armour class for the spec still has to win the compare below, but it
    // must get the chance to. The spec armour sets describe the endgame class;
    // below the level a class can wear its best armour the bot is dressed in the
    // creation kit (a protection warrior starts in cloth), and demanding both a
    // higher subclass and a strictly higher stat weight rejected exactly the
    // upgrades that fit those slots. Legality stays with CanUseItem above - this
    // is only a preference.
    //
    // A stat advantage alone must not buy the swap: the ARMOR case below accepts
    // a higher stat weight on its own, so without the sheet floor a plate wearer
    // would trade hundreds of armour for a better-statted cloth piece. A wrong
    // armour class is therefore only ever allowed while it gives up no armour.
    if (itemProto->Class == ITEM_CLASS_ARMOR && !armorForSpec)
    {
        if (oldItemProto->Class != ITEM_CLASS_ARMOR || sheetValue < oldSheetValue)
            return ItemUsage::ITEM_USAGE_NONE;

        shouldEquip = true;
    }

    // Spec transition: a bot that still wields a weapon its spec forbids
    // (e.g. an assassination rogue holding the mace it used while the
    // pre-talent default scale applied, or a protection warrior holding a
    // 1H weapon in the off hand) swaps to a spec-allowed weapon as soon
    // as one is available, even at somewhat lower DPS. The gates are
    // slot-aware: a 1H weapon is spec-legal in the main hand but forbidden
    // in a shield spec's off hand. Only the weight race is skipped; class
    // rules (CanUseItem above) still apply to the new item.
    bool const newWeaponForSpec = (itemProto->Class == ITEM_CLASS_WEAPON &&
        sRandomItemMgr.ShouldEquipWeaponForSlot(bot->GetClass(), specId, itemProto, slot, canDualWield));
    bool const oldWeaponAgainstSpec = (oldItemProto->Class == ITEM_CLASS_WEAPON &&
        !sRandomItemMgr.ShouldEquipWeaponForSlot(bot->GetClass(), specId, oldItemProto, slot, canDualWield));

    // The two-hander a fury warrior leveled on before it could dual wield is not
    // an off-spec mistake: learning Dual Wield must not downgrade it to the
    // first one-hander in the bags. Only the weight race below may replace it.
    // A protection warrior/paladin never levels on a two-hander on purpose -
    // its spec off hand is a shield, which a two-hander blocks - so for such a
    // spec the old two-hander is an off-spec mistake like any other. The extra
    // conjunct reads the spec set without Dual Wield: fury still allows the
    // two-hander there (it leveled on it), protection never does.
    bool const standInTwoHander = canDualWield &&
        oldItemProto->InventoryType == INVTYPE_2HWEAPON && itemProto->InventoryType != INVTYPE_2HWEAPON &&
        sRandomItemMgr.ShouldEquipWeaponForSlot(bot->GetClass(), specId, oldItemProto, slot, false);

    if (newWeaponForSpec && oldWeaponAgainstSpec && !standInTwoHander)
        return ItemUsage::ITEM_USAGE_EQUIP;

    // Shield transition: a bot that can return to a shield setup (protection or
    // holy spec, or the tank role - see BotCanReturnToShield) whose off hand
    // still holds a weapon takes the shield even at lower weight. No spec lets
    // a shield setup pair the shield with an off-hand weapon (protection wants
    // shield only, holy shield or held-in-hand), so any weapon there is wrong
    // and no weight race is needed; without this the shield falls into the
    // wrong-armour-class branch below and reads as vendor trash against any
    // off-hand weapon. Slot-aware on purpose: a generic one-hander is a legal
    // main hand, so the spec gate alone cannot spot it in the wrong hand.
    bool const shieldForSpec = (slot == EQUIPMENT_SLOT_OFFHAND &&
        itemProto->Class == ITEM_CLASS_ARMOR &&
        itemProto->SubClass == ITEM_SUBCLASS_ARMOR_SHIELD &&
        BotCanReturnToShield(bot, specId, itemProto));
    bool const oldOffSpecWeapon = (oldItemProto->Class == ITEM_CLASS_WEAPON);
    if (shieldForSpec && oldOffSpecWeapon)
        return ItemUsage::ITEM_USAGE_EQUIP;

    bool existingShouldEquip = true;
    if (oldItemProto->Class == ITEM_CLASS_WEAPON && !oldStatWeight)
        existingShouldEquip = false;
    if (oldItemProto->Class == ITEM_CLASS_ARMOR && !oldStatWeight)
        existingShouldEquip = false;

    //Compare items based on item level, quality.
    bool isBetter = false;
    // Upgrade needs a clear score win (AG-1): epsilon gains churn swaps
    // across audits for nothing. Exact ties fall through to the tiebreaks
    // below; spec-transition, broken-gear and forced swaps return above.
    if (ai::EquipUpgradeBetter(statWeight, oldStatWeight, sPlayerbotAIConfig.equipUpgradeThreshold))
        isBetter = true;
    else if (weightsTied && sheetValue != oldSheetValue)
        isBetter = sheetValue > oldSheetValue;
    else if (weightsTied && itemProto->Quality > oldItemProto->Quality)
        isBetter = true;
    else if (weightsTied && itemProto->Quality == oldItemProto->Quality && itemProto->ItemLevel > oldItemProto->ItemLevel)
        isBetter = true;

    Item* item = CurrentItem(itemProto, bot);
    bool itemIsBroken = item && item->GetUInt32Value(ITEM_FIELD_DURABILITY) == 0 && item->GetUInt32Value(ITEM_FIELD_MAXDURABILITY) > 0;
    bool oldItemIsBroken = oldItem->GetUInt32Value(ITEM_FIELD_DURABILITY) == 0 && oldItem->GetUInt32Value(ITEM_FIELD_MAXDURABILITY) > 0;
    if (itemProto->ItemId != oldItemProto->ItemId && (shouldEquip || !existingShouldEquip) && isBetter)
    {
        switch (itemProto->Class)
        {
        case ITEM_CLASS_ARMOR:
            if (oldItemProto->SubClass <= itemProto->SubClass || statWeight > oldStatWeight) {
                if (itemIsBroken && !oldItemIsBroken)
                    return ItemUsage::ITEM_USAGE_BROKEN_EQUIP;
                else
                    if (shouldEquip)
                        return ItemUsage::ITEM_USAGE_EQUIP;
                    else
                        return ItemUsage::ITEM_USAGE_BAD_EQUIP;
            }
            break;
        default:
            if (itemIsBroken && !oldItemIsBroken)
                return ItemUsage::ITEM_USAGE_BROKEN_EQUIP;
            else
                if (shouldEquip)
                    return ItemUsage::ITEM_USAGE_EQUIP;
                else
                    return ItemUsage::ITEM_USAGE_BAD_EQUIP;
        }
    }
    //Item is not better but current item is broken and new one is not.
    if (oldItemIsBroken && !itemIsBroken)
        return ItemUsage::ITEM_USAGE_EQUIP;

    return ItemUsage::ITEM_USAGE_NONE;
}

// Return the smallest plain-container bag equipped, or 0 when a bag slot is
// free. Quivers/ammo pouches, soul bags and profession bags hold their own
// item types, so counting their slots would make every plain bag look like an
// upgrade and trade bags back and forth on every audit.
uint32 ItemUsageValue::GetSmallestBagSize(Player* bot)
{
    int8 curSlot = 0;
    uint32 curSlots = 0;
    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
    {
        Item* bagItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag);
        const Bag* const pBag = (Bag*)bagItem;
        if (pBag)
        {
            ItemPrototype const* proto = bagItem->GetProto();
            if (!proto || proto->Class != ITEM_CLASS_CONTAINER || proto->SubClass != ITEM_SUBCLASS_CONTAINER)
                continue;

            if (curSlot > 0 && curSlots < pBag->GetBagSize())
                continue;

            curSlot = pBag->GetSlot();
            curSlots = pBag->GetBagSize();
        }
        else
            return 0;
    }

    return curSlots;
}

std::string ItemUsageValue::ReasonForNeed(ItemUsage usage, ItemQualifier qualifier, uint32 count, Player* bot)
{
    std::map<std::string, std::string> placeholders;
    placeholders["%item"] = ChatHelper::formatItem(qualifier);

    switch (usage)
    {
    case ItemUsage::ITEM_USAGE_EQUIP:
    {
        if (!qualifier || !bot)
            return BOT_TEXT2("for equiping as upgrade.", placeholders);

        Item* currentItem = ItemUsageValue::CurrentItemInSlot(qualifier.GetProto(), bot);
        if (!currentItem)
            return BOT_TEXT2("for equiping as upgrade because the slot is empty.", placeholders);

        placeholders["%current"] = ChatHelper::formatItem(currentItem);

        if (currentItem->GetUInt32Value(ITEM_FIELD_DURABILITY) == 0 && currentItem->GetUInt32Value(ITEM_FIELD_MAXDURABILITY) > 0)
            return BOT_TEXT2("for equiping as a replacement of %current because it is broken.", placeholders);

        uint32 currentStatWeight = sRandomItemMgr.ItemStatWeight(bot, currentItem);
        uint32 newStatWeight = sRandomItemMgr.ItemStatWeight(bot, qualifier);
        placeholders["%cPower"] = std::to_string(currentStatWeight);
        placeholders["%nPower"] = std::to_string(newStatWeight);
        if (newStatWeight && currentStatWeight)
            return BOT_TEXT2("for equiping as a replacement of %current (%cPower) because it is stronger (%nPower).", placeholders);

        return BOT_TEXT2("for equiping as a replacement of %current because it has a higher level or quality.", placeholders);
    }
    case ItemUsage::ITEM_USAGE_BAD_EQUIP:
        return BOT_TEXT2("for equiping until I can find something better.", placeholders);
    case ItemUsage::ITEM_USAGE_USE:
        return BOT_TEXT2("to use it when I need it.", placeholders);
    case ItemUsage::ITEM_USAGE_SKILL:
    case ItemUsage::ITEM_USAGE_DISENCHANT:
        return BOT_TEXT2("to use it for my profession.", placeholders);
    case ItemUsage::ITEM_USAGE_AMMO:
        return BOT_TEXT2("to use as ammo.", placeholders);
    case ItemUsage::ITEM_USAGE_QUEST:
        return BOT_TEXT2("to complete an objective for a quest.", placeholders);
    case ItemUsage::ITEM_USAGE_AH:
        if (!qualifier)
            return BOT_TEXT2("to repost on AH.", placeholders);

        placeholders["%price_min"] = ChatHelper::formatMoney(ItemUsageValue::GetBotSellPrice(qualifier.GetProto(), bot) * 0.75 * count);
        placeholders["%price_max"] = ChatHelper::formatMoney(ItemUsageValue::GetBotSellPrice(qualifier.GetProto(), bot) * count);
        return BOT_TEXT2("to repost on AH for %price_min to %price_max.", placeholders);
    case ItemUsage::ITEM_USAGE_VENDOR:
        if (!qualifier)
            return BOT_TEXT2("to sell to a vendor.", placeholders);

        placeholders["%price"] = ChatHelper::formatMoney(qualifier.GetProto()->SellPrice * count);
        return BOT_TEXT2("to sell to a vendor for %price.", placeholders);
    case ItemUsage::ITEM_USAGE_FORCE_NEED:
    case ItemUsage::ITEM_USAGE_FORCE_GREED:
        return BOT_TEXT2("because I was told to get this item.", placeholders);
    }

    return "";
}

uint32 ItemUsageValue::GetAhDepositCost(ItemPrototype const* proto, uint32 count)
{
    uint32 time;
    time = 8 * HOUR;

    float deposit = float(proto->SellPrice * count * (time / MIN_AUCTION_TIME));

    deposit = deposit * 15 * 3.0f / 100.0f;

    float min_deposit = float(sWorld.getConfig(CONFIG_UINT32_AUCTION_DEPOSIT_MIN));

    if (deposit < min_deposit)
        deposit = min_deposit;

    deposit *= sWorld.getConfig(CONFIG_FLOAT_RATE_AUCTION_DEPOSIT);

    return deposit;
}

uint32 ItemUsageValue::ItemCreatedFrom(uint32 wantItemId)
{
    // The former mapping was for a later expansion quest item pair that is
    // absent from the local Tortoise item data. Tortoise quest relationships are
    // read directly from the quest template instead.
    (void)wantItemId;
    return 0;
}

bool ItemUsageValue::IsNeededForQuest(Player* player, uint32 itemId, bool ignoreInventory)
{
    if (!itemId)
        return false;

    for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 entry = GetQuestSlotIdCompat(player, slot);
        Quest const* quest = sObjectMgr.GetQuestTemplate(entry);
        if (!quest)
            continue;

        QuestStatusData& qData = player->getQuestStatusMap()[quest->GetQuestId()];
        if (qData.m_status != QUEST_STATUS_INCOMPLETE)
            continue;

        for (int i = 0; i < 4; i++)
        {
            if (!quest->ReqItemCount[i])
                continue;

            if (quest->ReqItemId[i] != itemId && ItemCreatedFrom(quest->ReqItemId[i]) != itemId)
                continue;

            if (!ignoreInventory && player->GetItemCount(itemId, false) >= quest->ReqItemCount[i])
                continue;

            return true;
        }
    }

    return false;
}

bool ItemUsageValue::IsItemNeededForSkill(ItemPrototype const* proto)
{
    switch (proto->ItemId)
    {
    case 2901: //Mining pick
        return ai->HasSkill(SKILL_MINING);
    case 5956: //Blacksmith Hammer
        return ai->HasSkill(SKILL_BLACKSMITHING) || ai->HasSkill(SKILL_ENGINEERING);
    case 6219: //Arclight Spanner
        return ai->HasSkill(SKILL_ENGINEERING);
    case 6218: //Runed copper rod
        return ai->HasSkill(SKILL_ENCHANTING);
    case 6339: //Runed silver rod
        return ai->HasSkill(SKILL_ENCHANTING);
    case 11130: //Runed golden rod
        return ai->HasSkill(SKILL_ENCHANTING);
    case 11145: //Runed truesilver rod
        return ai->HasSkill(SKILL_ENCHANTING);
    case 16207: //Runed Arcanite Rod
        return ai->HasSkill(SKILL_ENCHANTING);
    case 7005: //Skinning Knife
        return ai->HasSkill(SKILL_SKINNING);
    case 4471: //Flint and Tinder
        return ai->HasSkill(SKILL_COOKING);
    case 4470: //Simple Wood
        return ai->HasSkill(SKILL_COOKING);
    case 6256: //Fishing Rod
        return ai->HasSkill(SKILL_FISHING);
    }

    return false;
}


bool ItemUsageValue::IsItemUsefulForSkill(ItemPrototype const* proto)
{
    switch (proto->Class)
    {
    case ITEM_CLASS_TRADE_GOODS:
    case ITEM_CLASS_MISC:
    case ITEM_CLASS_REAGENT:
    {
        if (ai->HasSkill(SKILL_TAILORING) && IsItemUsedBySkill(proto, SKILL_TAILORING))
            return true;
        if (ai->HasSkill(SKILL_LEATHERWORKING) && IsItemUsedBySkill(proto, SKILL_LEATHERWORKING))
            return true;
        if (ai->HasSkill(SKILL_ENGINEERING) && IsItemUsedBySkill(proto, SKILL_ENGINEERING))
            return true;
        if (ai->HasSkill(SKILL_BLACKSMITHING) && IsItemUsedBySkill(proto, SKILL_BLACKSMITHING))
            return true;
        if (ai->HasSkill(SKILL_ALCHEMY) && IsItemUsedBySkill(proto, SKILL_ALCHEMY))
            return true;
        if (ai->HasSkill(SKILL_ENCHANTING) && IsItemUsedBySkill(proto, SKILL_ENCHANTING))
            return true;
        if (ai->HasSkill(SKILL_FISHING) && IsItemUsedBySkill(proto, SKILL_FISHING))
            return true;
        if (ai->HasSkill(SKILL_FIRST_AID) && IsItemUsedBySkill(proto, SKILL_FIRST_AID))
            return true;
        if (ai->HasSkill(SKILL_COOKING) && IsItemUsedBySkill(proto, SKILL_COOKING))
            return true;
        if (ai->HasSkill(SKILL_MINING) &&
            (
                IsItemUsedBySkill(proto, SKILL_MINING)// ||
                //IsItemUsedBySkill(proto, SKILL_BLACKSMITHING) ||
                //IsItemUsedBySkill(proto, SKILL_ENGINEERING)
                ))
            return true;
        if (ai->HasSkill(SKILL_SKINNING) &&
            (IsItemUsedBySkill(proto, SKILL_SKINNING)))// || IsItemUsedBySkill(proto, SKILL_LEATHERWORKING)))
            return true;
        if (ai->HasSkill(SKILL_HERBALISM) &&
            (IsItemUsedBySkill(proto, SKILL_HERBALISM)))// || IsItemUsedBySkill(proto, SKILL_ALCHEMY)))
            return true;
        break;
    }
    case ITEM_CLASS_RECIPE:
    {
        if (bot->HasSpell(GetRecipeSpell(proto)))
            break;

        switch (proto->SubClass)
        {
        case ITEM_SUBCLASS_LEATHERWORKING_PATTERN:
            return ai->HasSkill(SKILL_LEATHERWORKING);
        case ITEM_SUBCLASS_TAILORING_PATTERN:
            return ai->HasSkill(SKILL_TAILORING);
        case ITEM_SUBCLASS_ENGINEERING_SCHEMATIC:
            return ai->HasSkill(SKILL_ENGINEERING);
        case ITEM_SUBCLASS_BLACKSMITHING:
            return ai->HasSkill(SKILL_BLACKSMITHING);
        case ITEM_SUBCLASS_COOKING_RECIPE:
            return ai->HasSkill(SKILL_COOKING);
        case ITEM_SUBCLASS_ALCHEMY_RECIPE:
            return ai->HasSkill(SKILL_ALCHEMY);
        case ITEM_SUBCLASS_FIRST_AID_MANUAL:
            return ai->HasSkill(SKILL_FIRST_AID);
        case ITEM_SUBCLASS_ENCHANTING_FORMULA:
            return ai->HasSkill(SKILL_ENCHANTING);
        case ITEM_SUBCLASS_FISHING_MANUAL:
            return ai->HasSkill(SKILL_FISHING);
        }
    }
    }
    return false;
}

bool ItemUsageValue::IsItemNeededForUsefullCraft(ItemPrototype const* proto, bool checkAllReagents)
{
    std::vector<uint32> spellIds = AI_VALUE(std::vector<uint32>, "craft spells");

    for (uint32 spellId : spellIds)
    {
        const SpellEntry* pSpellInfo = sServerFacade.LookupSpellInfo(spellId);

        if (!pSpellInfo)
            continue;

        bool isReagentFor = false;
        bool hasOtherReagents = true;

        for (uint8 i = 0; i < MAX_SPELL_REAGENTS; i++)
        {
            if (!pSpellInfo->ReagentCount[i] || !pSpellInfo->Reagent[i])
                continue;

            if (pSpellInfo->Reagent[i] == proto->ItemId)
            {
                isReagentFor = true;
            }
            else if (checkAllReagents)
            {
                const ItemPrototype* reqProto = sObjectMgr.GetItemPrototype(pSpellInfo->Reagent[i]);

                uint32 count = AI_VALUE2(uint32, "item count", reqProto->Name1);

                if (count < pSpellInfo->ReagentCount[i])
                    hasOtherReagents = false;
            }
        }

        if (!isReagentFor || !hasOtherReagents)
            continue;

        if (!AI_VALUE2(bool, "should craft spell", spellId))
            continue;

        return true;
    }

    return false;
}

Item* ItemUsageValue::CurrentItem(ItemPrototype const* proto, Player* bot)
{
    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    AiObjectContext* context = ai->GetAiObjectContext();
    ChatHelper* chat = ai->GetChatHelper();
    Item* bestItem = nullptr;
    std::list<Item*> found = AI_VALUE2(std::list < Item*>, "inventory items", chat->formatItem(proto));

    for (auto item : found)
    {
        if (bestItem && item->GetUInt32Value(ITEM_FIELD_DURABILITY) < bestItem->GetUInt32Value(ITEM_FIELD_DURABILITY))
            continue;

        if (bestItem && item->GetCount() < bestItem->GetCount())
            continue;

        bestItem = item;
    }

    return bestItem;
}

Item* ItemUsageValue::CurrentItemInSlot(ItemPrototype const* proto, Player* bot)
{
    uint16 dest;

    InventoryResult result;
    result = RandomBotFacade::CanEquipUnseenItem(bot, NULL_SLOT, dest, proto->ItemId);

    if (result != EQUIP_ERR_OK)
        return nullptr;

    return bot->GetItemByPos(dest);
}

float ItemUsageValue::CurrentStacks(PlayerbotAI* ai, ItemPrototype const* proto)
{
    uint32 maxStack = proto->GetMaxStackSize();

    AiObjectContext* context = ai->GetAiObjectContext();
    ChatHelper* chat = ai->GetChatHelper();

    std::list<Item*> found = AI_VALUE2(std::list<Item*>, "inventory items", chat->formatItem(proto));

    float itemCount = 0;

    for (auto stack : found)
    {
        itemCount += stack->GetCount();
    }

    return itemCount / maxStack;
}

float ItemUsageValue::BetterStacks(ItemPrototype const* proto, std::string itemType)
{
    std::list<Item*> items = AI_VALUE2(std::list<Item*>, "inventory items", itemType);

    float stacks = 0;

    for (auto& otherItem : items)
    {
        const ItemPrototype* otherProto = otherItem->GetProto();

        if (otherProto->Class != proto->Class || otherProto->SubClass != proto->SubClass)
            continue;

        if (otherProto->ItemLevel < proto->ItemLevel)
            continue;

        if (otherProto->ItemId == proto->ItemId)
            continue;

        stacks += CurrentStacks(ai, otherProto);
    }

    return stacks;
}


std::vector<uint32> ItemUsageValue::SpellsUsingItem(uint32 itemId, Player* bot)
{
    std::vector<uint32> retSpells;

    PlayerSpellMap const& spellMap = bot->GetSpellMap();

    for (auto& spell : spellMap)
    {
        uint32 spellId = spell.first;

        if (spell.second.state == PLAYERSPELL_REMOVED || spell.second.disabled || IsPassiveSpell(spellId))
            continue;

        const SpellEntry* pSpellInfo = sServerFacade.LookupSpellInfo(spellId);
        if (!pSpellInfo)
            continue;

        for (uint8 i = 0; i < MAX_SPELL_REAGENTS; i++)
            if (pSpellInfo->ReagentCount[i] > 0 && pSpellInfo->Reagent[i] == itemId)
                retSpells.push_back(spellId);
    }

    return retSpells;
}

bool ItemUsageValue::IsHpFoodOrDrink(ItemPrototype const* proto)
{
    if (proto->Class == ItemClass::ITEM_CLASS_CONSUMABLE
        && (proto->SubClass == ItemSubclassConsumable::ITEM_SUBCLASS_CONSUMABLE
            || proto->SubClass == ItemSubclassConsumable::ITEM_SUBCLASS_FOOD
            || proto->SubClass == ItemSubclassConsumable::ITEM_SUBCLASS_CONSUMABLE_OTHER))
    {
        for (auto spell : proto->Spells)
        {
            if (spell.SpellCategory == 11)
            {
                return true;
            }
        }
    }

    return false;
}

bool ItemUsageValue::IsManaFoodOrDrink(ItemPrototype const* proto)
{
    if (proto->Class == ItemClass::ITEM_CLASS_CONSUMABLE
        && (proto->SubClass == ItemSubclassConsumable::ITEM_SUBCLASS_CONSUMABLE
            || proto->SubClass == ItemSubclassConsumable::ITEM_SUBCLASS_FOOD
            || proto->SubClass == ItemSubclassConsumable::ITEM_SUBCLASS_CONSUMABLE_OTHER))
    {
        for (auto spell : proto->Spells)
        {
            if (spell.SpellCategory == 59)
            {
                return true;
            }
        }
    }

    return false;
}

bool ItemUsageValue::IsHealingPotion(ItemPrototype const* proto)
{
    if (proto->Class == ItemClass::ITEM_CLASS_CONSUMABLE &&
        (proto->SubClass == ItemSubclassConsumable::ITEM_SUBCLASS_CONSUMABLE
            || proto->SubClass == ItemSubclassConsumable::ITEM_SUBCLASS_POTION
            || proto->SubClass == ItemSubclassConsumable::ITEM_SUBCLASS_FLASK
            || proto->SubClass == ItemSubclassConsumable::ITEM_SUBCLASS_CONSUMABLE_OTHER))
    {
        for (int j = 0; j < MAX_ITEM_PROTO_SPELLS; j++)
        {
            const SpellEntry* const spellInfo = sServerFacade.LookupSpellInfo(proto->Spells[j].SpellId);
            if (spellInfo)
                for (int i = 0; i < 3; i++)
                {
                    if (spellInfo->Effect[i] == SPELL_EFFECT_HEAL)
                        return true;
                }
        }
    }

    return false;
}

bool ItemUsageValue::IsManaPotion(ItemPrototype const* proto)
{
    if (proto->Class == ItemClass::ITEM_CLASS_CONSUMABLE &&
        (proto->SubClass == ItemSubclassConsumable::ITEM_SUBCLASS_CONSUMABLE
            || proto->SubClass == ItemSubclassConsumable::ITEM_SUBCLASS_POTION
            || proto->SubClass == ItemSubclassConsumable::ITEM_SUBCLASS_FLASK
            || proto->SubClass == ItemSubclassConsumable::ITEM_SUBCLASS_CONSUMABLE_OTHER))
    {
        for (int j = 0; j < MAX_ITEM_PROTO_SPELLS; j++)
        {
            const SpellEntry* const spellInfo = sServerFacade.LookupSpellInfo(proto->Spells[j].SpellId);
            if (spellInfo)
                for (int i = 0; i < 3; i++)
                {
                    if (spellInfo->Effect[i] == SPELL_EFFECT_ENERGIZE)
                        return true;
                }
        }
    }

    return false;
}

bool ItemUsageValue::IsBandage(ItemPrototype const* proto)
{
    if (proto->Class == ItemClass::ITEM_CLASS_CONSUMABLE
        && proto->SubClass == ItemSubclassConsumable::ITEM_SUBCLASS_BANDAGE)
    {
        return true;
    }

    return false;
}

bool ItemUsageValue::IsAntiVenom(ItemPrototype const* proto)
{
    if (proto->Class == ItemClass::ITEM_CLASS_CONSUMABLE)
    {
        for (int j = 0; j < MAX_ITEM_PROTO_SPELLS; j++)
        {
            const SpellEntry* const spellInfo = sServerFacade.LookupSpellInfo(proto->Spells[j].SpellId);
            if (spellInfo)
            {
                for (int i = 0; i < 3; i++)
                {
                    if (spellInfo->Effect[i] == SPELL_EFFECT_DISPEL && spellInfo->EffectMiscValue[i] == DISPEL_POISON)
                        return true;
                }
            }
        }
    }

    return false;
}

uint32 ItemUsageValue::GetRecipeSpell(ItemPrototype const* proto)
{

    if (proto->Spells[2].SpellId)
        return proto->Spells[2].SpellId;

    for (uint8 i = 0; i < 4; i++)
    {
        if (proto->Spells[i].SpellId)
        {
            const SpellEntry* pSpellInfo = sServerFacade.LookupSpellInfo(proto->Spells[i].SpellId);

            if (!pSpellInfo)
                return 0;

            for (int j = 0; j < 3; ++j)
            {
                if (pSpellInfo->Effect[j] == SPELL_EFFECT_LEARN_SPELL)
                {
                    if (pSpellInfo->EffectTriggerSpell[j])
                        return pSpellInfo->EffectTriggerSpell[j];
                }
            }
        }
    }
    return 0;
}

void ItemUsageValue::PopulateProfessionReagentIds()
{
    m_allReagentItemIdsForCraftingSkillsVector.clear();

    for (uint32 i = 0; i < sSkillLineStore.GetNumRows(); ++i)
    {
        SkillLineEntry const* skillInfo = sSkillLineStore.LookupEntry(i);
        if (!skillInfo)
            continue;

        if (skillInfo->categoryId == SKILL_CATEGORY_PROFESSION || skillInfo->categoryId == SKILL_CATEGORY_SECONDARY)
        {
            for (uint32 j = 0; j < sSkillLineAbilityStore.GetNumRows(); ++j)
            {
                SkillLineAbilityEntry const* skillLine = sSkillLineAbilityStore.LookupEntry(j);
                if (!skillLine)
                    continue;

                // wrong skill
                if (skillLine->skillId != skillInfo->id)
                    continue;

                SpellEntry const* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(skillLine->spellId);
                if (spellInfo)
                {
                    bool isReagentFor = false;

                    for (uint8 i = 0; i < MAX_SPELL_REAGENTS; i++)
                    {
                        if (!spellInfo->ReagentCount[i] || !spellInfo->Reagent[i])
                            continue;

                        m_reagentItemIdsForCraftingSkills[skillLine->skillId].insert(spellInfo->Reagent[i]);
                        m_allReagentItemIdsForCraftingSkills.insert(spellInfo->Reagent[i]);
                        m_allReagentItemIdsForCraftingSkillsVector.push_back(spellInfo->Reagent[i]);
                    }
                }
            }
        }
    }
}

void ItemUsageValue::PopulateReagentItemIdsForCraftableItemIds()
{
    m_craftingReagentItemIdsForCraftableItem.clear();
    for (uint32 j = 0; j < sSkillLineAbilityStore.GetNumRows(); ++j)
    {
        SkillLineAbilityEntry const* skillLine = sSkillLineAbilityStore.LookupEntry(j);
        if (!skillLine)
            continue;

        SpellEntry const* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(skillLine->spellId);
        if (spellInfo)
        {
            for (int i = 0; i < 3; ++i)
            {
                if (spellInfo->Effect[i] == SPELL_EFFECT_CREATE_ITEM)
                {
                    uint32 craftedItemId = spellInfo->EffectItemType[i];

                    if (craftedItemId)
                    {
                        for (uint32 x = 0; x < MAX_SPELL_REAGENTS; ++x)
                        {
                            if (spellInfo->Reagent[x] <= 0)
                            {
                                continue;
                            }

                            uint32 reagentItemId = spellInfo->Reagent[x];
                            uint32 reagentsRequiredCount = spellInfo->ReagentCount[x];
                            if (reagentItemId && sObjectMgr.GetItemPrototype(reagentItemId))
                            {
                                m_craftingReagentItemIdsForCraftableItem[craftedItemId].push_back({ reagentItemId , reagentsRequiredCount });
                            }
                        }
                    }
                }
            }
        }
    }
}

void ItemUsageValue::PopulateSoldByVendorItemIds()
{
    if (auto result = WorldDatabase.PQuery("%s", "SELECT distinct item FROM npc_vendor"))
    {
        BarGoLink bar(result->GetRowCount());
        do
        {
            bar.step();
            Field* fields = result->Fetch();
            uint32 entry = fields[0].GetUInt32();
            if (!entry)
                continue;

            if (!sObjectMgr.GetItemPrototype(entry))
                continue;

            m_allItemIdsSoldByAnyVendors.insert(fields[0].GetUInt32());
        } while (result->NextRow());
    }

    if (auto result = WorldDatabase.PQuery("%s", "SELECT distinct item FROM npc_vendor WHERE maxcount > 0"))
    {
        BarGoLink bar(result->GetRowCount());
        do
        {
            bar.step();
            Field* fields = result->Fetch();
            uint32 entry = fields[0].GetUInt32();
            if (!entry)
                continue;

            if (!sObjectMgr.GetItemPrototype(entry))
                continue;

            m_itemIdsSoldByAnyVendorsWithLimitedMaxCount.insert(fields[0].GetUInt32());
        } while (result->NextRow());
    }
}

std::vector<uint32> ItemUsageValue::GetAllReagentItemIdsForCraftingSkillsVector()
{
    return m_allReagentItemIdsForCraftingSkillsVector;
}

std::vector<std::pair<uint32, uint32>> ItemUsageValue::GetAllReagentItemIdsForCraftingItem(ItemPrototype const* proto)
{
    auto items = m_craftingReagentItemIdsForCraftableItem.find(proto->ItemId);

    if (items == m_craftingReagentItemIdsForCraftableItem.end())
        return {};

    return items->second;
}

bool ItemUsageValue::IsItemSoldByAnyVendor(ItemPrototype const* proto)
{
    return m_allItemIdsSoldByAnyVendors.count(proto->ItemId) > 0;
}

bool ItemUsageValue::IsItemSoldByAnyVendorButHasLimitedMaxCount(ItemPrototype const* proto)
{
    return m_itemIdsSoldByAnyVendorsWithLimitedMaxCount.count(proto->ItemId) > 0;
}

bool ItemUsageValue::IsItemUsedBySkill(ItemPrototype const* proto, SkillType skillType)
{
    return m_reagentItemIdsForCraftingSkills[skillType].count(proto->ItemId) > 0;
}

bool ItemUsageValue::IsItemUsedToCraftAnything(ItemPrototype const* proto)
{
    return m_allReagentItemIdsForCraftingSkills.count(proto->ItemId) > 0;
}

uint32 ItemUsageValue::GetAHMedianBuyoutPricePerItem(ItemPrototype const* proto, Player* bot)
{
    if (sPlayerbotAIConfig.shouldQueryAHListingsOutsideOfAH)
    {
        std::vector<float> prices;

        std::vector<AuctionEntry> listings = sRandomBotFacade.GetAhPrices(proto->ItemId, bot);
        for (auto& auction : listings)
        {
            uint32 itemCount = GetAuctionItemCount(auction);
            if (itemCount)
                prices.push_back((float)auction.buyout / (float)itemCount);
        }

        if (prices.empty())
            return 0;

        size_t n = prices.size() / 2;
        std::nth_element(prices.begin(), prices.begin() + n, prices.end());
        float median = prices[n];
        if (median > 0 && median < 1)
            return 1;
        return static_cast<uint32>(median);
    }

    return 0;
}

uint32 ItemUsageValue::GetAHListingLowestBuyoutPricePerItem(ItemPrototype const* proto, Player* bot)
{
    if (sPlayerbotAIConfig.shouldQueryAHListingsOutsideOfAH)
    {
        // M9: track the minimum by PER-UNIT price and return it. The old code
        // compared listing totals but the function promises per-item.
        // REVIEW-FIX: keep a positive sentinel — a found listing with a
        // sub-copper unit price still returns 1, never the 0 of "no listing",
        // so AreCurrentAHListingsTooCheap and resale paths can tell the two
        // apart exactly as before (old code returned the positive total).
        float minPrice = 0;
        bool found = false;

        std::vector<AuctionEntry> listings = sRandomBotFacade.GetAhPrices(proto->ItemId, bot);
        for (auto& auction : listings)
        {
            uint32 itemCount = GetAuctionItemCount(auction);
            if (itemCount)
            {
                float unitPrice = (float)auction.buyout / (float)itemCount;
                if (!found || unitPrice < minPrice)
                {
                    minPrice = unitPrice;
                    found = true;
                }
            }
        }

        if (!found)
            return 0;
        if (minPrice > 0 && minPrice < 1)
            return 1;
        return (uint32)minPrice;
        /*
        auto query = CharacterDatabase.PQuery(
            "SELECT buyoutprice / item_count"
            " FROM auction"
            " WHERE item_template = '%u'"
            " ORDER BY buyoutprice ASC"
            " LIMIT 1",
            proto->ItemId
        );
        if (query)
        {
            do
            {
                Field* fields = query->Fetch();

                uint32 lowestBuyoutPrice = (fields[0].GetUInt32());

                return lowestBuyoutPrice;
            } while (query->NextRow());
        }
        */
    }

    return 0;
}

bool ItemUsageValue::AreCurrentAHListingsTooCheap(ItemPrototype const* proto)
{
    uint32 lowestAhItemListingBuyoutPrice = GetAHListingLowestBuyoutPricePerItem(proto);
    uint32 lowestAcceptapleAhBuyoutPrice = GetBotAHSellMinPrice(proto);

    //check if AH listings are already at the bottom price (with a 1% margin for possible calculation errors and is generally better)
    if (lowestAhItemListingBuyoutPrice > 0 && lowestAhItemListingBuyoutPrice <= lowestAcceptapleAhBuyoutPrice + (lowestAcceptapleAhBuyoutPrice * 0.01f))
    {
        return true;
    }

    return false;
}

bool ItemUsageValue::IsMoreProfitableToSellToAHThanToVendor(ItemPrototype const* proto, Player* bot)
{
    if (proto->Bonding == ItemBondingType::BIND_WHEN_PICKED_UP)
    {
        return false;
    }

    if (AreCurrentAHListingsTooCheap(proto))
    {
        return false;
    }

    for (auto itemId : sPlayerbotAIConfig.ahOverVendorItemIds)
    {
        if (itemId == proto->ItemId)
        {
            return true;
        }
    }

    for (auto itemId : sPlayerbotAIConfig.vendorOverAHItemIds)
    {
        if (itemId == proto->ItemId)
        {
            return false;
        }
    }

    //exceptions
    //cool consumeables?
    if (proto->ItemId == 2662) //noggerfogger
        return true;

    if (proto->Class == ItemClass::ITEM_CLASS_QUEST)
    {
        return true;
    }

    if (IsItemUsedToCraftAnything(proto) && !IsItemSoldByAnyVendor(proto))
    {
        return true;
    }

    if (IsItemSoldByAnyVendorButHasLimitedMaxCount(proto) && !(proto->Class == ItemClass::ITEM_CLASS_CONSUMABLE && proto->SubClass == ItemSubclassConsumable::ITEM_SUBCLASS_SCROLL))
    {
        return true;
    }

    switch (proto->Quality)
    {
    case ItemQualities::ITEM_QUALITY_POOR:
    {
        //gray items usually are only worth as much as their vendor sell price
        return false;
    }
    case ItemQualities::ITEM_QUALITY_NORMAL:
    {
        if (proto->Class == ItemClass::ITEM_CLASS_WEAPON)
        {
            //white weapons is trash
            return false;
        }

        if (proto->Class == ItemClass::ITEM_CLASS_ARMOR)
        {
            //shirts are nice to AH (or other cosmetics something?), also avoid something super cheap like starter boots
            if (proto->SubClass == ItemSubclassArmor::ITEM_SUBCLASS_ARMOR_MISC && GetItemBaseValue(proto) > 10)
            {
                return true;
            }

            //white armor is trash
            return false;
        }

        if (proto->Class == ItemClass::ITEM_CLASS_PROJECTILE)
        {
            //white projectile class is trash
            return false;
        }

        if (proto->Class == ItemClass::ITEM_CLASS_REAGENT)
        {
            //most reagents are sold by vendors
            return !IsItemSoldByAnyVendor(proto);
        }

        if (proto->Class == ItemClass::ITEM_CLASS_CONSUMABLE)
        {
            if (proto->SubClass == ItemSubclassConsumable::ITEM_SUBCLASS_SCROLL)
            {
                //scrolls are trash
                return false;
            }

            //food & drinks
            if ((proto->SubClass == ItemSubclassConsumable::ITEM_SUBCLASS_CONSUMABLE || proto->SubClass == ItemSubclassConsumable::ITEM_SUBCLASS_FOOD))
            {

                return !IsItemSoldByAnyVendor(proto);
            }
        }

        return true;
    }
    //UNCOMMON+ are usually all worth at least the "buy from vendor" price
    case ItemQualities::ITEM_QUALITY_UNCOMMON:
    case ItemQualities::ITEM_QUALITY_RARE:
    case ItemQualities::ITEM_QUALITY_EPIC:
    case ItemQualities::ITEM_QUALITY_LEGENDARY:
    case ItemQualities::ITEM_QUALITY_ARTIFACT:
    {
        return true;
    }
    default:
        break;
    }

    return false;
}

bool ItemUsageValue::IsWorthBuyingFromVendorToResellAtAH(ItemPrototype const* proto, bool isLimitedSupply)
{
    if (proto->Bonding == ItemBondingType::BIND_WHEN_PICKED_UP)
    {
        return false;
    }

    if (AreCurrentAHListingsTooCheap(proto))
    {
        return false;
    }

    for (auto itemId : sPlayerbotAIConfig.vendorOverAHItemIds)
    {
        if (itemId == proto->ItemId)
        {
            return false;
        }
    }

    //exceptions
    //cool consumeables?
    if (proto->ItemId == 2662) //noggerfogger
        return true;

    //copper rod
    if (proto->ItemId == 6217)
        return false;

    //do not buy items that require reputation to use
    if (proto->RequiredReputationFaction > 0 || proto->RequiredReputationRank > 0)
    {
        return false;
    }

    //do not buy items that require honor to use
    if (proto->RequiredHonorRank > 0)
    {
        return false;
    }

    if (GetAHMedianBuyoutPricePerItem(proto) > static_cast<uint32>(proto->BuyPrice * 1.1f))
    {
        return true;
    }

    if (isLimitedSupply && !(proto->Class == ItemClass::ITEM_CLASS_CONSUMABLE && proto->SubClass == ItemSubclassConsumable::ITEM_SUBCLASS_SCROLL))
    {
        return true;
    }

    //do not buy items that require skill to use, unless limited supply or recipe (just in case)
    if (proto->RequiredSkill > 0 && !(proto->Class == ItemClass::ITEM_CLASS_RECIPE))
    {
        return false;
    }

    switch (proto->Quality)
    {
    case ItemQualities::ITEM_QUALITY_POOR:
    {
        return false;
    }
    case ItemQualities::ITEM_QUALITY_NORMAL:
    {
        //some white items may be worth it

        if (proto->Class == ItemClass::ITEM_CLASS_RECIPE)
        {
            if (proto->SubClass == ItemSubclassRecipe::ITEM_SUBCLASS_BOOK)
            {
                return true;
            }
        }

        return false;
    }
    //UNCOMMON+ are usually all worth it
    case ItemQualities::ITEM_QUALITY_UNCOMMON:
    case ItemQualities::ITEM_QUALITY_RARE:
    case ItemQualities::ITEM_QUALITY_EPIC:
    case ItemQualities::ITEM_QUALITY_LEGENDARY:
    case ItemQualities::ITEM_QUALITY_ARTIFACT:
    {
        return true;
    }
    default:
        break;
    }

    return false;
}

bool ItemUsageValue::IsWorthBuyingFromAhToResellAtAH(ItemPrototype const* proto, uint32 totalCost, uint32 itemCount)
{
    uint32 pricePerItem = totalCost / itemCount;

    //bottom half is probably always worth buying? That should reduce oversupply
    return pricePerItem <= GetBotAHSellMinPrice(proto) + ((GetBotAHSellMaxPrice(proto) - GetBotAHSellMinPrice(proto)) / 2);
}

double ItemUsageValue::GetLevelPriceMultiplier(ItemPrototype const* proto)
{
    float x = 0.1f + proto->ItemLevel;
    return 0.5 + exp(x / 60) / 2;
}

uint32 ItemUsageValue::GetItemBaseValue(ItemPrototype const* proto, uint8 maxReagentLevel)
{
    if (proto->Quality == ItemQualities::ITEM_QUALITY_POOR)
    {
        return proto->SellPrice;
    }

    if (IsItemSoldByAnyVendor(proto))
    {
        //if item is sold by a vendor - price can never be lower than BuyPrice, because bots may buy from vendor to resell
        return proto->BuyPrice;
    }

    //calculate total value of reagents if the item is craftable
    if (maxReagentLevel > 0 && GetAllReagentItemIdsForCraftingItem(proto).size() > 0)
    {
        maxReagentLevel--;
        uint32 totalReagentsValue = 0;

        for (auto idCountPair : GetAllReagentItemIdsForCraftingItem(proto))
        {
            ItemPrototype const* reagentProto = sObjectMgr.GetItemPrototype(idCountPair.first);
            totalReagentsValue += GetItemBaseValue(reagentProto, maxReagentLevel) * idCountPair.second;
        }

        if (totalReagentsValue > 0)
        {
            return totalReagentsValue;
        }
    }

    //some items, which are not sold by vendors, have very low or very high vendor buy price, can't rely on it, need to adjust SellPrice
    return static_cast<uint32>(proto->SellPrice * GetLevelPriceMultiplier(proto) * 1.5f);
}

uint32 ItemUsageValue::GetBotBuyPrice(ItemPrototype const* proto, Player* bot)
{
    return static_cast<uint32>(GetItemBaseValue(proto) * sRandomBotFacade.GetBuyMultiplier(bot));
}

uint32 ItemUsageValue::GetBotSellPrice(ItemPrototype const* proto, Player* bot)
{
    //should never sell for less than sell to vendor price
    return std::max(
        static_cast<uint32>((GetItemBaseValue(proto) + 1) * sRandomBotFacade.GetSellMultiplier(bot)),
        static_cast<uint32>(proto->SellPrice * 1.1f)
    );
}

uint32 ItemUsageValue::GetBotAHSellMinPrice(ItemPrototype const* proto)
{
    return static_cast<uint32>(GetItemBaseValue(proto) * 2.00f);
}

uint32 ItemUsageValue::GetBotAHSellMaxPrice(ItemPrototype const* proto)
{
    return static_cast<uint32>(GetItemBaseValue(proto) * 2.5f);
}

uint32 ItemUsageValue::GetCraftingFee(ItemPrototype const* proto)
{
    uint32 fixedMinCraftingFee = 100;
    uint32 level = std::max(proto->ItemLevel, proto->RequiredLevel);
    return fixedMinCraftingFee * level * level / 40;
}

uint32 ItemUsageValue::DesiredPricePerItem(Player* bot, const ItemPrototype* proto, uint32 count, uint32 priceModifier)
{
    AuctionEntry lowestPrice;

    lowestPrice.Id = 0;

    uint32 lowestItemCount = 0;

    std::vector<AuctionEntry> listings = sRandomBotFacade.GetAhPrices(proto->ItemId, bot);
    for (auto& auction : listings)
    {
        uint32 itemCount = GetAuctionItemCount(auction);
        if (itemCount != count)
            continue;

        float pricePerItem = float(auction.buyout) / float(itemCount);
        if (lowestPrice.Id == 0 || pricePerItem < float(lowestPrice.buyout) / float(lowestItemCount))
        {
            lowestPrice = auction;
            lowestItemCount = itemCount;
        }
    }

    uint32 lowestBuyoutItemPricePerItem = lowestItemCount ?
        static_cast<uint32>(float(lowestPrice.buyout) / float(lowestItemCount)) : 0;

    uint32 maxAhPrice = GetBotAHSellMaxPrice(proto);
    uint32 minAhPrice = GetBotAHSellMinPrice(proto);

    if (!maxAhPrice)
    {
        minAhPrice = lowestBuyoutItemPricePerItem;
        maxAhPrice = GetAHMedianBuyoutPricePerItem(proto, bot) * 1.5f;
        if (!maxAhPrice)
            maxAhPrice = minAhPrice * 1.5f;
    }

    uint32 desiredPricePerItem = minAhPrice + static_cast<uint32>((maxAhPrice - minAhPrice) * priceModifier / 100);

    if (lowestBuyoutItemPricePerItem > 0 && lowestPrice.owner != bot->GetGUIDLow())
    {
        uint32 undercutByMoney = std::max(static_cast<uint32>(1), static_cast<uint32>(lowestBuyoutItemPricePerItem * frand(0.0f, 0.1f)));

        if (undercutByMoney < lowestBuyoutItemPricePerItem)
        {
            desiredPricePerItem = lowestBuyoutItemPricePerItem - undercutByMoney;
        }
        else
        {
            desiredPricePerItem = lowestBuyoutItemPricePerItem - 1;
        }
    }

    desiredPricePerItem = std::max(minAhPrice, desiredPricePerItem);

    return desiredPricePerItem;
}
