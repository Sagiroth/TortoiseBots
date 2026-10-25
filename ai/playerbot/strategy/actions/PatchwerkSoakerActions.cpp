#include "playerbot/playerbot.h"
#include "PatchwerkSoakerActions.h"
#include "playerbot/PatchwerkSoakerPolicy.h"
#include "playerbot/GroupMembers.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/values/PositionValue.h"
#include "playerbot/strategy/values/PossibleAttackTargetsValue.h"
#include "../../../../runtime/PlayerbotAIStorage.h"
#include <algorithm>
#include "playerbot/strategy/generic/PullStrategy.h"

using namespace ai;
namespace
{
bool Has(PlayerbotAI* ai, char const* name)
{
    return ai->HasStrategy(name, BotState::BOT_STATE_COMBAT);
}
bool OrderedElsewhere(PlayerbotAI* ai, Unit* boss)
{
    AiObjectContext* context = ai->GetAiObjectContext();
    PositionMap& positions = AI_VALUE(PositionMap&, "position");
    auto hold = positions.find("pull hold");
    if (Has(ai,"stay") || Has(ai,"follow") || Has(ai,"wait for attack") || Has(ai,"grind") ||
        (hold != positions.end() && hold->second.isSet()))
        return true;
    Unit* target = ai->GetUnit(AI_VALUE(ObjectGuid,"explicit attack target"));
    if (!target || !PossibleAttackTargetsValue::IsValid(target,ai->GetBot(),sPlayerbotAIConfig.sightDistance,false,false))
        target = AI_VALUE(Unit*,"rti target");
    return target && target != boss &&
        PossibleAttackTargetsValue::IsValid(target,ai->GetBot(),sPlayerbotAIConfig.sightDistance,false,false);
}
}

namespace
{
bool EligibleSoaker(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    return bot->IsInWorld() && bot->IsAlive() && bot->GetMapId() == 533 &&
        !bot->IsBeingTeleported() && bot->GetGroup() && PatchwerkSoakerAssigned(ai);
}
bool LivePatchwerk(Player* bot, Unit* boss)
{
    return boss && boss->IsInWorld() && boss->GetEntry() == 16028 && boss->IsAlive() &&
        boss->IsInMap(bot) && boss->IsInCombat() && bot->IsValidAttackTarget(boss) &&
        boss->IsCreature() && !static_cast<Creature*>(boss)->IsInEvadeMode() &&
        bot->IsWithinDistInMap(boss, sPlayerbotAIConfig.sightDistance);
}
char const* MissingSoakStance(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    if (bot->GetClass() == CLASS_WARRIOR && bot->HasSpell(71) && !ai->HasAura("defensive stance", bot))
        return "defensive stance";
    if (bot->GetClass() == CLASS_DRUID && !ai->HasAura("dire bear form", bot) && !ai->HasAura("bear form", bot))
    {
        if (bot->HasSpell(9634)) return "dire bear form";
        if (bot->HasSpell(5487)) return "bear form";
    }
    return nullptr;
}
}

namespace
{
bool PatchwerkMainPull(PlayerbotAI* ai)
{
    if (!ai) return false;
    Player* bot = ai->GetBot();
    PullStrategy* pull = PullStrategy::Get(ai);
    Unit* boss = pull ? pull->GetTarget() : nullptr;
    return bot->IsInWorld() && bot->IsAlive() && !bot->IsBeingTeleported() &&
        bot->GetMapId() == 533 && bot->GetGroup() && ai->IsTank(bot) &&
        Has(ai, "patchwerk main") && !Has(ai, "patchwerk soak") &&
        pull && pull->IsCommandActive() && !pull->IsCommandPullback() &&
        pull->HasPullStarted() && !pull->HasPullActionCompleted() &&
        boss && boss->IsInWorld() && boss->IsAlive() && boss->GetEntry() == 16028 &&
        boss->IsInMap(bot) && !boss->IsInCombat() && bot->IsValidAttackTarget(boss) &&
        boss->IsCreature() && !static_cast<Creature*>(boss)->IsInEvadeMode();
}
bool PullRendezvousExpired(PullStrategy* pull)
{
    return time(nullptr) - pull->GetPullStartTime() >= pull->GetMaxPullTime();
}
}

