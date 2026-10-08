
#include "playerbot/playerbot.h"
#include "GrindTargetValue.h"
#include "playerbot/GroupMembers.h"
#include "RtiTargetValue.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/RandomBotFacade.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/WorldPosition.h"
#include "AttackersValue.h"
#include "PossibleAttackTargetsValue.h"
#include "playerbot/strategy/actions/ChooseTargetActions.h"
#include "playerbot/strategy/values/FreeMoveValues.h"
#include "playerbot/PullRegenPolicy.h"
#include "playerbot/PointDangerPolicy.h"
#include "playerbot/GrindSpotPolicy.h"
#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"

namespace
{
// Highest level above the bot a grind target may have. A character below level 10 has
// weapon skill 5 and no abilities, so of the orders it placed on a mob two or more levels
// above it about 1% ended in a kill (0.3% for melee) against 16-28% at the bot's own
// level - and those orders were 15% of all grind orders in a measured level-1 pool. Only
// the solo grind is restricted: a bot following a real player is told what to fight, and
// the battleground exemption stays where it always was, on the check itself.
// The numbers live in PullRegenPolicy.h (PullGrindLevelCap) next to the
// pull-regen gate so the cap stays testable on its own.
int MaxGrindLevelOverBot(Player* bot, PlayerbotAI* ai)
{
    return ai::PullGrindLevelCap(bot->GetLevel(), ai->HasRealPlayerMaster());
}
}

bool GrindTargetValue::IsAllowedInstanceTarget(PlayerbotAI* ai, Unit* target)
{
    if (!target) return false;
    Player* bot = ai->GetBot();
    Group* group = bot->GetGroup();
    Player* master = ai->GetMaster();
    if (!group || !ai->HasRealPlayerMaster() || !master || master->GetGroup() != group ||
        !bot->IsInWorld() || !bot->GetMap()->IsDungeon())
        return true;

    AiObjectContext* context = ai->GetAiObjectContext();
    if (AI_VALUE(ObjectGuid, "explicit attack target") == target->GetObjectGuid()) return true;
    int const mark = RtiTargetValue::GetRtiIndex(AI_VALUE(std::string, "rti"));
    if (mark >= 0 && ObjectGuid(group->GetTargetIcon(mark)) == target->GetObjectGuid()) return true;

    // A global combat flag can belong to another party. Require this group's
    // actual victim/threat relationship, never a stale autonomous attack order.
    if (!target->IsInCombat() || !target->IsInMap(bot)) return false;
    for (Player* member : LiveGroupMembers(group))
    {
        if (!member->IsInWorld() || !member->IsAlive() || !member->IsInMap(bot)) continue;
        auto engaged = [target](Unit* unit)
        {
            return unit && (target->GetVictim() == unit || unit->GetVictim() == target ||
                target->GetThreatManager().getThreat(unit) > 0.0f);
        };
        if (engaged(member) || engaged(member->GetPet())) return true;
    }
    return false;
}

Unit* GrindTargetValue::Calculate()
{
    uint32 memberCount = 1;
    Group* group = bot->GetGroup();
    if (group)
        memberCount = group->GetMembersCount();

    Unit* target = NULL;
    uint32 assistCount = 0;
    while (!target && assistCount < memberCount)
    {
        target = FindTargetForGrinding(assistCount++);
    }

    // Idle-starter fallback: the normal scan found nothing and the bot holds
    // no travel destination, so without this it idles until a mob wanders
    // into the 60 yd scan. A low-level masterless bot then walks to the
    // nearest in-cap XP mob instead of gossip-touring the camp.
    if (!target)
        target = FindIdleFallbackTarget();

    return target;
}

