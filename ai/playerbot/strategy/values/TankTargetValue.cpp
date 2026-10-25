
#include "playerbot/playerbot.h"
#include "playerbot/strategy/actions/TwinsOpeningActions.h"
#include "TankTargetValue.h"
#include "playerbot/strategy/actions/FankrissTankActions.h"
#include "playerbot/strategy/actions/OnyxiasLairDungeonActions.h"
#include "playerbot/strategy/actions/PatchwerkSoakerActions.h"
#include "RaidDpsTargetPolicy.h"
#include "GolemaggTankAssignmentValue.h"
#include "GarrTankAssignmentValue.h"
#include "NefarianAddAssignmentValue.h"
#include "PossibleAttackTargetsValue.h"
#include "AttackersValue.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/FourHorsemenRotationPolicy.h"
#include "playerbot/GroupMembers.h"
#include "playerbot/MainTankPolicy.h"
#include "../../../../runtime/PlayerbotAIStorage.h"

using namespace ai;

namespace
{
    // Explicit raid Main Tank flag (`Group::GetMainTankGuid`): the donor's
    // IsExplicitMainTank, which checks MEMBER_FLAG_MAINTANK in the member
    // slots. Our core keeps the flag as a guid on Group instead.
    bool IsExplicitMainTank(Player* bot)
    {
        Group* group = bot ? bot->GetGroup() : nullptr;
        if (!group)
            return false;
        ObjectGuid const mainTank = group->GetMainTankGuid();
        return !mainTank.IsEmpty() && mainTank == bot->getObjectGuid();
    }

    // Living tanks on the bot's map (donor GetGroupTankNum): decides whether
    // the explicit-MT pin applies. Ungrouped: this bot alone when a tank.
    unsigned GroupTankCount(PlayerbotAI* ai, Player* bot)
    {
        if (!bot || !ai->IsTank(bot))
            return 0;
        Group* group = bot->GetGroup();
        if (!group)
            return 1;
        unsigned count = 0;
        for (Player* member : LiveGroupMembers(group))
        {
            if (member && member->IsAlive() && member->IsInWorld() &&
                member->GetMapId() == bot->GetMapId() && ai->IsTank(member))
                ++count;
        }
        return count;
    }

    // Four Horsemen mark rotation: a rotating tank keeps the horseman at
    // its new corner the same way DPS does, or "tank assist" (via the tank
    // target) pulls it back to the group tank's horseman every tick while
    // the move walks it the other way. Same stored choice, same lapse
    // rules, shared helper; a stay order stands the override down.
    Unit* FourHorsemenRotationTarget(PlayerbotAI* ai, Player* bot)
    {
        if (bot->GetMapId() != 533)
            return nullptr;
        AiObjectContext* context = ai->GetAiObjectContext();
        const int fromIdx = context->GetValue<int32>("manual int", kFourHorsemenFromKey)->Get() - 1;
        const int toIdx = context->GetValue<int32>("manual int", kFourHorsemenToKey)->Get() - 1;
        auto getStacks = [&](uint32 markSpell) -> uint32
        {
            Aura* aura = ai->GetAura(markSpell, bot);
            return aura ? aura->GetStackAmount() : 0;
        };
        auto findHorseman = [&](int idx) -> Unit*
        {
            for (ObjectGuid const& guid : context->GetValue<std::list<ObjectGuid>>("attackers")->Get())
            {
                Unit* unit = ai->GetUnit(guid);
                if (unit && unit->IsAlive() && unit->GetEntry() == kFourHorsemenEntries[idx])
                    return unit;
            }
            return nullptr;
        };
        return FourHorsemenRotationTargetRaw<Unit>(fromIdx, toIdx, getStacks, findHorseman,
            ai->HasStrategy("stay", BotState::BOT_STATE_COMBAT));
    }
} // namespace

