// Standalone policy test for AH bot seller account & character rules.
// Build and run:
//   g++ -std=c++17 -Wall -Wextra -pedantic tools/test_ahbot_seller_account_policy.cpp -o /tmp/test_ahbot_seller_account_policy && /tmp/test_ahbot_seller_account_policy

#include "../runtime/policy/ah/AhBotSellerAccountPolicy.h"

#include <cstdint>
#include <iostream>
#include <set>
#include <string>
#include <vector>

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
    // 1. Account name validity and prefix-collision rule.
    CHECK(ai::IsValidSellerAccountName("TBAHBOT", "RNDBOT"));
    CHECK(ai::IsValidSellerAccountName("tbahbot", "RNDBOT"));
    CHECK(ai::IsValidSellerAccountName("  AHSELLER1  ", "RNDBOT"));
    CHECK(ai::IsValidSellerAccountName("ABCDEFGHIJKLMNOP", "RNDBOT")); // 16 chars max
    CHECK(!ai::IsValidSellerAccountName("ABCDEFGHIJKLMNOPQ", "RNDBOT")); // 17 chars: too long
    CHECK(!ai::IsValidSellerAccountName("", "RNDBOT"));
    CHECK(!ai::IsValidSellerAccountName("   ", "RNDBOT"));
    CHECK(!ai::IsValidSellerAccountName("TB AHBOT", "RNDBOT")); // space inside
    CHECK(!ai::IsValidSellerAccountName("RNDBOT", "RNDBOT"));
    CHECK(!ai::IsValidSellerAccountName("RNDBOTAH", "RNDBOT"));
    CHECK(!ai::IsValidSellerAccountName("rndbot_ah", "RNDBOT"));
    CHECK(!ai::IsValidSellerAccountName("RndBot123", "rndbot"));
    // Empty random prefix falls back to RNDBOT.
    CHECK(!ai::IsValidSellerAccountName("RNDBOT99", ""));
    CHECK(ai::IsValidSellerAccountName("TBAHBOT", ""));
    // Custom prefix collision.
    CHECK(!ai::IsValidSellerAccountName("MYBOTAH", "MYBOT"));
    CHECK(ai::IsValidSellerAccountName("TBAHBOT", "MYBOT"));

    CHECK(ai::SellerAccountNameEquals("TBAHBOT", "tbahbot"));
    CHECK(ai::SellerAccountNameEquals("  tbahbot ", "TBAHBOT"));
    CHECK(!ai::SellerAccountNameEquals("TBAHBOT", "TBAHBOT2"));

    // 2. Faction / race picks (Vanilla + Turtle 1.18.1 races).
    {
        std::set<uint8_t> seenAlliance;
        std::set<uint8_t> seenHorde;
        for (uint32_t r = 0; r < 10; ++r)
        {
            uint8_t aRace = ai::PickSellerRace(ai::SellerFaction::Alliance, r);
            uint8_t hRace = ai::PickSellerRace(ai::SellerFaction::Horde, r);
            CHECK(ai::IsAllianceRace(aRace));
            CHECK(!ai::IsHordeRace(aRace));
            CHECK(ai::IsHordeRace(hRace));
            CHECK(!ai::IsAllianceRace(hRace));
            ai::SellerFaction f{};
            CHECK(ai::FactionForSellerRace(aRace, f) && f == ai::SellerFaction::Alliance);
            CHECK(ai::FactionForSellerRace(hRace, f) && f == ai::SellerFaction::Horde);
            seenAlliance.insert(aRace);
            seenHorde.insert(hRace);
        }
        CHECK(seenAlliance == (std::set<uint8_t>{ 1, 3, 4, 7, 10 }));
        CHECK(seenHorde == (std::set<uint8_t>{ 2, 5, 6, 8, 9 }));
        ai::SellerFaction f{};
        CHECK(!ai::FactionForSellerRace(0, f));
        CHECK(!ai::FactionForSellerRace(11, f));
    }

    // Faction string round-trip.
    {
        ai::SellerFaction f{};
        CHECK(ai::ParseSellerFaction("alliance", f) && f == ai::SellerFaction::Alliance);
        CHECK(ai::ParseSellerFaction("HORDE", f) && f == ai::SellerFaction::Horde);
        CHECK(!ai::ParseSellerFaction("neutral", f));
        CHECK(std::string(ai::SellerFactionName(ai::SellerFaction::Alliance)) == "alliance");
        CHECK(std::string(ai::SellerFactionName(ai::SellerFaction::Horde)) == "horde");
    }

    // House seller selection & readiness.
    {
        std::vector<uint32_t> alliance = { 101, 102 };
        std::vector<uint32_t> horde = { 201, 202 };
        CHECK(ai::IsReadyForSellers(true, 1, 1));
        CHECK(!ai::IsReadyForSellers(false, 1, 1));
        CHECK(!ai::IsReadyForSellers(true, 0, 1));
        CHECK(!ai::IsReadyForSellers(true, 1, 0));

        CHECK(ai::PickSellerGuidForHouse(1, alliance, horde, 0) == 101);
        CHECK(ai::PickSellerGuidForHouse(1, alliance, horde, 1) == 102);
        CHECK(ai::PickSellerGuidForHouse(6, alliance, horde, 0) == 201);
        CHECK(ai::PickSellerGuidForHouse(6, alliance, horde, 1) == 202);
        CHECK(ai::PickSellerGuidForHouse(7, alliance, horde, 0) == 101);
        CHECK(ai::PickSellerGuidForHouse(7, alliance, horde, 1) == 102);
        CHECK(ai::PickSellerGuidForHouse(7, alliance, horde, 2) == 201);
        CHECK(ai::PickSellerGuidForHouse(7, alliance, horde, 3) == 202);
        CHECK(ai::PickSellerGuidForHouse(99, alliance, horde, 0) == 0);
        CHECK(ai::PickSellerGuidForHouse(1, {}, horde, 0) == 0);
        CHECK(ai::PickSellerGuidForHouse(6, alliance, {}, 0) == 0);
        CHECK(ai::PickSellerGuidForHouse(7, {}, {}, 0) == 0);
        CHECK(ai::PickSellerGuidForHouse(7, {}, horde, 0) == 201);
    }

    // 3. Name validity.
    CHECK(ai::IsValidSellerCharacterName("Aldric"));
    CHECK(ai::IsValidSellerCharacterName("Ab"));
    CHECK(ai::IsValidSellerCharacterName("Abcdefghijkl")); // 12 chars
    CHECK(!ai::IsValidSellerCharacterName("A"));           // too short
    CHECK(!ai::IsValidSellerCharacterName("Abcdefghijklm")); // 13 chars: too long
    CHECK(!ai::IsValidSellerCharacterName("aldric"));      // lowercase initial
    CHECK(!ai::IsValidSellerCharacterName("ALDRIC"));      // uppercase remainder
    CHECK(!ai::IsValidSellerCharacterName("Aldric1"));     // digit
    CHECK(!ai::IsValidSellerCharacterName("Ald ric"));     // space
    CHECK(!ai::IsValidSellerCharacterName("Ald_ric"));     // punctuation
    CHECK(!ai::IsValidSellerCharacterName(""));

    // 4. Repair decision & interval.
    {
        // Invalid account name: fail closed.
        auto p = ai::DecideSellerRepair(false, false, false, 4, 0, 0, false);
        CHECK(!p.needCreateAccount);
        CHECK(!p.healthy);
        CHECK(p.createAllianceCount == 0 && p.createHordeCount == 0);

        // Missing account (fresh or deleted via .account delete).
        p = ai::DecideSellerRepair(true, false, false, 4, 0, 0, true);
        CHECK(p.needCreateAccount);
        CHECK(p.needCleanupStaleRows);
        CHECK(p.createAllianceCount == 4 && p.createHordeCount == 4);
        CHECK(!p.healthy);

        // Account exists, password not yet rotated this run, missing 1 Alliance and 2 Horde.
        p = ai::DecideSellerRepair(true, true, false, 4, 3, 2, false);
        CHECK(!p.needCreateAccount);
        CHECK(p.needRotatePassword);
        CHECK(!p.needCleanupStaleRows);
        CHECK(p.createAllianceCount == 1);
        CHECK(p.createHordeCount == 2);
        CHECK(!p.healthy);

        // Soft-deleted character left stale row in registry.
        p = ai::DecideSellerRepair(true, true, true, 4, 4, 4, true);
        CHECK(!p.needRotatePassword);
        CHECK(p.needCleanupStaleRows);
        CHECK(p.createAllianceCount == 0 && p.createHordeCount == 0);
        CHECK(!p.healthy);

        // Fully healthy.
        p = ai::DecideSellerRepair(true, true, true, 4, 4, 4, false);
        CHECK(!p.needCreateAccount);
        CHECK(!p.needRotatePassword);
        CHECK(!p.needCleanupStaleRows);
        CHECK(p.createAllianceCount == 0 && p.createHordeCount == 0);
        CHECK(p.healthy);

        // Repair interval checks.
        CHECK(ai::IsSellerRepairDue(false, 0, 300));
        CHECK(!ai::IsSellerRepairDue(true, 0, 300));
        CHECK(!ai::IsSellerRepairDue(true, 299999, 300));
        CHECK(ai::IsSellerRepairDue(true, 300000, 300));
    }

    if (failures)
    {
        std::cerr << "ahbot-seller-account policy: " << failures << " FAILURES\n";
        return 1;
    }
    std::cout << "ahbot-seller-account policy: PASS\n";
    return 0;
}
