#include "playerbot/playerbot.h"
#include "HoldKillBlockersValue.h"
#include <cstdlib>
#include "playerbot/BossTacticsPolicy.h"
#include "playerbot/GroupMembers.h"
#include "playerbot/ServerFacade.h"
#include "TargetValue.h"
#include "Objects/TemporarySummon.h"

using namespace ai;

namespace
{
// The qualified entry of the value ("hold kill blockers::<entry>" / "sole kill candidates::<entry>").
uint32 QualifierEntry(const std::string& qualifier)
{
    return static_cast<uint32>(std::strtoul(qualifier.c_str(), nullptr, 10));
}

// The entry's living creatures within kHoldKillRangeYd of the bot, by grid search (the core's
// AllCreaturesOfEntryInRange through the world object's entry-in-grid list).
void ScanEntry(Player* bot, uint32 entry, std::list<Creature*>& found, float range = kHoldKillRangeYd)
{
    if (entry && bot)
        bot->GetCreatureListWithEntryInGrid(found, entry, range);
}

// Whether `unit` is one of the bot's group: a member, or a pet or guardian whose owner is one. The bot alone counts
// when it runs without a group.
bool IsGroupUnit(Unit* unit, Player* bot, Group* group)
{
    if (!unit)
        return false;
    Player* const owner = unit->GetCharmerOrOwnerPlayerOrPlayerItself();
    return owner && (owner == bot || (group && group->IsMember(owner->GetObjectGuid())));
}
}

// Living creatures of the qualifier's entry within kHoldKillRangeYd of the bot. Never the bots' "attackers" list: the
// Portal of Madness 15141 a dead Mad Servant leaves is unselectable (unit_flags 33555200) and never attacks, so it would
// never show up there. A blocker counts while it fights, or while it can never fight at all: a loose Voidwalker that has
// not woken yet is not a blocker yet, the portal is.
uint32 HoldKillBlockersValue::Calculate()
{
    std::list<Creature*> found;
    ScanEntry(bot, QualifierEntry(qualifier), found);
    uint32 count = 0;
    for (Creature* creature : found)
    {
        if (!creature->IsAlive())
            continue;
        if (CountsAsBlocker(creature->IsInCombat(), creature->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE)))
            ++count;
    }
    return count;
}

// The qualifier of the gate value is "<entry>::<radius>": the blocker entry and the row's search radius in yards.
uint32 GateHoldFightingBlockersValue::Calculate()
{
    std::vector<std::string> const parts = Qualified::getMultiQualifiers(qualifier, "::");
    const uint32 entry = parts.empty() ? 0 : static_cast<uint32>(std::strtoul(parts[0].c_str(), nullptr, 10));
    const float radius = parts.size() > 1 ? static_cast<float>(std::strtod(parts[1].c_str(), nullptr))
                                          : kGateHoldRadiusYd;
    std::list<Creature*> found;
    ScanEntry(bot, entry, found, radius > 0.0f ? radius : kGateHoldRadiusYd);
    uint32 count = 0;
    for (Creature* creature : found)
    {
        if (!creature->IsAlive())
            continue;
        if (CountsAsFightingBlocker(creature->IsInCombat(), creature->IsInEvadeMode(),
                creature->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE)))
            ++count;
    }
    return count;
}

// The one_at_a_time candidates: every living creature of the entry within kHoldKillRangeYd of the bot that fights. A
// crowd-controlled or evading one stays on the list with `eligible` false: only PickSoleKill reads that flag (the raid
// never converges on a sheep or a mob running home), while the area veto still counts it as a held unit standing in the
// pile. A Servant of a later wave, out of combat at the portal, is not on the list, so it never becomes the pick of the
// wave the raid is on. Never the bot's own "attackers" list, which "focus rti targets" collapses to the raid mark.
std::vector<SoleCandidate> SoleKillCandidatesValue::Calculate()
{
    std::vector<SoleCandidate> candidates;
    std::list<Creature*> found;
    ScanEntry(bot, QualifierEntry(qualifier), found);
    for (Creature* creature : found)
    {
        if (!creature->IsAlive() || !creature->IsInCombat())
            continue;
        SoleCandidate candidate;
        candidate.guid = creature->GetObjectGuid().GetRawValue();
        candidate.hpPct = static_cast<int>(creature->GetHealthPercent());
        candidate.inCombat = true;
        candidate.eligible = !creature->IsInEvadeMode() && !FindNonCcTargetStrategy::IsCrowdControlled(ai, creature);
        candidates.push_back(candidate);
    }
    return candidates;
}

