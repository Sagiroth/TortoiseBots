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
    std::lock_guard<std::mutex> lock(mutex_);
    if (!player) return;
    ObjectGuid guid = player->GetObjectGuid();
    byGuid_.erase(guid);
    byPlayer_.erase(player);
}

PlayerbotAI* PlayerbotAIStorage::GetAI(Player* player) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!player) return nullptr;
    auto it = byPlayer_.find(player);
    if (it != byPlayer_.end()) return it->second;
    // Fallback: lookup by guid (covers cases where Player* identity changed but GUID same)
    ObjectGuid guid = player->GetObjectGuid();
    auto it2 = byGuid_.find(guid);
    if (it2 != byGuid_.end()) return it2->second;
    return nullptr;
}

PlayerbotAI* PlayerbotAIStorage::GetAI(ObjectGuid guid) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = byGuid_.find(guid);
    if (it != byGuid_.end()) return it->second;
    return nullptr;
}
