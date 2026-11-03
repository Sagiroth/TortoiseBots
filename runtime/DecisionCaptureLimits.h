#pragma once

// The AiPlayerbot.DecisionCapture.* limits and their clamps. The input is
// signed on purpose: the config reader hands back an int, and a negative value
// must clamp to the low bound, never wrap to a uint32 near the high one.
// Constants are the shipped defaults; aiplayerbot.conf.dist.in documents them.

#include <cstdint>

namespace TortoiseBots
{
namespace DecisionCapture
{

inline constexpr std::uint32_t kDefaultSegmentMiB = 32;
inline constexpr std::uint32_t kMinSegmentMiB = 1;
inline constexpr std::uint32_t kMaxSegmentMiB = 256;

inline constexpr std::uint32_t kDefaultCaptureMiB = 512;
inline constexpr std::uint32_t kMaxCaptureMiB = 4096;

inline constexpr std::uint32_t kDefaultTotalMiB = 4096;
inline constexpr std::uint32_t kMaxTotalMiB = 65536;

inline constexpr std::uint32_t kDefaultRecordKiB = 512;
inline constexpr std::uint32_t kMinRecordKiB = 64;
inline constexpr std::uint32_t kMaxRecordKiB = 4096;

inline constexpr std::uint32_t kDefaultKeyframeEvery = 60;
inline constexpr std::uint32_t kMinKeyframeEvery = 1;
inline constexpr std::uint32_t kMaxKeyframeEvery = 600;

inline std::uint32_t ClampSegmentMiB(std::int64_t mib)
{
    if (mib < static_cast<std::int64_t>(kMinSegmentMiB))
        return kMinSegmentMiB;
    if (mib > static_cast<std::int64_t>(kMaxSegmentMiB))
        return kMaxSegmentMiB;
    return static_cast<std::uint32_t>(mib);
}

// One capture holds at least one segment, so its floor is the segment size.
inline std::uint32_t ClampCaptureMiB(std::int64_t mib, std::uint32_t segmentMiB)
{
    if (mib < static_cast<std::int64_t>(segmentMiB))
        return segmentMiB;
    if (mib > static_cast<std::int64_t>(kMaxCaptureMiB))
        return kMaxCaptureMiB;
    return static_cast<std::uint32_t>(mib);
}

// The folder holds at least one capture, so its floor is the capture cap.
inline std::uint32_t ClampTotalMiB(std::int64_t mib, std::uint32_t captureMiB)
{
    if (mib < static_cast<std::int64_t>(captureMiB))
        return captureMiB;
    if (mib > static_cast<std::int64_t>(kMaxTotalMiB))
        return kMaxTotalMiB;
    return static_cast<std::uint32_t>(mib);
}

inline std::uint32_t ClampRecordKiB(std::int64_t kib)
{
    if (kib < static_cast<std::int64_t>(kMinRecordKiB))
        return kMinRecordKiB;
    if (kib > static_cast<std::int64_t>(kMaxRecordKiB))
        return kMaxRecordKiB;
    return static_cast<std::uint32_t>(kib);
}

inline std::uint32_t ClampKeyframeEvery(std::int64_t every)
{
    if (every < static_cast<std::int64_t>(kMinKeyframeEvery))
        return kMinKeyframeEvery;
    if (every > static_cast<std::int64_t>(kMaxKeyframeEvery))
        return kMaxKeyframeEvery;
    return static_cast<std::uint32_t>(every);
}

} // namespace DecisionCapture
} // namespace TortoiseBots
