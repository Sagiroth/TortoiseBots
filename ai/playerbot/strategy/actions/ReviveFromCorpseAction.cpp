
#include "playerbot/playerbot.h"
#include "ReviveFromCorpseAction.h"
#include "playerbot/PlayerbotFactory.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/FleeManager.h"
#include "playerbot/TravelMgr.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/values/DeadValues.h"
#include "Database/DBCStructure.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "runtime/BotManager.h"

using namespace ai;
// Note: with DisableActivityPriorities=1 the two wait-then-teleport windows never
// fire (AllowActivity(DETAILED_MOVE) is always true), so no bypass is needed or
// applied there - ghosts take the normal MoveTo walk path every tick like every
// other bot.

// Rate-limited ghost-movement diagnostics (live-server triage for the "ghost
// stands still" stall: the move action reports success but the bot does not
// displace). One row per bot per 30 s on the ghost corpse-run dispatch path,
// written to ghost_moves.csv when it is listed in AiPlayerbot.AllowedLogFiles.
// Fields: target + 3D distance, motion stack type before/after MoveTo (IDLE vs
// POINT vs anything else), whether a movement was actually started, and
// seconds without a position change.
static void LogGhostMoveDiag(PlayerbotAI* ai, Player* bot, WorldPosition const& botBefore,
    WorldPosition const& target, float distBefore, int32 movegenBefore, int32 movegenAfter,
    bool started, bool moveResult, float distAfter, uint32 posUnchangedSec)
{
    if (!bot || !ai || !sPlayerbotAIConfig.hasLog("ghost_moves.csv"))
        return;
    AiObjectContext* context = ai->GetAiObjectContext();
    if (!context)
        return;
    time_t now = time(nullptr);
    time_t last = AI_VALUE2(time_t, "manual time", "ghost move diag");
    if (last && now - last < 30)
        return;
    SET_AI_VALUE2(time_t, "manual time", "ghost move diag", now);
    // name,map,fromX,fromY,fromZ,targetX,targetY,targetZ,dist,movegenBefore,movegenAfter,started,result,distAfter,posUnchangedSec
    char row[320];
    snprintf(row, sizeof(row), "%s,%u,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%d,%d,%d,%d,%.1f,%u",
        bot->GetName(), target.GetMapId(), botBefore.getX(), botBefore.getY(), botBefore.getZ(),
        target.getX(), target.getY(), target.getZ(), distBefore, movegenBefore, movegenAfter,
        started ? 1 : 0, moveResult ? 1 : 0, distAfter, posUnchangedSec);
    sPlayerbotAIConfig.log("ghost_moves.csv", row);
}


// How long a corpse run waits for a nearby resurrect-capable master before
// running anyway. Covers the grouped report (random bot + real-player leader,
// ghost idle at the graveyard): the master may be the wrong class, OOM, mid-
// fight, or simply not noticing - waiting forever is never right. Donor
// mod-playerbots has no wait gate at all (wait block commented out,
// ReviveFromCorpseAction.cpp:85-90); 90 s keeps the polite wait without the
// infinite stall.
static constexpr int64 kWaitForMasterTimeoutSec = 90;


static bool FindInstanceEntranceTrigger(uint32 corpseMapId, uint32 botMapId, WorldPosition const& botPos,
                                         AreaTriggerEntry const*& outAtEntry, AreaTriggerTeleport const*& outAt)
{
    outAtEntry = nullptr;
    outAt = nullptr;
    float bestDist = FLT_MAX;

    for (uint32 i = 0; i < sAreaTriggerStore.GetNumRows(); ++i)
    {
        AreaTriggerEntry const* atEntry = sAreaTriggerStore.LookupEntry(i);
        if (!atEntry)
            continue;

        AreaTriggerTeleport const* at = sObjectMgr.GetAreaTriggerTeleport(i);
        if (!at)
            continue;

        if (at->destination.mapId != corpseMapId)
            continue;

        if (atEntry->mapid != botMapId)
            continue;

        WorldPosition triggerPos(atEntry->mapid, atEntry->x, atEntry->y, atEntry->z);
        float dist = botPos.sqDistance2d(triggerPos);
        if (dist < bestDist)
        {
            bestDist = dist;
            outAtEntry = atEntry;
            outAt = at;
        }
    }

    return outAtEntry != nullptr && outAt != nullptr;
}

