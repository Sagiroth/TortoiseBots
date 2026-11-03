#pragma once
class Player;
class Unit;
namespace ai
{
// Encounter-only direct-target exclusion. Damage/threat and incidental AoE remain real.
bool IgnoreFankrissGauntlet(Player* bot, Unit* target);
}