bool ai::PatchwerkThreatTank(PlayerbotAI* ai, Unit* target)
{
    return target && target->GetEntry() == 16028 && target->IsAlive() && target->IsInCombat() &&
        ai->GetBot()->GetMapId() == 533 && target->IsInMap(ai->GetBot()) && ai->IsTank(ai->GetBot()) &&
        Has(ai, "patchwerk main") != Has(ai, "patchwerk soak");
}

PatchwerkOpeningWindow PatchwerkOpeningWindowValue::Calculate()
{
    if (group != bot->GetGroup() || instance != bot->GetInstanceId())
    { window = {}; bossGuid.Clear(); group = bot->GetGroup(); instance = bot->GetInstanceId(); }
    if (!group || bot->GetMapId() != 533 || !ai->IsTank(bot) ||
        !Has(ai, "patchwerk main") || Has(ai, "patchwerk soak"))
    {
        window = {}; bossGuid.Clear(); return window;
    }
    // Encounter lifetime, not sight range: leaving melee must not reset a
    // finished opening and impose a new delay when the boss comes back.
    auto fighting = [this](Unit* unit)
    {
        return unit && unit->GetEntry() == 16028 && unit->IsAlive() && unit->IsInWorld() &&
            unit->IsInMap(bot) && unit->IsInCombat() && unit->IsCreature() &&
            !static_cast<Creature*>(unit)->IsInEvadeMode();
    };
    PullStrategy* pull = PullStrategy::Get(ai);
    Unit* boss = ai->GetUnit(bossGuid);
    bool const ordered = pull && pull->IsCommandActive() && !pull->IsCommandPullback();
    Unit* requested = ordered ? pull->GetTarget() : nullptr;
    if (requested && requested->GetEntry() == 16028) boss = requested;
    if (!fighting(boss))
    {
        // An explicit, still-unengaged pull holds DPS during the approach too.
        if (!(boss && boss == requested && boss->IsAlive() && boss->IsInWorld() &&
              boss->IsInMap(bot) && !boss->IsInCombat() && boss->IsCreature() &&
              !static_cast<Creature*>(boss)->IsInEvadeMode()))
        {
            boss = nullptr;
            for (Player* member : LiveGroupMembers(group))
            {
                if (!member->IsAlive() || !member->IsInMap(bot)) continue;
                if (fighting(member->GetVictim())) { boss = member->GetVictim(); break; }
                for (Unit* attacker : member->GetAttackers())
                    if (fighting(attacker)) { boss = attacker; break; }
                if (boss) break;
            }
        }
    }
    bossGuid = boss ? boss->GetObjectGuid() : ObjectGuid();
    window.Observe(bossGuid.GetRawValue(), boss && boss->IsInCombat(), ordered,
        WorldTimer::getMSTime());
    return window;
}

ObjectGuid PatchwerkOpeningOwnerValue::Calculate()
{
    if (bot->GetMapId() != 533 || !bot->GetGroup()) return ObjectGuid();
    for (Player* member : LiveGroupMembers(bot->GetGroup()))
    {
        if (!member->IsInWorld() || !member->IsInMap(bot) || !sServerFacade.IsFriendlyTo(bot, member)) continue;
        PlayerbotAI* peer = PlayerbotAIStorage::Instance().GetAI(member);
        if (peer && peer->IsTank(member) && Has(peer, "patchwerk main") && !Has(peer, "patchwerk soak"))
            return member->GetObjectGuid();
    }
    return ObjectGuid();
}

bool ai::PatchwerkDpsOpeningHold(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    if (!sPlayerbotAIConfig.patchwerkOpeningDelay || bot->GetMapId() != 533 ||
        !bot->IsAlive() || !bot->GetGroup() || ai->IsTank(bot) || ai->IsHeal(bot)) return false;
    AiObjectContext* context = ai->GetAiObjectContext();
    Unit* owner = ai->GetUnit(AI_VALUE(ObjectGuid, "patchwerk opening owner"));
    if (!owner || !owner->IsPlayer() || !owner->IsInMap(bot)) return false;
    Player* main = static_cast<Player*>(owner);
    if (main->GetGroup() != bot->GetGroup()) return false;
    PlayerbotAI* peer = PlayerbotAIStorage::Instance().GetAI(main);
    if (!peer || !Has(peer, "patchwerk main") || Has(peer, "patchwerk soak")) return false;
    PatchwerkOpeningWindow window = peer->GetAiObjectContext()->GetValue<PatchwerkOpeningWindow>("patchwerk opening window")->Get();
    return window.Holds(WorldTimer::getMSTime(), sPlayerbotAIConfig.patchwerkOpeningDelay * 1000);
}

