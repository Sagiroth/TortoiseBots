#include "WhyTrace.h"

#include <algorithm>
#include <cstdio>
#include <string_view>

#include "PlayerbotAIStorage.h"
#include "../ai/playerbot/PlayerbotAI.h"
#include "../ai/playerbot/PlayerbotAIConfig.h"
#include "../ai/playerbot/BotState.h"
#include "../ai/playerbot/strategy/AiObjectContext.h"
#include "../ai/playerbot/strategy/Engine.h"
#include "../ai/playerbot/strategy/Value.h"
#include "../ai/playerbot/strategy/actions/GenericSpellActions.h"
#include "../host/ModuleLog.h"
#include "Chat.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "SpellDefines.h"
#include "Timer.h"
#include "Unit.h"

namespace TortoiseBots
{

namespace
{

char const* const kPrefix = "TBWHY|";

WhyTrace::CastCheck CastCheckOf(uint32 result)
{
    switch (result)
    {
        case SPELL_FAILED_NOT_READY: return WhyTrace::CastCheck::NotReady;
        case SPELL_FAILED_OUT_OF_RANGE:
        case SPELL_FAILED_TOO_CLOSE: return WhyTrace::CastCheck::OutOfRange;
        case SPELL_FAILED_BAD_TARGETS: return WhyTrace::CastCheck::BadTargets;
        default: return WhyTrace::CastCheck::None;
    }
}

std::string GuidText(ObjectGuid guid)
{
    char text[24];
    std::snprintf(text, sizeof(text), "0x%llx", static_cast<unsigned long long>(guid.GetRawValue()));
    return text;
}

// Reads one action outcome and hands the service what only the live engine and
// bot know: the cast refusal behind an isPossible() no, the spell cooldown, the
// global cooldown, and whether an attached strategy still defines the action.
class WhyTraceListener : public ai::ActionExecutionListener
{
public:
    WhyTraceListener(uint64_t botRaw, PlayerbotAI* ai, ai::Engine* engine)
        : botRaw_(botRaw), ai_(ai), engine_(engine) {}

    bool Before(ai::Action*, const ai::Event&) override { return true; }
    bool AllowExecution(ai::Action*, const ai::Event&) override { return true; }
    void After(ai::Action*, bool, const ai::Event&) override {}
    bool OverrideResult(ai::Action*, bool executed, const ai::Event&) override { return executed; }

