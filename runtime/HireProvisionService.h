#pragma once

// pi-lens-ignore: clang:pp_file_not_found
#include "HireRaidPolicy.h"
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
// request came from `.bot hire` without a spec word (role only). gearTier is
// the chosen equipment tier (0 = today's random gear); validated against the
// unlock at hire time, ignored below level 60.
struct HireSelection
{
    uint8 classId = 0;
    uint8 race = 0;
    uint8 gender = 0;
    uint8 role = 0;
    int specIndex = -1;
    uint8 gearTier = 0;
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

// Issue #900: result of `.bot hire raid`. refused = nothing was hired because
// a pre-check failed (message says why). Otherwise hired of planned companions
// joined; message is empty on a full hire or names why the sequence stopped.
// requested is the player's total, planned the part the cap leaves room for.
struct HireRaidOutcome
{
    bool refused = false;
    // Plan accepted and queued: the hires run over the next ticks and the
    // player gets a closing summary plus the TBM line when the job ends.
    bool started = false;
    bool capped = false;
    // The party was converted to a raid when the job was accepted.
    bool convertedToRaid = false;
    HireStatus stopStatus = HireStatus::Ok;
    std::string message;
    uint32_t requested = 0;
    uint32_t planned = 0;
    uint32_t hired = 0;
    uint32_t tanks = 0;
    uint32_t healers = 0;
    uint32_t dps = 0;
    // Role split of the plan (hired splits above fill in as the job runs).
    uint32_t planTanks = 0;
    uint32_t planHealers = 0;
    uint32_t planDps = 0;
    uint8_t tier = 0;
    uint64_t quote = 0;
    uint64_t fee = 0;
};

// One accepted `.bot hire raid`, worked off a couple of Hire() calls at a time
// by HireProvisionService::Update so a 39-companion raid never stalls the
// world thread inside the chat command.
struct HireRaidJob
{
    ObjectGuid masterGuid;
    std::vector<HireRaidSlot> roster;
    size_t cursor = 0;
    HireRaidOutcome progress;
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

    // Issue #900: hire the whole raid described by request (see
    // runtime/HireRaidPolicy.h) through Hire(), one slot after the other.
    // Plans, quotes and refuses synchronously; on success the job is queued
    // (outcome.started) and Update() hires it a couple of slots per step.
    HireRaidOutcome HireRaid(Player* requester, HireRaidRequest const& request);

    // The gates every hire passes before it picks a class: in-game, enabled,
    // pool available, account security, recruiter/HireAnywhere. False fills
    // outcome with the status and message.
    static bool CheckHireAllowed(Player* requester, bool fromGossip, HireOutcome& outcome);
    // Fee for the hire that follows ownedBefore live/mustering companions, at
    // the level and effective gear tier (one formula for Hire and HireRaid).
    static uint32_t QuoteHireFee(uint32_t ownedBefore, uint32_t level, uint8_t gearTier);
    // Fills a missing race (random race of the requester's faction that can be
    // the class) and gender. False when no race of the faction fits the class.
    static bool FillDefaultRaceGender(Player* requester, HireSelection& sel);

    // Completes queued provisions once the hired character is controllable,
    // then groups it with its master. Bounded: at most two provisions per tick.
    void Update(uint32_t diff);

    // Native invite + immediate mature accept. Shared by provisioning and by
    // the grace-period rejoin so both paths group identically.
    bool EnsureGrouped(Player* master, Player* bot);

    // Live hired companions owned by master (grace-period resilient).
    uint32_t CountHired(Player* master) const;

    // Issue #490 PR 2: highest gear tier the player may hire at (start tier
    // or highest rewarded unlock quest). One small lookup per page/command.
    static uint8_t HighestUnlockedHireTierFor(Player* player);

    static bool ClassCanRole(uint8 classId, uint8 role);
    static uint8 DefaultRoleForClass(uint8 classId);