Unit* GrindTargetValue::FindTargetForGrinding(int assistCount)
{
    Group* group = bot->GetGroup();

    // TEMP-DEBUG(grind-target): the existing "debug grind" strategy only reaches a
    // real player master via TellPlayer, which silently no-ops for random bots (no
    // master to tell) - mirror the same verdict to a file so masterless random bots
    // are traceable too, tagging the reaction (hostile/neutral/friendly) to see if
    // neutral starting-zone wildlife is being systematically skipped. Remove once
    // the "bots not picking fights" investigation concludes.
    auto logGrind = [&](Unit* u, const std::string& reason)
    {
        if (ai->HasStrategy("debug grind", BotState::BOT_STATE_NON_COMBAT))
            ai->TellPlayer(GetMaster(), chat->formatWorldobject(u) + " " + reason);

        if (sPlayerbotAIConfig.hasLog("grind_target.csv"))
        {
            std::ostringstream out;
            out << sPlayerbotAIConfig.GetTimestampStr() << "+00,";
            out << bot->GetName() << ",\"" << u->GetName() << "\"," << u->GetEntry() << ",";
            out << (int)sServerFacade.IsHostileTo(bot, u) << "," << (int)sServerFacade.IsFriendlyTo(bot, u) << ",";
            out << bot->GetDistance(u) << ",\"" << reason << "\"";
            sPlayerbotAIConfig.log("grind_target.csv", out.str().c_str());
        }
    };

    int const maxLevelOver = MaxGrindLevelOverBot(bot, ai);

    // A mob an active quest asks for is skipped by the level cap while the
    // bot is below level 10 without a real player master: a level-2 bot on a
    // quest trip still cannot kill a level-5+ mob, and ordering it only
    // produces the corpse the destination gate above now refuses to walk to
    // (62% of level 1-4 pool deaths were by mobs 2+ levels above). From level
    // 10, or with a real player master, quest work keeps the old exemption.
    bool const questWorkCapped = bot->GetLevel() < 10 && !ai->HasRealPlayerMaster();
    auto levelTooHigh = [&](Unit* unit)
    {
        if (bot->InBattleGround() || unit->getObjectGuid().IsPlayer() ||
            (int)unit->GetLevel() - (int)bot->GetLevel() <= maxLevelOver)
            return false;

        if (questWorkCapped)
            return true;

        return !AI_VALUE2(bool, "need for quest", std::to_string(unit->GetEntry()));
    };

    std::list<ObjectGuid> attackers = context->GetValue<std::list<ObjectGuid>>("possible attack targets")->Get();
    for (std::list<ObjectGuid>::iterator i = attackers.begin(); i != attackers.end(); i++)
    {
        Unit* unit = ai->GetUnit(*i);
        if (!unit || !sServerFacade.IsAlive(unit) || !IsAllowedInstanceTarget(ai, unit))
            continue;

        // Belt and braces: "possible attack targets" is built from "attackers", which
        // already refuses an evading creature, but this loop trusts a cached list and
        // returns on the first hit - so the one predicate that decides whether the mob
        // can be hurt at all is applied here as well, with a reason in the grind log.
        // The core flags a creature it cannot path to long before it walks home:
        // m_TargetNotReachableTimer > 3 s (IsEvadeBecauseTargetNotReachable) while the
        // full evade reset takes 24 s. An order placed in that window never lands.
        if (unit->IsCreature() && static_cast<Creature*>(unit)->IsEvadeBecauseTargetNotReachable())
        {
            logGrind(unit, "(hostile) ignored (unreachable).");
            continue;
        }
        if (unit->IsCreature() && static_cast<Creature*>(unit)->IsInEvadeMode())
        {
            logGrind(unit, "(hostile) ignored (evading).");
            continue;
        }

        // Solo bots (no master at all) are bounded by scan radius only; any
        // follower keeps the free-move leash so grind never picks a target the
        // follow strategy would immediately pull it back from.
        if (!bot->InBattleGround() && ai->GetMaster() && !CanFreeMoveValue::CanFreeTarget(ai, GuidPosition(unit)))
        {
            logGrind(unit, "(hostile) ignored (out of free range).");
            continue;
        }

        // Self-defence has no level cap (donor mod-playerbots
        // GrindTargetValue::FindTargetForGrinding returns the first live
        // attacker unconditionally): a mob already fighting the bot must be
        // answered however far above it stands. The +1 grind cap stays on
        // NEW pulls in the possible-targets loop below (levelTooHigh there
        // is untouched); evade/unreachable and follower-leash guards above
        // stay. Without this the pick comes back null on a taker corridor,
        // AttackAnythingAction::isUseful bails before its revenge block, and
        // the bot walks through +2 mobs without a swing (Renees hand-ins:
        // 3/83 fought back vs 48% on grind trips, Oct 2026 pool).
        logGrind(unit, "(hostile) selected.");
        return unit;
    }

    std::list<ObjectGuid> targets = *context->GetValue<std::list<ObjectGuid> >("possible targets");

    if (targets.empty())
        return NULL;

    float distance = 0;
    Unit* result = NULL;

    bool travelTargetWorking = AI_VALUE(bool, "travel target working");
    bool travelTargetTraveling = AI_VALUE(bool, "travel target traveling");
    TravelTarget* travelTarget = AI_VALUE(TravelTarget*, "travel target");
    bool isGrindTravelDest = travelTarget && typeid(travelTarget->GetDestination()) == typeid(GrindTravelDestination);

    struct MemberInfo {
        Player* player;
        float x, y;
    };
    std::vector<MemberInfo> groupMembers;
    if (group)
    {
        Group::MemberSlotList const& groupSlot = group->GetMemberSlots();
        groupMembers.reserve(groupSlot.size());
        for (Group::member_citerator itr = groupSlot.begin(); itr != groupSlot.end(); itr++)
        {
            Player* member = sObjectMgr.GetPlayer(itr->guid);
            if (member && sServerFacade.IsAlive(member))
                groupMembers.push_back({ member, member->getPositionX(), member->getPositionY() });
        }
    }

    std::unordered_map<uint32, bool> needForQuestCache;

    for (std::list<ObjectGuid>::iterator tIter = targets.begin(); tIter != targets.end(); tIter++)
    {
        Unit* unit = ai->GetUnit(*tIter);
        if (!unit || !IsAllowedInstanceTarget(ai, unit))
            continue;


        if (abs(bot->getPositionZ() - unit->getPositionZ()) > sPlayerbotAIConfig.spellDistance)
        {
            logGrind(unit, "ignored (to far above/below).");
            continue;
        }

        // As above: solo bots skip, followers keep the leash. The old "far from
        // master" block is gone: it was unreachable (the master local was nulled
        // for real players, which never have an AI), and CanFreeTarget already
        // bounds followers to free-move range around the follow target.
        if (!bot->InBattleGround() && ai->GetMaster() && !CanFreeMoveValue::CanFreeTarget(ai, GuidPosition(unit))) //Do not grind mobs far away from master.
        {
            logGrind(unit, "ignored (out of free range).");
            continue;
        }

        if (levelTooHigh(unit))
        {
            logGrind(unit, std::to_string((int)unit->GetLevel() - (int)bot->GetLevel()) + " levels above bot).");
            continue;
        }

        // No player pull while either side is fresh from a revive: pool bots of
        // both factions revived at one shared spirit healer and killed each
        // other in a loop. Self-defence (the attackers loop above) still fights.
        if (Player* victim = unit->ToPlayer())
        {
            PlayerbotAI* victimAi = PlayerbotAIStorage::Instance().GetAI(victim);
            if (ai->InReviveGrace() || (victimAi && victimAi->InReviveGrace()) ||
                victim->HasAura(SPELL_ID_PASSIVE_RESURRECTION_SICKNESS))
            {
                logGrind(unit, "ignored (revive grace).");
                continue;
            }
        }

        Creature* creature = dynamic_cast<Creature*>(unit);
        if (creature && creature->GetCreatureInfo() && creature->GetCreatureInfo()->rank > CREATURE_ELITE_NORMAL && !AI_VALUE(bool, "can fight elite"))
        {
            logGrind(unit, "ignored (can not fight elites currently).");
            continue;
        }

        if (!AttackersValue::IsValid(unit, bot, nullptr, false, false))
        {
            logGrind(unit, "ignored (is pet or evading/unkillable).");
            continue;
        }

        if (!PossibleAttackTargetsValue::IsPossibleTarget(unit, bot, sPlayerbotAIConfig.sightDistance, false))
        {
            logGrind(unit, "ignored (tapped, cced or out of range).");
            continue;
        }

        // Wild XP-granting type-8 beasts (Deer/Cow) are fair game, not critter
        // skips: gate the skip on the grey-level/no-XP verdict. Pre-attack
        // MaNGOS::XP::Gain is 0 for any creature nobody has damaged yet.
        if (creature && creature->GetCreatureType() == CREATURE_TYPE_CRITTER && !bot->IsHonorOrXPTarget(unit) && urand(0, 10))
        {
            logGrind(unit, "ignored (ignore critters).");
            continue;
        }

        float newdistance = sServerFacade.getDistance2d(bot, unit);

        uint32 entry = unit->GetEntry();
        bool needForQuest = false;
        if (entry)
        {
            auto cacheIt = needForQuestCache.find(entry);
            if (cacheIt != needForQuestCache.end())
            {
                needForQuest = cacheIt->second;
            }
            else
            {
                needForQuest = AI_VALUE2(bool, "need for quest", std::to_string(entry));
                needForQuestCache[entry] = needForQuest;
            }
        }

        if (entry && !needForQuest)
        {
            // Was < 99 (99% skip chance for anything not tied to an active quest
            // objective) - with grind_target.csv logging live, this was confirmed as
            // the dominant reason bots weren't engaging: 23,863 "no grind target
            // found" against only 664 successful selections in one run, and the
            // rejected candidates were overwhelmingly real, appropriate-level,
            // killable mobs (Rat, Ragged Young Wolf, Duskbat, Defias Cutpurse, etc.),
            // not grey/no-XP ones - bots were holding out for quest-specific kills
            // almost exclusively instead of grinding on anything nearby. Lowered so
            // bots still lean toward quest-relevant kills when traveling somewhere
            // specific, but no longer skip everything else 99% of the time.
            if (urand(0, 100) < 20 && travelTargetWorking && !isGrindTravelDest)
            {
                logGrind(unit, "ignored (not needed for active quest).");
                continue;
            }
            // A grey (no-XP) creature is never worth starting a fight over (issue
            // #396): an autonomous pool bot skips it outright, mirroring the donor
            // mod-playerbots hard gate (!bot->isHonorOrXPTarget). Self-defence still
            // applies (the attackers loop above returns before this), quest-objective
            // kills stay allowed (needForQuest branch), and bots with a real-player
            // master keep the old probabilistic lean so explicit orders are unchanged.
            else if (creature && !bot->IsHonorOrXPTarget(unit))
            {
                if (!ai->HasRealPlayerMaster() && !bot->InBattleGround())
                {
                    logGrind(unit, "ignored (grey, no xp).");
                    continue;
                }
                if (urand(0, 50))
                {
                    logGrind(unit, "ignored (not xp and not needed for quest).");
                    continue;
                }
            }
            else if (urand(0, 100) < 75)
            {
                logGrind(unit, "increased distance (not needed for quest).");
                newdistance += 20;
            }
        }

        if (!bot->InBattleGround() && GetTargetingPlayerCount(unit) > assistCount)
        {
            logGrind(unit, std::to_string(GetTargetingPlayerCount(unit)) + " bots already targeting).");
            newdistance =+ GetTargetingPlayerCount(unit) * 5;
        }

        if (group)
        {
            Group::MemberSlotList const& groupSlot = group->GetMemberSlots();
            for (Group::member_citerator itr = groupSlot.begin(); itr != groupSlot.end(); itr++)
            {
                Player* member = sObjectMgr.GetPlayer(itr->guid);
                if (!member || !sServerFacade.IsAlive(member))
                    continue;

                newdistance = sServerFacade.getDistance2d(member, unit);
                if (!result || newdistance < distance)
                {
                    distance = newdistance;
                    result = unit;
                }
            }
        }
        else
        {
            if (!result || (newdistance < distance && urand(0, abs(distance - newdistance)) > sPlayerbotAIConfig.sightDistance * 0.1))
            {
                distance = newdistance;
                result = unit;
            }
        }
    }

    // One pathfind per pick, on the winner only: a candidate across a mine shaft or
    // cliff walls the bot (bot spends 24 s attacking air, the core evades the mob).
    if (result && result->IsCreature())
    {
        Creature* picked = static_cast<Creature*>(result);
        if (picked->IsEvadeBecauseTargetNotReachable() ||
            !WorldPosition(bot).canPathTo(WorldPosition(result), bot))
        {
            logGrind(result, "ignored (no path).");
            result = NULL;
        }
    }
    if (result)
    {
        logGrind(result, "selected.");
    }
    else if (sPlayerbotAIConfig.hasLog("grind_target.csv"))
    {
        std::ostringstream out;
        out << sPlayerbotAIConfig.GetTimestampStr() << "+00,";
        out << bot->GetName() << ",\"none\",0,0,0,0,\"no grind target found\"";
        sPlayerbotAIConfig.log("grind_target.csv", out.str().c_str());
    }
    if (!result && ai->HasStrategy("debug grind", BotState::BOT_STATE_NON_COMBAT))
    {
        ai->TellPlayer(GetMaster(), "No grind target found.");
    }

    return result;
}

