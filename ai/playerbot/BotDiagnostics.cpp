// playerbot.h first: it brings the real PlayerbotAI definition (and the module's
// `using namespace ai;`), so the PlayerbotAIConfig declarations parsed right after it
// bind to that class rather than to a bare forward declaration.
#include "BotDiagnostics.h"
#include "playerbot/playerbot.h"
#include "playerbot/ServerFacade.h"
#include <ctime>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <vector>

namespace ai { namespace botdiag {
    thread_local const char* gLastPhaseTag     = nullptr;
    thread_local const char* gLastPhaseBotName = nullptr;

    bool IsActionLogEnabled()
    {
        return sPlayerbotAIConfig.enableActionLog;
    }

    namespace
    {
        struct ActionCountRow
        {
            uint8_t botClass = 0;
            std::string name;
            uint64_t ok = 0;
            uint64_t fail = 0;
        };
        // Bots update on parallel map threads: every access takes the mutex.
        // Small row count (one per class x action), so a linear scan is fine
        // and costs no allocation once the row exists.
        std::mutex gActionCountsMutex;
        std::vector<ActionCountRow> gActionCounts;
    }

    void CountAction(uint8_t botClass, char const* actionName, bool ok)
    {
        if (!sPlayerbotAIConfig.actionCountsLog || !actionName)
            return;
        std::lock_guard<std::mutex> guard(gActionCountsMutex);
        for (ActionCountRow& row : gActionCounts)
        {
            if (row.botClass == botClass && row.name == actionName)
            {
                if (ok) ++row.ok; else ++row.fail;
                return;
            }
        }
        ActionCountRow row;
        row.botClass = botClass;
        row.name = actionName;
        if (ok) ++row.ok; else ++row.fail;
        gActionCounts.push_back(row);
    }

    void DumpActionCounts()
    {
        if (!sPlayerbotAIConfig.actionCountsLog)
            return;
        std::vector<ActionCountRow> snapshot;
        {
            std::lock_guard<std::mutex> guard(gActionCountsMutex);
            snapshot = gActionCounts;
        }
        if (snapshot.empty())
            return;
        // First dump of the run truncates (haslog=true bypasses the allowlist,
        // like bot_test_results.log), later dumps append. Header lives at the
        // top because nothing else writes this file.
        static bool sHeaderWritten = false;
        if (!sHeaderWritten)
        {
            if (!sPlayerbotAIConfig.openLog("action_counts.csv", "w", true))
                return;
            sPlayerbotAIConfig.log("action_counts.csv", "utc_time,class,action,ok_count,fail_count");
            sHeaderWritten = true;
        }
        time_t now = time(nullptr);
        tm* utc = gmtime(&now);
        char ts[20];
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", utc ? utc : localtime(&now));
        for (ActionCountRow const& row : snapshot)
        {
            std::ostringstream line;
            line << ts << "," << (unsigned)row.botClass << "," << row.name
                 << "," << row.ok << "," << row.fail;
            sPlayerbotAIConfig.log("action_counts.csv", line.str().c_str());
        }
    }
}}

namespace
{
// Sampling and logging cadence of the evade probe (see BotDiagnostics.h).
uint32 const kSampleSec = 2;      // one sample per bot per this many seconds
uint32 const kLogSec = 30;        // at most one EvadeProbe row per bot per this many seconds
uint32 const kCounterSec = 60;    // counter row cadence per fighting bot
int32 const kHealthRisePoints = 15; // health points (of 100) gained between samples = regenerating

// Horizontal distance and height difference are logged separately: the evade reports are
// about a mob that cannot close on the bot, where the height difference and line of sight
// are the interesting geometry, not the straight-line distance.
}

