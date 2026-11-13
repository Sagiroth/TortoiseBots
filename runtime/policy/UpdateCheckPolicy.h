#pragma once

// Pure helpers for the startup update check: read the release tag from a
// GitHub "releases/latest" reply and compare semantic versions.

#include <string>

namespace TortoiseBots {
namespace UpdateCheckPolicy {

// "1.22.0" or "v1.22.0" into three numbers; false for anything else ("dev").
inline bool ParseSemver(std::string text, unsigned (&out)[3])
{
    if (!text.empty() && (text[0] == 'v' || text[0] == 'V'))
        text.erase(0, 1);
    size_t pos = 0;
    for (int part = 0; part < 3; ++part)
    {
        if (pos >= text.size() || text[pos] < '0' || text[pos] > '9')
            return false;
        unsigned value = 0;
        while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9')
        {
            if (value > 100000)
                return false;
            value = value * 10 + unsigned(text[pos] - '0');
            ++pos;
        }
        out[part] = value;
        if (part < 2)
        {
            if (pos >= text.size() || text[pos] != '.')
                return false;
            ++pos;
        }
    }
    return pos == text.size();
}

// The release tag without its "v" ("1.22.0"), or "" when the reply has no
// valid semantic-version tag (offline, rate limited, no release).
inline std::string ParseReleaseTag(std::string const& json)
{
    size_t const key = json.find("\"tag_name\"");
    if (key == std::string::npos)
        return "";
    size_t const colon = json.find(':', key);
    if (colon == std::string::npos)
        return "";
    size_t const open = json.find('"', colon + 1);
    if (open == std::string::npos)
        return "";
    size_t const close = json.find('"', open + 1);
    if (close == std::string::npos)
        return "";
    std::string tag = json.substr(open + 1, close - open - 1);
    unsigned parts[3];
    if (!ParseSemver(tag, parts))
        return "";
    if (tag[0] == 'v' || tag[0] == 'V')
        tag.erase(0, 1);
    return tag;
}

// True only when both parse and latest is strictly newer.
inline bool IsNewer(std::string const& latest, std::string const& running)
{
    unsigned a[3], b[3];
    if (!ParseSemver(latest, a) || !ParseSemver(running, b))
        return false;
    for (int i = 0; i < 3; ++i)
        if (a[i] != b[i])
            return a[i] > b[i];
    return false;
}

} // namespace UpdateCheckPolicy
} // namespace TortoiseBots
