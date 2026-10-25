#include "playerbot/playerbot.h"
#include "ThaddiusDungeonTriggers.h"
#include "playerbot/policy/raid/ThaddiusPolarityPolicy.h"
#include "../../policy/raid/ThaddiusDungeonHelper.h"

using namespace ai;

namespace
{
    bool PetActive(Unit* pet)
    {
        return pet && IsThaddiusPetActive(pet->IsAlive(),
            pet->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE));
    }
}

bool ThaddiusStartFightTrigger::IsActive()
{
    if (ai->HasStrategy("thaddius", BotState::BOT_STATE_COMBAT))
        return false;
    if (!bot->IsInWorld() || bot->IsBeingTeleported())
        return false;
    Unit* stalagg;
    Unit* feugen;
    Unit* thaddius;
    FindThaddiusAdds(ai, bot, stalagg, feugen, thaddius);
    return stalagg != nullptr || feugen != nullptr;
}

bool ThaddiusPhasePetTrigger::IsActive()
{
    Unit* stalagg;
    Unit* feugen;
    Unit* thaddius;
    FindThaddiusAdds(ai, bot, stalagg, feugen, thaddius);
    return IsThaddiusPhasePet(PetActive(feugen), PetActive(stalagg));
}

bool ThaddiusPhaseTransitionTrigger::IsActive()
{
    Unit* stalagg;
    Unit* feugen;
    Unit* thaddius;
    FindThaddiusAdds(ai, bot, stalagg, feugen, thaddius);
    if (IsThaddiusPhasePet(PetActive(feugen), PetActive(stalagg)))
        return false;
    return thaddius && IsThaddiusPhaseTransition(false,
        thaddius->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE));
}

bool ThaddiusPhaseThaddiusTrigger::IsActive()
{
    // Keep the opt-in geometry, opposite-charge path checks and manual holds.
    if (ai->HasStrategy("thaddius polarity", BotState::BOT_STATE_COMBAT))
        return false;
    Unit* stalagg;
    Unit* feugen;
    Unit* thaddius;
    FindThaddiusAdds(ai, bot, stalagg, feugen, thaddius);
    if (IsThaddiusPhasePet(PetActive(feugen), PetActive(stalagg)))
        return false;
    return thaddius && !thaddius->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);
}