bool ReviveFromCorpseAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    Player* master = ai->GetGroupMaster();
    Corpse* corpse = bot->GetCorpse();

    // follow master when master revives
    WorldPacket& p = event.GetPacket();
    if (!p.empty() && p.getOpcode() == CMSG_RECLAIM_CORPSE && master && !corpse && sServerFacade.IsAlive(bot))
    {
        if (sServerFacade.IsDistanceLessThan(AI_VALUE2(float, "distance", "master target"), sPlayerbotAIConfig.farDistance))
        {
            std::string defaultMovementStrategy = ai->GetDefaultMovementStrategy();

            if (!ai->HasStrategy(defaultMovementStrategy, BotState::BOT_STATE_NON_COMBAT))
            {
                ai->TellPlayerNoFacing(requester, "Welcome back!", PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
                ai->ChangeStrategy("+" + defaultMovementStrategy + ",-stay", BotState::BOT_STATE_NON_COMBAT);
                return true;
            }
        }
    }

    if (!corpse)
        return false;

    if (corpse->GetGhostTime() + bot->GetCorpseReclaimDelay(corpse->GetType() == CORPSE_RESURRECTABLE_PVP) > time(nullptr))
    {
        int64 wait = corpse->GetGhostTime() + bot->GetCorpseReclaimDelay(corpse->GetType() == CORPSE_RESURRECTABLE_PVP) - time(nullptr);
        sLog.outDetail("[BOT CORPSE] %s: revive from corpse - BLOCKED: reclaim delay not elapsed (%llds left)", bot->GetName(), (long long)wait);
        return false;
    }

    if (master)
    {
        //Revive with master.
        if (bot != master && sServerFacade.UnitIsDead(master) && master->GetCorpse() && sServerFacade.IsDistanceLessThan(AI_VALUE2(float, "distance", "master target"), sPlayerbotAIConfig.farDistance))
        {
            sLog.outDetail("[BOT CORPSE] %s: revive from corpse - BLOCKED: master is dead & nearby, waiting to revive together", bot->GetName());
            return false;
        }
    }

    // The core drops CMSG_RECLAIM_CORPSE silently in five cases (not alive-check
    // inverted: alive, not ghost, no corpse, reclaim delay, out of
    // CORPSE_RECLAIM_RADIUS=39 yd, BG not in progress - MiscHandler.cpp:710-732),
    // so only claim success when the bot is actually alive afterwards. On a drop
    // the corpse-run continues next tick (corpse near re-fires) instead of
    // logging a phantom revive and running the post-rez rescue for a still-dead bot.
    float reclaimDist = (float)CORPSE_RECLAIM_RADIUS;
    if (!corpse->IsWithinDistInMap(bot, reclaimDist, true))
    {
        sLog.outDetail("[BOT CORPSE] %s: revive from corpse - BLOCKED: corpse %.1f yd away, core needs within %d yd; walking closer",
            bot->GetName(), bot->GetDistance(corpse), CORPSE_RECLAIM_RADIUS);
        return false;
    }

    sLog.outDetail("[BOT CORPSE] %s: revive from corpse - RECLAIMING corpse now", bot->GetName());

    ai->StopMoving();
    WorldPacket packet(CMSG_RECLAIM_CORPSE);
    packet << bot->getObjectGuid();
    bot->GetSession()->HandleReclaimCorpseOpcode(packet);

    if (!sServerFacade.IsAlive(bot))
    {
        sLog.outDetail("[BOT CORPSE] %s: revive from corpse - RECLAIM DROPPED by core (still dead), retrying next tick", bot->GetName());
        return false;
    }

    sLog.outDetail("Bot #%d %s:%d <%s> revives at body", bot->GetGUIDLow(), bot->GetTeam() == ALLIANCE ? "A" : "H", bot->GetLevel(), bot->GetName());
    SET_AI_VALUE(bool, "corpse run", false);
    // Post-rez rescue (best-effort, fail-closed): a random bot that keeps dying
    // where its level cannot survive is relocated once to validated fitting
    // ground instead of GY-camping. Normal and owned flows are unaffected.
    TortoiseBots::BotManager::Instance().RelocateHopelessBot(bot);
    // Deliberately NOT resetting "death count" here. BestGraveyardValue only
    // switches to a graveyard outside the current zone once the count reaches
    // DEATH_COUNT_BEFORE_TRYING_ANOTHER_GRAVEYARD - but a bot resurrecting is
    // exactly the moment the count has to survive. Resetting here pinned it at
    // 1 forever, so the escape hatch was unreachable and bots that died inside
    // an enemy town (Razor Hill, Aerie Peak) resurrected into the same guards
    // indefinitely. The count is still cleared by XpGainAction, i.e. once the
    // bot is alive and earning again.
    sPlayerbotAIConfig.logEvent(ai, "ReviveFromCorpseAction");

    return true;
}