bool ai::PatchwerkOpeningBlocksSpell(PlayerbotAI* ai, uint32 spellId, Unit* target)
{
    if (ai->GetBot()->GetMapId() != 533 || !sPlayerbotAIConfig.patchwerkOpeningDelay) return false;
    // Positive heals/buffs and other encounter targets remain available.
    if (target && target != ai->GetBot() && target->GetEntry() != 16028) return false;
    SpellEntry const* spell = sServerFacade.LookupSpellInfo(spellId);
    return spell && !IsPositiveSpell(spell) && PatchwerkDpsOpeningHold(ai);
}

ObjectGuid PatchwerkSoakPullerValue::Calculate()
{
    if (!EligibleSoaker(ai)) return ObjectGuid();
    for (Player* member : LiveGroupMembers(bot->GetGroup()))
    {
        if (!member->IsInMap(bot) || !sServerFacade.IsFriendlyTo(bot, member)) continue;
        PlayerbotAI* peer = PlayerbotAIStorage::Instance().GetAI(member);
        if (PatchwerkMainPull(peer) && !PullRendezvousExpired(PullStrategy::Get(peer)))
            return member->GetObjectGuid();
    }
    return ObjectGuid();
}

Player* ai::GetPatchwerkSoakPuller(PlayerbotAI* ai)
{
    if (!EligibleSoaker(ai)) return nullptr;
    AiObjectContext* context = ai->GetAiObjectContext();
    Unit* unit = ai->GetUnit(AI_VALUE(ObjectGuid, "patchwerk soak puller"));
    if (!unit || !unit->IsPlayer()) return nullptr;
    Player* main = static_cast<Player*>(unit);
    PlayerbotAI* peer = PlayerbotAIStorage::Instance().GetAI(main);
    // Recheck the cached GUID: cancellation, death, retarget and expiry must
    // stop staging immediately, not one cache interval later.
    return main->IsInMap(ai->GetBot()) && main->GetGroup() == ai->GetBot()->GetGroup() &&
        sServerFacade.IsFriendlyTo(ai->GetBot(), main) && PatchwerkMainPull(peer) &&
        !PullRendezvousExpired(PullStrategy::Get(peer)) ? main : nullptr;
}

bool ai::PatchwerkSoakersReadyForPull(PlayerbotAI* main)
{
    if (!PatchwerkMainPull(main)) return true;
    PullStrategy* pull = PullStrategy::Get(main);
    // Never interrupt the arrow's windup. The existing pull-end trigger owns
    // timeout cleanup; an expired rendezvous must not authorize a late shot.
    if (pull->HasPullActionCast()) return true;
    if (PullRendezvousExpired(pull)) return false;
    for (Player* member : LiveGroupMembers(main->GetBot()->GetGroup()))
    {
        PlayerbotAI* peer = PlayerbotAIStorage::Instance().GetAI(member);
        if (!peer || !PatchwerkSoakerAssigned(peer)) continue;
        if (!EligibleSoaker(peer) || !member->IsInMap(main->GetBot()) ||
            !sServerFacade.IsFriendlyTo(main->GetBot(), member) || !peer->CanMove() ||
            MissingSoakStance(peer) || peer->GetAIInternalUpdateDelay() > 250 ||
            member->GetDistance(main->GetBot()) > 1.0f ||
            !member->IsWithinLOSInMap(main->GetBot(), true))
            return false;
    }
    return true;
}

bool PatchwerkSoakStageAction::Execute(Event& event)
{
    Player* main = GetPatchwerkSoakPuller(ai);
    if (!main || !isPossible()) return false;
    if (char const* stance = MissingSoakStance(ai))
        return ai->DoSpecificAction(stance, event, true);
    if (bot->GetDistance(main) <= 1.0f && bot->IsWithinLOSInMap(main, true))
    {
        if (bot->IsMoving()) ai->StopMoving();
        return true;
    }
    // A friendly rendezvous, never an Attack/Chase of an idle boss. Use the
    // standard checked path to the main's current point, including on stay.
    bool const moved = MoveTo(main->GetMapId(), main->getPositionX(), main->getPositionY(), main->getPositionZ());
    if (moved) ai->SetActionDuration(std::min<uint32>(GetDuration(), 250));
    return moved;
}

