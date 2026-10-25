
#include "playerbot/playerbot.h"
#include "playerbot/GroupMembers.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/values/PartyMemberToHeal.h"
#include "MoltenCoreDungeonActions.h"

using namespace ai;
#include "playerbot/policy/raid/GolemaggPolicy.h"

bool GolemaggHealerPositionAction::Execute(Event& event)
{
    (void)event;
    // Already at the midpoint: hold.
    if (bot->GetDistance2d(kHealerX, kHealerY) <= kGolemaggHealerTolerance)
        return false;
    return MoveTo(bot->GetMapId(), kHealerX, kHealerY, kHealerZ, false, false, false, true);
}

bool GolemaggTankHoldAction::Execute(Event& event)
{
    (void)event;
    AiObjectContext* context = ai->GetAiObjectContext();
    const std::list<ObjectGuid> attackers = AI_VALUE(std::list<ObjectGuid>, "attackers");
    Unit* boss = nullptr;
    Unit* rager = nullptr;
    for (const ObjectGuid& attackerGuid : attackers)
    {
        Unit* attacker = ai->GetUnit(attackerGuid);
        if (!attacker)
            continue;
        if (attacker->GetEntry() == kGolemaggEntry)
            boss = attacker;
        else if (attacker->GetEntry() == kCoreRagerEntry && !rager)
            rager = attacker;
    }
    if (!boss)
        return false;
    // Main tank (raid Main Tank flag, else tank Main Assist, else first
    // living same-map tank by slot: GetRaidTankRoles) holds Golemagg at his
    // camp; assists hold a rager at its camp. Non-tanks never see this
    // action (strategy-gated); solo the bot is the main tank.
    bool botIsMain = !bot->GetGroup() || GetRaidTankRoles(ai).BotIsMainTank();
    float tx, ty, tz;
    if (botIsMain)
    {
        // Trust-buffed ragers mean the split is still on: drag the boss to
        // his camp so Trust (>30y) drops off the ragers.
        tx = kGolemaggTankX; ty = kGolemaggTankY; tz = kGolemaggTankZ;
        Unit* victim = boss->GetVictim();
        if (victim && victim->getObjectGuid() == bot->getObjectGuid() &&
            bot->GetDistance2d(tx, ty) <= boss->GetCombatReach() + 2.0f)
            return false;
    }
    else
    {
        if (!rager)
            return false;
        tx = kRagerTankX; ty = kRagerTankY; tz = kRagerTankZ;
        if (bot->GetDistance2d(tx, ty) <= 5.0f)
            return false;
    }
    return MoveTo(bot->GetMapId(), tx, ty, tz, false, false, false, true);
}

#include "playerbot/playerbot.h"
#include "MoltenCoreDungeonActions.h"
#include "playerbot/policy/raid/GeddonInfernoPolicy.h"
#include "playerbot/RaidDpsExclusionPolicy.h"
#include "playerbot/strategy/values/RaidDpsTargetPolicy.h"
#include "playerbot/strategy/values/RtiTargetValue.h"
#include "playerbot/strategy/values/PositionValue.h"

using namespace ai;

