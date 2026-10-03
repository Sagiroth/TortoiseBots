
#include "playerbot/playerbot.h"
#include "SkinningLootPolicy.h"
#include "LootAction.h"

#include "playerbot/LootObjectStack.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/RandomBotFacade.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/values/LootStrategyValue.h"
#include "playerbot/strategy/values/LootValues.h"
#include "playerbot/strategy/values/ItemUsageValue.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/values/SharedValueContext.h"


using namespace ai;

bool LootAction::Execute(Event& event)
{
    if (!AI_VALUE(bool, "has available loot"))
        return false;

    LootObject prevLoot = AI_VALUE(LootObject, "loot target");

    // The stack already dropped corpses this bot may not loot (round robin, another player's
    // tap) and corpses out of the master's safe range, so the nearest remaining entry is the
    // target. Re-filtering here duplicated those rules, and when they drifted apart the loot
    // action kept firing without ever selecting anything.
    LootObject lootObject = AI_VALUE(LootObjectStack*, "available loot")->GetLoot(sPlayerbotAIConfig.lootDistance);

    if (lootObject.IsEmpty())
    {
        // The stack filtered to nothing (stale corpses, master-range, TTL):
        // the trigger cache still offers "loot" for up to 5 s, and every
        // tick failed at full speed (ACTION_LOOP). Reset the caches so the
        // next tick re-evaluates instead of failing again on the same stale
        // answer.
        RESET_AI_VALUE(LootObject, "loot target");
        RESET_AI_VALUE(bool, "has available loot");
        return false;
    }

    bool released = false;
    if (!prevLoot.IsEmpty() && prevLoot.guid != lootObject.guid)
    {
        WorldPacket packet(CMSG_LOOT_RELEASE, 8);
        packet << prevLoot.guid;
        bot->GetSession()->HandleLootReleaseOpcode(packet);
        released = true;
    }

    sLog.outDebug("[BOT LOOT] %s: select target=%lu (prev=%lu released=%d)",
        bot->GetName(), lootObject.guid.GetRawValue(), prevLoot.guid.GetRawValue(), released ? 1 : 0);

    context->GetValue<LootObject>("loot target")->Set(lootObject);
    return true;
}

enum ProfessionSpells
{
    HERB_GATHERING               = 2366,
    MINING                       = 2575,
    SKINNING                      = 8613
};

bool OpenLootAction::Execute(Event& event)
{
    LootObject lootObject = AI_VALUE(LootObject, "loot target");
    bool result = DoLoot(lootObject);
    if (result)
    {
        AI_VALUE(LootObjectStack*, "available loot")->Remove(lootObject.guid);
        context->GetValue<LootObject>("loot target")->Set(LootObject());
    }
    return result;
}

