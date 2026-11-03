#pragma once

// Run trace (opt-in): records what a party of bots did during one run as a
// bt-trace v1 JSONL file plus a <run_id>.summary.json, for offline scoring by
// tools/trace_score.py. Schema: docs/server/run-trace.md. Records and decisions:
// runtime/RunTraceRecords.h; file writer: runtime/RunTraceWriter.h.
//
// Off by default (AiPlayerbot.RunTrace.Enabled = 0). Started and stopped by
// `.bot trace start <run_id> [party|bot <Name>] [driver=<model>:<effort>:<run>]` / `.bot trace stop [<run_id>]` (a GM, or
// the owner of every bot in scope, unless AiPlayerbot.RunTrace.GmOnly); one run
// at a time. While no run is active the only costs are one relaxed atomic load
// per world tick, and one walk over the engine's listener list per action in
// Engine::DoNextAction, which is empty again once a run's listeners are
// detached (the world tick after the run ends).
//
// Threading: the whole recorder runs on the world thread (the bots' AI, the
// command queue and Update() all do; see docs/developers/architecture.md).
// Every disk write after the file is opened is off it: lines, the summary and
// the end record go through a bounded queue to the writer thread (lines are
// dropped and counted when it is full), and ending a run only queues the end
// marker; Update() polls for the writer to be done. Shutdown does not wait
// either: a busy writer is let go and Initialize() reconciles its summary at
// the next start. Nothing here reads the database or scans the world;
// hostiles come from the bots' own attacker lists and are resolved by guid on
// the bot's map.

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "RunTraceRecords.h"
#include "RunTraceSummary.h"
#include "RunTraceWriter.h"
#include "WorldThreadQueue.h"

class Player;
class PlayerbotAI;
class Unit;

namespace TortoiseBots {

class RunTraceService
{
public:
    static RunTraceService& Instance();

    // Cheap check for hook sites.
    bool Active() const { return active_.load(std::memory_order_relaxed); }

    struct StartParams
    {
        std::string runId;
        bool wholeParty = true;     // false: only the bot `anchorGuid`
        uint64_t anchorGuid = 0;    // a member of the group to trace, or the bot
        std::string scopeName;      // shown in the header and the status
        std::string requester;      // name, for the header
        uint64_t requesterGuid = 0; // who may stop it (besides a GM)
        // True while the requester may record this bot (a GM, controls it, or
        // commands its group: RunTrace::MayRecordBot). Called for every bot of
        // the group at start and at every frame.
        bool (*mayRecord)(uint64_t requesterGuid, Player* bot) = nullptr;
        // `driver=<model>:<effort>:<run>` of the command, already checked
        // (RunTrace::DriverRefusal); empty = not given. Self-reported.
        std::string driver;
        // What the command glue knows about the run: who drives it (mode) and,
        // when `wantPlan`, the plan whose bosses the outcome is judged against.
        // Called at start and once a second on the world thread.
        bool (*context)(uint64_t requesterGuid, Player* anchor, bool wantPlan, RunTrace::Context& out) = nullptr;
    };

    // World thread. False with `message` filled when the run cannot start.
    bool Start(StartParams const& params, std::string& message);
    // World thread. False when no run is active. Returns at once: the writer
    // thread writes the end record and the summary.
    bool Stop(char const* reason, std::string& message);
    // `detail`: name the run id and scope (RunTrace::MaySeeRunDetail).
    std::string Status(bool detail) const;
    RunTrace::RunPhase Phase() const { return lifecycle_.Phase(); }
    std::string const& RunId() const { return header_.runId; }
    uint64_t RequesterGuid() const { return requesterGuid_; }

    // World thread, every world tick.
    void Update(uint32_t diff);
    // Module start, before the first world tick: marks the summaries of runs
    // the last server never finalised "incomplete" (RunTrace::ReconcileStaleSummaries).
    void Initialize();
    // Module shutdown: ends a run in progress without waiting for the disk; a
    // writer still busy is let go (the next start reconciles its summary).
    void Shutdown();

