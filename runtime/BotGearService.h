#pragma once

// `.bot gear` job queue: gears bots one tick at a time so a 40-bot raid does
// not hitch the world. World thread only. The pure grammar, cap filter, bag
// purge decision and summary line live in policy/hire/BotGearPolicy.h.

// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectGuid.h"

#include "policy/hire/BotGearPolicy.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class Player;
class PlayerbotAI;

namespace TortoiseBots
{

struct BotGearPin
{
    uint8_t tier = 0;
    uint32_t ilvlCap = 0;
};

class BotGearService
{
public:
    static BotGearService& Instance();

    struct Job
    {
        uint64_t requesterGuid = 0;
        std::vector<ObjectGuid> bots;
        BotGear::Args args;
    };

    struct BotOutcome
    {
        std::string name;
        std::string role;
        bool geared = false;
        std::string skip;
        float avgIlvl = 0;
        uint32_t cappedSlots = 0;
        uint32_t purged = 0;
        uint32_t kept = 0;
    };

    struct JobResult
    {
        uint8_t tier = 0;
        uint32_t ilvlCap = 0;
        std::vector<BotOutcome> bots;
    };

    bool Queue(uint64_t requesterGuid, std::vector<ObjectGuid> const& bots, BotGear::Args const& args);
    void Update(uint32_t diff);

    bool Busy() const { return m_running; }
    bool IsPinned(uint32_t guidLow) const;
    // The cap a pinned bot gears under (0 = no cap recorded). Consulted by
    // the bot's own auto-equip path so a pinned bot never puts an above-cap
    // item back on (raid-w1-r2 item 2).
    uint32_t PinnedCap(uint32_t guidLow) const;

private:
    BotGearService() = default;

    struct ActiveJob
    {
        uint64_t requesterGuid = 0;
        std::vector<ObjectGuid> bots;
        BotGear::Args args;
        size_t next = 0;
        JobResult result;
    };

    void FinishJob();
    static void SendLines(uint64_t requesterGuid, std::string const& plain, std::string const& structured);
    static uint32_t WornMaxIlvl(Player* bot);
    static float AverageWornIlvl(Player* bot);

    bool m_running = false;
    ActiveJob m_job;
    std::unordered_map<uint32_t, BotGearPin> m_pinned;
};

} // namespace TortoiseBots
