
#include "playerbot/playerbot.h"
#include "NaxxramasDungeonTriggers.h"

using namespace ai;
#include "playerbot/strategy/actions/NaxxramasDungeonActions.h"
bool MaexxnaRescueTrigger::IsActive()
{
    Unit* wrap = GetMaexxnaRescueTarget(ai);
    return wrap && wrap != AI_VALUE(Unit*, "current target");
}
