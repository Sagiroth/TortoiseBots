#include "playerbot/playerbot.h"
#include "ThaddiusDungeonActions.h"
#include "playerbot/ThaddiusPolarityPolicy.h"
#include "playerbot/strategy/ThaddiusDungeonHelper.h"
#include "playerbot/strategy/actions/GenericSpellActions.h"
#include "playerbot/strategy/actions/ChooseTargetActions.h"
#include "playerbot/strategy/actions/ReachTargetActions.h"

using namespace ai;

namespace
{
    bool PetActive(Unit* pet)
    {
        return pet && IsThaddiusPetActive(pet->IsAlive(),
            pet->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE));
    }
}

bool ThaddiusAttackNearestPetAction::isUseful()
{
    Unit* stalagg;
    Unit* feugen;
    Unit* thaddius;
    FindThaddiusAdds(ai, bot, stalagg, feugen, thaddius);

    Unit* target = nullptr;
    if (PetActive(feugen))
        target = feugen;
    if (PetActive(stalagg) && (!target || bot->GetDistance(stalagg) < bot->GetDistance(target)))
        target = stalagg;
    return target && bot->IsWithinDistInMap(target, 50.0f);
}

bool ThaddiusAttackNearestPetAction::Execute(Event& event)
{
    Unit* stalagg;
    Unit* feugen;
    Unit* thaddius;
    FindThaddiusAdds(ai, bot, stalagg, feugen, thaddius);

    Unit* target = nullptr;
    if (PetActive(feugen))
        target = feugen;
    if (PetActive(stalagg) && (!target || bot->GetDistance(stalagg) < bot->GetDistance(target)))
        target = stalagg;
    if (!target)
        return false;

    if (!bot->IsWithinDistInMap(target, 50.0f))
        return false;

    Unit* current = AI_VALUE(Unit*, "current target");
    if (current != target)
        return Attack(bot, target);

    return MoveTo(target, 0.0f);
}

bool ThaddiusMoveToPlatformAction::Execute(Event& event)
{
    // Donor edge/low spots (NaxxActions_Thaddius.cpp): pick the nearer
    // balcony edge by side; walk off through the low spot to the floor
    // center. Staged descent: MoveTo reports arrival (false) at each leg,
    // so chain edge -> low -> center instead of gating legs on Z (which
    // stuck bots on the balcony: arrival never drops Z below the gate).
    const bool leftSide = bot->GetDistance2d(3462.99f, -2918.90f) <
        bot->GetDistance2d(3520.65f, -2976.51f);
    const float edgeX = leftSide ? 3462.99f : 3520.65f;
    const float edgeY = leftSide ? -2918.90f : -2976.51f;
    const float lowX = leftSide ? 3471.36f : 3528.80f;
    const float lowY = leftSide ? -2910.65f : -2967.04f;

    if (bot->GetPositionZ() >= 309.0f)
    {
        // Leg 1: reach the edge. Arrival (false) falls to leg 2.
        if (MoveTo(bot->GetMapId(), edgeX, edgeY, 312.00f))
            return true;
        // Leg 2: off the edge through the low spot. Gravity drops Z;
        // arrival falls to leg 3.
        if (MoveTo(bot->GetMapId(), lowX, lowY, 304.02f))
            return true;
    }
    // Leg 3: floor center.
    return MoveTo(bot->GetMapId(), 3512.19f, -2928.58f, 304.02f);
}

bool ThaddiusMovePolarityAction::Execute(Event& event)
{
    const bool negative = ai->HasAura(28084, bot) || ai->HasAura(28085, bot) || ai->HasAura(29660, bot);
    const bool positive = ai->HasAura(28059, bot) || ai->HasAura(28062, bot) || ai->HasAura(29659, bot);
    const int side = ThaddiusPolaritySide(negative, positive);
    const bool ranged = ai->IsRanged(bot);

    // Donor polarity spots (same map geometry): left melee / left ranged /
    // right melee / right ranged / center melee / center ranged.
    static const float spots[6][2] = {
        { 3508.29f, -2920.12f }, { 3501.72f, -2913.36f },
        { 3519.74f, -2931.69f }, { 3524.32f, -2936.26f },
        { 3512.19f, -2928.58f }, { 3504.68f, -2936.68f },
    };
    const int idx = side * 2 + (ranged ? 1 : 0);
    return MoveTo(bot->GetMapId(), spots[idx][0], spots[idx][1], bot->GetPositionZ());
}

float ThaddiusEvenHpMultiplier::GetValue(Action* action)
{
    if (!action)
        return 1.0f;

    // One cached lookup (current target, attackers, possible
    // targets — no grid sweep per candidate action). Skip everything
    // when the pet phase is inactive.
    Unit* stalagg;
    Unit* feugen;
    Unit* thaddius;
    FindThaddiusAdds(ai, bot, stalagg, feugen, thaddius);
    const bool stalaggUp = PetActive(stalagg);
    const bool feugenUp = PetActive(feugen);

    // Pet-phase suppression (donor ThaddiusGenericMultiplier): the
    // nearest-pet action owns targeting while adds live. Donor zeroes
    // assists, ranged debuffs on attackers, reach-to-heal, tank buffs
    // and formation moves; formation moves do not exist here (generic
    // reach/flee holds positioning instead) so the other four are
    // suppressed. BuffOnTankAction covers all tank-buff spells (thorns,
    // fear ward, spirit link); melee-debuff-on-attacker stays allowed
    // per donor (only the ranged sibling is vetoed there).
    if (stalaggUp || feugenUp)
    {
        if (dynamic_cast<DpsAssistAction*>(action) || dynamic_cast<TankAssistAction*>(action))
            return 0.0f;
        if (dynamic_cast<CastRangedDebuffSpellOnAttackerAction*>(action))
            return 0.0f;
        if (dynamic_cast<ReachPartyMemberToHealAction*>(action))
            return 0.0f;
        if (dynamic_cast<BuffOnTankAction*>(action))
            return 0.0f;
    }
    if (!stalaggUp || !feugenUp)
        return 1.0f;

    Unit* target = AI_VALUE(Unit*, "current target");
    if (!target || (target != stalagg && target != feugen))
        return 1.0f;

    Unit* other = (target == stalagg) ? feugen : stalagg;
    const float targetPct = 100.0f * target->GetHealth() / target->GetMaxHealth();
    const float otherPct = 100.0f * other->GetHealth() / other->GetMaxHealth();
    if (!ShouldStopPetDps(targetPct, otherPct))
        return 1.0f;

    // Stop damage (not heals/movement): any damaging spell or assist.
    CastSpellAction* spell = dynamic_cast<CastSpellAction*>(action);
    if (spell && !dynamic_cast<CastHealingSpellAction*>(action))
        return 0.0f;
    if (dynamic_cast<DpsAssistAction*>(action) || dynamic_cast<TankAssistAction*>(action))
        return 0.0f;
    return 1.0f;
}
