
#include "playerbot/playerbot.h"
#include "playerbot/GroupMembers.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "MoltenCoreDungeonActions.h"

using namespace ai;
#include "playerbot/GolemaggPolicy.h"

bool GolemaggHealerPositionAction::Execute(Event& event)
{
    (void)event;
    // Already at the midpoint: hold.
    if (bot->GetDistance2d(kHealerX, kHealerY) <= kGolemaggHealerTolerance)
        return false;
    return MoveTo(bot->GetMapId(), kHealerX, kHealerY, kHealerZ, false, IsReaction(), false, true);
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
    // Main tank (first living tank by slot order — ld-8 value pending, see
    // policy header) holds Golemagg at his camp; assists hold a rager at
    // its camp. Non-tanks never see this action (strategy-gated).
    bool botIsMain = false;
    if (Group* group = bot->GetGroup())
    {
        for (Player* member : LiveGroupMembers(group))
        {
            if (!member || !sServerFacade.IsAlive(member))
                continue;
            if (!ai->IsTank(member))
                continue;
            botIsMain = (member == bot);
            break;
        }
    }
    else
    {
        botIsMain = true;
    }
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
    return MoveTo(bot->GetMapId(), tx, ty, tz, false, IsReaction(), false, true);
}
