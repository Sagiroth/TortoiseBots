#include "playerbot/playerbot.h"
#include "WarriorGapCloser.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/generic/PullStrategy.h"
#include "playerbot/strategy/values/TargetValue.h"
#include "playerbot/strategy/actions/BossTacticsActions.h"
#include "playerbot/BossTacticsMgr.h"
#include "playerbot/BossTacticsStand.h"

#include <algorithm>
#include <vector>

using namespace ai;

namespace
{
    bool OrderedMeleePullOf(PlayerbotAI* ai, Unit* target)
    {
        AiObjectContext* context = ai->GetAiObjectContext();
        if (AI_VALUE(ObjectGuid, "explicit attack target") == target->GetObjectGuid())
            return true;
        PullStrategy const* pull = PullStrategy::Get(ai);
        return pull && pull->IsCommandActive() && !pull->CommandWantsRanged() && pull->GetTarget() == target;
    }

    // A boss as the boss tactics and the threat rules define it: a creature with boss-tactics rows on this map, or a
    // rank 3 (world boss) elite.
    bool IsBossUnit(Unit* unit)
    {
        Creature* creature = unit ? dynamic_cast<Creature*>(unit) : nullptr;
        if (!creature)
            return false;
        return creature->IsWorldBoss() || GetBossTactics().Rows(creature->GetMapId(), creature->GetEntry()) != nullptr;
    }

    // The tank is the victim of a boss among the fight's attackers (it holds one).
    bool TankHoldsBoss(PlayerbotAI* ai, Player* bot)
    {
        AiObjectContext* context = ai->GetAiObjectContext();
        for (const ObjectGuid& guid : AI_VALUE(std::list<ObjectGuid>, "attackers"))
        {
            Unit* attacker = ai->GetUnit(guid);
            if (attacker && attacker->IsAlive() && attacker->GetVictim() == bot && IsBossUnit(attacker))
                return true;
        }
        return false;
    }

    // The target is a boss the tactics place the raid around: a pullback of it is running, or the bot has a stand seat.
    bool BossPullbackOrStand(PlayerbotAI* ai, Player* bot, Unit* target)
    {
        if (!IsBossUnit(target))
            return false;
        PullStrategy const* pull = PullStrategy::Get(ai);
        if (pull && pull->IsCommandActive() && pull->IsCommandPullback() && pull->GetTarget() == target)
            return true;
        return FindBossStand(ai, bot).valid;
    }
}

WarriorGapCloser ai::DecideWarriorGapCloser(PlayerbotAI* ai, Unit** chosen)
{
    if (chosen)
        *chosen = nullptr;
    if (!sPlayerbotAIConfig.warriorGapCloser || !ai)
        return WarriorGapCloser::None;

    Player* bot = ai->GetBot();
    if (!bot || !bot->IsAlive() || ai->HasStrategy("stay", ai->GetState()))
        return WarriorGapCloser::None;

    AiObjectContext* context = ai->GetAiObjectContext();
    bool const inCombat = bot->IsInCombat();
    bool const isTank = ai->IsTank(bot);
    // Cheapest exit first: in combat only a tank rescue is left.
    if (inCombat && !isTank)
        return WarriorGapCloser::None;

    Unit* rescueTarget = isTank ? AI_VALUE(Unit*, "attacker of non-tank target") : nullptr;
    Unit* ownTarget = inCombat ? nullptr : AI_VALUE(Unit*, "current target");
    if (!rescueTarget && !ownTarget)
        return WarriorGapCloser::None;

    uint32 const chargeId = AI_VALUE2(uint32, "spell id", "charge");
    uint32 const interceptId = AI_VALUE2(uint32, "spell id", "intercept");
    unsigned const tmRank = TacticalMasteryRank(bot->HasSpell(WARRIOR_TACTICAL_MASTERY_RANK_1),
        bot->HasSpell(WARRIOR_TACTICAL_MASTERY_RANK_2), bot->HasSpell(WARRIOR_TACTICAL_MASTERY_RANK_3),
        bot->HasSpell(WARRIOR_TACTICAL_MASTERY_RANK_4), bot->HasSpell(WARRIOR_TACTICAL_MASTERY_RANK_5));
    // Only a tank rescue in combat reads it; computed once, after the cheap exits.
    bool const tankHoldsBoss = inCombat && isTank && rescueTarget && TankHoldsBoss(ai, bot);
    Map* map = bot->GetMap();
    bool const inInstance = map && (map->IsDungeon() || map->IsRaid());

    std::vector<WarriorGapCloserFacts> facts;
    std::vector<Unit*> units;
    CcGuard ccGuard(ai);
    for (Unit* unit : { rescueTarget, ownTarget })
    {
        if (!unit || !unit->IsInWorld() || !unit->IsAlive() || unit->GetMapId() != bot->GetMapId() ||
            !sServerFacade.IsHostileTo(bot, unit))
            continue;
        if (std::find(units.begin(), units.end(), unit) != units.end())
            continue;

        WarriorGapCloserFacts f;
        f.enabled = true;
        f.warriorInCombat = inCombat;
        f.isTank = isTank;
        f.rescue = unit == rescueTarget;
        f.combatDistance = bot->GetCombatDistance(unit);
        if (!WarriorGapCloserInRange(f.combatDistance))
            continue;
        f.targetCrowdControlled = FindNonCcTargetStrategy::IsCrowdControlled(ai, unit);
        f.targetHeldCcMark = ccGuard.IsHeldCcMark(unit);
        f.lineOfSight = bot->IsWithinLOSInMap(unit, true);
        f.dpsHeld = IsDpsHeld(ai, bot, unit);
        f.bossPullbackOrStand = BossPullbackOrStand(ai, bot, unit);
        f.tankHoldsBoss = tankHoldsBoss;
        f.pullAllowed = WarriorGapCloserPullAllowed(unit->IsInCombat(), inInstance, OrderedMeleePullOf(ai, unit));
        f.chargeKnown = chargeId != 0;
        f.chargeReady = chargeId && sServerFacade.IsSpellReady(bot, chargeId);
        f.interceptKnown = interceptId != 0;
        f.interceptReady = interceptId && sServerFacade.IsSpellReady(bot, interceptId);
        f.inBerserkerStance = ai->HasAura("berserker stance", bot);
        f.rage = AI_VALUE2(uint8, "rage", "self target");
        f.tmRank = tmRank;
        facts.push_back(f);
        units.push_back(unit);
    }

    std::size_t const pick = PickWarriorGapCloserTarget(facts);
    if (pick >= units.size())
        return WarriorGapCloser::None;
    if (chosen)
        *chosen = units[pick];
    return ChooseWarriorGapCloser(facts[pick]);
}
