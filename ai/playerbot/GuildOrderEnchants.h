#pragma once

#include "runtime/GuildOrderEnchantPolicy.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ai
{

struct GuildOrderEnchantMaterial
{
    uint32_t itemId = 0;
    uint32_t count = 0;
};

// Read-only catalogue of permanent item enchants, built once at startup from
// the server's own spell and skill-line data (see runtime/GuildOrderEnchantPolicy.h
// for the slot and matching rules). W3 wires no command or TBM output; the
// search tasks read this catalogue.
void BuildGuildOrderEnchantIndex();
bool GuildOrderEnchantsReady();

// All catalogue entries, sorted by required skill ascending then name.
std::vector<TortoiseBots::GuildOrders::EnchantEntry> GuildOrderEnchants();

// One entry's materials: reagents in spell order, then the recipe tools
// (runed rods) the bot always carries.
std::vector<GuildOrderEnchantMaterial> GuildOrderEnchantReagents(uint32_t spellId);
std::vector<uint32_t> GuildOrderEnchantTools(uint32_t spellId);

} // namespace ai