bool OpenLootAction::DoLoot(LootObject& lootObject)
{
    if (lootObject.IsEmpty())
        return false;

    sLog.outDebug("[BOT LOOT] %s: DoLoot target=%lu", bot->GetName(), lootObject.guid.GetRawValue());

    Creature* creature = ai->GetCreature(lootObject.guid);

    // Re-confirm the creature is still a fresh, lootable corpse before sending CMSG_LOOT.
    // The cached UNIT_DYNFLAG_LOOTABLE can lag behind a corpse that has despawned or
    // respawned while the bot was busy chain-killing (corpses age up to 30s in the loot
    // stack). A stale entry makes the server reply with a loot error (DIDNT_KILL) and the
    // bot kneel/abort on a non-corpse. Same predicate LootObjectStack::Refresh uses.
    if (creature && sServerFacade.GetDeathState(creature) != CORPSE)
    {
        sLog.outDebug("[BOT LOOT] %s: not a fresh CORPSE (deathstate=%d), dropping guid=%lu",
            bot->GetName(), (int)sServerFacade.GetDeathState(creature), lootObject.guid.GetRawValue());
        AI_VALUE(LootObjectStack*, "available loot")->Remove(lootObject.guid);
        RESET_AI_VALUE(LootObject, "loot target");
        return false;
    }

    // Gate the loot send on the SERVER's exact loot-range rule (LootObject::IsInLootRange: 3D
    // GetMaxLootDistance for corpses, INTERACTION_DISTANCE for game objects). The old creature
    // gate here was 3D but "can loot" still compared 2D distance, so on a slope the bot fired
    // CMSG_LOOT from a few yards above/below a corpse it had not reached, the server replied
    // TOO_FAR, and the same 8.0-priority action re-fired every tick while "move to loot" (7.0)
    // never got to walk the last yards.
    if (!lootObject.IsInLootRange(bot))
    {
        if (creature)
            sLog.outDebug("[BOT LOOT] %s: not in loot range (dist2d=%.1f maxLoot=%.1f), keep approaching guid=%lu",
                bot->GetName(), sServerFacade.getDistance2d(bot, creature), bot->GetMaxLootDistance(creature), lootObject.guid.GetRawValue());
        else
            sLog.outDebug("[BOT LOOT] %s: not in loot range, keep approaching guid=%lu",
                bot->GetName(), lootObject.guid.GetRawValue());
        return false;
    }

    // Items first, also on a corpse the core already flagged skinnable: skinning is refused
    // while any corpse loot is left (Spell::CheckCast -> SPELL_FAILED_TARGET_NOT_LOOTED), so the
    // old !UNIT_FLAG_SKINNABLE requirement made beast corpses unlootable (non-skinners ignored
    // them) and left skinners casting Skinning on an unlooted corpse every tick forever.
    if (creature && creature->HasFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_LOOTABLE))
    {
        if (!lootObject.IsLootPossible(bot)) //Clear loot if bot can't loot it.
        {
            sLog.outDebug("[BOT LOOT] %s: IsLootPossible=false on lootable corpse, clearing (corpse stays lootable -> may re-add)",
                bot->GetName());
            return true;
        }

        sLog.outDebug("[BOT LOOT] %s: loot ctx guid=%lu alive=%d tapped=%d dist2d=%.1f maxLoot=%.1f",
            bot->GetName(), lootObject.guid.GetRawValue(),
            creature->IsAlive() ? 1 : 0, creature->IsTappedBy(bot) ? 1 : 0,
            sServerFacade.getDistance2d(bot, creature), bot->GetMaxLootDistance(creature));

        sLog.outDebug("[BOT LOOT] %s: sending CMSG_LOOT (crouch emote) guid=%lu lootDelay=%u",
            bot->GetName(), lootObject.guid.GetRawValue(), sPlayerbotAIConfig.lootDelay);

        WorldPacket packet(CMSG_LOOT, 8);
        packet << lootObject.guid;
        bot->GetSession()->HandleLootOpcode(packet);
        SetDuration(sPlayerbotAIConfig.lootDelay);

        if (isRealPlayer_Helper(bot))
        {
            WorldPacket data(SMSG_EMOTE, 4 + 8);
            data << uint32(EMOTE_ONESHOT_LOOT);
            data << bot->getObjectGuid();
            bot->GetSession()->SendPacket(&data);
        }

        return true;
    }

    if (creature)
    {
        SkillType skill = (SkillType)GetRequiredLootSkillCompat(creature->GetCreatureInfo());
        sLog.outDebug("[BOT LOOT] %s: gather/skin path skill=%u reqValue=%u", bot->GetName(), skill, lootObject.reqSkillValue);
        if (!CanOpenLock(skill, lootObject.reqSkillValue))
        {
            sLog.outDebug("[BOT LOOT] %s: CanOpenLock=false (skill %u), abort", bot->GetName(), skill);
            return false;
        }

        switch (skill)
        {
        case SKILL_ENGINEERING:
            return false;
        case SKILL_HERBALISM:
            return ai->HasSkill(SKILL_HERBALISM) ? ai->CastSpell(HERB_GATHERING, creature) : false;
        case SKILL_MINING:
            return ai->HasSkill(SKILL_MINING) ? ai->CastSpell(MINING, creature) : false;
        default:
            return ai->HasSkill(SKILL_SKINNING) ? ai->CastSpell(SKINNING, creature) : false;
        }
    }

    GameObject* go = ai->GetGameObject(lootObject.guid);
    if (go && (go->getLootState() == GO_ACTIVATED || go->GetGoState() == GO_STATE_ACTIVE))
        return false;

    if (lootObject.skillId == SKILL_MINING)
        return ai->HasSkill(SKILL_MINING) ? ai->CastSpell(MINING, bot) : false;

    if (lootObject.skillId == SKILL_HERBALISM)
    {
        // herb-like quest objects
        bool isForQuest = false;
        if (go && sObjectMgr.IsGameObjectForQuests(lootObject.guid.GetEntry()))
        {
            if (go->ActivateToQuest(bot))
            {
                std::list<uint32> lootItems = GAI_VALUE2(std::list<uint32>, "entry loot list", -1*int32(go->GetEntry()));
                isForQuest = !lootItems.empty() || go->getLootState() != GO_READY;
            }
        }

        if (!isForQuest)
        {
            return ai->HasSkill(SKILL_HERBALISM) ? ai->CastSpell(HERB_GATHERING, bot) : false;
        }
    }

    uint32 spellId = GetOpeningSpell(lootObject);
    if (!spellId)
    {
        sLog.outDebug("[BOT LOOT] %s: GO no opening spell, abort", bot->GetName());
        return false;
    }

    if (!lootObject.IsLootPossible(bot)) //Clear loot if bot can't loot it.
    {
        sLog.outDebug("[BOT LOOT] %s: GO IsLootPossible=false, clearing", bot->GetName());
        return true;
    }

    sLog.outDebug("[BOT LOOT] %s: GO opening with spell=%u guid=%lu", bot->GetName(), spellId, lootObject.guid.GetRawValue());

    //Keys need to use the key
    if (spellId == sPlayerbotAIConfig.openGoSpell && go && lootObject.reqItem && bot->HasItemCount(lootObject.reqItem,1,false))
    {
        return ai->DoSpecificAction("use", Event("do loot", chat->formatQItem(lootObject.reqItem) + " " + chat->formatGameobject(go)));
    }

    return ai->CastSpell(spellId, bot);
}

