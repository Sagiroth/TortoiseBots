#include "ClaimGuildAdapter.h"
#include "../runtime/ClaimLifecycle.h"
#include "../runtime/GuildOrderAccess.h"
#include "../runtime/GuildOrderStore.h"

#include "Guild/Guild.h"
#include "Player.h"

namespace TortoiseBots
{

ClaimGuildAdapter::ClaimGuildAdapter() : GuildScript("tortoisebots_claim_guild")
{
}

void ClaimGuildAdapter::OnAddMember(Guild* guild, Player* player, uint8& rank)
{
    if (!guild || !player)
        return;

    // The core inserts the member with this rank right after the hook, so a
    // bot joins at the guild's bot rank at once. Anyone else keeps the rank
    // the core assigned.
    if (std::optional<uint32_t> botRank = GuildOrders::JoinBotRank(guild->GetId(), player->GetGUIDLow()))
        rank = static_cast<uint8>(*botRank);
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
    GuildOrders::OnBotRankDisband(guild->GetId());
}

} // namespace TortoiseBots
