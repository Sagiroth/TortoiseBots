#include "LootObjectStack.h"
#include "playerbot.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/values/SharedValueContext.h"
#include "playerbot/QuestLogPolicy.h"
#include "../../runtime/GatherToolPolicy.h"

using namespace ai;

#define MAX_LOOT_OBJECT_COUNT 10

// How long a queued corpse stays eligible. The stack is a work queue, not the authority on whether
// a corpse still holds loot: every read re-validates it (Refresh: still a CORPSE with the lootable
// flag and this bot's entitlement; IsLootPossible: something left to take), so this window only
// has to outlast the reason the bot could not get to the corpse yet. That reason is a fight - the
// loot chain is non-combat only - while the corpse itself stays for Corpse.Decay.NORMAL (300s).
// The old 30s window dropped the corpse of the kill that started a fight before the bot was out of
// combat again, so those kills were never looted at all.
#define LOOT_OBJECT_TTL_SECONDS 180

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

// May this bot open the corpse's loot window? This is the core's own entitlement test -- the one
// that masks the loot sparkle on a real client (Object::BuildValuesUpdate -> Player::IsAllowedToLoot)
// with the two clauses trimmed that exist only so that a *player* standing at the corpse can open
// it on behalf of the party:
//
//  * GROUP_LOOT / NEED_BEFORE_GREED: an unblocked over-threshold item makes IsAllowedToLoot true
//    for every member, so anybody may open the corpse and start the roll. A bot never needs the
//    walk for that -- the roll packet reaches it wherever it is and LootRollAction answers it --
//    while the corpse itself is opened by the member whose turn it is. Keeping the clause made
//    the whole party leave its targets for every green drop and keep walking back for the rest
//    of the 60s roll window (Loot::hasOverThresholdItem does not look at is_blocked).
//  * MASTER_LOOT: the same clause let any member open the corpse, in the overworld too (the
//    module's own master-looter check only covered dungeons): the first bot to reach a world
//    boss received MASTER_PERMISSION and the master loot list instead of the assigned looter,
//    who is the one expected to distribute.
//
// Everything else stays with the core rule: free-for-all, the round-robin turn, a solo tap (and
// the allowed-looter set recorded at the kill), and the quest/FFA/conditional items that are
// personal to this bot. A corpse the assigned member never opens stays unlooted -- the same as
// for a group of players in 1.12, where only that member can see and release the items.
//
// One deliberate exclusion: a creature tapped and killed by the bot's *pet* only (the owner never
// damaged it) is refused, and rightly so. Unit::Kill sets pPlayerTap = null for a pet recipient,
// so that kill is never credited: no loot is ever rolled for the corpse and Player::SendLoot
// refuses it (Creature::GetLootRecipient() resolves the pet guid to nothing). "Always tag the mob
// yourself or the pet kill gives no loot and no XP" is the vanilla rule, not a module bug; making
// such corpses lootable needs the core to credit the pet's owner, not a change here.
static bool MayLootCorpse(Player* bot, Creature* creature)
{
    if (!bot->IsAllowedToLoot(creature))
        return false;

    Group* group = bot->GetGroup();
    if (!group || group->isBGGroup() || group->GetLootMethod() == FREE_FOR_ALL)
        return true;                                        // the core grants every member this corpse

    Loot const& loot = creature->loot;
    if (loot.roundRobinPlayer == 0 || loot.roundRobinPlayer == bot->GetObjectGuid())
        return true;                                        // no turn assigned (released) or this bot's turn

    // The assigned master looter opens its corpses even if the turn recorded at the kill went
    // elsewhere (the turn is the tapper when the group had no looter guid yet).
    if (group->GetLootMethod() == MASTER_LOOT && group->GetLooterGuid() == bot->GetObjectGuid())
        return true;

    return loot.hasItemFor(bot);                            // another member's turn: personal loot only
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
            // The lootable flag is not a right: the core masks it per viewer with Player::IsAllowedToLoot
            // (Object::BuildValuesUpdate), where the round-robin rule lives. Without that test every
            // grouped bot queues the same corpse and walks to it -- opening an empty window and taking
            // the gold out of turn -- while the member whose round-robin kill it is finds nothing left.
            if (MayLootCorpse(bot, creature))
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
        // Bone Chew Toy piles (GO 1000380 near Goldshire): the only quest
        // needing the item is the inactive 40298, so opening them only
        // fills bags with junk (892 copies across 118 bots). The manual
        // "skip go loot list" stays for player choices; this one is data.
        if (goId == ai::kBoneChewToyGoEntry)
        {
            if (debug)
                ai->TellDebug(ai->GetMaster(), "Go is a Bone Chew Toy pile.", "debug loot");
            return;
        }
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

// Is there anywhere at all for loot to go? Body loot answers that per item ("should loot object"
// -> StackSpaceForItem), but a node's loot and a skinnable corpse's loot are rolled when the
// window opens, so their items do not exist yet and the per-item test cannot be asked. Ask the
// coarser question instead: a free bag slot, or a stack that is not full yet. A bot with neither
// opens the object and stores nothing (StoreLootAction drops every item), which for a node also
// leaves it GO_ACTIVATED holding its payload until the server resets it.
static bool HasBagRoomForLoot(PlayerbotAI* ai, Player* bot)
{
    // "bag space" is the used percentage, so anything below 100 has a slot to spare. Asking it
    // first keeps the item scan off the normal path (it is the same bag walk, computed once a tick).
    AiObjectContext* context = ai->GetAiObjectContext();
    if (AI_VALUE(uint8, "bag space") < 100)
        return true;

    for (int i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; ++i)
    {
        Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, i);
        if (item && item->GetCount() < item->GetMaxStackCount())
            return true;
    }

    for (int i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
    {
        Bag* bag = (Bag*)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, i);
        if (!bag)
            continue;

        for (uint32 j = 0; j < bag->GetBagSize(); ++j)
        {
            Item* item = bag->GetItemByPos(j);
            if (item && item->GetCount() < item->GetMaxStackCount())
                return true;
        }
    }

    return false;
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

    // "should loot object" asks whether the object's *current* loot holds anything this bot wants.
    // That question only has an answer for a creature corpse, whose body loot the core rolls at
    // death (Unit::Kill -> Loot::FillLoot). Every other payload is a consequence of the action
    // itself and cannot be known here:
    //
    //  * a skinnable corpse reaches this point only when Refresh has already seen it empty and
    //    released (UNIT_FLAG_SKINNABLE + loot.isLooted()), and the skinning table is rolled by the
    //    Skinning cast itself - so gating on "does the corpse still hold body loot" rejected every
    //    skin target in exactly the state skinning requires. Live: not one Skinning cast in the
    //    whole bot log corpus, zero leather, zero skinning skill-ups, while 55% of the looted
    //    corpses were skinnable and every skinner owned its knife.
    //  * a herb/ore node (like every unopened game object) has its loot rolled on the first open
    //    (Player::SendLoot), so no node could ever pass this test either.
    //
    // Body loot stays gated: a corpse whose remaining items this bot will not take is still not
    // worth queueing.
    if (skillId == SKILL_NONE && guid.IsCreature() &&
        !AI_VALUE2_LAZY(bool, "should loot object", std::to_string(guid.GetRawValue())))
        return false;

    // Skill objects and game objects are the two payloads "should loot object" cannot answer for,
    // and they are also the only ones that reached this point without any capacity test: body loot
    // got one above. Queue them only when the bot has somewhere to put the loot.
    if ((skillId != SKILL_NONE || guid.IsGameObject()) && !HasBagRoomForLoot(ai, bot))
        return false;

    // Bone Chew Toy piles never queue even if ActivateToQuest says yes:
    // Refresh (the open path) already refuses them, and this Add path would
    // otherwise re-queue the same junk pile every sweep.
    if (guid.IsGameObject() && guid.GetEntry() == ai::kBoneChewToyGoEntry)
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

    // E02: donor allowlist (mod-playerbots LootObjectStack.cpp:337-343), minus the
    // WotLK-only ids with no 1.12 rows (40772/40892/40893). Every id here was
    // verified to exist in tw_world.item_template; lists live in
    // runtime/GatherToolPolicy.h and are pinned by tools/test_gather_tool_policy.cpp.
    if (skillId == SKILL_MINING &&
        !TortoiseBots::HasAnyTool([bot](uint32 item) { return bot->HasItemCount(item, 1); },
                                  TortoiseBots::kMiningPickIds))
        return false;

    if (skillId == SKILL_SKINNING &&
        !TortoiseBots::HasAnyTool([bot](uint32 item) { return bot->HasItemCount(item, 1); },
                                  TortoiseBots::kSkinningKnifeIds))
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

    // Re-adding refreshes the entry's age. Ignoring a guid that is already queued left the corpse
    // with the timestamp of the first Add, so OrderByDistance dropped it LOOT_OBJECT_TTL_SECONDS
    // later even though "add all loot" (or the bot's own next kill) had just re-offered it.
    // A refresh is not an addition, though (donor LootObjectStack::Add returns false too):
    // "add gathering loot" runs every second above travel, and counting the node it had
    // already queued as done took the decision of bots standing next to an ore vein or herb.
    bool const known = availableLoot.erase(LootTarget(guid)) > 0;
    availableLoot.insert(LootTarget(guid));

    if (availableLoot.size() < MAX_LOOT_OBJECT_COUNT)
        return !known;

    std::vector<LootObject> ordered = OrderByDistance();
    for (size_t i = MAX_LOOT_OBJECT_COUNT; i < ordered.size(); i++)
        Remove(ordered[i].guid);

    return !known;
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
    availableLoot.shrink(time(0) - LOOT_OBJECT_TTL_SECONDS);
    if (availableLoot.size() < beforeShrink)
        sLog.outDebug("[BOT LOOT] %s: loot stack expired %zu corpse(s) (>%us old, dropped before looting)",
            bot->GetName(), beforeShrink - availableLoot.size(), (unsigned)LOOT_OBJECT_TTL_SECONDS);

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
