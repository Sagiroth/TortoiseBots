
#include "playerbot/playerbot.h"
#include "LootRollAction.h"
#include "../../policy/loot/LootRollPolicy.h"
#include "playerbot/strategy/values/ItemUsageValue.h"
#include "playerbot/RandomItemMgr.h"
#include "playerbot/strategy/values/LootValues.h"
#include "../../../runtime/HireLifecycle.h"
#include "../../../runtime/policy/hire/HireLootPolicy.h"
#include "../../../../runtime/AltBotService.h"
#include "../../../../runtime/AltBotPolicy.h"
#include "Group/Group.h"
#include "Maps/Map.h"
using namespace ai;

namespace
{
    std::string RollVoteName(RollVote vote)
    {
        switch (vote)
        {
        case ROLL_NEED:
            return "need";
        case ROLL_GREED:
            return "greed";
        case ROLL_DISENCHANT:
            return "disenchant";
        default:
            return "pass";
        }
    }

    void LogLootRoll(PlayerbotAI* ai, ItemQualifier& itemQualifier, RollVote vote, std::string const& reason)
    {
        if (!sPlayerbotAIConfig.hasLog("bot_events.csv"))
            return;

        ItemPrototype const* proto = itemQualifier.GetProto();
        std::string itemName = proto ? proto->Name1 : "unknown item";
        sPlayerbotAIConfig.logEvent(ai, "LootRoll", itemName,
            RollVoteName(vote) + ":" + std::to_string(itemQualifier.GetId()) + ":" + reason);
    }
}

bool LootStartRollAction::Execute(Event& event)
{
    WorldPacket p(event.GetPacket()); //WorldPacket packet for CMSG_LOOT_ROLL, (8+4+1)
    ObjectGuid creatureGuid;
    uint32 itemSlot;
    uint32 itemId;
    uint32 randomSuffix;
    int32 randomPropertyId;
    uint32 timeout;

    p.rpos(0); //reset packet pointer
    p >> creatureGuid; //creature guid what we're looting
    p >> itemSlot; // the itemEntryId for the item that shall be rolled for
    p >> itemId; // the itemEntryId for the item that shall be rolled for
    p >> randomSuffix; // randomSuffix
    p >> randomPropertyId; // item random property ID
    p >> timeout;  // the countdown time to choose "need" or "greed"

    LootRollMap lootRolls = AI_VALUE(LootRollMap, "active rolls");

    if (!bot->GetGroup())
        return false;

    // One packet is sent per item, so remember exactly this slot. The old
    // code walked every loot slot through Loot::GetRollForSlot and found
    // nothing, and bailed out entirely once the creature was known - which
    // would have dropped the second item of any corpse anyway.
    bool known = false;
    auto range = lootRolls.equal_range(creatureGuid);
    for (auto itr = range.first; itr != range.second; ++itr)
    {
        if (itr->second == itemSlot)
        {
            known = true;
            break;
        }
    }

    // Prune vote sightings for rolls that already resolved (#967): match the
    // exact (guid, slot) pair against the pre-insert state, so a stale entry
    // for this same pair (same mob respawned, same slot rolling again) can
    // never demote the fresh roll. Runs BEFORE the insert for that reason.
    LootVoteMap seenVotes = AI_VALUE(LootVoteMap, "seen need rolls");
    for (auto itr = seenVotes.begin(); itr != seenVotes.end();)
    {
        bool alive = false;
        auto range = lootRolls.equal_range(std::get<0>(itr->first));
        for (auto roll = range.first; roll != range.second; ++roll)
        {
            if (roll->second == std::get<1>(itr->first))
            {
                alive = true;
                break;
            }
        }
        if (!alive)
            itr = seenVotes.erase(itr);
        else
            ++itr;
    }
    SET_AI_VALUE(LootVoteMap, "seen need rolls", seenVotes);

    if (!known)
        lootRolls.insert({ creatureGuid, itemSlot });

    ActiveRolls::CleanUp(bot, lootRolls);

    SET_AI_VALUE(LootRollMap, "active rolls", lootRolls);

    return false;
}

