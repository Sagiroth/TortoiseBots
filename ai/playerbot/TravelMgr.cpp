#include "playerbot/TravelMgr.h"
#include "playerbot/TravelRoutePolicy.h"
#include "playerbot/GrindSpotPolicy.h"
#include "playerbot/PullRegenPolicy.h"
#include "playerbot/PointDangerPolicy.h"
// Zone migration exclusion for the leave-outgrown-zone grind errand.
// Pure travel re-pick decisions (cooldown park, kind give-up, trap streak).
#include "playerbot/ZoneMigratePolicy.h"
#include "playerbot/TravelRepickPolicy.h"
#include "playerbot/QuestGiverStallPolicy.h"
#include "playerbot/WorkIdlePolicy.h"
#include <numeric>
#include <mutex>
#include <iomanip>

#include "playerbot/strategy/values/SharedValueContext.h"
#include "playerbot/strategy/values/TravelValues.h"
#include "Maps/PathFinder.h"
#include "TravelNode.h"
#include "PlayerbotAI.h"
#include "playerbot/RandomBotFacade.h"
#include "ObjectAccessor.h"
#include "Formulas.h"

using namespace ai;
using namespace MaNGOS;

// Penqle's Singleton<> requires an explicit instantiation in a .cpp file.
INSTANTIATE_SINGLETON_1(ai::TravelMgr);

PlayerTravelInfo::PlayerTravelInfo(Player* player)
{
    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(player);
    AiObjectContext* context = ai->GetAiObjectContext();

    position = player;
    homebind = WorldPosition(player->GetHomeBindLocation());

    team = player->GetTeam();
    level = player->GetLevel();
    identitySeed = player->GetGUIDLow();
    if (Group* group = player->GetGroup())
        identitySeed = group->GetLeaderGuid().GetCounter();
    currentSkill[SKILL_MINING] = player->GetSkillValue(SKILL_MINING);
    currentSkill[SKILL_HERBALISM] = player->GetSkillValue(SKILL_HERBALISM);
    currentSkill[SKILL_FISHING] = player->GetSkillValue(SKILL_FISHING);
    currentSkill[SKILL_SKINNING] = player->GetSkillValue(SKILL_SKINNING);

    skillMax[SKILL_MINING] = player->GetSkillMax(SKILL_MINING);
    skillMax[SKILL_HERBALISM] = player->GetSkillMax(SKILL_HERBALISM);
    skillMax[SKILL_FISHING] = player->GetSkillMax(SKILL_FISHING);
    skillMax[SKILL_SKINNING] = player->GetSkillMax(SKILL_SKINNING);

    money = player->GetMoney();

    if (player->GetGroup())
        groupSize = player->GetGroup()->GetMembersCount();

    // Same scope as ShouldLeaveOutgrownZoneValue: autonomous random bots only.
    // Snapshot here so destination filtering (async, off-tick) cannot block an
    // owned/hired bot from vendoring or repairing next to its player.
    masterlessRandom = sRandomBotFacade.IsRandomBot(player) && !ai->HasRealPlayerMaster();
    focusList = AI_VALUE(focusQuestTravelList, "focus travel target");

    for (auto& [valueName, value] : boolValues)
        value = AI_VALUE(bool, valueName);

    for (auto& [valueName, value] : uint8Values)
        value = AI_VALUE(uint8, valueName);

    for (auto& [valueName, value] : uint32Values)
        value = AI_VALUE(uint32, valueName);
}

std::string EntryTravelDestination::GetShortName() const
{
    switch (purpose)
    {
    case TravelDestinationPurpose::QuestGiver:
        return "questgiver";
    case TravelDestinationPurpose::QuestObjective1:
    case TravelDestinationPurpose::QuestObjective2:
    case TravelDestinationPurpose::QuestObjective3:
    case TravelDestinationPurpose::QuestObjective4:
        return "questobjective";
    case TravelDestinationPurpose::QuestTaker:
        return "questtaker";
    case TravelDestinationPurpose::Vendor:
        return "vendor";
    case TravelDestinationPurpose::AH:
        return "ah";
    case TravelDestinationPurpose::Repair:
        return "repair";
    case TravelDestinationPurpose::Mail:
        return "mail";
    case TravelDestinationPurpose::Trainer:
        return "trainer";
    case TravelDestinationPurpose::Explore:
        return "explore";
    case TravelDestinationPurpose::GenericRpg:
        return "rpg";
    case TravelDestinationPurpose::Grind:
        return "grind";
    case TravelDestinationPurpose::Boss:
        return  "boss";
    case TravelDestinationPurpose::GatherFishing:
    case TravelDestinationPurpose::GatherHerbalism:
    case TravelDestinationPurpose::GatherMining:
    case TravelDestinationPurpose::GatherSkinning:
        return "gather";
    default:
        return "unknown";
    }
    return "none";
};

std::string QuestTravelDestination::GetTitle() const {
    return ChatHelper::formatQuest(GetQuestTemplate());
}

bool QuestRelationTravelDestination::IsPossible(const PlayerTravelInfo& info) const
{
    if (!info.GetBoolValue2("has strategy", "rpg quest"))
        return false;

    // Never send bots to challenge quest givers (e.g. Mysterious Stranger)
    if (GetEntry() == 81030 || GetEntry() == 62609 || GetQuestId() == 80388)
        return false;

    bool forceThisQuest = info.HasFocusQuest();

    if (forceThisQuest && !info.IsFocusQuest(GetQuestId()))
        return false;

    const Quest* quest = GetQuestTemplate();

    if (GetRelation() == 0)
    {
        if (!forceThisQuest && (int32)quest->GetQuestLevel() >= (int32)info.GetLevel() + (int32)5)
            return false;

        // No trip back for a grey quest: the same XP grey rule the grind and
        // quest-log upkeep use (MaNGOS::XP::GetGrayLevel). A quest the bot
        // outlevels pays no XP, and the giver sits in the starter area whose
        // mobs are grey too, so the walk only parks the bot among no-XP mobs.
        // QuestLevel 0 is scaling (GetQuestLevelForPlayer falls back to bot
        // level): never grey. Scoped to givers; hand-ins always pay out.
        if (!forceThisQuest && quest->GetQuestLevel() > 0 &&
            (int32)quest->GetQuestLevel() <= (int32)MaNGOS::XP::GetGrayLevel(info.GetLevel()))
            return false;

        // MaxLevel 0 is "no upper bound" in this core, not "level 0": the take
        // gate reads it as `if (pQuest->GetMaxLevel() && pQuest->GetMaxLevel() <
        // GetLevel())` (Player::CanTakeQuest). 6,509 of the 7,190 quest templates
        // carry 0, and against a level 1+ bot the literal comparison below
        // rejected every one of them, so the giver search could only ever find
        // the handful of templates that do set it - 8 giver pairs across 60 live
        // level 1-8 bots, from Turtle's low-level 60145/60150. QuestTravelToGiver
        // stayed 0 over 60 minutes of live play while 1,514 of 1,572 fruitless
        // quest searches came back with an empty list, although 15-42 giver
        // (npc, quest) pairs whose quest-level window a sampled level 1-4 bot fits
        // sit within 1500 yd of it.
        if ((int32)info.GetLevel() < quest->GetMinLevel() ||
            (quest->GetMaxLevel() && (int32)info.GetLevel() > quest->GetMaxLevel()))
            return false;

        if (OnMap(info.getPosition())) //CanTakeQuest will check required conditions which will fail on a different map.
            if (quest->GetRequiredCondition())          //So we skip this quest for now.
                return false;

        if (!info.HasFocusQuest())
        {
            if (info.GetBoolValue("can fight equal"))
            {
                if (info.GetUint8Value("free quest log slots") < 5)
                    return false;
            }
            else
            {
                if (info.GetUint8Value("free quest log slots") < 10)
                    return false;
            }

            //Do not try to pick up dungeon/elite quests in instances without a group.
            if ((quest->GetType() == QUEST_TYPE_ELITE || quest->GetType() == QUEST_TYPE_DUNGEON) && !info.GetBoolValue("can fight boss"))
                return false;
        }
    }
    else
    {
        //Do not try to hand-in dungeon/elite quests in instances without a group.
        if ((quest->GetType() == QUEST_TYPE_ELITE || quest->GetType() == QUEST_TYPE_DUNGEON) && !info.GetBoolValue("can fight boss"))
        {
            if (IsOverWorld(info.getPosition()))
                return false;
        }

        // Leave-the-valley hand-ins wait: a pool bot below 10 only walks to
        // a taker whose quest is rated at most one above its own level and
        // whose area is rated the same (QuestTakerTripFits, same +1 as the
        // grind order cap). The taker search then comes back empty and the
        // caller parks the purpose like any other empty search, so the bot
        // keeps working its own valley until it reaches the quest's level.
        // Hand-ins in the same valley keep working; owned/hired bots and
        // bots at 10+ keep today's behaviour.
        if (WorldPosition* takerPoint = GetClosestPoint(info.getPosition()))
        {
            AreaTableEntry const* takerArea = takerPoint->GetArea();
            AreaTableEntry const* botArea = info.getPosition().GetArea();
            bool const takerElsewhere = takerArea && botArea && takerArea->Id != botArea->Id;
            if (!ai::QuestTakerTripFits((int)quest->GetQuestLevel(),
                takerElsewhere, info.GetLevel(), info.IsMasterlessRandom()))
                return false;
        }
    }

    // Don't send a bot to a quest giver in a zone far above its level, or cross-zone for lowbies.
    // A giver in an area the bot has outgrown is the same trip in the other
    // direction: the starter valley holds only grey mobs for it, so it walks
    // back into no-XP country. The floor mirrors the grind ladder
    // (GrindSpotPolicy.h: botLevel - GRIND_LEVEL_UNDER): below it the bot
    // earns nothing there. Unknown areas (level 0) fail open; capitals stay
    // reachable (trainers/AH live there); takers always pay out, so only
    // givers are floored.
    WorldPosition* point = GetClosestPoint(info.getPosition());
    if (point)
    {
        int32 destAreaLevel = point->GetAreaLevel();
        if (destAreaLevel > 0 && destAreaLevel > (int32)info.GetLevel() + 5)
            return false;

        if (GetRelation() == 0 && !forceThisQuest && destAreaLevel > 0 &&
            destAreaLevel + GRIND_LEVEL_UNDER < (int32)info.GetLevel() &&
            !point->HasAreaFlag(AREA_FLAG_CAPITAL))
            return false;

        if (info.GetLevel() <= 5 && point->distance(info.getPosition()) > 1500.0f)
            return false;
    }

    return true;
}

bool QuestRelationTravelDestination::IsActive(Player* bot, const PlayerTravelInfo& info) const {
    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    AiObjectContext* context = ai->GetAiObjectContext();

    if(!IsPossible(info))
        return false;

    bool forceThisQuest = info.HasFocusQuest(); //Checked in IsPossible if it's 'this' quest.
    if (GetRelation() == 0 && GetEntry() > 0 && info.IsMasterlessRandom())
    {
        // Giver-stall back-off (TravelAction parks the pair 30 min after 2
        // stalls with no quest-state change): this pair's menu never offers
        // the quest, so the search stops offering it and the next pick goes
        // elsewhere. Owned bots keep today's behaviour. Reads only a created
        // value (like the "no quest hand in until::<quest>" park the taker
        // path reads) so the value store grows no entry per pair.
        // The npc-level "can accept quest" check below passes when the giver
        // offers any quest; this quest itself may sit behind an unfinished
        // chain (Virulence 60113 behind 367) and never show in the menu.
        if (Quest const* quest = sObjectMgr.GetQuestTemplate(GetQuestId()))
            if (!bot->SatisfyQuestPreviousQuest(quest, false) || !bot->SatisfyQuestPrevChain(quest, false))
                return false;

        std::string const backoffKey = ai::QuestGiverBackoffKey(GetEntry(), GetQuestId());
        if (context->HasValue("manual time", backoffKey) &&
            context->GetValue<time_t>("manual time", backoffKey)->Get() > time(0))
            return false;
    }

    if (GetRelation() == 0)
    {
        if (!bot->GetMap()->IsContinent() && (GetClosestPoint(bot)->GetMapId() != bot->GetMapId())) //This gives issues for bot->CanTakeQuest so stop here.
            return false;

        if (forceThisQuest)
        {
            if (!AI_VALUE2(bool, "group or", "following party,can accept quest npc::" + std::to_string(GetEntry()))) //Noone has yellow exclamation mark.
                if (!AI_VALUE2(bool, "group or", "following party,can accept quest low level npc::" + std::to_string(GetEntry()))) //Noone can do this quest.
                    return false;
        }
        else
        {
            if (info.GetBoolValue("can fight equal"))
            {
                if (!AI_VALUE2(bool, "group or", "following party,can accept quest npc::" + std::to_string(GetEntry()))) //Noone has yellow exclamation mark.
                    if (!AI_VALUE2(bool, "group or", "following party,can accept quest low level npc::" + std::to_string(GetEntry()) + ",need quest reward::" + std::to_string(GetQuestId()))) //Noone can do this quest for a usefull reward.
                        return false;
            }
            else
            {
                if (!AI_VALUE2(bool, "group or", "following party,can accept quest npc::" + std::to_string(GetEntry()))) //Noone has yellow exclamation mark.
                    if (!AI_VALUE2(bool, "group or", "following party,can accept quest low level npc::" + std::to_string(GetEntry()))) //Noone can pick up this quest for money.
                        return false;
            }
        }
    }
    else
    {
        if (!AI_VALUE2(bool, "group or", "following party,can turn in quest npc::" + std::to_string(GetEntry())))
            return false;
    }

    if (GetEntry() > 0)
    {
        return !GuidPosition(HIGHGUID_UNIT, GetEntry()).IsHostileTo(bot);
    }

    return true;
}

std::string QuestRelationTravelDestination::GetTitle() const {
    std::ostringstream out;

    out << "talk to ";

    if (GetRelation() == 0)
        out << "questgiver ";
    else
        out << "questtaker ";

    out << ChatHelper::formatWorldEntry(GetEntry());
    return out.str();
}

bool QuestObjectiveTravelDestination::IsPossible(const PlayerTravelInfo& info) const
{
    if (GetEntry() == 81030 || GetEntry() == 62609 || GetQuestId() == 80388)
        return false;

    if (!info.GetBoolValue2("has strategy", "rpg quest"))
        return false;

    bool forceThisQuest = info.HasFocusQuest();

    if (forceThisQuest && !info.IsFocusQuest(GetQuestId()))
        return false;

    bool skipShouldGrindCheck = false;

    //Check mob level
    if (GetEntry() > 0)
    {
    }

    if (!forceThisQuest)
    {
        if ((int32)GetQuestTemplate()->GetQuestLevel() > (int32)info.GetLevel() + (int32)1)
            return false;

        if (!skipShouldGrindCheck && GetQuestTemplate()->GetQuestLevel() + 5 > (int)info.GetLevel() && !info.GetBoolValue("can fight equal"))
            return false;
    }

    if (info.IsInRaid() != (GetQuestTemplate()->GetType() == QUEST_TYPE_RAID))
        return false;


    bool skipKillableCheck = false;

    //Check mob level
    if (GetEntry() > 0)
    {

        CreatureInfo const* cInfo = GetCreatureInfo();

        if (cInfo->npc_flags & UNIT_NPC_FLAG_VENDOR && GetQuestTemplate()->ReqItemId[GetObjective()])
        {
            ItemPrototype const* proto = sObjectMgr.GetItemPrototype(GetQuestTemplate()->ReqItemId[GetObjective()]);
            if (GetQuestTemplate()->ReqItemCount[GetObjective()] * proto->BuyPrice > info.GetMoney()) //Need more money.
                return false;

            skipKillableCheck = true;
        }

        if (!skipKillableCheck && !forceThisQuest)
        {
            // A pool bot fights at most one level above its own below level 10
            // (PullGrindLevelCap): a creature past that cap is no quest
            // destination. The template read is the spawn entry itself, so an
            // over-level alternative drop of the same item never lures the bot
            // off its valley - the capped search comes back empty and the caller
            // parks the purpose instead. Vendors are exempt (buying needs no
            // fight); owned/hired bots keep today's behaviour. Matches the donor
            // mod-playerbots shape (its +4 mob check) at the pool's +1 number.
            if (cInfo && !ai::QuestObjectiveLevelFits((int)cInfo->level_max, info.GetLevel(),
                info.IsMasterlessRandom(),
                (cInfo->npc_flags & UNIT_NPC_FLAG_VENDOR) != 0))
                return false;

            if (cInfo && (int)cInfo->level_max - (int)info.GetLevel() > 4)
                return false;

            //Do not try to hand-in dungeon/elite quests in instances without a group.
            if (cInfo->rank > CREATURE_ELITE_NORMAL)
            {
                if (!IsOverWorld(info.getPosition()) && info.getPosition().GetMapId() != 609 && !info.GetBoolValue("can fight boss"))
                    return false;
                else if (!info.GetBoolValue("can fight elite"))
                    return false;
            }
        }
    }

    if (!forceThisQuest)
    {
        if (!skipKillableCheck && GetQuestTemplate()->GetType() == QUEST_TYPE_ELITE && !info.GetBoolValue("can fight elite"))
            return false;

        //Do not try to do dungeon/elite quests in instances without a group.
        if ((GetQuestTemplate()->GetType() == QUEST_TYPE_ELITE || GetQuestTemplate()->GetType() == QUEST_TYPE_DUNGEON || GetQuestTemplate()->GetType() == QUEST_TYPE_RAID) && !info.GetBoolValue("can fight boss"))
        {
            if (!IsOverWorld(info.getPosition()))
                return false;
        }

        //Do not try to do pvp quests in bg's (no way to travel there).
        if (GetQuestTemplate()->GetType() == QUEST_TYPE_PVP)
        {
            if (!IsOverWorld(info.getPosition()))
                return false;
        }
    }

    // Don't send a bot to a quest objective in a zone far above its level, or cross-zone for lowbies
    WorldPosition* point = GetClosestPoint(info.getPosition());
    if (point)
    {
        int32 destAreaLevel = point->GetAreaLevel();
        if (destAreaLevel > 0 && destAreaLevel > (int32)info.GetLevel() + 5)
            return false;

        if (info.GetLevel() <= 5 && point->distance(info.getPosition()) > 1500.0f)
            return false;
    }

    return true;
}

