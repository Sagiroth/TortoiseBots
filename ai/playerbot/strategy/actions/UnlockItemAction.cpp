// Vanilla/Turtle lockpicking action.
// Spell::CanOpenLock remains authoritative for the final skill/lock check;
// this action only selects a real locked inventory item and starts Pick Lock.

#include "playerbot/playerbot.h"
#include "UnlockItemAction.h"
#include "Database/DBCStores.h"
#include "Objects/Item.h"
#include "Objects/ItemPrototype.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "SharedDefines.h"

#include <sstream>

inline constexpr uint32_t PICK_LOCK_SPELL_ID = 1804;

namespace
{
bool IsPickable(Item* item, Player* bot)
{
    if (!item || !bot || !item->GetProto())
        return false;

    uint32 lockId = item->GetProto()->LockID;
    if (!lockId || item->HasFlag(ITEM_FIELD_FLAGS, ITEM_DYNFLAG_UNLOCKED))
        return false;

    LockEntry const* lockInfo = sLockStore.LookupEntry(lockId);
    if (!lockInfo)
        return false;

    uint32 skill = bot->GetSkillValue(SKILL_LOCKPICKING);
    for (uint8 i = 0; i < MAX_LOCK_CASE; ++i)
    {
        if (lockInfo->Type[i] != LOCK_KEY_SKILL ||
            SkillByLockType(LockType(lockInfo->Index[i])) != SKILL_LOCKPICKING)
            continue;

        return skill >= lockInfo->Skill[i];
    }

    return false;
}
}

bool UnlockItemAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    if (bot->GetClass() != CLASS_ROGUE || !botAI->HasSkill(SKILL_LOCKPICKING) || !bot->HasSpell(PICK_LOCK_SPELL_ID))
    {
        ai->TellError(requester ? requester : bot, "Only a rogue with Pick Lock and Lockpicking can do that.");
        return false;
    }

    std::list<Item*> items = AI_VALUE2(std::list<Item*>, "inventory items", "all");
    for (Item* item : items)
    {
        if (!IsPickable(item, bot))
            continue;

        UnlockItem(item, requester);
        return true;
    }

    ai->TellPlayer(requester ? requester : bot, "I have no locked item that my Lockpicking skill can open.");
    return false;
}

void UnlockItemAction::UnlockItem(Item* item, Player* requester)
{
    if (botAI->CastSpell(PICK_LOCK_SPELL_ID, bot, item))
    {
        std::ostringstream out;
        out << "Used Pick Lock on: " << item->GetProto()->Name1;
        ai->TellPlayer(requester ? requester : bot, out.str());
    }
    else
        ai->TellError(requester ? requester : bot, "Failed to cast Pick Lock.");
}
