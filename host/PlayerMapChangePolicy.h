#pragma once

// The map-change sweep (host/BotPlayerAdapter::OnMapChanged): a real player
// who leaves a dungeon brings the owned bots that are still inside back to
// him. Which bots those are is plain data here, so the polarity stays pinned
// by a standalone test; the adapter supplies the live map reads.

namespace TortoiseBots
{
namespace PlayerMapChange
{

// True for a bot still inside a dungeon, the only bots the sweep summons out.
// False for a bot between maps (no map to read) and for a bot already outside
// with its master: summoning those would pull a bot out of the world it stands
// in, the reverse of the sweep's job.
inline bool BotLeftInDungeon(bool hasMap, bool mapIsDungeon)
{
    return hasMap && mapIsDungeon;
}

} // namespace PlayerMapChange
} // namespace TortoiseBots
