
#include "playerbot/playerbot.h"
#include "ShamanActions.h"
#include "ShamanImbuePolicy.h"
#include "playerbot/AiFactory.h"

using namespace ai;

bool CastShamanWeaponUpkeepAction::Execute(Event& event)
{
    // Enhancement ranks windfury first; every other spec starts at
    // flametongue (ShamanImbuePolicy.h). GetPlayerSpecTab resolves the
    // bot's talent tab (elemental 0 / enhancement 1 / restoration 2, low
    // levels default to enhancement); only enhancement takes windfury head.
    bool const enhancement = AiFactory::GetPlayerSpecTab(bot) == SHAMAN_TAB_ENHANCEMENT;
    std::string const best = BestKnownShamanImbue(enhancement,
        ai->HasSpell("windfury weapon"), ai->HasSpell("flametongue weapon"),
        ai->HasSpell("frostbrand weapon"), ai->HasSpell("rockbiter weapon"));

    if (best.empty())
        return false;

    // Already imbued: upkeep satisfied. Report success so the engine does
    // not log a failure for a no-op (mirrors the imbue gate in
    // SpellCastUsefulValue::Calculate). Without this the always-true
    // trigger would convert the inner cast's USELESS into an outer FAILED
    // every tick the imbue is up.
    AiObjectContext* context = ai->GetAiObjectContext();
    uint32 const spellId = AI_VALUE2(uint32, "spell id", best);
    Item* const weapon = spellId ? AI_VALUE2(Item*, "item for spell", spellId) : nullptr;
    if (weapon && weapon->GetEnchantmentId(TEMP_ENCHANTMENT_SLOT))
        return true;

    return ai->DoSpecificAction(best, event, true);
}

bool CastShamanWeaponUpkeepAction::isUseful()
{
    bool const knownWindfury = ai->HasSpell("windfury weapon");
    bool const knownFlametongue = ai->HasSpell("flametongue weapon");
    bool const knownFrostbrand = ai->HasSpell("frostbrand weapon");
    bool const knownRockbiter = ai->HasSpell("rockbiter weapon");
    bool const imbueKnown = knownWindfury || knownFlametongue || knownFrostbrand || knownRockbiter;

    // Every one of these is a guaranteed FAILED cast below (PlayerbotAI::CastSpell
    // logs the gate and returns false with no retry delay on the upkeep path),
    // so queuing through them failed at tick speed while the imbue waited.
    bool const castBlocked = !bot->IsAlive() ||
        bot->GetStandState() != UNIT_STAND_STATE_STAND ||
        bot->IsNonMeleeSpellCasted(false, true, true) ||
        bot->IsFlying() || bot->IsTaxiFlying() ||
        bot->HasUnitState(UNIT_STAT_CAN_NOT_REACT_OR_LOST_CONTROL);

    // Any temp enchant means upkeep satisfied: only one imbue fits at a time,
    // so whichever rank left it, there is nothing to cast. Probe through any
    // known imbue (same mainhand either way); when nothing resolves, let
    // Execute decide.
    bool alreadyImbued = false;
    if (imbueKnown)
    {
        std::string const probe = knownRockbiter ? "rockbiter weapon"
            : knownFlametongue ? "flametongue weapon"
            : knownFrostbrand ? "frostbrand weapon" : "windfury weapon";
        AiObjectContext* context = ai->GetAiObjectContext();
        uint32 const spellId = AI_VALUE2(uint32, "spell id", probe);
        Item* const weapon = spellId ? AI_VALUE2(Item*, "item for spell", spellId) : nullptr;
        alreadyImbued = weapon && weapon->GetEnchantmentId(TEMP_ENCHANTMENT_SLOT) != 0;
    }

    return ShamanUpkeepShouldAttempt(imbueKnown, alreadyImbued, castBlocked);
}
