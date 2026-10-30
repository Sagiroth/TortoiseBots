#include "playerbot/playerbot.h"
#include <algorithm>
#include "BossTacticsActions.h"
#include "playerbot/BossTacticsMgr.h"
#include "playerbot/ServerFacade.h"
#include "ChooseTargetActions.h"
#include "ReachTargetActions.h"
#include "playerbot/strategy/values/PossibleAttackTargetsValue.h"

using namespace ai;

namespace
{
// Interrupt spells by name; the bot casts the first one it knows and can cast
// on the target right now (range, cooldown, stance and mana are CanCastSpell's).
const char* const kInterruptSpells[] = { "pummel", "shield bash", "kick", "counterspell", "earth shock" };

// A bot on tank or healer duty keeps it; an explicit player order wins.
bool IsFreeDps(PlayerbotAI* ai, Player* bot)
{
    if (PlayerbotAI::IsTank(bot) || PlayerbotAI::IsHeal(bot))
        return false;
    AiObjectContext* context = ai->GetAiObjectContext();
    return AI_VALUE(ObjectGuid, "explicit attack target").IsEmpty();
}
}

BossTacticsFight ai::ScanBossTactics(PlayerbotAI* ai, Player* bot)
{
    BossTacticsFight fight;
    const BossTacticTable& table = GetBossTactics();
    const uint32 mapId = bot->GetMapId();
    if (!table.HasMap(mapId))
        return fight;

    AiObjectContext* context = ai->GetAiObjectContext();
    const std::list<ObjectGuid> attackers = AI_VALUE(std::list<ObjectGuid>, "attackers");
    std::vector<uint32> seen;
    for (const ObjectGuid& guid : attackers)
    {
        Unit* unit = ai->GetUnit(guid);
        if (!unit || !sServerFacade.IsAlive(unit))
            continue;
        fight.units.push_back(unit);
        const uint32 entry = unit->GetEntry();
        if (std::find(seen.begin(), seen.end(), entry) != seen.end())
            continue;
        seen.push_back(entry);
        if (const std::vector<BossTacticRow>* rows = table.Rows(mapId, entry))
        {
            for (const BossTacticRow& row : *rows)
                fight.rows.push_back(&row);
        }
    }
    return fight;
}

BossInterruptScan ai::FindBossInterrupt(PlayerbotAI* ai, Player* bot, const BossTacticsFight& fight)
{
    BossInterruptScan scan;
    (void)bot;
    for (Unit* unit : fight.units)
    {
        Spell* spell = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        if (!spell || !spell->m_spellInfo || !IsInterruptListed(fight.rows, unit->GetEntry(), spell->m_spellInfo->Id))
            continue;
        for (const char* name : kInterruptSpells)
        {
            if (ai->CanCastSpell(std::string(name), unit))
            {
                scan.caster = unit;
                scan.spell = name;
                return scan;
            }
        }
    }
    return scan;
}

Unit* ai::FindBossKillFirst(PlayerbotAI* ai, Player* bot, const BossTacticsFight& fight)
{
    if (fight.rows.empty() || !IsFreeDps(ai, bot))
        return nullptr;

    std::vector<TacticCandidate> candidates;
    candidates.reserve(fight.units.size());
    for (Unit* unit : fight.units)
    {
        TacticCandidate c;
        c.entry = unit->GetEntry();
        c.distance = bot->GetDistance(unit);
        c.pickable = PossibleAttackTargetsValue::IsValid(unit, bot);
        candidates.push_back(c);
    }
    const int index = PickKillFirst(fight.rows, candidates);
    return index < 0 ? nullptr : fight.units[index];
}

Unit* ai::FindBossFrontDanger(PlayerbotAI* ai, Player* bot, const BossTacticsFight& fight)
{
    (void)ai;
    if (PlayerbotAI::IsTank(bot))
        return nullptr;
    for (const BossTacticRow* row : fight.rows)
    {
        if (row->kind != BossTacticKind::Behind)
            continue;
        for (Unit* unit : fight.units)
        {
            if (unit->GetEntry() != TacticSubjectEntry(*row))
                continue;
            // The one holding the boss stays where it is.
            if (unit->GetVictim() == bot || !unit->GetVictim())
                continue;
            if (InsideBossFront(unit->GetPositionX(), unit->GetPositionY(), unit->GetOrientation(),
                                bot->GetPositionX(), bot->GetPositionY(), row->param))
                return unit;
        }
    }
    return nullptr;
}

float ai::CastDangerOf(const BossTacticsFight& fight, Unit* unit)
{
    if (!unit)
        return 0.0f;
    Spell* spell = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL);
    if (!spell || !spell->m_spellInfo)
        return 0.0f;
    return AvoidCastRadius(fight.rows, unit->GetEntry(), spell->m_spellInfo->Id);
}

namespace
{
// The core hits a player whose centre is within the radius of the blast's source, in 3D and with no size factor
// (Spell.cpp SpellNotifierCreatureAndPlayer, PUSH_SRC_CENTER / PUSH_SELF_CENTER); a self-centred cast's source is
// the caster's position.
float CenterDistance(Unit* caster, float x, float y, float z)
{
    const float dx = caster->GetPositionX() - x;
    const float dy = caster->GetPositionY() - y;
    const float dz = caster->GetPositionZ() - z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}
}

