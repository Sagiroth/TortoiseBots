#include "playerbot/playerbot.h"
#include "KelthuzadDungeonActions.h"
#include "playerbot/KelthuzadAddsPolicy.h"
#include "playerbot/GroupMembers.h"
#include <cmath>
#include "AttackAction.h"
#include "ChooseTargetActions.h"
#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"

using namespace ai;

namespace
{
    // Center verified vs core pullPortal (3716.38/-5106.78); tank anchor
    // + guardian-tank anchor from the donor (same map geometry).
    const float kKtCenterX = 3716.38f;
    const float kKtCenterY = -5106.78f;
    const float kKtTankX = 3709.19f;
    const float kKtTankY = -5104.86f;
    const float kKtAssistTankX = 3746.05f;
    const float kKtAssistTankY = -5112.74f;

    // Adds only count inside 30yd of the center and within the bot's
    // spell range (donor exclusion: alcove campers stay out).
    bool KtCandidateUsable(PlayerbotAI* ai, Player* bot, Unit* unit)
    {
        if (!unit || !unit->IsAlive())
            return false;
        if (unit->GetDistance2d(kKtCenterX, kKtCenterY) > 30.0f)
            return false;
        if (bot->GetDistance(unit) > ai->GetRange("spell"))
            return false;
        return true;
    }

    struct KtTargets
    {
        Unit* soldier = nullptr;
        Unit* weaver = nullptr;
        Unit* abom = nullptr;
        Unit* guardian = nullptr;
        Unit* kt = nullptr;
    };

    void KtNote(KtTargets& into, Unit* unit)
    {
        switch (unit->GetEntry())
        {
            case kKtSoldier: if (!into.soldier) into.soldier = unit; break;
            case kKtWeaver: if (!into.weaver) into.weaver = unit; break;
            case kKtAbom: if (!into.abom) into.abom = unit; break;
            case kKtGuardian: if (!into.guardian) into.guardian = unit; break;
            case kKtBoss: if (!into.kt) into.kt = unit; break;
            default: break;
        }
    }

    // Single pass: cached lists first, one grid sweep only for entries
    // still missing. Dead units never mask live ones (IsAlive at every
    // layer, including the cached loops).
    KtTargets FindKtTargets(PlayerbotAI* ai, Player* bot)
    {
        KtTargets found;
        const std::list<ObjectGuid> attackers =
            ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("attackers")->Get();
        for (const ObjectGuid& guid : attackers)
        {
            Unit* unit = ai->GetUnit(guid);
            if (KtCandidateUsable(ai, bot, unit))
                KtNote(found, unit);
        }
        if (!found.soldier || !found.weaver || !found.abom || !found.guardian || !found.kt)
        {
            const std::list<ObjectGuid> targets =
                ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("possible attack targets")->Get();
            for (const ObjectGuid& guid : targets)
            {
                Unit* unit = ai->GetUnit(guid);
                if (KtCandidateUsable(ai, bot, unit))
                    KtNote(found, unit);
            }
        }
        if (!found.soldier || !found.weaver || !found.abom || !found.guardian || !found.kt)
        {
            std::list<Unit*> nearby;
            MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck check(bot, bot, 100.0f);
            MaNGOS::UnitListSearcher<MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck> searcher(nearby, check);
            Cell::VisitAllObjects(bot, searcher, 100.0f);
            for (Unit* unit : nearby)
            {
                if (KtCandidateUsable(ai, bot, unit))
                    KtNote(found, unit);
            }
        }
        return found;
    }
}

bool KelthuzadChooseTargetAction::Execute(Event& event)
{
    const bool ranged = ai->IsRanged(bot);
    const bool tank = PlayerbotAI::IsTank(bot);

    KtTargets found = FindKtTargets(ai, bot);

    const unsigned want = KtPickTarget(ranged, tank,
        found.soldier != nullptr, found.weaver != nullptr,
        found.abom != nullptr, found.guardian != nullptr,
        found.kt && !found.kt->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE));
    if (!want)
        return false;

    Unit* target = (want == kKtSoldier) ? found.soldier : (want == kKtWeaver) ? found.weaver :
        (want == kKtAbom) ? found.abom : (want == kKtGuardian) ? found.guardian : found.kt;
    if (!target)
        return false;

    if (AI_VALUE(Unit*, "current target") == target)
        return false;
    return Attack(bot, target);
}

bool KelthuzadPositionAction::Execute(Event& event)
{
    KtTargets found = FindKtTargets(ai, bot);
    const bool phaseTwo = found.kt && !found.kt->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);

    if (!phaseTwo)
    {
        // Phase 1: gather center only when idled (no current target), so
        // bots are not yanked off adds mid-DPS (donor mirror).
        if (AI_VALUE(Unit*, "current target") != nullptr)
            return false;
        if (bot->GetDistance2d(kKtCenterX, kKtCenterY) < 3.0f)
            return false;
        return MoveTo(bot->GetMapId(), kKtCenterX, kKtCenterY, bot->GetPositionZ());
    }

    if (PlayerbotAI::IsTank(bot))
    {
        // Guardian target first: a guardian OT holding aggro must take the
        // assist anchor, not the MT anchor (donor: only non-main tanks take
        // assist_tank_pos; the main tank never targets guardians).
        Unit* current = AI_VALUE(Unit*, "current target");
        if (current && current->GetEntry() == kKtGuardian)
        {
            if (bot->GetDistance2d(kKtAssistTankX, kKtAssistTankY) < 3.0f)
                return false;
            return MoveTo(bot->GetMapId(), kKtAssistTankX, kKtAssistTankY, bot->GetPositionZ());
        }
        if (AI_VALUE2(bool, "has aggro", "current target"))
        {
            if (bot->GetDistance2d(kKtTankX, kKtTankY) < 3.0f)
                return false;
            return MoveTo(bot->GetMapId(), kKtTankX, kKtTankY, bot->GetPositionZ());
        }
        return false;
    }

    if (ai->IsRanged(bot))
    {
        // Rings around the center by ranged-only index: inner 20yd for
        // the first 8, outer 32yd beyond (donor geometry).
        Group* group = bot->GetGroup();
        uint32 slot = 0;
        if (group)
        {
            for (Player* member : LiveGroupMembers(group))
            {
                if (member == bot)
                    break;
                if (ai->IsRanged(member))
                    ++slot;
            }
        }
        const float radius = (slot < 8) ? 20.0f : 32.0f;
        const float angle = float(slot % 8) * float(M_PI) / 4.0f;
        const float x = kKtCenterX + std::cos(angle) * radius;
        const float y = kKtCenterY + std::sin(angle) * radius;
        if (bot->GetDistance2d(x, y) < 3.0f)
            return false;
        return MoveTo(bot->GetMapId(), x, y, bot->GetPositionZ());
    }

    return false;
}

float KelthuzadSuppressMultiplier::GetValue(Action* action)
{
    if (!action)
        return 1.0f;
    // Explicit master orders win over the fight choreography.
    if (ai->HasActivePlayerMaster())
        return 1.0f;
    if (dynamic_cast<DpsAssistAction*>(action) || dynamic_cast<TankAssistAction*>(action) ||
        dynamic_cast<FleeAction*>(action))
        return 0.0f;
    return 1.0f;
}
