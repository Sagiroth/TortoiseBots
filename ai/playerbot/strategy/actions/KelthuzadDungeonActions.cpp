#include "playerbot/playerbot.h"
#include "KelthuzadDungeonActions.h"
#include "playerbot/KelthuzadAddsPolicy.h"
#include "playerbot/GroupMembers.h"
#include <cmath>
#include "AttackAction.h"
#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"

using namespace ai;

namespace
{
    // Center verified vs core pullPortal (3716.38/-5106.78); tank anchor
    // from the donor (same map geometry).
    const float kKtCenterX = 3716.38f;
    const float kKtCenterY = -5106.78f;
    const float kKtTankX = 3709.19f;
    const float kKtTankY = -5104.86f;

    Unit* FindEntry(PlayerbotAI* ai, Player* bot, uint32 entry)
    {
        const std::list<ObjectGuid> attackers =
            ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("attackers")->Get();
        for (const ObjectGuid& guid : attackers)
        {
            Unit* unit = ai->GetUnit(guid);
            if (unit && unit->GetEntry() == entry)
                return unit;
        }
        const std::list<ObjectGuid> targets =
            ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("possible attack targets")->Get();
        for (const ObjectGuid& guid : targets)
        {
            Unit* unit = ai->GetUnit(guid);
            if (unit && unit->GetEntry() == entry)
                return unit;
        }
        std::list<Unit*> nearby;
        MaNGOS::AllCreaturesOfEntryInRange check(bot, entry, 100.0f);
        MaNGOS::UnitListSearcher<MaNGOS::AllCreaturesOfEntryInRange> searcher(nearby, check);
        Cell::VisitAllObjects(bot, searcher, 100.0f);
        for (Unit* unit : nearby)
        {
            if (unit && unit->IsAlive())
                return unit;
        }
        return nullptr;
    }
}

bool KelthuzadChooseTargetAction::Execute(Event& event)
{
    const bool ranged = ai->IsRanged(bot);
    const bool tank = PlayerbotAI::IsTank(bot);

    Unit* soldier = FindEntry(ai, bot, kKtSoldier);
    Unit* weaver = FindEntry(ai, bot, kKtWeaver);
    Unit* abom = FindEntry(ai, bot, kKtAbom);
    Unit* guardian = FindEntry(ai, bot, kKtGuardian);
    Unit* kt = FindEntry(ai, bot, kKtBoss);

    const unsigned want = KtPickTarget(ranged, tank,
        soldier && soldier->IsAlive(), weaver && weaver->IsAlive(),
        abom && abom->IsAlive(), guardian && guardian->IsAlive(),
        kt && kt->IsAlive() && !kt->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE));
    if (!want)
        return false;

    Unit* target = (want == kKtSoldier) ? soldier : (want == kKtWeaver) ? weaver :
        (want == kKtAbom) ? abom : (want == kKtGuardian) ? guardian : kt;
    if (!target)
        return false;

    if (AI_VALUE(Unit*, "current target") == target)
        return false;
    return Attack(bot, target);
}

bool KelthuzadPositionAction::Execute(Event& event)
{
    Unit* kt = FindEntry(ai, bot, kKtBoss);
    const bool phaseTwo = kt && !kt->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);

    if (!phaseTwo)
    {
        // Phase 1: gather center so adds meet the raid together.
        if (bot->GetDistance2d(kKtCenterX, kKtCenterY) < 3.0f)
            return false;
        return MoveTo(bot->GetMapId(), kKtCenterX, kKtCenterY, bot->GetPositionZ());
    }

    if (PlayerbotAI::IsTank(bot))
    {
        if (bot->GetDistance2d(kKtTankX, kKtTankY) < 3.0f)
            return false;
        return MoveTo(bot->GetMapId(), kKtTankX, kKtTankY, bot->GetPositionZ());
    }

    if (ai->IsRanged(bot))
    {
        // 20yd ring around the center, spread by group slot.
        Group* group = bot->GetGroup();
        uint32 slot = 0;
        if (group)
        {
            for (Player* member : LiveGroupMembers(group))
            {
                if (member == bot)
                    break;
                ++slot;
            }
        }
        const float angle = float(slot % 8) * float(M_PI) / 4.0f;
        const float x = kKtCenterX + std::cos(angle) * kKtRingRadius;
        const float y = kKtCenterY + std::sin(angle) * kKtRingRadius;
        if (bot->GetDistance2d(x, y) < 3.0f)
            return false;
        return MoveTo(bot->GetMapId(), x, y, bot->GetPositionZ());
    }

    return false;
}