bool QuestObjectiveTravelDestination::IsActive(Player* bot, const PlayerTravelInfo& info) const {
    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    AiObjectContext* context = ai->GetAiObjectContext();

    if (!IsPossible(info))
        return false;

    bool forceThisQuest = info.HasFocusQuest();

    bool skipKillableCheck = false;

    if (GetEntry() > 0)
    {

        CreatureInfo const* cInfo = GetCreatureInfo();

        if (!skipKillableCheck && cInfo->npc_flags & UNIT_NPC_FLAG_VENDOR && GetQuestTemplate()->ReqItemId[GetObjective()] &&
            !GuidPosition(HIGHGUID_UNIT, GetEntry()).IsHostileTo(bot))
        {
            skipKillableCheck = true;
        }
    }

    std::vector<std::string> qualifier = { std::to_string(GetQuestTemplate()->GetQuestId()), std::to_string(GetObjective()) };

    if (!AI_VALUE2(bool, "group or", "following party,need quest objective::" + Qualified::MultiQualify(qualifier, ","))) //Noone needs the quest objective.
        return false;

    WorldPosition botPos(bot);

    if (!skipKillableCheck && GetEntry() > 0 && !IsOut(botPos))
    {
        TravelTarget* target = AI_VALUE(TravelTarget*, "travel target");

        //Only look for the target if it is unique or if we are currently working on it.
        if (IsUnique() || (target->GetStatus() == TravelStatus::TRAVEL_STATUS_WORK && target->GetEntry() == GetEntry()))
        {
            std::list<ObjectGuid> targets = AI_VALUE(std::list<ObjectGuid>, "possible targets");

            for (auto& target : targets)
                if (target.GetEntry() == GetEntry() && target.IsCreature() && ai->GetCreature(target) && ai->GetCreature(target)->IsAlive())
                    return true;

            return false;
        }
    }

    return true;
}

std::string QuestObjectiveTravelDestination::GetTitle() const {
    std::ostringstream out;

    if (GetQuestTemplate()->ReqItemCount[GetObjective()] > 0)
        out << "loot " << ChatHelper::formatItem(sObjectMgr.GetItemPrototype(GetQuestTemplate()->ReqItemId[GetObjective()]), 0, 0) << " from";
    else if (GetEntry() > 0)
        out << "kill";
    else
        out << "use";

    out << " " << ChatHelper::formatWorldEntry(GetEntry());

    out << " (objective " << (GetObjective() + 1) << ")";

    return out.str();
}

uint8 QuestObjectiveTravelDestination::GetObjective() const
{
    switch (GetPurpose())
    {
    case TravelDestinationPurpose::QuestObjective1:
        return 0;
    case TravelDestinationPurpose::QuestObjective2:
        return 1;
    case TravelDestinationPurpose::QuestObjective3:
        return 2;
    case TravelDestinationPurpose::QuestObjective4:
        return 3;
    default:
        return 0;
    }
    return 0;
}

uint8 QuestObjectiveTravelDestination::getObjective() const
{
    return GetObjective();
}

bool RpgTravelDestination::IsPossible(const PlayerTravelInfo& info) const
{
    bool const beginner = info.GetLevel() < 5;

    // Don't send low-level bots on RPG travel — the path to any NPC typically
    // crosses level 5+ mobs that kill level 1-4 bots instantly, creating a death loop.
    // Exception: the camp vendor. Selling the starter loot is the only income a fresh
    // random bot has, and the camp it spawns in is mob-safe; the trip is capped to the
    // beginner radius below and to the starting-zone level band, so the walk stays
    // inside that safe pocket. Every other RPG errand (trainer, AH, mail, generic rpg)
    // stays blocked below level 5, and owned/hired bots with a player master are
    // unaffected — their player decides where they walk.
    if (beginner && (GetPurpose() != TravelDestinationPurpose::Vendor || !info.IsMasterlessRandom()))
        return false;

    // Don't send a bot to an NPC sitting in a zone far above its level — the journey
    // crosses (and the destination sits among) mobs that farm the bot into a death
    // spiral at the local high-level graveyard. Margin matches the quest-level gate
    // (+5); the autonomous grind gate is one step tighter (GRIND_AREA_MARGIN in
    // GrindSpotPolicy.h - an errand's route is fixed, a grind pick is not).
    // getAreaLevel() returns -1/-2 for unknown areas; only reject on a real level.
    WorldPosition* point = GetClosestPoint(info.getPosition());
    if (point)
    {
        AreaTableEntry const* area = point->GetArea();
        uint32 zoneId = area ? (area->ZoneId ? area->ZoneId : area->Id) : 0;
        if (!sPlayerbotAIConfig.allowIsolatedCustomStartingZones &&
            (PlayerbotAIConfig::IsIsolatedCustomZone(zoneId) || (area && PlayerbotAIConfig::IsIsolatedCustomZone(area->Id))))
            return false;

        int32 destAreaLevel = point->GetAreaLevel();
        if (destAreaLevel > 0 && destAreaLevel > (int32)info.GetLevel() + 5)
            return false;

        // Outgrown services: skip NPCs in zones the bot outlevels by 10+, unless
        // the destination zone is a capital (class trainers, AH and bank live
        // there). Autonomous masterless random bots only: an owned/hired bot
        // with its player must still vendor/repair anywhere. Unknown area ids
        // fail closed (keep the destination).
        if (info.IsMasterlessRandom() && sPlayerbotAIConfig.leaveOutgrownZones &&
            destAreaLevel > 0 && destAreaLevel + 10 < (int32)info.GetLevel())
        {
            uint32 destZoneId = area ? (area->ZoneId ? area->ZoneId : area->Id) : 0;
            AreaTableEntry const* destZone = destZoneId ? GetAreaEntryByAreaID(destZoneId) : nullptr;
            if (!destZone || !(destZone->Flags & AREA_FLAG_CAPITAL))
                return false;
        }

        // A beginner vendor trip must stay inside the starting camp: the far walk is
        // exactly what the level-5 gate above protects against.
        if (beginner && point->distance(info.getPosition()) > sPlayerbotAIConfig.lowLevelVendorMaxDistance)
            return false;

        if (info.GetLevel() <= 5 && point->distance(info.getPosition()) > 1500.0f)
            return false;
    }
    else if (beginner) //A vendor trip we cannot measure is not worth the risk.
        return false;

    //Horde pvp baracks
    if (ClosestMapId(info.getPosition()) == 450 && info.GetTeam() == ALLIANCE)
        return false;

    //Alliance pvp baracks
    if (ClosestMapId(info.getPosition()) == 449 && info.GetTeam() == HORDE)
        return false;

    return true;
}

bool RpgTravelDestination::IsActive(Player* bot, const PlayerTravelInfo& info) const
{
    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    AiObjectContext* context = ai->GetAiObjectContext();

    if (!IsPossible(info))
        return false;

    //Once the target rpged with it is added to the ignore list. We can now move on.
    std::set<ObjectGuid>& ignoreList = AI_VALUE(std::set<ObjectGuid>&,"ignore rpg target");

    for (auto& i : ignoreList)
    {
        if (i.GetEntry() == GetEntry())
        {
            return false;
        }
    }

    return !GuidPosition(HIGHGUID_UNIT, GetEntry()).IsHostileTo(bot);
}

std::string RpgTravelDestination::GetTitle() const
{
    std::ostringstream out;

    switch (GetPurpose())
    {
    case TravelDestinationPurpose::Vendor:
        out << "sell items to";
        break;
    case TravelDestinationPurpose::AH:
        out << "put items on auction at";
        break;
    case TravelDestinationPurpose::Repair:
        out << "repair at";
        break;
    case TravelDestinationPurpose::Mail:
        out << "receive mail from";
        break;
    case TravelDestinationPurpose::Trainer:
        out << "train a skill at";
        break;
    case TravelDestinationPurpose::GenericRpg:
        out << "find"; //Named travel purpose.
        break;
    default:
        out << "";
        break;
    }

    /*
    out << " " << GetShortName();

    if (GetEntry() > 0)
        out << " npc ";
    else
        out << " object ";

    */

    out << " ";

    out << ChatHelper::formatWorldEntry(GetEntry());

    return out.str();
}

AreaTableEntry const* ZoneTravelDestination::GetArea() const
{
    for (uint32 areaid = 0; areaid < sAreaStore.GetMaxEntry(); ++areaid)
    {
        AreaTableEntry const* areaEntry = sAreaStore.LookupEntry<AreaEntry>(areaid);
        if (areaEntry && areaEntry->Id == GetEntry())
        {
            return areaEntry;
        }
    }

    return nullptr;
}

bool ExploreTravelDestination::IsPossible(const PlayerTravelInfo& info) const
{
    AreaTableEntry const* area = GetArea();
    if (!area)
        return false;

    uint32 zoneId = area->ZoneId ? area->ZoneId : area->Id;
    if (!sPlayerbotAIConfig.allowIsolatedCustomStartingZones &&
        (PlayerbotAIConfig::IsIsolatedCustomZone(zoneId) || (area && PlayerbotAIConfig::IsIsolatedCustomZone(area->Id))))
        return false;

    if (GetLevel() && (uint32)GetLevel() > info.GetLevel() && info.GetLevel() < DEFAULT_MAX_LEVEL)
        return false;

    if (info.IsMasterlessRandom())
    {
        // Enemy home zone, same rule as WorldPosition::isEnemyHomeZoneFor:
        // sub-areas usually carry no team and inherit their zone's.
        uint32 areaTeam = area->Team;
        if (areaTeam == AREATEAM_NONE && area->ZoneId)
            if (AreaEntry const* zone = AreaEntry::GetById(area->ZoneId))
                areaTeam = zone->Team;
        Team const botTeam = info.GetTeam();
        if ((areaTeam == AREATEAM_ALLY && botTeam == HORDE) || (areaTeam == AREATEAM_HORDE && botTeam == ALLIANCE))
            return false;
    }

    return true;
}

bool ExploreTravelDestination::IsActive(Player* bot, const PlayerTravelInfo& info) const
{
    if (!IsPossible(info))
        return false;

    AreaTableEntry const* area = GetArea();

    if (area->ExploreFlag == 0xffff)
        return false;
    int offset = area->ExploreFlag / 32;

    uint32 val = (uint32)(1 << (area->ExploreFlag % 32));
    uint32 currFields = bot->GetUInt32Value(PLAYER_EXPLORED_ZONES_1 + offset);

    return !(currFields & val);
}

bool GrindTravelDestination::IsPossible(const PlayerTravelInfo& info) const
{
    // The old veto read the cached "should sell" && "can sell" pair, which a fresh
    // pool bot satisfies with one grey pelt in its bags: the next grind search
    // found nothing, so the stranded beginner kept its empty result and its NPC.
    // Read the live vendor need instead ("vendor trip needed", snapshotted in the
    // constructor from VendorTripNeeded: repair, a spell the stock actually pays
    // for, rations it can afford). A parked vendor purpose (fruitless errand,
    // issue #393) is no need at all - the trip is not happening - so grind stays
    // a destination and the bot walks instead of idling. The park timestamp still
    // gates the vendor row itself.
    if (info.GetBoolValue("vendor trip needed"))
        return false;

    CreatureInfo const* cInfo = GetCreatureInfo();


    int32 botLevel = info.GetLevel();

    // Autonomous bots get the level-appropriate window (GrindSpotPolicy.h) so a spot
    // stops being a destination the moment the bot outlevels it and the next request
    // walks it to the next fitting field or zone. Owned bots keep the old conservative
    // window - their player decides where to hunt. Either way the beginner clamp keeps
    // levels 1-4 on their own level, and the gold rule below still lets those bots hunt
    // coinless starter beasts (Valley of Trials, Camp Narache: nothing but boars and
    // scorpids, gold 0), which they could otherwise never grind.
    bool const beginner = botLevel <= 4;
    GrindLevelBand const band = GetGrindLevelBand((uint32)botLevel, info.GetUint8Value("durability"), info.IsMasterlessRandom());

    if (!GrindLevelFits(band, (int32)cInfo->level_max)) //level 5: [3,6] where it used to be [3,3]
        return false;

    // Autonomous bots hunt coinless wildlife (wolves, boars, spiders, scorpids)
    // at every level: those carry no copper but they pay in XP, grey vendor loot
    // and skins, and they are the open-field hunt that keeps bots out of the
    // humanoid camps. Critters (zero XP) are never worth the walk. Owned bots keep
    // the old copper-only rule, with the beginner allowance they always had.
    if (!GrindPreyAllowed((int32)cInfo->gold_min, cInfo->type == CREATURE_TYPE_CRITTER, info.IsMasterlessRandom(), beginner))
        return false;

    if (cInfo->rank > CREATURE_ELITE_NORMAL && !info.GetBoolValue("can fight elite"))
        return false;

    // Don't send a bot to a grind creature located in a zone far above its level
    WorldPosition* point = GetClosestPoint(info.getPosition());
    if (point)
    {
        AreaTableEntry const* area = point->GetArea();
        uint32 zoneId = area ? (area->ZoneId ? area->ZoneId : area->Id) : 0;
        if (!sPlayerbotAIConfig.allowIsolatedCustomStartingZones &&
            (PlayerbotAIConfig::IsIsolatedCustomZone(zoneId) || (area && PlayerbotAIConfig::IsIsolatedCustomZone(area->Id))))
            return false;

        // Autonomous bots take the measured ceiling (GrindSpotPolicy.h): the first
        // areas above the bot's level hold the XP, the far tail (bot+4..+5) paid the
        // same XP per kill for about three times the deaths. The beginner clamp
        // window (levels 1-4) keeps the wider margin regardless - a starter valley's
        // own sub-areas are rated far above a fresh bot (Camp Narache and Mulgore 6,
        // Dun Morogh 7, Durotar 8), so the tighter margin would leave a level-1 bot
        // with no destination at all, which is the starter-valley wall the clamp
        // exists for. This margin and TravelMgr::IsLocationLevelValid's area ceiling
        // must agree, or the coarser area vote lets through points this one rejects.
        int32 const areaMargin = (info.IsMasterlessRandom() && !beginner) ? GRIND_AREA_MARGIN : GRIND_AREA_MARGIN_OWNED;
        int32 destAreaLevel = point->GetAreaLevel();
        // Starter-valley exemption (GrindSpotPolicy.h): the valley average sits
        // far above a level 1-4 bot while its mobs are vetted in-cap by the
        // band above, so the ceiling would veto every local point. Mirrors
        // the IsLocationLevelValid beginnerGrind exemption at this gate.
        if (destAreaLevel > 0 && destAreaLevel > (int32)info.GetLevel() + areaMargin &&
            !ai::GrindValleyExempted(info.GetLevel(), info.IsMasterlessRandom()))
            return false;

        // Outgrown-zone floor (task E, ZoneMigratePolicy.h): a pool bot at
        // 11+ holding only outgrown-zone grind destinations must lose them,
        // or the taker-only/hand-in latches aside every pick still lands
        // home. Zone level, not area: sub-areas inherit their parent zone
        // below. Unknown zones fail open; owned/hired bots keep the player.
        if (area && zoneId && info.IsMasterlessRandom())
        {
            int32 pointZoneLevel = 0;
            if (sTravelMgr.TryGetValidatedAreaLevel(zoneId, pointZoneLevel) && pointZoneLevel > 0 &&
                ai::OutgrownZoneRefusesPoint(pointZoneLevel, info.GetLevel(), true))
                return false;
        }

        if (info.GetLevel() <= 5 && point->distance(info.getPosition()) > 1500.0f)
            return false;
    }

    return true;
}