uint32 OpenLootAction::GetOpeningSpell(LootObject& lootObject)
{
    GameObject* go = ai->GetGameObject(lootObject.guid);
    if (go && sServerFacade.isSpawned(go))
        return GetOpeningSpell(lootObject, go);

    return 0;
}

uint32 OpenLootAction::GetOpeningSpell(LootObject& lootObject, GameObject* go)
{
    for (PlayerSpellMap::iterator itr = bot->GetSpellMap().begin(); itr != bot->GetSpellMap().end(); ++itr)
    {
        uint32 spellId = itr->first;

		if (itr->second.state == PLAYERSPELL_REMOVED || itr->second.disabled || IsPassiveSpell(spellId))
			continue;

		if (spellId == MINING || spellId == HERB_GATHERING)
			continue;

		const SpellEntry* pSpellInfo = sServerFacade.LookupSpellInfo(spellId);
		if (!pSpellInfo)
			continue;

        if (CanOpenLock(lootObject, pSpellInfo, go))
            return spellId;
    }

    for (uint32 spellId = 0; spellId < sServerFacade.GetSpellInfoRows(); spellId++)
    {
        if (spellId == MINING || spellId == HERB_GATHERING)
            continue;

		const SpellEntry* pSpellInfo = sServerFacade.LookupSpellInfo(spellId);
		if (!pSpellInfo)
            continue;

        if (CanOpenLock(lootObject, pSpellInfo, go))
            return spellId;
    }

    return sPlayerbotAIConfig.openGoSpell;
}

