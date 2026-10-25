#pragma once
#include <cstdint>
#include <string>
class PlayerbotAI;
class Unit;
namespace ai
{
    class Action;
    enum class OnyxiaAirLog { Recovery, State, Reach, Cast, Count };
    struct OnyxiaAirCombatState
    {
        std::uint64_t target = 0;
        std::uint32_t choiceAt = 0;
        std::uint64_t recovered = 0;
        std::uint32_t aoeAt = 0;
        unsigned aoeWhelps = 0;
        std::uint32_t lastLog[static_cast<unsigned>(OnyxiaAirLog::Count)]{};
    };
    bool IsOnyxiaAirCombatTarget(PlayerbotAI* ai, Unit* target);
    bool SuppressOnyxiaAirAoe(PlayerbotAI* ai, Action* action);
    bool OnyxiaAirNeedsAssist(PlayerbotAI* ai);
    void UpdateOnyxiaAirCombat(PlayerbotAI* ai);
    void LogOnyxiaAir(PlayerbotAI* ai, OnyxiaAirLog kind, char const* reason,
        Unit* target = nullptr, std::string const& detail = "");
    void TraceOnyxiaAirAction(PlayerbotAI* ai, Action* action, char const* stage, bool success);
}
