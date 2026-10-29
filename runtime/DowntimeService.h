#pragma once

#include "DowntimePolicy.h"

#include "ObjectGuid.h"

#include <ctime>
#include <unordered_map>
#include <vector>

class Player;
class PlayerbotAI;

namespace TortoiseBots
{

// Town errands on order (`.bot downtime`): a companion alt following its
// player temporarily gets the pool's errand kit - class trainer, junk vendor,
// repair - walks to the nearest reachable NPC for each need, and returns to
// the master when the errands are done or the window cap expires.
//
// State is in memory only; a restart drops every window. Lifecycle calls run
// on the world thread (chat command, BotManager::OnWorldUpdate).
class DowntimeService
{
public:
    static DowntimeService& Instance();

    // Orders the bot on its errands. On success it walks away from the master;
    // on refusal the reply text says why and nothing changes.
    bool Start(Player* bot, Player* master, std::string& reply);

    // BotManager::OnWorldUpdate.
    void Update();

    bool IsOnDowntime(uint32_t guidLow) const;

private:
    struct Window
    {
        ObjectGuid masterGuid;
        time_t startedAt = 0;
        DowntimeErrand current = DowntimeErrand::None;
        std::vector<DowntimeErrand> tried;
        bool hadFollow = false;
        bool hadWander = false;
        uint32_t lastTickMs = 0;
        uint32_t lastArrivalMs = 0;
        uint32_t visitDeadlineMs = 0;
    };

    DowntimeService() = default;

    void EnterErrandKit(Player* bot, Window& window);
    void RestoreMovement(Player* bot, Window& window);
    // Restores the bot's movement kit and tells the master; the caller erases
    // the window entry.
    void End(uint32_t guidLow, Window& window, char const* note);

    std::unordered_map<uint32_t, Window> m_windows; // key = bot guid counter
};

} // namespace TortoiseBots
