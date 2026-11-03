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

namespace ai
{
class GuidPosition;
}

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
    bool rpg = false;
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
    // Sweep owner logouts too: active windows owned by the leaving account end
    // quietly instead of waiting for the master-gone poll.
    static void OnBotLogout(Player* bot);
    static void OnOwnerLogout(Player* owner);

    // True when the downtime flag is up: quest accept/turn-in verbs stay off
    // while an errand window runs.
    static bool IsDowntimeActive(PlayerbotAI* ai);

    // Pure movement-mode comparison for the unit test: the real snapshot ->
    // enter -> tick sequence must not read as retaken, an owner stay must.
    static bool MovementModeRetaken(DowntimeEngineKit const& expected, DowntimeEngineKit const& now);

private:
    struct Window
    {
        ObjectGuid masterGuid;
        uint32_t masterAccountId = 0;
        time_t startedAt = 0;
        DowntimeErrand current = DowntimeErrand::None;
        std::vector<DowntimeErrand> tried;
        DowntimeStrategySnapshot before;
        // The kit the window set: retaken compares live state against this,
        // not against the pre-window snapshot it replaced.
        DowntimeStrategySnapshot expected;
        uint32_t lastTickMs = 0;
        uint32_t visitDeadlineMs = 0;
    };

    DowntimeService() = default;

    static DowntimeStrategySnapshot SnapshotStrategies(PlayerbotAI* ai);
    static DowntimeStrategySnapshot ExpectedErrandKit(DowntimeStrategySnapshot const& before);
    // Live NON_COMBAT kit, same shape as the snapshot, for the retaken check.
    static DowntimeEngineKit CurrentMovementKit(PlayerbotAI* ai);
    void EnterErrandKit(Player* bot, Window& window);
    void RestoreMovement(Player* bot, Window& window);
    // Restores the bot's movement kit and tells the master; the caller erases
    // the window entry. Silent skips the master whisper (logout path).
    void End(uint32_t guidLow, Window& window, char const* note, bool silent = false);
    void EndWindowsForOwner(uint32_t masterAccountId, bool silent);

    std::unordered_map<uint32_t, Window> m_windows; // key = bot guid counter
};

} // namespace TortoiseBots
