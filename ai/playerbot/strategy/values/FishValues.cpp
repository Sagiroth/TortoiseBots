
#include "playerbot/playerbot.h"
#include "playerbot/ServerFacade.h"
#include "FishValues.h"
#include "Timer.h"

using namespace ai;

bool CanFishValue::Calculate()
{
    if (!bot->GetSkill(SKILL_FISHING, false, false)) //Unable to fish.
        return false;

    // The shore search fishes from the bank; a swimming bot has no stand and
    // a fighting bot has no hands (donor CanFishValue, issue #402).
    if (bot->IsInWater() || bot->IsUnderwater())
        return false;

    if (sServerFacade.IsInCombat(bot))
        return false;

    std::list<Item*> poles = AI_VALUE2(std::list<Item*>, "inventory items", "fishing pole");

    if (poles.empty()) //No fishing pole.
        return false;

    return true;
}

bool CanOpenFishingDobberValue::Calculate()
{
    if (!bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
        return false;

    std::string spellName = bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL)->m_spellInfo->SpellName[0];
    if (spellName.find("Fishing") != 0)
        return false;

    return true;
}

bool DoneFishingValue::Calculate()
{
    if (!bot->GetSkill(SKILL_FISHING, false, false)) //Unable to fish.
        return false;

    Item* mhItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);

    //Does not have fishing pole equiped.
    if (!mhItem || mhItem->GetProto()->Class != ITEM_CLASS_WEAPON || mhItem->GetProto()->SubClass != ITEM_SUBCLASS_WEAPON_FISHING_POLE)
        return false;

    // Between two casts the bot holds the pole and channels nothing - that is not "done":
    // "done fishing" fired "equip upgrades" every tick between the casts, which took the
    // pole off (and, with a stat on it, put it right back). Half a minute after the last
    // cast the fishing is over and the weapon may come back.
    int const lastCast = AI_VALUE2(int, "manual int", "last fish cast");
    if (lastCast && WorldTimer::getMSTime() - uint32(lastCast) < 30000)
        return false;

    if (bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
    {
        std::string spellName = bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL)->m_spellInfo->SpellName[0];
        if (spellName.find("Fishing") == 0)
            return false;
    }

    return true;
}
