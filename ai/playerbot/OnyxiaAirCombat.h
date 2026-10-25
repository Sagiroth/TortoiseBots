#pragma once
class PlayerbotAI;
class Unit;
namespace ai
{
    bool IsOnyxiaAirCombatTarget(PlayerbotAI* ai, Unit* target);
    void UpdateOnyxiaAirCombat(PlayerbotAI* ai);
}
