#pragma once

#include <cstdint>
#include <unordered_map>

#include "ObjectGuid.h"

class Player;
class Unit;

namespace TortoiseBots {

// Player-owned convenience behaviours that need a short-lived world-tick
// transition. This module owns only the behaviour state: BotManager remains
// the sole owner of bot records, Headless lifecycle and durable master binding.
//
// A successful request means the behaviour was accepted and queued. Completion
// is asynchronous and may still fail closed when the bot, target or requester
// changes state before the action finishes.
class PlayerConvenience
{
public:
    static PlayerConvenience& Instance();

    bool RequestSummon(Player* requester, Player* bot);
    bool IsBusy(ObjectGuid botGuid) const;
    void Update(uint32 diff);

private:
    PlayerConvenience() = default;

    struct SummonState
    {
        ObjectGuid botGuid;
        ObjectGuid masterGuid;
        float destX = 0.0f;
        float destY = 0.0f;
        float destZ = 0.0f;
        float destO = 0.0f;
        uint32 destMap = 0;
        uint32 elapsedMs = 0;
        ObjectGuid portalGuid;

        enum class Phase { Delaying, AwaitingArrival } phase = Phase::Delaying;
    };

    void UpdateSummons(uint32 diff);

    std::unordered_map<uint32, SummonState> m_summons;
};

} // namespace TortoiseBots