bool FindCorpseAction::Execute(Event& event)
{
    if (bot->InBattleGround())
        return false;

    Corpse* corpse = bot->GetCorpse();
    if (!corpse)
    {
        sLog.outDetail("[BOT CORPSE] %s: find corpse - no corpse, abort", bot->GetName());
        return false;
    }

    // Manual override: the master commanded "corpse run", so ignore the wait-for-master gate
    // below and run to the corpse regardless of master proximity. Useful when the master cannot
    // resurrect the bot (no res spell / too low level) and would otherwise leave it waiting.
    bool manualCorpseRun = AI_VALUE(bool, "corpse run");

    Player* master = ai->GetGroupMaster();
    WorldPosition botPos(bot), corpsePos(corpse), moveToPos = corpsePos, masterPos(master);
    float reclaimDist = CORPSE_RECLAIM_RADIUS - 5.0f;
    int64 deadTime = time(nullptr) - corpse->GetGhostTime();

    bool corpseInDungeon = false;
    uint32 dungeonMapId = 0;
    if (MapEntry const* mapEntry = sMapStorage.LookupEntry<MapEntry>(corpsePos.GetMapId()))
    {
        if (mapEntry->IsDungeon())
        {
            corpseInDungeon = true;
            dungeonMapId = corpsePos.GetMapId();
        }
    }

    // Bounded wait for a nearby resurrect-capable master. Past the timeout the run
    // goes anyway: the master may be the wrong class, OOM, mid-fight, or simply not
    // noticing, and waiting forever strands the ghost at the graveyard (grouped
    // report, Sep 2026). Donor mod-playerbots has no wait gate at all (its wait
    // block is commented out); 90 s keeps the polite wait without the infinite stall.
    bool waitForMasterTimedOut = deadTime > kWaitForMasterTimeoutSec;
    if (waitForMasterTimedOut && master && !manualCorpseRun && master->GetMapId() == bot->GetMapId() && !corpseInDungeon)
    {
        // The condition stays true every tick for the whole walk back, so log
        // at most once a minute per bot via a dedicated manual-time key.
        time_t nowWait = time(nullptr);
        time_t lastWaitLog = AI_VALUE2(time_t, "manual time", "wait master logged");
        if (!lastWaitLog || nowWait - lastWaitLog >= 60)
        {
            SET_AI_VALUE2(time_t, "manual time", "wait master logged", nowWait);
            sLog.outDetail("[BOT CORPSE] %s: find corpse - wait-for-master timed out after %llds, running to corpse anyway",
                bot->GetName(), (long long)deadTime);
        }
    }
    if (master && !manualCorpseRun && !waitForMasterTimedOut && master->GetMapId() == bot->GetMapId() && !corpseInDungeon)
    {
        bool masterCanResurrect = sServerFacade.IsAlive(master) && !master->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST) &&
                                  (PlayerbotAI::IsHeal(master) || master->GetClass() == CLASS_PRIEST || master->GetClass() == CLASS_PALADIN ||
                                   master->GetClass() == CLASS_SHAMAN || master->GetClass() == CLASS_DRUID);

        float masterTargetDist = AI_VALUE2(float, "distance", "master target");
        if (masterCanResurrect && botPos.GetMapId() == corpsePos.GetMapId() && !PlayerbotAIStorage::Instance().GetAI(master) && sServerFacade.IsDistanceLessThan(masterTargetDist, sPlayerbotAIConfig.farDistance))
        {
            sLog.outDetail("[BOT CORPSE] %s: find corpse - BLOCKED: real-player master within farDistance (dist=%.1f < %.1f). Waiting for master to resurrect. Say 'corpse run' to override.",
                bot->GetName(), masterTargetDist, sPlayerbotAIConfig.farDistance);
            return false;
        }
    }

    if (corpseInDungeon && deadTime >= 5 * MINUTE)
    {
        AreaTriggerTeleport const* entranceTeleport = sObjectMgr.GetMapEntranceTrigger(dungeonMapId);
        if (entranceTeleport)
        {
            sLog.outBasic("[BOT CORPSE] %s: instance corpse run timeout (%llds >= 300s), appearing revived at dungeon entrance (map %u)",
                bot->GetName(), (long long)deadTime, dungeonMapId);
            bot->GetMotionMaster()->Clear();
            // Resurrect BEFORE the teleport. TeleportTo onto another map is async:
            // the player is pulled off its map and m_currMap is null until the
            // worldport ack lands next tick, and Player::ResurrectPlayer -> GetMap()
            // ASSERTS on a mapless player (Object.cpp:2046 -> terminate; observed
            // taking the whole world server down at the 300s corpse timeout). The
            // ghost still stands on a map here (at the graveyard outside, or inside
            // the instance), so revive it where it is and teleport the living bot to
            // the entrance. Never resurrect a mapless bot.
            if (!bot->FindMap())
                return true;
            bot->ResurrectPlayer(0.5f, false);
            bot->SpawnCorpseBones();
            bot->TeleportTo(entranceTeleport->destination.mapId, entranceTeleport->destination.x, entranceTeleport->destination.y, entranceTeleport->destination.z, entranceTeleport->destination.o);
            bot->SaveToDB();
            SET_AI_VALUE(bool, "corpse run", false);
            return true;
        }
    }

    if (botPos.GetMapId() != corpsePos.GetMapId())
    {
        AreaTriggerEntry const* entranceTrigger = nullptr;
        AreaTriggerTeleport const* entranceTeleport = nullptr;
        if (FindInstanceEntranceTrigger(corpsePos.GetMapId(), botPos.GetMapId(), botPos, entranceTrigger, entranceTeleport))
        {

            // 2. Near entrance portal: step into instance
            float triggerRadius = std::max(5.0f, entranceTrigger->radius);
            WorldPosition portalPos(entranceTrigger->mapid, entranceTrigger->x, entranceTrigger->y, entranceTrigger->z);
            float distToPortal = botPos.fDist(portalPos);
            if (distToPortal <= triggerRadius + 2.0f)
            {
                sLog.outBasic("[BOT CORPSE] %s: reached instance entrance portal (dist=%.1f <= %.1f), entering instance %u",
                    bot->GetName(), distToPortal, triggerRadius + 2.0f, corpsePos.GetMapId());
                bot->GetMotionMaster()->Clear();
                bot->TeleportTo(entranceTeleport->destination.mapId, entranceTeleport->destination.x, entranceTeleport->destination.y, entranceTeleport->destination.z, entranceTeleport->destination.o);
                return true;
            }

            // 3. Move towards the entrance portal on the bot's current map
            moveToPos = portalPos;
        }
        else
        {
            sLog.outDetail("[BOT CORPSE] %s: find corpse - cross-map corpse on map %u but no entrance trigger from map %u",
                bot->GetName(), corpsePos.GetMapId(), botPos.GetMapId());
            return false;
        }
    }
    else
    {
        float corpseDist = botPos.distance(corpsePos);

        //If player fell through terrain move corpse to player position.
        if (isRealPlayer_Helper(bot) && botPos.GetMapId() == moveToPos.GetMapId())
        {
            //Try to correct the position upward.
            if (!moveToPos.ClosestCorrectPoint(5.0f, 500.0f, bot->GetInstanceId()))
            {
                //Revive in place.
                corpse->Relocate(botPos.getX(), botPos.getY(), botPos.getZ());
                corpsePos = corpse;
                corpseDist = botPos.distance(corpsePos);
            }
            else
            {
                corpse->Relocate(moveToPos.getX(), moveToPos.getY(), moveToPos.getZ());
                corpsePos = corpse;
                corpseDist = botPos.distance(corpsePos);
            }
        }

        bool moveToMaster = master && master != bot && master->GetMapId() == corpsePos.GetMapId() && masterPos.fDist(corpsePos) < reclaimDist;

        sLog.outDetail("[BOT CORPSE] %s: find corpse - corpseDist=%.1f reclaimDist=%.1f reactDist=%.1f moveToMaster=%d deadTime=%llds",
            bot->GetName(), corpseDist, reclaimDist, sPlayerbotAIConfig.reactDistance, moveToMaster ? 1 : 0, (long long)deadTime);

        //Should we ressurect? If so, return false. Uses the core's own reclaim
        // predicate (39 yd 3D, same call the core enforces in MiscHandler) so the
        // ghost stops walking exactly when a reclaim would succeed - never earlier
        // (phantom yield outside reclaim range) and never later (walking past the
        // point where ReviveFromCorpse would fire). The old 34 yd 3D gate left a
        // dead band: at 34-39 yd FindCorpse kept dispatching MoveTo to a flee/
        // sidestep point ~34 yd away while ReviveFromCorpse could already have
        // reclaimed, and each dispatch re-launched a POINT spline the bot never
        // rode (live: byte-identical from-positions for 5+ min, dist 34-38).
        bool insideReclaimRange = corpse->IsWithinDistInMap(bot, (float)CORPSE_RECLAIM_RADIUS, true);
        if (insideReclaimRange)
        {
            if (moveToMaster) //We are near master.
            {
                if (botPos.fDist(masterPos) < sPlayerbotAIConfig.spellDistance)
                {
                    sLog.outDetail("[BOT CORPSE] %s: find corpse - within core reclaim range & near master, yielding to revive-from-corpse", bot->GetName());
                    return false;
                }
                // En route to the master but not yet in spell range: the master
                // may stand 26-34 yd from the corpse, so keep walking (the move
                // target below stays masterPos). Only the 8-min walked-too-long
                // backstop below may still yield - it runs for master legs too so
                // a stale one can never strand the ghost past it.
            }
            // Core reclaim delay still running: park the ghost where it is and wait
            // for the delay to expire. Walking closer cannot help (already inside
            // the 39 yd range) and risks gliding into a pack; the delay branch in
            // ReviveFromCorpseAction reclaims the moment it expires. Without this
            // the ghost walks to the corpse, MoveTo at destination returns false,
            // and the fallback below abandons the run for the spirit healer.
            // A ghost still walking to its master keeps walking.
            int64 reclaimWait = corpse->GetGhostTime() + bot->GetCorpseReclaimDelay(corpse->GetType() == CORPSE_RESURRECTABLE_PVP) - time(nullptr);
            if (reclaimWait > 0 && !moveToMaster)
            {
                ai->StopMoving();
                time_t const nowWait = time(nullptr);
                if (nowWait - AI_VALUE2(time_t, "manual time", "reclaim wait logged") >= 15)
                {
                    SET_AI_VALUE2(time_t, "manual time", "reclaim wait logged", nowWait);
                    sLog.outDetail("[BOT CORPSE] %s: find corpse - inside reclaim range, waiting out reclaim delay (%llds left)",
                        bot->GetName(), (long long)reclaimWait);
                }
                return true;
            }
            if (deadTime > 8 * MINUTE) //We have walked too long already.
            {
                sLog.outDetail("[BOT CORPSE] %s: find corpse - within core reclaim range & deadTime>8min, yielding to revive-from-corpse", bot->GetName());
                return false;
            }
            if (!moveToMaster)
            {
                std::list<ObjectGuid> units = AI_VALUE(std::list<ObjectGuid>, "possible targets no los");

                if (botPos.GetUnitsAggro(units, bot) == 0) //There are no mobs near.
                {
                    sLog.outDetail("[BOT CORPSE] %s: find corpse - within core reclaim range & no mobs near, yielding to revive-from-corpse", bot->GetName());
                    return false;
                }
            }
        }

        // Walk straight at the corpse only when already inside the core reclaim
        // range, ungrouped, and with no flee-worthy threat: any sidestep target
        // ~34 yd away re-launches a POINT spline every tick that the bot never
        // rides (the 34-38 yd stall). A grouped ghost keeps its master target
        // (within spellDistance the yield gate above fires and the reclaim runs);
        // with a threat the FleeManager branch below still picks a safe vector
        // instead of the pack that killed the bot.
        bool walkStraightToCorpse = false;
        if (insideReclaimRange && !moveToMaster)
        {
            std::list<ObjectGuid> nearUnits = AI_VALUE(std::list<ObjectGuid>, "possible targets no los");
            FleeManager probe(bot, reclaimDist, 0.0, urand(0, 1), moveToPos);
            if (!probe.IsUseful() && botPos.GetUnitsAggro(nearUnits, bot) == 0)
                walkStraightToCorpse = true;
        }
        if (walkStraightToCorpse)
            moveToPos = corpsePos;
        else if (corpseDist < sPlayerbotAIConfig.reactDistance)
        {
            if (moveToMaster)
            {
                if (ai->HasStrategy("debug move", BotState::BOT_STATE_NON_COMBAT))
                {
                    std::ostringstream out;
                    out << "Moving to revive near master.";
                    ai->TellPlayerNoFacing(GetMaster(), out);
                }
                moveToPos = masterPos;
            }
            else
            {
                FleeManager manager(bot, reclaimDist, 0.0, urand(0, 1), moveToPos);

                if (manager.IsUseful())
                {
                    float rx, ry, rz;
                    if (manager.CalculateDestination(&rx, &ry, &rz))
                    {
                        if (ai->HasStrategy("debug move", BotState::BOT_STATE_NON_COMBAT))
                        {
                            std::ostringstream out;
                            out << "Moving to revive some where safe.";
                            ai->TellPlayerNoFacing(GetMaster(), out);
                        }
                        moveToPos = WorldPosition(moveToPos.GetMapId(), rx, ry, rz, 0.0);
                    }
                    else if (!moveToPos.GetReachableRandomPointOnGround(bot, reclaimDist, urand(0, 1)))
                    {
                        if (ai->HasStrategy("debug move", BotState::BOT_STATE_NON_COMBAT))
                        {
                            std::ostringstream out;
                            out << "Moving to revive at corpse.";
                            ai->TellPlayerNoFacing(GetMaster(), out);
                        }
                        moveToPos = corpsePos;
                    }
                }
                else
                {
                    // No flee-worthy threat near: still step aside onto reachable
                    // ground within the reclaim radius so the reclaim lands off the
                    // exact death spot (revive->death median was 129 s live), instead
                    // of inside the same pack that killed the bot.
                    WorldPosition sidestep = corpsePos;
                    if (sidestep.GetReachableRandomPointOnGround(bot, reclaimDist, true))
                        moveToPos = WorldPosition(moveToPos.GetMapId(), sidestep.getX(), sidestep.getY(), sidestep.getZ(), 0.0);
                }
            }
        }
        else
        {
            if (ai->HasStrategy("debug move", BotState::BOT_STATE_NON_COMBAT))
            {
                std::ostringstream out;
                out << "Moving towards corpse.";
                ai->TellPlayerNoFacing(GetMaster(), out);
            }
        }
    }
    //Actual mobing part.
    bool moved = false;

    if (!ai->AllowActivity(DETAILED_MOVE_ACTIVITY) && !ai->HasPlayerNearby(moveToPos))
    {
        uint32 delay = sServerFacade.getDistance2d(bot, moveToPos.getX(), moveToPos.getY()) / bot->GetSpeed(MOVE_RUN); //Time a bot would take to travel to destination.
        delay = std::min(delay, uint32(10 * MINUTE)); //Cap time to get to destination at 10 minutes.

        if (deadTime > delay)
        {
            sLog.outDetail("[BOT CORPSE] %s: find corpse - no detailed-move activity, teleporting to corpse (deadTime=%llds > delay=%us)",
                bot->GetName(), (long long)deadTime, delay);
            bot->GetMotionMaster()->Clear();
            bot->TeleportTo(moveToPos.GetMapId(), moveToPos.getX(), moveToPos.getY(), moveToPos.getZ(), 0);
            if (isRealPlayer_Helper(bot))
                bot->SendHeartBeat();
        }
        else
        {
            sLog.outDetail("[BOT CORPSE] %s: find corpse - no detailed-move activity, waiting out teleport delay (deadTime=%llds < delay=%us)",
                bot->GetName(), (long long)deadTime, delay);
        }

        moved = true;
    }
    else
    {
        if (bot->IsMoving())
            moved = true;
        if (moved)
        {
            sLog.outDetail("[BOT CORPSE] %s: find corpse - already moving towards corpse", bot->GetName());
        }
        else
        {
            // Ghost-movement triage: capture the motion stack before/after the
            // dispatch. MoveTo=true with a live POINT generator that never
            // displaces the bot is the stall signature; MoveTo=true with an
            // IDLE stack means nothing was launched. Logged throttled
            // (one line per bot per 30 s).
            WorldPosition ghostBefore(bot);
            float ghostDistBefore = ghostBefore.distance(moveToPos);
            int32 movegenBefore = bot->GetMotionMaster() ? (int32)bot->GetMotionMaster()->GetCurrentMovementGeneratorType() : -1;
            moved = MoveTo(moveToPos.GetMapId(), moveToPos.getX(), moveToPos.getY(), moveToPos.getZ(), false, false);
            int32 movegenAfter = bot->GetMotionMaster() ? (int32)bot->GetMotionMaster()->GetCurrentMovementGeneratorType() : -1;
            bool started = bot->IsMoving() || (bot->GetMotionMaster() && !bot->GetMotionMaster()->empty() && movegenAfter != (int32)IDLE_MOTION_TYPE);
            float ghostDistAfter = WorldPosition(bot).distance(moveToPos);
            uint32 posUnchangedSec = AI_VALUE2(uint32, "time since last change", "current position");
            // Per-tick during a stall; failures drive the spirit-healer retry
            // below, so log them at most once a minute too (same key covers the
            // MoveTo line and both failure lines - one line per minute max).
            time_t nowMove = time(nullptr);
            time_t lastMoveLog = AI_VALUE2(time_t, "manual time", "find corpse moveto");
            bool logMoveTo = !lastMoveLog || nowMove - lastMoveLog >= 60;
            if (logMoveTo)
                SET_AI_VALUE2(time_t, "manual time", "find corpse moveto", nowMove);
            if (logMoveTo)
                sLog.outDetail("[BOT CORPSE] %s: find corpse - MoveTo(%.1f,%.1f,%.1f) returned %s",
                    bot->GetName(), moveToPos.getX(), moveToPos.getY(), moveToPos.getZ(), moved ? "true" : "false");
            // Throttled ghost-move CSV row (30 s key inside LogGhostMoveDiag):
            // the stall signature is MoveTo=true with a POINT generator that
            // never displaces the bot. A false return already falls through to
            // the spirit-healer retry below, so it needs no triage row.
            if (moved)
                LogGhostMoveDiag(ai, bot, ghostBefore, moveToPos, ghostDistBefore, movegenBefore, movegenAfter,
                    started, moved, ghostDistAfter, posUnchangedSec);

            if (!moved && !ai->HasActivePlayerMaster()) //We could not move to coprse. Try spirithealer instead.
            {
                if (logMoveTo)
                    sLog.outDetail("[BOT CORPSE] %s: find corpse - MoveTo failed & no active player master, trying spirit healer", bot->GetName());
                moved = ai->DoSpecificAction("spirit healer", Event(), true);
            }
            else if (!moved)
            {
                if (logMoveTo)
                    sLog.outDetail("[BOT CORPSE] %s: find corpse - MoveTo failed but has active player master, NOT using spirit healer -> FAILED loop", bot->GetName());
            }
        }
    }

    return moved;
}