bool OpenLootAction::CanOpenLock(LootObject& lootObject, const SpellEntry* pSpellInfo, GameObject* go)
{
    for (int effIndex = 0; effIndex <= EFFECT_INDEX_2; effIndex++)
    {
        if (pSpellInfo->Effect[effIndex] != SPELL_EFFECT_OPEN_LOCK && pSpellInfo->Effect[effIndex] != SPELL_EFFECT_SKINNING)
            return false;

        uint32 lockId = go->GetGOInfo()->GetLockId();
        if (!lockId)
            return false;

        LockEntry const *lockInfo = sLockStore.LookupEntry(lockId);
        if (!lockInfo)
            return false;

        bool reqKey = false;                                    // some locks not have reqs

        for(int j = 0; j < 8; ++j)
        {
            switch(lockInfo->Type[j])
            {
            /*
            case LOCK_KEY_ITEM:
                return true;
            */
            case LOCK_KEY_SKILL:
                {
                    if(uint32(pSpellInfo->EffectMiscValue[effIndex]) != lockInfo->Index[j])
                        continue;

                    uint32 skillId = SkillByLockType(LockType(lockInfo->Index[j]));
                    if (skillId == SKILL_NONE)
                        return true;

                    if (CanOpenLock(skillId, lockInfo->Skill[j]))
                        return true;
                }
            }
        }
    }

    return false;
}

bool OpenLootAction::CanOpenLock(uint32 skillId, uint32 reqSkillValue)
{
    uint32 skillValue = bot->GetSkillValue(skillId);
    return skillValue >= reqSkillValue || !reqSkillValue;
}

// The gathering skill a game object's lock asks for (herbalism / mining / ...), i.e. the value
// LootObject::Refresh derives when it classifies the node. Refresh only looks at an object that
// is still GO_READY, so it returns nothing for the node whose loot window is open right now.
static uint32 GetNodeGatherSkill(GameObject* go)
{
    if (!go)
        return SKILL_NONE;

    LockEntry const* lockInfo = sLockStore.LookupEntry(go->GetGOInfo()->GetLockId());
    if (!lockInfo)
        return SKILL_NONE;

    for (int i = 0; i < 8; ++i)
    {
        if (lockInfo->Type[i] != LOCK_KEY_SKILL)
            continue;

        if (uint32 skillId = SkillByLockType(LockType(lockInfo->Index[i])))
            return skillId;
    }

    return SKILL_NONE;
}

