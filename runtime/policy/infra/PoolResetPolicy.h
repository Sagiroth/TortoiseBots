#pragma once

// Pure policy logic for the managed random-bot pool reset (issue #265).
//
// Header-only and free of core/database types on purpose: the module's reset
// paths run against a live world database, so the decision rules that must not
// regress (setting parsing, one-shot generation comparison, username
// normalization, adoption challenge derivation) live here where
// tools/test_pool_reset_policy.cpp can exercise them standalone.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace TortoiseBots
{

enum class PoolResetMode
{
    Off,      // never reset automatically (default)
    Once,     // reset only while the token differs from the applied generation
    Always,   // development: reset on every process start
    Invalid,  // fail closed: log, do not reset
};

// AiPlayerbot.RandomBotPoolReset is a single string setting:
//   off | always | once:<token>
struct PoolResetSetting
{
    PoolResetMode mode = PoolResetMode::Off;
    std::string token;   // only meaningful for Once
    std::string error;   // only set for Invalid
};

// Bounded token length; a token is data, never interpolated into SQL.
inline constexpr size_t kPoolResetTokenMaxLength = 128;

// Shared with the module's config layer so `bot pool status` and the reset
// planner cannot disagree about what a token may contain.
inline bool IsValidPoolResetToken(std::string const& token)
{
    if (token.empty() || token.size() > kPoolResetTokenMaxLength)
        return false;
    for (char c : token)
    {
        // Printable, non-space ASCII only: the value is stored in a VARCHAR,
        // printed in logs, and compared as data.
        if (c <= 0x20 || c >= 0x7f)
            return false;
    }
    return true;
}

inline PoolResetSetting ParsePoolResetSetting(std::string const& raw)
{
    PoolResetSetting setting;

    std::string value = raw;
    value.erase(0, value.find_first_not_of(" \t\r\n"));
    if (!value.empty())
        value.erase(value.find_last_not_of(" \t\r\n") + 1);

    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (value.empty() || lower == "off")
        return setting;

    if (lower == "always")
    {
        setting.mode = PoolResetMode::Always;
        return setting;
    }

    if (lower.rfind("once:", 0) == 0)
    {
        std::string token = value.substr(5);
        token.erase(0, token.find_first_not_of(" \t"));
        if (!token.empty())
            token.erase(token.find_last_not_of(" \t") + 1);

        if (token.empty())
        {
            setting.mode = PoolResetMode::Invalid;
            setting.error = "once: requires a non-empty generation token";
            return setting;
        }
        if (!IsValidPoolResetToken(token))
        {
            setting.mode = PoolResetMode::Invalid;
            setting.error = "once: generation token must be 1-" +
                std::to_string(kPoolResetTokenMaxLength) + " printable, non-space ASCII characters";
            return setting;
        }
        setting.mode = PoolResetMode::Once;
        setting.token = token;
        return setting;
    }

    setting.mode = PoolResetMode::Invalid;
    setting.error = "expected 'off', 'always' or 'once:<token>'";
    return setting;
}

// A one-shot token is applied exactly once: it is recorded only after deletion
// and verification complete, so re-using the same value is a no-op and a crash
// mid-reset resumes on the next start. Comparison is case-sensitive: the token
// is data, not a keyword.
inline bool ShouldResetForGeneration(PoolResetSetting const& setting, std::string const& appliedGeneration)
{
    switch (setting.mode)
    {
        case PoolResetMode::Always:
            return true;
        case PoolResetMode::Once:
            return setting.token != appliedGeneration;
        case PoolResetMode::Off:
        case PoolResetMode::Invalid:
        default:
            return false;
    }
}

inline char const* PoolResetModeName(PoolResetMode mode)
{
    switch (mode)
    {
        case PoolResetMode::Off:     return "off";
        case PoolResetMode::Once:    return "once";
        case PoolResetMode::Always:  return "always";
        case PoolResetMode::Invalid: return "invalid";
        default:                     return "unknown";
    }
}

// Account usernames are compared case-insensitively (the login database stores
// them lowercased). The registry keeps the stored name for diagnostics and
// compares through this helper so a renamed account is detected instead of
// silently matching.
inline std::string NormalizeAccountUsername(std::string const& username)
{
    std::string normalized = username;
    normalized.erase(0, normalized.find_first_not_of(" \t"));
    if (!normalized.empty())
        normalized.erase(normalized.find_last_not_of(" \t") + 1);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return normalized;
}

inline bool AccountUsernameEquals(std::string const& a, std::string const& b)
{
    return NormalizeAccountUsername(a) == NormalizeAccountUsername(b);
}

inline bool AccountUsernameHasPrefix(std::string const& username, std::string const& prefix)
{
    if (prefix.empty())
        return false;
    std::string loweredUser = NormalizeAccountUsername(username);
    std::string loweredPrefix = NormalizeAccountUsername(prefix);
    return loweredUser.size() >= loweredPrefix.size() &&
        loweredUser.compare(0, loweredPrefix.size(), loweredPrefix) == 0;
}

// What the prefix-match scan should do after its COUNT(*) probe. The core
// returns a null query result for a SELECT that matched zero rows, exactly as
// for a failed query (which it logs itself), so the row scan cannot tell an
// empty account table from a database that did not answer. A COUNT(*) probe is
// answered with exactly one row on success, so it separates the two.
enum class AccountScanVerdict
{
    Failed,   // probe unanswered: the account list is unknown, report failure
    Empty,    // probe answered zero: an empty match list is a normal result
    Populate, // probe answered non-zero: run the row scan
};

inline AccountScanVerdict VerdictForAccountScanProbe(bool probeAnswered, uint32_t probeCount)
{
    if (!probeAnswered)
        return AccountScanVerdict::Failed;
    return probeCount ? AccountScanVerdict::Populate : AccountScanVerdict::Empty;
}

// Short-lived confirmation challenge for `bot pool adopt confirm <challenge>`.
// Derived from the exact previewed set (account id + stored username), so any
// change between preview and confirmation invalidates it.
inline std::string AdoptionChallenge(std::vector<std::pair<uint32_t, std::string>> const& accounts)
{
    std::vector<std::pair<uint32_t, std::string>> sorted = accounts;
    std::sort(sorted.begin(), sorted.end(), [](auto const& a, auto const& b)
    {
        if (a.first != b.first)
            return a.first < b.first;
        return NormalizeAccountUsername(a.second) < NormalizeAccountUsername(b.second);
    });

    uint64_t hash = 1469598103934665603ULL; // FNV-1a 64 offset basis
    auto mix = [&hash](std::string const& text)
    {
        for (unsigned char c : text)
        {
            hash ^= c;
            hash *= 1099511628211ULL;
        }
    };

    for (auto const& account : sorted)
    {
        mix(std::to_string(account.first));
        mix(":");
        mix(NormalizeAccountUsername(account.second));
        mix("\n");
    }

    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "%012llX", static_cast<unsigned long long>(hash & 0xFFFFFFFFFFFFULL));
    return std::string(buffer);
}

inline bool AdoptionChallengeMatches(std::string const& expected, std::string const& provided)
{
    if (expected.size() != provided.size() || expected.empty())
        return false;
    std::string a = expected;
    std::string b = provided;
    std::transform(a.begin(), a.end(), a.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    std::transform(b.begin(), b.end(), b.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return a == b;
}

} // namespace TortoiseBots