bool LootVoteAction::Execute(Event& event)
{
    WorldPacket p(event.GetPacket()); //WorldPacket packet for SMSG_LOOT_ROLL, (8+4+8+4+4+4+1+1)
    ObjectGuid lootGuid;
    uint32 itemSlot;
    ObjectGuid voterGuid;
    uint32 itemId;
    p.rpos(0); //reset packet pointer
    p >> lootGuid; //creature guid what we're looting
    p >> itemSlot; // the loot slot rolled for
    p >> voterGuid; // who voted
    p >> itemId; // the itemEntryId voted on
    p.read_skip<uint32>(); // randomSuffix, unused
    uint32 randomPropertyId = 0;
    p >> randomPropertyId;
    uint8 rollNumber = 0;
    p >> rollNumber;
    uint8 rollType = ROLL_PASS;
    p >> rollType;

    // Core wire format (Group::CountRollVote): a live vote is (number 0,
    // type 0) for NEED, (128, 2) for GREED, (128, 128) for PASS. Anything
    // else carries a real roll number and is a post-resolution echo
    // (CountTheRoll) - the roll is already decided, ignore it.
    if (ai::LootVoteIsEcho(rollNumber, rollType))
        return false;
    ai::LootRollVote liveVote = ai::LootWireVote(rollNumber, rollType);

    if (voterGuid == bot->getObjectGuid())
        return false;

    Player* voter = sObjectMgr.GetPlayer(voterGuid);
    if (!voter || !ai->IsRealPlayer(voter))
        return false;

    // Only while the bot is still holding this roll: late duplicates and
    // echoes for an already-voted slot never land.
    LootRollMap lootRolls = AI_VALUE(LootRollMap, "active rolls");
    bool active = false;
    auto range = lootRolls.equal_range(lootGuid);
    for (auto roll = range.first; roll != range.second; ++roll)
    {
        if (roll->second == itemSlot)
        {
            active = true;
            break;
        }
    }
    if (!active)
        return false;

    LootVoteMap seenVotes = AI_VALUE(LootVoteMap, "seen need rolls");
    seenVotes[{ lootGuid, itemSlot, voterGuid }] = (uint8)liveVote;
    SET_AI_VALUE(LootVoteMap, "seen need rolls", seenVotes);

    return true;
}

bool RollAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    std::string text = event.GetParam();

    if (text.empty())
    {
        ai->TellPlayerNoFacing(requester, "Please give a roll type or item. See " + ChatHelper::formatValue("help", "action:roll", "roll help") + " for more information.");
        return false;
    }

    ItemIds ids = ChatHelper::parseItems(text);

    std::string type = "auto";
    if (ids.empty())
        type = text;
    else
        type = text.substr(0, text.find(" "));

    if (type == "emote")
    {
        std::vector<std::string> args = ChatHelper::splitString(text, " ");

        if (args.size() == 2)
            args = { args[0], "1", args[1] };
        if (args.size() == 1)
            args = { args[0], "1", "100" };

        for (char& d : args[1]) //Check if itemId contains only numbers
            if (!isdigit(d))
                return false;

        for (char& d : args[2]) //Check if itemId contains only numbers
            if (!isdigit(d))
                return false;

        WorldPacket data(MSG_RANDOM_ROLL);
        data << stoi(args[1]);
        data << stoi(args[2]);
        bot->GetSession()->HandleRandomRollOpcode(data);

        return true;
    }

    bool rollFeedback = AI_VALUE2(bool, "manual bool", "roll feedback");

    if (type == "feedback")
    {
        rollFeedback = !rollFeedback;

        if (!rollFeedback)
            ai->TellPlayerNoFacing(requester, "Roll feedback disabled.");
        else
            ai->TellPlayerNoFacing(requester, "Roll feedback enabled.");

        SET_AI_VALUE2(bool, "manual bool", "roll feedback", rollFeedback);

        return true;
    }

    if (!bot->GetGroup())
        return false;

    if (AI_VALUE(LootRollMap, "active rolls").empty())
        return false;

    if (AI_VALUE(uint8, "bag space") >= 100)
        return false;

    if (type != "need" && type != "greed" && type != "pass" && type != "auto")
    {
        ai->TellPlayerNoFacing(requester, "Please give a correct roll type. need, greed, pass or auto. See " + ChatHelper::formatValue("help", "action:roll", "roll help") + " for more information.");
        return false;
    }

    RollVote vote = ROLL_NOT_VALID;

    if (type.find("need") == 0)
        vote = ROLL_NEED;
    else if (type.find("greed") == 0)
        vote = ROLL_GREED;
    else if (type.find("pass") == 0)
        vote = ROLL_PASS;

    uint32 rolledItems = 0;

    LootRollMap lootRolls = AI_VALUE(LootRollMap, "active rolls");

    for (auto roll : lootRolls)
    {
        ItemQualifier itemQualifier = GetRollItem(roll.first, roll.second);

        if (!itemQualifier.GetId())
            continue;

        if (!ids.empty() && ids.find(itemQualifier.GetId()) == ids.end())
            continue;

        RollVote doVote = vote;
        if (doVote == ROLL_NOT_VALID) //Auto
            doVote = CalculateRollVote(itemQualifier, nullptr, nullptr, true);
        else
        {
            std::string orderReason = std::string("ordered by ") + (requester ? requester->GetName() : "?");
            if (!StoreLootAction::IsLootAllowed(itemQualifier, PlayerbotAIStorage::Instance().GetAI(bot)))
                orderReason += " (can not be looted)";
            LogLootRoll(ai, itemQualifier, doVote, orderReason);
        }

        rolledItems += RollOnItemInSlot(doVote, roll.first, roll.second);
    }

    return rolledItems;
}