bool StoreLootAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    WorldPacket p(event.GetPacket()); // (8+1+4+1+1+4+4+4+4+4+1)
    ObjectGuid guid;
    uint8 loot_type;
    uint32 gold = 0;
    uint8 items = 0;

    p.rpos(0);
    p >> guid;      // 8 corpse guid
    p >> loot_type; // 1 loot type

    if (p.size() > 10)
    {
        p >> gold;      // 4 money on corpse
        p >> items;     // 1 number of items on corpse
    }
    else
    {
        // Not a loot response but a loot ERROR packet from Player::SendLootError:
        // [guid][uint8 loot_type=0][uint8 errorCode]. A real loot response (even an empty
        // one) always carries the gold + item-count block, so size <= 10 means the server
        // rejected the loot. Decode the trailing error byte so the rejection isn't a mystery.
        uint8 lootError = 0;
        if (p.rpos() < p.size())
            p >> lootError;

        const char* errName =
            lootError == 0 ? "DIDNT_KILL/no-permission" :
            lootError == 4 ? "TOO_FAR" : "other";
        sLog.outDebug("[BOT LOOT] %s: loot REJECTED guid=%lu error=%u (%s)",
            bot->GetName(), guid.GetRawValue(), lootError, errName);

        // Drop the corpse so the bot stops retrying a loot the server won't grant.
        AI_VALUE(LootObjectStack*, "available loot")->Remove(guid);
        RESET_AI_VALUE(LootObject, "loot target");
        return false;
    }

    sLog.outDebug("[BOT LOOT] %s: StoreLoot guid=%lu type=%u gold=%u items=%u",
        bot->GetName(), guid.GetRawValue(), loot_type, gold, items);

    // Profession loot is worth its own event: a StoreLootAction row cannot be told apart from
    // ordinary corpse loot afterwards, and skinning/gathering had no telemetry at all - the
    // "zero leather in five weeks" reading had to be dug out of item ids in loot.log. The wire
    // loot_type cannot identify a skin (#403): the core rewrites LOOT_SKINNING to LOOT_PICKPOCKETING
    // for the 1.12 client (Player::SendLoot), so every skin arrives as LOOT_PICKPOCKETING. The
    // server-side Loot still carries the real type (loot->loot_type), which is what marks a skin.
    // A node window is ordinary loot on a game object, so its skill comes from the object's lock.
    bot->SetLootGuid(guid);

    Loot* loot = sLootMgr.GetLoot(bot);

    if (!loot)
    {
        sLog.outDebug("[BOT LOOT] %s: StoreLoot GetLoot returned null, releasing to avoid crouch loop", bot->GetName());
        // Release the corpse so the bot stops the loot (kneel) animation instead of staying
        // crouched forever, and drop it from the loot stack so it isn't retried next tick.
        WorldPacket release(CMSG_LOOT_RELEASE, 8);
        release << guid;
        bot->GetSession()->HandleLootReleaseOpcode(release);
        AI_VALUE(LootObjectStack*, "available loot")->Remove(guid);
        RESET_AI_VALUE(LootObject, "loot target");
        return false;
    }

    static_assert((uint32)LOOT_SKINNING == ai::kSkinningLootType, "SkinningLootPolicy kSkinningLootType drifted from core LOOT_SKINNING");
    static_assert((uint32)SKILL_SKINNING == ai::kSkinningSkillId, "SkinningLootPolicy kSkinningSkillId drifted from core SKILL_SKINNING");
    LootType const internalLootType = LootAccess(loot).lootType();
    uint32 gatherSkill = ai::DecideGatherSkillForLoot((uint32)internalLootType, guid.IsGameObject(),
        guid.IsGameObject() ? GetNodeGatherSkill(ai->GetGameObject(guid)) : (uint32)SKILL_NONE);

    uint32 itemsTaken = 0;

    if (gold > 0)
    {
        WorldPacket packet(CMSG_LOOT_MONEY, 0);
        bot->GetSession()->HandleLootMoneyOpcode(packet);

        // Money taken before the item loop, so gold-only corpses (most low-level
        // kills) used to leave no trace: income was unmeasurable and a bot that
        // opened a corpse but took only money looked identical to one that never
        // opened it. Log the copper amount so bot_events.csv can correlate kills
        // with looting.
        sPlayerbotAIConfig.logEvent(ai, "LootMoney", std::to_string(gold), std::to_string(guid.GetEntry()));
    }

    for (uint8 i = 0; i < items; ++i)
    {
        uint32 itemid;
        uint32 randomPropertyId;
        uint32 itemcount;
        uint8 lootslot_type;
        uint8 itemindex;
        bool grab = false;

        p >> itemindex;
        p >> itemid;
        p >> itemcount;
        p.read_skip<uint32>();  // display id
        p.read_skip<uint32>();  // randomSuffix
        p >> randomPropertyId;  // randomPropertyId
        p >> lootslot_type;     // 0 = can get, 1 = look only, 2 = master get

        ItemQualifier itemQualifier(itemid, ((int32)randomPropertyId));

		if (lootslot_type != LOOT_SLOT_NORMAL
            )
		{
			sLog.outDebug("[BOT LOOT] %s: skip item=%u slot_type=%u (not normal/owner)", bot->GetName(), itemid, lootslot_type);
			continue;
		}

        if (!ai::IsSkinningLoot((uint32)internalLootType) && !IsLootAllowed(itemQualifier, ai))
        {
            sLog.outDebug("[BOT LOOT] %s: skip item=%u (IsLootAllowed=false)", bot->GetName(), itemid);
            continue;
        }

        if (AI_VALUE2(uint32, "stack space for item", itemid) < itemcount)
        {
            sLog.outDebug("[BOT LOOT] %s: skip item=%u (no stack space, need=%u)", bot->GetName(), itemid, itemcount);
            continue;
        }

        ItemPrototype const *proto = sItemStorage.LookupEntry<ItemPrototype>(itemid);
        if (!proto)
            continue;

        LootItem* lootItem = loot->LootItemInSlot(itemindex, bot->GetGUIDLow());

        if (!lootItem)
            continue;

        //have no right to loot
        if (lootItem->is_blocked || lootItem->GetSlotTypeForSharedLoot(ALL_PERMISSION, bot, loot ? loot->GetLootTarget() : nullptr) == MAX_LOOT_SLOT_TYPE)
        {
            sLog.outDebug("[BOT LOOT] %s: skip item=%u (no right: blocked=%u)", bot->GetName(), itemid, lootItem->is_blocked ? 1 : 0);
            continue;
        }

        Player* master = ai->GetMaster();
        if (sRandomBotFacade.IsRandomBot(bot) && master)
        {
            uint32 price = itemcount * ItemUsageValue::GetBotBuyPrice(proto, bot) + gold;
            if (price)
                sRandomBotFacade.AddTradeDiscount(bot, master, price);
        }

        WorldPacket packet(CMSG_AUTOSTORE_LOOT_ITEM, 1);
        packet << itemindex;
        bot->GetSession()->HandleAutostoreLootItemOpcode(packet);
        ++itemsTaken;
        sLog.outDebug("[BOT LOOT] %s: take item=%u x%u", bot->GetName(), itemid, itemcount);

        if (proto->Quality > ITEM_QUALITY_NORMAL && !urand(0, 50) && ai->HasStrategy("emote", BotState::BOT_STATE_NON_COMBAT)) ai->PlayEmote(TEXTEMOTE_CHEER);
        if (proto->Quality >= ITEM_QUALITY_RARE && !urand(0, 1) && ai->HasStrategy("emote", BotState::BOT_STATE_NON_COMBAT)) ai->PlayEmote(TEXTEMOTE_CHEER);

        if (requester && (ai->HasStrategy("debug", BotState::BOT_STATE_NON_COMBAT) || (requester->GetMapId() != bot->GetMapId() || WorldPosition(requester).sqDistance2d(bot) > (sPlayerbotAIConfig.sightDistance * sPlayerbotAIConfig.sightDistance))))
        {
            std::map<std::string, std::string> args;
            args["%item"] = chat->formatItem(itemQualifier);
            ai->TellPlayerNoFacing(requester, BOT_TEXT2("loot_command", args), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        }

        sPlayerbotAIConfig.logEvent(ai, "StoreLootAction", proto->Name1, std::to_string(proto->ItemId));

        if (gatherSkill != SKILL_NONE)
            sPlayerbotAIConfig.logEvent(ai, "GatherLoot", std::to_string(gatherSkill), std::to_string(proto->ItemId));

        BroadcastHelper::BroadcastLootingItem(ai, bot, proto, itemQualifier);
    }

    AI_VALUE(LootObjectStack*, "available loot")->Remove(guid);
    RESET_AI_VALUE(LootObject, "loot target");
    RESET_AI_VALUE2(bool, "should loot object", std::to_string(guid.GetRawValue()));

    sLog.outDebug("[BOT LOOT] %s: StoreLoot done guid=%lu items_taken=%u gold=%u, release sent",
        bot->GetName(), guid.GetRawValue(), itemsTaken, gold);

    // release loot
    WorldPacket packet(CMSG_LOOT_RELEASE, 8);
    packet << guid;
    bot->GetSession()->HandleLootReleaseOpcode(packet);

    ai->AccelerateRespawn(guid);

    return true;
}

