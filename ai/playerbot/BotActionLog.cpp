//
// BotActionLog — per-bot detailed action log. See BotActionLog.h for design.
//

#include "playerbot/playerbot.h"
#include "BotActionLog.h"
#include "PlayerbotAI.h"
#include "BotDiagnostics.h"  // for SC_LOG + IsActionLogEnabled
#include "PlayerbotAIConfig.h"
#include "Objects/Player.h"
#include "Objects/Unit.h"
#include "ObjectGuid.h"
#include "ObjectMgr.h"
#include "Spells/SpellAuraDefines.h"  // MAX_AURAS
#include "Spells/SpellMgr.h"
#include "Log.h"
#include "runtime/HireLifecycle.h"
#include "runtime/CommandedGroupQuery.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <mutex>
#include <vector>
#include <sstream>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#endif

namespace ai { namespace botdiag {

std::unordered_map<uint32, BotActionLog::OpenFile> BotActionLog::sFiles;
std::unordered_map<uint32, BotActionLog::ScopeState> BotActionLog::sScope;
std::unordered_set<uint32> BotActionLog::sTraced;
std::unordered_set<uint32> BotActionLog::sRetired;

// sFiles is reachable from any map thread (Spell.cpp / Unit.cpp hooks fire
// on whatever thread is updating that bot's map). Guard every read/write so
// the unordered_map doesn't get torn under concurrent insert/erase. Only
// engaged when AiPlayerbot.EnableActionLog=1; default-off path takes no lock.
static std::mutex sFilesMutex;

// Bytes the bots wrote since the last cleanup; Maintain() sweeps early when
// the bots wrote a quarter of the size limit.
static std::atomic<uint64> sWrittenSinceSweep{0};

static int64 NowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

static void MakeDirIdempotent(const char* path)
{
#ifdef _WIN32
    _mkdir(path);
#else
    mkdir(path, 0755);
#endif
}

void BotActionLog::EnsureLogDir()
{
    // mangosd's CWD depends on how it was launched. The typical live-server
    // layout puts it in bin/ (so logs/ is at ../logs), but CMake test-runs
    // launch it from the build dir where logs/ is adjacent. Try both;
    // MakeDirIdempotent silently ignores EEXIST.
    const char* roots[] = { "../logs", "logs", nullptr };
    for (int i = 0; roots[i]; ++i)
    {
        MakeDirIdempotent(roots[i]);
        std::string sub = std::string(roots[i]) + "/bots";
        MakeDirIdempotent(sub.c_str());
    }
}

std::string BotActionLog::BuildPath(Player* bot, uint32 seq)
{
    if (!bot)
        return "";

    // Filename: <botname>_acc<sessionId>_<YYYYMMDD-HHMMSS>.log. The timestamp
    // distinguishes multiple summons of the same bot within one mangosd run:
    // a bounce-and-resummon writes a fresh file rather than appending.
    std::time_t now = std::time(nullptr);
    std::tm lt{};
#ifdef _WIN32
    localtime_s(&lt, &now);
#else
    localtime_r(&now, &lt);
#endif
    char tsBuf[32];
    std::strftime(tsBuf, sizeof(tsBuf), "%Y%m%d-%H%M%S", &lt);

    uint32 sessionId = bot->GetSession() ? bot->GetSession()->GetAccountId() : 0u;

    std::ostringstream oss;
    oss << "../logs/bots/"
        << bot->GetName() << "_acc" << sessionId << "_" << tsBuf;
    if (seq)
        oss << "-" << seq;   // a rotation in the same second as the file it follows
    oss << ".log";
    return oss.str();
}

// Caller holds sFilesMutex. Only the cached answer is read here: the lookup
// itself (groups, bindings, the hire ledger) runs on the world thread in
// RefreshScope. A bot never looked up yet is out of scope until it is.
bool BotActionLog::ScopeAllowedLocked(uint32 guid)
{
    switch (sPlayerbotAIConfig.actionLogScope)
    {
        case ActionLogScope::All: return true;
        case ActionLogScope::None: return false;
        case ActionLogScope::Commanded: break;
    }
    auto it = sScope.find(guid);
    return it != sScope.end() && it->second.allowed;
}

void BotActionLog::SetTraced(uint32 botGuidLow, bool traced)
{
    std::lock_guard<std::mutex> g(sFilesMutex);
    if (traced)
        sTraced.insert(botGuidLow);
    else
        sTraced.erase(botGuidLow);
    auto it = sScope.find(botGuidLow);
    if (it != sScope.end())
        it->second.nextCheckMs = 0;   // look again at the bot's next update
}

void BotActionLog::RefreshScope(PlayerbotAI* ai)
{
    if (!sPlayerbotAIConfig.enableActionLog ||
        sPlayerbotAIConfig.actionLogScope != ActionLogScope::Commanded || !ai)
        return;
    Player* bot = ai->GetBot();
    if (!bot)
        return;

    uint32 const guid = bot->GetGUIDLow();
    int64 const now = NowMs();
    bool traced = false;
    {
        std::lock_guard<std::mutex> g(sFilesMutex);
        auto it = sScope.find(guid);
        if (it != sScope.end() && now < it->second.nextCheckMs)
            return;
        traced = sTraced.count(guid) != 0;
    }

    bool const commanded = TortoiseBots::BotCommands::IsInCommandedGroup(bot);
    bool const hired = TortoiseBots::HireLifecycle::Instance().IsHired(bot->GetObjectGuid());
    bool const allowed = ActionLogScopeAllows(ActionLogScope::Commanded, commanded, hired, traced);

    std::FILE* toClose = nullptr;
    {
        std::lock_guard<std::mutex> g(sFilesMutex);
        ScopeState& st = sScope[guid];
        st.allowed = allowed;
        st.nextCheckMs = now + kActionLogScopeRecheckMs;
        if (!allowed)
        {
            // Left the scope: stop writing and let go of the file, which then
            // counts for the retention cleanup like any other.
            auto f = sFiles.find(guid);
            if (f != sFiles.end())
            {
                toClose = f->second.file;
                sFiles.erase(f);
            }
        }
    }
    if (toClose)
    {
        fprintf(toClose, "# end-of-log (bot left the action log scope)\n");
        fflush(toClose);
        fclose(toClose);
    }
}

std::FILE* BotActionLog::Open(PlayerbotAI* ai)
{
    // Gate: when AiPlayerbot.EnableActionLog is off, never open log files.
    // All Write* methods route through Open and become silent no-ops.
    if (!ai::botdiag::IsActionLogEnabled()) return nullptr;
    if (!ai) return nullptr;
    Player* bot = ai->GetBot();
    if (!bot) return nullptr;

    uint32 guid = bot->GetGUIDLow();
    {
        std::lock_guard<std::mutex> g(sFilesMutex);
        auto it = sFiles.find(guid);
        if (it != sFiles.end() && it->second.file)
            return it->second.file;
        if (!ScopeAllowedLocked(guid))
            return nullptr;
        // Logout tombstone: the AI is gone; a map-thread hook holding the
        // dead pointer must not reopen a file Close just shut.
        if (sRetired.count(guid))
            return nullptr;
    }

    EnsureLogDir();

    // A file of the same name (a rotation within the same second) is never
    // appended to: the next free "-N" name is taken instead, so a full file is
    // not reopened just to rotate again.
    FILE* f = nullptr;
    std::string path;
    for (uint32 seq = 0; seq < 100 && !f; ++seq)
    {
        path = BuildPath(bot, seq);
        if (path.empty()) return nullptr;
        FILE* candidate = fopen(path.c_str(), "a");
        if (!candidate)
        {
            // Fallback: mangosd may have been launched from the layout root rather
            // than from bin/, in which case ../logs/bots/ doesn't exist but
            // logs/bots/ does. Retry with the bare path.
            const char* prefix = "../logs/bots/";
            std::string alt = path;
            if (alt.compare(0, std::strlen(prefix), prefix) == 0)
                alt = alt.substr(3); // drop the "../"
            candidate = fopen(alt.c_str(), "a");
            if (candidate) path = alt;
        }
        if (!candidate)
            break;
        fseek(candidate, 0, SEEK_END);
        if (ftell(candidate) > 0 && seq + 1 < 100)
        {
            fclose(candidate);
            continue;
        }
        f = candidate;
    }
    if (!f)
    {
        SC_LOG("BotActionLog::Open FAILED for bot=%s path=%s",
               bot->GetName(), path.c_str());
        return nullptr;
    }

    {
        std::lock_guard<std::mutex> g(sFilesMutex);
        // Another thread may have raced us through the open()+fopen window
        // for the same guid, or logout may have retired it meanwhile.
        // Either way, close our handle instead of registering a live fd.
        auto race = sFiles.find(guid);
        if ((race != sFiles.end() && race->second.file) || sRetired.count(guid))
        {
            fclose(f);
            return (race != sFiles.end()) ? race->second.file : nullptr;
        }
        OpenFile& entry = sFiles[guid];
        entry.file = f;
        entry.path = path;
        entry.bytes = 0;
    }
    SC_LOG("BotActionLog opened bot=%s path=%s", bot->GetName(), path.c_str());

    // Header line: human-readable; subsequent lines are tag-prefixed
    // events.
    char header[512];
    int n = snprintf(header, sizeof(header), "# BotActionLog v1 — bot=%s guid=%u accountId=%u opened=%s\n",
            bot->GetName(), guid,
            bot->GetSession() ? bot->GetSession()->GetAccountId() : 0u,
            path.c_str());
    if (n > 0)
    {
        size_t len = std::min<size_t>(static_cast<size_t>(n), sizeof(header) - 1);
        std::lock_guard<std::mutex> g(sFilesMutex);
        auto it = sFiles.find(guid);
        if (it != sFiles.end() && it->second.file == f)
        {
            fwrite(header, 1, len, f);
            fflush(f);
            it->second.bytes += len;
            sWrittenSinceSweep += len;
        }
    }

    return f;
}

void BotActionLog::Close(PlayerbotAI* ai)
{
    if (!ai) return;
    Player* bot = ai->GetBot();
    if (!bot) return;
    SC_LOG("BotActionLog closed bot=%s", bot->GetName() ? bot->GetName() : "?");
    Close(bot->GetGUIDLow());
}

void BotActionLog::NoteLogin(uint32 botGuidLow)
{
    std::lock_guard<std::mutex> g(sFilesMutex);
    sRetired.erase(botGuidLow);
}

void BotActionLog::Close(uint32 guid)
{
    std::FILE* handle = nullptr;
    {
        std::lock_guard<std::mutex> g(sFilesMutex);
        sScope.erase(guid);
        sTraced.erase(guid);
        sRetired.insert(guid);
        auto it = sFiles.find(guid);
        if (it == sFiles.end()) return;
        handle = it->second.file;
        sFiles.erase(it);
    }
    if (handle)
    {
        fprintf(handle, "# end-of-log (Close called)\n");
        fflush(handle);
        fclose(handle);
    }
}

void BotActionLog::CloseAll()
{
    std::vector<std::FILE*> handles;
    {
        std::lock_guard<std::mutex> g(sFilesMutex);
        sScope.clear();
        sTraced.clear();
        sRetired.clear();
        for (auto& kv : sFiles)
            if (kv.second.file)
                handles.push_back(kv.second.file);
        sFiles.clear();
    }
    for (std::FILE* handle : handles)
    {
        fprintf(handle, "# end-of-log (CloseAll called)\n");
        fflush(handle);
        fclose(handle);
    }
}

std::FILE* BotActionLog::GetHandle(PlayerbotAI* ai)
{
    if (!ai) return nullptr;
    // Off (the default): no lock, no lookup.
    if (!ai::botdiag::IsActionLogEnabled()) return nullptr;
    Player* bot = ai->GetBot();
    if (!bot) return nullptr;
    {
        std::lock_guard<std::mutex> g(sFilesMutex);
        if (!ScopeAllowedLocked(bot->GetGUIDLow()))
            return nullptr;
        auto it = sFiles.find(bot->GetGUIDLow());
        if (it != sFiles.end() && it->second.file)
            return it->second.file;
    }
    // Lazy-open on first use. Open() itself gates on AiPlayerbot.EnableActionLog,
    // returning nullptr (and not creating any file) when the flag is off.
    return Open(ai);
}

static std::string TimestampWithMs()
{
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::tm lt{};
#ifdef _WIN32
    localtime_s(&lt, &t);
#else
    localtime_r(&t, &lt);
#endif
    char buf[32];
    int n = (int)std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &lt);
    snprintf(buf + n, sizeof(buf) - n, ".%03d", (int)ms.count());
    return buf;
}

void BotActionLog::Write(PlayerbotAI* ai, const char* tag, const char* fmt, ...)
{
    FILE* f = GetHandle(ai);
    if (!f) return;

    char line[2048];
    int prefixLen = snprintf(line, sizeof(line), "[%s] [%s] ",
                              TimestampWithMs().c_str(), tag ? tag : "?");
    if (prefixLen < 0 || prefixLen >= (int)sizeof(line))
    {
        // prefix didn't fit (shouldn't happen) — bail
        return;
    }

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line + prefixLen, sizeof(line) - prefixLen, fmt, ap);
    va_end(ap);

