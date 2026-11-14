#include "playerbot/playerbot.h"
#include <algorithm>
#include <cmath>
#include <string>
#include "ObjectAccessor.h"
#include "BossTacticsTriggers.h"
#include "playerbot/BossTacticsMgr.h"
#include "playerbot/BossStandMgr.h"
#include "playerbot/GroupMembers.h"
#include "playerbot/ThreatCapPolicy.h"
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

// The dispel types this class can cure with its own spells, for the carrier strike's dispel guard. CLASS_* ids are kept
// literal because BossTacticsPolicy.h stays free of the core includes:
//   priest 5: Dispel Magic (magic), Cure Disease (disease);
//   paladin 2: Cleanse (magic/poison/disease), Purify (poison/disease);
//   druid 11: Remove Curse (curse), Cure/Abolish Poison (poison);
//   shaman 7: Cure Poison, Cure Disease and their cleansing totems;
//   mage 8: Remove Curse.
std::vector<uint32> CarrierCureTypes(uint8 cls)
{
    switch (cls)
    {
        case 5:  return {DISPEL_MAGIC, DISPEL_DISEASE};
        case 2:  return {DISPEL_MAGIC, DISPEL_POISON, DISPEL_DISEASE};
        case 11: return {DISPEL_CURSE, DISPEL_POISON};
        case 7:  return {DISPEL_POISON, DISPEL_DISEASE};
        case 8:  return {DISPEL_CURSE};
        default: return {};
    }
}

// A member in reach carries an aura a dispel_priority row names in mode `now` (an empty mode word is `now`), of a type
// this bot's class can cure. The dispel rows the fight scan kept are the authority, as for the cure itself.
bool CarrierDispelNowPending(PlayerbotAI* ai, Player* bot, const ActiveBossRows& rows)
{
    std::vector<uint32> const cureTypes = CarrierCureTypes(bot->GetClass());
    if (cureTypes.empty())
        return false;
    float const reach = bot->InBattleGround() ? sPlayerbotAIConfig.healDistanceBg : ai->GetRange("heal");
    auto const memberDue = [&](Unit* member)
    {
        if (!member || !member->IsInWorld() || !member->IsAlive() || member->GetMapId() != bot->GetMapId())
            return false;
        if (member != bot && sServerFacade.getDistance2d(bot, member) > reach)
            return false;
        std::vector<AuraOnMember> auras;
        CollectAllDispelAuras(member, auras);
        for (const AuraOnMember& aura : auras)
        {
            if (std::find(cureTypes.begin(), cureTypes.end(), aura.dispelType) == cureTypes.end())
                continue;
            for (const BossTacticRow* row : rows)
            {
                if (row->kind != BossTacticKind::DispelPriority || row->spellId != aura.spellId)
                    continue;
                if (row->mode == TacticMode::Now || row->mode == TacticMode::Default)
                    return true;
            }
        }
        return false;
    };
    if (memberDue(bot))
        return true;
    Group* group = bot->GetGroup();
    if (group)
        for (Player* member : LiveGroupMembers(group))
            if (member && member != bot && memberDue(member))
                return true;
    return false;
}

// The strike the action would use, for the log row: a melee off-tank, the first castable class spell for a healer, or
// none.
std::string CarrierStrikeSpellText(PlayerbotAI* ai, Player* bot, CarrierStrikeVerdict verdict, CarrierStrikeRole role,
    Unit* shade)
{
    if (verdict != CarrierStrikeVerdict::Strike)
        return "none";
    if (role == CarrierStrikeRole::OffTank)
        return "melee";
    if (role != CarrierStrikeRole::Healer || !shade)
        return "none";
    AiObjectContext* context = ai->GetAiObjectContext();
    for (const char* name : CarrierStrikeSpells(bot->GetClass()))
    {
        std::string const spell(name);
        if (!ai->CanCastSpell(spell, shade))
            continue;
        uint32 const spellId = AI_VALUE2(uint32, "spell id", spell);
        return spellId ? std::to_string(spellId) : std::string("none");
    }
    return "none";
}

