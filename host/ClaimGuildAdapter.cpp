// pi-lens-ignore-file: clang:pp_file_not_found,clang:unknown_typename,clang:use_of_undeclared_identifier,clang:unknown_type_name,clang:undeclared_var_use,clang:incomplete_member_access
#include "ClaimGuildAdapter.h"
#include "../runtime/ClaimLifecycle.h"
#include "../runtime/GuildOrderStore.h"

#include "Guild/Guild.h"
#include "Player.h"

namespace TortoiseBots
{

ClaimGuildAdapter::ClaimGuildAdapter() : GuildScript("tortoisebots_claim_guild")
{
}

void ClaimGuildAdapter::OnRemoveMember(Guild* /*guild*/, Player* player, bool /*isDisbanding*/, bool /*isKicked*/)
{
    if (!player)
        return;

    ClaimLifecycle::Instance().OnGuildDelMember(player->GetGUIDLow());
    GuildOrders::OnGuildDelMember(player->GetGUIDLow());
}

void ClaimGuildAdapter::OnDisband(Guild* guild)
{
    if (!guild)
        return;

    ClaimLifecycle::Instance().OnGuildDisband(guild->GetId());
    GuildOrders::OnGuildDisband(guild->GetId());
}

} // namespace TortoiseBots
