#pragma once
#include <cstdint>
#include <sstream>
#include <string>

namespace ai
{
struct SarturaDiagnosticThrottle
{
    bool Due(uint32_t now) const { return !emitted || uint32_t(now - last) >= 2000; }
    void Mark(uint32_t now) { emitted = true; last = now; }
    bool emitted = false;
    uint32_t last = 0;
};
inline std::string SarturaDiagnosticRow(unsigned entry, unsigned aura, float distance,
    std::string const& reason, bool success, bool hasDestination, float x, float y, float z)
{
    std::ostringstream row;
    row << "entry=" << entry << "|aura=" << aura << "|distance=" << distance
        << "|reason=" << reason << "|success=" << (success ? 1 : 0)
        << "|destination=";
    if (hasDestination) row << x << ',' << y << ',' << z;
    else row << "none";
    return row.str();
}
}