bool GrindTravelDestination::IsActive(Player* bot, const PlayerTravelInfo& info) const
{
    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    AiObjectContext* context = ai->GetAiObjectContext();

    if (!IsPossible(info))
        return false;

    // A kind of creature the bot recently gave up on (unreachable from where it stood, see
    // ReachTargetAction) is no grind destination for a while either - otherwise the bot
    // walks straight back to the same mine and starts over.
    std::map<uint32, uint32>& unreachableKinds = context->GetValue<std::map<uint32, uint32>&>("unreachable entries")->Get();
    auto givenUpKind = unreachableKinds.find(GetEntry());
    if (givenUpKind != unreachableKinds.end())
    {
        if (WorldTimer::getMSTime() < givenUpKind->second)
            return false;
        unreachableKinds.erase(givenUpKind);
    }

    // Neutral starter wildlife is prey, not scenery. The old hostile-only read
    // rejected every neutral beast - Thistle Boars / Nightsabers (faction
    // 189/7 read REP_NEUTRAL against a player faction template) - so a level
    // 1-3 bot with no quest destination never held a grind destination and
    // looped QuestTripNoTarget instead of walking to its wolves (issue #393).
    // The rule lives in GrindSpotPolicy.h next to the other prey rules; the
    // donor mod-playerbots grind filter keeps loot-carrying neutrals the same
    // way and only refuses non-hostile NPCs. The neutral gates mirror the
    // arrival-side attackability verdict, so an admitted destination is one
    // the bot can actually hit: reputation factions (core IsValidAttackTarget
    // refuses neutral-rep targets without AT_WAR), unattackable unit flags
    // (core IsTargetable), elites/rares. All data-only (creature template +
    // faction DBC), safe on the world thread.
    GuidPosition botPos(bot);
    GuidPosition preyPos(HIGHGUID_UNIT, GetEntry());
    CreatureInfo const* preyInfo = GetCreatureInfo();
    bool noReputation = true;
    if (preyInfo)
    {
        // Penqle uses sObjectMgr.GetFactionTemplateEntry/GetFactionEntry, not
        // the cmangos sFactionTemplateStore/sFactionStore globals (see
        // PlayerbotAI.h). Missing rows fail open: a template or faction the
        // core never loaded cannot carry a reputation list, so the core AT_WAR
        // check in IsValidAttackTarget cannot refuse it either.
        if (FactionTemplateEntry const* preyTemplate = sObjectMgr.GetFactionTemplateEntry(preyInfo->faction))
            if (FactionEntry const* preyFaction = sObjectMgr.GetFactionEntry(preyTemplate->faction))
                noReputation = !preyFaction->CanHaveReputation();
    }
    return GrindHostilityAllowed(botPos.IsHostileTo(preyPos, bot->GetInstanceId()),
        botPos.IsFriendlyTo(preyPos, bot->GetInstanceId()),
        preyInfo ? preyInfo->npc_flags : 0,
        preyInfo && preyInfo->xp_multiplier != 0.0f,
        noReputation,
        preyInfo ? preyInfo->unit_flags : 0,
        preyInfo ? preyInfo->rank : 0);
}

std::string GrindTravelDestination::GetTitle() const
{
    std::ostringstream out;

    out << "get xp or gold from killing ";

    out << ChatHelper::formatWorldEntry(GetEntry());

    return out.str();
}

bool BossTravelDestination::IsPossible(const PlayerTravelInfo& info) const
{
    if (!info.GetBoolValue("can fight boss"))
        return false;

    CreatureInfo const* cInfo = sObjectMgr.GetCreatureTemplate(GetEntry());

    if ((int32)cInfo->level_max > info.GetLevel() + 3)
        return false;

    const MapEntry* mapEntry = ClosetMapEntry(info.getPosition());

    if (info.IsInGroup())
    {
        if (info.IsInRaid())
        {
            if (mapEntry && mapEntry->IsNonRaidDungeon())
                return false;
        }
        else if (mapEntry && mapEntry->IsRaid())
            return false;
    }

    //Ragefire casm
    if (ClosestMapId(info.getPosition()) == 389 && info.GetTeam() == ALLIANCE)
        return false;

    //Stockades
    if (ClosestMapId(info.getPosition()) == 34 && info.GetTeam() == HORDE)
        return false;

    //Do not move to overworld bosses/uniques that are far away.
    if (mapEntry && mapEntry->IsContinent() && DistanceTo(info.getPosition()) > 2000.0f)
        return false;

    return true;
}

bool BossTravelDestination::IsActive(Player* bot, const PlayerTravelInfo& info) const
{
    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    AiObjectContext* context = ai->GetAiObjectContext();

    if (!IsPossible(info))
        return false;

    if (!GuidPosition(bot).IsHostileTo(GuidPosition(HIGHGUID_UNIT, GetEntry()), bot->GetInstanceId()))
        return false;

    WorldPosition botPos(bot);

    if (!IsOut(botPos))
    {
        std::list<ObjectGuid> targets = AI_VALUE(std::list<ObjectGuid>, "possible targets");

        for (auto& target : targets)
            if (target.GetEntry() == GetEntry() && target.IsCreature() && ai->GetCreature(target) && ai->GetCreature(target)->IsAlive())
                return true;

        return false;
    }

    if (!AI_VALUE2(bool, "has upgrade",  GetEntry()))
        return false;

    return true;
}

std::string BossTravelDestination::GetTitle() const
{
    std::ostringstream out;

    out << "get loot from ";

    out << ChatHelper::formatWorldEntry(GetEntry());

    return out.str();
}

bool GatherTravelDestination::IsPossible(const PlayerTravelInfo& info) const
{
    uint32 skillId = SKILL_NONE;
    uint32 reqSkillValue = 0;

    if (GetPurpose() == TravelDestinationPurpose::GatherFishing)
    {
        skillId = SKILL_FISHING;

        int32 reqSkillValue = sObjectMgr.GetFishingBaseSkillLevel(GetEntry());

        if (!reqSkillValue) //no fishable zone or area should be 0
            return false;

        if (reqSkillValue > 5)
            reqSkillValue -= 5; //Give chance to levelup.
    }
    else if (GetEntry() > 0)
    {
        CreatureInfo const* cInfo = GetCreatureInfo();

        if (!cInfo)
            return false;

        skillId = GetRequiredLootSkillCompat(cInfo);
        uint32 targetLevel = cInfo->level_max;
        reqSkillValue = targetLevel < 10 ? 1 : targetLevel < 20 ? (targetLevel - 10) * 10 : targetLevel * 5;
    }
    else
    {
        GameObjectInfo const* goInfo = GetGoInfo();

        if (!goInfo)
            return false;

        uint32 lockId = goInfo->GetLockId();
        LockEntry const* lockInfo = sLockStore.LookupEntry(lockId);
        if (!lockInfo)
            return false;

        for (int i = 0; i < 8; ++i)
        {
            if (lockInfo->Type[i] == LOCK_KEY_SKILL)
                if (SkillByLockType(LockType(lockInfo->Index[i])) > 0)
                {
                    skillId = SkillByLockType(LockType(lockInfo->Index[i]));
                    reqSkillValue = std::max((uint32)1, lockInfo->Skill[i]);
                    break;
                }
        }
    }

    if (!info.GetCurrentSkill((SkillType)skillId))
        return false;

    uint32 skillValue = uint32(info.GetCurrentSkill((SkillType)skillId));
    if (reqSkillValue > skillValue)
        return false;

    if (info.GetSkillMax((SkillType)skillId) <= skillValue) //Not able to increase skill.
        return false;

    if (GetPurpose() != TravelDestinationPurpose::GatherFishing && reqSkillValue + 100 < skillValue) //Gray level = no skillup
        return false;

    WorldPosition* point = GetClosestPoint(info.getPosition());
    if (point)
    {
        int32 destAreaLevel = point->GetAreaLevel();
        if (destAreaLevel > 0 && destAreaLevel > (int32)info.GetLevel() + 5)
            return false;

        // Outgrown-zone floor (task E, ZoneMigratePolicy.h): mining/herb
        // nodes keep no mob band, so without this an outgrown starter node
        // stays a destination forever. Same +5 shape and scope as the grind
        // gate; fishing untouched (FishingSpotPolicy owns that errand).
        // Next-tier guard: starter Copper/Earthroot (req 1) stay while the
        // bot's skill cannot gather next-zone Tin/Briarthorn (req 65) -
        // refusing them would trap a low-skill bot with nowhere to skill
        // up. The grey rule above already drops nodes 100 past skill.
        if (skillValue >= ai::OUTGROWN_GATHER_NEXT_TIER_SKILL && info.IsMasterlessRandom() &&
            (GetPurpose() == TravelDestinationPurpose::GatherMining ||
                GetPurpose() == TravelDestinationPurpose::GatherHerbalism))
        {
            AreaTableEntry const* area = point->GetArea();
            uint32 zoneId = area ? (area->ZoneId ? area->ZoneId : area->Id) : 0;
            if (zoneId)
            {
                int32 pointZoneLevel = 0;
                if (sTravelMgr.TryGetValidatedAreaLevel(zoneId, pointZoneLevel) && pointZoneLevel > 0 &&
                    ai::OutgrownZoneRefusesPoint(pointZoneLevel, info.GetLevel(), true))
                    return false;
            }
        }

        if (info.GetLevel() <= 5 && point->distance(info.getPosition()) > 1500.0f)
            return false;
    }

    return true;
}

bool GatherTravelDestination::IsActive(Player* bot, const PlayerTravelInfo& info) const
{
    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    AiObjectContext* context = ai->GetAiObjectContext();

    if (!IsPossible(info))
        return false;


    if (GetPurpose() != TravelDestinationPurpose::GatherFishing)
    {
        if (!IsOut(bot))
        {
            if (this->GetEntry() > 0)
            {
                std::list<ObjectGuid> targets = AI_VALUE(std::list<ObjectGuid>, "possible targets");

                for (auto& target : targets)
                    if (target.GetEntry() == GetEntry() && target.IsCreature() && ai->GetCreature(target) && ai->GetCreature(target)->IsAlive())
                        return true;
            }
            else
            {
                std::list<ObjectGuid> targets = AI_VALUE(std::list<ObjectGuid>, "nearest game objects no los");

                for (auto& target : targets)
                    if (target.GetEntry() == GetEntry())
                        return true;
            }

            return false;
        }
    }

    return true;
}

std::string GatherTravelDestination::GetTitle() const {
    std::ostringstream out;

    if (GetPurpose() == TravelDestinationPurpose::GatherFishing)
    {
        out << "fish";
    }
    else
    {
        switch (GetPurpose())
        {
            case TravelDestinationPurpose::GatherSkinning:
                out << "skin ";
                break;
            case TravelDestinationPurpose::GatherMining:
                out << "mine ";
                break;
            case TravelDestinationPurpose::GatherHerbalism:
                out << "gather from ";
                break;
        }

        out << ChatHelper::formatWorldEntry(GetEntry());
    }

    return out.str();
}

TravelTarget::TravelTarget(PlayerbotAI* ai) : AiObject(ai)
{
    sTravelMgr.SetNullTravelTarget(this);
    SetStatus(TravelStatus::TRAVEL_STATUS_EXPIRED);
}

void TravelTarget::SetTarget(TravelDestination* tDestination1, WorldPosition* wPosition1) {
    if (dynamic_cast<TemporaryTravelDestination*>(tDestination) && tDestination1 != tDestination)
        delete tDestination;

    if (tDestination != tDestination1)
    {
        sTravelMgr.ReleaseGrindSpot(tDestination);
        sTravelMgr.AcquireGrindSpot(tDestination1);
    }

    wPosition = wPosition1;
    tDestination = tDestination1;

    // A new destination (or a new point of one) starts with a fresh move
    // budget: the failed moves that built up moveRetryCount belonged to the
    // previous spot. Never reset, the counter ratcheted across destinations
    // (IncRetry steps by 2, only a successful move decays it by 1, and nothing
    // else ever set it back), so once it passed IsMaxRetry the first failed
    // MoveTo dropped every new target - live counters reached 1187 and 91.7%
    // of drops carried >12 against a drop threshold of 10.
    // extendRetryCount is deliberately NOT reset here: it counts how often the
    // same destination was re-pointed (RefreshTravelTargetAction) and is what
    // retires a spot that keeps being re-picked; CopyTarget carries it over
    // from the freshly chosen target. UnstuckAction saves/restores both
    // counters around the re-SetTarget it does after its reset.
    moveRetryCount = 0;

    SetStatus(TravelStatus::TRAVEL_STATUS_TRAVEL);
}

TravelTarget::~TravelTarget()
{
    // A bot that logs out must not leave its grind spot counted as taken, or the
    // pool would slowly fill the world with "crowded" spots nobody is on.
    sTravelMgr.ReleaseGrindSpot(tDestination);
}

void TravelTarget::CopyTarget(TravelTarget* const target) {
    SetTarget(target->tDestination, target->wPosition);
    groupMember = target->groupMember;
    forced = target->forced;
    relevance = target->relevance;
    extendRetryCount = target->extendRetryCount;
}

void TravelTarget::SetStatus(TravelStatus status) {
    m_status = status;
    startTime = WorldTimer::getMSTime();

    switch (m_status) {
    case TravelStatus::TRAVEL_STATUS_NONE:
    case TravelStatus::TRAVEL_STATUS_PREPARE:
    case TravelStatus::TRAVEL_STATUS_EXPIRED:
        statusTime = 1;
        break;
    case TravelStatus::TRAVEL_STATUS_READY:
        statusTime = HOUR *  1000;
        break;
    case TravelStatus::TRAVEL_STATUS_TRAVEL:
        statusTime = GetMaxTravelTime() * 2 + sPlayerbotAIConfig.maxWaitForMove;
        break;
    case TravelStatus::TRAVEL_STATUS_WORK:
        statusTime = tDestination->GetExpireDelay();
        break;
    case TravelStatus::TRAVEL_STATUS_COOLDOWN:
        statusTime = tDestination->GetCooldownDelay();
    default: break;
    }
}

bool TravelTarget::IsDestinationActive()
{
    Player* player = bot;
    if (groupMember)
    {
        Player* member = groupMember.GetPlayer();
        if (!ai->IsSafe(member)) //unknown
            return true;
        else
            player = member;
    }

    if (!PlayerbotAIStorage::Instance().GetAI(player)) //No ai so clear target.
        return false;

    return tDestination->IsActive(player, PlayerTravelInfo(player));
}

