#include "playerbot/playerbot.h"
#include "BossTacticsTriggers.h"
#include "playerbot/BossTacticsMgr.h"
#include "playerbot/BossStandMgr.h"
#include "playerbot/GroupMembers.h"
#include "playerbot/strategy/actions/BossTacticsActions.h"
#include "playerbot/strategy/values/PartyMemberToHeal.h"
#include "Pet.h"
#include "../../../../runtime/CommandedGroupQuery.h"

using namespace ai;

namespace
{
// A map has rows when either table names it: a stand set alone arms the strategy, as a tactics row does.
bool MapHasRows(uint32 mapId)
{
    return GetBossTactics().HasMap(mapId) || GetBossStands().HasMap(mapId);
}

bool TacticsWanted(Player* bot)
{
    if (!bot->IsInWorld() || bot->IsBeingTeleported())
        return false;
    // Map first: off every map without rows, and the commander lookup only runs
    // where it could matter.
    const bool mapHasRows = MapHasRows(bot->GetMapId());
    if (!mapHasRows)
        return false;
    const bool optIn = sPlayerbotAIConfig.dungeonTactics;
    return BossTacticsWanted(optIn, !optIn && TortoiseBots::BotCommands::IsInCommandedGroup(bot), mapHasRows);
}

bool Ready(PlayerbotAI* ai, Player* bot)
{
    return bot->IsInWorld() && !bot->IsBeingTeleported() && sServerFacade.IsAlive(bot) &&
           MapHasRows(bot->GetMapId()) && ai->HasStrategy("boss tactics", BotState::BOT_STATE_COMBAT);
}
}

bool BossTacticsEnterTrigger::IsActive()
{
    return !ai->HasStrategy("boss tactics", BotState::BOT_STATE_COMBAT) && TacticsWanted(bot);
}

bool BossTacticsLeaveTrigger::IsActive()
{
    if (!ai->HasStrategy("boss tactics", BotState::BOT_STATE_COMBAT))
        return false;
    // A bot in a teleport or loading screen keeps the strategy until it lands.
    if (!bot->IsInWorld() || bot->IsBeingTeleported())
        return false;
    return !TacticsWanted(bot);
}

bool BossTacticsInterruptTrigger::IsActive()
{
    if (!Ready(ai, bot))
        return false;
    const BossTacticsFight fight = ScanBossTactics(ai, bot);
    return !fight.rows.empty() && FindBossInterrupt(ai, bot, fight).caster != nullptr;
}

bool BossTacticsDispelSweepTrigger::IsActive()
{
    if (!Ready(ai, bot))
        return false;
    return SweepBossDispelCures(ai, bot, false);
}

bool BossTacticsKillFirstTrigger::IsActive()
{
    if (!Ready(ai, bot))
        return false;
    const BossTacticsFight fight = ScanBossTactics(ai, bot);
    Unit* pick = FindBossKillFirst(ai, bot, fight);
    if (!pick)
    {
        // A Delusions carrier with a Shade the pick did not take is the case this trigger is the only one to see (a
        // pick would put the action on it): one row per bot per 5 s says why (t1106b).
        LogCarrierNoSwitch(ai, bot, fight, nullptr, AI_VALUE(Unit*, "current target"));
        return false;
    }
    AiObjectContext* context = ai->GetAiObjectContext();
    return AI_VALUE(Unit*, "current target") != pick;
}

bool BossTacticsHoldKillTrigger::IsActive()
{
    if (!Ready(ai, bot))
        return false;
    const BossTacticsFight fight = ScanBossTactics(ai, bot);
    return !fight.rows.empty() && FindBossHoldKill(ai, bot, fight) != nullptr;
}

bool BossTacticsDpsHoldTrigger::IsActive()
{
    if (!Ready(ai, bot))
        return false;
    AiObjectContext* context = ai->GetAiObjectContext();
    Unit* current = AI_VALUE(Unit*, "current target");
    return current && IsDpsHeld(ai, bot, current);
}

