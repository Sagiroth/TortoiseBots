#include "PlayerbotAIStorage.h"
#include "ObjectGuid.h"
#include "Player.h"

PlayerbotAIStorage& PlayerbotAIStorage::Instance()
{
    static PlayerbotAIStorage instance;
    return instance;
}

void PlayerbotAIStorage::SetAI(Player* player, PlayerbotAI* ai)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!player) return;
    ObjectGuid guid = player->GetObjectGuid();
    if (ai)
    {
        byGuid_[guid] = ai;
        byPlayer_[player] = ai;
    }
    else
    {
        byGuid_.erase(guid);
        byPlayer_.erase(player);
    }
}

void PlayerbotAIStorage::RemoveAI(Player* player)
{
    // PlayerbotAIAdapter can outlive Player during WorldSession logout. A raw
    // pointer remains a valid map key but must never be dereferenced here.
    std::lock_guard<std::mutex> lock(mutex_);
    if (!player) return;
    auto playerIt = byPlayer_.find(player);
    if (playerIt == byPlayer_.end()) return;
    PlayerbotAI* ai = playerIt->second;
    byPlayer_.erase(playerIt);
    for (auto it = byGuid_.begin(); it != byGuid_.end(); )
    {
        if (it->second == ai) it = byGuid_.erase(it);
        else ++it;
    }
}

PlayerbotAI* PlayerbotAIStorage::GetAI(Player* player) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!player) return nullptr;
    auto it = byPlayer_.find(player);
    if (it != byPlayer_.end()) return it->second;
    ObjectGuid guid = player->GetObjectGuid();
    auto it2 = byGuid_.find(guid);
    if (it2 != byGuid_.end()) return it2->second;
    return nullptr;
}

PlayerbotAI* PlayerbotAIStorage::GetAI(ObjectGuid guid) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = byGuid_.find(guid);
    return it != byGuid_.end() ? it->second : nullptr;
}