ItemQualifier RollAction::GetRollItem(ObjectGuid lootGuid, uint32 slot)
{
    if (Loot* loot = sLootMgr.GetLoot(bot, lootGuid))
        if (LootItem* item = loot->LootItemInSlot(slot, bot->GetGUIDLow()))
            return ItemQualifier(item);

    // The core keeps active roll state private; do not infer an item or vote.
    return ItemQualifier();
}

RollVote RollAction::CalculateRollVote(ItemQualifier& itemQualifier, RollVote* finalVote, std::string* reasonOut, bool log)
{
    ItemUsage usage = AI_VALUE2(ItemUsage, "item usage", itemQualifier.GetQualifier());

    RollVote needVote = ROLL_PASS;
    switch (usage)
    {
    case ItemUsage::ITEM_USAGE_EQUIP:
    case ItemUsage::ITEM_USAGE_GUILD_TASK:
    case ItemUsage::ITEM_USAGE_FORCE_NEED:
        needVote = ROLL_NEED;
        break;
    case ItemUsage::ITEM_USAGE_SKILL:
    case ItemUsage::ITEM_USAGE_USE:
    case ItemUsage::ITEM_USAGE_AH:
    case ItemUsage::ITEM_USAGE_BROKEN_AH:
    case ItemUsage::ITEM_USAGE_VENDOR:
    case ItemUsage::ITEM_USAGE_FORCE_GREED:
        needVote = ROLL_GREED;
        break;
    case ItemUsage::ITEM_USAGE_DISENCHANT:
        needVote = ROLL_GREED;
        break;
    }

    // special case for bad equip
    if (usage == ItemUsage::ITEM_USAGE_BAD_EQUIP)
    {
        bool shouldEquipBadItems = sPlayerbotAIConfig.rollBadItemsWithPlayer || !ai->HasRealPlayerMaster();
        if (shouldEquipBadItems)
            needVote = ROLL_NEED;
        else
            needVote = ROLL_GREED;
    }
    Group* group = bot->GetGroup();
    Map* map = bot->GetMap();
    const ItemPrototype* itemProto = itemQualifier.GetProto();
    bool sharedLoot = group && (group->GetLootMethod() == GROUP_LOOT || group->GetLootMethod() == NEED_BEFORE_GREED);
    bool groupLootContext = sharedLoot || (map && (map->IsDungeon() || map->IsRaid()));
    uint32 specId = sRandomItemMgr.GetPlayerSpecId(bot);
    if (!specId)
        specId = sRandomItemMgr.GetFallbackSpecId(bot->GetClass());

    if (usage != ItemUsage::ITEM_USAGE_FORCE_NEED &&
        itemProto && itemProto->Class == ITEM_CLASS_ARMOR &&
        groupLootContext &&
        !sRandomItemMgr.ShouldEquipArmorForSpec(bot->GetClass(), specId, itemProto))
    {
        needVote = ROLL_GREED;
    }
    // Issue #189 Phase 2: roll weapons the same way the bot would wear them.
    if (usage != ItemUsage::ITEM_USAGE_FORCE_NEED &&
        itemProto && itemProto->Class == ITEM_CLASS_WEAPON &&
        groupLootContext &&
        !sRandomItemMgr.ShouldEquipWeaponForSpec(bot->GetClass(), specId, itemProto, bot->CanDualWield()))
    {
        needVote = ROLL_GREED;
    }

    // Donor-parity vote gates (mod-playerbots LootRollAction::Execute,
    // CanBotUseToken, RollUniqueCheck; E08). All three apply only to the
    // plain auto vote: an explicit FORCE_NEED / FORCE_GREED from the
    // player (or the bad-equip rule above) is a direct order and wins.
    // Pool bots only for recipe/token (no competing player rolls against
    // them); the duplicate-unique demotion applies to every bot because
    // the core refuses a second unique copy anyway.
    bool poolRoll = !ai->HasRealPlayerMaster();
    if (itemProto && usage != ItemUsage::ITEM_USAGE_FORCE_NEED &&
        usage != ItemUsage::ITEM_USAGE_FORCE_GREED)
    {
        Group* rollGroup = bot->GetGroup();
        if (rollGroup && !ai::LootMethodTakesRolls((int)rollGroup->GetLootMethod()))
        {
            std::string lootMethodReason = "because the group's loot method takes no rolls.";
            if (finalVote)
                *finalVote = ROLL_PASS;
            if (reasonOut)
                *reasonOut = lootMethodReason;
            if (log)
                LogLootRoll(ai, itemQualifier, ROLL_PASS, lootMethodReason);
            return ROLL_PASS;
        }

        // Recipe gate: SKILL usage on a recipe is already the learned-state
        // verdict (ItemUsageValue: known recipes fall to KEEP/NONE, unknown
        // + usable ones read SKILL) - NEED it when learnable, PASS a
        // soulbound one it cannot use, GREED a tradeable one for the AH.
        if (poolRoll && itemProto->Class == ITEM_CLASS_RECIPE)
        {
            bool canLearn = usage == ItemUsage::ITEM_USAGE_SKILL;
            if (usage == ItemUsage::ITEM_USAGE_SKILL || usage == ItemUsage::ITEM_USAGE_KEEP ||
                usage == ItemUsage::ITEM_USAGE_NONE)
            {
                ai::LootRollVote recipeVote = ai::RecipeRollVote(canLearn,
                    itemProto->Bonding == BIND_WHEN_PICKED_UP);
                needVote = recipeVote == ai::LootRollVote::Need ? ROLL_NEED :
                    recipeVote == ai::LootRollVote::Greed ? ROLL_GREED : ROLL_PASS;
            }
        }
        // Class-token gate: epic junk-class tokens are NEEDed only by a
        // class in their AllowableClass mask (donor CanBotUseToken; empty
        // mask is unrestricted).
        else if (poolRoll && itemProto->Class == ITEM_CLASS_JUNK &&
            itemProto->Quality == ITEM_QUALITY_EPIC)
        {
            needVote = ai::TokenUsableByClass(itemProto->AllowableClass, bot->GetClassMask()) ?
                ROLL_NEED : ROLL_GREED;
        }

        // Duplicate-unique gate (donor RollUniqueCheck at the default
        // NEED-becomes-GREED level): a UNIQUE_EQUIPPED item already worn,
        // or a stack already at MaxCount, cannot be looted again.
        if (needVote == ROLL_NEED && itemProto->MaxCount > 0 &&
            bot->GetItemCount(itemProto->ItemId, false) >= itemProto->MaxCount)
            needVote = ROLL_GREED;
        if (needVote == ROLL_NEED && (itemProto->Flags & ITEM_FLAG_UNIQUE_EQUIPPED))
        {
            bool equipped = false;
            for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
                if (Item* worn = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                    if (worn->GetProto()->ItemId == itemProto->ItemId)
                    {
                        equipped = true;
                        break;
                    }
            if (equipped && ai::UniqueCopyOwned(true, true, 1, 1))
                needVote = ROLL_GREED;
        }
    }


    bool canLoot = StoreLootAction::IsLootAllowed(itemQualifier, PlayerbotAIStorage::Instance().GetAI(bot));

    std::string reason = "because it can not be looted.";
    if (canLoot)
        reason = ItemUsageValue::ReasonForNeed(usage, itemQualifier, 1, bot);

    if (AI_VALUE2(bool, "manual bool", "roll feedback"))
    {
        std::string voteName = "Passing";
        if (needVote == ROLL_GREED)
            voteName = "Rolling greed";
        else if (needVote == ROLL_NEED)
            voteName = "Rolling need";
        else if (needVote == ROLL_DISENCHANT)
            voteName = "Rolling disenchant";

        ai->TellPlayerNoFacing(ai->GetMaster(), voteName + " on " + ChatHelper::formatItem(itemQualifier) + " " + reason);
    }

    // Issue #872: an autonomous alt rolls Greed or Pass, never Need (C).
    if (needVote == ROLL_NEED &&
        TortoiseBots::AltBotService::Instance().IsAutonomous(bot->GetObjectGuid()))
        needVote = ROLL_GREED;

    RollVote decision = canLoot ? needVote : ROLL_PASS;
    if (finalVote)
        *finalVote = decision;
    if (reasonOut)
        *reasonOut = reason;
    if (log)
        LogLootRoll(ai, itemQualifier, decision, reason);

    return decision;
}

bool RollAction::RollOnItemInSlot(RollVote vote, ObjectGuid lootGuid, uint32 slot)
{
    Group* group = bot->GetGroup();
    if (!group)
        return false;

    if (vote != ROLL_NEED && vote != ROLL_GREED && vote != ROLL_PASS)
        vote = ROLL_PASS;

    // Same entry point the client uses via CMSG_LOOT_ROLL. The core checks
    // eligibility and resolves the roll once everyone has voted.
    if (!group->CountRollVote(bot, lootGuid, slot, vote))
        return false;

    LootRollMap lootRolls = AI_VALUE(LootRollMap, "active rolls");

    ActiveRolls::CleanUp(bot, lootRolls, lootGuid, slot);

    SET_AI_VALUE(LootRollMap, "active rolls", lootRolls);

    // The bot voted: its decision for this slot is final, so drop the vote
    // sightings with it. A later roll on the same pair starts untainted.
    LootVoteMap seenVotes = AI_VALUE(LootVoteMap, "seen need rolls");
    for (auto itr = seenVotes.begin(); itr != seenVotes.end();)
    {
        if (std::get<0>(itr->first) == lootGuid && std::get<1>(itr->first) == slot)
            itr = seenVotes.erase(itr);
        else
            ++itr;
    }
    SET_AI_VALUE(LootVoteMap, "seen need rolls", seenVotes);

    return true;
}

// Would rolling need on this item take it from a real player? Every real
// player in the group who is in this roll counts (same map, inside the
// core's loot distance of the corpse). seenVoter/seenVote carry the one
// vote the bot knows: its owner's own CMSG_LOOT_ROLL that woke "delayed
// roll". The core keeps the other votes private, so an unseen player's bare
// "can use it" only competes for a hired companion grouped with its master
// (#967): alts wait like pool bots instead.
bool RollAction::RealPlayerMayNeed(ObjectGuid lootGuid, ItemQualifier& itemQualifier, ObjectGuid seenVoter, uint8 seenVote)
{
    const ItemPrototype* proto = itemQualifier.GetProto();
    if (!proto || !bot->GetGroup() || !bot->GetMap())
        return false;

    Creature* lootTarget = bot->GetMap()->GetCreature(lootGuid);

    bool const yieldUnseenUsable = TortoiseBots::HireLifecycle::Instance().IsHired(bot->GetObjectGuid()) &&
        ai->HasRealPlayerMaster();
    for (Player* member : ai->GetPlayersInGroup())
    {
        if (member == bot || !member->IsInWorld() || member->GetMapId() != bot->GetMapId())
            continue;

        if (lootTarget && !member->IsWithinLootXPDist(lootTarget))
            continue;

        bool voteSeen = !seenVoter.IsEmpty() && member->GetObjectGuid() == seenVoter && seenVote < MAX_ROLL_FROM_CLIENT;
        ai::LootRollVote vote = voteSeen ? ai::LootRollVote(seenVote) : ai::LootRollVote::Pass;
        bool canUse = member->CanUseItem(proto) == EQUIP_ERR_OK;

        if (ai::RealPlayerCompetes(voteSeen, vote, canUse, yieldUnseenUsable))
            return true;
    }

    return false;
}

// True while this bot must hold its vote on this roll: a pool bot (or alt)
// in a group with real players waits so the humans get the whole window,
// voting ~15 s before it ends or as soon as every real roller has voted
// (#967). Hired companions grouped with their master never wait (they keep
// the old yield-on-usable behaviour); bot-only groups vote at once.
bool RollAction::ShouldWaitForPlayers(ObjectGuid lootGuid, uint32 slot)
{
    Group* group = bot->GetGroup();
    if (!group || !bot->GetMap())
        return false;

    if (TortoiseBots::HireLifecycle::Instance().IsHired(bot->GetObjectGuid()) && ai->HasRealPlayerMaster())
        return false;

    bool anyRealPlayer = false;
    Creature* lootTarget = bot->GetMap()->GetCreature(lootGuid);
    for (Player* member : ai->GetPlayersInGroup())
    {
        if (member == bot || !member->IsInWorld() || member->GetMapId() != bot->GetMapId())
            continue;

        if (lootTarget && !member->IsWithinLootXPDist(lootTarget))
            continue;

        anyRealPlayer = true;
        break;
    }

    if (!anyRealPlayer)
        return false;

    // A NEED the bot already saw ends the wait on its own: the vote is
    // decided (step down to GREED), so cast it now.
    LootVoteMap seenVotes = AI_VALUE(LootVoteMap, "seen need rolls");
    bool seenNeed = false;
    for (auto const& entry : seenVotes)
    {
        if (std::get<0>(entry.first) == lootGuid && std::get<1>(entry.first) == slot &&
            (ai::LootRollVote)entry.second == ai::LootRollVote::Need)
        {
            seenNeed = true;
            break;
        }
    }
    if (seenNeed)
        return false;

    // The core counts down from LOOT_ROLL_TIMEOUT (60 s); a zero timer means
    // the window already resolved - vote now rather than hold forever.
    uint32 remainingMs = lootTarget ? lootTarget->GetGroupLootTimer() : 0;
    if (!remainingMs)
        return false;

    return !ai::RollWaitElapsed(remainingMs, AllRealRollersVoted(lootGuid, slot));
}
// True when every real player in this roll already voted: each eligible
// roller (same map, loot distance, CanUseItem OK, AllowedForPlayer) has a
// recorded live vote for this (guid, slot) - need, greed or pass. The
// recorder only stores packets for active rolls, so an entry here is fresh
// by construction. Cheap on purpose: one short member walk over the group
// (usually 5), no world scan, no DB.
bool RollAction::AllRealRollersVoted(ObjectGuid lootGuid, uint32 slot)
{
    ItemQualifier itemQualifier = GetRollItem(lootGuid, slot);
    const ItemPrototype* proto = itemQualifier.GetProto();
    if (!proto)
        return false;

    Loot* loot = sLootMgr.GetLoot(bot, lootGuid);
    LootItem* item = (loot && slot < loot->items.size()) ? &loot->items[slot] : nullptr;

    Creature* lootTarget = bot->GetMap() ? bot->GetMap()->GetCreature(lootGuid) : nullptr;
    LootVoteMap seenVotes = AI_VALUE(LootVoteMap, "seen need rolls");

    for (Player* member : ai->GetPlayersInGroup())
    {
        if (member == bot || !member->IsInWorld() || member->GetMapId() != bot->GetMapId())
            continue;

        if (lootTarget && !member->IsWithinLootXPDist(lootTarget))
            continue;

        if (member->CanUseItem(proto) != EQUIP_ERR_OK)
            continue;

        if (item && !item->AllowedForPlayer(member, lootTarget))
            continue;

        if (seenVotes.find({ lootGuid, slot, member->GetObjectGuid() }) == seenVotes.end())
            return false;
    }

    return true;
}

// Vote on one item the polite way: never roll need against a real player.
bool RollAction::RollWithEtiquette(ObjectGuid lootGuid, uint32 slot, ObjectGuid seenVoter, uint8 seenVote)
{
    ItemQualifier itemQualifier = GetRollItem(lootGuid, slot);
    if (!itemQualifier.GetId())
        return false;

    if (!TortoiseBots::HireMayLoot(TortoiseBots::HireLifecycle::Instance().IsHired(bot->GetObjectGuid()),
            sPlayerbotAIConfig.hireLoot))
    {
        LogLootRoll(ai, itemQualifier, ROLL_PASS, "because hired companion loot is off.");
        return RollOnItemInSlot(ROLL_PASS, lootGuid, slot);
    }

    std::string reason;
    RollVote vote = CalculateRollVote(itemQualifier, nullptr, &reason, false);

    if (vote == ROLL_NEED)
    {
        // A FORCE_NEED usage is the player's own order and still needs.
        bool forcedNeed = AI_VALUE2(ItemUsage, "item usage", itemQualifier.GetQualifier()) == ItemUsage::ITEM_USAGE_FORCE_NEED;
        bool seenNeed = false;
        LootVoteMap seen = AI_VALUE(LootVoteMap, "seen need rolls");
        for (auto const& entry : seen)
        {
            if (std::get<0>(entry.first) == lootGuid && std::get<1>(entry.first) == slot &&
                (ai::LootRollVote)entry.second == ai::LootRollVote::Need)
            {
                seenNeed = true;
                break;
            }
        }
        bool competes = !forcedNeed && (RealPlayerMayNeed(lootGuid, itemQualifier, seenVoter, seenVote) || seenNeed);
        if (ai::PoliteRollVote(ai::LootRollVote::Need, competes, forcedNeed) == ai::LootRollVote::Greed)
        {
            vote = ROLL_GREED;

            if (AI_VALUE2(bool, "manual bool", "roll feedback"))
                ai->TellPlayerNoFacing(ai->GetMaster(), "Rolling greed on " + ChatHelper::formatItem(itemQualifier) + " instead: a player in the group may need it.");

            LogLootRoll(ai, itemQualifier, vote, reason + " Stepped down to greed: a player in the group may need it.");
            return RollOnItemInSlot(vote, lootGuid, slot);
        }
    }

    LogLootRoll(ai, itemQualifier, vote, reason);

    return RollOnItemInSlot(vote, lootGuid, slot);
}

bool LootRollAction::Execute(Event& event)
{
    Player* bot = QueryItemUsageAction::ai->GetBot();

    WorldPacket p(event.GetPacket()); //WorldPacket packet for CMSG_LOOT_ROLL, (8+4+1)
    ObjectGuid guid;
    uint32 slot;
    uint8 rollType;
    p.rpos(0); //reset packet pointer
    p >> guid; //guid of the item rolled
    p >> slot; //number of players invited to roll
    p >> rollType; //need,greed or pass on roll

    // The packet is the owner's own vote: when they passed or greeded, the
    // bot may need without competing with them.
    Player* voter = GetMaster();
    return RollWithEtiquette(guid, slot, voter ? voter->GetObjectGuid() : ObjectGuid(), rollType);
}

bool AutoLootRollAction::Execute(Event& event)
{
    LootRollMap lootRolls = AI_VALUE(LootRollMap, "active rolls");
    if (lootRolls.empty())
        return false;

    // Vote every roll whose wait elapsed in this tick: waiting synchronises
    // votes near the deadline, so one random roll per tick would drain N
    // simultaneous rolls over ~N ticks (#967 S1). Rolls still waiting stay
    // in "active rolls" for the next tick.
    uint32 voted = 0;
    for (auto const& roll : lootRolls)
    {
        if (ShouldWaitForPlayers(roll.first, roll.second))
            continue;

        if (RollWithEtiquette(roll.first, roll.second))
            ++voted;
    }

    return voted > 0;
}

bool AutoLootRollAction::isPossible()
{
    return bot->GetGroup() && !AI_VALUE(LootRollMap, "active rolls").empty() && AI_VALUE(uint8, "bag space") < 100;
}

bool RollWonLogAction::Execute(Event& event)
{
    // SMSG_LOOT_ROLL_WON layout (Group::SendLootRollWon): [looted guid][slot]
    // [item id][randomSuffix:0][randomPropertyId][winner guid][roll][type].
    // It reaches every roller, so only the winner logs; the core also stores
    // the item in the winner's bags before this packet goes out (Group::
    // CountTheRoll), so no re-fetch or ownership check is needed here.
    WorldPacket p(event.GetPacket());
    p.rpos(0);
    ObjectGuid lootGuid;
    uint32 slot;
    uint32 itemId;
    p >> lootGuid;
    p >> slot;
    p >> itemId;
    p.read_skip<uint32>(); // randomSuffix, unused
    uint32 randomPropertyId = 0;
    p >> randomPropertyId;
    ObjectGuid winner;
    p >> winner;

    if (winner != bot->getObjectGuid())
        return false;

    ItemPrototype const* proto = sItemStorage.LookupEntry<ItemPrototype>(itemId);
    if (!proto)
        return false;

    ItemQualifier itemQualifier(itemId, (int32)randomPropertyId);
    sPlayerbotAIConfig.logEvent(ai, "RollWon", proto->Name1, std::to_string(itemId));
    return true;
}