bool TravelTarget::IsConditionsActive(bool clear)
{
    Player* player = bot;
    if (groupMember)
    {
        Player* member = groupMember.GetPlayer();
        if (!ai->IsSafe(member)) //unknown
            return true;
        else
            player = member;
    }

    if (!player || !PlayerbotAIStorage::Instance().GetAI(player)) //No ai so clear target.
        return false;

    AiObjectContext* playerContext = PlayerbotAIStorage::Instance().GetAI(player)->GetAiObjectContext();

    if (!playerContext)
        return false;

    if (clear)
        for (auto& condition : travelConditions)
            playerContext->ClearValues(condition);

    for (auto& condition : travelConditions)
    {
        auto* value = playerContext->GetValue<bool>(condition);
        if (!value || !value->Get())
            return false;
    }

    return true;
}

namespace
{
    // A grind destination the bot has simply outgrown: its creatures sit outside the
    // bot's level band, so the spot can never come back and the bot must walk on. The
    // band test is the same one the destination filter uses - this only asks whether
    // the level window is the reason (see the other gates in IsPossible).
    bool GrindSpotOutgrown(TravelDestination* destination, Player* bot)
    {
        GrindTravelDestination* grind = dynamic_cast<GrindTravelDestination*>(destination);

        if (!grind || !grind->GetCreatureInfo() || !bot)
            return false;

        PlayerTravelInfo info(bot);

        return !GrindLevelFits(GetGrindLevelBand((uint32)info.GetLevel(), info.GetUint8Value("durability"), info.IsMasterlessRandom()),
            (int32)grind->GetCreatureInfo()->level_max);
    }
}

void TravelTarget::CheckStatus()
{
    if (!IsActive())
        return;

    if (groupMember)
    {
        Player* member = groupMember.GetPlayer();
        if (!member || (ai->IsSafe(member) && member->GetGroup() != bot->GetGroup()))
        {
            ai->TellDebug(ai->GetMaster(), "Travel target expired because it was from a player that is no longer in the group.", "debug travel");
            SetStatus(TravelStatus::TRAVEL_STATUS_EXPIRED);
            return;
        }
    }

    if (!ai->HasStrategy("travel", BotState::BOT_STATE_NON_COMBAT) && !ai->HasStrategy("travel once", BotState::BOT_STATE_NON_COMBAT))
    {
        ai->TellDebug(ai->GetMaster(), "The target is clearing because it was a travel once destination.", "debug travel");
        sTravelMgr.SetNullTravelTarget(this);
        return;
    }

    if (statusTime != 0 && GetTimeLeft() <= 0 && !IsForced())
    {
        ai->TellDebug(ai->GetMaster(), "Travel target expired because the status time was exceeded.", "debug travel");
        SetStatus(TravelStatus::TRAVEL_STATUS_EXPIRED);
        // No ClearValues: the time-boxed purpose park (flag + timestamp) must
        // survive a routine expiry of another purpose; the request gate clears
        // each flag lazily once its own park expires.
        return;
    }

    if (GetStatus() == TravelStatus::TRAVEL_STATUS_TRAVEL)
    {
        bool HasArrived = tDestination->IsIn(bot);

        if (HasArrived)
        {
            if (ai->HasStrategy("travel once", BotState::BOT_STATE_NON_COMBAT))
            {
                ai->TellDebug(ai->GetMaster(), "The target is clearing because it was a travel once destination.", "debug travel");
                ai->ChangeStrategy("nc -travel once", BotState::BOT_STATE_NON_COMBAT);
                sTravelMgr.SetNullTravelTarget(this);
                return;
            }

            ai->TellDebug(ai->GetMaster(), "The target is starting to work because the destination has been reached.", "debug travel");
            SetStatus(TravelStatus::TRAVEL_STATUS_WORK);
            return;
        }
        else if(IsForced()) return; //While traveling do not go into cooldown
    }

    if (GetStatus() != TravelStatus::TRAVEL_STATUS_COOLDOWN)
    {
        bool destinationInactive = !IsDestinationActive() && !IsForced();
        bool conditionsInactive = !destinationInactive && !IsConditionsActive(); // Only check conditions if destination is still active

        if (destinationInactive || conditionsInactive)
        {
            // A grind spot the bot has simply outgrown is not a temporary failure:
            // its creatures sit below the bot's level band, nothing will bring them
            // back, and the ordinary cooldown would freeze the bot in place for a
            // full minute on every ding. Expire it so the very next tick can request
            // a spot that fits. Every other reason a destination went inactive
            // (unreachable kind, tapped, blacklist) keeps the cooldown pause.
            if (destinationInactive && GrindSpotOutgrown(tDestination, bot))
            {
                ai->TellDebug(ai->GetMaster(), "The target is expiring because the bot has outgrown the spot.", "debug travel");
                SetStatus(TravelStatus::TRAVEL_STATUS_EXPIRED);
                // No ClearValues here either (see above): this expiry only
                // ends the outgrown spot's own target, not other purposes' parks.
                return;
            }

            // A quest errand that went inactive is done (quest accepted,
            // objective complete, reward taken): it cannot be re-picked, so the
            // 1-minute cooldown only froze the pool bot - COOLDOWN still counts
            // as active and the request gate refuses every new pick meanwhile
            // (live 2026-10-09: 1-3 min idle after each hand-in). Expire it so
            // the next tick picks the next errand. Owned bots keep the cooldown.
            if (destinationInactive && sRandomBotFacade.IsRandomBot(bot) && !ai->HasRealPlayerMaster() &&
                (tDestination->GetPurpose() == TravelDestinationPurpose::QuestGiver ||
                 tDestination->GetPurpose() == TravelDestinationPurpose::QuestTaker ||
                 static_cast<uint32>(tDestination->GetPurpose()) & static_cast<uint32>(TravelDestinationPurpose::QuestAllObjective)))
            {
                ai->TellDebug(ai->GetMaster(), "The target is expiring because its quest errand is done.", "debug travel");
                forced = false;
                SetStatus(TravelStatus::TRAVEL_STATUS_EXPIRED);
                return;
            }
            // A gather trip that went inactive before arrival reached an empty
            // node (looted, tapped, despawned): the static destination data
            // still offers it, so the 1-minute cooldown only stands the pool
            // bot at nothing - COOLDOWN still counts as active and the request
            // gate refuses every new pick meanwhile (live 2026-10-09: ~17% of
            // gather-stalled bot-time sits in cooldown a median 57 yd out).
            // Expire it so the next tick walks a live node; the dead one is
            // skipped (SetBestTarget requires IsActive). An arrived trip
            // (WORK) keeps today's behaviour. Owned bots keep the cooldown.
            if (destinationInactive && GetStatus() == TravelStatus::TRAVEL_STATUS_TRAVEL &&
                sRandomBotFacade.IsRandomBot(bot) && !ai->HasRealPlayerMaster() &&
                (tDestination->GetPurpose() == TravelDestinationPurpose::GatherMining ||
                 tDestination->GetPurpose() == TravelDestinationPurpose::GatherHerbalism))
            {
                ai->TellDebug(ai->GetMaster(), "The target is expiring because its gather node is gone.", "debug travel");
                forced = false;
                SetStatus(TravelStatus::TRAVEL_STATUS_EXPIRED);
                return;
            }

            ai->TellDebug(ai->GetMaster(), "The target is cooling down because the destination was no longer active or the conditions are no longer true.", "debug travel");
            forced = false;
            // A trip that never arrived must not re-request on expiry: the
            // kind blacklist only covers one creature kind and the 6-fail
            // drop never fires (the 60 s cooldown expires first), so without
            // a park the same zone is re-picked every ~2.5 min from the same
            // standstill (night2 capital-loop bots). Park the purpose like a
            // drop (same keys the request gate reads); other purposes keep
            // working. An arrived trip (WORK) keeps today's behaviour.
            // Grind only: a quest/vendor trip also cools down when its errand
            // completes en route, and parking those would stall questing.
            if (tDestination->GetPurpose() == TravelDestinationPurpose::Grind &&
                ai::TravelCooldownParksPurpose(sRandomBotFacade.IsRandomBot(bot) && !ai->HasRealPlayerMaster(),
                GetStatus() == TravelStatus::TRAVEL_STATUS_TRAVEL))
            {
                std::string const parkKey = std::to_string(static_cast<uint32>(tDestination->GetPurpose()));
                context->GetValue<bool>("no active travel destinations", parkKey)->Set(true);
                context->GetValue<time_t>("manual time", "no travel purpose until::" + parkKey)->Set(time(0) + ai::TRAVEL_COOLDOWN_PARK_SECONDS);
            }
            SetStatus(TravelStatus::TRAVEL_STATUS_COOLDOWN);
            return;
        }
    }

    // Empty-destination WORK release (workidle): a masterless pool bot that
    // arrived where there is nothing to do holds WORK with the destination
    // verdict still green (static data, not the live scene), blocking the
    // next request and the idle drift until WORK expires (~5 min). Past the
    // horizon with no attackable grind prey and nobody fighting the bot the
    // target expires, so the next visit requests a new one - the donor
    // mod-playerbots shape (GO_GRIND/WANDER_NPC return to IDLE with no
    // target instead of holding). Both signals are already-cached engine
    // reads ("grind target" 2 s, core attacker set + combat flag free), so
    // no DB hit, world scan or graph rebuild here. Pool upkeep bots only;
    // owned/hired bots keep the full WORK clock.
    if (GetStatus() == TravelStatus::TRAVEL_STATUS_WORK &&
        !IsForced() && !IsGroupCopy() &&
        sRandomBotFacade.IsRandomBot(bot) && !ai->HasRealPlayerMaster())
    {
        AiObjectContext* workContext = ai->GetAiObjectContext();
        Unit* grindPrey = workContext->GetValue<Unit*>("grind target")->Get();
        bool const hasAttackers = !bot->GetAttackers().empty() || bot->IsInCombat();
        time_t const now = time(0);
        time_t anchor = workContext->GetValue<time_t>("manual time", ai::WorkIdleAnchorKey())->Get();
        anchor = ai::WorkIdleAnchor(anchor, now, grindPrey != nullptr, hasAttackers);
        workContext->GetValue<time_t>("manual time", ai::WorkIdleAnchorKey())->Set(anchor);
        if (ai::WorkIdleStale(anchor, now, grindPrey != nullptr, hasAttackers))
        {
            ai->TellDebug(ai->GetMaster(), "The target is expiring because there is nothing to do here.", "debug travel");
            sPlayerbotAIConfig.logEvent(ai, "WorkIdleStale", tDestination ? tDestination->GetShortName() : "unknown");
            workContext->GetValue<time_t>("manual time", ai::WorkIdleAnchorKey())->Set(time_t(0));
            SetStatus(TravelStatus::TRAVEL_STATUS_EXPIRED);
            return;
        }
    }
}

bool TravelTarget::IsActive() {
    if (m_status == TravelStatus::TRAVEL_STATUS_NONE || m_status == TravelStatus::TRAVEL_STATUS_EXPIRED || m_status == TravelStatus::TRAVEL_STATUS_PREPARE)
        return false;

    return true;
};

TravelState TravelTarget::GetTravelState() {
    if (!tDestination || typeid(*tDestination) == typeid(NullTravelDestination))
        return TravelState::TRAVEL_STATE_IDLE;

    if (typeid(*tDestination) == typeid(QuestRelationTravelDestination))
    {
        if (((QuestRelationTravelDestination*)tDestination)->GetRelation() == 0)
        {
            if (GetStatus() == TravelStatus::TRAVEL_STATUS_TRAVEL || GetStatus() == TravelStatus::TRAVEL_STATUS_PREPARE)
                return TravelState::TRAVEL_STATE_TRAVEL_PICK_UP_QUEST;
            if (GetStatus() == TravelStatus::TRAVEL_STATUS_WORK)
                return TravelState::TRAVEL_STATE_WORK_PICK_UP_QUEST;
        }
        else
        {
            if (GetStatus() == TravelStatus::TRAVEL_STATUS_TRAVEL || GetStatus() == TravelStatus::TRAVEL_STATUS_PREPARE)
                return TravelState::TRAVEL_STATE_TRAVEL_HAND_IN_QUEST;
            if (GetStatus() == TravelStatus::TRAVEL_STATUS_WORK)
                return TravelState::TRAVEL_STATE_WORK_HAND_IN_QUEST;
        }
    }
    else if (typeid(*tDestination) == typeid(QuestObjectiveTravelDestination))
    {
        if (GetStatus() == TravelStatus::TRAVEL_STATUS_TRAVEL || GetStatus() == TravelStatus::TRAVEL_STATUS_PREPARE)
            return TravelState::TRAVEL_STATE_TRAVEL_DO_QUEST;
        if (GetStatus() == TravelStatus::TRAVEL_STATUS_WORK)
            return TravelState::TRAVEL_STATE_WORK_DO_QUEST;
    }
    else if (typeid(*tDestination) == typeid(RpgTravelDestination))
    {
        return TravelState::TRAVEL_STATE_TRAVEL_RPG;
    }
    else if (typeid(*tDestination) == typeid(ExploreTravelDestination))
    {
        return TravelState::TRAVEL_STATE_TRAVEL_EXPLORE;
    }

    return TravelState::TRAVEL_STATE_IDLE;
}

void TravelMgr::Clear()
{
    HashMapHolder<Player>::ReadGuard g(HashMapHolder<Player>::GetLock());
    HashMapHolder<Player>::MapType& m = sObjectAccessor.GetPlayers();
    for (HashMapHolder<Player>::MapType::iterator itr = m.begin(); itr != m.end(); ++itr)
        TravelMgr::SetNullTravelTarget(itr->second);
    for (auto& [purpose, entries] : destinationMap)
        for (auto& [id, dests] : entries)
            for (auto& dest : dests)
                delete dest;
    destinationMap.clear();
    pointsMap.clear();
    fishPoints.clear();
    taxiNodeZones.clear();
}

int32 TravelMgr::GetAreaLevel(uint32 area_id)
{
    auto lev = areaLevels.find(area_id);

    if (lev != areaLevels.end())
        return lev->second;

    AreaTableEntry const* area = GetAreaEntryByAreaID(area_id);

    if (!area)
    {
        areaLevels[area_id] = -2;
        return -2;
    }

    //Get exploration level
    if (area->AreaLevel)
    {
        areaLevels[area_id] = area->AreaLevel;
        return area->AreaLevel;
    }


    int32 level = 0;
    uint32 cnt = 0;

    //Get sub-area's
    for (uint32 i = 0; i < sAreaStore.GetMaxEntry(); i++)
    {
        AreaTableEntry const* subArea = GetAreaEntryByAreaID(i);

        if (!subArea || subArea->ZoneId != area->Id)
            continue;

        int32 subLevel = GetAreaLevel(subArea->Id);

        if (!subLevel)
            continue;

        level += subLevel;

        cnt++;
    }

    if (cnt)
    {
        areaLevels[area_id] = std::max(uint32(1), level / cnt);
        return areaLevels[area_id];
    }

    //Get units avarage
    FactionTemplateEntry const* humanFaction = sFactionTemplateStore.LookupEntry(1);
    FactionTemplateEntry const* orcFaction = sFactionTemplateStore.LookupEntry(2);

    for (auto& creaturePair : WorldPosition().GetCreaturesNear())
    {
        if (WorldPosition(creaturePair).GetArea() != area)
            continue;

        CreatureData const cData = creaturePair->second;
        CreatureInfo const* cInfo = sObjectMgr.GetCreatureTemplate(cData.creature_id[0]);

        if (!cInfo)
            continue;

        FactionTemplateEntry const* factionEntry = sFactionTemplateStore.LookupEntry(cInfo->faction);
        ReputationRank reactionHum = PlayerbotAI::GetFactionReaction(humanFaction, factionEntry);
        ReputationRank reactionOrc = PlayerbotAI::GetFactionReaction(orcFaction, factionEntry);

        if (reactionHum > REP_NEUTRAL || reactionOrc > REP_NEUTRAL)
            continue;

        level += cInfo->level_max;
        cnt++;
    }

    if (cnt)
    {
        areaLevels[area_id] = std::max(uint32(1),level / cnt);
        return areaLevels[area_id];
    }

    //Use parent zone value.
    if (area->ZoneId)
    {
        areaLevels[area_id] = 0; //Set a temporary value so it wont be counted.
        level = GetAreaLevel(area->ZoneId);
        areaLevels[area_id] = level;
        return areaLevels[area_id];
    }

    areaLevels[area_id] = -1;

    return areaLevels[area_id];
}

