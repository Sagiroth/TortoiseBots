#include "LootObjectStack.h"
#include "playerbot.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/values/SharedValueContext.h"

using namespace ai;

#define MAX_LOOT_OBJECT_COUNT 10

LootTarget::LootTarget(ObjectGuid guid) : guid(guid), asOfTime(time(0))
{
}

LootTarget::LootTarget(LootTarget const& other)
{
    guid = other.guid;
    asOfTime = other.asOfTime;
}

LootTarget& LootTarget::operator=(LootTarget const& other)
{
    if((void*)this == (void*)&other)
        return *this;

    guid = other.guid;
    asOfTime = other.asOfTime;

    return *this;
}

bool LootTarget::operator< (const LootTarget& other) const
{
    return guid < other.guid;
}

void LootTargetList::shrink(time_t fromTime)
{
    for (std::set<LootTarget>::iterator i = begin(); i != end(); )
    {
        if (i->asOfTime <= fromTime)
            erase(i++);
		else
			++i;
    }
}

LootObject::LootObject(Player* bot, ObjectGuid guid)
	: guid(), skillId(SKILL_NONE), reqSkillValue(0), reqItem(0)
{
    Refresh(bot, guid);
}

void LootObject::Refresh(Player* bot, ObjectGuid guid, bool debug)
{
    skillId = SKILL_NONE;
    reqSkillValue = 0;
    reqItem = 0;
    this->guid = ObjectGuid();

    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    Creature* creature = ai->GetCreature(guid);
    if (creature && sServerFacade.GetDeathState(creature) == CORPSE)
    {
        // Corpse items come first: while loot is left, "open loot" must use the regular
        // CMSG_LOOT path, also on a corpse the core already flagged skinnable. Skinning such a
        // corpse is refused (Spell::CheckCast -> SPELL_FAILED_TARGET_NOT_LOOTED while
        // !loot.isLooted()), so preferring the skin path here parked skinners next to an
        // unlootable-and-unskinnable beast casting Skinning every tick.
        if (creature->HasFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_LOOTABLE))
        {
            // The lootable flag is not a right: the core masks it per viewer with exactly
            // Player::IsAllowedToLoot (Object::BuildValuesUpdate), where the round-robin rule
            // lives. Without that test every grouped bot queues the same corpse and walks to
            // it -- opening an empty window and taking the gold out of turn -- while the member
            // whose round-robin kill it is finds nothing left.
            if (bot->IsAllowedToLoot(creature))
            {
                if (debug)
                    ai->TellDebug(ai->GetMaster(), "Creature flag lootable.", "debug loot");

                this->guid = guid;
                return;
            }

            if (debug)
                ai->TellDebug(ai->GetMaster(), "Creature lootable but not this bot's to loot.", "debug loot");
        }

        // Skinnable only once the corpse loot is gone (same condition the core applies to the
        // Skinning spell) and only while the core would let this bot skin it.
        if (creature->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SKINNABLE) && creature->loot.isLooted() &&
            creature->IsSkinnableBy(bot))
        {
            skillId = SKILL_SKINNING;
            uint32 targetLevel = creature->GetLevel();
            reqSkillValue = targetLevel < 10 ? 1 : targetLevel < 20 ? (targetLevel - 10) * 10 : targetLevel * 5;
            if (ai->HasSkill((SkillType)skillId) && bot->GetSkillValue(skillId) >= reqSkillValue)
            {
                if (debug)
                    ai->TellDebug(ai->GetMaster(), "Creature flag skinnable and has skill.", "debug loot");
                this->guid = guid;
            }
            else if (debug)
                ai->TellDebug(ai->GetMaster(), "Creature flag skinnable not enough skill.", "debug loot");

            return;
        }

        if (debug)
            ai->TellDebug(ai->GetMaster(), "Creature without loot or skin flag.", "debug loot");

        return;
    }

    GameObject* go = ai->GetGameObject(guid);
    if (go && sServerFacade.isSpawned(go) && go->getLootState() == GO_READY)
    {
        bool isQuestItemOnly = false;

        /*if (!guid.IsEmpty())
        {
            for (auto& entry : GAI_VALUE2(std::list<int32>, "item drop list", -1*uint(go->GetEntry())))
            {
                if (IsNeededForQuest(bot, entry))
                {
                    this->guid = guid;
                    return;
                }
                isQuestItemOnly |= entry > 0;
            }
        }*/

        if (isQuestItemOnly)
        {
            if (debug)
                ai->TellDebug(ai->GetMaster(), "Go has only quests items we don't need.", "debug loot");
            return;
        }

        uint32 goId = go->GetGOInfo()->id;
        std::set<uint32>& skipGoLootList = ai->GetAiObjectContext()->GetValue<std::set<uint32>&>("skip go loot list")->Get();
        if (skipGoLootList.find(goId) != skipGoLootList.end())
        {
            if (debug)
                ai->TellDebug(ai->GetMaster(), "Go in skip go loot list.", "debug loot");
            return;
        }

        uint32 lockId = go->GetGOInfo()->GetLockId();
        LockEntry const* lockInfo = sLockStore.LookupEntry(lockId);
        if (!lockInfo)
        {
            if (debug)
                ai->TellDebug(ai->GetMaster(), "Go has no lockid.", "debug loot");
            return;
        }

        for (int i = 0; i < 8; ++i)
        {
            switch (lockInfo->Type[i])
            {
                case LOCK_KEY_ITEM:
                    if (lockInfo->Index[i] > 0)
                    {
                        if (debug)
                            ai->TellDebug(ai->GetMaster(), "Go has lock with key requirement.", "debug loot");
                        reqItem = lockInfo->Index[i];
                        this->guid = guid;
                    }
                    break;
                case LOCK_KEY_SKILL:
                    if (goId == 13891 || goId == 19535) // Serpentbloom
                    {
                        if (debug)
                            ai->TellDebug(ai->GetMaster(), "Go is serpentbloom.", "debug loot");
                        this->guid = guid;
                    }
                    else if (SkillByLockType(LockType(lockInfo->Index[i])) > 0)
                    {
                        if (debug)
                            ai->TellDebug(ai->GetMaster(), "Go requires skill.", "debug loot");
                        skillId = SkillByLockType(LockType(lockInfo->Index[i]));
                        reqSkillValue = std::max((uint32)1, lockInfo->Skill[i]);
                        this->guid = guid;
                    }
                    break;
                case LOCK_KEY_NONE:
                    if (debug)
                        ai->TellDebug(ai->GetMaster(), "Go has open lock.", "debug loot");
                    this->guid = guid;
                    break;
            }
        }
    }

    if (debug && guid && !this->guid)
        ai->TellDebug(ai->GetMaster(), "Go has bad lock.", "debug loot");
}

