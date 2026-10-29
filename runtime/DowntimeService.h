// Town errands on order (`.bot downtime`): a companion alt following its
// player temporarily gets the pool's errand kit - class trainer, junk vendor,
// repair - walks to the nearest reachable NPC for each need, and returns to
// the master when the errands are done or the window cap expires.
//
// State is in memory only; a restart drops every window. Lifecycle calls run
// on the world thread (chat command, BotManager::OnWorldUpdate).
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

// The pre-window kit, snapshot per engine so restore is exact: every layer
// the window adds is removed only when it was absent before.
struct DowntimeEngineKit
{
    bool follow = false;
    bool wander = false;
    bool stay = false;
    bool guard = false;
    bool free = false;
    bool travelOnce = false;
    bool rpgMaintenance = false;
};

struct DowntimeStrategySnapshot
{
    DowntimeEngineKit reaction;
    DowntimeEngineKit nonCombat;
    DowntimeEngineKit combat;
};

enum class DowntimeStartVerdict : uint8_t
{
    Ok,
    AlreadyOut,
    Refused,
};

class DowntimeService
{
public:
    static DowntimeService& Instance();

    // Orders the bot on its errands. On success it walks away from the master;
    // on refusal (or an already-running window) the reply text says why and
    // nothing changes.
    DowntimeStartVerdict Start(Player* bot, Player* master, std::string& reply);

    // BotManager::OnWorldUpdate.
    void Update();

    // Called from the bot logout/shutdown path so an active window restores
    // the kit while the AI still exists instead of relying on the next tick.
    static void OnBotLogout(Player* bot);

private:
    struct Window
    {
        ObjectGuid masterGuid;
        time_t startedAt = 0;
        DowntimeErrand current = DowntimeErrand::None;
        std::vector<DowntimeErrand> tried;
        DowntimeStrategySnapshot before;
        uint32_t lastTickMs = 0;
        uint32_t lastArrivalMs = 0;
        uint32_t visitDeadlineMs = 0;
    };

    DowntimeService() = default;

    static DowntimeStrategySnapshot SnapshotStrategies(PlayerbotAI* ai);
    // True when the owner's movement mode changed since the snapshot:
    // follow/wander/stay/guard/free deltas all end the window.
    static bool MovementModeRetaken(PlayerbotAI* ai, DowntimeStrategySnapshot const& before);
    void EnterErrandKit(Player* bot, Window& window);
    void RestoreMovement(Player* bot, Window& window);
    // Restores the bot's movement kit and tells the master; the caller erases
    // the window entry.
    void End(uint32_t guidLow, Window& window, char const* note);

    std::unordered_map<uint32_t, Window> m_windows; // key = bot guid counter
};

} // namespace TortoiseBots