    void Outcome(ai::Action* action, ai::ActionOutcome outcome, const ai::Event&, float, float relevance) override
    {
        if (!action)
            return;
        WhyTraceService& service = WhyTraceService::Instance();
        if (!service.Active())
            return;

        WhyTraceService::Verdict verdict;
        verdict.name = action->getName();
        verdict.engineState = engine_ ? static_cast<uint8_t>(engine_->GetState()) : static_cast<uint8_t>(0);
        verdict.relevance = relevance;
        switch (outcome)
        {
            case ai::ActionOutcome::Ok: verdict.chosen = true; break;
            case ai::ActionOutcome::Failed: verdict.failed = true; break;
            case ai::ActionOutcome::Impossible: verdict.possible = false; break;
            case ai::ActionOutcome::Useless: verdict.useful = false; break;
            case ai::ActionOutcome::Prereq: verdict.prereq = true; break;
            case ai::ActionOutcome::Vetoed: verdict.vetoed = true; break;
            case ai::ActionOutcome::Backoff: verdict.backedOff = true; break;
        }

        if (!verdict.chosen)
        {
            verdict.hasTarget = action->GetTarget() != nullptr;
            verdict.strategyActive = engine_ ? engine_->HasActionNode(verdict.name) : true;
            // NoteOutcome stores the cast check of a FAILED/IMPOSSIBLE verdict
            // just before this listener runs.
            if (outcome == ai::ActionOutcome::Impossible && engine_)
                verdict.cast = CastCheckOf(engine_->GetLastOutcomeCastFail());
            if (ai_)
            {
                verdict.gcdRemainingMs = ai_->SpellGlobalCooldownRemainingMs();
                Player* bot = ai_->GetBot();
                if (bot)
                    if (ai::CastSpellAction* cast = dynamic_cast<ai::CastSpellAction*>(action))
                        if (uint32 spellId = cast->GetSpellID())
                            verdict.spellCooldownMs =
                                static_cast<uint32_t>(bot->GetSpellCooldownDelay(spellId)) * 1000u;
            }
        }

        service.NoteVerdict(botRaw_, verdict);
    }

private:
    uint64_t botRaw_;
    PlayerbotAI* ai_;
    ai::Engine* engine_;
};

std::string JoinStrategies(ai::Engine* engine)
{
    if (!engine)
        return std::string();
    std::string joined;
    for (std::string_view name : engine->GetStrategies())
    {
        if (!joined.empty())
            joined += ',';
        joined += name;
    }
    return joined;
}

} // namespace

WhyTraceService& WhyTraceService::Instance()
{
    static WhyTraceService service;
    return service;
}

WhyTraceService::TickScope::~TickScope()
{
    WhyTraceService::Instance().FinishTick(ai_);
}

void WhyTraceService::AttachListeners(PlayerbotAI* ai)
{
    if (!ai || !ai->GetBot())
        return;
    uint64_t const botRaw = ai->GetBot()->GetObjectGuid().GetRawValue();
    for (uint8 i = 0; i < static_cast<uint8>(BotState::BOT_STATE_ALL); ++i)
    {
        ai::Engine* engine = ai->GetEngineByState(static_cast<BotState>(i));
        if (engine && !engine->HasActionExecutionListener<WhyTraceListener>())
            engine->AddActionExecutionListener(new WhyTraceListener(botRaw, ai, engine));
    }
}

void WhyTraceService::DetachListeners(PlayerbotAI* ai)
{
    if (!ai)
        return;
    for (uint8 i = 0; i < static_cast<uint8>(BotState::BOT_STATE_ALL); ++i)
        if (ai::Engine* engine = ai->GetEngineByState(static_cast<BotState>(i)))
            engine->RemoveActionExecutionListeners<WhyTraceListener>();
}

bool WhyTraceService::Start(Player* bot, unsigned seconds, Player* requester, std::string& message)
{
    uint32_t const now = WorldTimer::getMSTime();
    if (session_.active())
    {
        message = "A trace is already running (" + botName_ + ", " +
            std::to_string(session_.RemainingMs(now) / 1000 + 1) + " s left). One trace at a time.";
        return false;
    }
    if (!sPlayerbotAIConfig.enabled)
    {
        message = "Bots are off (AiPlayerbot.Enabled = 0); there is no AI to trace.";
        return false;
    }
    if (!bot || !bot->IsInWorld())
    {
        message = "That bot is not in the world.";
        return false;
    }
    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    if (!ai)
    {
        message = std::string(bot->GetName()) + " has no AI controller.";
        return false;
    }

    botGuid_ = bot->GetObjectGuid().GetRawValue();
    botName_ = bot->GetName();
    requesterGuid_ = requester ? requester->GetObjectGuid().GetRawValue() : 0;
    requesterName_ = requester ? requester->GetName() : std::string("console");
    if (!session_.Start(now, seconds))
    {
        message = "A trace is already running.";
        return false;
    }
    budget_ = WhyTrace::LineBudget();
    verdicts_.clear();
    tickStart_.clear();
    engineStrategies_.clear();
    strategyCheckAccumMs_ = 0;
    attachedAi_ = ai;
    AttachListeners(ai);

    TB_LOG_BASIC("%sstart|%s|seconds=%u|by=%s", kPrefix, botName_.c_str(), seconds, requesterName_.c_str());
    EmitStrategies(ai, now);
    message = "Tracing " + botName_ + "'s AI for " + std::to_string(seconds) +
        " s: every tick goes to the server log with prefix " + kPrefix +
        " (target, victim, chosen action, top five that lost and why). The summary comes here when it ends.";
    return true;
}

void WhyTraceService::Stop(WhyTrace::StopReason reason)
{
    if (!session_.active())
        return;
    uint32_t const now = WorldTimer::getMSTime();

    std::string chosenTally;
    for (auto const& entry : session_.chosenTallies())
    {
        if (!chosenTally.empty())
            chosenTally += ',';
        chosenTally += WhyTrace::ClipName(entry.first) + ":" + std::to_string(entry.second);
    }
    std::string lostTally;
    for (auto const& entry : session_.lossTallies())
    {
        if (!lostTally.empty())
            lostTally += ',';
        lostTally += std::string(WhyTrace::LossReasonName(entry.first)) + ":" + std::to_string(entry.second);
    }
    uint32_t const ticks = session_.ticks();
    session_.Stop(reason, now);
    std::string const summary = session_.Summary(botName_);

    TB_LOG_BASIC("%send|%s|reason=%s|ticks=%u|dropped=%u|chosen=%s|lost=%s", kPrefix,
        botName_.c_str(), WhyTrace::StopReasonName(reason), ticks, budget_.dropped(),
        chosenTally.empty() ? "-" : chosenTally.c_str(), lostTally.empty() ? "-" : lostTally.c_str());
    TB_LOG_BASIC("TortoiseBots: why trace ended: %s", summary.c_str());
    NotifyRequester(summary);

    if (attachedAi_)
    {
        Player* bot = ObjectAccessor::FindPlayer(ObjectGuid(botGuid_));
        if (bot && PlayerbotAIStorage::Instance().GetAI(bot) == attachedAi_)
            DetachListeners(attachedAi_);
    }
    Reset();
}

void WhyTraceService::Shutdown()
{
    // Stop() detaches only from an AI the bot still resolves to; a bot already
    // torn down takes its listener with it, so a stale pointer is never used.
    Stop(WhyTrace::StopReason::ServerShutdown);
}

void WhyTraceService::Reset()
{
    botGuid_ = 0;
    botName_.clear();
    attachedAi_ = nullptr;
    requesterGuid_ = 0;
    requesterName_.clear();
    verdicts_.clear();
    tickStart_.clear();
    engineStrategies_.clear();
    strategyCheckAccumMs_ = 0;
}

void WhyTraceService::NotifyRequester(std::string const& text)
{
    if (!requesterGuid_)
        return;
    if (Player* requester = ObjectAccessor::FindPlayer(ObjectGuid(requesterGuid_)))
        ChatHandler(requester).PSendSysMessage("%s", text.c_str());
}

bool WhyTraceService::Emit(std::string const& line, uint32_t nowMs)
{
    if (!budget_.Take(nowMs))
        return false;
    TB_LOG_BASIC("%s", line.c_str());
    return true;
}

void WhyTraceService::EmitStrategies(PlayerbotAI* ai, uint32_t nowMs)
{
    for (uint8 i = 0; i < static_cast<uint8>(BotState::BOT_STATE_ALL); ++i)
    {
        ai::Engine* engine = ai->GetEngineByState(static_cast<BotState>(i));
        if (!engine)
            continue;
        std::string const joined = JoinStrategies(engine);
        auto it = engineStrategies_.find(i);
        if (it != engineStrategies_.end() && it->second == joined)
            continue;
        engineStrategies_[i] = joined;
        Emit(std::string(kPrefix) + "strategies|" + botName_ + "|st=" +
            WhyTrace::EngineStateName(i) + "|names=" + (joined.empty() ? "-" : joined), nowMs);
    }
}

void WhyTraceService::Update(uint32_t diff)
{
    if (!session_.active())
        return;
    uint32_t const now = WorldTimer::getMSTime();
    if (session_.ShouldStop(now))
    {
        Stop(WhyTrace::StopReason::Timeout);
        return;
    }
    Player* bot = ObjectAccessor::FindPlayer(ObjectGuid(botGuid_));
    if (!bot || !bot->IsInWorld())
    {
        Stop(WhyTrace::StopReason::BotGone);
        return;
    }
    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    if (!ai)
    {
        Stop(WhyTrace::StopReason::BotGone);
        return;
    }
    if (ai != attachedAi_)
    {
        // A relogin between the same guid installed a new AI: its engines have
        // no listener yet.
        attachedAi_ = ai;
        AttachListeners(ai);
        engineStrategies_.clear();
        EmitStrategies(ai, now);
    }
    // Strategy sets change on strategy commands, not every tick: one check a
    // second is enough and keeps the tick path free of string joins.
    strategyCheckAccumMs_ += diff;
    if (strategyCheckAccumMs_ >= 1000)
    {
        strategyCheckAccumMs_ = 0;
        EmitStrategies(ai, now);
    }
}

bool WhyTraceService::Traces(PlayerbotAI* ai) const
{
    return session_.active() && ai && ai->GetBot() &&
        ai->GetBot()->GetObjectGuid().GetRawValue() == botGuid_;
}

void WhyTraceService::NoteTickStart(uint64_t botRaw, std::vector<std::pair<std::string, float>> const& pending)
{
    if (!session_.active() || botRaw != botGuid_)
        return;
    tickStart_ = pending;
}

void WhyTraceService::NoteVerdict(uint64_t botRaw, Verdict const& verdict)
{
    if (!session_.active() || botRaw != botGuid_)
        return;
    // One tick cannot produce more useful verdicts than this; a runaway
    // preresolved queue must not grow the trace's memory.
    if (verdicts_.size() >= 128)
        return;
    verdicts_.push_back(verdict);
}

void WhyTraceService::NoteOrder(PlayerbotAI* ai, Player* from, std::string const& text)
{
    if (!session_.active() || !ai || !ai->GetBot())
        return;
    Player* bot = ai->GetBot();
    if (bot->GetObjectGuid().GetRawValue() != botGuid_)
        return;
    // Internal action code re-enters HandleCommand with the bot itself as the
    // sender; those are not orders received.
    if (!from || from->GetObjectGuid() == bot->GetObjectGuid())
        return;
    if (text.empty())
        return;
    uint32_t const now = WorldTimer::getMSTime();
    Emit(std::string(kPrefix) + "order|" + botName_ + "|from=" + WhyTrace::ClipName(from->GetName()) +
        "|text=" + WhyTrace::ClipName(text, WhyTrace::kMaxOrderChars), now);
}

void WhyTraceService::NoteOrderAction(PlayerbotAI* ai, Player* from, std::string const& action,
    std::string const& source)
{
    if (!session_.active() || !ai || !ai->GetBot())
        return;
    Player* bot = ai->GetBot();
    if (bot->GetObjectGuid().GetRawValue() != botGuid_)
        return;
    if (!from || from->GetObjectGuid() == bot->GetObjectGuid())
        return;
    uint32_t const now = WorldTimer::getMSTime();
    Emit(std::string(kPrefix) + "call|" + botName_ + "|from=" + WhyTrace::ClipName(from->GetName()) +
        "|act=" + WhyTrace::ClipName(action) + "|src=" + WhyTrace::ClipName(source, WhyTrace::kMaxOrderChars), now);
}

void WhyTraceService::FinishTick(PlayerbotAI* ai)
{
    if (!session_.active() || !ai)
        return;
    Player* bot = ai->GetBot();
    if (!bot || bot->GetObjectGuid().GetRawValue() != botGuid_)
        return;
    uint32_t const now = WorldTimer::getMSTime();
    session_.NoteTick(now);
    Emit(TickLine(ai, now), now);
    verdicts_.clear();
    tickStart_.clear();
}

std::string WhyTraceService::TickLine(PlayerbotAI* ai, uint32_t nowMs)
{
    Player* bot = ai->GetBot();

    std::vector<WhyTrace::Candidate> candidates;
    std::string chosenName;
    float chosenRelevance = 0.0f;

    for (Verdict const& verdict : verdicts_)
    {
        if (verdict.chosen)
        {
            chosenName = verdict.name;
            chosenRelevance = verdict.relevance;
            session_.NoteChosen(verdict.name, nowMs);
            continue;
        }
        WhyTrace::Candidate candidate;
        candidate.name = verdict.name;
        candidate.relevance = verdict.relevance;
        candidate.strategyActive = verdict.strategyActive;
        candidate.hasTarget = verdict.hasTarget;
        candidate.possible = verdict.possible;
        candidate.useful = verdict.useful;
        candidate.vetoed = verdict.vetoed;
        candidate.backedOff = verdict.backedOff;
        candidate.prereq = verdict.prereq;
        candidate.failed = verdict.failed;
        candidate.cast = verdict.cast;
        candidate.spellCooldownMs = verdict.spellCooldownMs;
        candidate.gcdRemainingMs = verdict.gcdRemainingMs;
        candidates.push_back(candidate);
    }

    // The candidates that were queued before the action loop and never ran
    // (one with a verdict above is already reported with its reason; this
    // also keeps the loop's own requeue of it from hiding that verdict).
    for (auto const& entry : tickStart_)
    {
        bool hasVerdict = false;
        for (Verdict const& verdict : verdicts_)
            if (verdict.name == entry.first)
            {
                hasVerdict = true;
                break;
            }
        if (hasVerdict)
            continue;
        WhyTrace::Candidate candidate;
        candidate.name = entry.first;
        candidate.relevance = entry.second;
        candidate.queuedOnly = true;
        if (ai::Engine* engine = ai->GetCurrentEngine())
            candidate.strategyActive = engine->HasActionNode(entry.first);
        candidates.push_back(candidate);
    }

    std::sort(candidates.begin(), candidates.end(),
        [](WhyTrace::Candidate const& a, WhyTrace::Candidate const& b) { return a.relevance > b.relevance; });
    // One row per action name: its best-relevance attempt says why it lost.
    std::vector<WhyTrace::Candidate> lost;
    for (WhyTrace::Candidate const& candidate : candidates)
    {
        bool seen = false;
        for (WhyTrace::Candidate const& kept : lost)
            if (kept.name == candidate.name)
            {
                seen = true;
                break;
            }
        if (!seen)
            lost.push_back(candidate);
        if (lost.size() >= WhyTrace::kMaxLostCandidates)
            break;
    }

    std::string lostText;
    for (WhyTrace::Candidate const& candidate : lost)
    {
        WhyTrace::LossReason const reason = WhyTrace::Classify(candidate);
        session_.NoteLoss(reason, nowMs);
        if (!lostText.empty())
            lostText += ',';
        lostText += WhyTrace::FormatLostEntry(candidate.name, candidate.relevance, reason);
    }

    Unit* target = nullptr;
    if (ai::Value<Unit*>* value = ai->GetAiObjectContext()->GetValue<Unit*>("current target"))
        target = value->Get();

    std::string targetText;
    std::string victimText;
    if (target)
    {
        targetText = WhyTrace::FormatTarget(GuidText(target->GetObjectGuid()), target->GetName());
        if (Unit* victim = target->GetVictim())
            victimText = WhyTrace::FormatTarget(GuidText(victim->GetObjectGuid()), victim->GetName());
    }

    char const* state = ai->GetCurrentEngine() ?
        WhyTrace::EngineStateName(static_cast<uint8_t>(ai->GetCurrentEngine()->GetState())) : "none";

    return WhyTrace::FormatTickLine(botName_, session_.ElapsedMs(nowMs), session_.ticks(), state,
        chosenName, chosenRelevance, !chosenName.empty(), targetText, victimText, lostText);
}

} // namespace TortoiseBots
