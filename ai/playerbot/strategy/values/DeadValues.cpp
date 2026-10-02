
#include "playerbot/playerbot.h"
#include "DeadValues.h"
#include "playerbot/TravelMgr.h"

using namespace ai;

// A ghost whose distance to its corpse stops closing for this long is not
// corpse-running any more: the MoveTo spline is dispatched every tick and never
// ridden (the stall signature LogGhostMoveDiag records - identical from-position,
// no distance gain). Left to the dead-time gates below, the bot stands there
// until deadTime > 10 * MINUTE, i.e. up to ten minutes of a dead bot doing
// nothing; on the live cycle-3 masterless pool 168 of 799 corpse runs spent ~601 s
// that way and produced 76% of all ghost time.
static constexpr uint32 kGhostStallSec = 60;

// Yards of progress toward the corpse that still count as "moving": a run that
// closes the distance slowly keeps its clock reset, only a stopped one trips.
static constexpr float kGhostStallProgressYd = 5.0f;

GuidPosition GraveyardValue::Calculate()
{
    WorldPosition refPosition = bot, botPos(bot);

    if (getQualifier() == "master")
    {
        if (ai->GetGroupMaster() && ai->IsSafe(ai->GetGroupMaster()) && ai->GetGroupMaster()->GetMapId() == bot->GetMapId())
        {
            refPosition = ai->GetGroupMaster();
        }
    }
    else if (getQualifier() == "travel")
    {
        auto travelTarget = AI_VALUE(TravelTarget*, "travel target");

        if (travelTarget && travelTarget->getPosition() && travelTarget->getPosition()->GetMapId() == bot->GetMapId())
        {
            refPosition = *travelTarget->getPosition();
        }
    }
    else if (getQualifier() == "another closest appropriate")
    {
        //just get ANOTHER nearest appropriate for level (neutral or same team zone)
        if (auto anotherAppropriate = GetAnotherAppropriateClosestGraveyard())
        {
            return GuidPosition(0, anotherAppropriate);
        }
    }

    WorldSafeLocsEntry const* ClosestGrave = sObjectMgr.GetClosestGraveYard(
        refPosition.getX(),
        refPosition.getY(),
        refPosition.getZ(),
        refPosition.GetMapId(),
        bot->GetTeam()
    );

    if (!ClosestGrave)
    {
        sLog.outDetail(
            "ERROR: Unable to find closest graveyard in GraveyardValue, will return GuidPosition() which is 0,0,0 - bot #%d %s:%d <%s>",
            bot->GetGUIDLow(),
            bot->GetTeam() == ALLIANCE ? "A" : "H",
            bot->GetLevel(),
            bot->GetName()
        );
        return GuidPosition();
    }

    return GuidPosition(0, ClosestGrave);
}

WorldSafeLocsEntry const* GraveyardValue::GetAnotherAppropriateClosestGraveyard() const
{
    // The Penqle core exposes closest-graveyard lookup but not an iterable
    // graveyard registry. The old donor loop also never populated its fallback
    // entry, so fail closed rather than walking private core state.
    return nullptr;
}

GuidPosition BestGraveyardValue::Calculate()
{
    Corpse* corpse = bot->GetCorpse();
    if (!corpse)
    {
        sLog.outDetail(
            "ERROR: Unable to find closest graveyard in BestGraveyardValue, will return GuidPosition() which is 0,0,0 - bot #%d %s:%d <%s>",
            bot->GetGUIDLow(),
            bot->GetTeam() == ALLIANCE ? "A" : "H",
            bot->GetLevel(),
            bot->GetName()
        );
        return GuidPosition();
    }

    uint32 deathCount = AI_VALUE(uint32, "death count");

    //attempt to revive at other same map graveyards which are not enemy territory
    if (!ai->HasActivePlayerMaster() && deathCount >= DEATH_COUNT_BEFORE_TRYING_ANOTHER_GRAVEYARD)
    {
        GuidPosition anotherGraveyard = AI_VALUE2(GuidPosition, "graveyard", "another closest appropriate");
        if (anotherGraveyard)
        {
            return anotherGraveyard;
        }
        sLog.outDetail(
            "ERROR: Unable to find another closest appropriate graveyard in BestGraveyardValue, resorting to self graveyard - bot #%d %s:%d <%s>",
            bot->GetGUIDLow(),
            bot->GetTeam() == ALLIANCE ? "A" : "H",
            bot->GetLevel(),
            bot->GetName()
        );
    }

    //Revive near master.
    if ((ai->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT) ||
        ai->HasStrategy("wander", BotState::BOT_STATE_NON_COMBAT)) &&
        ai->GetGroupMaster() && ai->GetGroupMaster() != bot)
    {
        GuidPosition masterGraveyard = AI_VALUE2(GuidPosition, "graveyard", "master");
        if (masterGraveyard)
        {
            return masterGraveyard;
        }
        sLog.outDetail(
            "ERROR: Unable to find master graveyard in BestGraveyardValue, resorting to self graveyard - bot #%d %s:%d <%s>",
            bot->GetGUIDLow(),
            bot->GetTeam() == ALLIANCE ? "A" : "H",
            bot->GetLevel(),
            bot->GetName()
        );
    }

    //Revive near travel target if it's far away from last death.
    if (AI_VALUE2(GuidPosition, "graveyard", "travel") && AI_VALUE2(GuidPosition, "graveyard", "travel").fDist(corpse) > sPlayerbotAIConfig.reactDistance)
    {
        GuidPosition travelGraveyard = AI_VALUE2(GuidPosition, "graveyard", "travel");
        if (travelGraveyard)
        {
            return travelGraveyard;
        }
        sLog.outDetail(
            "ERROR: Unable to find travel graveyard in BestGraveyardValue, resorting to self graveyard - bot #%d %s:%d <%s>",
            bot->GetGUIDLow(),
            bot->GetTeam() == ALLIANCE ? "A" : "H",
            bot->GetLevel(),
            bot->GetName()
        );
    }

    return AI_VALUE2(GuidPosition, "graveyard", "self");
}

