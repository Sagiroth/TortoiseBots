"""Compile the production log opener with config/logger doubles."""
import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest


class LogOpenRotationTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which('g++'), 'g++ unavailable')
    def test_write_append_disabled_and_failure_paths(self):
        root = pathlib.Path(__file__).resolve().parents[1]
        source = pathlib.Path(os.environ.get('TBOTS_TEST_SOURCE', root / 'ai/playerbot/PlayerbotAIConfig.cpp')).read_text()
        body = source[source.index('bool PlayerbotAIConfig::openLog('):source.index('void PlayerbotAIConfig::log(')]
        fixture = r'''
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <sys/stat.h>
#include <system_error>
#include <unordered_map>
#include <vector>
#include <zlib.h>
#include "LogFileRotation.h"
using namespace std;
struct Config { string dir; string GetStringDefault(char const*, char const*) { return dir; } } sConfig;
struct Log { unsigned errors=0; void outError(char const*, ...) { ++errors; } } sLog;
// Synchronous twin of the production background packer: same .gz output, no
// detached thread, so the assertions below stay deterministic.
static void CompressLogPiece(string const& piece)
{
    FILE* in = fopen(piece.c_str(), "rb");
    if (!in)
        return;
    gzFile out = gzopen((piece + ".gz").c_str(), "wb6");
    if (!out)
    {
        fclose(in);
        return;
    }
    bool ok = true;
    char buffer[64 * 1024];
    size_t n;
    while (ok && (n = fread(buffer, 1, sizeof(buffer), in)) > 0)
        ok = gzwrite(out, buffer, unsigned(n)) == int(n);
    fclose(in);
    if (gzclose(out) != Z_OK)
        ok = false;
    if (ok)
        std::remove(piece.c_str());
}
class PlayerbotAIConfig {
public:
 bool enabled=true;
 uint32_t logRetentionDays=0;
 unordered_map<string,pair<FILE*,bool>> logFiles;
 unordered_map<string,time_t> logHour;
 ai::LogFileRotation logRotation;
 bool hasLog(string const&) { return enabled; }
 bool openLog(string, char const*, bool haslog=false);
 string logsDirPath();
 bool archiveLog(string const& path, time_t now);
 void rotateLogHourly(string const& fileName, FILE* file);
 ~PlayerbotAIConfig() { for(auto const& entry: logFiles) if(entry.second.second) fclose(entry.second.first); }
};
''' + body + r'''
static void Write(string const& p, char const* text) { ofstream(p) << text; }
static string Read(string const& p) { ifstream in(p); return {istreambuf_iterator<char>(in),istreambuf_iterator<char>()}; }
static string ReadGz(string const& p)
{
    gzFile in = gzopen(p.c_str(), "rb");
    assert(in);
    string out;
    char buf[4096];
    int n;
    while ((n = gzread(in, buf, sizeof(buf))) > 0)
        out.append(buf, n);
    gzclose(in);
    return out;
}
static vector<string> GzPieces(string const& dir, string const& stem)
{
    vector<string> out;
    for (auto const& entry : filesystem::directory_iterator(dir))
    {
        string f = entry.path().filename().string();
        if (f.compare(0, stem.size(), stem) == 0 && f.size() > stem.size() + 3 &&
            f.compare(f.size() - 3, 3, ".gz") == 0)
            out.push_back(entry.path().string());
    }
    return out;
}
static bool HasContent(vector<string> const& pieces, char const* want)
{
    for (string const& p : pieces)
        if (ReadGz(p) == want)
            return true;
    return false;
}
int main(int argc, char** argv) {
 assert(argc==2); sConfig.dir=argv[1]; string base=sConfig.dir+"/";
 string path=base+"bot_events.csv";
 Write(path,"prior run\n");
 {
  // Disabled: the live file is left alone.
  PlayerbotAIConfig cfg; cfg.enabled=false;
  assert(!cfg.openLog("bot_events.csv","w"));
  assert(Read(path)=="prior run\n" && GzPieces(base,"bot_events_").empty());
  // First open of a run packs the previous run and starts a fresh live file.
  cfg.enabled=true; assert(cfg.openLog("bot_events.csv","w"));
  assert(Read(path).empty());
  vector<string> pieces=GzPieces(base,"bot_events_");
  assert(pieces.size()==1 && ReadGz(pieces[0])=="prior run\n");
  // Reopening in the same run just truncates: nothing more to pack.
  fputs("current run\n",cfg.logFiles.at("bot_events.csv").first);
  assert(cfg.openLog("bot_events.csv","w"));
  assert(Read(path).empty() && GzPieces(base,"bot_events_").size()==1);
  fputs("refreshed\n",cfg.logFiles.at("bot_events.csv").first);
 }
 {
  // A new run packs the live file again, keeping both packs.
  PlayerbotAIConfig cfg; assert(cfg.openLog("bot_events.csv","w"));
  assert(Read(path).empty());
  vector<string> pieces=GzPieces(base,"bot_events_");
  assert(pieces.size()==2 && HasContent(pieces,"prior run\n") && HasContent(pieces,"refreshed\n"));
  // The hourly roll packs the live file once the hour changes.
  fputs("hour one\n",cfg.logFiles.at("bot_events.csv").first);
  cfg.logHour["bot_events.csv"]=0;
  cfg.rotateLogHourly("bot_events.csv",cfg.logFiles.at("bot_events.csv").first);
  assert(!cfg.logFiles.at("bot_events.csv").second && !filesystem::exists(path));
  pieces=GzPieces(base,"bot_events_");
  assert(pieces.size()==3 && HasContent(pieces,"hour one\n"));
 }
 // A rename that cannot succeed keeps the live file and reports once.
 string rodir=base+"ro/"; filesystem::create_directory(rodir);
 string blocked=rodir+"blocked.csv"; Write(blocked,"keep me\n");
 assert(chmod(rodir.c_str(),0555)==0);
 {
  PlayerbotAIConfig cfg;
  assert(!cfg.openLog("ro/blocked.csv","w"));
  assert(!cfg.logFiles.at("ro/blocked.csv").first && !cfg.logFiles.at("ro/blocked.csv").second);
  assert(Read(blocked)=="keep me\n" && sLog.errors==1);
 }
 assert(chmod(rodir.c_str(),0755)==0);
 {
  // Append mode packs the previous content first, then appends to a fresh file.
  PlayerbotAIConfig cfg;
  assert(cfg.openLog("ro/blocked.csv","a"));
  fputs("appended\n",cfg.logFiles.at("ro/blocked.csv").first);
 }
 assert(Read(blocked)=="appended\n");
 assert(HasContent(GzPieces(rodir,"blocked_"),"keep me\n"));
 {
  PlayerbotAIConfig cfg; cfg.enabled=false;
  Write(base+"forced.log","forced previous\n");
  assert(cfg.openLog("forced.log","w",true));
  assert(Read(base+"forced.log").empty());
  assert(HasContent(GzPieces(base,"forced_"),"forced previous\n"));
  cfg.enabled=true;
  assert(!cfg.openLog("missing/subdir.csv","w"));
  assert(!cfg.logFiles.at("missing/subdir.csv").first && !cfg.logFiles.at("missing/subdir.csv").second);
 }
 assert(sLog.errors==2);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            cpp = pathlib.Path(tmp) / 'test.cpp'
            exe = pathlib.Path(tmp) / 'test'
            cpp.write_text(fixture)
            subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-I',
                            str(root / 'ai/playerbot'), str(cpp), '-o', str(exe), '-lz'], check=True)
            subprocess.run([str(exe), tmp], check=True)


if __name__ == '__main__':
    unittest.main()