// A carrier strike cast already flying at the Shade is interrupted once the verdict stops being Strike: a Smite that
// lands after the raid dropped below the guard would defeat the guard.
void InterruptCarrierStrikeCast(PlayerbotAI* ai, Player* bot, uint32 shadeEntry)
{
    Spell* const current = bot->GetCurrentSpell(CURRENT_GENERIC_SPELL);
    if (!current || !current->m_spellInfo)
        return;
    // The cast's cached unit-target pointer is refreshed only in the bot's next Spell::update, and the map frees a unit
    // removed in this tick (Jin'do's death despawns the Shades), so resolve the guid the cast holds: a target that left
    // the world reads none.
    ObjectGuid const targetGuid = current->m_targets.getUnitTargetGuid();
    Unit* const target = targetGuid ? ObjectAccessor::GetUnit(*bot, targetGuid) : nullptr;
    if (!target || target->GetEntry() != shadeEntry)
        return;
    AiObjectContext* context = ai->GetAiObjectContext();
    for (const char* name : CarrierStrikeSpells(bot->GetClass()))
    {
        std::string const spell(name);
        uint32 const spellId = AI_VALUE2(uint32, "spell id", spell);
        if (spellId != 0 && spellId == current->m_spellInfo->Id)
        {
            bot->InterruptNonMeleeSpells(false);
            return;
        }
    }
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
    // The Shade cover row (walk 56 F3): a carrier names the cursed bots that leave a Shade on a healer alone.
    LogShadeCover(ai, bot, fight);
    Unit* pick = FindBossKillFirst(ai, bot, fight);
    if (!pick)
    {
        // A Delusions carrier with a Shade the pick did not take is the case this trigger is the only one to see (a
        // pick would put the action on it): one row per bot per 5 s says why (t1106b).
        LogCarrierNoSwitch(ai, bot, fight, nullptr, AI_VALUE(Unit*, "current target"));
        return false;
    }
    return KillFirstNeedsAttack(true, AI_VALUE(Unit*, "current target") == pick, bot->GetVictim() == pick);
}

