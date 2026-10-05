#pragma once

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <set>
#include <string>

// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectGuid.h"
#include "RandomBotAccountRegistry.h"

namespace TortoiseBots
{

class RandomBotService
{
public:
    static RandomBotService& Instance();

    // Load the configured random-account character pool once. With
    // AiPlayerbot.RandomBotAutoCreate=1 the service also creates the bounded
    // deficit toward the configured target via AccountMgr/CharacterCreation
    // on the world thread (core PR #416). BotManager
    // remains the sole Headless-session owner.
    void Initialize();
    void Update(uint32_t diff);
    void Shutdown();

    void OnHumanLogin();
    void OnHumanLogout();

    // False while the startup pool reset is draining, deleting, verifying or
    // has failed. Every pool consumer (autologin, auto-create, pinned
    // resolution, hiring, battleground selection) stays paused while it is
    // false, so nothing races the deletion.
    bool IsPoolAvailable() const;

private:
    struct Candidate
    {
        uint32_t accountId = 0;
        ObjectGuid characterGuid;
        uint8_t level = 1;      // from the characters table at load, refreshed while online
        uint32_t team = 0;      // Team enum from the race (ALLIANCE / HORDE)
    };

    RandomBotService() = default;
    ~RandomBotService() = default;

    void LoadCandidates();
    // Startup pool reset (issue #265): planned in Initialize, executed one
    // bounded step per world tick, and followed by a candidate reload.
    void DrivePoolReset(uint32_t diff);
    // Register a module-created account before any character is created on it.
    bool RegisterPoolAccount(uint32_t accountId, std::string const& username, RegistrationSource source);
    // Startup diagnostic: prefix-matching accounts that are not managed.
    void WarnAboutUnregisteredPrefixAccounts();
    void MaintainOnlinePool();
    // Level ladder (AiPlayerbot.LevelLadder), see MaintainOnlinePool.
    uint32_t LadderBandCount() const;
    uint32_t LadderBandOf(uint32_t level) const;
    uint32_t LadderBandTarget(uint32_t band) const;
    std::string LadderBandName(uint32_t band) const;
    // Why one bucket build left a candidate out of the pool; the pass log prints these, so
    // a pool that stays short can be told apart from a pool that is merely held back.
    struct LadderSkips
    {
        uint32_t held = 0;      // held by a legitimate hold (busy retry / quick-logout quarantine)
        uint32_t staleHold = 0; // held by a hold beyond any legitimate window - repaired and picked
        uint32_t tracked = 0;   // a BotManager record outlived the logout (never picked)
    };
    void LadderBuildBuckets(std::vector<std::vector<size_t>>& buckets, uint32_t nowMs, LadderSkips& skips);
    int LadderPickFromBuckets(std::vector<std::vector<size_t>>& buckets, std::vector<uint32_t> const& onlinePerBand, uint32_t onlineAlliance, uint32_t onlineHorde) const;
    void LadderLog(uint32_t diff);
    void RemoveExpiredBots(uint32_t diff);
    uint32_t TargetCount() const;
    uint32_t DesiredTargetCount() const;
    bool TryAutoCreate();
    enum class AutoCreateCharResult { Success, TransientName, TransientError, Permanent };
    AutoCreateCharResult TryCreateCharacterOnAccount(uint32_t accountId, std::vector<std::pair<uint8_t, uint8_t>> const& validForAccount);
    // Returns TEAM_NONE if empty/unknown, otherwise HORDE/ALLIANCE (67/469). Sets isMixed
    // when cached candidates contain both factions (must be excluded).
    uint32_t GetAccountAllowedTeam(uint32_t accountId, bool& isMixed) const;
    void ResolvePinnedBots();
    bool IsPinnedGuid(uint32 guidLow) const { return m_pinnedGuids.find(guidLow) != m_pinnedGuids.end(); }