bool BossTacticsLinkedDeathTrigger::IsActive()
{
    if (!Ready(ai, bot))
        return false;
    Unit* pick = FindBossLinkedDeath(ai, bot);
    if (!pick)
        return false;
    AiObjectContext* context = ai->GetAiObjectContext();
    return AI_VALUE(Unit*, "current target") != pick;
}

bool BossTacticsFrontTrigger::IsActive()
{
    if (!Ready(ai, bot))
        return false;
    const BossTacticsFight fight = ScanBossTactics(ai, bot);
    return !fight.rows.empty() && FindBossFrontDanger(ai, bot, fight) != nullptr;
}

bool BossTacticsFreezeTrigger::IsActive()
{
    if (!Ready(ai, bot))
    {
        // The aura may have ended without another freeze read (the strategy left, the bot left the map): still write
        // the held window's row.
        FreezeEpisodeTick(ai, bot, 0);
        return false;
    }
    // The scan drops the six aura kinds with the key off (and a map without one has none to read), so this trigger's
    // answer would always be no: the reaction engine runs it every update, so do not pay for the scan.
    if (!sPlayerbotAIConfig.dungeonRunAuraTactics || !GetBossTactics().HasAuraTacticMap(bot->GetMapId()))
    {
        FreezeEpisodeTick(ai, bot, 0);
        return false;
    }
    const BossTacticsFight fight = ScanBossTactics(ai, bot);
    const std::uint32_t aura = FreezeAuraOnBot(ai, bot, fight);
    FreezeEpisodeTick(ai, bot, aura);
    return aura != 0;
}

bool BossTacticsFleeTrigger::IsActive()
{
    if (!Ready(ai, bot))
        return false;
    if (!sPlayerbotAIConfig.dungeonRunAuraTactics || !GetBossTactics().HasAuraTacticMap(bot->GetMapId()))
        return false;
    const BossTacticsFight fight = ScanBossTactics(ai, bot);
    const BossTacticRow* row = ActiveFleeRow(ai, bot, fight);
    if (!row)
        return false;
    if (row->mode == TacticMode::ToTank)
    {
        if (PlayerbotAI::IsTank(bot))
            return false;
        Player* tank = GetRaidTankRoles(ai).MainTank();
        if (!tank || tank == bot || !sServerFacade.IsAlive(tank) || tank->GetMapId() != bot->GetMapId())
            return false;
        const float radius = row->param > 0.0f ? row->param : 5.0f;
        return bot->GetDistance2d(tank) > radius;
    }
    Group* group = bot->GetGroup();
    if (!group)
        return false;
    const float radius = row->param > 0.0f ? row->param : sPlayerbotAIConfig.bombRunoutDistance;
    for (Player* member : LiveGroupMembers(group))
    {
        if (!member || member == bot || !sServerFacade.IsAlive(member) || member->GetMapId() != bot->GetMapId())
            continue;
        if (bot->GetDistance2d(member) < radius)
            return true;
    }
    return false;
}

bool BossTacticsAvoidCastTrigger::IsActive()
{
    if (!Ready(ai, bot))
        return false;
    return FindBossCastDanger(ai, bot, ScanBossTactics(ai, bot)).caster != nullptr;
}

bool BossTacticsStandTrigger::IsActive()
{
    if (!Ready(ai, bot))
        return false;
    const BossStandTarget stand = FindBossStand(ai, bot);
    return stand.valid && !StandArrived(bot->GetPositionX(), bot->GetPositionY(), stand.x, stand.y);
}

bool BossTacticsUseObjectTrigger::IsActive()
{
    if (!Ready(ai, bot))
        return false;
    return FindBossUseObject(ai, bot).valid;
}