    // Hooks (world thread; callers check Active() first).
    void NoteAction(uint64_t botGuid, std::string const& name, std::string const& source,
        RunTrace::ActionOutcome outcome, float relevance);
    void NoteBotEvent(PlayerbotAI* ai, std::string const& eventName, std::string const& info1,
        std::string const& info2);

    // PlayerbotAI::SpellInterrupted: one of the bot's own casts was cancelled.
    void NoteSpellInterrupted(PlayerbotAI* ai, uint32_t spellId);

    // Attaches the action listener to the bot's engines (once per engine).
    static void AttachListeners(PlayerbotAI* ai);
    // Removes them again (world tick, outside every engine loop).
    static void DetachListeners(PlayerbotAI* ai);

private:
    RunTraceService() = default;

    struct BotTrack
    {
        bool known = false;     // party record written
        bool alive = true;
        bool inCombat = false;
        std::string name;       // for the summary's deaths
        std::string role;
        RunTrace::Spot lastDeath;  // where it last died (the wipe spot)
        std::string strategy;   // last strategy string written
        uint64_t lastAttacker = 0;  // a hostile that targeted it in the previous frame
    };

    void Sample();
    void RefreshContext();
    void CheckBossKills(std::vector<Player*> const& bots);
    void ResolveBots(std::vector<Player*>& bots);
    void Emit(std::string line);
    void EmitEvent(RunTrace::Event const& e);
    void EmitAction(RunTrace::ActionRecord const& rec);
    void FlushActions(bool all);
    void Finish(char const* reason, char const* status);
    void CompleteFinish();
    void DetachAllListeners();
    std::string DirPath() const;

    std::atomic<bool> active_{false};  // lifecycle_ is Running (the hooks read this)
    RunTrace::RunLifecycle lifecycle_;
    std::string finishReason_;
    std::string lastRunId_;
    std::string lastFailure_;  // why the last run's end failed; shown by Status()
    bool detachPending_ = false;
    std::unordered_set<uint64_t> attachedGuids_;  // bots whose engines carry the listener
    // Hooks that run off the world thread are ignored: the recorder state is not locked.
    WorldThreadMark worldMark_;
    std::unique_ptr<RunTrace::Builder> builder_;
    RunTrace::RunTraceWriter writer_;
    RunTrace::Header header_;
    RunTrace::Counters counters_;
    RunTrace::ActionCoalescer coalescer_;
    RunTrace::LoopDetector loops_;
    RunTrace::RunDigest digest_;  // what the summary says besides the counters (RunTraceSummary.h)
    // Boss units seen alive and not yet found dead, with the instance they stand in (the bots may be split across maps).
    struct BossUnit
    {
        uint32_t entry = 0;
        uint32_t map = 0;
        uint32_t instance = 0;
    };
    std::unordered_map<uint64_t, BossUnit> bossGuids_;
    bool (*context_)(uint64_t requesterGuid, Player* anchor, bool wantPlan, RunTrace::Context& out) = nullptr;
    std::string tracePath_;
    std::string summaryPath_;
    bool wholeParty_ = true;
    uint64_t anchorGuid_ = 0;
    uint64_t requesterGuid_ = 0;
    bool (*mayRecord_)(uint64_t requesterGuid, Player* bot) = nullptr;
    uint32_t elapsedMs_ = 0;
    uint32_t frameAccumMs_ = 0;
    uint32_t checkAccumMs_ = 0;
    uint32_t emptyScopeMs_ = 0;
    uint32_t intervalMs_ = RunTrace::kDefaultIntervalMs;
    std::unordered_map<uint64_t, BotTrack> tracks_;
    std::unordered_set<uint64_t> seenMobs_;
    RunTrace::ScopeSet scopeGuids_;  // read by NoteAction on the world thread only
};

} // namespace TortoiseBots