bool TravelMgr::TryGetValidatedAreaLevel(uint32 areaId, int32& outLevel) const
{
    auto it = areaLevels.find(areaId);
    if (it != areaLevels.end() && it->second > 0)
    {
        outLevel = it->second;
        return true;
    }
    AreaTableEntry const* area = GetAreaEntryByAreaID(areaId);
    if (!area)
        return false;
    if (area->ZoneId)
    {
        auto pit = areaLevels.find(area->ZoneId);
        if (pit != areaLevels.end() && pit->second > 0)
        {
            outLevel = pit->second;
            return true;
        }
    }
    if (area->AreaLevel > 0)
    {
        outLevel = area->AreaLevel;
        return true;
    }
    if (area->ZoneId)
    {
        if (AreaTableEntry const* parent = GetAreaEntryByAreaID(area->ZoneId))
            if (parent->AreaLevel > 0)
            {
                outLevel = parent->AreaLevel;
                return true;
            }
    }
    return false;
}

void TravelMgr::LoadTaxiNodeZones()
{
	if (!taxiNodeZones.empty())
		return;

	// One pass over the DBC taxi-node table at startup, while the world is
	// still loading: resolves each node's zone id from loaded terrain and
	// caches it, so travel ticks never call WorldPosition::GetArea (which
	// loads that tile's vmap on the world thread). Terrain lookups here are
	// in-memory grid reads; a node whose tile is not loaded yet records 0
	// and fails closed at use (no flight through an unknown zone).
	uint32 const maxNode = sObjectMgr.GetMaxTaxiNodeId();
	for (uint32 nodeId = 1; nodeId < maxNode; ++nodeId)
	{
		TaxiNodesEntry const* node = sObjectMgr.GetTaxiNodeEntry(nodeId);
		if (!node)
			continue;

		uint32 zoneId = 0;
		uint32 areaId = 0;
		sTerrainMgr.GetZoneAndAreaId(zoneId, areaId, node->map_id, node->x, node->y, node->z);
		taxiNodeZones[nodeId] = zoneId;
	}

	sLog.outString(">> Loaded " SIZEFMTD " taxi node zones.", taxiNodeZones.size());
}

uint32 TravelMgr::GetTaxiNodeZoneId(uint32 taxiNodeId) const
{
	auto it = taxiNodeZones.find(taxiNodeId);
	return it != taxiNodeZones.end() ? it->second : 0;
}

void TravelMgr::LoadAreaLevels()
{
    if (!areaLevels.empty())
        return;

    // The module migration owns this table. Do not create or bulk-populate it
    // during world startup: a missing migration must be visible, and an empty
    // cache should fall back to the in-memory area-level calculation instead of
    // issuing one INSERT per DBC area on the world thread.
    // Login scatter uses TryGetValidatedAreaLevel which adds a bounded DBC
    // AreaTable AreaLevel / parent fallback, so an empty ai_playerbot_zone_level
    // on a stock install does not make EnableRandomTeleports permanently no-op
    // while still avoiding creature scans and DB writes.
    if (!WorldDatabase.PQuery("SHOW TABLES LIKE 'ai_playerbot_zone_level'"))
    {
        sLog.outErrorDb("TortoiseBots: ai_playerbot_zone_level is missing; using uncached area levels");
        return;
    }

    auto result = WorldDatabase.PQuery("SELECT id, level FROM ai_playerbot_zone_level");
    if (!result)
    {
        sLog.outString(">> No cached area levels found; using in-memory calculation.");
        return;
    }

    BarGoLink bar(result->GetRowCount());
    do
    {
        Field* fields = result->Fetch();
        bar.step();
        areaLevels[fields[0].GetUInt32()] = fields[1].GetInt32();
    } while (result->NextRow());

    sLog.outString(">> Loaded " SIZEFMTD " area levels.", areaLevels.size());
}

