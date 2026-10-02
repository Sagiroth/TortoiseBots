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

// Core revision the module is compiled into, probed by TortoiseBots.cmake with
// a safe.directory override. The core's own CMake detection uses a plain
// `git rev-parse`, which silently fails in the containerised dev builder (the
// root-owned bind mount trips git's dubious-ownership guard) and bakes in the
// "unknown"/epoch fallback. Empty define (out-of-tree build, git missing)
// keeps the core's compiled values from <revision.h>.
#ifndef TORTOISEBOTS_CORE_REVISION
#define TORTOISEBOTS_CORE_REVISION ""
#endif
#ifndef TORTOISEBOTS_CORE_DATE
#define TORTOISEBOTS_CORE_DATE ""
#endif

#include <revision.h>
#include <string>

namespace TortoiseBots {

// The -D values arrive as quoted literals ("2026-09-25-v8"), so strip the
// quotes at runtime instead of stringifying (which would keep them).
inline std::string StripQuotes(std::string raw)
{
    if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"')
        raw = raw.substr(1, raw.size() - 2);
    return raw;
}

// Human-readable build id, e.g. "2026-09-25-v3" or "dev".
inline std::string BuildVersion()
{
    std::string raw = StripQuotes(TORTOISEBOTS_BUILD_VERSION);
    if (raw.empty())
        return "dev";
    return raw;
}

// Revision/date of the core the module runs inside. Prefer the values this
// build probed from the core checkout; fall back to what the core compiled in.
inline std::string CoreRevision()
{
    std::string raw = StripQuotes(TORTOISEBOTS_CORE_REVISION);
    return raw.empty() ? std::string(REVISION_HASH) : raw;
}

inline std::string CoreRevisionDate()
{
    std::string raw = StripQuotes(TORTOISEBOTS_CORE_DATE);
    return raw.empty() ? std::string(REVISION_DATE) : raw;
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
