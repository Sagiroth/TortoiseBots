
#include "playerbot/playerbot.h"
#include "TankTargetValue.h"
#include "playerbot/strategy/actions/FankrissTankActions.h"
#include "playerbot/strategy/actions/PatchwerkSoakerActions.h"
#include "PossibleAttackTargetsValue.h"
#include "AttackersValue.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/FourHorsemenRotationPolicy.h"
#include "playerbot/GroupMembers.h"
#include "playerbot/MainTankPolicy.h"
#include "playerbot/BossTacticsMgr.h"
#include "playerbot/strategy/actions/BossTacticsActions.h"
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

    // A loose add a tank_hold row lists (its own entry is the caster of a listed avoid_cast blast,
    // docs/play/boss-tactics.md) and that is on a non-tank player: the tank takes it before the nearest add. A ZG Mad
    // Voidwalker held by a non-tank channels, then shocks the whole raid (15 yd, ~1000-1200 shadow) once its target
    // walks away; a tank on it stops both. The preference is part of the boss tactics tactic, so it is armed only
    // while the bot runs "boss tactics" in combat: without it a comparison is exactly as on 5ef9e9771 (nearest loose
    // add first).
    bool IsTankHoldPickup(PlayerbotAI* ai, Unit* unit)
    {
        if (!unit || !ai->HasStrategy("boss tactics", BotState::BOT_STATE_COMBAT))
            return false;
        const std::vector<BossTacticRow>* rows = GetBossTactics().Rows(unit->GetMapId(), unit->GetEntry());
        if (!rows)
            return false;
        bool listed = false;
        for (const BossTacticRow& row : *rows)
        {
            if (row.kind == BossTacticKind::TankHold)
            {
                listed = true;
                break;
            }
        }
        if (!listed)
            return false;
        Player* victimPlayer = unit->GetVictim() ? dynamic_cast<Player*>(unit->GetVictim()) : nullptr;
        return victimPlayer && !ai->IsTank(victimPlayer);
    }

    // Whether the mark's own entry is listed by a tank_hold row: it is then the loose caster the row covers.
    bool MarkHasTankHoldRow(Unit* mark)
    {
        const std::vector<BossTacticRow>* rows = GetBossTactics().Rows(mark->GetMapId(), mark->GetEntry());
        if (!rows)
            return false;
        for (const BossTacticRow& row : *rows)
        {
            if (row.kind == BossTacticKind::TankHold)
                return true;
        }
        return false;
    }

    // Whether a tank_hold pickup stands nearer to this bot than `mark`: the mark then yields, so the tank takes the
    // loose add the row covers (the tournament's loose bucket ranks pickups first) instead of walking to the skull.
    bool MarkYieldsToNearerPickup(PlayerbotAI* ai, Player* bot, Unit* mark)
    {
        if (!MarkHasTankHoldRow(mark))
            return false;
        const std::list<ObjectGuid> candidates =
            ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("possible attack targets")->Get();
        bool hasPickup = false;
        float nearestPickup = 0.0f;
        for (ObjectGuid const& guid : candidates)
        {
            Unit* unit = ai->GetUnit(guid);
            if (!unit || unit == mark || !IsTankHoldPickup(ai, unit))
                continue;
            float const distance = bot->GetDistance(unit);
            if (!hasPickup || distance < nearestPickup)
            {
                hasPickup = true;
                nearestPickup = distance;
            }
        }
        return !TankTakesMark(true, hasPickup, nearestPickup, bot->GetDistance(mark));
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

        if (!attacker || !attacker->IsAlive())
            return;

        if (IsCcTarget(attacker)) return;

        // The tank's pick skips a subject a ranged_only row holds above it (the adds are its fight until it lands).
        if (IsRangedOnlyHeld(ai, ai->GetBot(), attacker)) return;

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
        // The third off-tank keeps the seek_aura source it drags: the source follows it to the spot, and a loose add
        // switching the tank off would leave the Son on the healer (M3).
        if (oldUnit && ::ai::IsSeekSourceForTankPickup(ai, bot, oldUnit))
            return false;
        // AiPlayerbot.DungeonRunTankPostKeepsUnit: a latched `tank_post` keeps its unit, even when the raid tank order
        // moved the post's row to another slot index (its record still names the unit it arrived on), and no other
        // loose add pulls the tank off it. Neither candidate latched: the ranking below is unchanged.
        std::uint64_t const oldGuid = oldUnit ? oldUnit->GetObjectGuid().GetRawValue() : 0;
        // The record's lock is taken only with the key on: off, the pick is the pre-key comparison.
        std::uint64_t const postLatched =
            sPlayerbotAIConfig.dungeonRunTankPostKeepsUnit ? ::ai::TankPostLatchedUnit(bot) : 0;
        int const postPick = ::ai::TankPostUnitPick(sPlayerbotAIConfig.dungeonRunTankPostKeepsUnit, postLatched,
            newUnit->GetObjectGuid().GetRawValue(), oldGuid);
        if (postPick > 0)
            return true;
        if (postPick < 0)
            return false;
        // A loose add a tank_hold row lists, on a non-tank, outranks the explicit main tank's stickiness below (a ZG
        // Mad Voidwalker shocks the raid while a non-tank holds it): only when exactly one of the two candidates is
        // such a pickup, so a tank holding its pickup keeps it. Without the row or the strategy both are false and the
        // order below is the pre-row one (5ef9e9771).
        bool const newPickup = IsTankHoldPickup(ai, newUnit);
        bool const oldPickup = IsTankHoldPickup(ai, oldUnit);
        if (newPickup != oldPickup)
            return newPickup;

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

        // Loose adds: nearest first, so the pickup is quick (a tank_hold pickup was already decided above).
        if (GetIntervalLevel(newUnit) == 2)
        {
            return bot->GetDistance(newUnit) < bot->GetDistance(oldUnit);
        }

        // Among mobs the tank already holds, stay on the current one; lowest
        // threat only orders the others, so two held mobs never ping-pong.
        Unit* current = ai->GetAiObjectContext()->GetValue<Unit*>("current target")->Get();
        if (oldUnit == current)
            return false;
        if (newUnit == current)
            return true;

        return newThreat < oldThreat;
    }

    // 2 = the tank holds nothing here (loose add: a tank_hold caster on a non-tank first, then nearest).
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

    // Assigned Fankriss boss tanks keep threat during their off-tank turn
    // instead of joining the loose-add pick; a spare Fankriss tank takes the
    // raid's Spawns. Other attack and mark orders still win (both return
    // nothing while the tank is ordered elsewhere).
    if (Unit* boss = GetAssignedFankrissTankTarget(ai))
        return boss;
    if (Unit* spawn = GetAssignedFankrissSpawnTankTarget(ai))
        return spawn;
    // An assigned Patchwerk main or soaker stays on Patchwerk (nothing while
    // it is ordered elsewhere).
    if (Unit* patchwerk = GetAssignedPatchwerkTankTarget(ai))
        return patchwerk;

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
    Unit* rti = RtiTargetValue::CalculateFocus();
    if (rti && AttackersValue::IsKeepoutSetAside(rti, bot))
        rti = nullptr;
    // A ranged_only subject is out of the melee tanks' reach: they take the adds and the boss's threat stays with the
    // ranged until it lands.
    if (rti && IsRangedOnlyHeld(ai, bot, rti))
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
        // The mark's own entry is a tank_hold caster on a non-tank: a loose add the row covers, nearer than the mark,
        // is the real pickup. The mark drops so the tournament's loose bucket (pickups first) takes it, instead of the
        // tank walking to the skull while the Voidwalker stands in the pile.
        if (MarkYieldsToNearerPickup(ai, bot, rti))
            rti = nullptr;
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
                // The mark this tank is on, which is Skull after the focus
                // fallback even when its own setting names another mark.
                std::string myRti = MarkOn(rti);
                if (!victimAi || victimAi->GetAiObjectContext()->GetValue<std::string>("rti")->Get() != myRti)
                    return rti;
            }
            else
                return rti;
        }
        else if (!victim)
            return rti;
    }

    FindTankTargetSmartStrategy strategy(ai);
    if (Unit* target = FindTarget(&strategy))
        return target;
    // Only CC-marked mobs left: pick the last one up instead of idling.
    return FindLastCcTarget();
}
