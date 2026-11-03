#pragma once

// The authoritative count of ranged shots a player fired (Auto Shot, a wand's Shoot): the participation clock of the
// wedged rescue (commands/Commander.cpp, DungeonRun::MemberParticipates) compares it between run ticks.
//
// Why a counter, never a timer snapshot: the run reads a bot about once a second, and a shot every 1.4 to 3 s can
// fall between two reads whatever the ranged attack timer shows at the two instants, so a bot that keeps firing
// can look idle and the destructive rescue (combat drop + summon) hits a bot that is fighting. The count moves on
// every shot that executed, so any read after a shot sees it, at any phase.
//
// The host side (host/RangedShotAdapter.cpp) records a shot from the core's PlayerScript OnSpellCast hook, which
// Spell::cast runs for the auto-repeat spell once its targets are filled in, i.e. a shot that actually fired and
// not one CheckCast refused. The hook runs on the map threads, so the ledger is locked. No Player pointer is kept.
//
// Evidence unavailable: until the host side has armed the ledger, no shot is ever recorded, so a bot that is
// shooting looks idle. RangedShotEvidenceMissing says so, and the glue then treats a bot with auto-repeat on as
// NOT eligible for the rescue (fail safe) and writes a note.

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace TortoiseBots
{
namespace DungeonRun
{

// Whether a shot fired between the last read of a bot and this one. `observed` is false for the first read of a
// bot (no earlier count to compare with). The count is compared for inequality: it wraps, and the ledger may have
// dropped an entry under its bound (the count restarts at zero), which reads as a shot: the safe direction.
inline bool RangedShotFiredSince(bool observed, std::uint32_t lastCount, std::uint32_t count)
{
    return observed && count != lastCount;
}

// The shot evidence cannot be trusted: the ledger was never armed by its host hook. A bot with an auto-repeat spell
// on is then not eligible for the destructive rescue, since nothing can tell its shots from silence.
inline bool RangedShotEvidenceMissing(bool armed, bool autoRepeatOn)
{
    return !armed && autoRepeatOn;
}

class RangedShotLedger
{
public:
    static constexpr std::size_t kMaxEntries = 1024;

    static RangedShotLedger& Instance()
    {
        static RangedShotLedger ledger;
        return ledger;
    }

    // The host hook is registered: shots are recorded from now on.
    void Arm()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_armed = true;
    }

    bool Armed() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_armed;
    }

    // A shot of `guid` executed.
    void Record(std::uint64_t guid)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_counts.size() >= kMaxEntries && m_counts.find(guid) == m_counts.end())
            m_counts.erase(m_counts.begin()); // bounded: any entry goes; its bot restarts at 0 (reads as a shot)
        ++m_counts[guid];
    }

    // The shots recorded for `guid` so far (0 for none).
    std::uint32_t Count(std::uint64_t guid) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_counts.find(guid);
        return it == m_counts.end() ? 0 : it->second;
    }

    std::size_t Size() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_counts.size();
    }

private:
    mutable std::mutex m_mutex;
    bool m_armed = false;
    std::unordered_map<std::uint64_t, std::uint32_t> m_counts;
};

} // namespace DungeonRun
} // namespace TortoiseBots
