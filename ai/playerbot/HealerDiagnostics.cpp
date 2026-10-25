#include "playerbot/playerbot.h"
#include "HealerDiagnostics.h"
#include "GroupMembers.h"
#include "PlayerbotAIConfig.h"
#include "ServerFacade.h"
#include "strategy/Engine.h"
#include "Movement/MotionMaster.h"
#include <iomanip>
#include <sstream>

namespace
{
bool Enabled(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    return sPlayerbotAIConfig.healerDiagnostics && bot->IsInWorld() && bot->IsAlive() &&
        bot->GetGroup() && sPlayerbotAIConfig.hasLog("bot_events.csv");
}

bool HealingSpell(PlayerbotAI* ai, uint32 spellId)
{
    SpellEntry const* spell = sServerFacade.LookupSpellInfo(spellId);
    return spell && PlayerbotAI::IsHealSpell(spell) && PlayerbotAI::IsHeal(ai->GetBot());
}

char const* Reason(uint32 result)
{
    switch (result)
    {
        case SPELL_FAILED_OUT_OF_RANGE: return "out-of-range";
        case SPELL_FAILED_LINE_OF_SIGHT: return "line-of-sight";
        case SPELL_FAILED_NO_POWER: return "no-power";
        case SPELL_FAILED_SPELL_IN_PROGRESS: return "casting";
        case SPELL_FAILED_MOVING: return "moving";
        case SPELL_FAILED_BAD_TARGETS: return "target-invalid";
        case SPELL_FAILED_NOT_IN_CONTROL: return "not-in-control";
        case SPELL_FAILED_NOT_READY: return "not-ready";
        case SPELL_FAILED_NOT_KNOWN: return "not-known";
        default: return "cast-check-failed";
    }
}

// Only numeric identifiers and flags: no user-controlled CSV delimiters.
void State(std::ostringstream& out, PlayerbotAI* ai, Unit* target)
{
    Player* bot = ai->GetBot();
    out << std::fixed << std::setprecision(1)
        << "map=" << bot->GetMapId() << " instance=" << bot->GetInstanceId()
        << " mana=" << bot->GetPower(POWER_MANA) << "/" << bot->GetMaxPower(POWER_MANA)
        << " moving=" << bot->IsMoving() << " combat=" << bot->IsInCombat()
        << " casting=" << bot->IsNonMeleeSpellCasted(false)
        << " target=" << (target ? target->GetObjectGuid().GetCounter() : 0);
    MotionMaster* motion = bot->GetMotionMaster();
    out << " motion=" << unsigned(motion->GetCurrentMovementGeneratorType());
    float x, y, z;
    if (motion->GetDestination(x, y, z))
        out << " dest=" << x << ":" << y << ":" << z;
    else
        out << " dest=unavailable";
    if (target && target->IsInWorld() && target->GetMap() == bot->GetMap())
        out << " hp=" << target->GetHealthPercent()
            << " distance=" << bot->GetDistance(target)
            << " los=" << bot->IsWithinLOSInMap(target);
}
}

void ai::HealerDiagnostics::CheckFailed(PlayerbotAI* ai, Unit* target, uint32 spellId, uint32 result)
{
    if (!Enabled(ai) || !HealingSpell(ai, spellId))
        return;
    ++failedChecks;
    if (!skips.Due(WorldTimer::getMSTime()))
        return;
    std::ostringstream out;
    out << "spell=" << spellId << " result=" << result << " ";
    State(out, ai, target);
    sPlayerbotAIConfig.logEvent(ai, "HealSkip", Reason(result), out.str());
}

void ai::HealerDiagnostics::CastAttempt(PlayerbotAI* ai, Unit* target, uint32 spellId, bool accepted)
{
    if (!Enabled(ai) || !HealingSpell(ai, spellId))
        return;
    ++castAttempts;
    if (accepted)
        ++acceptedAttempts;
    else if (skips.Due(WorldTimer::getMSTime()))
    {
        std::ostringstream out;
        out << "spell=" << spellId << " ";
        State(out, ai, target);
        sPlayerbotAIConfig.logEvent(ai, "HealSkip", "execute-refused", out.str());
    }
}

void ai::HealerDiagnostics::Sample(PlayerbotAI* ai)
{
    if (!Enabled(ai) || !samples.Due(WorldTimer::getMSTime()) || !PlayerbotAI::IsHeal(ai->GetBot()))
        return;
    Player* bot = ai->GetBot();
    Player* lowest = nullptr;
    unsigned injured = 0;
    unsigned nearby = 0;
    float const healRange = ai->GetRange("heal");
    // At most the live group roster once per ten seconds; no world scan,
    // spell probes, target-value recomputation, DB calls or pathfinding.
    for (Player* member : LiveGroupMembers(bot->GetGroup()))
    {
        if (!member->IsAlive() || !member->IsInWorld() || member->GetMap() != bot->GetMap() ||
            !bot->IsFriendlyTo(member) || member->GetHealthPercent() >= sPlayerbotAIConfig.almostFullHealth)
            continue;
        ++injured;
        if (bot->GetDistance(member) > healRange)
            continue;
        ++nearby;
        if (!lowest || member->GetHealthPercent() < lowest->GetHealthPercent())
            lowest = member;
    }
    if (!injured && !failedChecks && !castAttempts)
        return;
    std::ostringstream out;
    out << "injured=" << injured << " nearby=" << nearby
        << " failed_checks=" << failedChecks << " attempts=" << castAttempts
        << " accepted=" << acceptedAttempts << " ";
    State(out, ai, lowest);
    Engine* engine = ai->GetCurrentEngine();
    Action const* action = engine ? engine->GetLastExecutedAction() : nullptr;
    // Engine owns mutable actions; the legacy name getter is non-const.
    std::string last = action ? const_cast<Action*>(action)->getName() : "none";
    for (char& ch : last)
        if (ch == '"' || ch == ',' || ch == '\n' || ch == '\r') ch = '_';
    out << " last_action=" << last;
    sPlayerbotAIConfig.logEvent(ai, "HealState", "snapshot", out.str());
    failedChecks = castAttempts = acceptedAttempts = 0;
}
