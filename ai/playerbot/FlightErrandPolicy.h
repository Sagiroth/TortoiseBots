#pragma once

#include <cstdint>

namespace ai
{
    // Whether one direct taxi leg out of the bot's current flight master may
    // carry the bot toward the travel destination it already chose (#426).
    //
    // A flight is transport, not an errand: the destination the bot walks to
    // was already vetted by the pick gates (level fit, #418 objective cap,
    // #428 point danger, #434 taker gate, hostile towns, route survival), so
    // the only question left here is whether THIS leg lands the bot somewhere
    // it may stand and meaningfully closer to that destination. The donor
    // (mod-playerbots, gold standard) treats flight the same way: its
    // `TravelFlight` status routes a bot with a cross-zone goal through the
    // flight master toward that goal (`SelectRandomFlightTaxiNode`,
    // NewRpgBaseAction.cpp:1065; `GetOptimalFlightDestinations`,
    // TravelMgr.cpp:4405). Live night2 pool (4 h): 0 `is flying from` rows in
    // bot_events.csv; pool bots walk everywhere, including the night-elf
    // Teldrassil exit at 11-12 that motivated the issue.
    //
    // DBC and taxi-mask reads stay at the call site; the decision below is
    // pure (numbers/flags in, verdict out) so it is unit-testable and costs
    // nothing on the world thread. It runs only when the resolved travel
    // route already contains a flight leg out of the nearest flight master —
    // once per travel-target pick, never per tick.

    // A flight leg only pays off over distance: below this the bot walks.
    constexpr float FLIGHT_TRANSPORT_MIN_TRIP_YD = 1500.0f;

    // The landing must shorten the remaining walk by at least this much, or
    // the bot paid a fare to stand where it started.
    constexpr float FLIGHT_TRANSPORT_MIN_SAVED_YD = 500.0f;

    // Highest destination area level a bot may fly to: its own level plus the
    // margin the RPG walk gate allows (RpgTravelDestination::IsPossible).
    // Unknown levels FAIL CLOSED (<= 0 refuses): an unresolvable destination
    // is how a low-level bot strands itself in a high-level zone with every
    // local gate (#418/#428/#434) rejecting all content.
    constexpr std::int32_t FLIGHT_TRANSPORT_LEVEL_CEILING_OVER_BOT = 5;

    // A destination this far below the bot is outgrown - unless it is a
    // capital, where trainers/AH/bank live (same exemption as the walk gate).
    constexpr std::int32_t FLIGHT_TRANSPORT_OUTGROWN_MARGIN = 10;

    inline bool FlightTransportDestinationUsable(std::uint32_t botLevel, bool botInCapital,
        std::int32_t destAreaLevel, bool destZoneIsCapital)
    {
        // Fail closed: no resolvable area (unloaded tile, cross-map node),
        // no flight. The walk layer never gets to vet a bot that never lands.
        if (destAreaLevel <= 0)
            return false;

        if (destAreaLevel > (std::int32_t)botLevel + FLIGHT_TRANSPORT_LEVEL_CEILING_OVER_BOT)
            return false;

        if (!destZoneIsCapital &&
            destAreaLevel + FLIGHT_TRANSPORT_OUTGROWN_MARGIN < (std::int32_t)botLevel)
            return false;

        // A bot sitting in a capital gains nothing from flying to another
        // capital (donor GetOptimalFlightDestinations guard).
        if (botInCapital && destZoneIsCapital)
            return false;

        return true;
    }

    // Whether the leg is worth the fare: the trip is long and the landing is
    // meaningfully closer to the destination than the takeoff.
    inline bool FlightTransportLegWorthwhile(float tripDistanceYd,
        float walkFromTakeoffYd, float walkFromLandingYd)
    {
        if (tripDistanceYd < FLIGHT_TRANSPORT_MIN_TRIP_YD)
            return false;

        return (walkFromTakeoffYd - walkFromLandingYd) >= FLIGHT_TRANSPORT_MIN_SAVED_YD;
    }

    // Whether the bot may spend the fare: own gold, above the class-trainer
    // reserve it must not eat into.
    inline bool FlightTransportAffordable(std::uint32_t botMoney, std::uint32_t fare,
        std::uint32_t trainerReserve)
    {
        if (fare == 0)
            return botMoney > trainerReserve;

        return botMoney >= fare && (botMoney - fare) >= trainerReserve;
    }
}
