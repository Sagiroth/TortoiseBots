#pragma once

#include <cstdint>
#include <vector>
#include <set>
#include <string>

// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectGuid.h"

namespace TortoiseBots
{

class RandomBotService
{
public:
    static RandomBotService& Instance();

    // Load the configured random-account character pool once. With
    // AiPlayerbot.RandomBotAutoCreate=1 the service also creates the bounded
    // deficit toward the configured target via AccountMgr/CharacterCreation
    // on the world thread (core PR #412/7084557, final 7084557). BotManager
    // remains the sole Headless-session owner.
    void Initialize();
    void Update(uint32_t diff);
    void Shutdown();

    void OnHumanLogin();
    void OnHumanLogout();

private:
    struct Candidate
    {
        uint32_t accountId = 0;
        ObjectGuid characterGuid;
    };

    RandomBotService() = default;
    ~RandomBotService() = default;

    void LoadCandidates();
    void MaintainOnlinePool();
    void RemoveExpiredBots(uint32_t diff);
    uint32_t TargetCount() const;
    uint32_t DesiredTargetCount() const;
    bool TryAutoCreate();
    enum class AutoCreateCharResult { Success, TransientName, TransientError, Permanent };
    AutoCreateCharResult TryCreateCharacterOnAccount(uint32_t accountId, std::vector<std::pair<uint8_t, uint8_t>> const& validForAccount);
    // Returns TEAM_NONE if empty/unknown, otherwise ALLIANCE/HORDE (67/469). Sets isMixed
    // when cached candidates contain both factions (must be excluded).
    uint32_t GetAccountAllowedTeam(uint32_t accountId, bool& isMixed) const;
    void ResolvePinnedBots();
    bool IsPinnedGuid(uint32 guidLow) const { return m_pinnedGuids.find(guidLow) != m_pinnedGuids.end(); }

    std::vector<Candidate> m_candidates;
    std::vector<uint32_t> m_ageMs;
    std::vector<uint32_t> m_strategyAgeMs;
    std::vector<uint32_t> m_randomizeAgeMs;
    size_t m_nextCandidate = 0;
    uint32_t m_serviceElapsedMs = 0;
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
    // (mixed, disabled, faction violation, limit, or other materialization
    // errors) are logged once and never retried. Transient name collisions
    // are not recorded here and remain retryable. Reset only on Initialize.
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
    // login-DB INSERT visibility (core PR #412/7084557, final 7084557): after
    // AOR_OK but GetId still 0, remember exactly one pending fresh account
    // name, retry that same name with bounded/log-throttled cadence while
    // continuing the existing-account selection path and without allocating
    // another fresh account; log once after prolonged unresolved period.
    // Cleared once the id is visible, then one character creation is attempted.
    std::string m_pendingAccountName;
    time_t m_pendingNextRetry = 0;
    time_t m_pendingSince = 0;
    bool m_pendingStaleLogged = false;
};

} // namespace TortoiseBots