// mod-playerbots FindTankTargetSmartStrategy (a63c6b67, refined by 0a76fc1d):
// bucket attackers by GetIntervalLevel instead of a flat lowest-threat
// tournament. Loose mobs (nothing held) come first so adds get picked up;
// held mobs rank by melee reach, then lowest threat, so the tank finishes
// what it holds instead of ping-ponging. CC skips stay.
class FindTankTargetSmartStrategy : public FindNonCcTargetStrategy
{
public:
    FindTankTargetSmartStrategy(PlayerbotAI* ai) : FindNonCcTargetStrategy(ai) {}

public:
    virtual void CheckAttacker(Unit* attacker, ThreatManager* /*threatManager*/) override
    {
        Player* bot = ai->GetBot();
        AiObjectContext* context = ai->GetAiObjectContext();

        if (!attacker || !attacker->IsAlive() || IsGolemaggTankTargetExcluded(ai, attacker) ||
            IsGarrTankTargetExcluded(ai, attacker) ||
            IsNefarianAddTargetExcluded(ai, attacker))
            return;

        if (IsCcTarget(attacker)) return;

        if (!PossibleAttackTargetsValue::IsValid(attacker, bot))
        {
            std::list<ObjectGuid> attackers = AI_VALUE(std::list<ObjectGuid>, "possible attack targets");
            if (std::find(attackers.begin(), attackers.end(), attacker->getObjectGuid()) == attackers.end())
                return;
        }

        if (!result || IsBetter(attacker, result))
            result = attacker;
    }

    bool IsBetter(Unit* newUnit, Unit* oldUnit)
    {
        Player* bot = ai->GetBot();
        // Explicit main tank with an off-tank beside it holds the skull-side
        // mob it has (donor TankTargetValue.cpp:76-83): current first, every
        // other candidate after, so loose adds never pull it off. Explicit-
        // only — without the raid Main Tank flag the tournament below runs
        // unchanged, and off-tanks always peel (OffTankPeelsToAdds).
        if (MainTankSticksToCurrent(IsExplicitMainTank(bot), GroupTankCount(ai, bot)))
        {
            Unit* current = ai->GetAiObjectContext()->GetValue<Unit*>("current target")->Get();
            if (oldUnit == current)
                return false;
            if (newUnit == current)
                return true;
        }

        float newThreat = sServerFacade.GetThreatManager(newUnit).getThreat(bot);
        float oldThreat = sServerFacade.GetThreatManager(oldUnit).getThreat(bot);
        if (GetIntervalLevel(newUnit) != GetIntervalLevel(oldUnit))
            return GetIntervalLevel(newUnit) > GetIntervalLevel(oldUnit);

        // Loose adds: nearest first so the pickup is quick.
        if (GetIntervalLevel(newUnit) == 2)
            return bot->GetDistance(newUnit) < bot->GetDistance(oldUnit);

        // Among mobs the tank already holds, stay on the current one; lowest
        // threat only orders the others, so two held mobs never ping-pong.
        Unit* current = ai->GetAiObjectContext()->GetValue<Unit*>("current target")->Get();
        if (oldUnit == current)
            return false;
        if (newUnit == current)
            return true;

        return newThreat < oldThreat;
    }

    // 2 = the tank holds nothing here (loose add: pick up first, nearest).
    // 1 = held and in melee reach (finish it). 0 = held but out of reach.
    int GetIntervalLevel(Unit* unit)
    {
        if (!HasTankAggro(unit))
            return 2;

        if (ai->GetBot()->CanReachWithMeleeAutoAttack(unit))
            return 1;

        return 0;
    }

    // "Has aggro" for an arbitrary candidate, mirroring HasAggroValue
    // (values/AttackerCountValues.cpp) minus the multi-tank plumbing: the
    // live victim decides, falling back to the threat manager's victim.
    bool HasTankAggro(Unit* unit)
    {
        Player* bot = ai->GetBot();
        if (!unit)
            return false;

        if (Unit* victim = unit->GetVictim())
        {
            if (victim == bot)
                return true;
            if (Player* victimPlayer = dynamic_cast<Player*>(victim))
                return ai->IsTank(victimPlayer);
            return false;
        }

        HostileReference* ref = sServerFacade.GetThreatManager(unit).getCurrentVictim();
        Unit* victim = ref ? ref->getTarget() : nullptr;
        if (victim == bot)
            return true;
        if (Player* victimPlayer = dynamic_cast<Player*>(victim))
            return ai->IsTank(victimPlayer);
        return false;
    }
};


