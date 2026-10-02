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
// Tailoring / Enchanting note: 0 holders is not a weighting bug. No pair in
// the factory table includes them - both are crafts with no gathering skill
// that feeds them, and the table invariant bans craft+craft pairs (a craft
// without its gathering can never be skilled or fed). Adding them would need
// an owner decision to relax that invariant, so the table is untouched.

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

} // namespace TortoiseBots
