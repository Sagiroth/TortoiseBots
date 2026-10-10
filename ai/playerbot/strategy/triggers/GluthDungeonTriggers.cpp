#include "playerbot/playerbot.h"
#include "GluthDungeonTriggers.h"
#include "playerbot/GluthKitePolicy.h"

using namespace ai;

bool GluthMortalWoundSwapTrigger::IsActive()
{
    // Explicit master orders win over the fight choreography (same
    // guard as the chooser: a player-ordered tank holds its orders).
    if (!AI_VALUE(ObjectGuid, "explicit attack target").IsEmpty())
        return false;

    // Find Gluth through the encounter, not the bot's current target:
    // the off-tank may be targeting chow (or nothing) when the swap is
    // needed, and must still answer it.
    Unit* gluth = nullptr;
    const std::list<ObjectGuid> attackers =
        ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("attackers")->Get();
    for (const ObjectGuid& guid : attackers)
    {
        Unit* unit = ai->GetUnit(guid);
        if (unit && unit->IsAlive() && unit->GetEntry() == 15932)
        {
            gluth = unit;
            break;
        }
    }
    if (!gluth)
        return false;

    Unit* victim = gluth->GetVictim();
    if (!victim || victim == bot)
        return false;
    // Any player victim counts — including a human main tank, who never
    // registers as a bot-tank but still stacks wounds that need swapping.
    Player* victimPlayer = dynamic_cast<Player*>(victim);
    if (!victimPlayer)
        return false;

    Aura* wound = ai->GetAura(25646, victim);
    const uint32 stacks = wound ? wound->GetStackAmount() : 0;
    return ShouldGluthTauntSwap(true, true, true, stacks);
}

bool GluthTrigger::IsActive()
{
    if (PlayerbotAI::IsTank(bot))
        return false;
    const std::list<ObjectGuid> attackers =
        ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("attackers")->Get();
    for (const ObjectGuid& guid : attackers)
    {
        Unit* unit = ai->GetUnit(guid);
        if (unit && unit->IsAlive() && unit->GetEntry() == 15932)
            return true;
    }
    const std::list<ObjectGuid> targets =
        ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("possible attack targets")->Get();
    for (const ObjectGuid& guid : targets)
    {
        Unit* unit = ai->GetUnit(guid);
        if (unit && unit->IsAlive() && unit->GetEntry() == 15932)
            return true;
    }
    return false;
}

bool GluthChowUpTrigger::IsActive()
{
    if (PlayerbotAI::IsTank(bot))
        return false;

    // Cheap cached check only: any chow on the shared target lists.
    // (Kept for status/introspection; the strategy drives triage off the
    // continuous "gluth" trigger so the target also swaps back to boss.)
    const std::list<ObjectGuid> targets =
        ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("possible attack targets")->Get();
    for (const ObjectGuid& guid : targets)
    {
        Unit* unit = ai->GetUnit(guid);
        if (unit && unit->IsAlive() && unit->GetEntry() == 16360)
            return true;
    }
    return false;
}