Unit* ai::GetActiveGeddonInferno(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    AiObjectContext* context = ai->GetAiObjectContext();
    PositionMap& positions = AI_VALUE(PositionMap&, "position");
    auto hold = positions.find("pull hold");
    bool const ordered = ai->HasStrategy("stay", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("follow", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("wait for attack", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("grind", BotState::BOT_STATE_COMBAT) ||
        (hold != positions.end() && hold->second.isSet());
    if (!GeddonInfernoEnabled(ai->HasStrategy("geddon inferno", BotState::BOT_STATE_COMBAT),
        bot->IsInWorld() && !bot->IsBeingTeleported() && bot->IsAlive(),
        bot->GetMapId() == 409, bot->IsInCombat(), ordered))
        return nullptr;

    for (ObjectGuid const& guid : AI_VALUE(std::list<ObjectGuid>, "attackers"))
    {
        Unit* boss = ai->GetUnit(guid);
        if (boss && boss->GetEntry() == 12056 && boss->IsInMap(bot) &&
            GeddonInfernoActive(boss->IsAlive(), boss->IsInCombat(),
                boss->HasAura(19695), boss->HasUnitState(UNIT_STAT_ROOT)))
            return boss;
    }
    return nullptr;
}

bool GeddonInfernoRetreatAction::isUseful()
{
    Unit* boss = GetActiveGeddonInferno(ai);
    return MovementAction::isUseful() && boss &&
        GeddonInfernoNeedsRetreat(bot->GetCombatDistance(boss));
}

bool GeddonInfernoRetreatAction::Execute(Event& event)
{
    Unit* boss = GetActiveGeddonInferno(ai);
    if (!boss || !GeddonInfernoNeedsRetreat(bot->GetCombatDistance(boss)))
        return false;

    WorldPosition const start(bot);
    WorldPosition const center(boss);
    float const radius = kGeddonInfernoClearance + 1.0f + bot->GetCombatReach() + boss->GetCombatReach();
    float const away = center.GetAngleTo(start);
    // Prefer directly outward. Destinations are on one fixed boss-centered
    // ring, not an additional step from the bot on every reaction tick.
    for (float offset : { 0.0f, 0.35f, -0.35f, 0.7f, -0.7f, 1.05f, -1.05f })
    {
        float const angle = away + offset;
        WorldPosition point = center + WorldPosition(0, radius * cos(angle), radius * sin(angle), 0.0f);
        point.setZ(point.GetHeight());
        if (fabs(point.getZ() - start.getZ()) > 5.0f || IsHazardNearPosition(point) ||
            !bot->IsWithinLOS(point.getX(), point.getY(), point.getZ() + bot->GetCollisionHeight()) ||
            !start.canPathTo(point, bot))
            continue;

        bool nearUnpulled = false;
        for (ObjectGuid const& guid : AI_VALUE(std::list<ObjectGuid>, "possible targets"))
        {
            Unit* unit = ai->GetUnit(guid);
            if (unit && unit->IsAlive() && !unit->IsInCombat() &&
                sServerFacade.getDistance2d(unit, point.getX(), point.getY()) < sPlayerbotAIConfig.aggroDistance)
            {
                nearUnpulled = true;
                break;
            }
        }
        if (!nearUnpulled && MoveTo(bot->GetMapId(), point.getX(), point.getY(), point.getZ(),
            false, IsReaction(), false, true))
        {
            if (IsReaction())
                WaitForReach(start.distance(point));
            return true;
        }
    }
    return false;
}


bool ai::IsRaidDpsTargetExcluded(PlayerbotAI* ai, Unit* target)
{
    Player* bot = ai->GetBot();
    if (!target || !target->IsAlive() || !target->IsInMap(bot) || !bot->IsAlive() || !bot->IsInCombat())
        return false;
    RaidDpsEncounter const encounter = RaidDpsEncounterForTarget(target->GetEntry(),
        ai->HasStrategy("golemagg dps", BotState::BOT_STATE_COMBAT),
        ai->HasStrategy("majordomo dps", BotState::BOT_STATE_COMBAT),
        ai->HasStrategy("garr dps", BotState::BOT_STATE_COMBAT));
    bool const protectedEntry = encounter != RaidDpsEncounter::None;
    bool const enabled = protectedEntry;
    bool const inEncounterMap = bot->GetMapId() == 409;
    bool const tank = ai->IsTank(bot);
    if (!protectedEntry || !enabled || !inEncounterMap || tank)
        return false;
    AiObjectContext* context = ai->GetAiObjectContext();
    bool ordered = AI_VALUE(ObjectGuid, "explicit attack target") == target->GetObjectGuid();
    if (Group* group = bot->GetGroup())
    {
        int const mark = RtiTargetValue::GetRtiIndex(AI_VALUE(std::string, "rti"));
        if (mark >= 0 && ObjectGuid(group->GetTargetIcon(mark)) == target->GetObjectGuid())
            ordered = true;
    }
    // Majordomo yields when his adds die. Do not autonomously burn him down,
    // but preserve the assigned tank and explicit damage orders.
    bool encounterActive = encounter == RaidDpsEncounter::Majordomo &&
        target->IsInCombat() && !sServerFacade.IsFriendlyTo(bot, target);
    if (encounter == RaidDpsEncounter::Golemagg || encounter == RaidDpsEncounter::Garr)
    {
        uint32 const bossEntry = encounter == RaidDpsEncounter::Golemagg ? 11988 : 12057;
        for (ObjectGuid const& guid : AI_VALUE(std::list<ObjectGuid>, "attackers"))
        {
            Unit* boss = ai->GetUnit(guid);
            if (boss && boss->GetEntry() == bossEntry && boss->IsInMap(bot) && boss->IsAlive() && boss->IsInCombat())
            {
                encounterActive = true;
                break;
            }
        }
    }
    return RaidDpsTargetExcluded(enabled, inEncounterMap, tank, ordered, protectedEntry, encounterActive);
}

bool RaidDpsRetargetAction::isUseful()
{
    Pet* pet = bot->GetPet();
    return IsRaidDpsTargetExcluded(ai, AI_VALUE(Unit*, "current target")) ||
        (pet && IsRaidDpsTargetExcluded(ai, pet->GetVictim()));
}

bool RaidDpsRetargetAction::Execute(Event& event)
{
    bool changed = false;
    Pet* pet = bot->GetPet();
    if (pet && IsRaidDpsTargetExcluded(ai, pet->GetVictim()))
    {
        // Use the existing command path to cancel the old pet attack/movement.
        WorldPacket data(CMSG_PET_ACTION);
        data << pet->GetObjectGuid() << uint32((ACT_COMMAND << 24) | COMMAND_FOLLOW) << ObjectGuid();
        bot->GetSession()->HandlePetAction(data);
        changed = true;
    }
    if (IsRaidDpsTargetExcluded(ai, AI_VALUE(Unit*, "current target")))
    {
        Unit* excluded = AI_VALUE(Unit*, "current target");
        // Cancel only casts aimed at the discarded enemy; do not cancel a heal.
        for (int type = CURRENT_MELEE_SPELL; type < CURRENT_MAX_SPELL; ++type)
            if (Spell* spell = bot->GetCurrentSpell(CurrentSpellTypes(type)))
                if (spell->m_targets.getUnitTargetGuid() == excluded->GetObjectGuid())
                    bot->InterruptSpell(CurrentSpellTypes(type));
        bot->AttackStop();
        SET_AI_VALUE(Unit*, "current target", nullptr);
        context->GetValue<Unit*>("dps target")->Reset();
        ai->DoSpecificAction("dps assist", event, true);
        changed = true;
    }
    if (changed && pet && !IsRaidDpsTargetExcluded(ai, AI_VALUE(Unit*, "current target")))
        ai->DoSpecificAction("pet attack", event, true);
    return changed;
}


Unit* ai::GetActiveGarr(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    if (!bot->IsAlive() || !bot->IsInCombat() || bot->GetMapId() != 409 || ai->IsTank(bot) ||
        !ai->HasStrategy("garr dps", BotState::BOT_STATE_COMBAT))
        return nullptr;
    AiObjectContext* context = ai->GetAiObjectContext();
    for (ObjectGuid const& guid : AI_VALUE(std::list<ObjectGuid>, "attackers"))
    {
        Unit* boss = ai->GetUnit(guid);
        if (boss && boss->GetEntry() == 12057 && boss->IsInMap(bot) && boss->IsAlive() && boss->IsInCombat())
            return boss;
    }
    return nullptr;
}
