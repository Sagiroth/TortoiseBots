#pragma once

// The exact wording for the core's per-account instance limit (Player.h
// MAX_INSTANCE_PER_ACCOUNT_PER_HOUR, AccountMgr::CheckInstanceCount): an account may enter only so many
// different instances an hour. A refused entry used to be reported together with unrelated reasons (a full
// instance, an encounter in progress, a script). The text names the bot, never the account name.
// Plain data and the standard library only.

#include <cstdint>
#include <string>

namespace TortoiseBots {

inline std::string InstanceLimitReason(std::string const& botName, std::uint32_t limit)
{
    return "instance limit: " + (botName.empty() ? std::string("the bot") : botName) + "'s account entered " +
        std::to_string(limit) + " instances this hour";
}

} // namespace TortoiseBots
