#pragma once
#include <cstdint>
#include <ctime>
#include <map>
#include <utility>

// A bot that refuses to loot or gather an object an enemy is blocking whispers
// "X is blocking Y" once per object per 10 minutes. The record is shared by
// every bot, and two real players in one group share one warning, so the key
// names the group when the requester is grouped and the requester's own guid
// otherwise. Core-free so the standalone policy test can include it.

namespace ai
{
    constexpr time_t kBlockedTellCooldownSeconds = 600;
    constexpr size_t kBlockedTellMaxEntries = 256;

    // A group id is tagged into the top bit, which no player guid sets, so a
    // group record and a player record can never collide.
    constexpr uint64_t kBlockedTellGroupTag = uint64_t(1) << 63;

    // Owner half of the cooldown key: the group's id when the requester is in
    // one, the requester's guid when it is not.
    inline uint64_t BlockedTellOwner(uint64_t requesterGuid, uint32_t groupId)
    {
        return groupId ? (kBlockedTellGroupTag | uint64_t(groupId)) : requesterGuid;
    }

    // True when this owner may whisper about this object now; records the
    // whisper when it may. Expired records are pruned, live ones never are.
    inline bool BlockedTellAllowed(std::map<std::pair<uint64_t, uint64_t>, time_t>& told,
                                   uint64_t owner, uint64_t objectGuid, time_t now)
    {
        std::map<std::pair<uint64_t, uint64_t>, time_t>::iterator it =
            told.find(std::make_pair(owner, objectGuid));
        if (it != told.end() && now - it->second < kBlockedTellCooldownSeconds)
            return false;

        if (told.size() > kBlockedTellMaxEntries)
        {
            for (it = told.begin(); it != told.end();)
            {
                if (now - it->second >= kBlockedTellCooldownSeconds)
                    it = told.erase(it);
                else
                    ++it;
            }
        }

        told[std::make_pair(owner, objectGuid)] = now;
        return true;
    }
}
