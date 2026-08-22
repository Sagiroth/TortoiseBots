#pragma once
// pi-lens-ignore: clang:pp_file_not_found,clang:unknown_typename,clang:unknown_type_name,clang:use_of_undeclared_identifier
#include "ObjectGuid.h"
#include <map>
#include <unordered_map>
#include <mutex>

class Player;
class PlayerbotAI;

// Module-local PlayerbotAI registry — replaces core Player::m_playerbotAI / GetPlayerbotAI() coupling.
// The Tortoise core remains unaware of PlayerBots; the module maps Player* / ObjectGuid -> PlayerbotAI*
// via this registry, keyed by the Headless GUID lifecycle (BotManager).
// This is the TortoiseBots equivalent of the donor's Player::GetPlayerbotAI() but kept entirely
// in the module, consistent with the generic Headless SessionTransport architecture.

class PlayerbotAIStorage
{
public:
    static PlayerbotAIStorage& Instance();

    void SetAI(Player* player, PlayerbotAI* ai);
    void RemoveAI(Player* player);
    PlayerbotAI* GetAI(Player* player) const;
    PlayerbotAI* GetAI(ObjectGuid guid) const;

    // Helper for donor code that does `player->GetPlayerbotAI()` — use `GetAI(player)` instead.
    // Provided as a free function for easy search/replace in donor files.

private:
    mutable std::mutex mutex_;
    std::unordered_map<ObjectGuid, PlayerbotAI*> byGuid_;
    std::unordered_map<Player*, PlayerbotAI*> byPlayer_;
};

inline PlayerbotAI* GetPlayerbotAI(Player* player) { return PlayerbotAIStorage::Instance().GetAI(player); }
inline PlayerbotAI* GetPlayerbotAI(ObjectGuid guid) { return PlayerbotAIStorage::Instance().GetAI(guid); }
