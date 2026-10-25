#pragma once
#include <cstdint>

namespace ai
{
class FleeRetryState
{
public:
    bool CanRetry(uint32_t now) const
    {
        return !failed || uint32_t(now - failedAt) >= 3000;
    }

    void Failed(uint32_t now)
    {
        failed = true;
        failedAt = now;
    }

    bool ShouldReport(uint32_t now)
    {
        if (reported && uint32_t(now - reportedAt) < 10000)
            return false;
        reported = true;
        reportedAt = now;
        return true;
    }

private:
    bool failed = false;
    bool reported = false;
    uint32_t failedAt = 0;
    uint32_t reportedAt = 0;
};
}
