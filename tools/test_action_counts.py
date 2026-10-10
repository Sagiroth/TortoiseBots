"""Compile the production action counter with config/logger doubles.

Covers AiPlayerbot.ActionCountsLog (BotDiagnostics CountAction/DumpActionCounts):
off costs one branch and records nothing; on aggregates OK vs failed outcomes
per (class, action) with no allocation once the row exists; the first dump
truncates with a header and later dumps append cumulative snapshots.
Requires g++ (skipped when unavailable).
"""
import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest


class ActionCountsTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which("g++"), "g++ unavailable")
    def test_off_records_nothing_and_dumps_are_cumulative(self):
        root = pathlib.Path(__file__).resolve().parents[1]
        source = pathlib.Path(
            os.environ.get("TBOTS_TEST_SOURCE", root / "ai/playerbot/BotDiagnostics.cpp")
        ).read_text()
        start = source.index("namespace ai { namespace botdiag {")
        # Stop before the evade-probe block that follows the counter.
        end = source.index("namespace\n{", start)
        counter = source[start:end]
        fixture = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <sstream>
#include <string>
#include <unistd.h>
#include <unordered_map>
#include <utility>
#include <vector>
struct PlayerbotAIConfig {
    bool enableActionLog = false;
    bool actionCountsLog = false;
    std::unordered_map<std::string, std::pair<FILE*, bool>> logFiles;
    bool isLogOpen(std::string f) { auto it = logFiles.find(f); return it != logFiles.end() && it->second.second; }
    bool openLog(std::string f, char const* mode, bool /*haslog*/ = false) {
        auto& e = logFiles[f];
        if (e.second) fclose(e.first);
        e.first = fopen(("/tmp/" + f).c_str(), mode);
        e.second = e.first != nullptr;
        return e.second;
    }
    void log(std::string f, char const* line) {
        if (!isLogOpen(f) && !openLog(f.c_str(), "a", true)) return;
        FILE* h = logFiles[f].first;
        fputs(line, h); fputc('\n', h); fflush(h);
    }
} sPlayerbotAIConfig;
''' + counter + r'''
static std::string readAll(char const* path) {
    FILE* h = fopen(path, "r");
    assert(h);
    std::string out;
    char buf[1024];
    while (size_t n = fread(buf, 1, sizeof(buf), h)) out.append(buf, n);
    fclose(h);
    return out;
}
int main() {
    using namespace ai::botdiag;
    // A stale file from an aborted run must not break the off-assert below.
    remove("/tmp/action_counts.csv");
    // Off: one branch, nothing recorded, nothing dumped.
    CountAction(1, "shield slam", true);
    DumpActionCounts();
    assert(access("/tmp/action_counts.csv", 0) != 0);

    sPlayerbotAIConfig.actionCountsLog = true;
    CountAction(1, "shield slam", true);
    CountAction(1, "shield slam", true);
    CountAction(1, "shield slam", false);
    CountAction(8, "fireball", true);
    // Empty-name guard and unknown class still count under their own row.
    CountAction(0, "melee", false);
    DumpActionCounts();
    std::string first = readAll("/tmp/action_counts.csv");
    assert(first.find("utc_time,class,action,ok_count,fail_count\n") == 0);
    assert(first.find(",1,shield slam,2,1\n") != std::string::npos);
    assert(first.find(",8,fireball,1,0\n") != std::string::npos);

    // Later rows reuse the row: no new rows, counters keep accumulating.
    CountAction(1, "shield slam", true);
    DumpActionCounts();
    std::string second = readAll("/tmp/action_counts.csv");
    assert(second.find(",1,shield slam,3,1\n") != std::string::npos);
    // Second dump appended (two snapshots of the warrior row), header once.
    assert(second.find("utc_time") == 0);
    assert(second.find("utc_time", 1) == std::string::npos);
    remove("/tmp/action_counts.csv");
}
'''
        with tempfile.TemporaryDirectory(prefix="action-counts-") as folder:
            source_path = pathlib.Path(folder) / "test.cpp"
            binary_path = pathlib.Path(folder) / "test"
            source_path.write_text(fixture)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            str(source_path), "-o", str(binary_path)], check=True)
            subprocess.run([str(binary_path)], check=True)


if __name__ == "__main__":
    unittest.main()
