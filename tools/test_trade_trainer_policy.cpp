#include "../runtime/policy/provision/TradeTrainerPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::TradeSpellJustifiesTrip;
using ai::TrainerRowTeachesFirstRankPrimary;
using ai::FirstRankPrimaryTeachableNow;
using ai::TaughtSpellIds;

int main()
{
    // Next rank or recipe for a held skill: always worth the walk.
    CHECK(TradeSpellJustifiesTrip(171, true, false, false));
    CHECK(TradeSpellJustifiesTrip(171, true, false, true));
    CHECK(TradeSpellJustifiesTrip(171, true, true, false));

    // First-rank primary with a free slot: the bot could take it up.
    CHECK(TradeSpellJustifiesTrip(182, false, true, true));

    // First-rank primary without a free slot: no third primary.
    CHECK(!TradeSpellJustifiesTrip(182, false, true, false));

    // Higher rank or recipe for an unheld skill: teaches nothing usable.
    CHECK(!TradeSpellJustifiesTrip(182, false, false, true));
    CHECK(!TradeSpellJustifiesTrip(182, false, false, false));

    // Unresolvable skill id: fail closed, no trip.
    CHECK(!TradeSpellJustifiesTrip(0, false, true, true));
    CHECK(!TradeSpellJustifiesTrip(0, true, false, true));

    // The predicate stands in for core IsPrimaryProfessionFirstRankSpell:
    // 2575 Mining is rank 1, 2581 Apprentice Miner (the teaching spell) and
    // 3564 Journeyman Mining are not.
    auto isFirstRank = [](uint32_t id) { return id == 2575; };

    // Direct first-rank row: the row spell itself is rank 1.
    CHECK(TrainerRowTeachesFirstRankPrimary(2575, TaughtSpellIds(), isFirstRank));
    CHECK(!TrainerRowTeachesFirstRankPrimary(3564, TaughtSpellIds(), isFirstRank));
    CHECK(!TrainerRowTeachesFirstRankPrimary(0, TaughtSpellIds(), isFirstRank));

    // Teaching-spell row: 2581 is not rank 1 itself, but its taught spell
    // 2575 is - the guard must fire (core GetTrainerSpellState shape).
    TaughtSpellIds taught;
    taught.ids[0] = 2575;
    taught.count = 1;
    CHECK(TrainerRowTeachesFirstRankPrimary(2581, taught, isFirstRank));

    // Taught spells without a rank-1 primary: no guard.
    TaughtSpellIds other;
    other.ids[0] = 3564;
    other.ids[1] = 9999;
    other.count = 2;
    CHECK(!TrainerRowTeachesFirstRankPrimary(2581, other, isFirstRank));

    // Only the first `count` ids are scanned.
    TaughtSpellIds stale;
    stale.ids[0] = 3564;
    stale.ids[1] = 2575;
    stale.count = 1;
    CHECK(!TrainerRowTeachesFirstRankPrimary(2581, stale, isFirstRank));

    // Visit gate: a first-rank row teaches only below two held primaries;
    // non-first-rank rows (ranks, recipes) always pass.
    CHECK(FirstRankPrimaryTeachableNow(true, 0));
    CHECK(FirstRankPrimaryTeachableNow(true, 1));
    CHECK(!FirstRankPrimaryTeachableNow(true, 2));
    CHECK(!FirstRankPrimaryTeachableNow(true, 3));
    CHECK(FirstRankPrimaryTeachableNow(false, 0));
    CHECK(FirstRankPrimaryTeachableNow(false, 2));

    std::cout << "trade trainer policy: OK\n";
    return 0;
}