bool PatchwerkMainEngageAction::Execute(Event& event)
{
    PullStrategy* pull = PullStrategy::Get(ai);
    Unit* boss = pull ? pull->GetTarget() : nullptr;
    if (!bot->IsInWorld() || !bot->IsAlive() || bot->IsBeingTeleported() ||
        !bot->GetGroup() || !ai->IsTank(bot) || !Has(ai, "patchwerk main") ||
        Has(ai, "patchwerk soak") || bot->GetMapId() != 533 || !isPossible() ||
        !pull || !pull->IsCommandActive() || pull->IsCommandPullback() ||
        !pull->HasPullActionCast() || !LivePatchwerk(bot, boss) ||
        boss->GetThreatManager().getThreat(bot) <= 0.0f)
        return false;
    // Combat alone can precede the arrow (a healer or soaker may body-pull).
    // Require the main's actual threat before interrupting ranged auto-repeat.
    // Finish through the normal action so command, pet and movement bookkeeping
    // is identical. This also consumes the request: a peer cannot repeat it.
    if (!ai->DoSpecificAction("pull end", event, true))
        return false;
    bot->InterruptSpell(CURRENT_AUTOREPEAT_SPELL);
    ai->ClearActionClock();
    if (!Attack(GetMaster(), boss))
        return false;
    context->GetValue<Unit*>("tank target")->Reset();
    context->GetValue<bool>("invalid target", "current target")->Reset();
    if (!bot->CanReachWithMeleeAutoAttack(boss) || !bot->IsWithinLOSInMap(boss, true))
    {
        ChaseTo(boss, 0.0f, bot->GetAngle(boss));
        bot->Attack(boss, true); // ChaseTo's fallback may use Attack(false).
    }
    // Do not charge another pull-end/movement action delay before the normal
    // threat rotation. Spell GCDs remain enforced by the ordinary cast checks.
    ai->ClearActionClock();
    if (sPlayerbotAIConfig.hasLog("bot_events.csv"))
        sPlayerbotAIConfig.logEvent(ai, "PatchwerkMainEngage", boss->GetObjectGuid(),
            "shot landed; melee armed and rotation released");
    return true;
}

void ai::WakePatchwerkPullMain(PlayerbotAI* caller)
{
    Player* bot = caller->GetBot();
    if (!bot->GetGroup() || bot->GetMapId() != 533) return;
    // Only the assigned soaker's first combat transition scans the roster.
    // This handles update order: the main is armed before that soaker attacks.
    for (Player* member : LiveGroupMembers(bot->GetGroup()))
    {
        if (!member->IsInMap(bot) || !sServerFacade.IsFriendlyTo(bot, member)) continue;
        PlayerbotAI* peer = PlayerbotAIStorage::Instance().GetAI(member);
        if (!peer || !Has(peer, "patchwerk main")) continue;
        PatchwerkMainEngageAction opening(peer);
        Event event("patchwerk main opening");
        opening.Execute(event);
    }
}

ObjectGuid PatchwerkSoakBossValue::Calculate()
{
    if (!EligibleSoaker(ai))
        return ObjectGuid();
    // Bounded raid roster, no grid/world scan. Selection alone is not a pull:
    // require a live group member's actual victim or attacker in combat.
    for (Player* member : LiveGroupMembers(bot->GetGroup()))
    {
        if (!member->IsInWorld() || !member->IsAlive() || !member->IsInMap(bot) ||
            !sServerFacade.IsFriendlyTo(bot, member))
            continue;
        Unit* victim = member->GetVictim();
        if (member->IsInCombat() && LivePatchwerk(bot, victim))
            return victim->GetObjectGuid();
        for (Unit* attacker : member->GetAttackers())
            if (LivePatchwerk(bot, attacker))
                return attacker->GetObjectGuid();
    }
    return ObjectGuid();
}

Unit* ai::GetPatchwerkSoakBoss(PlayerbotAI* ai)
{
    if (!EligibleSoaker(ai))
        return nullptr;
    AiObjectContext* context = ai->GetAiObjectContext();
    Unit* boss = ai->GetUnit(AI_VALUE(ObjectGuid, "patchwerk soak boss"));
    // Cached GUIDs never extend the stay exception beyond death/evade.
    return LivePatchwerk(ai->GetBot(), boss) ? boss : nullptr;
}