    // Highest known spell of a chain: the spell map holds every rank the
    // bot learned, but the client activates the top one, so the intro names
    // that. Walks the core chain down from the known ranks via
    // GetPrevSpellInChain (same call the factory's rank gate uses).
    static uint32_t HighestKnownRankInChain(Player* bot, uint32_t anyKnownRank);
    // Rank-CHAIN heads for the intro's "key spells" line, per class (see the
    // .cpp for the provenance of every id). Returns 0 past the end.
    static uint32_t IntroWishlist(uint8_t classId, size_t rank);

    struct PendingProvision
    {
        ObjectGuid botGuid;
        ObjectGuid masterGuid;
        uint32_t masterAccountId = 0;
        uint8 targetLevel = 1;
        uint8 role = 0;
        int specIndex = -1;
        // Chosen equipment tier (0 = today's random gear). Set at hire time
        // after the unlock check; the static pass reads it, not the config.
        uint8 gearTier = 0;
        time_t queuedAt = 0;
        // Issue #281: heavy provisioning (level/talents/spells/gear/SaveToDB)
        // runs exactly once per hire; retries only redo teleport/grouping.
        bool provisioned = false;
        // Reunite attempts so far (invite failures + teleport waits).
        uint32_t reuniteAttempts = 0;
        // Spellbook snapshot at the end of the heavy pass (post-deploy
        // diagnosis of what the intro could see) plus the intro-once guard
        // (issue #382).
        std::vector<uint32_t> spellsAtProvision;
        bool introSent = false;
    };
    // Every hire creates its own character on a managed pool account; the
    // character is deleted when the hire ends.
    bool CreateCandidate(HireSelection const& sel, uint32_t requesterTeam, uint32_t& accountId, ObjectGuid& guid);
    bool ProvisionNow(Player* bot, PendingProvision& pending);
    static void SnapshotKnownSpells(Player* bot, std::vector<uint32_t>& out);
    static void DropPendingLevelupEcho(PlayerbotAI* ai);
    static void AnnounceIntro(Player* bot, Player* master, PendingProvision& pending);
    // Grace-path entry: same intro for hires grouped by HireLifecycle::Reunite
    // (master was offline at provision). introSent is the HiredRecord guard.
    static void AnnounceForGraceHire(Player* bot, Player* master, bool& introSent);
    // Issue #490: static best-in-slot pass after ProvisionSpellsAndGear.
    // Equips exactly the cached table rows per slot (item_id 0 = empty
    // slot), then applies each row's enchant_spell directly, bypassing the
    // tier/rep skip. No-op unless the hire is level 60 at tier >= 1 with
    // rows for its class/spec.
    static void ProvisionHireGear(Player* bot, PendingProvision const& pending, PlayerbotAI* ai);
    // Same pass by explicit class/spec key (PR 3 respec): equips the tier's
    // set for the new spec over whatever the hire wears. False when there is
    // no set for the key, so the caller keeps the current kit.
    static bool EquipHireGearSet(Player* bot, PlayerbotAI* ai, uint8_t classId, char const* specKey, uint8_t race, uint8_t tier);
    // Issue #281: heavy one-shot work (level/talents/spells/gear/SaveToDB).
    void ProvisionHeavy(Player* bot, PendingProvision const& pending, PlayerbotAI* ai, Player* master);
    // Issue #281: teleport + grouping only; safe to retry every tick.
    bool Reunite(Player* bot, Player* master);
    void DropStalePending();

    std::vector<PendingProvision> m_pending;
    // Issue #900: queued raid hires, one step every kRaidStepMs.
    std::vector<HireRaidJob> m_raidJobs;
    uint32_t m_raidStepElapsedMs = 0;
    // Account the previous slot of the running raid job landed on: tried
    // first so the job does not rescan every full pool account per hire.
    uint32_t m_raidAccountHint = 0;
    bool m_raidHireActive = false;
    void StepRaidJobs();
    static void ReportRaidJob(Player* master, HireRaidOutcome const& job);
    uint32_t m_updateElapsedMs = 0;
};

} // namespace TortoiseBots