WorldObject* LootObject::GetWorldObject(Player* bot)
{
    Refresh(bot, guid);

    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);

    Creature *creature = ai->GetCreature(guid);
    if (creature && sServerFacade.GetDeathState(creature) == CORPSE)
        return creature;

    GameObject* go = ai->GetGameObject(guid);
    if (go && sServerFacade.isSpawned(go))
        return go;

    return NULL;
}

LootObject::LootObject(const LootObject& other)
{
    guid = other.guid;
    skillId = other.skillId;
    reqSkillValue = other.reqSkillValue;
    reqItem = other.reqItem;
}

bool LootObject::IsLootPossible(Player* bot)
{
    if (IsEmpty() || !GetWorldObject(bot))
        return false;

    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);

    if (reqItem && !bot->HasItemCount(reqItem, 1))
        return false;

    if (guid.IsCreature())
    {
        Creature* creature = ai->GetCreature(guid);
        if (creature && sServerFacade.GetDeathState(creature) == CORPSE)
        {
            // loot.CanLoot check stubbed for Penqle baseline
            (void)bot;
        }
    }

    AiObjectContext* context = ai->GetAiObjectContext();

    if (!AI_VALUE2_LAZY(bool, "should loot object", std::to_string(guid.GetRawValue())))
        return false;

    // Check if the game object has quest loot and bot has the quest for it
    if (guid.IsGameObject())
    {
        GameObject* go = ai->GetGameObject(guid);
        if (go)
        {
            // Ignore for mining nodes and herbs
            if (skillId != SKILL_MINING && skillId != SKILL_HERBALISM)
            {
                if (sObjectMgr.IsGameObjectForQuests(guid.GetEntry()))
                {
                    if (!go->ActivateToQuest(bot))
                    {
                        return false;
                    }
                }
            }

            // herb-like quest objects
            if (skillId == SKILL_HERBALISM && reqSkillValue == 1)
            {
                if (sObjectMgr.IsGameObjectForQuests(guid.GetEntry()))
                {
                    if (go->ActivateToQuest(bot))
                    {
                        bool hasQuestItems = false;
                        for (auto& entry : GAI_VALUE2(std::list<uint32>, "entry loot list", -1*int(go->GetEntry())))
                        {
                            if (ItemUsageValue::IsNeededForQuest(bot, entry))
                            {
                                hasQuestItems = true;
                            }
                        }
                        return hasQuestItems || go->getLootState() != GO_READY;
                    }
                }
            }

            //Ignore objects that are currently in use.
            if (go->getLootState() != GO_READY || go->GetGoState() == GO_STATE_ACTIVE)
                return false;
        }
    }

    if (skillId == SKILL_NONE)
        return true;

    if (skillId == SKILL_FISHING)
        return false;

    if (!ai->HasSkill((SkillType)skillId))
        return false;

    if (!reqSkillValue)
        return true;

    uint32 skillValue = uint32(bot->GetSkillValue(skillId));
    if (reqSkillValue > skillValue)
        return false;

    if (skillId == SKILL_MINING && !bot->HasItemCount(2901, 1))
        return false;

    if (skillId == SKILL_SKINNING && !bot->HasItemCount(7005, 1))
        return false;

    return true;
}

