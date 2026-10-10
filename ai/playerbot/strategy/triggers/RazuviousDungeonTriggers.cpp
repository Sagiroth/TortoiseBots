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
    // Two controllers. Priests already holding an Understudy keep their slot;
    // the rest is filled by non-healer priests first, then healers, each in
    // group slot order, so the raid keeps its healing priests when it can.
    std::vector<Player*> holders, dps, heal;
    for (Player* member : LiveGroupMembers(group))
    {
        if (!member || member->GetClass() != CLASS_PRIEST || !member->IsAlive())
            continue;
        if (member->GetCharm())
            holders.push_back(member);
        else if (ai->IsHeal(member))
            heal.push_back(member);
        else
            dps.push_back(member);
    }
    std::vector<Player*> order = holders;
    order.insert(order.end(), dps.begin(), dps.end());
    order.insert(order.end(), heal.begin(), heal.end());
    for (size_t i = 0; i < order.size() && i < 2; ++i)
        if (order[i] == bot)
            return true;
    return false;
}
