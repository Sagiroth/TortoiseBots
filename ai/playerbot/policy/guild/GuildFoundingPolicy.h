#pragma once

#include <cstdint>

// Pure eligibility for the autonomous guild-founding trip (petition buy and
// turn-in legs). The world-facing reads (charter item, signature count, guild
// membership, grouper/guilder roles, free money, area flags, travel state)
// live in the callers (ShouldTravelNamedValue "petition",
// PetitionTurnInAction::isUseful, BuyPetitionAction::canBuyPetition); this
// header holds only the level, affordability and cap math so the standalone
// test never links the server.

namespace ai
{
    // Guild Charter item and its vendor price (core PetitionsHandler.cpp:
    // GUILD_CHARTER 5863, GUILD_CHARTER_COST 1000 copper).
    constexpr uint32_t GUILD_CHARTER_ITEM_ID = 5863;
    constexpr uint32_t GUILD_CHARTER_COST_COPPER = 1000;

    // Charter budget rule (BudgetValues.cpp NeedMoneyFor::guild): a bot above
    // level 10 without a charter reserves the charter price; at 10 and below
    // no charter money is reserved, so no trip either.
    constexpr uint32_t GUILD_CHARTER_MIN_LEVEL = 10;

    // True when this bot level reserves charter money at all.
    inline bool GuildCharterLevelEligible(uint32_t botLevel)
    {
        return botLevel > GUILD_CHARTER_MIN_LEVEL;
    }

    // True when the free-money purse covers the charter price.
    inline bool GuildCharterAffordable(uint32_t freeMoneyForGuild)
    {
        return freeMoneyForGuild >= GUILD_CHARTER_COST_COPPER;
    }

    // Turn-in leg needs the core's required signature count
    // (CONFIG_UINT32_MIN_PETITION_SIGNS, default 9). Plain comparison so the
    // value and the action share one spelling of the gate.
    inline bool GuildPetitionReadyToTurnIn(uint8_t signs, uint32_t minSigns)
    {
        return signs >= minSigns;
    }

    // Buy-leg trip need: unguilded, no charter yet, level and purse allow it.
    inline bool GuildFoundingBuyTripNeeded(bool inGuild, bool hasCharter,
        uint32_t botLevel, uint32_t freeMoneyForGuild)
    {
        if (inGuild || hasCharter)
            return false;
        return GuildCharterLevelEligible(botLevel) && GuildCharterAffordable(freeMoneyForGuild);
    }

    // Turn-in-leg trip need: unguilded, charter in bags, signatures complete.
    inline bool GuildFoundingTurnInTripNeeded(bool inGuild, bool hasCharter,
        uint8_t signs, uint32_t minSigns)
    {
        if (inGuild || !hasCharter)
            return false;
        return GuildPetitionReadyToTurnIn(signs, minSigns);
    }
}
