
#include "playerbot/playerbot.h"
#include "OnyxiasLairDungeonTriggers.h"
#include "playerbot/policy/combat/RangedApproachPolicy.h"
#include "playerbot/policy/raid/OnyxiaWhelpEggPolicy.h"
#include "playerbot/strategy/actions/OnyxiasLairDungeonActions.h"
#include "playerbot/strategy/values/PossibleAttackTargetsValue.h"

using namespace ai;

bool OnyxiaAirborneTrigger::IsActive()
{
    Unit* target = GetTarget();
    if (!target || !sServerFacade.IsAlive(target) || target->GetEntry() != 10184)
        return false;
    // Core drives phase 2 flight via the Hover aura (17131); IsFlying()
    // covers the transition frames where auras are mid-update.
    return target->HasAura(17131) || target->IsFlying();
}

bool OnyxiaWhelpsTrigger::IsActive()
{
    // Toggle and map first: the default install pays nothing here.
    if (!sPlayerbotAIConfig.onyxiaWhelpsAndEggs || bot->GetMapId() != kOnyxiaMapId)
        return false;
    if (!bot->IsInWorld() || bot->IsBeingTeleported() || !sServerFacade.IsAlive(bot))
        return false;
    if (!ai->HasStrategy("onyxia", BotState::BOT_STATE_COMBAT) || !IsOnyxiaWhelpDuty(ai, bot))
        return false;
    AiObjectContext* context = ai->GetAiObjectContext();
    const bool explicitOrder = !AI_VALUE(ObjectGuid, "explicit attack target").IsEmpty();
    Unit* current = AI_VALUE(Unit*, "current target");
    const bool onWhelp = current && sServerFacade.IsAlive(current) && current->GetEntry() == kOnyxianWhelpEntry;
    const OnyxiaWhelpScan scan = ScanOnyxiaWhelps(ai, bot);
    return ShouldHuntOnyxiaWhelps(true, true, scan.bossAirborne, true, explicitOrder, onWhelp, scan.whelp != nullptr);
}

bool OnyxiaEggsNearTrigger::IsActive()
{
    if (!sPlayerbotAIConfig.onyxiaWhelpsAndEggs || bot->GetMapId() != kOnyxiaMapId)
        return false;
    if (!bot->IsInWorld() || bot->IsBeingTeleported() || !sServerFacade.IsAlive(bot))
        return false;
    if (!ai->HasStrategy("onyxia", BotState::BOT_STATE_COMBAT))
        return false;
    return ShouldAvoidOnyxiaEggs(true, true, IsInOnyxiaEggZone(bot->GetPositionX(), bot->GetPositionY()),
                                 FindOnyxiaEggInDanger(ai, bot) != nullptr);
}

bool OnyxiaBossDutyTrigger::IsActive()
{
    // The key, the map and the strategy first: the default install pays nothing here.
    if (!sPlayerbotAIConfig.dungeonRunAddCrew || bot->GetMapId() != kOnyxiaMapId)
        return false;
    if (!bot->IsInWorld() || bot->IsBeingTeleported() || !sServerFacade.IsAlive(bot))
        return false;
    if (!ai->HasStrategy("onyxia", BotState::BOT_STATE_COMBAT))
        return false;
    AiObjectContext* context = ai->GetAiObjectContext();
    Unit* boss = nullptr;
    for (const ObjectGuid& attackerGuid : AI_VALUE(std::list<ObjectGuid>, "attackers"))
    {
        Unit* attacker = ai->GetUnit(attackerGuid);
        if (attacker && attacker->GetEntry() == kOnyxiaEntry && sServerFacade.IsAlive(attacker))
        {
            boss = attacker;
            break;
        }
    }
    const bool explicitOrder = !AI_VALUE(ObjectGuid, "explicit attack target").IsEmpty();
    Unit* current = AI_VALUE(Unit*, "current target");
    const bool onWhelp = current && sServerFacade.IsAlive(current) && current->GetEntry() == kOnyxianWhelpEntry;
    const OnyxiaWhelpScan scan = ScanOnyxiaWhelps(ai, bot);
    return ShouldReturnToOnyxia(true, AI_VALUE2(uint8, "add crew", static_cast<int32>(kOnyxiaEntry)),
        HoverReachTakerIsRangedDps(PlayerbotAI::IsTank(bot), PlayerbotAI::IsHeal(bot), ai->IsRanged(bot)),
        scan.bossAirborne, onWhelp, explicitOrder, boss && PossibleAttackTargetsValue::IsValid(boss, bot));
}
