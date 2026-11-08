
#include "playerbot/playerbot.h"
#include "AttackerOfNonTankTargetValue.h"
#include "playerbot/policy/combat/AttackerOfNonTankPolicy.h"
#include "TargetValue.h"
#include "CcMarkHoldPolicy.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/BossTacticsMgr.h"

using namespace ai;

Unit* AttackerOfNonTankTargetValue::Calculate()
{
    std::list<ObjectGuid> const& attackers =
        ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("possible attack targets")->Get();
    CcGuard ccGuard(ai);
    // A held CC mark may be touched only by the tank's Taunt under the
    // held-mark exception; the IsTank walk and the scan radius never change
    // inside the loop, so hoist them out. The radius only caps which mobs
    // are considered; the taunt's own range comes from the spell and its
    // reach prerequisite walks the tank in.
    bool const tankTaunt = ai->IsTank(bot);
    float const scanRadius = ai->GetRange("spell");
    Player* const master = ai->GetMaster();

    std::vector<AttackerOfNonTankCandidate> candidates;
    std::vector<Unit*> units;
    for (ObjectGuid guid : attackers)
    {
        Unit* unit = ai->GetUnit(guid);
        if (!unit || unit->IsPlayer())
            continue;
        // A needs_aura subject (the Shade of Jin'do) is no pick for either user, the tank's taunt or the warrior gap
        // closer's Charge: a bot without the aura cannot hurt it (DungeonRunTauntGate, off = every unit as before).
        if (AttackerOfNonTankSkipsUnit(sPlayerbotAIConfig.dungeonRunTauntGate,
                GetBossTactics().RequiredAura(bot->GetMapId(), unit->GetEntry())))
            continue;

        Unit* victim = unit->GetVictim();
        Player* victimPlayer = victim ? victim->ToPlayer() : nullptr;
        if (!victimPlayer)
            continue;

        AttackerOfNonTankCandidate candidate;
        candidate.victimIsGroupPlayer = victimPlayer == bot || victimPlayer == master ||
            (bot->GetGroup() && victimPlayer->GetGroup() == bot->GetGroup());
        if (!candidate.victimIsGroupPlayer)
            continue;

        candidate.victimIsTank = ai->IsTank(victimPlayer);
        candidate.victimIsHealer = PlayerbotAI::IsHeal(victimPlayer);
        candidate.distance = sServerFacade.getDistance2d(bot, unit);
        if (candidate.distance > scanRadius)
            continue;

        candidate.heldCcMark = ccGuard.IsHeldCcMark(unit);
        if (candidate.heldCcMark)
            candidate.heldMarkTauntAllowed = CcHeldMarkTauntAllowed(tankTaunt,
                FindNonCcTargetStrategy::IsCrowdControlled(ai, unit),
                ccGuard.GroupCastsCcAt(unit), candidate.victimIsHealer);

        candidates.push_back(candidate);
        units.push_back(unit);
    }

    std::size_t const pick = AttackerOfNonTankPick(candidates);
    return pick < units.size() ? units[pick] : NULL;
}