bool PatchwerkSoakEngageAction::isUseful()
{
    Unit* boss = GetPatchwerkSoakBoss(ai);
    return boss && (MissingSoakStance(ai) || bot->GetVictim() != boss ||
        !bot->HasUnitState(UNIT_STAT_MELEE_ATTACKING) ||
        !bot->CanReachWithMeleeAutoAttack(boss) || !bot->IsWithinLOSInMap(boss, true));
}

bool PatchwerkSoakEngageAction::Execute(Event& event)
{
    Unit* boss = GetPatchwerkSoakBoss(ai);
    if (!boss || !isPossible())
        return false;
    // Prepare mitigation before arming an attack or running into Hateful range.
    if (char const* stance = MissingSoakStance(ai))
        return ai->DoSpecificAction(stance, event, true);
    if (bot->IsNonMeleeSpellCasted(true, false, true))
        return false;
    if (bot->GetVictim() != boss || !bot->HasUnitState(UNIT_STAT_MELEE_ATTACKING))
    {
        if (!Attack(GetMaster(), boss))
            return false;
        context->GetValue<Unit*>("tank target")->Reset();
        context->GetValue<bool>("invalid target", "current target")->Reset();
    }
    if (!bot->CanReachWithMeleeAutoAttack(boss) || !bot->IsWithinLOSInMap(boss, true))
    {
        bool const moved = ChaseTo(boss, 0.0f, bot->GetAngle(boss));
        // ChaseTo's core fallback arms a chase with Attack(false). Restore
        // melee swings so arrival never needs another attack command.
        bot->Attack(boss, true);
        if (moved)
            ai->SetActionDuration(std::min<uint32>(GetDuration(), 1000));
        return moved;
    }
    return true;
}

Unit* ai::GetPatchwerkBoss(PlayerbotAI* ai)
{
    if (Unit* boss = GetPatchwerkSoakBoss(ai))
        return boss;
    Player* bot = ai->GetBot();
    if (bot->GetMapId() != 533 || !bot->IsInWorld() || !bot->IsAlive() ||
        bot->IsBeingTeleported() || !bot->IsInCombat() ||
        (!Has(ai,"patchwerk main") && !Has(ai,"patchwerk soak") && !Has(ai,"patchwerk melee safety")))
        return nullptr;
    AiObjectContext* context = ai->GetAiObjectContext();
    for (ObjectGuid const& guid : AI_VALUE(std::list<ObjectGuid>,"attackers"))
    {
        Unit* boss = ai->GetUnit(guid);
        if (boss && boss->GetEntry() == 16028 && boss->IsAlive() && boss->IsInCombat() &&
            boss->IsInMap(bot) && !OrderedElsewhere(ai,boss))
            return boss;
    }
    return nullptr;
}

bool ai::PatchwerkSoakerAssigned(PlayerbotAI* ai)
{
    return ai->IsTank(ai->GetBot()) && Has(ai,"patchwerk soak") && !Has(ai,"patchwerk main");
}

Unit* ai::GetAssignedPatchwerkTankTarget(PlayerbotAI* ai)
{
    if (ai->GetBot()->GetMapId() != 533 || !ai->IsTank(ai->GetBot()) ||
        Has(ai,"patchwerk main") == Has(ai,"patchwerk soak"))
        return nullptr;
    return GetPatchwerkBoss(ai);
}

bool ai::PatchwerkNeedsMeleeHold(PlayerbotAI* ai, Unit* boss)
{
    if (!boss || !Has(ai,"patchwerk melee safety") || ai->IsTank(ai->GetBot()))
        return false;
    Player* main = nullptr;
    std::vector<uint64> soakers;
    SpellEntry const* strike = sSpellTemplate.LookupEntry<SpellEntry>(28308);
    if (!strike)
        return true;
    for (Player* member : LiveGroupMembers(ai->GetBot()->GetGroup()))
    {
        PlayerbotAI* candidate = member ? PlayerbotAIStorage::Instance().GetAI(member) : nullptr;
        if (!candidate || !candidate->IsTank(member))
            continue;
        bool isMain = Has(candidate,"patchwerk main");
        bool isSoak = Has(candidate,"patchwerk soak");
        if (!isMain && !isSoak)
            continue;
        if (isMain == isSoak || !member->IsAlive() || !member->IsInWorld() ||
            member->IsBeingTeleported() || !member->IsInMap(boss) ||
            !member->IsInCombat() || OrderedElsewhere(candidate,boss) ||
            !boss->CanReachWithMeleeSpellAttack(member) || member->IsImmuneToSpell(strike,false))
            return true;
        if (isMain)
        {
            if (main)
                return true;
            main = member;
        }
        else
            soakers.push_back(member->GetObjectGuid().GetRawValue());
    }
    if (!main || boss->GetVictim() != main)
        return true;
    std::vector<PatchwerkThreatPlayer> players;
    for (HostileReference const* ref : boss->GetThreatManager().getThreatList())
    {
        if (!ref->getUnitGuid().IsPlayer())
            continue;
        Player* member = boss->GetMap()->GetPlayer(ref->getUnitGuid());
        if (!member || !member->IsInMap(boss))
            continue;
        bool eligible = member->IsAlive() && boss->CanReachWithMeleeSpellAttack(member) &&
            !member->IsImmuneToSpell(strike,false);
        players.push_back({member->GetObjectGuid().GetRawValue(),eligible});
    }
    return !PatchwerkMeleeSlotsSafe(main->GetObjectGuid().GetRawValue(),soakers,players,
        ai->GetBot()->GetObjectGuid().GetRawValue());
}

