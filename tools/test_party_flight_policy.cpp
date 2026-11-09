#include "../ai/playerbot/PartyFlightPolicy.h"

#include <cstdint>
#include <iostream>

namespace
{
int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { \
        std::cerr << "FAIL line " << __LINE__ << ": " #cond "\n"; \
        ++failures; \
    } \
} while (0)
} // namespace

int main()
{
    // Eligible: real-player master, alive, not in combat, follow strategy, not pool bot.
    CHECK(ai::PartyFlightEligible(true, true, false, true, false));
    CHECK(!ai::PartyFlightEligible(false, true, false, true, false)); // master not real player
    CHECK(!ai::PartyFlightEligible(true, false, false, true, false)); // bot not alive
    CHECK(!ai::PartyFlightEligible(true, true, true, true, false));   // bot in combat
    CHECK(!ai::PartyFlightEligible(true, true, false, false, false)); // no follow strategy (e.g. stay/guard)
    CHECK(!ai::PartyFlightEligible(true, true, false, true, true));   // pool bot

    // PartyFlightAtMaster: same map and within 30 yd.
    CHECK(ai::PartyFlightAtMaster(false, 0.0f));
    CHECK(ai::PartyFlightAtMaster(false, 15.0f));
    CHECK(ai::PartyFlightAtMaster(false, ai::kPartyFlightAtMasterYd));
    CHECK(!ai::PartyFlightAtMaster(false, ai::kPartyFlightAtMasterYd + 0.1f));
    CHECK(!ai::PartyFlightAtMaster(true, 10.0f)); // cross map

    // PartyFlightBoardExpired: >= 60000 ms.
    CHECK(!ai::PartyFlightBoardExpired(0));
    CHECK(!ai::PartyFlightBoardExpired(ai::kPartyFlightBoardWindowMs - 1));
    CHECK(ai::PartyFlightBoardExpired(ai::kPartyFlightBoardWindowMs));
    CHECK(ai::PartyFlightBoardExpired(ai::kPartyFlightBoardWindowMs + 1000));

    // PartyFlightEpisodeExpired: >= 15 minutes.
    CHECK(!ai::PartyFlightEpisodeExpired(0));
    CHECK(!ai::PartyFlightEpisodeExpired(ai::kPartyFlightEpisodeMaxMs - 1));
    CHECK(ai::PartyFlightEpisodeExpired(ai::kPartyFlightEpisodeMaxMs));
    CHECK(ai::PartyFlightEpisodeExpired(ai::kPartyFlightEpisodeMaxMs + 1000));

    // PartyFlightReached: within interaction distance (5.0 yd).
    CHECK(ai::PartyFlightReached(0.0f));
    CHECK(ai::PartyFlightReached(3.0f));
    CHECK(ai::PartyFlightReached(ai::kPartyFlightReachYd));
    CHECK(!ai::PartyFlightReached(ai::kPartyFlightReachYd + 0.1f));

    if (failures != 0)
    {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "test_party_flight_policy: all checks passed\n";
    return 0;
}
