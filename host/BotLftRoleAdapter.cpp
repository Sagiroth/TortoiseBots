// pi-lens-ignore: clang:pp_file_not_found,clang:unknown_typename,clang:unknown_type_name,clang:undeclared_var_use,clang:use_of_undeclared_identifier,clang:incomplete_member_access,clang:member_decl_does_not_match,clang:all
#include "BotLftRoleAdapter.h"

#include "../runtime/BotManager.h"
#include "../runtime/LftBotFillService.h"
#include "../runtime/PlayerbotAIStorage.h"
#include "../ai/playerbot/AiFactory.h"
#include "../ai/playerbot/PlayerbotAI.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "Player.h"

namespace TortoiseBots {

BotLftRoleAdapter::BotLftRoleAdapter()
    // pi-lens-ignore: clang:undeclared_var_use,clang:use_of_undeclared_identifier
    : PlayerScript("tortoisebots_lft_roles", { PLAYERHOOK_GET_ALLOWED_ROLES })
{
}

// pi-lens-ignore: clang:unknown_typename,clang:unknown_typename_suggest,clang:member_decl_does_not_match
bool BotLftRoleAdapter::GetAllowedRoles(Player const* player, uint8& roles)
{
    if (!player)
        return false;
    // pi-lens-ignore: clang:incomplete_member_access
    ObjectGuid guid = player->GetObjectGuid();
    if (!BotManager::Instance().IsBot(guid))
        return false;
    // Constrain to autonomous fill-owned Headless bots; preserve manual human-owned bot role semantics.
    // Human-owned bots (with active master) must keep manual/class role, not our spec override.
    // pi-lens-ignore: clang:incomplete_member_access
    Player* p = const_cast<Player*>(player);
    // pi-lens-ignore: clang:incomplete_member_access
    if (!p->GetSession() || !p->GetSession()->IsHeadless())
        return false;
    if (!BotManager::Instance().IsRandomBot(guid))
        return false;
    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(p);
    if (!ai)
        return false;
    if (ai->HasActivePlayerMaster())
        return false;
    // Only answer for bots we are filling (pending) or have just forced a role for.
    // At QueuePlayer time pending is not yet recorded but forced role is already set, so check both.
    // pi-lens-ignore: clang:unknown_typename,clang:unknown_type_name,clang:incomplete_member_access,clang:all
    uint32 guidLow = guid.GetCounter();
    bool isPending = LftBotFillService::Instance().IsPending(guidLow);
    bool hasForced = ai->GetForcedRole() != 0;
    if (!isPending && !hasForced)
        return false;
    // pi-lens-ignore: clang:unknown_typename
    BotRoles botRole = AiFactory::GetPlayerRoles(player);
    // pi-lens-ignore: clang:undeclared_var_use,clang:use_of_undeclared_identifier
    if (botRole == BOT_ROLE_NONE)
        return false;
    // pi-lens-ignore: clang:unknown_typename,clang:unknown_typename_suggest
    roles = static_cast<uint8>(botRole);
    return true;
}

} // namespace TortoiseBots
