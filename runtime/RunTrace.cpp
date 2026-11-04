#include "RunTrace.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <list>
#include <string_view>
#include <system_error>

#include "BotManager.h"
#include "PlayerbotAIStorage.h"
#include "../ai/playerbot/PlayerbotAI.h"
#include "../ai/playerbot/PlayerbotAIConfig.h"
#include "../ai/playerbot/ServerFacade.h"
#include "../ai/playerbot/strategy/Engine.h"
#include "../ai/playerbot/strategy/Value.h"
#include "../host/ModuleLog.h"
#include "../host/ModuleVersion.h"
#include "Creature.h"
#include "Group.h"
#include "ObjectMgr.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Spell.h"
#include "Threat/ThreatManager.h"

namespace TortoiseBots {

namespace {

constexpr size_t kQueueRecords = 8192;
constexpr size_t kQueueBytes = 4u * 1024u * 1024u;
// The scope may be empty this long (group disbanded, bots logged out) before
// the run ends by itself.
constexpr uint32_t kEmptyScopeMs = 10000;

// Reports every action outcome of one bot's engine to the service. Owned by the
// engine (deleted with it); it carries nothing but the bot's guid, so one that
// outlives a run only finds the service inactive.
class RunTraceListener : public ai::ActionExecutionListener
{
public:
    explicit RunTraceListener(uint64_t guid) : guid_(guid) {}
    bool Before(ai::Action*, const ai::Event&) override { return true; }
    bool AllowExecution(ai::Action*, const ai::Event&) override { return true; }
    void After(ai::Action*, bool, const ai::Event&) override {}
    bool OverrideResult(ai::Action*, bool executed, const ai::Event&) override { return executed; }
    void Outcome(ai::Action* action, ai::ActionOutcome outcome, const ai::Event& event, float, float relevance) override
    {
        RunTraceService& service = RunTraceService::Instance();
        if (!action || !service.Active())
            return;
        RunTrace::ActionOutcome mapped = RunTrace::ActionOutcome::Ok;
        switch (outcome)
        {
            case ai::ActionOutcome::Ok: mapped = RunTrace::ActionOutcome::Ok; break;
            case ai::ActionOutcome::Failed: mapped = RunTrace::ActionOutcome::Failed; break;
            case ai::ActionOutcome::Impossible: mapped = RunTrace::ActionOutcome::Impossible; break;
            case ai::ActionOutcome::Useless: mapped = RunTrace::ActionOutcome::Useless; break;
            case ai::ActionOutcome::Prereq: mapped = RunTrace::ActionOutcome::Prereq; break;
            case ai::ActionOutcome::Vetoed: mapped = RunTrace::ActionOutcome::Vetoed; break;
            case ai::ActionOutcome::Backoff: mapped = RunTrace::ActionOutcome::Backoff; break;
        }
        service.NoteAction(guid_, action->getName(), event.getSource(), mapped, relevance);
    }

private:
    uint64_t guid_;
};

char const* ClassToken(uint8 cls)
{
    switch (cls)
    {
        case CLASS_WARRIOR: return "warrior";
        case CLASS_PALADIN: return "paladin";
        case CLASS_HUNTER: return "hunter";
        case CLASS_ROGUE: return "rogue";
        case CLASS_PRIEST: return "priest";
        case CLASS_SHAMAN: return "shaman";
        case CLASS_MAGE: return "mage";
        case CLASS_WARLOCK: return "warlock";
        case CLASS_DRUID: return "druid";
        default: return "unknown";
    }
}

char const* RoleToken(Player* bot)
{
    if (PlayerbotAI::IsTank(bot))
        return "tank";
    if (PlayerbotAI::IsHeal(bot))
        return "heal";
    return "dps";
}

char StateLetter(PlayerbotAI* ai, Player* bot)
{
    if (!bot->IsAlive())
        return 'd';
    if (!ai)
        return 'n';
    switch (ai->GetState())
    {
        case BotState::BOT_STATE_COMBAT: return 'c';
        case BotState::BOT_STATE_DEAD: return 'd';
        case BotState::BOT_STATE_REACTION: return 'r';
        default: return 'n';
    }
}

uint32_t CurrentSpellId(Unit* unit)
{
    if (Spell* spell = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL))
        if (spell->m_spellInfo)
            return spell->m_spellInfo->Id;
    if (Spell* spell = unit->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
        if (spell->m_spellInfo)
            return spell->m_spellInfo->Id;
    return 0;
}

// Degrees (0..180) between where the bot faces and the direction to the unit.
int FacingDegrees(Player* bot, Unit* unit)
{
    constexpr float kTwoPi = 6.28318530718f;
    constexpr float kPi = 3.14159265359f;
    float d = std::fabs(bot->GetAngle(unit) - bot->GetOrientation());
    while (d > kTwoPi)
        d -= kTwoPi;
    if (d > kPi)
        d = kTwoPi - d;
    return static_cast<int>(d * 180.0f / kPi + 0.5f);
}

// The spell id of a harmful area effect the bot stands in: a negative aura from
// a dynamic object (Rain of Fire, Blizzard) or an area aura of someone else,
// with a periodic damage effect. Reads the bot's own aura list (a few dozen
// entries); 0 when none.
uint32_t HarmfulGroundAura(Player* bot)
{
    ObjectGuid const self = bot->GetObjectGuid();
    for (auto const& entry : bot->GetSpellAuraHolderMap())
    {
        SpellAuraHolder* holder = entry.second;
        if (!holder || holder->IsPositive() || holder->GetCasterGuid() == self)
            continue;
        if (!holder->IsPersistent() && !holder->IsAreaAura())
            continue;
        SpellEntry const* spell = holder->GetSpellProto();
        if (!spell)
            continue;
        for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
            if (spell->EffectApplyAuraName[i] == SPELL_AURA_PERIODIC_DAMAGE ||
                spell->EffectApplyAuraName[i] == SPELL_AURA_PERIODIC_DAMAGE_PERCENT)
                return spell->Id;
    }
    return 0;
}

// One of the bot's guid-list values, only when it already holds a calculated
// list (never a fresh calculation).
void ReadCachedGuidList(PlayerbotAI* ai, char const* name, std::list<ObjectGuid>& out)
{
    out.clear();
    ai::AiObjectContext* context = ai ? ai->GetAiObjectContext() : nullptr;
    if (!context || !context->HasValue(name))
        return;
    auto* value = dynamic_cast<ai::CalculatedValue<std::list<ObjectGuid>>*>(
        context->GetValue<std::list<ObjectGuid>>(name));
    if (value && value->HasCachedValue())
        out = value->LazyGet();
}

// A hostile the bot sees: in world on its map, alive, not friendly, detected.
Unit* SeenHostile(Player* bot, ObjectGuid guid)
{
    if (guid.IsEmpty() || guid == bot->GetObjectGuid())
        return nullptr;
    Map* map = bot->FindMap();
    Unit* unit = map ? map->GetUnit(guid) : nullptr;
    if (!unit || !unit->IsInWorld() || !unit->IsAlive())
        return nullptr;
    if (BotManager::Instance().IsBot(unit->GetObjectGuid()) || sServerFacade.IsFriendlyTo(unit, bot))
        return nullptr;
    if (!unit->IsVisibleForOrDetect(bot, bot->GetCamera().GetBody(), true))
        return nullptr;
    return unit;
}

} // namespace

RunTraceService& RunTraceService::Instance()
{
    static RunTraceService service;
    return service;
}

void RunTraceService::AttachListeners(PlayerbotAI* ai)
{
    if (!ai || !ai->GetBot())
        return;
    uint64_t const guid = ai->GetBot()->GetObjectGuid().GetRawValue();
    for (uint8 i = 0; i < static_cast<uint8>(BotState::BOT_STATE_ALL); ++i)
    {
        ai::Engine* engine = ai->GetEngineByState(static_cast<BotState>(i));
        if (engine && !engine->HasActionExecutionListener<RunTraceListener>())
            engine->AddActionExecutionListener(new RunTraceListener(guid));
    }
}

void RunTraceService::DetachListeners(PlayerbotAI* ai)
{
    if (!ai)
        return;
    for (uint8 i = 0; i < static_cast<uint8>(BotState::BOT_STATE_ALL); ++i)
        if (ai::Engine* engine = ai->GetEngineByState(static_cast<BotState>(i)))
            engine->RemoveActionExecutionListeners<RunTraceListener>();
}

// Called from Update(), on the world tick and outside every engine's action
// loop, so no listener list is being walked. A bot that logged out took its
// engines (and their listeners) with it.
void RunTraceService::DetachAllListeners()
{
    detachPending_ = false;
    for (uint64_t guid : attachedGuids_)
        if (Player* bot = ObjectAccessor::FindPlayer(ObjectGuid(guid)))
            DetachListeners(PlayerbotAIStorage::Instance().GetAI(bot));
    attachedGuids_.clear();
}

std::string RunTraceService::DirPath() const
{
    std::string dir = sPlayerbotAIConfig.runTraceDir;
    if (dir.empty())
        dir = "traces";
    std::filesystem::path const p(dir);
    if (p.is_absolute())
        return dir;
    return sPlayerbotAIConfig.logsDirPath() + dir;
}

bool RunTraceService::Start(StartParams const& params, std::string& message)
{
    if (char const* refusal = lifecycle_.StartRefusal())
    {
        message = std::string(refusal) == "finishing"
            ? "The previous trace (" + header_.runId + ") is still being written; try again in a moment."
            : "A trace is already running (" + header_.runId + "). Stop it first: .bot trace stop";
        return false;
    }
    if (char const* off = RunTrace::TraceOffReason(sPlayerbotAIConfig.enabled, sPlayerbotAIConfig.runTrace))
    {
        message = off;
        return false;
    }
    if (!params.mayRecord)
    {
        message = "Cannot start the trace: no permission check supplied.";
        return false;
    }
    if (!RunTrace::ValidRunId(params.runId))
    {
        message = "Invalid run id: use 1-64 letters, digits, '.', '_' or '-'.";
        return false;
    }

    std::string const dir = DirPath();
    std::filesystem::path const base = std::filesystem::path(dir) / params.runId;
    tracePath_ = base.string() + ".jsonl";
    summaryPath_ = base.string() + ".summary.json";
    std::error_code ec;
    if (std::filesystem::exists(tracePath_, ec))
    {
        message = "A trace named " + params.runId + " already exists; pick another run id.";
        return false;
    }

    Player* anchor = ObjectAccessor::FindPlayer(ObjectGuid(params.anchorGuid));

    header_ = RunTrace::Header();
    header_.runId = params.runId;
    header_.scope = params.wholeParty ? "party" : "bot";
    header_.scopeName = params.scopeName;
    header_.requester = params.requester;
    header_.tbVersion = BuildVersion();
    header_.startUnix = static_cast<uint64_t>(std::time(nullptr));
    header_.intervalMs = sPlayerbotAIConfig.runTraceIntervalMs;
    header_.maxBytes = static_cast<uint64_t>(sPlayerbotAIConfig.runTraceMaxFileMb) * 1024u * 1024u;
    header_.mapId = anchor ? anchor->GetMapId() : 0;
    header_.instanceId = anchor ? anchor->GetInstanceId() : 0;

    std::string err;
    if (!writer_.Open(tracePath_, header_.maxBytes, kQueueRecords, kQueueBytes, err))
    {
        message = "Cannot start the trace: " + err;
        return false;
    }

    builder_.reset(new RunTrace::Builder(params.runId));
    counters_ = RunTrace::Counters();
    coalescer_ = RunTrace::ActionCoalescer();
    loops_ = RunTrace::LoopDetector();
    digest_.Reset();
    digest_.SetDriver(params.driver);
    digest_.SetBuild(header_.tbVersion, CoreRevision().substr(0, 8));
    bossGuids_.clear();
    context_ = params.context;
    tracks_.clear();
    seenMobs_.clear();
    scopeGuids_.Clear();
    wholeParty_ = params.wholeParty;
    anchorGuid_ = params.anchorGuid;
    requesterGuid_ = params.requesterGuid;
    mayRecord_ = params.mayRecord;
    intervalMs_ = header_.intervalMs;
    elapsedMs_ = 0;
    frameAccumMs_ = 0;
    checkAccumMs_ = 0;
    emptyScopeMs_ = 0;
    RefreshContext();

    Emit(builder_->BuildHeader(header_));
    // The "running" summary is written by the writer thread too.
    writer_.PushFile(summaryPath_, builder_->BuildSummary(header_, "running", "", 0, 0, counters_,
        std::filesystem::path(tracePath_).filename().string(), std::string(),
        digest_.Render(RunTrace::RenderArgs{ false, std::string(), header_.startUnix, 0 })));
    lifecycle_.Start();
    lastFailure_.clear();
    active_.store(true, std::memory_order_relaxed);
    Sample();
    message = "Tracing " + header_.scope + " " + header_.scopeName + " as run " + params.runId + " -> " + tracePath_;
    TB_LOG_BASIC("TortoiseBots: run trace %s started (%s %s) -> %s", params.runId.c_str(),
        header_.scope.c_str(), header_.scopeName.c_str(), tracePath_.c_str());
    return true;
}

bool RunTraceService::Stop(char const* reason, std::string& message)
{
    if (!Active())
    {
        message = "No trace is running.";
        return false;
    }
    std::string const run = header_.runId;
    Finish(reason, "complete");
    message = "Trace " + run + " stopped (" + reason + "); the summary is written to " + summaryPath_;
    return true;
}

void RunTraceService::Initialize()
{
    // Module start, before the first world tick: a summary still "running"
    // belongs to a run the last server never finalised (a crash, or a shutdown
    // that let the writer go). Mark it "incomplete"; its trace file stays.
    RunTrace::ReconcileResult const r = RunTrace::ReconcileStaleSummaries(DirPath(),
        lifecycle_.Phase() == RunTrace::RunPhase::Idle ? std::string() : header_.runId,
        static_cast<uint64_t>(std::time(nullptr)));
    for (std::string const& run : r.marked)
        TB_LOG_BASIC("TortoiseBots: run trace %s was not finalised before the server stopped; summary marked incomplete",
            run.c_str());
    for (std::string const& error : r.errors)
        TB_LOG_BASIC("TortoiseBots: run trace summary not reconciled: %s", error.c_str());
}

void RunTraceService::Shutdown()
{
    if (Active())
        Finish("shutdown", "complete");
    if (lifecycle_.Phase() != RunTrace::RunPhase::Finishing)
        return;
    if (writer_.Finished())
    {
        CompleteFinish();
        return;
    }
    // Shutdown does not wait for the disk either. The writer owns everything it
    // touches (shared state, copies of the run's builder and header), so it is
    // let go: it writes the end record and the summary if it gets there before
    // the process ends; otherwise the next start marks the summary incomplete.
    writer_.Detach();
    TB_LOG_BASIC("TortoiseBots: run trace %s (%s): the writer was still busy at shutdown and is left to finish on its own",
        header_.runId.c_str(), finishReason_.c_str());
    lifecycle_.Finished();
}

void RunTraceService::CompleteFinish()
{
    writer_.Reap();
    RunTrace::Counters const c = writer_.FinalCounters();
    lastRunId_ = header_.runId;
    lastFailure_ = writer_.Failure();
    if (!lastFailure_.empty())
        TB_LOG_BASIC("TortoiseBots: run trace %s (%s) failed: %s", header_.runId.c_str(), finishReason_.c_str(),
            lastFailure_.c_str());
    TB_LOG_BASIC("TortoiseBots: run trace %s ended (%s): %llu frames, %llu actions, %llu events, %llu dropped",
        header_.runId.c_str(), finishReason_.c_str(), static_cast<unsigned long long>(c.frames),
        static_cast<unsigned long long>(c.actions), static_cast<unsigned long long>(c.events),
        static_cast<unsigned long long>(c.dropped));
    lifecycle_.Finished();
}

std::string RunTraceService::Status(bool detail) const
{
    if (lifecycle_.Phase() == RunTrace::RunPhase::Finishing)
        return detail ? "Trace " + header_.runId + " is ending (writing its end record and summary)."
                      : "A trace is ending.";
    if (!Active())
    {
        // A failed end is reported until the next run starts.
        if (!lastFailure_.empty())
            return detail ? "No trace is running. The last one (" + lastRunId_ + ") failed: " + lastFailure_
                          : "No trace is running. The last one failed: " + lastFailure_;
        return "No trace is running.";
    }
    if (!detail)
        return "A trace is running.";
    RunTrace::WriterStats const ws = writer_.Snapshot();
    return "Trace " + header_.runId + " running (" + header_.scope + " " + header_.scopeName + "): " +
        std::to_string(elapsedMs_ / 1000) + " s, " + std::to_string(counters_.frames) + " frames, " +
        std::to_string(counters_.actions) + " actions, " + std::to_string(counters_.events) + " events, " +
        std::to_string(ws.bytes / 1024) + " KiB written, " + std::to_string(ws.dropped) + " dropped.";
}

void RunTraceService::Update(uint32_t diff)
{
    worldMark_.MarkCurrent();
    if (lifecycle_.Phase() == RunTrace::RunPhase::Finishing && writer_.Finished())
        CompleteFinish();
    if (detachPending_)
        DetachAllListeners();
    if (!Active())
        return;
    // BotHostAdapter::OnUpdate calls this with the AI off too, so a run
    // cannot outlive a reload that switched either off.
    if (RunTrace::TraceOffReason(sPlayerbotAIConfig.enabled, sPlayerbotAIConfig.runTrace))
    {
        Finish("disabled", "complete");
        return;
    }
    elapsedMs_ += diff;
    frameAccumMs_ += diff;
    checkAccumMs_ += diff;
    FlushActions(false);

    if (checkAccumMs_ >= 1000)
    {
        checkAccumMs_ = 0;
        RefreshContext();
        RunTrace::WriterStats const ws = writer_.Snapshot();
        if (ws.failed)
        {
            Finish("write_failed", "aborted");
            return;
        }
        if (ws.truncated)
        {
            Finish("size_cap", "complete");
            return;
        }
    }
    if (frameAccumMs_ >= intervalMs_)
    {
        frameAccumMs_ -= intervalMs_;
        if (frameAccumMs_ >= intervalMs_)
            frameAccumMs_ = 0;  // a long stall: one frame, not a burst
        Sample();
    }
}

void RunTraceService::Emit(std::string line)
{
    writer_.Push(std::move(line));
}

void RunTraceService::EmitEvent(RunTrace::Event const& e)
{
    ++counters_.events;
    Emit(builder_->BuildEvent(elapsedMs_, e));
}

void RunTraceService::EmitAction(RunTrace::ActionRecord const& rec)
{
    ++counters_.actions;
    Emit(builder_->BuildAction(rec));
}

void RunTraceService::FlushActions(bool all)
{
    std::vector<RunTrace::ActionRecord> done;
    coalescer_.Flush(elapsedMs_, all, done);
    for (RunTrace::ActionRecord const& rec : done)
        EmitAction(rec);
}

void RunTraceService::NoteAction(uint64_t botGuid, std::string const& name, std::string const& source,
    RunTrace::ActionOutcome outcome, float relevance)
{
    if (!Active() || !worldMark_.IsCurrent() || !scopeGuids_.Contains(botGuid))
        return;
    RunTrace::ActionRecord rec;
    rec.guid = botGuid;
    rec.name = name;
    rec.source = source;
    rec.outcome = outcome;
    rec.relevance = relevance;
    rec.t = rec.tLast = elapsedMs_;
    RunTrace::ActionRecord out;
    if (coalescer_.Add(rec, out))
        EmitAction(out);

    if (RunTrace::OutcomeIsFailure(outcome))
    {
        uint32_t count = 0;
        if (loops_.Note(botGuid, name, elapsedMs_, &count))
        {
            ++counters_.loops;
            RunTrace::Event e;
            e.ev = "action_loop";
            e.a = botGuid;
            e.s = name;
            e.s2 = RunTrace::OutcomeName(outcome);
            e.v = count;
            EmitEvent(e);
        }
    }
}

void RunTraceService::NoteBotEvent(PlayerbotAI* ai, std::string const& eventName, std::string const& info1,
    std::string const& info2)
{
    if (!Active() || !worldMark_.IsCurrent() || !ai || !ai->GetBot())
        return;
    bool const item = eventName == "StoreLootAction";
    bool const money = eventName == "LootMoney";
    if (!item && !money)
        return;
    uint64_t const guid = ai->GetBot()->GetObjectGuid().GetRawValue();
    if (!scopeGuids_.Contains(guid))
        return;
    RunTrace::Event e;
    e.ev = "loot";
    e.a = guid;
    if (item)
    {
        e.s = info1;
        e.v = std::strtoll(info2.c_str(), nullptr, 10);
    }
    else
    {
        e.s = "money";
        e.v = std::strtoll(info1.c_str(), nullptr, 10);
    }
    EmitEvent(e);
}

void RunTraceService::NoteSpellInterrupted(PlayerbotAI* ai, uint32_t spellId)
{
    if (!Active() || !worldMark_.IsCurrent() || !ai || !ai->GetBot())
        return;
    Player* bot = ai->GetBot();
    uint64_t const guid = bot->GetObjectGuid().GetRawValue();
    if (!scopeGuids_.Contains(guid))
        return;
    RunTrace::Event e;
    e.ev = "cast_interrupted";
    e.a = guid;
    e.v = spellId;
    // The bot was moving when the cast was cancelled: its own movement (or the
    // order that started it) is the likely cause. A heuristic, not a reason code.
    if (sServerFacade.isMoving(bot))
        e.s = "moving";
    EmitEvent(e);
}

// Once a second (and at start and end): the instance map the party stands on,
// who drives the run and, until one is known, the plan whose bosses the outcome
// is judged against. In-memory reads only: the plan table is sealed at startup,
// creature names come from the cached templates.
void RunTraceService::RefreshContext()
{
    Player* anchor = ObjectAccessor::FindPlayer(ObjectGuid(anchorGuid_));
    if (!anchor)
        return;
    if (Map* map = anchor->FindMap())
        if (map->IsDungeon())
            digest_.NoteDungeon(map->GetId());
    if (!context_)
        return;
    RunTrace::Context ctx;
    if (!context_(requesterGuid_, anchor, !digest_.HasPlan(), ctx))
        return;
    digest_.NoteMode(ctx.mode);
    if (ctx.hasStep)
        digest_.NoteStep(ctx.leg, ctx.seq);
    digest_.NoteKnownBosses(ctx.knownBosses);
    if (digest_.HasPlan() || ctx.bossEntries.empty())
        return;
    std::vector<RunTrace::BossProgress> bosses;
    for (uint32_t entry : ctx.bossEntries)
    {
        RunTrace::BossProgress boss;
        boss.entry = entry;
        CreatureInfo const* info = sObjectMgr.GetCreatureTemplate(entry);
        boss.name = info && !info->name.empty() ? info->name : "Boss " + std::to_string(entry);
        bosses.push_back(std::move(boss));
    }
    digest_.NotePlan(ctx.planName, std::move(bosses));
}

// Boss units a bot saw alive: found dead by guid on the bot's map (a corpse
// stays in the map's object store for a while), once per frame. A lookup per
// tracked boss, never a scan.
void RunTraceService::CheckBossKills(std::vector<Player*> const& bots)
{
    if (bossGuids_.empty() || bots.empty())
        return;
    for (auto it = bossGuids_.begin(); it != bossGuids_.end();)
    {
        // The map the boss stands in: that of a bot in the same instance.
        Map* map = nullptr;
        for (Player* bot : bots)
        {
            if (bot->GetMapId() != it->second.map || bot->GetInstanceId() != it->second.instance)
                continue;
            if ((map = bot->FindMap()))
                break;
        }
        Unit* unit = map ? map->GetUnit(ObjectGuid(it->first)) : nullptr;
        if (unit && unit->IsInWorld() && !unit->IsAlive())
        {
            if (digest_.NoteBossKill(it->second.entry, elapsedMs_))
            {
                RunTrace::Event e;
                e.ev = "boss_kill";
                e.a = it->first;
                e.v = it->second.entry;
                e.s = unit->GetName();
                EmitEvent(e);
            }
            it = bossGuids_.erase(it);
        }
        else
            ++it;
    }
}

void RunTraceService::ResolveBots(std::vector<Player*>& bots)
{
    bots.clear();
    Player* anchor = ObjectAccessor::FindPlayer(ObjectGuid(anchorGuid_));
    if (!anchor)
        return;

    // Every controllable bot of the group (or the one bot), then the permission
    // filter: the group changes while a run lasts, the requester's rights over
    // its members must hold at every frame, not only at the start.
    static std::vector<Player*> found;
    found.clear();
    Group* group = anchor->GetGroup();
    RunTrace::ScopeSource const source = RunTrace::PickScopeSource(wholeParty_, group != nullptr,
        BotManager::Instance().IsControllableBot(anchor));
    if (source == RunTrace::ScopeSource::Group)
    {
        for (Group::MemberSlot const& slot : group->GetMemberSlots())
        {
            Player* member = ObjectAccessor::FindPlayer(slot.guid);
            if (member && member->IsInWorld() && BotManager::Instance().IsControllableBot(member))
                found.push_back(member);
        }
    }
    else if (source == RunTrace::ScopeSource::Anchor)
        found.push_back(anchor);

    std::vector<RunTrace::ScopeCandidate> candidates;
    candidates.reserve(found.size());
    bool const online = ObjectAccessor::FindPlayer(ObjectGuid(requesterGuid_)) != nullptr;
    for (Player* member : found)
        candidates.push_back({ member->GetObjectGuid().GetRawValue(),
            online && mayRecord_ && mayRecord_(requesterGuid_, member) });
    // mayRecord_ already includes the GameMaster exemption.
    for (size_t index : RunTrace::SelectScope(online, candidates))
        bots.push_back(found[index]);
}

void RunTraceService::Sample()
{
    static std::vector<Player*> bots;
    static std::vector<RunTrace::BotSample> botSamples;
    static std::vector<RunTrace::MobSample> mobSamples;
    static std::unordered_set<uint64_t> frameMobs;
    static std::list<ObjectGuid> cached;
    static std::vector<RunTrace::MemberState> members;

    ResolveBots(bots);
    // The hooks record only for this frame's bots: replace the set before the
    // empty check, so an empty frame stops their records too.
    static std::vector<uint64_t> scopeList;
    scopeList.clear();
    for (Player* bot : bots)
        scopeList.push_back(bot->GetObjectGuid().GetRawValue());
    scopeGuids_.Replace(scopeList);
    if (bots.empty())
    {
        emptyScopeMs_ += intervalMs_;
        if (emptyScopeMs_ >= kEmptyScopeMs)
            Finish("scope_lost", "complete");
        return;
    }
    emptyScopeMs_ = 0;

    botSamples.clear();
    mobSamples.clear();
    frameMobs.clear();
    members.clear();

    for (Player* bot : bots)
    {
        uint64_t const guid = bot->GetObjectGuid().GetRawValue();
        PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
        AttachListeners(ai);
        attachedGuids_.insert(guid);
        BotTrack& track = tracks_[guid];
        bool const alive = bot->IsAlive();
        bool const inCombat = bot->IsInCombat();

        auto fallSpot = [&]()
        {
            RunTrace::Spot spot;
            spot.tMs = elapsedMs_;
            spot.map = bot->GetMapId();
            spot.x = bot->GetPositionX();
            spot.y = bot->GetPositionY();
            spot.z = bot->GetPositionZ();
            spot.bot = track.name;
            spot.role = track.role;
            return spot;
        };

        if (!track.known)
        {
            RunTrace::PartyMember member;
            member.guid = guid;
            member.name = bot->GetName();
            member.cls = ClassToken(bot->GetClass());
            member.role = RoleToken(bot);
            member.level = bot->GetLevel();
            ++counters_.party;
            Emit(builder_->BuildParty(elapsedMs_, member));
            track.name = member.name;
            track.role = member.role;
            digest_.NoteParty({ member.cls, member.role, member.level });
            if (!alive)
                track.lastDeath = fallSpot();  // first seen dead: the wipe spot still needs a place
            track.known = true;
            track.alive = alive;
            track.inCombat = inCombat;
        }
        else
        {
            if (track.alive && !alive)
            {
                ++counters_.deaths;
                track.lastDeath = fallSpot();
                digest_.NoteDeath(track.lastDeath);
                RunTrace::Event e;
                e.ev = "death";
                e.a = guid;
                e.b = track.lastAttacker;
                EmitEvent(e);
            }
            if (track.inCombat != inCombat)
            {
                RunTrace::Event e;
                e.ev = inCombat ? "combat_on" : "combat_off";
                e.a = guid;
                EmitEvent(e);
            }
            track.alive = alive;
            track.inCombat = inCombat;
        }
        track.lastAttacker = 0;

        RunTrace::BotSample s;
        s.guid = guid;
        s.alive = alive;
        s.inCombat = inCombat;
        s.state = StateLetter(ai, bot);
        s.hp = bot->GetHealth();
        s.maxHp = bot->GetMaxHealth();
        Powers const powerType = bot->GetPowerType();
        s.powerType = static_cast<uint8_t>(powerType);
        s.power = bot->GetPower(powerType);
        s.maxPower = bot->GetMaxPower(powerType);
        s.x = bot->GetPositionX();
        s.y = bot->GetPositionY();
        s.z = bot->GetPositionZ();
        ObjectGuid target = bot->GetTargetGuid();
        if (target.IsEmpty())
            if (Unit* victim = bot->GetVictim())
                target = victim->GetObjectGuid();
        s.target = target.GetRawValue();
        s.casting = CurrentSpellId(bot);
        s.moving = sServerFacade.isMoving(bot);
        s.fire = HarmfulGroundAura(bot);
        if (!target.IsEmpty())
            if (Map* map = bot->FindMap())
                if (Unit* targetUnit = map->GetUnit(target))
                    if (targetUnit != bot && targetUnit->IsInWorld() && !sServerFacade.IsFriendlyTo(targetUnit, bot))
                        s.facing = FacingDegrees(bot, targetUnit);
        if (ai)
        {
            if (ai::Engine* engine = ai->GetCurrentEngine())
            {
                s.action = engine->GetLastAction();
                std::string strategy;
                for (std::string_view name : engine->GetStrategies())
                {
                    if (!strategy.empty())
                        strategy += ',';
                    strategy.append(name.data(), name.size());
                }
                if (strategy != track.strategy)
                {
                    track.strategy = strategy;
                    s.strategy = strategy;
                }
            }
        }
        botSamples.push_back(std::move(s));
        RunTrace::MemberState member;
        member.alive = alive;
        member.lastDeath = track.lastDeath;
        members.push_back(std::move(member));
    }
    digest_.NoteFrame(elapsedMs_, members);

    // Hostiles: what each bot fights, targets, or is attacked by.
    auto addHostile = [&](Player* bot, ObjectGuid guid)
    {
        if (mobSamples.size() >= RunTrace::kMaxMobsPerFrame || guid.IsEmpty() ||
            frameMobs.count(guid.GetRawValue()))
            return;
        Unit* unit = SeenHostile(bot, guid);
        if (!unit || !frameMobs.insert(guid.GetRawValue()).second)
            return;
        uint64_t const raw = guid.GetRawValue();
        if (bossGuids_.size() < RunTrace::kMaxBosses * 2 && !bossGuids_.count(raw))
        {
            // The plan's boss, a boss of a plan on this map, or any world-boss rank unit: its death is a boss kill.
            Creature const* creature = unit->ToCreature();
            CreatureInfo const* info = creature ? creature->GetCreatureInfo() : nullptr;
            uint32_t const entry = unit->GetEntry();
            if (digest_.IsTrackedBoss(entry, info && info->rank == CREATURE_ELITE_WORLDBOSS))
            {
                digest_.NoteSeenBoss(entry, unit->GetName());
                bossGuids_[raw] = BossUnit{ entry, bot->GetMapId(), bot->GetInstanceId() };
            }
        }
        if (seenMobs_.size() < RunTrace::kMaxRegistryMobs && seenMobs_.insert(raw).second)
        {
            RunTrace::Mob mob;
            mob.guid = raw;
            mob.entry = unit->GetEntry();
            mob.name = unit->GetName();
            mob.level = unit->GetLevel();
            if (Creature* creature = unit->ToCreature())
                if (CreatureInfo const* info = creature->GetCreatureInfo())
                {
                    mob.rank = info->rank;
                    mob.boss = info->rank == CREATURE_ELITE_WORLDBOSS;
                }
            ++counters_.mobs;
            Emit(builder_->BuildMobs(elapsedMs_, mob));
        }
        RunTrace::MobSample m;
        m.guid = raw;
        m.hpPct = unit->GetMaxHealth() ? static_cast<uint32_t>(unit->GetHealth() * 100ull / unit->GetMaxHealth()) : 0;
        m.x = unit->GetPositionX();
        m.y = unit->GetPositionY();
        m.z = unit->GetPositionZ();
        m.target = unit->GetTargetGuid().GetRawValue();
        m.casting = CurrentSpellId(unit);
        m.inCombat = unit->IsInCombat();
        Creature* creature = unit->ToCreature();
        m.evading = creature && creature->IsInEvadeMode();
        m.cc = unit->HasUnitState(UNIT_STAT_STUNNED | UNIT_STAT_CONFUSED | UNIT_STAT_FLEEING | UNIT_STAT_ROOT);
        if (unit->IsInCombat() && unit->CanHaveThreatList())
        {
            ThreatList const& list = unit->GetThreatManager().getThreatList();
            float top = 0.0f;
            for (HostileReference const* ref : list)
                if (ref && ref->getThreat() > top)
                    top = ref->getThreat();
            if (top > 0.0f)
            {
                std::vector<std::pair<float, uint64_t>> entries;
                for (HostileReference const* ref : list)
                    if (ref)
                        entries.emplace_back(ref->getThreat(), ref->getUnitGuid().GetRawValue());
                std::sort(entries.begin(), entries.end(),
                    [](std::pair<float, uint64_t> const& a, std::pair<float, uint64_t> const& b) { return a.first > b.first; });
                for (size_t i = 0; i < entries.size() && i < RunTrace::kThreatTopN; ++i)
                    m.threat.push_back({ entries[i].second, static_cast<uint32_t>(entries[i].first * 100.0f / top + 0.5f) });
            }
        }
        mobSamples.push_back(std::move(m));
    };

    for (Player* bot : bots)
    {
        if (!bot->IsInWorld() || !bot->IsAlive())
            continue;
        if (Unit* victim = bot->GetVictim())
            addHostile(bot, victim->GetObjectGuid());
        addHostile(bot, bot->GetTargetGuid());
        for (Unit* attacker : bot->GetAttackers())
            if (attacker)
                addHostile(bot, attacker->GetObjectGuid());
        ReadCachedGuidList(PlayerbotAIStorage::Instance().GetAI(bot), "attackers", cached);
        for (ObjectGuid const& guid : cached)
            addHostile(bot, guid);
    }

    CheckBossKills(bots);

    // Whom each hostile has on its target, for the next frame's death record.
    for (RunTrace::MobSample const& m : mobSamples)
    {
        auto it = tracks_.find(m.target);
        if (it != tracks_.end() && it->second.lastAttacker == 0)
            it->second.lastAttacker = m.guid;
    }

    ++counters_.frames;
    Emit(builder_->BuildFrame(elapsedMs_, botSamples, mobSamples));
}

void RunTraceService::Finish(char const* reason, char const* status)
{
    active_.store(false, std::memory_order_relaxed);
    if (!lifecycle_.BeginFinish())
        return;  // not running: already ending
    finishReason_ = reason;
    FlushActions(true);
    if (std::string_view(reason) != "shutdown")  // the command glue's tables may be gone at shutdown
        RefreshContext();

    // The writer thread ends the file: when it reaches this marker every line
    // queued before it is written (or dropped), so the end record's records /
    // bytes / dropped / truncated count the lines before it, and the summary is
    // built from the same counters. The job carries copies only.
    RunTrace::EndJob job;
    job.counters = counters_;
    RunTrace::Builder const builder = *builder_;
    RunTrace::Header const header = header_;
    uint32_t const elapsed = elapsedMs_;
    uint64_t const endUnix = static_cast<uint64_t>(std::time(nullptr));
    std::string const endReason = reason;
    std::string const endStatus = status;
    std::string const traceFile = std::filesystem::path(tracePath_).filename().string();
    // Rendered once, here; the writer thread gets the text.
    std::string const digest = digest_.Render(RunTrace::RenderArgs{ true, endReason, header.startUnix, endUnix });
    job.buildEnd = [builder, elapsed, endReason](RunTrace::Counters const& c)
        {
            RunTrace::EndInfo end;
            end.reason = endReason;
            end.counters = c;
            return builder.BuildEnd(elapsed, end);
        };
    job.buildSummary = [builder, header, elapsed, endUnix, endReason, endStatus, traceFile, digest](
        RunTrace::Counters const& c, std::string const& failure)
        {
            return builder.BuildSummary(header, failure.empty() ? endStatus.c_str() : "failed", endReason, endUnix,
                elapsed, c, traceFile, failure, digest);
        };
    job.summaryPath = summaryPath_;
    if (!writer_.PushEnd(std::move(job)))
    {
        // No writer thread took it (never opened): nothing to wait for.
        writer_.Reap();
        lifecycle_.Finished();
    }

    detachPending_ = true;
    scopeGuids_.Clear();
    tracks_.clear();
    seenMobs_.clear();
    bossGuids_.clear();
    builder_.reset();
}

} // namespace TortoiseBots
