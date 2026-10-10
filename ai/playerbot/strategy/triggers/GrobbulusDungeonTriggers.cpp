#include "playerbot/playerbot.h"
#include "GrobbulusDungeonTriggers.h"

using namespace ai;

bool GrobbulusInjectionRangedTrigger::IsActive()
{
    if (!ai->IsRanged(bot) || PlayerbotAI::IsTank(bot))
        return false;
    return ai->HasAura(28169, bot);
}
