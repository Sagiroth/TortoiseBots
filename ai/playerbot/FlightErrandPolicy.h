#pragma once

#include <cstdint>

namespace ai
{
    // Whether one direct taxi leg out of the bot's current flight master is a
    // usable autonomous flight errand for a pool bot (issue #426).
    //
    // The donor (mod-playerbots, gold standard) treats flight as an RPG status:
    // `SelectRandomFlightTaxiNode` (NewRpgBaseAction.cpp:1065) requires a
    // nearest flight master, and `GetOptimalFlightDestinations`
    // (TravelMgr.cpp:4405) only offers zones whose level bracket fits the bot
    // (nearest FM within 500 yd; a bot sitting in a capital is never flown to
    // another capital, which just shuffles it between cities). Ours had no
    // equivalent filter: `RpgTaxiAction::Execute` drew uniformly from every
    // sibling TaxiPath out of the current node, including the short hop home
    // and legs into zones far above the bot - and `RpgTaxiAction::isUseful`
    // additionally required `bot->GetGroup()`, so solo pool bots (the bulk of
    // the pool) never fired it at all. Live night2 pool (4 h): 0 `is flying
    // from` rows in bot_events.csv; pool bots walk everywhere, including the
    // night-elf Teldrassil exit at 11-12 that motivated the issue.
    //
    // Preconditions (known node, same-faction mount) stay at the call site in
    // RpgSubActions.cpp next to the existing checks; the decision below is
    // pure (levels/flags in, verdict out). It fires only when the bot is
    // already standing at a flight master (the rpg-taxi trigger), once per
    // firing over the sibling TaxiPaths of one node: no per-tick world scan,
    // no per-candidate spawn walk.
    //
    // The level window deliberately mirrors the walk gate the bot would face
    // on foot (`RpgTravelDestination::IsPossible` in TravelMgr.cpp): a flight
    // may not land where the walk gate would not let the bot stand. Ceiling
    // +5, outgrown floor +10 with a capital exemption, unknown area levels
    // fail open. Fail-open (not the donor's fail-closed bracket membership)
    // because a cross-continent destination's vmap is usually unloaded, so its
    // area is unresolvable at pick time - failing closed would ground exactly
    // the long flights this errand exists for. The walk layer still vets the
    // bot on arrival.

    // Highest destination area level a bot may fly to: its own level plus the
    // margin the RPG walk gate allows (RpgTravelDestination::IsPossible).
    constexpr std::int32_t FLIGHT_ERRAND_LEVEL_CEILING_OVER_BOT = 5;

    // A destination this far below the bot is outgrown - unless it is a
    // capital, where trainers/AH/bank live (same exemption as the walk gate).
    constexpr std::int32_t FLIGHT_ERRAND_OUTGROWN_MARGIN = 10;

    inline bool FlightErrandDestinationUsable(std::uint32_t botLevel, bool botInCapital,
        std::int32_t destAreaLevel, bool destZoneIsCapital)
    {
        // Levels <= 0 mean "no resolvable area" (unloaded vmap, cross-map
        // destination): fail open, the walk layer vets on arrival.
        if (destAreaLevel > 0 && destAreaLevel > (std::int32_t)botLevel + FLIGHT_ERRAND_LEVEL_CEILING_OVER_BOT)
            return false;

        if (destAreaLevel > 0 && !destZoneIsCapital &&
            destAreaLevel + FLIGHT_ERRAND_OUTGROWN_MARGIN < (std::int32_t)botLevel)
            return false;

        // A bot sitting in a capital gains nothing from flying to another
        // capital (donor GetOptimalFlightDestinations guard).
        if (botInCapital && destZoneIsCapital)
            return false;

        return true;
    }
}