// Mirrors the range the server itself enforces when a loot window is requested, so the bot
// keeps approaching until the server will really open the object instead of standing a few
// yards off it: creatures use the 3D Player::GetMaxLootDistance rule (Player::SendLoot ->
// Object::_IsWithinDist with SizeFactor::None), game objects the plain INTERACTION_DISTANCE
// check. A plain 2D "distance" comparison here once let "can loot" fire (2D <= 5) while
// "open loot" failed the 3D gate on sloped ground, which re-fired every tick.
bool LootObject::IsInLootRange(Player* bot)
{
    if (IsEmpty())
        return false;

    WorldObject* wo = GetWorldObject(bot);
    if (!wo)
        return false;

    if (wo->IsCreature())
    {
        Creature* creature = (Creature*)wo;
        return creature->IsWithinDistInMap(bot, bot->GetMaxLootDistance(creature), true, SizeFactor::None);
    }

    return sServerFacade.IsDistanceLessOrEqualThan(sServerFacade.getDistance2d(bot, wo), INTERACTION_DISTANCE);
}

void LootObjectStack::NoteApproachFailure(ObjectGuid guid)
{
    time_t now = time(0);

    // Drop aged-out memory (cheap: only a handful of entries, only on failure).
    for (std::map<ObjectGuid, std::pair<uint32, time_t> >::iterator i = approachFailures.begin(); i != approachFailures.end();)
    {
        if (now - i->second.second > APPROACH_FAILURE_TTL)
            i = approachFailures.erase(i);
        else
            ++i;
    }

    std::pair<uint32, time_t>& entry = approachFailures[guid];
    entry.first++;
    entry.second = now;
}

