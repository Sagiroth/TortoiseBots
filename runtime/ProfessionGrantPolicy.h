#pragma once

#include <cstdint>

// Profession-grant policy (owner decision): every pool bot gets its PRIMARY
// profession pair at level 5 instead of at creation (level 1), together with
// the matching tools. Secondary skills (First Aid, Cooking, Fishing) stay as
// they are - granted at creation at any level.
//
// Pure decision rules over durable facts (level, pool identity, whether the
// character already holds a primary), unit-tested on their own
// (tools/test_profession_grant_policy.cpp). The table that picks the pair
// itself stays in PlayerbotFactory::EnsurePrimaryProfessions; this header
// only owns the WHEN, so the seed path, the level-up hook, the login
// backstop and the trainer-travel gate cannot drift apart again.
//
// Tailoring / Enchanting: the one craft+craft exception to the table
// invariant below (owner decision). No pair in the factory table used to
// include them - both are crafts with no gathering skill that feeds them,
// and the invariant bans craft+craft pairs. Cloth classes (mage, priest,
// warlock) may now roll Tailoring + Enchanting together, but only a share
// of them (TAILOR_ENCHANT_SHARE_DENOMINATOR); the rest keep the gathering
// pairs, and every other class keeps its pairs unchanged.

namespace TortoiseBots
{

// Level from which a pool bot without primaries is granted its pair.
inline constexpr uint32_t PRIMARY_PROFESSION_MIN_LEVEL = 5;

struct ProfessionGrantInputs
{
    uint32_t level = 1;
    bool isPoolBot = false;           // random-pool record (incl. hired companions)
    bool hasPrimaryProfession = false; // character already holds any primary skill
};

enum class ProfessionGrantDecision
{
    LeaveAlone,        // not a pool bot, or already holds a primary: never re-roll
    SecondariesOnly,   // pool bot below the gate: secondaries now, primaries later
    GrantAll,          // pool bot at/above the gate without primaries: grant the pair
};

inline ProfessionGrantDecision DecideProfessionGrant(ProfessionGrantInputs const& inputs)
{
    if (!inputs.isPoolBot || inputs.hasPrimaryProfession)
        return ProfessionGrantDecision::LeaveAlone;
    if (inputs.level < PRIMARY_PROFESSION_MIN_LEVEL)
        return ProfessionGrantDecision::SecondariesOnly;
    return ProfessionGrantDecision::GrantAll;
}

// Trainer-travel parity: rank-1 profession spells become trainable from the
// same level the factory grants the pair (TrainerValues.cpp).
inline bool IsRankOneProfessionTrainable(uint32_t level)
{
    return level >= PRIMARY_PROFESSION_MIN_LEVEL;
}

// Cloth-only Tailoring + Enchanting share (owner decision). Class ids are
// SharedDefines.h values, kept numeric so this header stays core-free
// (same precedent as StarterKitPolicy.h): priest 5, mage 8, warlock 9.
inline constexpr uint32_t TAILOR_ENCHANT_CLASS_PRIEST = 5;
inline constexpr uint32_t TAILOR_ENCHANT_CLASS_MAGE = 8;
inline constexpr uint32_t TAILOR_ENCHANT_CLASS_WARLOCK = 9;

// About 1 in 3 eligible cloth bots takes the pair; the rest roll the
// gathering pairs as before.
inline constexpr uint32_t TAILOR_ENCHANT_SHARE_DENOMINATOR = 3;

inline bool IsTailorEnchantClass(uint32_t classId)
{
    return classId == TAILOR_ENCHANT_CLASS_PRIEST ||
           classId == TAILOR_ENCHANT_CLASS_MAGE ||
           classId == TAILOR_ENCHANT_CLASS_WARLOCK;
}

// roll is the caller-side urand(0, TAILOR_ENCHANT_SHARE_DENOMINATOR - 1);
// only roll 0 takes the pair, so about 1 in 3 eligible bots.
inline bool RollsTailorEnchantPair(uint32_t classId, uint32_t roll)
{
    return IsTailorEnchantClass(classId) && roll == 0;
}
} // namespace TortoiseBots
