#pragma once

#include <cstdint>
#include <vector>

#include "ObjectGuid.h"

namespace TortoiseBots
{

class RandomBotService
{
public:
    static RandomBotService& Instance();

    // Load the configured random-account character pool once. This service
    // never creates accounts/characters and never owns a session directly;
    // BotManager remains the sole Headless-session owner.
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

    std::vector<Candidate> m_candidates;
    std::vector<uint32_t> m_ageMs;
    size_t m_nextCandidate = 0;
    uint32_t m_updateTimerMs = 0;
    uint32_t m_targetCount = 0;
    uint32_t m_humanSessions = 0;
    bool m_initialized = false;
    bool m_started = false;
};

} // namespace TortoiseBots