    AppendLine(ai, line, std::strlen(line));
}

// Appends one line under the lock (a scope change on the world thread may
// close the file at any time). A file that would pass ActionLogMaxFileMB is
// closed with a note and the line starts the next file.
void BotActionLog::AppendLine(PlayerbotAI* ai, const char* line, size_t len)
{
    Player* bot = ai->GetBot();
    if (!bot) return;
    uint32 const guid = bot->GetGUIDLow();

    for (int attempt = 0; attempt < 2; ++attempt)
    {
        std::FILE* full = nullptr;
        {
            std::lock_guard<std::mutex> g(sFilesMutex);
            auto it = sFiles.find(guid);
            if (it == sFiles.end() || !it->second.file || !ScopeAllowedLocked(guid))
                return;
            OpenFile& entry = it->second;
            if (attempt == 0 && ActionLogFileFull(entry.bytes, len + 1, sPlayerbotAIConfig.actionLogMaxFileMB))
            {
                full = entry.file;
                fprintf(full, "# file cap of %u MB reached; the log continues in a new file\n",
                        sPlayerbotAIConfig.actionLogMaxFileMB);
                sFiles.erase(it);
            }
            else
            {
                fwrite(line, 1, len, entry.file);
                fputc('\n', entry.file);
                fflush(entry.file);
                entry.bytes += len + 1;
                sWrittenSinceSweep += len + 1;
                return;
            }
        }
        fflush(full);
        fclose(full);
        if (!Open(ai))
            return;
    }
}

