// pi-lens-ignore-file: clang:pp_file_not_found,clang:unknown_typename,clang:use_of_undeclared_identifier,clang:unknown_type_name,clang:undeclared_var_use,clang:incomplete_member_access
#include "WorldBuffKillAdapter.h"
#include "../runtime/WorldBuffPolicy.h"
#include "../ai/playerbot/playerbot.h"

#include "ScriptObjects.h"
#include "Player.h"
#include "Creature.h"

namespace TortoiseBots
{

WorldBuffKillAdapter::WorldBuffKillAdapter()
    : PlayerScript("tortoisebots_worldbuff_kills", { PLAYERHOOK_ON_CREATURE_KILL })
{
}

void WorldBuffKillAdapter::OnCreatureKill(Player* killer, Creature* killed)
{
    if (!killer || !killed)
        return;
    // Runtime gate, not migration-conditional: with the feature off, boss
    // kills never credit the invisible Rally entry.
    if (!sPlayerbotAIConfig.worldBuffsEnabled)
        return;
    uint32_t credit = BossKillCreditEntry(killed->GetEntry());
    if (credit == 0)
        return;
    // Group fan-out in reward distance (same vehicle core uses for event
    // credit): the whole raid party with the quest progresses together.
    killer->RewardPlayerAndGroupAtEvent(credit, killed);
}

} // namespace TortoiseBots
