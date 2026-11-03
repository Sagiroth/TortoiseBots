#pragma once

#include <cstdint>
#include <ctime>
#include <unordered_map>
#include "ObjectGuid.h"
#include "SummonRestorePolicy.h"

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

    // Optional summon condition set (issue #473, donor summon-condition
    // knobs). Plain data: the accept hook fills it from config, the policy
    // helper decides, RequestSummon executes unchanged.
    struct SummonConditions
    {
        bool allowInCombat = false;
        bool allowMasterDead = false;
        bool allowBotDead = false;
        bool revive = false;
        bool repair = false;
        // Seconds between group summons per bot (0 = no cooldown). The accept
        // hook passes the configured value; RequestGroupSummon enforces it.
        uint32 cooldown = 0;
    };

    // requester is the bot's master (the durable master binding must match).
    // anchor, when given, is where the bot is summoned to instead of the
    // master: the remote commander (issue #884) summons a bot to the lead
    // bot of its group while the commander itself is elsewhere. The anchor
    // then also carries the portal and the map/alive/teleport checks the
    // master carries otherwise; the master check and the follow restore on
    // arrival stay the master's.
    bool RequestSummon(Player* requester, Player* bot, Player* anchor = nullptr);
    // Conditional entry: applies the donor condition knobs before (and
    // after) the native RequestSummon preconditions. Revive runs first when
    // both the dead-bot and revive knobs are on; repair runs after arrival.
    bool RequestGroupSummon(Player* requester, Player* bot, SummonConditions const& conditions);
    bool IsBusy(ObjectGuid botGuid) const;
    // How many of this bot's summons were dropped before the teleport (it fought, died, flew or teleported while
    // the portal waited) since the server started. A queued summon is answered "queued" at once; a dungeon run
    // reads a rise of this count as a failed summon (DungeonRun::UpdateSummonOutcomes).
    uint32 SummonCancels(ObjectGuid botGuid) const;
    // An explicit stationary order (`stay`, `guard`, `hold`, `comestay`) for
    // this bot: if a summon is pending, the follow restore on arrival is
    // cancelled (latest explicit order wins). Returns true when a pending
    // summon was affected. No-op otherwise.
    bool NoteHoldOrder(ObjectGuid botGuid);
    // An explicit `come` order: the latest order again means "come, then
    // follow", so it clears a hold noted earlier while the summon was pending.
    bool NoteComeOrder(ObjectGuid botGuid);
    void Update(uint32 diff);

private:
    PlayerConvenience() = default;

    struct SummonState
    {
        ObjectGuid botGuid;
        ObjectGuid masterGuid;
        // Empty: the master is the summon point (the normal case).
        ObjectGuid anchorGuid;
        float destX = 0.0f;
        float destY = 0.0f;
        float destZ = 0.0f;
        float destO = 0.0f;
        uint32 destMap = 0;
        uint32 elapsedMs = 0;
        ObjectGuid portalGuid;
        SummonRestorePolicy restore;

        enum class Phase { Delaying, AwaitingArrival } phase = Phase::Delaying;
    };

    void UpdateSummons(uint32 diff);

    std::unordered_map<uint32, SummonState> m_summons;
    // Last successful group-accept summon per bot (character counter ->
    // time). Bounds uninvite/invite macro abuse; entries are tiny and only
    // created for bots that actually group-summoned.
    std::unordered_map<uint32, time_t> m_groupSummonAt;
    // Summons dropped before the teleport, per bot (character counter -> count): see SummonCancels.
    std::unordered_map<uint32, uint32> m_summonCancels;
};

} // namespace TortoiseBots