void BotActionLog::LogState(PlayerbotAI* ai, const char* reason)
{
    if (!ai) return;
    Player* bot = ai->GetBot();
    if (!bot) return;
    FILE* f = GetHandle(ai);
    if (!f) return;

    // Build a compact one-liner with all the per-tick state we usually
    // care about.
    uint32 hp     = bot->GetMaxHealth() ? (bot->GetHealth() * 100u / bot->GetMaxHealth()) : 0u;
    uint32 maxMana = bot->GetMaxPower(bot->GetPowerType());
    uint32 manaCur = bot->GetPower(bot->GetPowerType());
    uint32 mp     = maxMana ? (manaCur * 100u / maxMana) : 0u;
    bool inCombat = bot->IsInCombat();
    Unit* tgt     = bot->GetVictim();
    const char* tgtName = tgt ? tgt->GetName() : "(none)";

    Write(ai, "STATE",
          "reason=%s hp=%u%% mp=%u%% combat=%d alive=%d level=%u "
          "pos=%.1f,%.1f,%.1f,%.1f map=%u zone=%u "
          "target=%s targetGuid=0x%llx",
          reason ? reason : "tick",
          hp, mp, (int)inCombat, (int)bot->IsAlive(), bot->GetLevel(),
          bot->getPositionX(), bot->getPositionY(), bot->getPositionZ(), bot->getOrientation(),
          bot->GetMapId(), bot->getZoneId(),
          tgtName, (unsigned long long)(tgt ? tgt->getObjectGuid().GetRawValue() : 0));

    // Aura snapshot: dump every aura currently
    // on the bot from m_spellAuraHolders. This lets us correlate what the user *sees*
    // in the client buff bar with what's actually in the server-side aura state. The
    // AURA_ATTEMPT hook only fires when a holder is being added; this dump shows the
    // steady-state list, so a buff that's been on the bot since before the log opened
    // (e.g. login-loaded persistent aura) still shows up here.
    //
    // We also dump UNIT_FIELD_AURA slot fields directly so we can detect mismatch
    // between the holder map and the visible-aura slots (which is what the client
    // actually renders).
    {
        std::string holders;
        holders.reserve(256);
        auto const& hm = bot->GetSpellAuraHolderMap();
        for (auto const& it : hm)
        {
            char buf[64];
            snprintf(buf, sizeof(buf), "%u,", it.first);
            holders += buf;
        }
        if (holders.empty()) holders = "(none)";

        std::string slots;
        slots.reserve(256);
        for (uint32 slot = 0; slot < MAX_AURAS; ++slot)
        {
            uint32 sid = bot->GetUInt32Value(UNIT_FIELD_AURA + slot);
            if (sid)
            {
                char buf[64];
                snprintf(buf, sizeof(buf), "[%u]=%u ", slot, sid);
                slots += buf;
            }
        }
        if (slots.empty()) slots = "(none)";

        Write(ai, "AURA_DUMP", "holders=%s slots=%s", holders.c_str(), slots.c_str());
    }
}