bool BossTacticsTankSwapTrigger::IsActive()
{
    if (!Ready(ai, bot))
        return false;
    // Both keys gate the kind: it is one of the six aura kinds, and it changes what the tanks do mid-fight.
    if (!sPlayerbotAIConfig.dungeonRunTankSwap || !sPlayerbotAIConfig.dungeonRunAuraTactics ||
        !GetBossTactics().HasAuraTacticMap(bot->GetMapId()))
        return false;
    return FindBossTankSwap(ai, bot, ScanBossTactics(ai, bot)).valid;
}

bool BossTacticsTankSwapHoldTrigger::IsActive()
{
    if (!Ready(ai, bot))
        return false;
    // Both keys gate the kind: it is one of the six aura kinds, and it changes what the tanks do mid-fight.
    if (!sPlayerbotAIConfig.dungeonRunTankSwap || !sPlayerbotAIConfig.dungeonRunAuraTactics ||
        !GetBossTactics().HasAuraTacticMap(bot->GetMapId()) || !PlayerbotAI::IsTank(bot))
        return false;
    return TankSwapStandDownBoss(ai, bot, ScanBossTactics(ai, bot)) != nullptr;
}

bool BossTacticsObjectAnchorTrigger::IsActive()
{
    if (!Ready(ai, bot))
        return false;
    return FindBossUseObjectAnchor(ai, bot).valid;
}

bool BossTacticsSeekTrigger::IsActive()
{
    if (!Ready(ai, bot))
        return false;
    return FindBossSeek(ai, bot).valid;
}

bool BossTacticsSeekPetTrigger::IsActive()
{
    if (!Ready(ai, bot))
        return false;
    Pet* pet = bot->GetPet();
    return pet && pet->IsAlive() && BossSeekPetMustLeave(ai, bot);
}

bool BossTacticsSeekTotemTrigger::IsActive()
{
    if (!Ready(ai, bot) || bot->GetClass() != CLASS_SHAMAN)
        return false;
    for (int slot = 0; slot < MAX_TOTEM_SLOT; ++slot)
    {
        if (!bot->GetTotemGuid(TotemSlot(slot)).IsEmpty())
            return BossSeekTotemsMustLeave(ai, bot);
    }
    return false;
}

bool BossTacticsSeekTankTrigger::IsActive()
{
    if (!Ready(ai, bot))
        return false;
    return FindBossSeekTank(ai, bot).valid;
}

bool BossTacticsSeekKillTrigger::IsActive()
{
    if (!Ready(ai, bot))
        return false;
    Unit* pick = FindBossSeekKill(ai, bot);
    if (!pick)
        return false;
    AiObjectContext* context = ai->GetAiObjectContext();
    return AI_VALUE(Unit*, "current target") != pick;
}

bool BossTacticsTauntAfterTrigger::IsActive()
{
    if (!Ready(ai, bot))
        return false;
    // The kind is the aura-tactics key's own (ScanBossTactics drops its rows otherwise), and the map flag keeps every
    // other map at one lookup.
    if (!sPlayerbotAIConfig.dungeonRunAuraTactics || !GetBossTactics().HasAuraTacticMap(bot->GetMapId()))
        return false;
    return FindBossTauntAfter(ai, bot, ScanBossTactics(ai, bot)).valid;
}

bool BossTacticsBurnManaTrigger::IsActive()
{
    if (!Ready(ai, bot) || !sPlayerbotAIConfig.dungeonRunBurnMana)
        return false;
    return FindBossBurnMana(ai, bot, ScanBossTactics(ai, bot)) != nullptr;
}

bool BossTacticsRangedOnlyTrigger::IsActive()
{
    if (!Ready(ai, bot))
        return false;
    Unit* current = AI_VALUE(Unit*, "current target");
    float gapYd = 0.0f;
    if (!current || !IsRangedOnlyHeld(ai, bot, current, &gapYd))
        return false;
    LogBossRangedOnly(ai, bot, current, gapYd);
    return true;
}
