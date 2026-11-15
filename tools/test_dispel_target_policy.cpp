#include "../ai/playerbot/policy/heal/DispelTargetPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::DispelClaimKey;
using ai::DispelReachCap;
using ai::DispelSpellEffectInput;
using ai::DispelSpellInput;
using ai::DispelTargetTier;
using ai::IsNeverDispelSpellId;
using ai::IsSpreadingAuraSpell;
using ai::IsWithinDispelReach;
using ai::kDispelTargetRejected;

int main()
{
    std::cout << "Starting TortoiseBots dispel-target policy tests...\n";

    float const spell = 26.0f;

    // Reach cap: 1.5 x spell distance, never the 60yd sight distance.
    CHECK(DispelReachCap(spell) == 39.0f);
    CHECK(IsWithinDispelReach(39.0f, spell));
    CHECK(!IsWithinDispelReach(39.1f, spell));
    CHECK(!IsWithinDispelReach(60.0f, spell));
    std::cout << "  [PASS] reach cap is 1.5 x spell distance\n";

    // Out of sight or past the cap is never a target.
    CHECK(DispelTargetTier(10.0f, spell, false, false) == kDispelTargetRejected);
    CHECK(DispelTargetTier(45.0f, spell, true, false) == kDispelTargetRejected);
    std::cout << "  [PASS] no line of sight / too far is rejected\n";

    // In range beats a walk; unclaimed beats claimed at the same reach.
    int const inRangeFree = DispelTargetTier(20.0f, spell, true, false);
    int const inRangeClaimed = DispelTargetTier(20.0f, spell, true, true);
    int const walkFree = DispelTargetTier(30.0f, spell, true, false);
    int const walkClaimed = DispelTargetTier(30.0f, spell, true, true);
    CHECK(inRangeFree == 0);
    CHECK(inRangeFree < inRangeClaimed);
    CHECK(inRangeClaimed < walkFree);
    CHECK(walkFree < walkClaimed);
    CHECK(DispelTargetTier(spell, spell, true, false) == 0);
    std::cout << "  [PASS] in range, then unclaimed, decide the tier\n";

    // A claim lowers the rank but never rejects: someone still cures it.
    CHECK(walkClaimed != kDispelTargetRejected);
    std::cout << "  [PASS] a claimed member is still a fallback\n";

    // One key per dispel type: decursers share it, other types do not.
    CHECK(DispelClaimKey("2") == DispelClaimKey("2"));
    CHECK(DispelClaimKey("1") != DispelClaimKey("2"));
    CHECK(DispelClaimKey("2") != "2");
    std::cout << "  [PASS] claim key is per dispel type\n";

    // Priority buckets: spreading (player or pet) < non-spreading player < non-spreading pet.
    int const spreadingPlayer = DispelTargetTier(20.0f, spell, true, false, true, false);
    int const spreadingPetWalkClaimed = DispelTargetTier(30.0f, spell, true, true, true, true);
    int const playerInRange = DispelTargetTier(20.0f, spell, true, false, false, false);
    int const playerWalkClaimed = DispelTargetTier(30.0f, spell, true, true, false, false);
    int const petInRange = DispelTargetTier(20.0f, spell, true, false, false, true);
    int const petWalkClaimed = DispelTargetTier(30.0f, spell, true, true, false, true);
    CHECK(spreadingPlayer == 0);
    CHECK(spreadingPlayer < spreadingPetWalkClaimed);
    CHECK(spreadingPetWalkClaimed < playerInRange);
    CHECK(playerInRange < playerWalkClaimed);
    CHECK(playerWalkClaimed < petInRange);
    CHECK(petInRange < petWalkClaimed);
    CHECK(DispelTargetTier(10.0f, spell, false, false, true, false) == kDispelTargetRejected);
    std::cout << "  [PASS] spreading beats players, players beat pets\n";

    // Spreading detection: 45997 Fungal Spores (self-replicating AoE) is spreading.
    auto noLookup = [](std::uint32_t, DispelSpellInput&) { return false; };
    DispelSpellInput fungalSpores;
    fungalSpores.spellId = 45997;
    fungalSpores.dispelType = 3; // Disease
    fungalSpores.isPositive = false;
    fungalSpores.effects.push_back({6, 3, 0, 22, 15, 13});     // Periodic damage
    fungalSpores.effects.push_back({6, 23, 45997, 22, 15, 13}); // Periodic trigger self
    CHECK(IsSpreadingAuraSpell(fungalSpores, noLookup));
    std::cout << "  [PASS] 45997 Fungal Spores detected as spreading\n";

    // 1010 Curse of Idiocy (self-triggering single target, no AoE/radius) is not spreading.
    DispelSpellInput curseOfIdiocy;
    curseOfIdiocy.spellId = 1010;
    curseOfIdiocy.dispelType = 2; // Curse
    curseOfIdiocy.isPositive = false;
    curseOfIdiocy.effects.push_back({6, 23, 1010, 6, 0, 0});
    CHECK(!IsSpreadingAuraSpell(curseOfIdiocy, noLookup));
    std::cout << "  [PASS] 1010 Curse of Idiocy is not spreading\n";

    // 28169 Mutating Injection is never-dispel and must not count as spreading.
    CHECK(IsNeverDispelSpellId(28169));
    DispelSpellInput mutatingInjection;
    mutatingInjection.spellId = 28169;
    mutatingInjection.dispelType = 3;
    mutatingInjection.isPositive = false;
    mutatingInjection.effects.push_back({6, 23, 28169, 22, 15, 13});
    CHECK(!IsSpreadingAuraSpell(mutatingInjection, noLookup));
    std::cout << "  [PASS] 28169 Mutating Injection is never-dispel and not spreading\n";

    // Non-self-triggering periodic/proc spreaders via triggered spell lookup.
    DispelSpellInput wanderingPlague;
    wanderingPlague.spellId = 3436;
    wanderingPlague.dispelType = 3;
    wanderingPlague.isPositive = false;
    wanderingPlague.effects.push_back({6, 42, 3439, 1, 0, 0}); // Proc trigger 3439
    auto lookup3439 = [](std::uint32_t trigId, DispelSpellInput& out) -> bool {
        if (trigId != 3439)
            return false;
        out.spellId = 3439;
        out.effects.push_back({6, 3, 0, 4, 0, 0}); // TARGET_UNIT_NEAR_CASTER
        return true;
    };
    CHECK(IsSpreadingAuraSpell(wanderingPlague, lookup3439));
    std::cout << "  [PASS] proc-triggering AoE/near-caster spell detected as spreading\n";

    std::cout << "All dispel-target policy tests passed.\n";
    return 0;
}
