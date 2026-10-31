#pragma once

// Pure decisions for the per-bot action log (BotActionLog): which bots may
// write a file, when a file is full, which old files a cleanup deletes. No
// server types, so tools/test_action_log_policy.cpp builds it on its own.
//
//   AiPlayerbot.ActionLogScope      all | commanded | none
//   AiPlayerbot.ActionLogMaxAgeDays files older than this are deleted (0 = no age limit)
//   AiPlayerbot.ActionLogMaxTotalMB logs/bots may hold this much (0 = no size limit)
//   AiPlayerbot.ActionLogMaxFileMB  one bot file rotates at this size (0 = no cap)

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <regex>
#include <string>
#include <vector>

namespace ai { namespace botdiag {

enum class ActionLogScope : uint8_t
{
    All,        // every bot, as before the scope existed
    Commanded,  // commander-bound or hired bots (and bots marked with BotActionLog::SetTraced)
    None,       // no per-bot files
};

// Case-insensitive; blanks around the word are ignored. Anything else falls
// back to Commanded (the shipped default) and sets *recognised to false.
inline ActionLogScope ParseActionLogScope(std::string value, bool* recognised = nullptr)
{
    size_t b = 0, e = value.size();
    while (b < e && std::isspace(static_cast<unsigned char>(value[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(value[e - 1]))) --e;
    value = value.substr(b, e - b);
    for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (recognised) *recognised = true;
    if (value == "all") return ActionLogScope::All;
    if (value == "commanded") return ActionLogScope::Commanded;
    if (value == "none") return ActionLogScope::None;
    if (recognised) *recognised = false;
    return ActionLogScope::Commanded;
}

inline char const* ActionLogScopeName(ActionLogScope scope)
{
    switch (scope)
    {
        case ActionLogScope::All: return "all";
        case ActionLogScope::Commanded: return "commanded";
        case ActionLogScope::None: return "none";
    }
    return "commanded";
}

// Does this bot get a file? commanded: the bot is in a group a commander
// bound, or is a hire, or was marked traced (BotActionLog::SetTraced; nothing on
// dev calls it yet).
inline bool ActionLogScopeAllows(ActionLogScope scope, bool inCommandedGroup, bool isHire, bool inTracedGroup)
{
    switch (scope)
    {
        case ActionLogScope::All: return true;
        case ActionLogScope::Commanded: return inCommandedGroup || isHire || inTracedGroup;
        case ActionLogScope::None: return false;
    }
    return false;
}

// How long a bot's scope answer is trusted before it is looked up again (ms).
// A bot that is bound, hired or traced starts logging within this time; one
// that left stops within it.
constexpr int64_t kActionLogScopeRecheckMs = 15000;

constexpr uint64_t kActionLogMB = 1024ull * 1024ull;

// True when appending addBytes to a file of `bytes` would pass the cap.
inline bool ActionLogFileFull(uint64_t bytes, uint64_t addBytes, uint32_t maxFileMB)
{
    return maxFileMB != 0 && bytes + addBytes > static_cast<uint64_t>(maxFileMB) * kActionLogMB;
}

// The cleanup only ever touches what BotActionLog::BuildPath writes:
// <botname>_acc<id>_<YYYYMMDD-HHMMSS>[-<n>].log, directly inside logs/bots. Any
// other name, a path, a symlink or a directory is left alone, so a server that
// keeps its own files in that folder loses none of them.
inline bool IsActionLogFileName(std::string const& name)
{
    static std::regex const pattern("^[^/\\\\]+_acc[0-9]+_[0-9]{8}-[0-9]{6}(-[0-9]+)?\\.log$");
    return name.find("..") == std::string::npos && std::regex_match(name, pattern);
}

inline bool ActionLogEntryDeletable(std::string const& name, bool isRegularFile, bool isSymlink)
{
    return isRegularFile && !isSymlink && IsActionLogFileName(name);
}

struct ActionLogFile
{
    std::string name;
    uint64_t bytes = 0;
    int64_t mtime = 0;   // seconds, last write
    bool live = false;   // a bot holds it open right now
};

// Indexes into `files`, in the order they are deleted. Files past the age limit
// go first; then, while the folder (live files included) is over the size limit,
// the oldest remaining file goes. A live file is never picked, so the folder
// can stay over the limit when live files alone exceed it.
inline std::vector<size_t> PickActionLogDeletions(std::vector<ActionLogFile> const& files, int64_t now,
                                                  uint32_t maxAgeDays, uint32_t maxTotalMB)
{
    std::vector<size_t> order(files.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        if (files[a].mtime != files[b].mtime) return files[a].mtime < files[b].mtime;
        return files[a].name < files[b].name;
    });

    std::vector<size_t> picked;
    std::vector<bool> gone(files.size(), false);
    uint64_t total = 0;
    for (ActionLogFile const& f : files) total += f.bytes;

    if (maxAgeDays != 0)
    {
        int64_t const cutoff = now - static_cast<int64_t>(maxAgeDays) * 86400;
        for (size_t i : order)
        {
            if (!files[i].live && files[i].mtime < cutoff)
            {
                picked.push_back(i);
                gone[i] = true;
                total -= files[i].bytes;
            }
        }
    }

    if (maxTotalMB != 0)
    {
        uint64_t const limit = static_cast<uint64_t>(maxTotalMB) * kActionLogMB;
        for (size_t i : order)
        {
            if (total <= limit) break;
            if (gone[i] || files[i].live) continue;
            picked.push_back(i);
            gone[i] = true;
            total -= files[i].bytes;
        }
    }
    return picked;
}

// Cleanup runs at startup (never swept yet), then every hour, and early when
// the bots wrote a quarter of the size limit since the last one, so a busy
// hour cannot run far past it.
constexpr int64_t kActionLogSweepIntervalSec = 3600;

inline bool ActionLogSweepDue(bool everSwept, int64_t secondsSinceSweep, uint64_t bytesSinceSweep, uint32_t maxTotalMB)
{
    if (!everSwept || secondsSinceSweep >= kActionLogSweepIntervalSec || secondsSinceSweep < 0)
        return true;
    return maxTotalMB != 0 && bytesSinceSweep >= static_cast<uint64_t>(maxTotalMB) * kActionLogMB / 4;
}

// Shutdown transition: the per-tick Maintain closes every open descriptor
// when the log is switched off or the scope narrows to none, so logged-out
// bots stop holding one fd each until the table fills.
inline bool ActionLogNeedsCloseAll(bool wasEnabled, bool enabled,
                                   ActionLogScope lastScope, ActionLogScope scope,
                                   bool haveLastScope)
{
    if (!enabled && wasEnabled)
        return true;
    return enabled && haveLastScope && lastScope != ActionLogScope::None &&
        scope == ActionLogScope::None;
}

}}  // namespace ai::botdiag