bool FindCorpseAction::isUseful()
{
    if (bot->InBattleGround())
        return false;

    return bot->GetCorpse();
}

bool SpiritHealerAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    Corpse* corpse = bot->GetCorpse();
    if (!corpse)
    {
        // A ghost whose corpse the core already removed has nothing to reclaim, and every
        // graveyard lookup below is corpse-based: it used to fail here silently forever.
        // Do what a player would: resurrect at the nearest spirit healer.
        if (bot->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST))
        {
            sLog.outDetail("Bot #%d <%s> is a ghost without a corpse, reviving at the nearest graveyard", bot->GetGUIDLow(), bot->GetName());
            bot->RepopAtGraveyard();
            bot->ResurrectPlayer(0.5f, !ai->HasCheat(BotCheatMask::repair));
            bot->DurabilityLossAll(0.25f, true);
            bot->SaveToDB();
            SET_AI_VALUE(bool, "corpse run", false);
            sPlayerbotAIConfig.logEvent(ai, "ReviveFromSpiritHealerAction", "no corpse");
            return true;
        }

        ai->TellPlayerNoFacing(requester, "I am not a spirit");
        return false;
    }

    uint32 dCount = AI_VALUE(uint32, "death count");
    GuidPosition grave = AI_VALUE(GuidPosition, "best graveyard");

    //something went wrong
    if (!grave)
    {
        //prevent doing weird stuff OR GOING TO 0,0,0
        sLog.outDetail(
            "ERROR: no graveyard in SpiritHealerAction for bot #%d %s:%d <%s>, evacuating to prevent weird behavior",
            bot->GetGUIDLow(),
            bot->GetTeam() == ALLIANCE ? "A" : "H",
            bot->GetLevel(),
            bot->GetName()
        );
        ai->DoSpecificAction("repop");
        return false;
    }

    if (grave && grave.fDist(bot) < sPlayerbotAIConfig.sightDistance)
    {
        bool foundSpiritHealer = false;
        std::list<ObjectGuid> npcs = AI_VALUE(std::list<ObjectGuid>, "nearest npcs");
        for (std::list<ObjectGuid>::iterator i = npcs.begin(); i != npcs.end(); i++)
        {
            Unit* unit = ai->GetUnit(*i);
            if (unit && unit->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_SPIRITHEALER))
            {
                foundSpiritHealer = true;
                break;
            }
        }

        if (!foundSpiritHealer)
        {
            sLog.outDetail("Bot #%d %s:%d <%s> can't find a spirit healer", bot->GetGUIDLow(), bot->GetTeam() == ALLIANCE ? "A" : "H", bot->GetLevel(), bot->GetName());
            ai->TellPlayerNoFacing(requester, "Cannot find any spirit healer nearby");
        }


        sLog.outDetail("Bot #%d %s:%d <%s> revives at spirit healer", bot->GetGUIDLow(), bot->GetTeam() == ALLIANCE ? "A" : "H", bot->GetLevel(), bot->GetName());
        PlayerbotChatHandler ch(bot);
        bot->ResurrectPlayer(0.5f, !ai->HasCheat(BotCheatMask::repair));
        bot->DurabilityLossAll(0.25f, true);

        bot->SpawnCorpseBones();
        bot->SaveToDB();
        SET_AI_VALUE(bool, "corpse run", false);
        // Same post-rez rescue as the corpse path: hopeless mismatch relocates
        // once instead of GY-camping (see above).
        TortoiseBots::BotManager::Instance().RelocateHopelessBot(bot);
        // Deliberately NOT resetting "death count" here. BestGraveyardValue only
        // switches to a graveyard outside the current zone once the count reaches
        // DEATH_COUNT_BEFORE_TRYING_ANOTHER_GRAVEYARD - but a bot resurrecting is
        // exactly the moment the count has to survive. Resetting here pinned it at
        // 1 forever, so the escape hatch was unreachable and bots that died inside
        // an enemy town (Razor Hill, Aerie Peak) resurrected into the same guards
        // indefinitely. The count is still cleared by XpGainAction, i.e. once the
        // bot is alive and earning again.
        context->GetValue<Unit*>("current target")->Set(nullptr);
        bot->SetSelectionGuid(ObjectGuid());
        ai->TellPlayer(requester, BOT_TEXT("hello"), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        sPlayerbotAIConfig.logEvent(ai, "ReviveFromSpiritHealerAction");

        return true;
    }

    bool shouldTeleportToGY = false;

    const int64 deadTime = time(nullptr) - corpse->GetGhostTime();

    // Prevent taking too long to go to corpse (10 mins)
    // no need to wait longer, because bot is probably stuck in navigating issues
    shouldTeleportToGY = deadTime > uint32(10 * MINUTE);

    // Check if we can teleport to the graveyard when nobody is looking
    if (!shouldTeleportToGY && !ai->AllowActivity(DETAILED_MOVE_ACTIVITY) && !ai->HasPlayerNearby(WorldPosition(grave)))
    {
        //Time a bot would take to travel to it's corpse.
        uint32 delay = sServerFacade.getDistance2d(bot, corpse) / bot->GetSpeed(MOVE_RUN);
        //Cap time to get to corpse at 10 minutes.
        delay = std::min(delay, uint32(10 * MINUTE));

        shouldTeleportToGY = deadTime > delay;
    }

    if (ai->HasStrategy("debug move", BotState::BOT_STATE_NON_COMBAT))
    {
        std::ostringstream out;
        out << "Moving towards graveyard.";
        ai->TellPlayerNoFacing(GetMaster(), out);
    }

    if (shouldTeleportToGY)
    {
        bot->GetMotionMaster()->Clear();
        bot->TeleportTo(grave.GetMapId(), grave.getX(), grave.getY(), grave.getZ(), 0);
        if (isRealPlayer_Helper(bot))
            bot->SendHeartBeat();
        return true;
    }
    else
    {
        return MoveTo(grave.GetMapId(), grave.getX(), grave.getY(), grave.getZ(), false, false);
    }
}

bool SpiritHealerAction::isUseful()
{
    if (bot->InBattleGround())
        return false;

    return bot->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST);
}
