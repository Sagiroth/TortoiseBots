#pragma once

// pi-lens-ignore: clang:pp_file_not_found
#include "ScriptObjects.h"

namespace TortoiseBots
{

// Module-owned GuildScript adapter for claimed guild bot departure (Issue #489).
// When a claimed bot is removed from a guild or the guild is disbanded,
// it unclaims the bot back to the wandering pool.
class ClaimGuildAdapter final : public GuildScript
{
public:
    ClaimGuildAdapter();

    void OnAddMember(Guild* guild, Player* player, uint8& rank) override;
    void OnRemoveMember(Guild* guild, Player* player, bool isDisbanding, bool isKicked) override;
    void OnDisband(Guild* guild) override;
};

} // namespace TortoiseBots
