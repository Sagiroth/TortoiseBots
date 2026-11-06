#pragma once

#include <cstdint>

// Conjured trade-service rank choice: which food, water or healthstone a mage
// or warlock bot hands out when a trade window opens. Pure data tables taken
// from the core (Spell.dbc Effect 24 EffectItemType for Conjure Food/Water,
// item_template required_level/stackable, spell_warlock Create Healthstone
// item rows): the world-facing side (spellbook scan, bag creation, trade
// slots) lives in TradeStatusAction, so the standalone test never links the
// server.

namespace ai
{
    struct ConjuredRankRow
    {
        uint32_t spellId = 0;
        uint32_t itemId = 0;
        uint32_t traderLevel = 0;
    };

    // Mage Conjure Food ranks 1-7. traderLevel is the created item's
    // required_level from item_template.
    inline ConjuredRankRow const kConjureFoodRanks[] =
    {
        { 587, 5349, 1 },
        { 597, 1113, 5 },
        { 990, 1114, 15 },
        { 6129, 1487, 25 },
        { 10144, 8075, 35 },
        { 10145, 8076, 45 },
        { 28612, 22895, 55 },
    };

    // Mage Conjure Water ranks 1-7, same layout.
    inline ConjuredRankRow const kConjureWaterRanks[] =
    {
        { 5504, 5350, 1 },
        { 5505, 2288, 5 },
        { 5506, 2136, 15 },
        { 6127, 3772, 25 },
        { 10138, 8077, 35 },
        { 10139, 8078, 45 },
        { 10140, 8079, 55 },
    };

    // Warlock Create Healthstone rows 0-4. traderLevel is the base item's
    // required_level; the improved variants share it.
    struct ConjuredStoneRow
    {
        uint32_t spellId = 0;
        uint32_t baseItem = 0;
        uint32_t improved1Item = 0;
        uint32_t improved2Item = 0;
        uint32_t traderLevel = 0;
    };

    inline ConjuredStoneRow const kHealthstoneRanks[] =
    {
        { 6201, 5512, 19004, 19005, 1 },
        { 6202, 5511, 19006, 19007, 12 },
        { 5699, 5509, 19008, 19009, 24 },
        { 11729, 5510, 19010, 19011, 36 },
        { 11730, 9421, 19012, 19013, 48 },
    };

    // Best rank the bot knows (HasSpell) that the trader can use
    // (item required_level <= traderLevel). Rows are ascending, so the last
    // match wins. hasSpell stands in for Player::HasSpell.
    template <typename HasSpell>
    inline ConjuredRankRow const* PickConjuredRank(ConjuredRankRow const* rows,
        uint32_t rowCount, HasSpell hasSpell, uint32_t traderLevel)
    {
        ConjuredRankRow const* best = nullptr;
        for (uint32_t i = 0; i < rowCount; ++i)
            if (rows[i].traderLevel <= traderLevel && hasSpell(rows[i].spellId))
                best = &rows[i];
        return best;
    }

    inline uint32_t HealthstoneItem(ConjuredStoneRow const& row, bool improved1, bool improved2)
    {
        if (improved2)
            return row.improved2Item;
        if (improved1)
            return row.improved1Item;
        return row.baseItem;
    }

    // Same rule for healthstones: best known Create Healthstone rank capped by
    // trader level, with the Improved Healthstone variant when talented.
    template <typename HasSpell>
    inline uint32_t PickHealthstoneItem(HasSpell hasSpell, uint32_t traderLevel,
        bool improved1, bool improved2)
    {
        ConjuredStoneRow const* best = nullptr;
        for (uint32_t i = 0; i < 5; ++i)
            if (kHealthstoneRanks[i].traderLevel <= traderLevel && hasSpell(kHealthstoneRanks[i].spellId))
                best = &kHealthstoneRanks[i];
        return best ? HealthstoneItem(*best, improved1, improved2) : 0;
    }

    // Trader-side healthstone check: any rank or variant counts.
    inline bool IsHealthstoneItem(uint32_t itemId)
    {
        for (uint32_t i = 0; i < 5; ++i)
            if (itemId == kHealthstoneRanks[i].baseItem ||
                itemId == kHealthstoneRanks[i].improved1Item ||
                itemId == kHealthstoneRanks[i].improved2Item)
                return true;
        return false;
    }
}