void TravelMgr::LoadQuestTravelTable()
{
    if (!sTravelMgr.destinationMap.empty())
        return;

    // Clearing store (for reloading case)
    Clear();

    sLog.outString("Loading trainable spells.");
    GAI_VALUE(trainableSpellMap*, "trainable spell map");
    GAI_VALUE(std::vector<MountValue>, "full mount list");

    sLog.outString("Loading object locations.");

    EntryGuidps const& guidpMap = GAI_VALUE_REF(EntryGuidps, "entry guidps");

    sLog.outString("Finding possible travel destinations.");

    EntryQuestRelationMap const& eMap = GAI_VALUE_REF(EntryQuestRelationMap, "entry quest relation");

    sLog.outString("Creating travel destinations.");

    BarGoLink bar(eMap.size());

    for (auto const& [entry, relation] : eMap)
    {


        bar.step();
        for (auto const& [questId, flag] : relation)
        {
            if (guidpMap.find(entry) == guidpMap.end())
            {
                sLog.outDebug("Entry %d for quest %d has no valid location.", entry, questId);
                continue;
            }

            std::vector<QuestTravelDestination*> locs;

            for (uint32 purposeFlagNr = 0; purposeFlagNr < 6; purposeFlagNr++)
            {
                TravelDestinationPurpose purposeFlag = (TravelDestinationPurpose)(1 << purposeFlagNr);
                if (flag & (uint32)purposeFlag)
                {
                    QuestTravelDestination* loc = nullptr;

                    if (purposeFlag == TravelDestinationPurpose::QuestGiver || purposeFlag == TravelDestinationPurpose::QuestTaker)
                        loc = AddDestination<QuestRelationTravelDestination>(entry, purposeFlag, questId);
                    else
                        loc = AddDestination<QuestObjectiveTravelDestination>(entry, purposeFlag, questId);

                    locs.push_back(loc);
                }
            }

            if (!locs.empty())
            {
                for (auto const& guidP : guidpMap.at(entry))
                {
                    pointsMap.insert(std::make_pair(guidP.GetRawValue(), guidP));

                    for (auto tLoc : locs)
                    {
                        tLoc->AddPoint(&pointsMap.at(guidP.GetRawValue()));
                    }
                }
            }
        }
    }

    sLog.outString("Loading all travel locations.");

    for (auto const& [entry, purpose] : GAI_VALUE_REF(EntryTravelPurposeMap, "entry travel purpose"))
    {
        std::vector<TravelDestination*> dests;

        if (guidpMap.find(entry) == guidpMap.end())
            continue;

        for (uint32 purposeFlagNr = 6; purposeFlagNr < 18; purposeFlagNr++)
        {
            TravelDestinationPurpose purposeFlag = (TravelDestinationPurpose)(1 << purposeFlagNr);
            if (purpose & (uint32)purposeFlag)
            {
                switch (purposeFlag) {
                case TravelDestinationPurpose::GenericRpg:
                case TravelDestinationPurpose::Trainer:
                case TravelDestinationPurpose::Repair:
                case TravelDestinationPurpose::Vendor:
                case TravelDestinationPurpose::AH:
                case TravelDestinationPurpose::Mail:
                    dests.push_back(AddDestination<RpgTravelDestination>(entry, purposeFlag));
                    break;
                case TravelDestinationPurpose::GatherSkinning:
                case TravelDestinationPurpose::GatherMining:
                case TravelDestinationPurpose::GatherHerbalism:
                    dests.push_back(AddDestination<GatherTravelDestination>(entry, purposeFlag));
                    break;
                case TravelDestinationPurpose::Grind:
                    dests.push_back(AddDestination<GrindTravelDestination>(entry, purposeFlag));
                    break;
                case TravelDestinationPurpose::Boss:
                    dests.push_back(AddDestination<BossTravelDestination>(entry, purposeFlag));
                    break;
                default:
                    break;
                };
            }
        }

        if (dests.empty())
            continue;

        for (auto& guidP : guidpMap.at(entry))
        {
            pointsMap.insert(std::make_pair(guidP.GetRawValue(), guidP));

            for (auto tLoc : dests)
            {
                tLoc->AddPoint(&pointsMap.at(guidP.GetRawValue()));
            }
        }
    }

    sLog.outString("Loading Explore locations.");

    //Explore points
    for (auto& creatureDataPair : WorldPosition().GetCreaturesNear())
    {
        ExploreTravelDestination* loc;

        AsyncGuidPosition point(creatureDataPair);

        if (!point.isValid())
            continue;

        AreaTableEntry const* area = point.GetArea();

        if (!area)
            continue;

        if (!area->ExploreFlag)
            continue;

        point.FetchArea();

        pointsMap.insert_or_assign(point.GetRawValue(), point);

        loc = AddDestination<ExploreTravelDestination>(area->Id, TravelDestinationPurpose::Explore);
        loc->AddPoint(&pointsMap.at(point.GetRawValue()));
    }

    GetPopulatedGrids();

    //Analyse log files
    if (sPlayerbotAIConfig.hasLog("log_analysis.csv"))
    {
        sLog.outString("Running analysis.");
    }

    sLog.outString("Opening log files (previous run retained as .1).");

     //Clear these logs files
     // Start fresh per-run logs; openLog preserves the previous run.
    sPlayerbotAIConfig.openLog("zones.csv", "w");
    sPlayerbotAIConfig.openLog("creatures.csv", "w");
    sPlayerbotAIConfig.openLog("gos.csv", "w");
    sPlayerbotAIConfig.openLog("bot_movement.csv", "w");
    sPlayerbotAIConfig.openLog("bot_pathfinding.csv", "w");
    sPlayerbotAIConfig.openLog("pathfind_attempt.csv", "w");
    sPlayerbotAIConfig.openLog("pathfind_attempt_point.csv", "w");
    sPlayerbotAIConfig.openLog("pathfind_result.csv", "w");
    sPlayerbotAIConfig.openLog("load_map_grid.csv", "w");
    sPlayerbotAIConfig.openLog("strategy.csv", "w");

    sPlayerbotAIConfig.openLog("unload_grid.csv", "w");
    sPlayerbotAIConfig.openLog("unload_obj.csv", "w");
    sPlayerbotAIConfig.openLog("bot_events.csv", "w");
    sPlayerbotAIConfig.openLog("travel_map.csv", "w");
    sPlayerbotAIConfig.openLog("quest_map.csv", "w");
    // deaths.csv is an event history, not a per-run dump like the files above:
    // opening it in append keeps every death across a pool reset (and across a
    // full restart when the file already exists), so before/after comparisons
    // stay possible.
    sPlayerbotAIConfig.openLog("deaths.csv", "a");
    sPlayerbotAIConfig.openLog("player_paths.csv", "w");
    sPlayerbotAIConfig.openLog("travel_destinations.csv", "w");
    sPlayerbotAIConfig.openLog("deadzone.csv", "w");
    sPlayerbotAIConfig.openLog("bot_test_results.log", "w", true);


    sLog.outString("Loading travel nodes.");

    sTravelNodeMap.loadNodeStore();
    sTravelNodeMap.generateAll();
    sTravelNodeMap.printMap();
    sTravelNodeMap.printNodeStore();
    if (sPlayerbotAIConfig.generateTravelNodes)
        sTravelNodeMap.saveNodeStore();

    LoadFishLocations();

    //Creature/gos/zone export.
    if (sPlayerbotAIConfig.hasLog("creatures.csv"))
    {
        sLog.outString("Create creature overlay exports.");

        for (auto& creaturePair : WorldPosition().GetCreaturesNear())
        {
            CreatureData const cData = creaturePair->second;
            CreatureInfo const* cInfo = sObjectMgr.GetCreatureTemplate(cData.creature_id[0]);

            if (!cInfo)
                continue;

            WorldPosition point = WorldPosition(cData.position.mapId, cData.position.x, cData.position.y, cData.position.z, cData.position.o);

            std::string name = cInfo->name;
            name.erase(remove(name.begin(), name.end(), ','), name.end());
            name.erase(remove(name.begin(), name.end(), '\"'), name.end());

            std::ostringstream out;
            out << name << ",";
            point.printWKT(out);
            out << cInfo->level_max << ",";
            out << cInfo->rank << ",";
            out << cInfo->faction << ",";
            out << cInfo->npc_flags << ",";
            out << point.GetAreaName() << ",";
            out << std::fixed;

            sPlayerbotAIConfig.log("creatures.csv", out.str().c_str());
        }
    }

    if (sPlayerbotAIConfig.hasLog("vmangoslines.csv"))
    {

        uint32 mapId = 0;
        std::vector<WorldPosition> pos;

            static float const topNorthSouthLimit[] = {
                2032.048340f, -6927.750000f,
                1634.863403f, -6157.505371f,
                1109.519775f, -5181.036133f,
                1315.204712f, -4096.020508f,
                1073.089233f, -3372.571533f,
                 825.833191f, -3125.778809f,
                 657.343994f, -2314.813232f,
                 424.736145f, -1888.283691f,
                 744.395813f, -1647.935425f,
                1424.160645f,  -654.948181f,
                1447.065308f,  -169.751358f,
                1208.715454f,   189.748703f,
                1596.240356f,   998.616699f,
                1577.923706f,  1293.419922f,
                1458.520264f,  1727.373291f,
                1591.916138f,  3728.139404f
            };

            pos.clear();

# define my_sizeof(type) ((char *)(&type+1)-(char*)(&type))

            int size = my_sizeof(topNorthSouthLimit) / my_sizeof(topNorthSouthLimit[0]);

            for (int32 i = 0; i < size-1; i=i+2)
            {
                if (topNorthSouthLimit[i] == 0)
                    break;
                pos.push_back(WorldPosition(mapId, topNorthSouthLimit[i], topNorthSouthLimit[i + 1], 0));
            }

            std::ostringstream out;
            out << "topNorthSouthLimit" << ",";
            WorldPosition().printWKT(pos,out,1);
            out << std::fixed;

            sPlayerbotAIConfig.log("vmangoslines.csv", out.str().c_str());

            static float const ironforgeAreaSouthLimit[] = {
                -7491.33f,  3093.74f,
                -7472.04f,  -391.88f,
                -6366.68f,  -730.10f,
                -6063.96f, -1411.76f,
                -6087.62f, -2190.21f,
                -6349.54f, -2533.66f,
                -6308.63f, -3049.32f,
                -6107.82f, -3345.30f,
                -6008.49f, -3590.52f,
                -5989.37f, -4312.29f,
                -5806.26f, -5864.11f
            };

            pos.clear();

            size = my_sizeof(ironforgeAreaSouthLimit) / my_sizeof(ironforgeAreaSouthLimit[0]);

            for (int32 i = 0; i < size - 1; i = i + 2)
            {
                if (ironforgeAreaSouthLimit[i] == 0)
                    break;
                pos.push_back(WorldPosition(mapId, ironforgeAreaSouthLimit[i], ironforgeAreaSouthLimit[i + 1], 0));
            }

            out.str("");
            out.clear();

            out << "ironforgeAreaSouthLimit" << ",";
            WorldPosition().printWKT(pos, out, 1);
            out << std::fixed;

            sPlayerbotAIConfig.log("vmangoslines.csv", out.str().c_str());

            static float const stormwindAreaNorthLimit[] = {
                 -8004.25f,  3714.11f,
                 -8075.00f, -179.00f,
                 -8638.00f, 169.00f,
                 -9044.00f, 35.00f,
                 -9068.00f, -125.00f,
                 -9094.00f, -147.00f,
                 -9206.00f, -290.00f,
                 -9097.00f, -510.00f,
                 -8739.00f, -501.00f,
                 -8725.50f, -1618.45f,
                 -9810.40f, -1698.41f,
                -10049.60f, -1740.40f,
                -10670.61f, -1692.51f,
                -10908.48f, -1563.87f,
                -13006.40f, -1622.80f,
                -12863.23f, -4798.42f
            };

            pos.clear();

            size = my_sizeof(stormwindAreaNorthLimit) / my_sizeof(stormwindAreaNorthLimit[0]);

            for (int32 i = 0; i < size - 1; i = i + 2)
            {
                if (stormwindAreaNorthLimit[i] == 0)
                    break;
                pos.push_back(WorldPosition(mapId, stormwindAreaNorthLimit[i], stormwindAreaNorthLimit[i + 1], 0));
            }

            out.str("");
            out.clear();

            out << "stormwindAreaNorthLimit" << ",";
            WorldPosition().printWKT(pos, out, 1);
            out << std::fixed;

            sPlayerbotAIConfig.log("vmangoslines.csv", out.str().c_str());

            static float const stormwindAreaSouthLimit[] = {
                 -8725.337891f,  3535.624023f,
                 -9525.699219f,   910.132568f,
                 -9796.953125f,   839.069580f,
                 -9946.341797f,   743.102844f,
                -10287.361328f,   760.076477f,
                -10083.828125f,   380.389893f,
                -10148.072266f,    80.056450f,
                -10014.583984f,  -161.638519f,
                 -9978.146484f,  -361.638031f,
                 -9877.489258f,  -563.304871f,
                 -9980.967773f, -1128.510498f,
                 -9991.717773f, -1428.793213f,
                 -9887.579102f, -1618.514038f,
                -10169.600586f, -1801.582031f,
                 -9966.274414f, -2227.197754f,
                 -9861.309570f, -2989.841064f,
                 -9944.026367f, -3205.886963f,
                 -9610.209961f, -3648.369385f,
                 -7949.329590f, -4081.389404f,
                 -7910.859375f, -5855.578125f
            };

            pos.clear();

            size = my_sizeof(stormwindAreaSouthLimit) / my_sizeof(stormwindAreaSouthLimit[0]);

            for (int32 i = 0; i < size - 1; i = i + 2)
            {
                if (stormwindAreaSouthLimit[i] == 0)
                    break;
                pos.push_back(WorldPosition(mapId, stormwindAreaSouthLimit[i], stormwindAreaSouthLimit[i + 1], 0));
            }

            out.str("");
            out.clear();

            out << "stormwindAreaSouthLimit" << ",";
            WorldPosition().printWKT(pos, out, 1);
            out << std::fixed;

            sPlayerbotAIConfig.log("vmangoslines.csv", out.str().c_str());

            mapId = 1;

            static float const teldrassilSouthLimit[] = {
            7916.0f,   3475.0f,
            7916.0f,   1000.0f,
            8283.0f,   -501.0f,
            8804.0f,   -1098.0f
            };

            pos.clear();

            size = my_sizeof(teldrassilSouthLimit) / my_sizeof(teldrassilSouthLimit[0]);

            for (int32 i = 0; i < size - 1; i = i + 2)
            {
                if (teldrassilSouthLimit[i] == 0)
                    break;
                pos.push_back(WorldPosition(mapId, teldrassilSouthLimit[i], teldrassilSouthLimit[i + 1], 0));
            }

            out.str("");
            out.clear();

            out << "teldrassilSouthLimit" << ",";
            WorldPosition().printWKT(pos, out, 1);
            out << std::fixed;

            sPlayerbotAIConfig.log("vmangoslines.csv", out.str().c_str());

            static float const northMiddleLimit[] = {
                  -2280.00f,  4054.00f,
                  -2401.00f,  2365.00f,
                  -2432.00f,  1338.00f,
                  -2286.00f,   769.00f,
                  -2137.00f,   662.00f,
                  -2044.54f,   489.86f,
                  -1808.52f,   436.39f,
                  -1754.85f,   504.55f,
                  -1094.55f,   651.75f,
                   -747.46f,   647.73f,
                   -685.55f,   408.43f,
                   -311.38f,   114.43f,
                   -358.40f,  -587.42f,
                   -377.92f,  -748.70f,
                   -512.57f,  -919.49f,
                   -280.65f, -1008.87f,
                    -81.29f,  -930.89f,
                    284.31f, -1105.39f,
                    568.86f,  -892.28f,
                   1211.09f, -1135.55f,
                    879.60f, -2110.18f,
                    788.96f, -2276.02f,
                    899.68f, -2625.56f,
                   1281.54f, -2689.42f,
                   1521.82f, -3047.85f,
                   1424.22f, -3365.69f,
                   1694.11f, -3615.20f,
                   2373.78f, -4019.96f,
                   2388.13f, -5124.35f,
                   2193.79f, -5484.38f,
                   1703.57f, -5510.53f,
                   1497.59f, -6376.56f,
                   1368.00f, -8530.00f
            };

            pos.clear();

            size = my_sizeof(northMiddleLimit) / my_sizeof(northMiddleLimit[0]);

            for (int32 i = 0; i < size - 1; i = i + 2)
            {
                if (northMiddleLimit[i] == 0)
                    break;
                pos.push_back(WorldPosition(mapId, northMiddleLimit[i], northMiddleLimit[i + 1], 0));
            }

            out.str("");
            out.clear();

            out << "northMiddleLimit" << ",";
            WorldPosition().printWKT(pos, out, 1);
            out << std::fixed;

            sPlayerbotAIConfig.log("vmangoslines.csv", out.str().c_str());

            static float const durotarSouthLimit[] = {
                    2755.00f, -3766.00f,
                    2225.00f, -3596.00f,
                    1762.00f, -3746.00f,
                    1564.00f, -3943.00f,
                    1184.00f, -3915.00f,
                     737.00f, -3782.00f,
                     -75.00f, -3742.00f,
                    -263.00f, -3836.00f,
                    -173.00f, -4064.00f,
                     -81.00f, -4091.00f,
                     -49.00f, -4089.00f,
                     -16.00f, -4187.00f,
                      -5.00f, -4192.00f,
                     -14.00f, -4551.00f,
                    -397.00f, -4601.00f,
                    -522.00f, -4583.00f,
                    -668.00f, -4539.00f,
                    -790.00f, -4502.00f,
                   -1176.00f, -4213.00f,
                   -1387.00f, -4674.00f,
                   -2243.00f, -6046.00f
            };

            pos.clear();

            size = my_sizeof(durotarSouthLimit) / my_sizeof(durotarSouthLimit[0]);

            for (int32 i = 0; i < size - 1; i = i + 2)
            {
                if (durotarSouthLimit[i] == 0)
                    break;
                pos.push_back(WorldPosition(mapId, durotarSouthLimit[i], durotarSouthLimit[i + 1], 0));
            }

            out.str("");
            out.clear();

            out << "durotarSouthLimit" << ",";
            WorldPosition().printWKT(pos, out, 1);
            out << std::fixed;

            sPlayerbotAIConfig.log("vmangoslines.csv", out.str().c_str());

            static float const valleyoftrialsSouthLimit[] = {
                    -324.00f, -3869.00f,
                    -774.00f, -3992.00f,
                    -965.00f, -4290.00f,
                    -932.00f, -4349.00f,
                    -828.00f, -4414.00f,
                    -661.00f, -4541.00f,
                    -521.00f, -4582.00f
            };

            pos.clear();

            size = my_sizeof(valleyoftrialsSouthLimit) / my_sizeof(valleyoftrialsSouthLimit[0]);

            for (int32 i = 0; i < size - 1; i = i + 2)
            {
                if (valleyoftrialsSouthLimit[i] == 0)
                    break;
                pos.push_back(WorldPosition(mapId, valleyoftrialsSouthLimit[i], valleyoftrialsSouthLimit[i + 1], 0));
            }

            out.str("");
            out.clear();

            out << "valleyoftrialsSouthLimit" << ",";
            WorldPosition().printWKT(pos, out, 1);
            out << std::fixed;

            sPlayerbotAIConfig.log("vmangoslines.csv", out.str().c_str());

            static float const middleToSouthLimit[] = {
                        -2402.01f,      4255.70f,
                    -2475.933105f,  3199.568359f, // Desolace
                    -2344.124023f,  1756.164307f,
                    -2826.438965f,   403.824738f, // Mulgore
                    -3472.819580f,   182.522476f, // Feralas
                    -4365.006836f, -1602.575439f, // the Barrens
                    -4515.219727f, -1681.356079f,
                    -4543.093750f, -1882.869385f, // Thousand Needles
                        -4824.16f,     -2310.11f,
                    -5102.913574f, -2647.062744f,
                    -5248.286621f, -3034.536377f,
                    -5246.920898f, -3339.139893f,
                    -5459.449707f, -4920.155273f, // Tanaris
                        -5437.00f,     -5863.00f
            };

            pos.clear();

            size = my_sizeof(middleToSouthLimit) / my_sizeof(middleToSouthLimit[0]);

            for (int32 i = 0; i < size - 1; i = i + 2)
            {
                if (middleToSouthLimit[i] == 0)
                    break;
                pos.push_back(WorldPosition(mapId, middleToSouthLimit[i], middleToSouthLimit[i + 1], 0));
            }

            out.str("");
            out.clear();

            out << "middleToSouthLimit" << ",";
            WorldPosition().printWKT(pos, out, 1);
            out << std::fixed;

            sPlayerbotAIConfig.log("vmangoslines.csv", out.str().c_str());

            static float const orgrimmarSouthLimit[] = {
                    2132.5076f, -3912.2478f,
                    1944.4298f, -3855.2583f,
                    1735.6906f, -3834.2417f,
                    1654.3671f, -3380.9902f,
                    1593.9861f, -3975.5413f,
                    1439.2548f, -4249.6923f,
                    1436.3106f, -4007.8950f,
                    1393.3199f, -4196.0625f,
                    1445.2428f, -4373.9052f,
                    1407.2349f, -4429.4145f,
                    1464.7142f, -4545.2875f,
                    1584.1331f, -4596.8764f,
                    1716.8065f, -4601.1323f,
                    1875.8312f, -4788.7187f,
                    1979.7647f, -4883.4585f,
                    2219.1562f, -4854.3330f
            };

            pos.clear();

            size = my_sizeof(orgrimmarSouthLimit) / my_sizeof(orgrimmarSouthLimit[0]);

            for (int32 i = 0; i < size - 1; i = i + 2)
            {
                if (orgrimmarSouthLimit[i] == 0)
                    break;
                pos.push_back(WorldPosition(mapId, orgrimmarSouthLimit[i], orgrimmarSouthLimit[i + 1], 0));
            }

            out.str("");
            out.clear();

            out << "orgrimmarSouthLimit" << ",";
            WorldPosition().printWKT(pos, out, 1);
            out << std::fixed;

            sPlayerbotAIConfig.log("vmangoslines.csv", out.str().c_str());

            static float const feralasThousandNeedlesSouthLimit[] = {
                    -6495.4995f, -4711.981f,
                    -6674.9995f, -4515.0019f,
                    -6769.5717f, -4122.4272f,
                    -6838.2651f, -3874.2792f,
                    -6851.1314f, -3659.1179f,
                    -6624.6845f, -3063.3843f,
                    -6416.9067f, -2570.1301f,
                    -5959.8466f, -2287.2634f,
                    -5947.9135f, -1866.5028f,
                    -5947.9135f,  -820.4881f,
                    -5876.7114f,    -3.5138f,
                    -5876.7114f,   917.6407f,
                    -6099.3603f,  1153.2884f,
                    -6021.8989f,  1638.1809f,
                    -6091.6176f,  2335.8892f,
                    -6744.9946f,  2393.4855f,
                    -6973.8608f,  3077.0281f,
                    -7068.7241f,  4376.2304f,
                    -7142.1211f,  4808.4331f
            };


            pos.clear();

            size = my_sizeof(feralasThousandNeedlesSouthLimit) / my_sizeof(feralasThousandNeedlesSouthLimit[0]);

            for (int32 i = 0; i < size - 1; i = i + 2)
            {
                if (feralasThousandNeedlesSouthLimit[i] == 0)
                    break;
                pos.push_back(WorldPosition(mapId, feralasThousandNeedlesSouthLimit[i], feralasThousandNeedlesSouthLimit[i + 1], 0));
            }

            out.str("");
            out.clear();

            out << "feralasThousandNeedlesSouthLimit" << ",";
            WorldPosition().printWKT(pos, out, 1);
            out << std::fixed;

            sPlayerbotAIConfig.log("vmangoslines.csv", out.str().c_str());

    }

    if (sPlayerbotAIConfig.hasLog("gos.csv"))
    {
        sLog.outString("Create go overlay exports.");
        for (auto& gameObjectPair : WorldPosition().GetGameObjectsNear())
        {
            GameObjectData const gData = gameObjectPair->second;
            auto data = sGOStorage.LookupEntry<GameObjectInfo>(gData.id);

            if (!data)
                continue;

            WorldPosition point = WorldPosition(gData.position.mapId, gData.position.x, gData.position.y, gData.position.z, gData.position.o);

            std::string name = data->name;
            name.erase(remove(name.begin(), name.end(), ','), name.end());
            name.erase(remove(name.begin(), name.end(), '\"'), name.end());

            std::ostringstream out;
            out << name << ",";
            point.printWKT(out);
            out << data->type << ",";
            out << point.GetAreaName() << ",";
            out << std::fixed;

            sPlayerbotAIConfig.log("gos.csv", out.str().c_str());
        }
    }

    if (sPlayerbotAIConfig.hasLog("zones.csv"))
    {
        sLog.outString("Create zone overlay exports.");

        std::unordered_map<std::string, std::vector<WorldPosition>> zoneLocs;

        std::vector<WorldPosition> Locs = {};

        for (auto& cdp : WorldPosition().GetCreaturesNear())
        {
            WorldPosition point(cdp);
            std::string name = std::to_string(point.GetMapId()) + point.GetAreaName();

            if (zoneLocs.find(name) == zoneLocs.end())
                zoneLocs.insert_or_assign(name, Locs);

            zoneLocs.at(name).push_back(point);
        }

        for (auto& loc : zoneLocs)
        {
            if (loc.second.empty())
                continue;

            if (!sTravelNodeMap.GetMapOffset(loc.second.front().GetMapId()) && loc.second.front().GetMapId() != 0)
                continue;

            std::vector<WorldPosition> points = loc.second;;

            std::ostringstream out;

            WorldPosition pos = WorldPosition(points, WP_MEAN_CENTROID);

            out << "\"center\"" << ",";
            out << points.begin()->GetMapId() << ",";
            out << points.begin()->GetAreaName() << ",";
            out << points.begin()->GetAreaName(true, true) << ",";

            pos.printWKT(out);

            if(points.begin()->GetArea())
                out << std::to_string(points.begin()->GetAreaLevel());
            else
                out << std::to_string(-1);

            out << "\n";

            out << "\"area\"" << ",";
            out << points.begin()->GetMapId() << ",";
            out << points.begin()->GetAreaName() << ",";
            out << points.begin()->GetAreaName(true, true) << ",";

            WorldPosition().printWKT(points, out, 0);

            if (points.begin()->GetArea())
                out << std::to_string(points.begin()->GetAreaLevel());
            else
                out << std::to_string(-1);

            sPlayerbotAIConfig.log("zones.csv", out.str().c_str());
        }
    }

    if (sPlayerbotAIConfig.hasLog("telecache.csv"))
    {
        sLog.outString("Create telecache overlay exports.");

        sRandomBotFacade.PrintTeleportCache();
    }

    if (sPlayerbotAIConfig.hasLog("travel_destinations.csv"))
    {
        sLog.outString("Create travel destinations export.");

        for (auto& [purpose, entryDestinations] : destinationMap)
        {
            for (auto& [entry, destinations] : entryDestinations)
            {
                for (auto& destination : destinations)
                {
                    std::ostringstream out;

                    out << TravelDestinationPurposeName.at(purpose) << ",";
                    out << entry << ",";
                    out << destination->GetShortName() << ",";
                    out << "\"" << destination->GetTitle() << "\",";
                    out << destination->GetSize() << ",";
                    out << "\"MultiPoint(";
                    destination->printWKT(out, false);
                    out.seekp(-1, out.cur);
                    out << ")\"";
                    sPlayerbotAIConfig.log("travel_destinations.csv", out.str().c_str());
                }
            }
        }
    }

    sTerrainMgr.Update(60 * 60 * 24);
}

void TravelMgr::GetPopulatedGrids()
{
    sLog.outString("Finding populated grids.");

    BarGoLink bar(sMapStore.GetNumRows());

    for (uint32 i = 0; i < sMapStore.GetNumRows(); ++i)
    {
        bar.step();

        if (!sMapStore.LookupEntry(i))
            continue;

        uint32 mapId = sMapStore.LookupEntry(i)->id;

        GetPopulatedGrids(mapId);
    }
}

void TravelMgr::GetPopulatedGrids(uint32 mapId)
{
    EntryGuidps const& guidpMap = GAI_VALUE_REF(EntryGuidps, "entry guidps");

    for (auto const& [entry, guidPs] : guidpMap)
    {
        for (auto const& guidP : guidPs)
        {
            if (guidP.GetMapId() == mapId)
            {
                populatedGrids[guidP.GetMapId()][guidP.getGridPair().x_coord][guidP.getGridPair().y_coord] = true;
            }
        }
    }
}

