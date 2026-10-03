#pragma once

// Pure decision rule for issue #383: AoE fear (priest Psychic Scream, warlock
// Howl of Terror, warrior Intimidating Shout) scatters packs, and a feared
// mob runs into neighbouring packs and pulls them. In a dungeon or raid there
// is always another pack near, so the cast is never safe there; the same
// holds for a bot grouped with a real player in the open world, where the
// pull belongs to the player. Outside, with no master to disrupt, the old
// emergency behaviour stays: a surrounded bot may scream to make space.
//
// The world-facing part (current map, master type) lives at the call site;
// the decision below is pure so it can be tested on its own.

namespace ai
{

inline bool AoeFearAllowed(bool inDungeonOrRaid, bool hasRealPlayerMaster)
{
    if (inDungeonOrRaid)
        return false;
    if (hasRealPlayerMaster)
        return false;
    return true;
}

} // namespace ai