void BotActionLog::LogCastStart(PlayerbotAI* ai, uint32 spellId, ObjectGuid targetGuid, uint32 castTimeMs)
{
    SpellEntry const* info = sSpellMgr.GetSpellEntry(spellId);
    const char* spellName = info ? info->SpellName[0].c_str() : "?";
    Write(ai, "CAST_START",
          "spell=%s(%u) targetGuid=0x%llx castTime=%ums",
          spellName, spellId,
          (unsigned long long)targetGuid.GetRawValue(),
          castTimeMs);
}

void BotActionLog::LogCastResult(PlayerbotAI* ai, uint32 spellId, uint8 result, const char* phase)
{
    SpellEntry const* info = sSpellMgr.GetSpellEntry(spellId);
    const char* spellName = info ? info->SpellName[0].c_str() : "?";
    // REVIEW-FIX (M1 review): SPELL_CAST_OK is 0xFF on this core
    // (SpellDefines.h), not 0 — 0x00 is SPELL_FAILED_AFFECTING_COMBAT.
    // Comparing against the symbol keeps the tag truthful.
    Write(ai, result == SPELL_CAST_OK ? "CAST_OK" : "CAST_FAIL",
          "spell=%s(%u) result=%u phase=%s",
          spellName, spellId, (unsigned)result, phase ? phase : "?");
}