uint32 TravelMgr::GetFishZone(const AsyncGuidPosition& pos) const
{
    uint32 zone, subzone;
    int32 areaFlag = pos.GetAreaFlag();

    TerrainManager::GetZoneAndAreaIdByAreaFlag(zone, subzone, areaFlag, pos.GetMapId());

    if (sObjectMgr.GetFishingBaseSkillLevel(subzone))
        return subzone;

    if (sObjectMgr.GetFishingBaseSkillLevel(zone))
        return zone;

    return 0;
}

void TravelMgr::LoadFishLocations()
{
    sLog.outString("Loading Fish locations.");

    fishPoints.clear();
    fishMap.clear();
    destinationMap[TravelDestinationPurpose::GatherFishing].clear();

    auto result = WorldDatabase.Query("SELECT `name`, `map_id`, `position_x`, `position_y`, `position_z`, `orientation`, `description` FROM `ai_playerbot_named_location` WHERE `name` LIKE 'FISH_LOCATION%'");

    if (!result)
    {
        if (!sPlayerbotAIConfig.generateFishLocations)
        {
            sLog.outString("No persisted fish locations; generation is disabled, using direct fishing fallback.");
            return;
        }

        sTravelNodeMap.setHasToGen();
        GetFishLocations();
        sTravelNodeMap.setHasToGen(false);
        SaveFishLocations();
        return;
    }

    uint32 count = 0;
    do
    {
        ++count;

        Field* fields = result->Fetch();

        std::string name = fields[0].GetCppString();
        uint32 mapId = fields[1].GetUInt32();
        float positionX = fields[2].GetFloat();
        float positionY = fields[3].GetFloat();
        float positionZ = fields[4].GetFloat();
        float orientation = fields[5].GetFloat();
        std::string description = fields[6].GetCppString();

        AsyncGuidPosition* point = &fishPoints.emplace_back(GuidPosition(0, WorldPosition(mapId, positionX, positionY, positionZ, orientation)));

        int32 areaFlag = stoi(description);
        point->setAreaFlag(areaFlag);
        uint32 zone = GetFishZone(*point);

        if (!zone)
            continue;

        TravelDestination* dest = AddDestination<GatherTravelDestination>(zone, TravelDestinationPurpose::GatherFishing);

        dest->AddPoint(point);
        fishMap.AddPoint(point);

    } while (result->NextRow());

    sLog.outString(">> Loaded %u fish locations", count);
    sLog.outString();
}

void TravelMgr::GetFishLocations()
{
    sLog.outError("TortoiseBots: fish-location generation is unavailable with the pinned core PathInfo area query; use persisted locations or direct fishing.");
}

void TravelMgr::GetFishLocations(uint32 mapId)
{
    (void)mapId;
    sLog.outError("TortoiseBots: fish-location generation is unavailable with the pinned core PathInfo area query; use persisted locations or direct fishing.");
}

void TravelMgr::SaveFishLocations()
{
    sLog.outString("Saving Fish locations.");

    //Save fishpoints sorted by mapId, areaFlag, X, Y.
    fishPoints.sort([](AsyncGuidPosition i, AsyncGuidPosition j) {
        if (i.GetMapId() != j.GetMapId()) return i.GetMapId() > j.GetMapId();
        if (i.GetAreaFlag() != j.GetAreaFlag()) return i.GetAreaFlag() > j.GetAreaFlag();
        if (i.getX() != j.getX()) return i.getX() > j.getX();
        return i.getY() > j.getY(); });

    WorldDatabase.BeginTransaction();

    BarGoLink bar(fishPoints.size());

    uint32 count = 0;

    for (auto& fishPoint : fishPoints)
    {
        std::string name = "FISH_LOCATION_" + std::to_string(fishPoint.GetMapId()) + "_" + std::to_string(fishPoint.GetAreaFlag()) + "_" + std::to_string(count);
        std::string description = std::to_string(fishPoint.GetAreaFlag());
        name.erase(remove(name.begin(), name.end(), '\''), name.end());

        WorldDatabase.PExecute("INSERT INTO `ai_playerbot_named_location` (`name`, `map_id`, `position_x`, `position_y`, `position_z`, `orientation`, `description`) VALUES ('%s', '%d', '%f', '%f', '%f', '%f', '%s')"
            , name.c_str(), fishPoint.GetMapId(), fishPoint.getX(), fishPoint.getY(), fishPoint.getZ(), fishPoint.getO(), description.c_str());

        bar.step();
        count++;
    }

    WorldDatabase.CommitTransaction();
}

WorldPosition* TravelMgr::GetFishSpot(WorldPosition start, bool onlyNearestGrid)
{
    //0% Different map, 25% different grid, 25% different cell, 100% different point (inside cell)
    std::list<uint8> chances = { 0, 25, 25, 100 };

    if (onlyNearestGrid)
        chances = { 0, 0, 0, 100 };

    return fishMap.GetNextPoint(start, chances);
}

DestinationList TravelMgr::GetDestinations(const PlayerTravelInfo& info, uint32 purposeFlag, const std::vector<int32>& entries, bool onlyPossible, float maxDistance) const
{
    WorldPosition center = info.getPosition();
    DestinationList retDests;

    for (auto& [purpose, entryDests] : destinationMap)
    {
        if (purposeFlag != (uint32)TravelDestinationPurpose::None && !((uint32)purpose & (uint32)purposeFlag))
            continue;

        if (!entries.empty())
        {
            for (int32 entry : entries)
            {
                auto it = entryDests.find(entry);
                if (it == entryDests.end())
                    continue;

                for (auto& dest : it->second)
                {
                    float dist = dest->DistanceTo(center);
                    if (dist == FLT_MAX || (maxDistance > 0 && dist > maxDistance))
                        continue;

                    if (onlyPossible && !dest->IsPossible(info))
                        continue;

                    retDests.push_back(dest);
                }
            }
        }
        else
        {
            for (auto& [destEntry, dests] : entryDests)
            {
                for (auto& dest : dests)
                {
                    float dist = dest->DistanceTo(center);
                    if (dist == FLT_MAX || (maxDistance > 0 && dist > maxDistance))
                        continue;

                    if (onlyPossible && !dest->IsPossible(info))
                        continue;

                    retDests.push_back(dest);
                }
            }
        }
    }

    return retDests;
}

void TravelMgr::GetPartitionsLock(bool getLock)
{
    std::unique_lock<std::mutex> lock(sTravelMgr.getDestinationMutex);
    if (getLock)
    {
        sTravelMgr.getDestinationVar.wait(lock, [&] { return sTravelMgr.availableDestinationWorkers; });
        sTravelMgr.availableDestinationWorkers--;

        return;
    }

    sTravelMgr.availableDestinationWorkers++;
    sTravelMgr.getDestinationVar.notify_one();
}

bool TravelMgr::IsLocationLevelValid(const WorldPosition& position, const PlayerTravelInfo& info, uint32 purposeFlag, int32 grindZoneFloor, uint32 excludeZoneId)
{
    bool canFightElite = info.GetBoolValue("can fight elite");
    int32 botLevel = (int32)info.GetLevel();

    if (info.GetBoolValue("can fight boss"))
        botLevel += 5;
    else if (canFightElite)
        botLevel += 2;
    else if (!info.GetBoolValue("can fight equal"))
    {
        botLevel -= (int32)(2 + info.GetUint32Value("death count"));
    }

    if (info.IsMasterlessRandom() && position.GetMapId() != info.getPosition().GetMapId())
        return false;

    if (botLevel < 1)
        botLevel = 1;

    uint32 areaLevel = position.GetAreaLevel();

    if (!position.isOverworld() && !canFightElite)
        areaLevel += 10;

    // Handing a quest in is not a difficulty decision. The bot has already done
    // the work; whether the giver happens to stand in a neighbourhood rated above
    // its level says nothing about whether it should walk back. And the gate was
    // firing for a reason that has nothing to do with level at all: getAreaFlag
    // returns 0 whenever the vmap for that spot is not loaded, which inside the
    // asynchronous destination search is most of the time, and an area level of 0
    // was read here as "too high for you". Measured on a live realm before the
    // fix: of 350 rejected quest-taker points, 299 had no resolvable area
    // whatsoever.
    //
    AreaTableEntry const* posArea = position.GetArea();
    if (!sPlayerbotAIConfig.allowIsolatedCustomStartingZones && posArea)
    {
        uint32 posZoneId = posArea->ZoneId ? posArea->ZoneId : posArea->Id;
        if (PlayerbotAIConfig::IsIsolatedCustomZone(posZoneId) || PlayerbotAIConfig::IsIsolatedCustomZone(posArea->Id))
            return false;
    }

    // A beginner vendor trip is the one RPG errand a level 1-4 bot may make (see
    // RpgTravelDestination::IsPossible). The area-level check would reject the bot's
    // own camp vendor: starting areas are rated 2-5 while the bot is 1-4. The trip is
    // already bounded to the beginner radius and to the starting-zone level band
    // (area level <= bot level + 5), so the walk stays in the mob-safe camp.
    bool const beginnerVendorTrip =
        info.GetLevel() < 5 &&
        info.IsMasterlessRandom() &&
        (purposeFlag & (uint32)TravelDestinationPurpose::Vendor) &&
        position.distance(info.getPosition()) <= sPlayerbotAIConfig.lowLevelVendorMaxDistance;

    // A beginner vendor trip is a walk inside its own camp, whatever the other
    // gates would allow. The vendor search no longer runs the IsPossible
    // pre-filter that used to enforce this radius (see
    // RequestTravelTargetAction::Execute), so it is enforced here instead - and
    // a radius of 0 keeps meaning "no beginner vendor trips at all".
    if (info.GetLevel() < 5 && info.IsMasterlessRandom() &&
        (purposeFlag & (uint32)TravelDestinationPurpose::Vendor) &&
        position.distance(info.getPosition()) > sPlayerbotAIConfig.lowLevelVendorMaxDistance)
        return false;

    // Beginners grinding their starter valley are exempt from the zone-average
    // gate: GrindTravelDestination::IsPossible already caps their mob window to
    // their own level (and excludes critters), but the valley average sits above
    // a level 1-4 bot whenever higher-tier mobs spawn elsewhere in the same
    // valley - vetoing every local grind point, so the bot parks at spawn
    // re-rolling out-of-range rabbits instead of walking 200 yd to its wolves.
    // The grind floor below still applies, so grey zones stay excluded. Scoped
    // to masterless random bots so owned lowbies never wander off mid-follow.
    bool const beginnerGrind = (purposeFlag & (uint32)TravelDestinationPurpose::Grind) &&
        info.GetLevel() <= 4 && info.IsMasterlessRandom();
    // Same starter-valley wall for the quest errand (objectives and givers;
    // hand-ins stay exempt as before): a level-1 bot holding The Hunt Begins
    // (QuestLevel 2) sees every Plainstrider point vetoed by the Camp
    // Narache/Mulgore area-6 average and parks the quest purpose with an
    // empty list (live 2026-10-03: 57 tauren bots QuestTripNoTarget '0' at
    // the Camp Narache spawn). The quest's own gates already vet the trip -
    // the +1 quest-level window, the spawn entry's own template
    // (QuestObjectiveLevelFits) and the 40 yd point-danger surroundings -
    // so the area average is redundant here, not protective. Owned/hired
    // bots keep the ceiling: their player decides.
    bool const beginnerQuest = (purposeFlag & ((uint32)TravelDestinationPurpose::QuestGiver |
        (uint32)TravelDestinationPurpose::QuestAllObjective)) != 0 &&
        ai::QuestValleyExempted(info.GetLevel(), info.IsMasterlessRandom());

    // Both exemptions above drop the area ceiling, so keep their destinations
    // inside the starter valley instead (GrindSpotPolicy.h
    // BeginnerValleyLeashAllows): same zone as the bot, near its homebind.
    if (beginnerGrind || beginnerQuest)
    {
        AreaTableEntry const* botArea = info.getPosition().GetArea();
        uint32 const botZoneId = botArea ? (botArea->ZoneId ? botArea->ZoneId : botArea->Id) : 0;
        uint32 const pointZoneId = posArea ? (posArea->ZoneId ? posArea->ZoneId : posArea->Id) : 0;
        WorldPosition const& home = info.GetHomebind();
        bool const homeOnSameMap = home.GetMapId() == position.GetMapId();
        if (!ai::BeginnerValleyLeashAllows(true, botZoneId, pointZoneId, homeOnSameMap,
                homeOnSameMap ? home.distance(position) : 0.0f))
            return false;
    }

    // A grind destination's own creatures are already bounded by the policy band in
    // GrindSpotPolicy.h ([botLevel-2, botLevel+1]), so the area average is a second,
    // coarser veto on top of it. It is the one that bites at level 5: the beginner
    // exemption above ends exactly there (level <= 4) while the band has just widened
    // to level 3-6 creatures. Measured on the live stage-3 pool (500 bots, 90 min) at
    // the level-5 pick centres: of the spawns inside the band and within 1500 yd, 66%
    // of the level-4+ mobs and 81% of the level-5/6 ones sat in areas rated 6-10 and
    // died to this single comparison, so level-5 bots ground level 1-3 creatures for
    // 14-42 XP while the level 5-6 wildlife worth 56-70 lived one area rating away -
    // 40% of the level-5 bots never got a grind destination at all, and the ones that
    // did took 27 min to reach level 6 against 16 min for level 4->5.
    // Autonomous grind therefore gets the same margin that
    // GrindTravelDestination::IsPossible applies on the closest point, so the two
    // gates agree instead of the finer band being undone by the coarser average.
    // That margin is GRIND_AREA_MARGIN (+3): the cycle-3 deaths review showed the
    // far tail (areas rated bot+4..bot+5) paying the same XP per kill as the first
    // band (49.5 -> 50-52) for 117-150 deaths per 1,000 kills against 46 at or
    // below the bot's level, so it only bought corpses. The band the ceiling was
    // widened for (Elwynn 6, Teldrassil 7 for a level-5 bot) is +1/+2 and stays
    // open. Levels 1-4 never reach this ceiling (beginnerGrind above), which is
    // what keeps their start-valley destinations: those sub-areas are rated far
    // above a fresh bot (Camp Narache and Mulgore 6, Dun Morogh 7, Durotar 8) and
    // the destination gate gives them the wider margin for the same reason.
    // Owned/hired bots keep the strict ceiling: their player decides.
    // Quest-giver destinations keep the plain ceiling: a +5 margin (cycle 5) walked
    // level 1-4 bots out of their valley into the level 5-8 belt toward givers and
    // takers, and deaths went 3.9 -> 14-22 per minute (60% on quest travel).
    int32 areaCeiling = botLevel;
    if ((purposeFlag & (uint32)TravelDestinationPurpose::Grind) && info.IsMasterlessRandom())
        areaCeiling += GRIND_AREA_MARGIN;

    if (!beginnerGrind && !beginnerQuest && !(purposeFlag & (uint32)TravelDestinationPurpose::QuestTaker) && !beginnerVendorTrip)
    {
        if (!areaLevel || (uint32)areaCeiling < areaLevel) //Skip points that are in a area that is too high level.
            return false;
    }

    // Grind-specific: don't grind in a zone whose overall level is at/below the bot's
    // own floor for acceptable mobs - mirrors the per-mob minLevel window already used
    // in GrindTravelDestination::IsPossible(), applied to the zone as a whole so a
    // starting zone's own top-tier mobs can't keep a wildly over-leveled bot latched
    // there with no reason to ever travel farther. Ordinary Grind/Gather picks
    // for an outgrown pool bot use the zone level (OutgrownZoneRefusesPoint,
    // +5 like the leave rule) so the two cannot drift, still bounded by the
    // area ceiling above - an undergeared bot is never pushed anywhere it
    // cannot survive. Scoped to pool bots level 11+ on these three XP
    // purposes only (not Rpg/Quest/Explore, whose trips to a bot's home zone
    // stay walkable, and not services - Task B owns city/AH gating); levels
    // 1-10 never reach the gate, so starter behaviour is unchanged. Unknown
    // point zones (id 0) and unvalidated levels (<= 0) fail open, like today.
    static uint32 const outgrownXPPurposes =
        (uint32)TravelDestinationPurpose::Grind |
        (uint32)TravelDestinationPurpose::GatherMining |
        (uint32)TravelDestinationPurpose::GatherHerbalism;
    if (purposeFlag & (uint32)TravelDestinationPurpose::Grind)
    {
        int32 rawLevel = (int32)info.GetLevel();
        uint8 botPowerLevel = info.GetUint8Value("durability");
        float levelMod = botPowerLevel / 500.0f;
        float levelBoost = botPowerLevel / 50.0f;
        int32 grindMinLevel = std::max(rawLevel * (0.4f + levelMod), rawLevel - 12.0f + levelBoost);
        // Leave-rule Grind must land in a zone that fits the bot: the zone
        // itself must not be outgrown (zone + 5 >= bot level). The mob-window
        // floor above still admits the old zone's top-tier mobs, so the
        // caller's bot-level floor (botLevel - 5) wins whenever set.
        // Ordinary Grind passes floor 0 and is unchanged.
        if (grindZoneFloor > grindMinLevel)
            grindMinLevel = grindZoneFloor;
        if ((int32)areaLevel <= grindMinLevel)
            return false;
        // Zone migration (ZoneMigratePolicy.h): a leave-errand search must
        // not land back in the zone being left. The point's zone id comes
        // from its area (sub-areas inherit their parent zone). The trigger
        // already vetted outgrownness, so the exclusion is by zone id alone;
        // capital-idle leaves pass 0 and skip this entirely.
        if (excludeZoneId)
        {
            uint32 pointZoneId = posArea ? (posArea->ZoneId ? posArea->ZoneId : posArea->Id) : 0;
            if (pointZoneId == excludeZoneId)
                return false;
        }
    }
    if ((purposeFlag & outgrownXPPurposes) && !grindZoneFloor &&
        info.IsMasterlessRandom() && info.GetLevel() > 10)
    {
        uint32 pointZoneId = posArea ? (posArea->ZoneId ? posArea->ZoneId : posArea->Id) : 0;
        if (pointZoneId)
        {
            int32 pointZoneLevel = 0;
            if (sTravelMgr.TryGetValidatedAreaLevel(pointZoneId, pointZoneLevel) && pointZoneLevel > 0 &&
                ai::OutgrownZoneRefusesPoint(pointZoneLevel, info.GetLevel(), true))
                return false;
        }
    }
    // A quest/grind/gather point whose surroundings hold hostile spawns past
    // the bot's grind cap is no point: the spawn entry itself was in cap (see
    // #418), but the field around it is not - a level-4 bot on the item-750
    // trip walks to a Timber Wolf point outside Northshire and dies to the
    // Defias Cutpurse 5 / Forest Spider 6 / Mangy Wolf 6 standing next to
    // it. Gather nodes are the same shape with no mob of their own at all -
    // a level-17 bot mines a req-1 Copper Vein on the Daggerspine shore next
    // to level-30 nagas (Oct 2026 pool). Masterless pool bots only
    // (PointDangerApplies); quest objectives, quest loot, grind, mining and
    // herbalism. The static cell index (40 yd, one build, no
    // world scan) keeps this cheap inside the async search; neutral camps
    // and wildlife never count (template reaction), so giver/taker walks
    // through town stay untouched. When every point of a destination is
    // dangerous the search comes back empty and the caller parks the
    // purpose like any other empty search.
    if (ai::PointDangerApplies(info.GetLevel(), info.IsMasterlessRandom()) &&
        (purposeFlag & ((uint32)TravelDestinationPurpose::QuestAllObjective | (uint32)TravelDestinationPurpose::Grind |
            (uint32)TravelDestinationPurpose::GatherMining | (uint32)TravelDestinationPurpose::GatherHerbalism)))
    {
        Team const botTeam = info.GetTeam();
        uint32 const highestNear = position.GetHighestHostileLevelNear(ai::POINT_DANGER_RADIUS_YD, botTeam);
        if (highestNear != 0 && ai::PointDangerous((int)highestNear, info.GetLevel()))
            return false;
    }

    return true;
}