// The kill_first grid: every living creature of the entry within kBossTacticsEngageRange of the bot, with its
// engagement with our group and the unit that owns it (owner, else creator, else the temporary summon's summoner).
// The caller decides who may be picked (KillFirstGridEligible): the owner has to be a unit of the fight, which only
// the fight knows.
std::vector<KillFirstGridUnit> KillFirstGridUnitsValue::Calculate()
{
    std::vector<KillFirstGridUnit> units;
    std::list<Creature*> found;
    ScanEntry(bot, QualifierEntry(qualifier), found, kBossTacticsEngageRange);
    Group* const group = bot->GetGroup();
    for (Creature* creature : found)
    {
        if (!creature->IsAlive())
            continue;
        KillFirstGridUnit unit;
        unit.guid = creature->GetObjectGuid().GetRawValue();
        unit.entry = creature->GetEntry();
        unit.distance = bot->GetDistance(creature);
        unit.alive = true;
        unit.evading = creature->IsInEvadeMode();
        unit.victimOurs = IsGroupUnit(creature->GetVictim(), bot, group);
        Player* const victim = dynamic_cast<Player*>(creature->GetVictim());
        unit.victimHealer = unit.victimOurs && victim && PlayerbotAI::IsHeal(victim);
        for (Unit* attacker : creature->GetAttackers())
        {
            if (IsGroupUnit(attacker, bot, group))
            {
                unit.weHoldIt = true;
                break;
            }
        }
        // A member that pulled and stopped swinging is off the attackers list but still on the threat list: a hit in
        // combat with an NPC or a stranger is neither, and stays out.
        auto const holds = [creature](Unit* who)
        {
            return who && sServerFacade.GetThreatManager(creature).getThreat(who) > 0.0f;
        };
        unit.weHoldIt = holds(bot) || holds(bot->GetPet());
        if (!unit.weHoldIt && group)
        {
            for (Player* member : LiveGroupMembers(group))
            {
                if (member && (holds(member) || holds(member->GetPet())))
                {
                    unit.weHoldIt = true;
                    break;
                }
            }
        }
        ObjectGuid summoner = creature->GetOwnerGuid();
        if (summoner.IsEmpty())
            summoner = creature->GetCreatorGuid();
        if (summoner.IsEmpty())
        {
            if (TemporarySummon* summon = dynamic_cast<TemporarySummon*>(creature))
                summoner = summon->GetSummonerGuid();
        }
        unit.owner = summoner.GetRawValue();
        units.push_back(unit);
    }
    return units;
}

// The awaited units of a tank_post row: every creature of the entry within kTankPostSearchYd of the bot, alive or
// dead (a dead one still ends the post), with the flags the arrival read needs. The same grid search as the blocker
// count; the pick is PickTankPostUnit's.
std::vector<TankPostUnit> TankPostUnitsValue::Calculate()
{
    std::vector<TankPostUnit> units;
    std::list<Creature*> found;
    ScanEntry(bot, QualifierEntry(qualifier), found, kTankPostSearchYd);
    for (Creature* creature : found)
    {
        if (!creature)
            continue;
        TankPostUnit unit;
        unit.guid = creature->GetObjectGuid().GetRawValue();
        unit.x = creature->GetPositionX();
        unit.y = creature->GetPositionY();
        unit.z = creature->GetPositionZ();
        unit.alive = creature->IsAlive();
        unit.selectable = !creature->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);
        unit.evading = creature->IsInEvadeMode();
        units.push_back(unit);
    }
    return units;
}