Unit* GrindTargetValue::FindIdleFallbackTarget()
{
    // Gate first, scan never: the wider grid visit only runs for an idle
    // bot with no journey and an empty normal pick (see
    // GrindIdleFallbackAllowed in GrindSpotPolicy.h). Everything else keeps
    // today's behaviour. Called only when the normal pick came back empty,
    // so normalPickEmpty is true by construction.
    TravelTarget* travelTarget = AI_VALUE(TravelTarget*, "travel target");
    if (!ai::GrindIdleFallbackAllowed(sRandomBotFacade.IsRandomBot(bot) && !ai->HasRealPlayerMaster(),
        bot->GetLevel(), travelTarget && travelTarget->IsActive(),
        sServerFacade.IsInCombat(bot), bot->InBattleGround(),
        WorldPosition(bot).isOverworld(), AI_VALUE(bool, "can move around"), true))
        return nullptr;

    uint32 const nowMs = WorldTimer::getMSTime();
    if (lastIdleFallbackMs && nowMs - lastIdleFallbackMs < ai::GRIND_IDLE_FALLBACK_INTERVAL_MS)
        return nullptr;
    lastIdleFallbackMs = nowMs;
    // The normal 60 yd scan is empty by construction here: look a little
    // wider for the nearest mob the bot can actually fight. Every per-mob
    // gate mirrors the normal pick - in-cap only (PullGrindLevelCap, never
    // loosened), XP-paying only (no greys), attackable, unclaimed - plus a
    // path check per candidate so the order never becomes the 50 yd stare
    // the reach action then has to give up on.
    std::list<Unit*> units;
    MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck u_check(bot, bot, ai::GRIND_IDLE_FALLBACK_RANGE_YD);
    MaNGOS::UnitListSearcher<MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck> searcher(units, u_check);
    Cell::VisitAllObjects(bot, searcher, ai::GRIND_IDLE_FALLBACK_RANGE_YD);

    int const maxLevelOver = MaxGrindLevelOverBot(bot, ai);
    Unit* best = nullptr;
    float bestDist = 0.0f;

    for (Unit* unit : units)
    {
        if (!unit || !sServerFacade.IsAlive(unit) || !IsAllowedInstanceTarget(ai, unit))
            continue;

        Creature* creature = dynamic_cast<Creature*>(unit);
        if (!creature)
            continue;

        if (creature->IsEvadeBecauseTargetNotReachable())
            continue;
        if (creature->IsInEvadeMode())
            continue;

        if ((int)unit->GetLevel() - (int)bot->GetLevel() > maxLevelOver)
            continue;

        if (creature->GetCreatureInfo() && creature->GetCreatureInfo()->rank > CREATURE_ELITE_NORMAL &&
            !AI_VALUE(bool, "can fight elite"))
            continue;

        if (!AttackersValue::IsValid(unit, bot, nullptr, false, false))
            continue;

        if (!PossibleAttackTargetsValue::IsPossibleTarget(unit, bot, ai::GRIND_IDLE_FALLBACK_RANGE_YD, false))
            continue;

        // Grey (no-XP) creatures are never worth starting a fight over, and
        // critters only when they pay XP (issue #396). Revenge still applies
        // through the attackers loop, which runs before this.
        if (!bot->IsHonorOrXPTarget(unit))
            continue;

        float const dist = sServerFacade.GetDistance2d(bot, unit);
        if (best && !(dist < bestDist))
            continue;

        // Above the starter band the fallback walks into real camps, so the
        // travel layer's own survival gates apply: no mob inside a camp the
        // bot keeps dying in (issue #398), and no mob whose surroundings
        // hold hostiles past the grind cap (PointDangerPolicy.h, #418).
        // Both are data-only reads (avoidance list, static spawn index),
        // run only for a candidate nearer than the current best.
        WorldPosition mobPos(unit);
        if (ai->IsDeathSpotAvoided(mobPos.GetMapId(), mobPos.getX(), mobPos.getY(), nowMs))
            continue;
        uint32 const highestNear = mobPos.GetHighestHostileLevelNear(
            ai::POINT_DANGER_RADIUS_YD, bot->GetTeam());
        if (highestNear != 0 && ai::PointDangerous((int)highestNear, bot->GetLevel()))
            continue;

        if (!WorldPosition(bot).canPathTo(WorldPosition(unit), bot))
            continue;

        best = unit;
        bestDist = dist;
    }

    return best;
}

int GrindTargetValue::GetTargetingPlayerCount( Unit* unit )
{
    Group* group = bot->GetGroup();
    if (!group)
        return 0;

    int count = 0;
    Group::MemberSlotList const& groupSlot = group->GetMemberSlots();
    for (Group::member_citerator itr = groupSlot.begin(); itr != groupSlot.end(); itr++)
    {
        Player *member = sObjectMgr.GetPlayer(itr->guid);
        if( !member || !sServerFacade.IsAlive(member) || member == bot)
            continue;

        PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(member);
        if ((ai && *ai->GetAiObjectContext()->GetValue<Unit*>("current target") == unit) ||
            (!ai && member->GetSelectionGuid() == unit->getObjectGuid()))
            count++;
    }

    return count;
}
