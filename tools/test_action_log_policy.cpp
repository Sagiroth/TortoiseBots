// Standalone regression test for the per-bot action log policy: which bots
// get a file (AiPlayerbot.ActionLogScope), when a file is full, which files a
// cleanup deletes, and when a cleanup is due.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_action_log_policy.cpp -o /tmp/test_action_log_policy
//   /tmp/test_action_log_policy

#include "../ai/playerbot/ActionLogPolicy.h"

#include <cstdio>
#include <cstdlib>

using namespace ai::botdiag;

static int g_failed = 0;
#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failed; } } while (0)

static ActionLogFile F(char const* name, uint64_t mb, int64_t mtime, bool live = false)
{
    ActionLogFile f;
    f.name = name;
    f.bytes = mb * kActionLogMB;
    f.mtime = mtime;
    f.live = live;
    return f;
}

static bool Has(std::vector<size_t> const& v, size_t i)
{
    for (size_t x : v) if (x == i) return true;
    return false;
}

int main()
{
    // Scope word: case and blanks do not matter, anything unknown is the default.
    {
        bool ok = false;
        CHECK(ParseActionLogScope("all", &ok) == ActionLogScope::All && ok);
        CHECK(ParseActionLogScope("  Commanded ", &ok) == ActionLogScope::Commanded && ok);
        CHECK(ParseActionLogScope("NONE", &ok) == ActionLogScope::None && ok);
        CHECK(ParseActionLogScope("group", &ok) == ActionLogScope::Commanded && !ok);
        CHECK(ParseActionLogScope("", &ok) == ActionLogScope::Commanded && !ok);
        CHECK(ParseActionLogScope("1") == ActionLogScope::Commanded);
        CHECK(std::string(ActionLogScopeName(ActionLogScope::None)) == "none");
    }

    // Scope decisions.
    {
        // all: every bot, even a pool bot nobody commands.
        CHECK(ActionLogScopeAllows(ActionLogScope::All, false, false, false));
        CHECK(ActionLogScopeAllows(ActionLogScope::All, true, true, true));
        // none: nobody, whatever the bot is.
        CHECK(!ActionLogScopeAllows(ActionLogScope::None, true, true, true));
        CHECK(!ActionLogScopeAllows(ActionLogScope::None, false, false, false));
        // commanded: a pool bot is out; any one of the three reasons is in.
        CHECK(!ActionLogScopeAllows(ActionLogScope::Commanded, false, false, false));
        CHECK(ActionLogScopeAllows(ActionLogScope::Commanded, true, false, false));
        CHECK(ActionLogScopeAllows(ActionLogScope::Commanded, false, true, false));
        CHECK(ActionLogScopeAllows(ActionLogScope::Commanded, false, false, true));
        CHECK(ActionLogScopeAllows(ActionLogScope::Commanded, true, true, true));
        // The shipped default is the word the parser falls back to.
        CHECK(ParseActionLogScope("commanded") == ActionLogScope::Commanded);
        CHECK(kActionLogScopeRecheckMs > 0 && kActionLogScopeRecheckMs <= 60000);
    }

    // File cap: the byte that does not fit rotates; 0 is no cap.
    {
        CHECK(!ActionLogFileFull(0, 100, 100));
        CHECK(!ActionLogFileFull(100 * kActionLogMB - 10, 10, 100));
        CHECK(ActionLogFileFull(100 * kActionLogMB - 10, 11, 100));
        CHECK(ActionLogFileFull(100 * kActionLogMB, 1, 100));
        CHECK(!ActionLogFileFull(500 * kActionLogMB, 1000, 0));
    }

    // Retention: oldest first, age limit, size limit, live files kept.
    {
        int64_t const now = 1000000000;
        int64_t const day = 86400;

        // Nothing over either limit: nothing deleted.
        {
            std::vector<ActionLogFile> files = { F("a.log", 10, now - day), F("b.log", 10, now - 2 * day) };
            CHECK(PickActionLogDeletions(files, now, 3, 2048).empty());
        }

        // Age limit: only files older than the days go, regardless of size.
        {
            std::vector<ActionLogFile> files = {
                F("old.log", 1, now - 4 * day),
                F("edge.log", 1, now - 3 * day),       // exactly 3 days: kept
                F("new.log", 1, now - 1 * day),
            };
            auto p = PickActionLogDeletions(files, now, 3, 2048);
            CHECK(p.size() == 1 && p[0] == 0);
        }

        // Size limit: the oldest go first until the folder fits.
        {
            std::vector<ActionLogFile> files = {
                F("newest.log", 400, now - 100),
                F("oldest.log", 400, now - 3000),
                F("middle.log", 400, now - 2000),
            };
            auto p = PickActionLogDeletions(files, now, 3, 1000);   // 1200 MB held, limit 1000
            CHECK(p.size() == 1 && p[0] == 1);
            p = PickActionLogDeletions(files, now, 3, 500);         // needs two gone: 1200 -> 400
            CHECK(p.size() == 2 && p[0] == 1 && p[1] == 2);
            p = PickActionLogDeletions(files, now, 3, 1200);        // exactly at the limit: nothing
            CHECK(p.empty());
        }

        // Both limits at once: age deletes count toward the size, then the size trims the rest.
        {
            std::vector<ActionLogFile> files = {
                F("stale.log", 600, now - 5 * day),
                F("a.log", 300, now - 2000),
                F("b.log", 300, now - 1000),
                F("c.log", 300, now - 500),
            };
            auto p = PickActionLogDeletions(files, now, 3, 700);
            // stale (age) frees 600 -> 900 left; 900 > 700 so the oldest remaining (a) goes -> 600
            CHECK(p.size() == 2 && p[0] == 0 && p[1] == 1);
            CHECK(!Has(p, 2) && !Has(p, 3));
        }

        // Live files are never picked, not for age and not for size.
        {
            std::vector<ActionLogFile> files = {
                F("live-old.log", 500, now - 9 * day, true),
                F("dead-new.log", 500, now - 100),
                F("dead-old.log", 500, now - 5 * day),
            };
            auto p = PickActionLogDeletions(files, now, 3, 600);
            // dead-old goes for age; 1000 left > 600; the only other candidate is dead-new (live is skipped)
            CHECK(p.size() == 2 && p[0] == 2 && p[1] == 1);
            CHECK(!Has(p, 0));
        }

        // Live files alone over the limit: nothing else to take, nothing live taken.
        {
            std::vector<ActionLogFile> files = { F("l1.log", 900, now - 10, true), F("l2.log", 900, now - 20, true) };
            CHECK(PickActionLogDeletions(files, now, 3, 1000).empty());
        }

        // 0 switches a limit off.
        {
            std::vector<ActionLogFile> files = { F("ancient.log", 5000, now - 100 * day), F("x.log", 5000, now - 50) };
            CHECK(PickActionLogDeletions(files, now, 0, 0).empty());
            auto p = PickActionLogDeletions(files, now, 3, 0);
            CHECK(p.size() == 1 && p[0] == 0);
            p = PickActionLogDeletions(files, now, 0, 6000);
            CHECK(p.size() == 1 && p[0] == 0);
        }

        // Same time: name order decides, so the pick is stable.
        {
            std::vector<ActionLogFile> files = { F("b.log", 10, now - 5), F("a.log", 10, now - 5) };
            auto p = PickActionLogDeletions(files, now, 3, 10);
            CHECK(p.size() == 1 && p[0] == 1);
        }

        // The incident: 731 files, 14 GB, limit 2 GB, all within 3 days -> trimmed to the limit.
        {
            std::vector<ActionLogFile> files;
            for (int i = 0; i < 731; ++i)
                files.push_back(F("bot.log", 20, now - i * 300));
            auto p = PickActionLogDeletions(files, now, 3, 2048);
            uint64_t left = 0;
            for (size_t i = 0; i < files.size(); ++i) if (!Has(p, i)) left += files[i].bytes;
            CHECK(left <= 2048ull * kActionLogMB);
            CHECK(left > 2048ull * kActionLogMB - 20 * kActionLogMB);   // not over-deleted
            CHECK(Has(p, 730) && !Has(p, 0));                           // the oldest went, the newest stayed
        }
    }

    // Deletion safety: only the files this log wrote are ever cleanup candidates.
    {
        CHECK(IsActionLogFileName("Thrall_acc12_20261017-031500.log"));
        CHECK(IsActionLogFileName("Thrall_acc12_20261017-031500-3.log"));    // same-second rotation
        CHECK(IsActionLogFileName("A_b_acc7_20261017-031500.log"));          // underscore in the name
        CHECK(!IsActionLogFileName("server.log"));                           // a non-matching .log
        CHECK(!IsActionLogFileName("Thrall_acc12_20261017.log"));
        CHECK(!IsActionLogFileName("Thrall_acc12_20261017-031500.log.gz"));
        CHECK(!IsActionLogFileName("Thrall_acc12_20261017-031500.log.bak"));
        CHECK(!IsActionLogFileName("_acc12_20261017-031500.log"));           // no bot name
        CHECK(!IsActionLogFileName("x.txt"));
        CHECK(!IsActionLogFileName("../evil.log"));
        CHECK(!IsActionLogFileName("../Thrall_acc12_20261017-031500.log"));
        CHECK(!IsActionLogFileName("sub/Thrall_acc12_20261017-031500.log"));
        CHECK(!IsActionLogFileName("sub\\Thrall_acc12_20261017-031500.log"));
        CHECK(!IsActionLogFileName(""));

        // A regular file with a matching name is the only deletable entry.
        CHECK(ActionLogEntryDeletable("Thrall_acc12_20261017-031500.log", true, false));
        CHECK(!ActionLogEntryDeletable("Thrall_acc12_20261017-031500.log", true, true));    // symlink
        CHECK(!ActionLogEntryDeletable("Thrall_acc12_20261017-031500.log", false, false));  // subdirectory
        CHECK(!ActionLogEntryDeletable("Thrall_acc12_20261017-031500.log", false, true));
        CHECK(!ActionLogEntryDeletable("server.log", true, false));
        CHECK(!ActionLogEntryDeletable("x.txt", true, false));
        CHECK(!ActionLogEntryDeletable("../evil.log", true, false));
    }

    // Cleanup schedule.
    {
        CHECK(ActionLogSweepDue(false, 0, 0, 2048));                    // startup
        CHECK(!ActionLogSweepDue(true, 10, 0, 2048));
        CHECK(!ActionLogSweepDue(true, 3599, 100, 2048));
        CHECK(ActionLogSweepDue(true, 3600, 0, 2048));                  // hourly
        CHECK(ActionLogSweepDue(true, -5, 0, 2048));                    // clock went back
        CHECK(!ActionLogSweepDue(true, 60, 511 * kActionLogMB, 2048));
        CHECK(ActionLogSweepDue(true, 60, 512 * kActionLogMB, 2048));   // a quarter of the limit written
        CHECK(!ActionLogSweepDue(true, 60, 99999 * kActionLogMB, 0));   // no size limit, no early sweep
    }

    // Shutdown transitions: disabling the log or narrowing the scope to
    // none must release every open descriptor, or the process keeps one
    // fd per logged-out bot until the fd table fills.
    {
        CHECK(ActionLogNeedsCloseAll(true, false, ActionLogScope::All, ActionLogScope::All, true));
        CHECK(ActionLogNeedsCloseAll(true, false, ActionLogScope::Commanded, ActionLogScope::None, true));
        CHECK(ActionLogNeedsCloseAll(true, true, ActionLogScope::All, ActionLogScope::None, true));
        CHECK(!ActionLogNeedsCloseAll(true, true, ActionLogScope::Commanded, ActionLogScope::None, false));
        CHECK(!ActionLogNeedsCloseAll(true, true, ActionLogScope::None, ActionLogScope::None, true));
        CHECK(!ActionLogNeedsCloseAll(true, true, ActionLogScope::All, ActionLogScope::Commanded, true));
        CHECK(!ActionLogNeedsCloseAll(false, false, ActionLogScope::All, ActionLogScope::All, true));
        CHECK(!ActionLogNeedsCloseAll(false, true, ActionLogScope::Commanded, ActionLogScope::Commanded, false));
    }

    if (g_failed)
    {
        std::fprintf(stderr, "%d check(s) failed\n", g_failed);
        return 1;
    }
    std::printf("test_action_log_policy: all checks passed\n");
    return 0;
}
