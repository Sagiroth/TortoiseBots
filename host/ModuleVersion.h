#pragma once

// Per-merge build version (<UTC date>-v<N>) stamped by
// .github/workflows/generate-changelog.yml into the root VERSION file.
// The CMake build defines TORTOISEBOTS_BUILD_VERSION from that file and
// falls back to "dev" for checkouts without it; keep the same default here
// so static analysis and any TU compiled without the define still build.
//
// The -D value arrives as a quoted literal ("2026-09-25-v8"), so strip the
// quotes at runtime instead of stringifying (which would keep them).
#ifndef TORTOISEBOTS_BUILD_VERSION
#define TORTOISEBOTS_BUILD_VERSION "dev"
#endif

#include <string>

namespace TortoiseBots {

// Human-readable build id, e.g. "2026-09-25-v3" or "dev".
inline std::string BuildVersion()
{
    std::string raw = TORTOISEBOTS_BUILD_VERSION;
    if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"')
        raw = raw.substr(1, raw.size() - 2);
    if (raw.empty())
        return "dev";
    return raw;
}

// Author credit and canonical source shown at startup, on login and by
// `.bot about`. This is the program's Appropriate Legal Notice under
// AGPL-3.0 section 5(d); modified versions must keep displaying it.
constexpr char const* kModuleAuthor = "Sagiroth";
constexpr char const* kModuleSourceUrl = "https://github.com/Sagiroth/TortoiseBots";
constexpr char const* kModuleLicence = "AGPL-3.0";

inline std::string AttributionLine()
{
    return std::string("TortoiseBots by ") + kModuleAuthor + " - " + kModuleSourceUrl +
        " (" + kModuleLicence + ", source available)";
}

} // namespace TortoiseBots