bool ShouldSpiritHealerValue::Calculate()
{
    uint32 deathCount = AI_VALUE(uint32, "death count");
    uint8 durability = AI_VALUE(uint8, "durability");

    // A grouped ghost still corpse-runs: with an active player master the wait-for-
    // master gate in find corpse holds the ghost briefly for a res, then (90 s timeout)
    // runs to the body. Sending it to the spirit healer instead strands it: the res
    // sickness + durability loss buy nothing, and the grouped report (ghost idle at
    // the GY with its leader standing nearby) is exactly this branch firing.
    if (ai->HasActivePlayerMaster())
        return false;

    //Nothing to lose
    if (ai->HasAura(SPELL_ID_PASSIVE_RESURRECTION_SICKNESS, bot) || durability < 10)
        return true;

    //Died too many times
    if (deathCount > DEATH_COUNT_BEFORE_REVIVING_AT_SPIRIT_HEALER)
        return true;

    Corpse* corpse = bot->GetCorpse();
    if (!corpse)
    {
        //if no corpse (?) then definitely should revive at spirit healer
        return true;
    }

    // Never revive at spirit healer when corpse is in an instance (dungeon/raid)
    if (MapEntry const* mapEntry = sMapStorage.LookupEntry<MapEntry>(corpse->GetMapId()))
    {
        if (mapEntry->IsDungeon())
            return false;
    }

    uint32 deadTime = time(nullptr) - corpse->GetGhostTime();

    //Dead for a long time
    if (deadTime > 10 * MINUTE && deathCount > 1)
        return true;

    //Dead for a long time
    if (deadTime > 20 * MINUTE)
        return true;

    // Stalled corpse run: the ghost stopped closing the distance to its corpse,
    // so the walk is not going anywhere and waiting out the ten minutes above
    // buys nothing. Hand the run to the spirit healer exactly like a long dead
    // time does. Inside the core reclaim radius the corpse revive fires without
    // walking (find corpse parks the ghost there on purpose while the reclaim
    // delay runs), so only a ghost that still has ground to cover counts.
    if (!corpse->IsWithinDistInMap(bot, (float)CORPSE_RECLAIM_RADIUS, true))
    {
        float const corpseDist = WorldPosition(bot).fDist(corpse);
        time_t const now = time(nullptr);
        int32 bestDist = AI_VALUE2(int32, "manual int", "ghost walk best");
        time_t anchor = AI_VALUE2(time_t, "manual time", "ghost walk anchor");

        // An anchor from a previous corpse run (or before this death) restarts
        // the clock; so does any progress toward the corpse.
        if (anchor < corpse->GetGhostTime())
        {
            anchor = 0;
            bestDist = 0;
        }

        if (bestDist <= 0 || corpseDist < (float)bestDist - kGhostStallProgressYd)
        {
            SET_AI_VALUE2(int32, "manual int", "ghost walk best", (int32)corpseDist);
            SET_AI_VALUE2(time_t, "manual time", "ghost walk anchor", now);
            anchor = now;
        }

        if (anchor != 0 && now - anchor >= (time_t)kGhostStallSec)
        {
            sLog.outDetail("[BOT CORPSE] %s: find corpse stalled %.1fy from the corpse for %us, reviving at the spirit healer",
                bot->GetName(), corpseDist, kGhostStallSec);
            SET_AI_VALUE2(time_t, "manual time", "ghost walk anchor", now); // re-arm, the revive may take a tick or two
            return true;
        }
    }

    //If there are enemies near grave and corpse we go to corpse first.
    if (AI_VALUE2(bool, "manual bool", "enemies near graveyard"))
        return false;

    //Enemies near corpse so try grave first.
    if (AI_VALUE2(bool, "manual bool", "enemies near corpse"))
        return true;

    GuidPosition graveyard = AI_VALUE(GuidPosition, "best graveyard");

    float corpseDistance = WorldPosition(bot).fDist(corpse);
    float graveYardDistance = WorldPosition(bot).fDist(graveyard);
    bool corpseInSight = corpseDistance < sPlayerbotAIConfig.sightDistance;
    bool graveInSight = graveYardDistance < sPlayerbotAIConfig.sightDistance;
    bool enemiesNear = !AI_VALUE(std::list<ObjectGuid>, "possible targets").empty();

    if (enemiesNear)
    {
        if (graveInSight)
        {
            SET_AI_VALUE2(bool, "manual bool", "enemies near graveyard", true);
            return false;
        }
        if (corpseInSight)
        {
            SET_AI_VALUE2(bool, "manual bool", "enemies near corpse", true);
            return true;
        }
    }

    //If grave is near and no ress sickness go there.
    if (graveInSight && !corpseInSight && ai->HasCheat(BotCheatMask::repair))
        return true;

    //Stick to corpse.
    return false;
}