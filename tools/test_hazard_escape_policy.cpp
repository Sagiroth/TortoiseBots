#include "../ai/playerbot/HazardEscapePolicy.h"

#include <cstdio>

using namespace ai;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main()
{
    // Inside A (distance 3, radius 4); B is nearer (1) but its radius 0.5 does not reach the bot: flee A.
    CHECK(SelectEscapeHazard({ { 3.0f, 4.0f }, { 1.0f, 0.5f } }) == 0);
    // Same hazards, list order reversed: still A.
    CHECK(SelectEscapeHazard({ { 1.0f, 0.5f }, { 3.0f, 4.0f } }) == 1);
    // Both contain the bot: the nearer centre.
    CHECK(SelectEscapeHazard({ { 3.0f, 4.0f }, { 1.0f, 2.0f } }) == 1);
    // Exactly on the edge counts as inside.
    CHECK(SelectEscapeHazard({ { 4.0f, 4.0f } }) == 0);
    // None contains the bot, or no hazards: nothing to flee.
    CHECK(SelectEscapeHazard({ { 5.0f, 4.0f }, { 1.0f, 0.5f } }) == kNoEscapeHazard);
    CHECK(SelectEscapeHazard({}) == kNoEscapeHazard);

    if (failures)
        return 1;
    std::printf("hazard escape policy: ok\n");
    return 0;
}
