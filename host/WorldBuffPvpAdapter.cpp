// pi-lens-ignore-file: clang:pp_file_not_found,clang:unknown_typename,clang:use_of_undeclared_identifier,clang:unknown_type_name,clang:undeclared_var_use,clang:incomplete_member_access
#include "WorldBuffPvpAdapter.h"
#include "../runtime/WorldBuffPolicy.h"
#include "../ai/playerbot/playerbot.h"

#include "ScriptObjects.h"
#include "Player.h"

namespace TortoiseBots
{

WorldBuffPvpAdapter::WorldBuffPvpAdapter()
    : PlayerScript("tortoisebots_worldbuff_pvp", { PLAYERHOOK_ON_PVP_KILL })
{
}

void WorldBuffPvpAdapter::OnPVPKill(Player* killer, Player* killed)
{
    if (!killer || !killed)
        return;
    if (!sPlayerbotAIConfig.worldBuffsEnabled)
        return;
    // Silithus only, opposite faction only.
    if (killer->GetZoneId() != 1377)
        return;
    if (killer->GetTeam() == killed->GetTeam())
        return;
    // Group fan-out in reward distance: grants credit to every member in the
    // killer's group/raid who has the quest in progress (including players whose
    // bots landed the final blow).
    killer->RewardPlayerAndGroupAtEvent(kWorldBuffSilithystCredit, killed);
}

} // namespace TortoiseBots