bool PatchwerkMeleeRetreatAction::isUseful()
{
    Unit* boss = GetPatchwerkBoss(ai);
    return PatchwerkNeedsMeleeHold(ai,boss) &&
        bot->GetDistance2d(boss,SizeFactor::None) < boss->GetCombatReach(bot,true,0.0f) + 2.0f;
}

bool PatchwerkMeleeRetreatAction::Execute(Event& event)
{
    Unit* boss = GetPatchwerkBoss(ai);
    if (!PatchwerkNeedsMeleeHold(ai,boss))
        return false;
    ai->StopMoving();
    bot->AttackStop();
    WorldPosition start(bot), center(boss);
    float const radius = boss->GetCombatReach(bot,true,0.0f) + 3.0f;
    if (bot->GetDistance2d(boss,SizeFactor::None) >= radius - 1.0f)
        return false;
    float const away = center.GetAngleTo(start);
    for (float offset : {0.0f,0.35f,-0.35f,0.7f,-0.7f,1.05f,-1.05f})
    {
        float angle = away + offset;
        WorldPosition point = center + WorldPosition(0,radius*cos(angle),radius*sin(angle),0.0f);
        point.setZ(point.GetHeight());
        if (!std::isfinite(point.getZ()) || fabs(point.getZ()-start.getZ()) > 5.0f || IsHazardNearPosition(point) ||
            !bot->IsWithinLOS(point.getX(),point.getY(),point.getZ()+bot->GetCollisionHeight()) ||
            !start.canPathTo(point,bot))
            continue;
        auto path = start.GetPathTo(point,bot);
        if (!point.isPathTo(path))
            continue;
        bool safe = true;
        float length = 0.0f;
        float const initialRadius = bot->GetDistance2d(boss,SizeFactor::None);
        WorldPosition previous = start;
        for (WorldPosition const& waypoint : path)
        {
            float distance = previous.distance(waypoint);
            length += distance;
            if (!std::isfinite(distance) || length > 35.0f)
            {
                safe = false;
                break;
            }
            unsigned steps = std::max(1u,unsigned(std::ceil(distance)));
            for (unsigned i = 1; i <= steps; ++i)
            {
                float t = float(i)/steps;
                WorldPosition sample(533,
                    previous.getX()+(waypoint.getX()-previous.getX())*t,
                    previous.getY()+(waypoint.getY()-previous.getY())*t,
                    previous.getZ()+(waypoint.getZ()-previous.getZ())*t);
                if (!std::isfinite(sample.getZ()) || fabs(sample.getZ()-start.getZ()) > 5.0f ||
                    IsHazardNearPosition(sample) || std::hypot(sample.getX()-center.getX(),
                    sample.getY()-center.getY())+0.5f < initialRadius)
                    safe = false;
            }
            previous = waypoint;
        }
        for (ObjectGuid const& guid : AI_VALUE(std::list<ObjectGuid>,"possible targets"))
        {
            Unit* unit = ai->GetUnit(guid);
            if (unit && unit->IsAlive() && !unit->IsInCombat() &&
                sServerFacade.getDistance2d(unit,point.getX(),point.getY()) < sPlayerbotAIConfig.aggroDistance)
                safe = false;
        }
        if (safe && MoveTo(533,point.getX(),point.getY(),point.getZ(),false,IsReaction(),false,true))
        {
            if (IsReaction())
                WaitForReach(start.distance(point));
            return true;
        }
    }
    return false;
}
