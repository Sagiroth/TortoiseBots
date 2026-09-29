#pragma once

// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectGuid.h"

#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

class Player;
class PlayerbotAI;

namespace TortoiseBots
{

// Companion choice as gathered from gossip or the `.bot hire` command.
// role uses the ai::BOT_ROLE_* bits (tank 0x01, healer 0x02, dps 0x04).
// specIndex is the gossip spec-menu position (SpecsFor order); -1 when the
// request came from `.bot hire` without a spec word (role only).
struct HireSelection
{
    uint8 classId = 0;
    uint8 race = 0;
    uint8 gender = 0;
    uint8 role = 0;
    int specIndex = -1;
};

enum class HireStatus
{
    Ok,
    Disabled,
    NoPermission,
    RecruiterRequired,
    InvalidChoice,
    GroupFull,
    CapReached,
    Poor,
    NoCandidate,
    Failed
};

struct HireOutcome
{
    HireStatus status = HireStatus::Failed;
    std::string message;
    std::string botName;
    uint32_t cost = 0;
};

// Issue #192: on-demand companion hiring. Creates a character on an RNDBOT
// account matching the requested class/race/gender, levels it to the
// requester, provisions talents/spells/skills/gear for the requested role,
// and invites it to the requester's group. Companion characters are temporary:
// the hire ledger records each created character and HireLifecycle deletes it
// when the hire ends. Character creation uses the generic CharacterCreation
// seam on the world thread; login goes through the normal Headless queue owned
// by BotManager. Provisioning itself is deferred to Update() because the
// character only exists as a live Player once the Headless session finishes
// logging in.
class HireProvisionService
{
public:
    static HireProvisionService& Instance();

    // fromGossip skips the resting check: standing at the recruiter inside the
    // inn is proof of presence. The `.bot hire` fast path enforces it.
    HireOutcome Hire(Player* requester, HireSelection const& sel, bool fromGossip);

    // Completes queued provisions once the hired character is controllable,
    // then groups it with its master. Bounded: at most two provisions per tick.
    void Update(uint32_t diff);

    // Native invite + immediate mature accept. Shared by provisioning and by
    // the grace-period rejoin so both paths group identically.
    bool EnsureGrouped(Player* master, Player* bot);

    // Live hired companions owned by master (grace-period resilient).
    uint32_t CountHired(Player* master) const;

    static bool ClassCanRole(uint8 classId, uint8 role);
    static uint8 DefaultRoleForClass(uint8 classId);

    struct PendingProvision
    {
        ObjectGuid botGuid;
        ObjectGuid masterGuid;
        uint32_t masterAccountId = 0;
        uint8 targetLevel = 1;
        uint8 role = 0;
        int specIndex = -1;
        time_t queuedAt = 0;
        // Issue #281: heavy provisioning (level/talents/spells/gear/SaveToDB)
        // runs exactly once per hire; retries only redo teleport/grouping.
        bool provisioned = false;
        // Reunite attempts so far (invite failures + teleport waits).
        uint32_t reuniteAttempts = 0;
    };
    // Every hire creates its own character on a managed pool account; the
    // character is deleted when the hire ends.
    bool CreateCandidate(HireSelection const& sel, uint32_t requesterTeam, uint32_t& accountId, ObjectGuid& guid);
    bool ProvisionNow(Player* bot, PendingProvision& pending);
    // Issue #281: heavy one-shot work (level/talents/spells/gear/SaveToDB).
    void ProvisionHeavy(Player* bot, PendingProvision const& pending, PlayerbotAI* ai, Player* master);
    // Issue #281: teleport + grouping only; safe to retry every tick.
    bool Reunite(Player* bot, Player* master);
    void DropStalePending();

    std::vector<PendingProvision> m_pending;
    uint32_t m_updateElapsedMs = 0;
};

} // namespace TortoiseBots