bool BossTacticsCarrierStrikeTrigger::IsActive()
{
    if (!sPlayerbotAIConfig.dungeonRunCarrierStrike)
        return false;
    if (!Ready(ai, bot))
        return false;
    // The kill_first node already covers every battle DPS; this one is for the healers and off-tanks.
    if (IsBattleDps(bot))
        return false;
    const BossTacticsFight fight = ScanBossTactics(ai, bot);
    if (!HasCarrierKillFirstRow(fight.rows))
    {
        SetShadeCoverLatch(bot, ShadeCoverLatch()); // the curse is over: the cover latch ends with it
        return false;
    }

    const BossTacticRow* carrierRow = nullptr;
    for (const BossTacticRow* row : fight.rows)
        if (CarrierKillFirstRow(*row))
        {
            carrierRow = row;
            break;
        }
    if (!carrierRow)
        return false;
    uint32 const shadeEntry = TacticSubjectEntry(*carrierRow);
    uint32 const aura = carrierRow->botAura;

    RaidTankRoles const tankRoles = GetRaidTankRoles(ai);
    CarrierStrikeRole role = CarrierStrikeRole::Dps;
    if (PlayerbotAI::IsHeal(bot))
        role = CarrierStrikeRole::Healer;
    else if (PlayerbotAI::IsTank(bot))
        role = tankRoles.BotIsMainTank() ? CarrierStrikeRole::MainTank : CarrierStrikeRole::OffTank;

    Unit* const shade = FindBossCarrierStrikePick(ai, bot, fight);
    bool holdsBoss = false;
    for (Unit* unit : fight.units)
    {
        if (!unit || unit->GetVictim() != bot)
            continue;
        for (const BossTacticRow* row : fight.rows)
            if (row->bossEntry == unit->GetEntry())
            {
                holdsBoss = true;
                break;
            }
        if (holdsBoss)
            break;
    }

    // The lowest member in the heal rows' own reach (twice the heal range), this bot included. The cover's line leaves
    // out only the Shade's victim (the healer it eats), never this bot.
    Unit* const shadeVictim = shade ? shade->GetVictim() : nullptr;
    uint8 lowestMemberPct = static_cast<uint8>(bot->GetHealthPercent());
    uint8 lowestCoverPct = lowestMemberPct;
    std::vector<CarrierHealer> otherHealers;
    float const reach = 2.0f * (bot->InBattleGround() ? sPlayerbotAIConfig.healDistanceBg : ai->GetRange("heal"));
    Group* const group = bot->GetGroup();
    if (group)
    {
        for (Player* member : LiveGroupMembers(group))
        {
            if (!member || !member->IsInWorld() || member->GetMapId() != bot->GetMapId())
                continue;
            float const distanceYd = sServerFacade.getDistance2d(bot, member);
            if (member->IsAlive() && distanceYd <= reach)
            {
                lowestMemberPct = std::min(lowestMemberPct, static_cast<uint8>(member->GetHealthPercent()));
                if (member == bot || member != shadeVictim)
                    lowestCoverPct = std::min(lowestCoverPct, static_cast<uint8>(member->GetHealthPercent()));
            }
            if (member == bot || !PlayerbotAI::IsHeal(member))
                continue;
            CarrierHealer healer;
            healer.alive = member->IsAlive();
            healer.cursed = member->HasAura(aura);
            healer.charmed = !member->GetCharmerGuid().IsEmpty();
            healer.distanceYd = distanceYd;
            healer.dropYd = bot->GetPositionZ() - member->GetPositionZ();
            otherHealers.push_back(healer);
        }
    }
    uint8 const otherHealerCount = CarrierHealerCount(otherHealers, reach);
    Player* const mainTank = tankRoles.MainTank();
    uint8 const tankPct = mainTank ? static_cast<uint8>(mainTank->GetHealthPercent()) : 0;

    AiObjectContext* context = ai->GetAiObjectContext();
    CarrierStrikeInput in;
    in.role = role;
    in.cursed = bot->HasAura(aura);
    in.shadeInReach = shade != nullptr;
    in.holdsBoss = holdsBoss;
    in.threatSuccessor = HasThreatSuccessorRow(fight.rows);
    in.lowestMemberPct = lowestMemberPct;
    in.tankPct = tankPct;
    in.otherHealers = otherHealerCount;
    in.manaPct = AI_VALUE2(uint8, "mana", "self target");
    in.dispelNowPending = CarrierDispelNowPending(ai, bot, fight.rows);
    // The healer cover (AiPlayerbot.DungeonRunShadeHealerCover), the off-tank's only: the pick already ranks a Shade on
    // a healer within 30 yd of this bot and on its floor first, and keeps the Shade this off-tank pulled off a healer
    // (the latch, read by FindBossCarrierStrikePick before the general pick), so the cover holds when the pick is one.
    if (sPlayerbotAIConfig.dungeonRunShadeHealerCover && role == CarrierStrikeRole::OffTank)
    {
        bool const inCoverReach = shade && bot->GetDistance(shade) <= kShadeCoverReachYd &&
            ShadeCoverSameFloor(bot->GetPositionZ() - shade->GetPositionZ());
        ShadeCoverLatch latch = GetShadeCoverLatch(bot);
        bool const latched = ShadeCoverLatchHolds(latch, shade ? shade->GetObjectGuid().GetRawValue() : 0,
            in.cursed, shade && ShadeOnGroupHealer(bot, shade), shadeVictim == bot, WorldTimer::getMSTime());
        SetShadeCoverLatch(bot, latch);
        // On a threat_successor fight only while leaving cannot change the successor: this off-tank's threat on that
        // boss is above 0 and at least half the highest tank's (kThreatCapOffTankAwayShare, the KeepsAway line).
        bool threatOk = !in.threatSuccessor;
        if (in.threatSuccessor)
            for (Unit* unit : fight.units)
            {
                if (!unit || !sServerFacade.IsAlive(unit))
                    continue;
                bool successorBoss = false;
                for (const BossTacticRow* row : fight.rows)
                    if (row->kind == BossTacticKind::ThreatSuccessor && row->bossEntry == unit->GetEntry())
                        successorBoss = true;
                if (!successorBoss)
                    continue;
                float const own = sServerFacade.GetThreatManager(unit).getThreat(bot);
                float highest = own;
                for (Player* tank : tankRoles.tanks)
                    if (tank)
                        highest = std::max(highest, sServerFacade.GetThreatManager(unit).getThreat(tank));
                threatOk = ShadeCoverThreatAllows(own, highest, kThreatCapOffTankAwayShare);
                break;
            }
        in.shadeCover = latched && inCoverReach && threatOk;
    }
    else if (sPlayerbotAIConfig.dungeonRunShadeHealerCover)
        SetShadeCoverLatch(bot, ShadeCoverLatch()); // only an off-tank latches
    in.lowestCoverPct = lowestCoverPct;

    CarrierStrikeVerdict const verdict =
        CarrierStrikeAllowed(in, static_cast<uint8>(sPlayerbotAIConfig.lowHealth));
    float const shadeYd = shade ? bot->GetDistance(shade) : -1.0f;
    if (verdict != CarrierStrikeVerdict::Strike)
    {
        // The guard failed or the curse ended: a strike cast still in flight must not land (the heal or the cure wins).
        InterruptCarrierStrikeCast(ai, bot, shadeEntry);
        LogCarrierStrike(ai, bot, verdict, role, shade, shadeYd, lowestMemberPct, tankPct, otherHealerCount, in.manaPct,
            "none");
        return false;
    }
    std::string const spell = CarrierStrikeSpellText(ai, bot, verdict, role, shade);
    LogCarrierStrike(ai, bot, verdict, role, shade, shadeYd, lowestMemberPct, tankPct, otherHealerCount, in.manaPct,
        spell.c_str());
    return true;
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

bool BossTacticsSpreadSlotTrigger::IsActive()
{
    if (!Ready(ai, bot))
        return false;
    // The key gate comes first: with it off the slot read writes nothing and the old step-away owns the row, so the
    // read is not even taken. With it on, the read writes the row on the tick path and only the verdicts that need a
    // walk or a hold fire the action.
    if (!sPlayerbotAIConfig.dungeonRunBossSpreadSlots)
        return false;
    const BossSpreadSlotTask task = FindBossSpreadSlot(ai, bot);
    if (!task.slotBot)
        return false;
    return task.verdict == SpreadSlotVerdict::SlotMove || task.verdict == SpreadSlotVerdict::SlotHold ||
           task.verdict == SpreadSlotVerdict::Cast;
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

bool BossTacticsTankPostTrigger::IsActive()
{
    if (!Ready(ai, bot))
        return false;
    // Its own key and the map flag keep every other map and every key-off fight at one lookup.
    if (!sPlayerbotAIConfig.dungeonRunTankPost || !GetBossTactics().HasTankPostMap(bot->GetMapId()) ||
        !PlayerbotAI::IsTank(bot))
        return false;
    const BossTankPost post = FindBossTankPost(ai, bot, ScanBossTactics(ai, bot));
    return post.valid && !post.holdingOtherHostile;
}

bool BossTacticsTankPostHealerTrigger::IsActive()
{
    if (!Ready(ai, bot))
        return false;
    // Its own key and the map flag keep every other map and every key-off fight at one lookup.
    if (!sPlayerbotAIConfig.dungeonRunTankPostHealers || !GetBossTactics().HasTankPostMap(bot->GetMapId()))
        return false;
    const TankPostHealDuty duty = FindTankPostHealDuty(ai, bot);
    if (!duty.holder || bot->IsNonMeleeSpellCasted(false))
        return false;
    const float dx = bot->GetPositionX() - duty.holder->GetPositionX();
    const float dy = bot->GetPositionY() - duty.holder->GetPositionY();
    return TankPostHealerFar(std::sqrt(dx * dx + dy * dy));
}

bool BossTacticsTankPostCooldownTrigger::IsActive()
{
    if (!Ready(ai, bot))
        return false;
    // Its own key, the map flag and the tank test keep every other bot and every key-off fight at one lookup.
    if (!sPlayerbotAIConfig.dungeonRunTankPostHealers || !GetBossTactics().HasTankPostMap(bot->GetMapId()) ||
        !PlayerbotAI::IsTank(bot))
        return false;
    return TankPostCooldownDue(bot);
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