bool StoreLootAction::IsLootAllowed(ItemQualifier& itemQualifier, PlayerbotAI *ai)
{
    AiObjectContext *context = ai->GetAiObjectContext();

    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemQualifier.GetId());
    if (!proto)
        return false;

    std::set<uint32>& lootItems = AI_VALUE(std::set<uint32>&, "always loot list");
    if (lootItems.find(itemQualifier.GetId()) != lootItems.end())
        return true;

    std::set<uint32>& skipItems = AI_VALUE(std::set<uint32>&, "skip loot list");
    if (skipItems.find(itemQualifier.GetId()) != skipItems.end())
        return false;

    uint32 max = proto->MaxCount;
    if (max > 0 && ai->GetBot()->HasItemCount(itemQualifier.GetId(), max, true))
        return false;

    if (proto->StartQuest)
    {
        if (sPlayerbotAIConfig.syncQuestWithPlayer)
            return false; //Quest is autocomplete for the bot so no item needed.
        else
            return true;
    }

    for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 entry = GetQuestSlotIdCompat(ai->GetBot(), slot);
        Quest const* quest = sObjectMgr.GetQuestTemplate(entry);
        if (!quest)
            continue;

        for (int i = 0; i < 4; i++)
        {
            if (quest->ReqItemId[i] == itemQualifier.GetId())
            {
                if (ai->GetMaster() && sPlayerbotAIConfig.syncQuestWithPlayer)
                    return false; //Quest is autocomplete for the bot so no item needed.

                if (AI_VALUE2(uint32, "item count", proto->Name1) >= quest->ReqItemCount[i])
                    return false;
            }
        }
    }

    //if (proto->Bonding == BIND_QUEST_ITEM ||  //Still testing if it works ok without these lines.
    //    proto->Bonding == BIND_QUEST_ITEM1 || //Eventually this has to be removed.
    //    proto->Class == ITEM_CLASS_QUEST)
    //{

    bool canLoot = LootStrategyValue::CanLoot(itemQualifier, ai);

    //if (canLoot && proto->Bonding == BIND_WHEN_PICKED_UP && ai->HasActivePlayerMaster())
    //    canLoot = sPlayerbotAIConfig.IsInRandomAccountList(sObjectMgr.GetPlayerAccountIdByGUID(ai->GetBot()->getObjectGuid()));

    return canLoot;
}

