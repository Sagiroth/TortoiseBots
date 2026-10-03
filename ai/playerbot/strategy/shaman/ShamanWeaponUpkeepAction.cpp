
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