void BotActionLog::LogAuraApply(PlayerbotAI* ai, uint32 spellId, int32 durationMs, ObjectGuid casterGuid, bool force)
{
    // Filter heuristic: skip very-short transient auras unless force=true.
    if (!force && durationMs > 0 && durationMs < 3000)
        return;

    SpellEntry const* info = sSpellMgr.GetSpellEntry(spellId);
    const char* spellName = info ? info->SpellName[0].c_str() : "?";
    Write(ai, "AURA_APPLY",
          "spell=%s(%u) duration=%dms caster=0x%llx",
          spellName, spellId, durationMs,
          (unsigned long long)casterGuid.GetRawValue());
}

void BotActionLog::LogAuraRemove(PlayerbotAI* ai, uint32 spellId, ObjectGuid casterGuid, bool force)
{
    SpellEntry const* info = sSpellMgr.GetSpellEntry(spellId);
    const char* spellName = info ? info->SpellName[0].c_str() : "?";
    Write(ai, "AURA_REMOVE",
          "spell=%s(%u) caster=0x%llx",
          spellName, spellId,
          (unsigned long long)casterGuid.GetRawValue());
}

// ---------------------------------------------------------------------------
// Retention. logs/bots holds one file per summoned bot; left on, a busy realm
// wrote ~8 GB a day. Sweep() deletes the oldest files until both
// ActionLogMaxAgeDays and ActionLogMaxTotalMB hold (ActionLogPolicy.h picks
// them), and never a file a bot has open. It lists one folder and deletes, so
// it runs at startup and then hourly from Maintain(), never per tick.
// ---------------------------------------------------------------------------
namespace fs = std::filesystem;

static fs::path ResolveBotLogDir()
{
    // Same two layouts as EnsureLogDir/Open: bin/ as the working directory
    // ("../logs") or the layout root ("logs").
    const char* roots[] = { "../logs/bots", "logs/bots", nullptr };
    for (int i = 0; roots[i]; ++i)
    {
        std::error_code ec;
        if (fs::is_directory(roots[i], ec))
            return roots[i];
    }
    return fs::path();
}