bool ReleaseLootAction::Execute(Event& event)
{
    std::list<ObjectGuid> gos = context->GetValue<std::list<ObjectGuid> >("nearest game objects no los")->Get();
    for (std::list<ObjectGuid>::iterator i = gos.begin(); i != gos.end(); i++)
    {
        WorldPacket packet(CMSG_LOOT_RELEASE, 8);
        packet << *i;
        bot->GetSession()->HandleLootReleaseOpcode(packet);
    }

    std::list<ObjectGuid> corpses = context->GetValue<std::list<ObjectGuid> >("nearest corpses")->Get();
    for (std::list<ObjectGuid>::iterator i = corpses.begin(); i != corpses.end(); i++)
    {
        WorldPacket packet(CMSG_LOOT_RELEASE, 8);
        packet << *i;
        bot->GetSession()->HandleLootReleaseOpcode(packet);
    }

    return true;
}

// ---------- bot-only green/blue drop boost ----------
//
// The server's Rate.Drop.Item.Uncommon/Rare apply to every player equally, so a bot-only
// boost cannot come from mangosd.conf: the module rolls the creature's own loot template
// one extra time per kill and keeps only quality-2/3 entries. It runs in the xpgain
// handler, which fires exactly once per kill and before the corpse can be opened, so the
// bonus items are part of the loot window the bot later stores.
//
// The extra roll's chance is (multiplier - 1) x the entry's DB chance, gated by the
// chance of every reference row on the path. The base roll already happened at death, so
// the expected number of greens/blues per kill is approximately multiplier x normal.
// Groups are walked entry by entry (a group's own pick consumes the base roll only) and
// no quality-2/3 row of a low-level creature uses equal-chanced (chance = 0) groups, so
// those are skipped rather than guessed at.
namespace
{
    constexpr int MAX_LOOT_BONUS_REF_DEPTH = 4;

    void BoostRareLootTemplate(LootTemplateAccess const* access, float pathChance, Player* bot,
        Loot& loot, Creature* creature, PlayerbotAI* ai, int depth);