PartitionedTravelList TravelMgr::GetPartitions(const WorldPosition& center, const std::vector<uint32>& distancePartitions, const PlayerTravelInfo& info, uint32 purposeFlag, const std::vector<int32>& entries, bool onlyPossible, float maxDistance, int32 grindZoneFloor, uint32 excludeZoneId) const
{
    sTravelMgr.GetPartitionsLock();

    PartitionedTravelList pointMap;
    DestinationList destinations = GetDestinations(info, purposeFlag, entries, onlyPossible, maxDistance);



    unsigned seed = GetStableTravelSelectionSeed(info.GetIdentitySeed(), purposeFlag,
        center.GetMapId(), center.getX(), center.getY());
    std::shuffle(destinations.begin(), destinations.end(), std::default_random_engine(seed));

    // TEMPORARY counters. Quest takers are offered to a bot and nothing comes
    // back: 79 destinations across ten bots in 42 minutes produced two journeys,
    // and every failure ended with an empty list. Four things here can discard a
    // destination and from outside they are indistinguishable. Remove once the
    // answer is in.
    uint32 probeTotal = destinations.size(), probeNoPartition = 0, probeNoPoint = 0;
    uint32 probeRejectLevel = 0, probeRejectDistance = 0, probeFarthest = 0;

    for (auto& dest : destinations)
    {
        TravelPoint point(dest, sTravelMgr.nullWorldPosition, 0.0f);

        std::pair<uint32, std::vector<WorldPosition*>> pointRange = dest->GetClosestPartition(center, distancePartitions);

        if (!pointRange.first)
        {
            probeNoPartition++;
            continue;
        }

        MANGOS_ASSERT(pointRange.second.size());
        std::vector<WorldPosition*> points = pointRange.second;
        unsigned const pointSeed = MixTravelRouteSeed(seed ^ pointRange.first);
        std::shuffle(points.begin(), points.end(), std::default_random_engine(pointSeed));

        float minDistance = FLT_MAX;
        for (auto& position : points)
        {
            float distance = position->distance(center);

            if (distance > maxDistance)
            {
                probeRejectDistance++;
                if (distance > probeFarthest)
                    probeFarthest = uint32(distance);
                continue;
            }

            if (info.GetLevel() <= 5 && distance > 1500.0f)
            {
                probeRejectDistance++;
                continue;
            }

            if (!IsLocationLevelValid(*position, info, purposeFlag, grindZoneFloor, excludeZoneId))
            {
                probeRejectLevel++;
                continue;
            }

            if (distance < minDistance)
            {
                minDistance = distance;
                point = TravelPoint(dest, position, distance);
            }
        }

        if (std::get<2>(point) > 0)
            pointMap[pointRange.first].push_back(point);
        else
            probeNoPoint++;
    }

    if (pointMap.empty() && probeTotal && (purposeFlag & (uint32)TravelDestinationPurpose::QuestTaker))
        sLog.outBasic("PARTPROBE: level %u, %u taker destinations, none survived - %u had no partition, %u no usable point; points rejected: %u by level, %u by distance (max allowed %.0f, farthest seen %u)",
            info.GetLevel(), probeTotal, probeNoPartition, probeNoPoint,
            probeRejectLevel, probeRejectDistance, maxDistance, probeFarthest);

    sTravelMgr.GetPartitionsLock(false);

    return pointMap;
}

void TravelMgr::ShuffleTravelPoints(std::vector<TravelPoint>&points)
{
    unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
    std::shuffle(points.begin(), points.end(), std::default_random_engine(seed));
    /*
    std::vector<uint32> weights;
    std::transform(points.begin(), points.end(), std::back_inserter(weights), [](TravelPoint point) { return 200000 / (1 + std::get<2>(point)); });

    //If any weight is 0 add 1 to all weights.
    for (auto& w : weights)
    {
        if (w > 0)
            continue;

        std::for_each(weights.begin(), weights.end(), [](uint32& d) { d += 1; });
        break;
    }

    std::mt19937 gen(time(0));

    WeightedShuffle(points.begin(), points.end(), weights.begin(), weights.end(), gen);
    */
}

namespace
{
    // Destination -> travel targets currently holding it. Deliberately never
    // destroyed: pool bots log out during shutdown, and their TravelTarget
    // destructors may run after static destruction.
    std::unordered_map<TravelDestination*, uint32>& GrindSpotDemand()
    {
        static std::unordered_map<TravelDestination*, uint32>* demand = new std::unordered_map<TravelDestination*, uint32>();
        return *demand;
    }

    // Bots on different maps update on different map threads, so every access
    // to the shared demand map goes through this lock. Leaked like the map.
    std::mutex& GrindSpotDemandLock()
    {
        static std::mutex* lock = new std::mutex();
        return *lock;
    }
}

void TravelMgr::AcquireGrindSpot(TravelDestination* destination)
{
    if (!destination || destination->GetPurpose() != TravelDestinationPurpose::Grind)
        return;

    std::lock_guard<std::mutex> guard(GrindSpotDemandLock());
    GrindSpotDemand()[destination]++;
}

void TravelMgr::ReleaseGrindSpot(TravelDestination* destination)
{
    if (!destination || destination->GetPurpose() != TravelDestinationPurpose::Grind)
        return;

    std::lock_guard<std::mutex> guard(GrindSpotDemandLock());
    auto it = GrindSpotDemand().find(destination);
    if (it == GrindSpotDemand().end())
        return;

    if (--it->second == 0)
        GrindSpotDemand().erase(it);
}

bool TravelMgr::IsGrindSpotCrowded(TravelDestination* destination) const
{
    // Room for what the spot actually is: a two-spawn cave does not feed five bots,
    // a forty-spawn field is not full with twelve. The floor of two lets a bot always
    // join a spot a single other bot is working.
    uint32 const capacity = GrindSpotCapacity(destination->GetSize());
    std::lock_guard<std::mutex> guard(GrindSpotDemandLock());
    auto it = GrindSpotDemand().find(destination);

    return it != GrindSpotDemand().end() && it->second >= capacity;
}

void TravelMgr::DropCrowdedGrindPoints(PartitionedTravelList& points) const
{
    for (auto it = points.begin(); it != points.end();)
    {
        std::vector<TravelPoint>& list = it->second;

        list.erase(std::remove_if(list.begin(), list.end(), [this](const TravelPoint& point)
        {
            TravelDestination* destination = std::get<0>(point);
            return destination && destination->GetPurpose() == TravelDestinationPurpose::Grind && IsGrindSpotCrowded(destination);
        }), list.end());

        // Drop the range with it: SetBestTarget's "skip to a longer range" roll keys
        // off the last range, and an empty one would make it abandon a pick.
        it = list.empty() ? points.erase(it) : std::next(it);
    }
}

void TravelMgr::SetNullTravelTarget(TravelTarget* target) const
{
    if (target)
    {
        target->SetTarget(nullTravelDestination, nullWorldPosition);
        target->SetStatus(TravelStatus::TRAVEL_STATUS_NONE);
    }
}

void TravelMgr::SetNullTravelTarget(Player* player) const
{
    if (!player)
        return;

    if (!PlayerbotAIStorage::Instance().GetAI(player))
        return;

    TravelTarget* target = PlayerbotAIStorage::Instance().GetAI(player)->GetAiObjectContext()->GetValue<TravelTarget*>("travel target")->Get();

    SetNullTravelTarget(target);
}

void TravelMgr::AddMapTransfer(WorldPosition start, WorldPosition end, float portalDistance, bool makeShortcuts)
{
    uint32 sMap = start.GetMapId();
    uint32 eMap = end.GetMapId();

    if (sMap == eMap)
        return;

    //Calculate shortcuts.
    if(makeShortcuts)
        for (auto& mapTransfers : mapTransfersMap)
        {
            uint32 sMapt = mapTransfers.first.first;
            uint32 eMapt = mapTransfers.first.second;

            for (auto& mapTransfer : mapTransfers.second)
            {
                if (eMapt == sMap && sMapt != eMap) // [S1 >MT> E1 -> S2] >THIS> E2
                {
                    float newDistToEnd = MapTransDistance(mapTransfer.GetPointFrom(), start) + portalDistance;
                    if (MapTransDistance(mapTransfer.GetPointFrom(), end) > newDistToEnd)
                        AddMapTransfer(mapTransfer.GetPointFrom(), end, newDistToEnd, false);
                }

                if (sMapt == eMap && eMapt != sMap) // S1 >THIS> [E1 -> S2 >MT> E2]
                {
                    float newDistToEnd = portalDistance + MapTransDistance(end, mapTransfer.GetPointTo());
                    if (MapTransDistance(start, mapTransfer.GetPointTo()) > newDistToEnd)
                        AddMapTransfer(start, mapTransfer.GetPointTo(), newDistToEnd, false);
                }
            }
        }

    //Add actual transfer.
    auto mapTransfers = mapTransfersMap.find(std::make_pair(start.GetMapId(), end.GetMapId()));

    if (mapTransfers == mapTransfersMap.end())
        mapTransfersMap.insert({ { sMap, eMap }, {MapTransfer(start, end, portalDistance)} });
    else
        mapTransfers->second.push_back(MapTransfer(start, end, portalDistance));
};

void TravelMgr::LoadMapTransfers()
{
    for (auto& startNode : sTravelNodeMap.GetNodes())
    {
        for (auto& [endNode, path] : *startNode->GetLinks())
        {
            AddMapTransfer(*startNode->getPosition(), *endNode->getPosition(), path->getDistance());
        }
    }
}

void TravelMgr::CollectMapTransferPortals(const WorldPosition& start, uint32 endMapId,
    std::vector<std::pair<WorldPosition, float>>& outPortals) const
{
    outPortals.clear();
    uint32 startMapId = start.GetMapId();
    if (startMapId == endMapId)
    {
        outPortals.emplace_back(start, 0.0f);
        return;
    }

    auto mapTransfers = mapTransfersMap.find({ startMapId, endMapId });

    if (mapTransfers == mapTransfersMap.end())
        return;

    outPortals.reserve(mapTransfers->second.size());
    for (auto& mapTrans : mapTransfers->second)
    {
        WorldPosition portalOnEndMap = mapTrans.GetPointTo();

        float sqDist = mapTrans.sqDist(start, portalOnEndMap);

        outPortals.emplace_back(portalOnEndMap, sqDist);
    }
}

std::vector<std::pair<WorldPosition, float>> TravelMgr::sqMapTransDistances(const WorldPosition& start, uint32 endMapId) const
{
    // Copy-returning wrapper kept for the remaining callers (WorldSquare,
    // WorldPosition helpers are covered by MinMapTransDistance below).
    // Allocates once via CollectMapTransferPortals; prefer the out-param form
    // on hot paths.
    std::vector<std::pair<WorldPosition, float>> retPortals;
    CollectMapTransferPortals(start, endMapId, retPortals);
    return retPortals;
}

// Minimum squared through-portal distance without any heap allocation: the
// portal lists are static data, so iterate the map-transfer entries directly
// instead of materialising a vector first. Same arithmetic as MapTransDistance.
float TravelMgr::MinSqMapTransDistance(const WorldPosition& start, const WorldPosition& end) const
{
    uint32 startMapId = start.GetMapId();
    uint32 endMapId = end.GetMapId();
    if (startMapId == endMapId)
        return start.sqDistance2d(end);

    auto mapTransfers = mapTransfersMap.find({ startMapId, endMapId });
    if (mapTransfers == mapTransfersMap.end())
        return FLT_MAX;

    float minsqDist = FLT_MAX;
    for (auto& mapTrans : mapTransfers->second)
    {
        WorldPosition portalOnEndMap = mapTrans.GetPointTo();
        float sqDist = mapTrans.sqDist(start, portalOnEndMap) + portalOnEndMap.sqDistance2d(end);
        if (sqDist < minsqDist)
            minsqDist = sqDist;
    }
    return minsqDist;
}

float TravelMgr::MapTransDistance(const WorldPosition& start, const WorldPosition& end, bool toMap) const
{
    float const minsqDist = MinSqMapTransDistance(start, end);
    if (minsqDist == FLT_MAX)
        return FLT_MAX;
    return sqrt(minsqDist);
}