Unit* TankTargetValue::Calculate()
{
    if (Unit* explicitTarget = GetExplicitAttackTarget())
        return explicitTarget;

    // Explicit boss tanks keep threat during their off-tank turn instead of
    // joining the loose-add tournament. Other attack/mark orders still win.
    if (Unit* boss = GetAssignedFankrissTankTarget(ai))
        return boss;
    Unit* twin=nullptr;
    if (SelectTwinsTankTarget(ai,twin)) return twin;

    // A Four Horsemen mark carrier fights the horseman at its new corner:
    // the rotation outranks raid marks here, or the mark walks the tank
    // back into the mark it just left (DpsTargetValue keeps the same order).
    if (Unit* horseman = FourHorsemenRotationTarget(ai, bot))
        return horseman;

    // mod-playerbots conditional tank RTI (LD-5, TankTargetValue.cpp:110-131):
    // take the marked target only when its victim is a non-tank (peel for
    // them) or a tank bot with a different RTI setting (else it is the other
    // tank's mob — leave it for them). Otherwise fall through to the smart
    // tournament instead of peeling the main tank's skull.
    // (A tank never holds a creature aura set-aside; a non-tank on "tank
    // assist" does, and its set-aside mark falls through like a dps pick.)
    Unit* rti = RtiTargetValue::Calculate();
    if (rti && AttackersValue::IsKeepoutSetAside(rti, bot))
        rti = nullptr;
    // Off-tanks take loose adds first: when another tank already holds the
    // mark, the mark drops out of this tank's pick and the tournament below
    // (loose bucket first) hands it a loose add instead of doubling up on
    // the skull. The explicit main tank itself keeps the LD-5 gate below.
    if (rti && OffTankPeelsToAdds(IsExplicitMainTank(bot), GroupTankCount(ai, bot)))
    {
        Unit* victim = rti->GetVictim();
        if (victim && victim != bot)
        {
            if (Player* victimPlayer = dynamic_cast<Player*>(victim))
            {
                if (ai->IsTank(victimPlayer))
                    rti = nullptr;
            }
        }
    }
    if (rti)
    {
        Unit* victim = rti->GetVictim();
        if (victim && victim != bot)
        {
            if (Player* victimPlayer = dynamic_cast<Player*>(victim))
            {
                if (!ai->IsTank(victimPlayer))
                    return rti;
                PlayerbotAI* victimAi = PlayerbotAIStorage::Instance().GetAI(victimPlayer);
                std::string myRti = ai->GetAiObjectContext()->GetValue<std::string>("rti")->Get();
                if (!victimAi || victimAi->GetAiObjectContext()->GetValue<std::string>("rti")->Get() != myRti)
                    return rti;
            }
            else
                return rti;
        }
        else if (!victim)
            return rti;
    }

    if (Unit* assigned = GetAssignedGarrTankTarget(ai))
        return assigned;

    if (Unit* assigned = GetAssignedPatchwerkTankTarget(ai))
        return assigned;

    if (Unit* boss = GetAssignedOnyxiaLandingTarget(ai))
        return boss;

    if (Unit* whelp = GetOnyxiaWhelpTarget(ai))
        return whelp;

    if (IsNefarianAddTankOptedIn(ai))
    {
        auto const nefarianAdds = AI_VALUE(NefarianAddAssignment, "nefarian add assignment");
        if (Unit* assigned = GetAssignedNefarianAddTarget(ai, nefarianAdds))
            return assigned;
    }

    FindTankTargetSmartStrategy strategy(ai);
    if (Unit* target = FindTarget(&strategy))
        return target;
    // Only CC-marked mobs left: pick the last one up instead of idling.
    return FindLastCcTarget();
}
