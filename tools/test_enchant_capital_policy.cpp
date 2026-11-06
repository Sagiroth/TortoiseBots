// Standalone policy test for task W8: the free capital teleport rules.
// Pure header, no core types:
//   g++ -std=c++17 -Wall -Wextra tools/test_enchant_capital_policy.cpp -o /tmp/test_enchant_capital_policy && /tmp/test_enchant_capital_policy

#include "../runtime/policy/guild/EnchantCapitalPolicy.h"

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

using TortoiseBots::EnchantCapital::CanTeleportNow;
using TortoiseBots::EnchantCapital::CapitalMapId;
using TortoiseBots::EnchantCapital::CapitalZoneId;
using TortoiseBots::EnchantCapital::IsInCapital;
using TortoiseBots::EnchantCapital::TeleportWait;
using TortoiseBots::EnchantCapital::WaitToken;

int main()
{
    // Team -> capital mapping: Alliance Stormwind, Horde Orgrimmar.
    CHECK(CapitalZoneId(true) == 1519);
    CHECK(CapitalZoneId(false) == 1637);
    CHECK(CapitalMapId(true) == 0);
    CHECK(CapitalMapId(false) == 1);
    CHECK(CapitalZoneId(true) != CapitalZoneId(false));

    // In-capital check is per team: the same zone is home for one side only.
    CHECK(IsInCapital(1519, true));
    CHECK(!IsInCapital(1637, true));
    CHECK(IsInCapital(1637, false));
    CHECK(!IsInCapital(1519, false));
    // Outside both capitals: neither side counts.
    CHECK(!IsInCapital(3483, true));
    CHECK(!IsInCapital(3483, false));
    CHECK(!IsInCapital(0, true));

    // All clear: teleport may go.
    CHECK(CanTeleportNow(false, false, false, true, false) == TeleportWait::Ok);
    CHECK(WaitToken(TeleportWait::Ok)[0] == '\0');

    // Each wait reason fires alone.
    CHECK(CanTeleportNow(true, false, false, true, false) == TeleportWait::Combat);
    CHECK(CanTeleportNow(false, true, false, true, false) == TeleportWait::Instance);
    CHECK(CanTeleportNow(false, false, true, true, false) == TeleportWait::Group);
    CHECK(CanTeleportNow(false, false, false, false, false) == TeleportWait::Dead);
    CHECK(CanTeleportNow(false, false, false, true, true) == TeleportWait::Taxi);

    // Tokens the command layer will report.
    CHECK(std::string(WaitToken(TeleportWait::Combat)) == "combat");
    CHECK(std::string(WaitToken(TeleportWait::Instance)) == "instance");
    CHECK(std::string(WaitToken(TeleportWait::Group)) == "group");
    CHECK(std::string(WaitToken(TeleportWait::Dead)) == "dead");
    CHECK(std::string(WaitToken(TeleportWait::Taxi)) == "taxi");

    // Order is the unsafe-first order a stuck bot hits: combat before
    // instance, instance before group, group before dead, dead before taxi.
    CHECK(CanTeleportNow(true, true, true, false, true) == TeleportWait::Combat);
    CHECK(CanTeleportNow(false, true, true, false, true) == TeleportWait::Instance);
    CHECK(CanTeleportNow(false, false, true, false, true) == TeleportWait::Group);
    CHECK(CanTeleportNow(false, false, false, false, true) == TeleportWait::Dead);

    if (failures)
    {
        std::cerr << "enchant-capital policy: " << failures << " FAILURES\n";
        return 1;
    }
    std::cout << "enchant-capital policy: OK\n";
    return 0;
}
