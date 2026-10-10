#include "playerbot/playerbot.h"
#include "GluthDungeonActions.h"
#include "playerbot/GluthKitePolicy.h"
#include "AttackAction.h"
#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"

using namespace ai;

namespace
{
    Unit* FindGluthBoss(PlayerbotAI* ai)
    {
        const std::list<ObjectGuid> attackers =
            ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("attackers")->Get();
        for (const ObjectGuid& guid : attackers)
        {
            Unit* unit = ai->GetUnit(guid);
            if (unit && unit->IsAlive() && unit->GetEntry() == 15932)
                return unit;
        }
        const std::list<ObjectGuid> targets =
            ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("possible attack targets")->Get();
        for (const ObjectGuid& guid : targets)
        {
            Unit* unit = ai->GetUnit(guid);
            if (unit && unit->IsAlive() && unit->GetEntry() == 15932)
                return unit;
        }
        return nullptr;
    }
}

bool GluthChooseTargetAction::Execute(Event& event)
{
    // Explicit master orders win over the fight choreography.
    ObjectGuid explicitGuid = AI_VALUE(ObjectGuid, "explicit attack target");
    if (!explicitGuid.IsEmpty())
        return false;

    Unit* boss = FindGluthBoss(ai);

    // Tanks hold Gluth (donor pins MainTank/Assist0 to the boss): bank
    // second-place threat and stay in taunt range, never chase chow.
    if (PlayerbotAI::IsTank(bot))
    {
        if (!boss || AI_VALUE(Unit*, "current target") == boss)
            return false;
        return Attack(bot, boss);
    }

    // One 30yd sweep for execute-range chow. Chow must come from the
    // world, not the cached target lists: post-Decimate chow MoveFollow
    // Gluth without aggroing DPS, so they never appear there.
    Unit* execute = nullptr;
    std::list<Unit*> nearby;
    MaNGOS::AllCreaturesOfEntryInRange check(bot, 16360, 30.0f);
    MaNGOS::UnitListSearcher<MaNGOS::AllCreaturesOfEntryInRange> searcher(nearby, check);
    Cell::VisitAllObjects(bot, searcher, 30.0f);
    for (Unit* unit : nearby)
    {
        if (!unit || !unit->IsAlive())
            continue;
        const float pct = 100.0f * unit->GetHealth() / unit->GetMaxHealth();
        if (!IsGluthChowExecute(pct))
            continue;
        // Donor picks the chow closest to the main tank (boss position —
        // the tank holds Gluth): kill what reaches him first.
        const float anchorDist = boss ? unit->GetDistance(boss) : bot->GetDistance(unit);
        const float bestDist = !execute ? -1.0f : (boss ? execute->GetDistance(boss) : bot->GetDistance(execute));
        if (!execute || anchorDist < bestDist)
            execute = unit;
    }

    // Runs continuously while chow are up, so the target swaps back to
    // the boss naturally when nothing qualifies (donor wiring).
    Unit* want = execute ? execute : boss;
    if (!want)
        return false;
    if (AI_VALUE(Unit*, "current target") == want)
        return false;
    return Attack(bot, want);
}

bool GluthTauntSwapAction::Execute(Event& event)
{
    if (!PlayerbotAI::IsTank(bot))
        return false;

    Unit* gluth = FindGluthBoss(ai);
    if (!gluth || !gluth->IsAlive())
        return false;

    // Class-correct taunt: these actions are always registered in the
    // class AiObjectContext (unlike the legacy "taunt spell" node alias).
    // No paladin branch: WotLK-era taunts (Hand of Reckoning /
    // Righteous Defense) do not exist in 1.12, so paladin tanks sit
    // the swap out.
    const char* taunt = nullptr;
    switch (bot->GetClass())
    {
        case CLASS_WARRIOR: taunt = "taunt"; break;
        case CLASS_DRUID: taunt = "growl"; break;
        default: return false;
    }
    // Move "current target" onto Gluth first: the taunt spell only sets
    // selection, so without this the rotation keeps hitting chow and the
    // boss snaps back to the wounded tank. Attack() sticks even when the
    // taunt itself cannot fire yet (cooldown/range).
    if (AI_VALUE(Unit*, "current target") != gluth)
    {
        if (Attack(bot, gluth) && !ai->CanCastSpell(taunt, gluth))
            return true;
    }
    if (!ai->CanCastSpell(taunt, gluth))
        return false;
    return ai->CastSpell(taunt, gluth);
