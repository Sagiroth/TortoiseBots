#pragma once

// Pure policy for issue #388. A bot with a real player master bypasses the
// activity throttling that quiets instance bots (PlayerbotAI::GetPriorityType
// returns HAS_REAL_PLAYER_MASTER before the instance branch), so the travel
// strategy it was granted while standing in the open world keeps running its
// destination search after it zones in: a ~39-bot hired raid in Naxxramas
// measured single bot-update passes up to 25.7 s, all of it the deferred search
// ChooseTravelTargetAction runs on the world tick while
// AiPlayerbot.AsyncTravelPartitions is off. Travel destinations are overworld
// places, so there is nothing to select inside a dungeon or raid: the travel
// actions stand down until the bot leaves, which also means the guard needs no
// zone-in/zone-out bookkeeping. The world-facing part (master type, current
// map) lives in the travel actions; the decision below is pure so it can be
// tested on its own.

namespace ai
{
    inline bool TravelSelectionBlockedByInstance(bool hasRealPlayerMaster, bool inInstanceMap)
    {
        return hasRealPlayerMaster && inInstanceMap;
    }
}