    std::vector<Candidate> m_candidates;
    // Even-start-zone cache: per-zone level-1 pool counts for the
    // least-populated-zone race pick. Refreshed once per creation batch (see
    // StartZoneCountsForBatch), so up to 5 creations per cadence share one
    // bounded SELECT instead of re-counting per character.
    uint32_t m_startZoneCounts[6] = {};
    uint32_t m_startZoneBatch = 0; // creation-batch id the counts were built for
    void StartZoneCountsForBatch(uint32_t batch);
    std::vector<uint32_t> m_ageMs;
    std::vector<uint32_t> m_strategyAgeMs;
    std::vector<uint32_t> m_randomizeAgeMs;
    size_t m_nextCandidate = 0;
    uint32_t m_ladderLogMs = 0;
    uint32_t m_ladderPassLogMs = 0;
    std::vector<uint32_t> m_ladderRetryMs; // per candidate: not picked again before this time (failed / busy)
    uint32_t m_quickLogouts = 0;           // bots that left within a minute of logging in (diagnostic)
    std::vector<uint8_t> m_wasBot;         // per candidate: was a random bot at the last service interval
    // Stall watchdog: last seen level/XP per online pool bot and how long it
    // has not changed. A bot that earns no XP for RandomBotStallRelogMinutes
    // is relogged, the one thing measured to unstick it.
    struct XpProgress { uint32_t level = 0; uint32_t xp = 0; uint32_t stallMs = 0; };
    std::unordered_map<uint32_t, XpProgress> m_xpProgress;
    uint32_t m_quickLogoutLogMs = 0;
    uint32_t m_serviceElapsedMs = 0;
    // Stable target: snapshot of DesiredTargetCount once at Initialize when
    // auto-create is enabled (no per-cadence re-roll/ratchet toward Max). For
    // non-auto, snapshot of TargetCount (capped). Handles bounds and deficit
    // via size check in TryAutoCreate.
    uint32_t m_targetCount = 0;
    uint32_t m_humanSessions = 0;
    bool m_initialized = false;
    bool m_started = false;
    std::set<uint32> m_pinnedGuids;
    bool m_pinnedResolved = false;
    // Idempotent creation: known RNDBOT account ids (from LoadCandidates and
    // auto-created). No per-tick LIKE scan; one bounded DB COUNT per creation
    // happens inside CharacterCreation validation.
    std::vector<uint32_t> m_rndBotAccountIds;
    // Process-lifetime auto-create failure state: permanently failed accounts
    // (mixed, limit, or other materialization errors) are logged once and
    // never retried. Transient name collisions (NAME_IN_USE/RESERVED/PROFANE)
    // are not recorded here and remain retryable; CHAR_CREATE_DISABLED and
    // CHAR_CREATE_PVP_TEAMS_VIOLATION are treated as transient 60s backoff
    // (dynamic creation-disabled/faction-balance, not permanent) via
    // m_charCreateErrorNextRetry to avoid poisoning a healthy account.
    // Reset only on Initialize.
    std::set<uint32_t> m_failedAutoCreateAccounts;
    // After a fresh-account permanent character-creation failure, stop
    // allocating additional empty RNDBOT accounts for this process (log once);
    // existing accounts remain eligible.
    bool m_freshAutoCreateDisabled = false;
    // Process-lifetime disable when no valid DBC/PlayerInfo race/class remains
    // (missing CharRaces/CharClasses or playercreateinfo) – log once.
    bool m_autoCreateNoValidData = false;
    // Bounded retry backoff for allocation/creation transient failures.
    // Throttled to at most one error line per interval; retry after expiry.
    time_t m_accountAllocNextRetry = 0;
    time_t m_charCreateErrorNextRetry = 0;
    // Minimal pending-account state for AccountMgr::CreateAccount async
    // login-DB INSERT visibility (LoginDatabase async after AllowAsyncTransactions, separate from core PR #416): after
    // AOR_OK but GetId still 0, remember exactly one pending fresh account
    // name, retry that same name with bounded/log-throttled cadence while
    // continuing the existing-account selection path and without allocating
    // another fresh account; log once after prolonged unresolved period.
    // Cleared once the id is visible, then one character creation is attempted.
    // Startup pool reset state (issue #265). Deletion details stay inside
    // RandomBotPoolReset; this service only drives it and reloads afterwards.
    // Registry validity is always read from the registry itself.
    std::string m_pendingAccountName;
    time_t m_pendingNextRetry = 0;
    time_t m_pendingSince = 0;
    bool m_pendingStaleLogged = false;
};

} // namespace TortoiseBots
