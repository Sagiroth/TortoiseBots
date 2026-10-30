#include "playerbot/playerbot.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <unordered_map>
#include "BossTacticsActions.h"
#include "playerbot/BossTacticsMgr.h"
#include "playerbot/ServerFacade.h"
#include "ChooseTargetActions.h"
#include "PositionAction.h"
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

// A tank the caster is on stays in the blast (BossTacticsPolicy.h HoldsCaster).
bool BotHoldsCaster(Player* bot, Unit* caster)
{
    return HoldsCaster(PlayerbotAI::IsTank(bot), caster->GetVictim() == bot);
}
}

std::vector<CastDangerZone> ai::CastDangerZones(const BossTacticsFight& fight, Player* bot)
{
    std::vector<CastDangerZone> zones;
    for (Unit* unit : fight.units)
    {
        const float radius = CastDangerOf(fight, unit);
        if (radius <= 0.0f || (bot && BotHoldsCaster(bot, unit)))
            continue;
        CastDangerZone zone;
        zone.x = unit->GetPositionX();
        zone.y = unit->GetPositionY();
        zone.z = unit->GetPositionZ();
        zone.radius = radius;
        zones.push_back(zone);
    }
    return zones;
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
        if (!MustLeaveCast(BotHoldsCaster(bot, unit), radius, distance))
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
    if (!action || !GetBossTactics().HasAvoidCastMap(bot->GetMapId()))
        return 1.0f;
    MovementAction* move = dynamic_cast<MovementAction*>(action);
    if (!move)
        return 1.0f;
    bool const walkOut = dynamic_cast<BossTacticsAvoidCastAction*>(action) != nullptr;
    if (walkOut)
        return 1.0f;
    const BossTacticsFight fight = ScanBossTactics(ai, bot);
    if (fight.rows.empty())
        return 1.0f;
    const std::vector<CastDangerZone> zones = CastDangerZones(fight, bot);
    if (zones.empty())
        return 1.0f;
    // A stay walks the bot back to its spot: held while that spot lies in a blast.
    if (dynamic_cast<ReturnToStayPositionAction*>(action))
    {
        AiObjectContext* context = ai->GetAiObjectContext();
        PositionMap& positions = AI_VALUE(PositionMap&, "position");
        auto stay = positions.find("stay");
        bool const has = stay != positions.end() && stay->second.isSet() && stay->second.mapId == bot->GetMapId();
        return has && CastHoldsMove(false, false, true, stay->second.x, stay->second.y, stay->second.z, zones) ? 0.0f
                                                                                                             : 1.0f;
    }
    // Any other move towards a unit (a reach, a chase, a heal walk, a follow): held while that unit stands in a blast.
    Unit* target = action->GetTarget();
    if (!target || !target->IsInWorld() || target->GetMapId() != bot->GetMapId())
        return 1.0f;
    return CastHoldsMove(false, target == bot, true, target->GetPositionX(), target->GetPositionY(),
                         target->GetPositionZ(), zones)
               ? 0.0f
               : 1.0f;
}

// Not MovementAction::isUseful: that one is false on a stay, and the bots of a run fight on one (a pull's anchor hold,
// a healer's stand). The blast kills in one hit; the stay walks the bot back once the cast is over.
bool BossTacticsAvoidCastAction::isUseful()
{
    return true;
}

