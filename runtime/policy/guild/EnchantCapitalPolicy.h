#pragma once

// Free teleport to the capital inn for an enchant order's gathering leg.
// Pure data-in/data-out on purpose: the rules are exercised standalone in
// tools/test_enchant_capital_policy.cpp, while the thin core adapter
// (runtime/EnchantCapital.*) owns the Player reads and the TeleportTo call.
// The bot teleports to its faction's main capital (Stormwind for the
// Alliance, Orgrimmar for the Horde). A teleport must never change the bot's
// homebind: the caller uses Player::TeleportTo, never Hearthstone and never
// TeleportToHomebind.

#include <cstdint>

namespace TortoiseBots
{
namespace EnchantCapital
{

// Zone ids the order code already uses for the two capitals
// (commands/Order.cpp): Stormwind City and Orgrimmar.
inline constexpr uint32_t kStormwindZoneId = 1519;
inline constexpr uint32_t kOrgrimmarZoneId = 1637;

// Both capitals sit on the Eastern Kingdoms / Kalimdor overworld maps.
inline constexpr uint32_t kEasternKingdomsMap = 0;
inline constexpr uint32_t kKalimdorMap = 1;

// Hearthstone visuals, without ever using the item: the spell alone
// (8690) is the 10 s cast. Kept in one place so callers agree on it.
inline constexpr uint32_t kHearthstoneSpellId = 8690;

// The bot's main capital by faction. True = Alliance (Stormwind).
inline uint32_t CapitalZoneId(bool alliance) { return alliance ? kStormwindZoneId : kOrgrimmarZoneId; }
inline uint32_t CapitalMapId(bool alliance) { return alliance ? kEasternKingdomsMap : kKalimdorMap; }

// A player standing in its team's capital: the giver meets the bot there,
// so the zone check is the gate for both sides (zone covers the sub-areas:
// Trade District reports zone 1519, Valley of Strength zone 1637).
inline bool IsInCapital(uint32_t playerZoneId, bool alliance)
{
    return playerZoneId == CapitalZoneId(alliance);
}

// Why the bot may not teleport right now. Ok means it may go.
enum class TeleportWait
{
    Ok,
    Combat,
    Instance,
    Group,
    Dead,
    Taxi,
};

// Pure verdict: gathering starts at once and waits for nothing else, so the
// only waits are states where a teleport is unsafe or wrong. group: in a
// group with a real player (a group of bots only is not a wait).
inline TeleportWait CanTeleportNow(bool inCombat, bool inInstance, bool inGroupWithPlayer, bool alive,
    bool onTaxi)
{
    if (inCombat)
        return TeleportWait::Combat;
    if (inInstance)
        return TeleportWait::Instance;
    if (inGroupWithPlayer)
        return TeleportWait::Group;
    if (!alive)
        return TeleportWait::Dead;
    if (onTaxi)
        return TeleportWait::Taxi;
    return TeleportWait::Ok;
}

// Token the command layer reports for each wait (empty for Ok).
inline char const* WaitToken(TeleportWait wait)
{
    switch (wait)
    {
        case TeleportWait::Ok: return "";
        case TeleportWait::Combat: return "combat";
        case TeleportWait::Instance: return "instance";
        case TeleportWait::Group: return "group";
        case TeleportWait::Dead: return "dead";
        case TeleportWait::Taxi: return "taxi";
    }
    return "";
}

} // namespace EnchantCapital
} // namespace TortoiseBots