// Defined here rather than in PlayerbotAI.cpp so the whole probe sits in one file, at
// global scope like every other PlayerbotAI member definition (the class lives in the
// global namespace, despite the module's `using namespace ai;`). The state lives in the
// PlayerbotAI member declared in PlayerbotAI.h; the diagnostics header must not name the
// AI class at all (see BotDiagnostics.h).
void PlayerbotAI::UpdateEvadeProbe()
{
    PlayerbotAI* ai = this;
    Player* bot = ai->GetBot();
    if (!bot || !bot->IsInWorld() || bot->IsBeingTeleported())
        return;

    time_t const now = time(nullptr);
    if (evadeProbe.lastSampleSec && now - (time_t)evadeProbe.lastSampleSec < (time_t)kSampleSec)
        return;

    evadeProbe.lastSampleSec = (uint32)now;

    // The whole probe is off unless the event log it writes to is enabled.
    if (!sPlayerbotAIConfig.hasLog("bot_events.csv"))
        return;

    AiObjectContext* context = ai->GetAiObjectContext();
    if (!context)
        return;

    Unit* unit = context->GetValue<Unit*>("current target")->Get();
    if (!unit)
        unit = ai->GetUnit(context->GetValue<ObjectGuid>("attack target")->Get());

    if (!unit || !unit->IsInWorld() || unit->GetMapId() != bot->GetMapId())
        unit = nullptr;

    Creature* creature = (unit && unit->IsAlive()) ? unit->ToCreature() : nullptr;
    if (!creature)
    {
        // No creature in hand (or it just died): the next one starts a fresh series.
        evadeProbe.sampledTarget = 0;
        evadeProbe.sampledSec = 0;
        evadeProbe.sampledHealthPct = 0;
        evadeProbe.riseCountedTarget = 0;
        return;
    }

    uint64 const guid = creature->getObjectGuid().GetRawValue();
    uint32 const healthPct = (uint32)creature->GetHealthPercent();
    bool const sameTarget = (evadeProbe.sampledTarget == guid);
    bool const victim = (creature->GetVictim() == bot);
    // Engaged with this bot: it holds the mob as its victim or has threat on it. The raw
    // Player::IsInCombat() flag is logged separately - in this core it only turns on once
    // damage has been traded, so it misses the "bot ordered an attack it cannot land" case
    // this probe is about.
    bool const inFight = victim || sServerFacade.GetThreatManager(creature).getThreat(bot) > 0.0f;
    int32 const healthDelta = sameTarget ? (int32)healthPct - (int32)evadeProbe.sampledHealthPct : 0;
    bool const healthRise = sameTarget && inFight && healthDelta >= kHealthRisePoints;
    bool const evading = creature->IsInEvadeMode();
    bool const notReachable = creature->IsEvadeBecauseTargetNotReachable();

    if (!sameTarget && (inFight || evading || notReachable))
    {
        ++evadeProbe.counterFights;
        evadeProbe.riseCountedTarget = 0;
    }

    if (healthRise && evadeProbe.riseCountedTarget != guid)
    {
        evadeProbe.riseCountedTarget = guid;
        ++evadeProbe.counterRiseFights;
    }

    if (evading)
        ++evadeProbe.counterEvade;

    if (notReachable)
        ++evadeProbe.counterNoReach;

    if ((evading || notReachable || healthRise) && (!evadeProbe.lastLogSec || now - (time_t)evadeProbe.lastLogSec >= (time_t)kLogSec))
    {
        evadeProbe.lastLogSec = (uint32)now;
        ++evadeProbe.counterRows;

        std::ostringstream out;
        out << "mob=" << creature->GetName();
        out << " entry=" << creature->GetEntry();
        out << " mlvl=" << (uint32)creature->GetLevel();
        out << " hp=" << healthPct;
        out << " dist2d=" << (uint32)sServerFacade.getDistance2d(bot, creature);
        out << " dz=" << (int)(bot->GetPositionZ() - creature->GetPositionZ());
        out << " los=" << (int)bot->IsWithinLOSInMap(creature, true);
        out << " botcombat=" << (int)bot->IsInCombat();
        out << " victim=" << (int)victim;
        out << " botmove=" << (int)bot->IsMoving();
        out << " botcast=" << (int)bot->IsNonMeleeSpellCasted(false);
        out << " evade=" << (int)evading;
        out << " notreach=" << (int)notReachable;
        out << " hpdelta=" << healthDelta;
        out << " dt=" << (sameTarget ? (uint32)(now - (time_t)evadeProbe.sampledSec) : 0);
        out << " class=" << (uint32)bot->GetClass();
        out << " blvl=" << (uint32)bot->GetLevel();

        // Why the creature is in evade, and what it looks like while it is. The core has
        // three ways in: the unreachable timer (logged above as notreach), EnterEvadeMode
        // having sent it home - HOME motion, the walk back to its spawn point - and the
        // hard leash, which no starting-zone creature has (its leash_range is 0). motion
        // tells which one, homedist how far the fight dragged it from the spawn point it
        // is now walking back to (an evading mob is invulnerable and regenerating the
        // whole way), csdist how far the current fight has moved from where it started -
        // the core stops counting a victim that left the threat radius around that point
        // and evades when no reference is left - and atk/threat whether anything is still
        // holding it. petvictim separates a creature the bot's own pet is fighting from
        // one the bot is, which is the report "the hunter's pet pulls, the owner walks on
        // and the mob is left alone".
        float homeX, homeY, homeZ, homeO;
        creature->GetRespawnCoord(homeX, homeY, homeZ, &homeO);
        float combatStartX, combatStartY, combatStartZ;
        creature->GetCombatStartPosition(combatStartX, combatStartY, combatStartZ);
        Unit* pet = bot->GetPet();
        out << " motion=" << (uint32)creature->GetMotionMaster()->GetCurrentMovementGeneratorType();
        out << " homedist=" << (uint32)creature->GetDistance(homeX, homeY, homeZ);
        out << " csdist=" << (uint32)creature->GetDistance(combatStartX, combatStartY, combatStartZ);
        out << " atk=" << (uint32)creature->GetAttackers().size();
        out << " threat=" << (uint32)sServerFacade.GetThreatManager(creature).getThreat(bot);
        out << " petvictim=" << (int)(pet && pet->GetVictim() == creature);
        sPlayerbotAIConfig.logEvent(ai, "EvadeProbe", out.str(), std::to_string(creature->GetEntry()));
    }

    evadeProbe.sampledTarget = guid;
    evadeProbe.sampledSec = (uint32)now;
    evadeProbe.sampledHealthPct = healthPct;

    if (!evadeProbe.lastCounterSec)
    {
        evadeProbe.lastCounterSec = (uint32)now;
    }
    else if (now - (time_t)evadeProbe.lastCounterSec >= (time_t)kCounterSec)
    {
        evadeProbe.lastCounterSec = (uint32)now;
        if (evadeProbe.counterFights)
        {
            std::ostringstream out;
            out << "fights=" << evadeProbe.counterFights;
            out << " rising=" << evadeProbe.counterRiseFights;
            out << " evadeSamples=" << evadeProbe.counterEvade;
            out << " notreachSamples=" << evadeProbe.counterNoReach;
            out << " probeRows=" << evadeProbe.counterRows;
            sPlayerbotAIConfig.logEvent(ai, "EvadeProbeCounter", out.str(), "");
        }

        evadeProbe.counterFights = 0;
        evadeProbe.counterRiseFights = 0;
        evadeProbe.counterEvade = 0;
        evadeProbe.counterNoReach = 0;
        evadeProbe.counterRows = 0;
    }
}
