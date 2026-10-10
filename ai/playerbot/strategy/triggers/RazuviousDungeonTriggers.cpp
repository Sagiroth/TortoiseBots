#include "playerbot/playerbot.h"
#include "RazuviousDungeonTriggers.h"
#include "playerbot/GroupMembers.h"

using namespace ai;

bool RazuviousMindControlTrigger::IsActive()
{
    if (bot->GetClass() != CLASS_PRIEST)
        return false;
    if (Unit* charm = bot->GetCharm())
        return charm->GetEntry() == 16803;

    Group* group = bot->GetGroup();
    if (!group)
        return false;
    unsigned seen = 0;
    for (Player* member : LiveGroupMembers(group))
    {
        if (!member || member->GetClass() != CLASS_PRIEST || !member->IsAlive())
            continue;
        if (member == bot)
            return true;
        if (++seen >= 2)
            return false;
    }
    return false;
}