void BotActionLog::Sweep()
{
    sWrittenSinceSweep = 0;

    fs::path const dir = ResolveBotLogDir();
    if (dir.empty())
        return;   // nothing was ever written here

    std::unordered_set<std::string> liveNames;
    {
        std::lock_guard<std::mutex> g(sFilesMutex);
        for (auto const& kv : sFiles)
            liveNames.insert(fs::path(kv.second.path).filename().string());
    }

    int64 const nowSec = static_cast<int64>(std::time(nullptr));
    auto const fsNow = fs::file_time_type::clock::now();

    std::vector<ActionLogFile> files;
    std::vector<fs::path> paths;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
    {
        std::error_code fileEc;
        // symlink_status does not follow links: a symlink is never a regular file here.
        fs::file_status const status = it->symlink_status(fileEc);
        if (fileEc || !ActionLogEntryDeletable(it->path().filename().string(),
                                               fs::is_regular_file(status), fs::is_symlink(status)))
            continue;   // not a file this log wrote: never listed, never deleted
        uintmax_t const size = it->file_size(fileEc);
        if (fileEc) continue;
        auto const written = it->last_write_time(fileEc);
        if (fileEc) continue;

        ActionLogFile f;
        f.name = it->path().filename().string();
        f.bytes = size;
        f.mtime = nowSec - std::chrono::duration_cast<std::chrono::seconds>(fsNow - written).count();
        f.live = liveNames.count(f.name) != 0;
        files.push_back(f);
        paths.push_back(it->path());
    }
    if (ec)
        return;

    std::vector<size_t> const picked = PickActionLogDeletions(files, nowSec,
        sPlayerbotAIConfig.actionLogMaxAgeDays, sPlayerbotAIConfig.actionLogMaxTotalMB);

    uint64 freed = 0;
    uint32 removed = 0;
    for (size_t i : picked)
    {
        std::error_code rmEc;
        if (fs::remove(paths[i], rmEc) && !rmEc)
        {
            freed += files[i].bytes;
            ++removed;
        }
    }

    uint64 kept = 0;
    uint32 live = 0;
    for (ActionLogFile const& f : files)
    {
        kept += f.bytes;
        if (f.live) ++live;
    }
    kept -= std::min(kept, freed);

    sLog.outString("[BotActionLog] cleanup: %u of %zu files removed (%.1f MB), %zu files / %.1f MB kept (%u open), limits %u days / %u MB",
        removed, files.size(), double(freed) / double(kActionLogMB),
        files.size() - removed, double(kept) / double(kActionLogMB), live,
        sPlayerbotAIConfig.actionLogMaxAgeDays, sPlayerbotAIConfig.actionLogMaxTotalMB);
}

void BotActionLog::Maintain()
{
    static bool everSwept = false;
    static int64 lastSweepSec = 0;
    static bool wasEnabled = false;
    static ActionLogScope lastScope = ActionLogScope::Commanded;
    static bool haveLastScope = false;

    bool const enabled = ai::botdiag::IsActionLogEnabled();
    ActionLogScope const scope = sPlayerbotAIConfig.actionLogScope;
    if (ActionLogNeedsCloseAll(wasEnabled, enabled, lastScope, scope, haveLastScope))
        CloseAll();
    wasEnabled = enabled;
    lastScope = scope;
    haveLastScope = true;

    int64 const nowSec = static_cast<int64>(std::time(nullptr));
    if (!ActionLogSweepDue(everSwept, nowSec - lastSweepSec, sWrittenSinceSweep.load(),
                           sPlayerbotAIConfig.actionLogMaxTotalMB))
        return;
    everSwept = true;
    lastSweepSec = nowSec;
    Sweep();
}

}}  // namespace ai::botdiag — close so wrappers below are at global
    // scope where the host's forward-declarations expect them.

// ============================================================================
// Host-code hook wrappers. game.lib (Unit.cpp, Spell.cpp, etc.) calls these
// without #including the cmangos/playerbots header chain. Symbols resolve
// at the final mangosd.exe link step — same pattern as SC_PHASE.
//
// Bot-only filtering is done HERE so the host-code hooks just call the
// wrapper unconditionally and pay only the (very cheap) bot-AI null check
// when the player is a real human.
// ============================================================================
using ai::botdiag::BotActionLog;

