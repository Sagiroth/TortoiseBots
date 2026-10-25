#pragma once
#include <algorithm>
#include <cstdint>

namespace TortoiseBots
{
enum class ConsumableCategory { Health, Mana, Elixir, Scroll, Flask, Food };
enum ConsumableRole : unsigned { Tank = 1, Healer = 2, Caster = 4, Physical = 8 };
struct RoleConsumable { std::uint32_t item; ConsumableCategory category; unsigned roles; };
// Strongest first within each category. Native item data supplies level, spell,
// class/skill restrictions and stack limits. No expansion items or protection pots.
inline constexpr RoleConsumable RoleConsumables[] = {
    {13510, ConsumableCategory::Flask, Tank | Physical},
    {13511, ConsumableCategory::Flask, Healer},
    {13512, ConsumableCategory::Flask, Caster},
    {13446, ConsumableCategory::Health, 15}, {3928, ConsumableCategory::Health, 15},
    {1710, ConsumableCategory::Health, 15}, {929, ConsumableCategory::Health, 15},
    {858, ConsumableCategory::Health, 15}, {118, ConsumableCategory::Health, 15},
    {13444, ConsumableCategory::Mana, 15}, {13443, ConsumableCategory::Mana, 15},
    {6149, ConsumableCategory::Mana, 15}, {3827, ConsumableCategory::Mana, 15},
    {3385, ConsumableCategory::Mana, 15}, {2455, ConsumableCategory::Mana, 15},
    {13445, ConsumableCategory::Elixir, Tank}, {8951, ConsumableCategory::Elixir, Tank},
    {3389, ConsumableCategory::Elixir, Tank}, {5997, ConsumableCategory::Elixir, Tank},
    {13447, ConsumableCategory::Elixir, Healer}, {9179, ConsumableCategory::Elixir, Healer},
    {13454, ConsumableCategory::Elixir, Caster}, {9155, ConsumableCategory::Elixir, Caster},
    {3383, ConsumableCategory::Elixir, Healer | Caster},
    {13452, ConsumableCategory::Elixir, Physical}, {9187, ConsumableCategory::Elixir, Physical},
    {8949, ConsumableCategory::Elixir, Physical}, {3390, ConsumableCategory::Elixir, Physical},
    {2457, ConsumableCategory::Elixir, Physical},
    {10307, ConsumableCategory::Scroll, Tank}, {4422, ConsumableCategory::Scroll, Tank},
    {1711, ConsumableCategory::Scroll, Tank}, {1180, ConsumableCategory::Scroll, Tank},
    {10308, ConsumableCategory::Scroll, Healer | Caster}, {4419, ConsumableCategory::Scroll, Healer | Caster},
    {2290, ConsumableCategory::Scroll, Healer | Caster}, {955, ConsumableCategory::Scroll, Healer | Caster},
    {10309, ConsumableCategory::Scroll, Physical}, {4425, ConsumableCategory::Scroll, Physical},
    {1477, ConsumableCategory::Scroll, Physical}, {3012, ConsumableCategory::Scroll, Physical},
    {13931, ConsumableCategory::Food, Healer}, {18254, ConsumableCategory::Food, Caster},
    {13928, ConsumableCategory::Food, Physical}, {21023, ConsumableCategory::Food, Tank},
    // Additional Classic master signals; never copy resistance/utility effects.
    {13513, ConsumableCategory::Flask, 0},
    {10305, ConsumableCategory::Scroll, 0}, {10306, ConsumableCategory::Scroll, 0},
    {10310, ConsumableCategory::Scroll, 0}, {954, ConsumableCategory::Scroll, 0},
    {1181, ConsumableCategory::Scroll, 0}, {1478, ConsumableCategory::Scroll, 0},
    {1712, ConsumableCategory::Scroll, 0}, {2289, ConsumableCategory::Scroll, 0},
    {3013, ConsumableCategory::Scroll, 0}, {4421, ConsumableCategory::Scroll, 0},
    {4424, ConsumableCategory::Scroll, 0}, {4426, ConsumableCategory::Scroll, 0},
    {2454, ConsumableCategory::Elixir, 0}, {2458, ConsumableCategory::Elixir, 0},
    {3391, ConsumableCategory::Elixir, 0}, {3825, ConsumableCategory::Elixir, 0},
    {6373, ConsumableCategory::Elixir, 0}, {9206, ConsumableCategory::Elixir, 0},
    {9264, ConsumableCategory::Elixir, 0}, {13453, ConsumableCategory::Elixir, 0},
    {17708, ConsumableCategory::Elixir, 0}, {21546, ConsumableCategory::Elixir, 0},
    {20452, ConsumableCategory::Food, 0}, {13813, ConsumableCategory::Food, 0}
};

inline bool ConsumableUnlocked(ConsumableCategory category, bool provisionHired,
                               bool potions, bool elixirs, bool flasks)
{
    if (provisionHired) return true;
    if (!potions) return false;
    if (category == ConsumableCategory::Health || category == ConsumableCategory::Mana) return true;
    if (!elixirs) return false;
    return category != ConsumableCategory::Flask || flasks;
}
inline std::uint32_t ConsumableTopUp(ConsumableCategory category, std::uint32_t have, std::uint32_t stack)
{
    std::uint32_t goal = category == ConsumableCategory::Flask ? 2 :
        category == ConsumableCategory::Health || category == ConsumableCategory::Mana ? 10 : 5;
    goal = std::min(goal, stack);
    return have < goal ? goal - have : 0;
}
inline bool NeedsConsumablePotion(ConsumableCategory category, bool combat,
                                 float health, float mana, bool hasMana)
{
    return combat && (category == ConsumableCategory::Health ? health < 30.0f :
        category == ConsumableCategory::Mana && hasMana && mana < 65.0f);
}
}