    void BoostRareLootEntry(LootStoreItem const& entry, float pathChance, Player* bot,
        Loot& loot, Creature* creature, PlayerbotAI* ai, int depth)
    {
        if (entry.mincountOrRef < 0)                            // reference row: gate the subtree by its own chance
        {
            LootTemplate const* referenced = LootTemplates_Reference.GetLootFor(uint32(-entry.mincountOrRef));
            if (!referenced)
                return;

            float refChance = entry.chance >= 100.0f ? 1.0f : entry.chance / 100.0f;
            BoostRareLootTemplate(reinterpret_cast<LootTemplateAccess const*>(referenced),
                pathChance * refChance, bot, loot, creature, ai, depth + 1);
            return;
        }

        if (entry.mincountOrRef <= 0 || entry.chance <= 0.0f || entry.needs_quest)
            return;

        ItemPrototype const* proto = sObjectMgr.GetItemPrototype(entry.itemid);
        if (!proto || (proto->Quality != ITEM_QUALITY_UNCOMMON && proto->Quality != ITEM_QUALITY_RARE))
            return;

        // Quest rules: a quest-only drop or quest-starting item belongs to the quest system,
        // never to a drop-rate boost. Unique rules: adding something the bot already holds at
        // its max_count would only leave an item it can never pick up.
        if (proto->Class == ITEM_CLASS_QUEST || proto->StartQuest)
            return;

        if (proto->MaxCount > 0 && bot->HasItemCount(entry.itemid, proto->MaxCount, true))
            return;

        float rate = proto->Quality == ITEM_QUALITY_UNCOMMON
            ? sPlayerbotAIConfig.botLootRateUncommon
            : sPlayerbotAIConfig.botLootRateRare;

        if (rate <= 1.0f)
            return;

        // Stack on the server's own quality rate so the multiplier means the same thing
        // whichever Rate.Drop.Item.* mangosd.conf ships.
        float serverRate = LootTemplates_Creature.IsRatesAllowed()
            ? sWorld.getConfig(proto->Quality == ITEM_QUALITY_UNCOMMON
                ? CONFIG_FLOAT_RATE_DROP_ITEM_UNCOMMON : CONFIG_FLOAT_RATE_DROP_ITEM_RARE)
            : 1.0f;

        float chance = entry.chance * pathChance * serverRate * (rate - 1.0f);
        if (chance > 100.0f)
            chance = 100.0f;

        if (chance <= 0.0f || !roll_chance_f(chance))
            return;

        loot.AddItem(entry);

        sPlayerbotAIConfig.logEvent(ai, "BotLootBonus", creature->GetObjectGuid(),
            std::to_string(proto->Quality) + ":" + std::to_string(proto->ItemId) + ":" + proto->Name1);
    }

    void BoostRareLootTemplate(LootTemplateAccess const* access, float pathChance, Player* bot,
        Loot& loot, Creature* creature, PlayerbotAI* ai, int depth)
    {
        if (!access || depth > MAX_LOOT_BONUS_REF_DEPTH)
            return;

        for (LootStoreItem const& entry : access->Entries)
            BoostRareLootEntry(entry, pathChance, bot, loot, creature, ai, depth);

        for (LootLootGroupAccess const& group : access->Groups)
        {
            for (LootStoreItem const& entry : group.ExplicitlyChanced)
                BoostRareLootEntry(entry, pathChance, bot, loot, creature, ai, depth);

            for (LootStoreItem const& entry : group.EqualChanced)
                BoostRareLootEntry(entry, pathChance, bot, loot, creature, ai, depth);
        }
    }
}

void ai::ApplyBotLootBonus(PlayerbotAI* ai, Player* bot, ObjectGuid victimGuid)
{
    if (sPlayerbotAIConfig.botLootRateUncommon <= 1.0f && sPlayerbotAIConfig.botLootRateRare <= 1.0f)
        return;

    // Masterless pool bots only: a player character or a hired/alt bot keeps the server's
    // rates, so the boost can never leak into a real player's economy.
    if (!sRandomBotFacade.IsRandomBot(bot))
        return;

    Creature* creature = ai->GetCreature(victimGuid);
    if (!creature || creature->IsAlive())
        return;

    // Personal loot of this bot's own kill. m_personal is false whenever the core rolled the
    // corpse for a group, which is exactly the "loot shared with a real player" case.
    Loot& loot = creature->loot;
    if (!loot.m_personal || creature->GetLootRecipient() != bot)
        return;

    uint32 lootId = creature->GetLootId();
    if (!lootId)
        return;

    LootTemplate const* lootTemplate = LootTemplates_Creature.GetLootFor(lootId);
    if (!lootTemplate)
        return;

    BoostRareLootTemplate(reinterpret_cast<LootTemplateAccess const*>(lootTemplate),
        1.0f, bot, loot, creature, ai, 0);
}
