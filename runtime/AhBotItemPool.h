#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

#include "runtime/policy/ah/AhBotItemPoolPolicy.h"

namespace TortoiseBots
{

class AhBotItemPool
{
public:
    static AhBotItemPool& Instance();

    void Load();
    bool IsLoaded() const;
    uint32_t PickItemForListing() const;
    bool IsCandidate(uint32_t itemId) const;
    size_t CandidateCount() const;
    std::string GetStatus() const;

private:
    AhBotItemPool() = default;

    mutable std::mutex m_mutex;
    bool m_loaded = false;
    ai::AhBotItemPoolTable m_table;
};

} // namespace TortoiseBots