bool ai::InsideCastDanger(const BossTacticsFight& fight, float x, float y, float z)
{
    for (Unit* unit : fight.units)
    {
        const float radius = CastDangerOf(fight, unit);
        if (radius > 0.0f && CenterDistance(unit, x, y, z) < radius)
            return true;
    }
    return false;
}

namespace
{
// A tank-role bot the caster is on (or that nothing is on yet) stays in the blast; an off-tank leaves.
bool HoldsCaster(Player* bot, Unit* caster)
{
    return PlayerbotAI::IsTank(bot) && (!caster->GetVictim() || caster->GetVictim() == bot);
}
}

BossCastDanger ai::FindBossCastDanger(PlayerbotAI* ai, Player* bot, const BossTacticsFight& fight)
{
    (void)ai;
    BossCastDanger danger;
    if (fight.rows.empty())
        return danger;
    float nearest = 0.0f;
    for (Unit* unit : fight.units)
    {
        const float radius = CastDangerOf(fight, unit);
        if (radius <= 0.0f)
            continue;
        const float distance = CenterDistance(unit, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
        if (!MustLeaveCast(HoldsCaster(bot, unit), radius, distance))
            continue;
        if (!danger.caster || distance < nearest)
        {
            danger.caster = unit;
            danger.radius = radius;
            nearest = distance;
        }
    }
    return danger;
}

bool BossTacticsInterruptAction::Execute(Event& event)
{
    (void)event;
    const BossTacticsFight fight = ScanBossTactics(ai, bot);
    const BossInterruptScan scan = FindBossInterrupt(ai, bot, fight);
    return scan.caster && ai->CastSpell(std::string(scan.spell), scan.caster);
}

bool BossTacticsKillFirstAction::Execute(Event& event)
{
    (void)event;
    Unit* pick = FindBossKillFirst(ai, bot, ScanBossTactics(ai, bot));
    if (!pick)
        return false;
    AiObjectContext* context = ai->GetAiObjectContext();
    Unit* current = AI_VALUE(Unit*, "current target");
    if (current == pick)
        return false;
    return Attack(bot, pick);
}

float BossTacticsHoldMultiplier::GetValue(Action* action)
{
    if (!action)
        return 1.0f;
    const bool assist = dynamic_cast<DpsAssistAction*>(action) || dynamic_cast<TankAssistAction*>(action);
    const bool aoe = dynamic_cast<DpsAoeAction*>(action) != nullptr;
    if (!assist && !aoe)
        return 1.0f;
    AiObjectContext* context = ai->GetAiObjectContext();
    Unit* current = AI_VALUE(Unit*, "current target");
    if (!current)
        return 1.0f;
    return FindBossKillFirst(ai, bot, ScanBossTactics(ai, bot)) == current ? 0.0f : 1.0f;
}

float BossTacticsCastHoldMultiplier::GetValue(Action* action)
{
    if (!action || !dynamic_cast<ReachTargetAction*>(action))
        return 1.0f;
    Unit* target = action->GetTarget();
    if (!target || HoldsCaster(bot, target))
        return 1.0f;
    return CastDangerOf(ScanBossTactics(ai, bot), target) > 0.0f ? 0.0f : 1.0f;
}

bool BossTacticsAvoidCastAction::isPossible()
{
    return MovementAction::isPossible() && ai->CanMove();
}

bool BossTacticsAvoidCastAction::Execute(Event& event)
{
    (void)event;
    const BossTacticsFight fight = ScanBossTactics(ai, bot);
    const BossCastDanger danger = FindBossCastDanger(ai, bot, fight);
    if (!danger.caster)
        return false;
    Unit* caster = danger.caster;
    AvoidCastPoint points[kAvoidCastPoints];
    const int count = AvoidCastDestinations(caster->GetPositionX(), caster->GetPositionY(), bot->GetPositionX(),
                                            bot->GetPositionY(), danger.radius, bot->GetOrientation() + M_PI_F, points);
    // A cast of the bot's own would hold it in the blast.
    if (bot->IsNonMeleeSpellCasted(true))
        bot->CastStop();
    for (int i = 0; i < count; ++i)
    {
        float z = std::max(caster->GetPositionZ(), bot->GetPositionZ()) + 2.0f;
        bot->UpdateGroundPositionZ(points[i].x, points[i].y, z);
        // The ground under a ring point can lie on another floor (a ledge, the stairs below) and so inside the 3D
        // radius again; another caster's blast is no way out either.
        if (InsideCastDanger(fight, points[i].x, points[i].y, z))
            continue;
        if (!bot->IsWithinLOS(points[i].x, points[i].y, z + bot->GetCollisionHeight()))
            continue;
        if (MoveTo(bot->GetMapId(), points[i].x, points[i].y, z, false, false, false, true))
            return true;
    }
    return false;
}

bool BossTacticsStepBehindAction::isPossible()
{
    return MovementAction::isPossible() && ai->CanMove();
}

bool BossTacticsStepBehindAction::Execute(Event& event)
{
    (void)event;
    Unit* boss = FindBossFrontDanger(ai, bot, ScanBossTactics(ai, bot));
    if (!boss)
        return false;
    float x = 0.0f, y = 0.0f;
    BossFlankDestination(boss->GetPositionX(), boss->GetPositionY(), boss->GetOrientation(),
                         bot->GetPositionX(), bot->GetPositionY(), 3.0f, x, y);
    float z = boss->GetPositionZ();
    bot->UpdateGroundPositionZ(x, y, z);
    return MoveTo(bot->GetMapId(), x, y, z);
}
