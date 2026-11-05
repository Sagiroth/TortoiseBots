#pragma once

#include "policy/guild/EnchantCapitalPolicy.h"

#include <cstdint>

class Player;

namespace TortoiseBots
{
namespace EnchantCapital
{
struct CapitalInn
{
    uint32_t mapId = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float o = 0.0f;
    bool found = false;
};

// The innkeeper spawn inside the capital's zone, from the world DB spawn
// table (same source TravelNode's npc nodes read). Empty when the table
// carries no innkeeper in that zone; the caller then falls back to the
// compiled position below.
CapitalInn FindCapitalInn(bool alliance);

// One position per capital, beside its innkeeper spawn (measured off the
// world DB spawn, not guessed): the DB row is a spawn point, not a floor
// the bot can stand on, so the adapter cannot teleport onto it blindly.
// Refresh against the live DB if the inn moves.
CapitalInn FallbackInn(bool alliance);

// Thin adapter over Player state: reads the verdict inputs and performs the
// safe teleport (dismount, stop, clear travel target, TeleportTo). Homebind
// is never touched: no hearthstone, no TeleportToHomebind.
bool BotInCapital(Player* bot);
TeleportWait WaitReason(Player* bot);
bool TeleportToCapitalInn(Player* bot);

} // namespace EnchantCapital
} // namespace TortoiseBots