namespace {
    inline PlayerbotAI* AiFor(Unit* u) {
        if (!u || !u->IsPlayer()) return nullptr;
        return PlayerbotAIStorage::Instance().GetAI(static_cast<Player*>(u));
    }
}

void BotActionLog_LogAuraApply(Unit* target, uint32 spellId, int32 durationMs, uint64 casterGuidRaw)
{
    PlayerbotAI* ai = AiFor(target);
    if (!ai) return;
    BotActionLog::LogAuraApply(ai, spellId, durationMs, ObjectGuid(casterGuidRaw), /*force*/ true);
}

// AURA_ATTEMPT: top-of-AddSpellAuraHolder hook. Logs every incoming aura BEFORE early-returns
// (refresh, stack, dead, debuff-limit, etc). Lets us see what's *trying* to land on a bot,
// even when it gets merged into an existing holder. Tag is AURA_ATTEMPT so the log clearly
// distinguishes from the post-success AURA_APPLY tag.
void BotActionLog_LogAuraAttempt(Unit* target, uint32 spellId, int32 durationMs, uint64 casterGuidRaw)
{
    PlayerbotAI* ai = AiFor(target);
    if (!ai) return;
    SpellEntry const* info = sSpellMgr.GetSpellEntry(spellId);
    const char* spellName = info ? info->SpellName[0].c_str() : "?";
    BotActionLog::Write(ai, "AURA_ATTEMPT",
                        "spell=%s(%u) duration=%dms caster=0x%llx",
                        spellName, spellId, durationMs,
                        (unsigned long long)casterGuidRaw);
}

void BotActionLog_LogAuraRemove(Unit* target, uint32 spellId, uint64 casterGuidRaw)
{
    PlayerbotAI* ai = AiFor(target);
    if (!ai) return;
    BotActionLog::LogAuraRemove(ai, spellId, ObjectGuid(casterGuidRaw), /*force*/ true);
}

void BotActionLog_LogCastStart(WorldObject* caster, uint32 spellId, uint64 targetGuidRaw, uint32 castTimeMs)
{
    if (!caster || !caster->IsPlayer()) return;
    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(static_cast<Player*>(caster));
    if (!ai) return;
    BotActionLog::LogCastStart(ai, spellId, ObjectGuid(targetGuidRaw), castTimeMs);
}

void BotActionLog_LogCastResult(WorldObject* caster, uint32 spellId, uint8 result, const char* phase)
{
    if (!caster || !caster->IsPlayer()) return;
    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(static_cast<Player*>(caster));
    if (!ai) return;
    BotActionLog::LogCastResult(ai, spellId, result, phase);
}

void BotActionLog_LogDamage(Unit* attacker, Unit* victim, uint32 damage, uint32 spellId, const char* damageType)
{
    // Throttle: only log every Nth damage event per bot to avoid log-spam in
    // raid-trash scenarios. Per-bot counter via thread_local map keyed by guid.
    static thread_local std::unordered_map<uint64, uint32> sCounter;
    constexpr uint32 kSampleEvery = 5;

    PlayerbotAI* attackerAi = AiFor(attacker);
    PlayerbotAI* victimAi   = AiFor(victim);
    if (!attackerAi && !victimAi) return;

    auto bucket = [&](Unit* who) -> bool {
        uint64 g = who ? who->getObjectGuid().GetRawValue() : 0;
        return ((++sCounter[g]) % kSampleEvery) == 0;
    };

    if (attackerAi && bucket(attacker)) {
        BotActionLog::Write(attackerAi, "DMG_DEALT",
            "victim=0x%llx victimName=%s spell=%u dmg=%u type=%s",
            (unsigned long long)(victim ? victim->getObjectGuid().GetRawValue() : 0),
            victim ? victim->GetName() : "?",
            spellId, damage, damageType ? damageType : "?");
    }
    if (victimAi && bucket(victim)) {
        BotActionLog::Write(victimAi, "DMG_TAKEN",
            "attacker=0x%llx attackerName=%s spell=%u dmg=%u type=%s",
            (unsigned long long)(attacker ? attacker->getObjectGuid().GetRawValue() : 0),
            attacker ? attacker->GetName() : "?",
            spellId, damage, damageType ? damageType : "?");
    }
}
