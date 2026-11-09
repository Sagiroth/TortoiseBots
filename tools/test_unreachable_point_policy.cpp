#include "../ai/playerbot/UnreachablePointPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::DeathAvoidSpot;
using ai::IsDeathPositionAvoided;
using ai::kUnreachableNotedMs;
using ai::kUnreachableRefusedMs;
using ai::kUnreachablePointSlots;
using ai::NoteUnreachablePoint;
using ai::PurposeRemembersUnreachablePoints;

int main()
{
    std::cout << "Starting TortoiseBots unreachable point policy tests...\n";

    // Only the many-point purposes take it: fishing and generic rpg.
    CHECK(PurposeRemembersUnreachablePoints(true, false));
    CHECK(PurposeRemembersUnreachablePoints(false, true));
    CHECK(!PurposeRemembersUnreachablePoints(false, false));

    std::uint32_t const NOW = 1'000'000;
    std::uint32_t const MAP = 0;

    {
        // One failure only notes the point; the second refuses it and the
        // points around it, while a point 40 yd away stays open.
        DeathAvoidSpot noted[kUnreachablePointSlots] = {};
        DeathAvoidSpot refused[kUnreachablePointSlots] = {};
        CHECK(!NoteUnreachablePoint(noted, refused, MAP, -5189.0f, 36.0f, NOW));
        CHECK(!IsDeathPositionAvoided(MAP, -5189.0f, 36.0f, NOW, refused, kUnreachablePointSlots));
        CHECK(NoteUnreachablePoint(noted, refused, MAP, -5185.0f, 40.0f, NOW + 1000));
        CHECK(IsDeathPositionAvoided(MAP, -5189.0f, 36.0f, NOW + 2000, refused, kUnreachablePointSlots));
        CHECK(IsDeathPositionAvoided(MAP, -5180.0f, 30.0f, NOW + 2000, refused, kUnreachablePointSlots));
        CHECK(!IsDeathPositionAvoided(MAP, -5149.0f, 36.0f, NOW + 2000, refused, kUnreachablePointSlots));
        CHECK(!IsDeathPositionAvoided(MAP + 1, -5189.0f, 36.0f, NOW + 2000, refused, kUnreachablePointSlots));
    }

    {
        // A failure noted long ago does not pair with a new one.
        DeathAvoidSpot noted[kUnreachablePointSlots] = {};
        DeathAvoidSpot refused[kUnreachablePointSlots] = {};
        CHECK(!NoteUnreachablePoint(noted, refused, MAP, -5189.0f, 36.0f, NOW));
        CHECK(!NoteUnreachablePoint(noted, refused, MAP, -5189.0f, 36.0f, NOW + kUnreachableNotedMs + 1000));
        CHECK(!IsDeathPositionAvoided(MAP, -5189.0f, 36.0f, NOW + kUnreachableNotedMs + 2000, refused, kUnreachablePointSlots));
    }

    {
        // The refusal lapses after its window, and a refused point stays
        // refused when noted again.
        DeathAvoidSpot noted[kUnreachablePointSlots] = {};
        DeathAvoidSpot refused[kUnreachablePointSlots] = {};
        NoteUnreachablePoint(noted, refused, MAP, -5189.0f, 36.0f, NOW);
        CHECK(NoteUnreachablePoint(noted, refused, MAP, -5189.0f, 36.0f, NOW + 1000));
        CHECK(NoteUnreachablePoint(noted, refused, MAP, -5189.0f, 36.0f, NOW + 5000));
        CHECK(IsDeathPositionAvoided(MAP, -5189.0f, 36.0f, NOW + kUnreachableRefusedMs - 1000, refused, kUnreachablePointSlots));
        CHECK(!IsDeathPositionAvoided(MAP, -5189.0f, 36.0f, NOW + kUnreachableRefusedMs + 10000, refused, kUnreachablePointSlots));
    }

    std::cout << "All unreachable point policy tests passed.\n";
    return 0;
}