bool LootObjectStack::IsAbandoned(ObjectGuid guid)
{
    std::map<ObjectGuid, std::pair<uint32, time_t> >::iterator i = approachFailures.find(guid);
    if (i == approachFailures.end())
        return false;

    if (time(0) - i->second.second > APPROACH_FAILURE_TTL)
    {
        approachFailures.erase(i);
        return false;
    }

    return i->second.first >= MAX_APPROACH_FAILURES;
}

bool LootObjectStack::Add(ObjectGuid guid)
{
    // A corpse we repeatedly failed to reach stays out of the stack until the
    // failure memory ages out, even if a later "add all loot" sweep re-adds it.
    if (IsAbandoned(guid))
        return false;

    if (!availableLoot.insert(guid).second)
        return false;

    if (availableLoot.size() < MAX_LOOT_OBJECT_COUNT)
        return true;

    std::vector<LootObject> ordered = OrderByDistance();
    for (size_t i = MAX_LOOT_OBJECT_COUNT; i < ordered.size(); i++)
        Remove(ordered[i].guid);

    return true;
}

void LootObjectStack::Remove(ObjectGuid guid)
{
    LootTargetList::iterator i = availableLoot.find(guid);
    if (i != availableLoot.end())
        availableLoot.erase(i);
}

void LootObjectStack::Clear()
{
    availableLoot.clear();
    approachFailures.clear();
}

bool LootObjectStack::CanLoot(float maxDistance)
{
    std::vector<LootObject> ordered = OrderByDistance(maxDistance);
    return !ordered.empty();
}

LootObject LootObjectStack::GetLoot(float maxDistance)
{
    std::vector<LootObject> ordered = OrderByDistance(maxDistance);
    return ordered.empty() ? LootObject() : *ordered.begin();
}

bool LootObjectStack::IsWithinMasterLootRange(LootObject& loot)
{
    if (loot.IsEmpty())
        return true;

    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    Player* master = ai->GetMaster();
    if (!master || master == bot)
        return true;

    Creature* creature = ai->GetCreature(loot.guid);
    if (!creature || sServerFacade.GetDeathState(creature) != CORPSE)
        return true;

    return sServerFacade.IsDistanceLessOrEqualThan(sServerFacade.getDistance2d(master, creature), sPlayerbotAIConfig.lootDistance);
}

std::vector<LootObject> LootObjectStack::OrderByDistance(float maxDistance)
{
    size_t beforeShrink = availableLoot.size();
    availableLoot.shrink(time(0) - 30);
    if (availableLoot.size() < beforeShrink)
        sLog.outDebug("[BOT LOOT] %s: loot stack expired %zu corpse(s) (>30s old, dropped before looting)",
            bot->GetName(), beforeShrink - availableLoot.size());

    std::map<float, LootObject> sortedMap;
    LootTargetList safeCopy(availableLoot);
    for (LootTargetList::iterator i = safeCopy.begin(); i != safeCopy.end(); i++)
    {
        ObjectGuid guid = i->guid;
        if (IsAbandoned(guid))
            continue;

        LootObject lootObject(bot, guid);
        if (!lootObject.IsLootPossible(bot))
            continue;

        // A corpse out of the master's safe range is not available loot: "loot" refuses it and
        // "far from current loot" gives up on it, so listing it here only made the loot action
        // fire every tick without ever selecting a target (and never approaching one), while
        // follow starved behind it.
        if (!IsWithinMasterLootRange(lootObject))
            continue;

        float distance = bot->GetDistance(lootObject.GetWorldObject(bot));
        if (!maxDistance || distance <= maxDistance)
            sortedMap[distance] = lootObject;
    }

    std::vector<LootObject> result;
    for (std::map<float, LootObject>::iterator i = sortedMap.begin(); i != sortedMap.end(); i++)
        result.push_back(i->second);
    return result;
}