namespace
{
// The walk-out's candidate points: the clear ring points (AvoidCastEscapes, shortest walk first), each on the ground
// and checked against every blast in 3D and for sight. Writes up to kAvoidCastPoints points; returns how many.
struct AvoidCastSpot
{
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

int AvoidCastSpots(Player* bot, Unit* caster, float radius, const std::vector<CastDangerZone>& zones, AvoidCastSpot* out)
{
    AvoidCastPoint points[kAvoidCastPoints];
    const int count = AvoidCastEscapes(caster->GetPositionX(), caster->GetPositionY(), bot->GetPositionX(),
                                       bot->GetPositionY(), radius, bot->GetOrientation() + M_PI_F, zones, points);
    int n = 0;
    for (int i = 0; i < count; ++i)
    {
        float z = std::max(caster->GetPositionZ(), bot->GetPositionZ()) + 2.0f;
        bot->UpdateGroundPositionZ(points[i].x, points[i].y, z);
        // The ground under a ring point can lie on another floor (a ledge, the stairs below) and so inside the 3D
        // radius again; another caster's blast is no way out either.
        if (InsideAnyCastZone(zones, points[i].x, points[i].y, z))
            continue;
        if (!bot->IsWithinLOS(points[i].x, points[i].y, z + bot->GetCollisionHeight()))
            continue;
        out[n].x = points[i].x;
        out[n].y = points[i].y;
        out[n].z = z;
        ++n;
    }
    return n;
}
}

// Possible only with a usable way out, so the reaction engine does not pick the walk-out for a bot that has none (it
// would keep its cast; see ShouldReactionInterruptCast).
bool BossTacticsAvoidCastAction::isPossible()
{
    if (!MovementAction::isPossible() || !ai->CanMove())
        return false;
    const BossTacticsFight fight = ScanBossTactics(ai, bot);
    const BossCastDanger danger = FindBossCastDanger(ai, bot, fight);
    if (!danger.caster)
        return false;
    AvoidCastSpot spots[kAvoidCastPoints];
    return AvoidCastSpots(bot, danger.caster, danger.radius, CastDangerZones(fight, bot), spots) > 0;
}

namespace
{
// Each bot's last walk-out (BossTacticsPolicy.h AvoidCastLogState), by bot guid, so one cast logs one line per bot.
// Only touched while bot_events.csv is on; a mutex guards it because every bot's engine reaches it.
std::mutex g_avoidCastLogMutex;
std::unordered_map<uint64, AvoidCastLogState> g_avoidCastLog;

// One bot_events.csv row per bot per cast (the audit of the boss tactics' avoid_cast walk-out; the walk-6/7 sampler of
// the bots' current action missed a walk that lasts less than its poll). Row: event BossTacticsAvoidCast, then
//   info1 = "caster=<entry>:<guid low>,spell=<id>"
//   info2 = "dist=<bot to caster centre before the move>,radius=<row radius>,dest=<distance of the point MoveTo took,
//            or none>,stopped=<1 when the walk-out broke the bot's own cast>,walk=<started|deferred|none>"
// walk: started = a walk is under way; deferred = MoveTo took the point but no walk started (a collapsed path, a
// transport or teleport wait); none = no usable point. A no-op unless bot_events.csv is in AiPlayerbot.AllowedLogFiles,
// like the hunter telemetry; the throttle is checked before any string is built.
void LogAvoidCast(PlayerbotAI* ai, Player* bot, Unit* caster, Spell* spell, uint32 spellId, float radius, float distBefore,
                  float destDistance, bool stopped, AvoidCastWalk walk)
{
    uint32 const nowMs = WorldTimer::getMSTime();
    {
        std::lock_guard<std::mutex> lock(g_avoidCastLogMutex);
        if (!AvoidCastIsNewCast(g_avoidCastLog[bot->GetObjectGuid().GetRawValue()], caster->GetObjectGuid().GetRawValue(),
                                spellId, static_cast<uint64>(reinterpret_cast<std::uintptr_t>(spell)), nowMs))
            return;
    }
    char info1[64];
    snprintf(info1, sizeof(info1), "caster=%u:%u,spell=%u", caster->GetEntry(), caster->GetGUIDLow(), spellId);
    char info2[112];
    if (destDistance >= 0.0f)
        snprintf(info2, sizeof(info2), "dist=%.1f,radius=%.1f,dest=%.1f,stopped=%d,walk=%s", distBefore, radius,
                 destDistance, stopped ? 1 : 0, AvoidCastWalkName(walk));
    else
        snprintf(info2, sizeof(info2), "dist=%.1f,radius=%.1f,dest=none,stopped=%d,walk=%s", distBefore, radius,
                 stopped ? 1 : 0, AvoidCastWalkName(walk));
    sPlayerbotAIConfig.logEvent(ai, "BossTacticsAvoidCast", info1, info2);
}

bool WalkUnderWay(Player* bot)
{
    return sServerFacade.isMoving(bot) || !bot->movespline->Finalized();
}
}

bool BossTacticsAvoidCastAction::Execute(Event& event)
{
    (void)event;
    const BossTacticsFight fight = ScanBossTactics(ai, bot);
    const BossCastDanger danger = FindBossCastDanger(ai, bot, fight);
    if (!danger.caster)
        return false;
    Unit* caster = danger.caster;
    const bool logIt = sPlayerbotAIConfig.hasLog("bot_events.csv");
    const float distBefore = logIt ? CenterDistance(caster, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ())
                                   : 0.0f;
    AvoidCastSpot spots[kAvoidCastPoints];
    const int count = AvoidCastSpots(bot, caster, danger.radius, CastDangerZones(fight, bot), spots);
    bool stopped = false;
    float destDistance = -1.0f;
    AvoidCastWalk walk = AvoidCastWalk::None;
    for (int i = 0; i < count && walk != AvoidCastWalk::Started; ++i)
    {
        // A cast of the bot's own would hold it in the blast. MoveTo breaks a running cast itself once it launches the
        // walk (MoveTo2); what is left (a wand's auto-repeat) is stopped only once a walk is under way, so a bot with
        // no way out keeps its heal or bolt.
        bool const casting = bot->IsNonMeleeSpellCasted(true);
        if (!MoveTo(bot->GetMapId(), spots[i].x, spots[i].y, spots[i].z, false, false, false, true))
            continue;
        // MoveTo can say yes without a walk (a collapsed path rebuilt next tick, a transport or teleport wait): only a
        // walk under way counts; otherwise the next point is tried.
        if (!WalkUnderWay(bot))
        {
            walk = AvoidCastWalk::Deferred;
            continue;
        }
        walk = AvoidCastWalk::Started;
        if (casting)
        {
            if (bot->IsNonMeleeSpellCasted(true))
                bot->CastStop();
            stopped = true;
        }
        if (logIt)
            destDistance = CenterDistance(caster, spots[i].x, spots[i].y, spots[i].z);
    }
    if (logIt)
    {
        Spell* spell = caster->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        LogAvoidCast(ai, bot, caster, spell, spell && spell->m_spellInfo ? spell->m_spellInfo->Id : 0, danger.radius,
                     distBefore, destDistance, stopped, walk);
    }
    return walk == AvoidCastWalk::Started;
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
