
#include "playerbot/playerbot.h"
#include "AttackersValue.h"
#include "PossibleTargetsValue.h"
#include "EnemyPlayerValue.h"
#include "WorldPvpSeekChecks.h"
#include "playerbot/policy/combat/AttackersAoeDensityPolicy.h"
#include "Movement/spline/MoveSpline.h"
#include "playerbot/policy/combat/RunnerFinishPolicy.h"
#include "playerbot/CharmYieldPolicy.h"
#include "Maps/PathFinder.h"
#include "runtime/RunReleaseHold.h"

using namespace ai;
using namespace MaNGOS;

std::list<ObjectGuid> AttackersValue::Calculate()
{
    std::list<ObjectGuid> result;
    if (!ai->AllowActivity(ALL_ACTIVITY))
        return result;

    if (!bot->IsInWorld())
        return result;

    if (bot->IsBeingTeleported())
        return result;

    if (bot->IsFlying() && WorldPosition(bot).currentHeight() > 10.0f)
        return result;

    // Lost control, e.g. a battleground ended.
    if (bot->HasUnitState(UNIT_STAT_CAN_NOT_REACT_OR_LOST_CONTROL))
        return result;

    if (ai->HasStrategy("focus rti targets", BotState::BOT_STATE_COMBAT))
    {
        Unit* rtiTarget = AI_VALUE(Unit*, "rti target");

        // A mark the aura keep-out set aside is no attacker here either:
        // this list skips IgnoreTarget, so the tournament would hand the
        // ringed mark straight back (hold, set aside, re-pick).
        if (rtiTarget && rtiTarget->IsInWorld() && rtiTarget->GetMapId() == bot->GetMapId() &&
            !IsKeepoutSetAside(rtiTarget, bot))
        {
            return { rtiTarget->getObjectGuid() };
        }

        return result;
    }

    // Check if we only need one attacker
    bool getOne = false;
    if (!qualifier.empty())
    {
        getOne = atoi(qualifier.c_str()) != 0;
    }

    if (sPlayerbotAIConfig.shareTargets)
    {
        // Try to get the value from nearby friendly bots.
        std::list<ObjectGuid> nearGuids = ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid> >("nearest friendly players")->Get();
        for (auto& i : nearGuids)
        {
            Player* player = sObjectMgr.GetPlayer(i);

            if (!player)
                continue;

            if (player == bot)
                continue;

            if (!ai->IsSafe(player))
                continue;

            if (sServerFacade.getDistance2d(bot, player) > 10.0f)
                continue;

            PlayerbotAI* botAi = PlayerbotAIStorage::Instance().GetAI(player);

            if (!botAi)
                continue;

            std::string valueName = "attackers" + (!qualifier.empty() ? "::" + qualifier : "");

            // Ignore bots without the value.
            if (!PHAS_AI_VALUE(valueName))
                continue;

            UntypedValue* pValue = botAi->GetAiObjectContext()->GetUntypedValue(valueName);

            AttackersValue* pAttackersValue = dynamic_cast<AttackersValue*>(pValue);

            if (!pAttackersValue)
                continue;

            // Ignore expired values.
            if (pAttackersValue->Expired())
                continue;

            // Never copy an empty list: the source simply had nothing at scan
            // time, which says nothing about us (#549). Scanning fresh here is
            // still cheaper than staying blind and re-resetting every 2 s.
            if (PAI_VALUE(std::list<ObjectGuid>, valueName).empty())
                continue;

            if (pAttackersValue->calculatePos.sqDistance2d(bot) > 100.0f)
                continue;

            // Make the value expire at the same time as the copied value.
            lastCheckTime = pAttackersValue->lastCheckTime;

            calculatePos = pAttackersValue->calculatePos;

            result = PAI_VALUE(std::list<ObjectGuid>, valueName);

            std::vector<std::string> specificTargetNames = { "current target","old target","attack target","pull target" };
            Unit* target;

            // The other bot's own targets stay in the copied list. Its current
            // target is usually the mob the party is fighting; dropping it left
            // a tank or healer standing next to the mage with no attackers, so
            // they left combat and followed the master while the rogue fought
            // on. The validation below already drops any of them that does not
            // threaten us or a groupmate, which is what a fresh scan keeps.

            // Append bot specific targets of this bot in natural priority order.
            for (auto& targetName : specificTargetNames)
            {
                target = (targetName == "attack target") ? ai->GetUnit(AI_VALUE(ObjectGuid, targetName)) : AI_VALUE(Unit*, targetName);
                if (target)
                    result.push_back(target->getObjectGuid());
            }

            // Validate these targets and enforce the invariant of distinct hostile units,
            // preserving original sequence (first-seen stable deduplication).
            // A copied target counts when it threatens us or anyone we fight for
            // (group, master) - the same rule a fresh scan applies. Validating
            // against ourselves only dropped every threat aimed at a groupmate,
            // so grouped bots never engaged (#549). The dedup/first-seen/getOne
            // loop is the shared DistinctValidTargets policy rule.
            return DistinctValidTargets(result,
                [this](const ObjectGuid& guid)
                {
                    return IsValidForGroup(ai->GetUnit(guid));
                },
                getOne);
        }
    }

    calculatePos = bot;

    std::set<Unit*> targets;
    std::set<ObjectGuid> invalidTargets;

    // Add the targets of the bot
    AddTargetsOf(bot, targets, invalidTargets, getOne);

    // Don't check for group member targets if we only need one
    if (targets.empty() || !getOne)
    {
        // Add the targets of the members of the group
        Group* group = bot->GetGroup();
        if (group && !bot->InBattleGround())
        {
            AddTargetsOf(group, targets, invalidTargets, getOne);
        }

        Player* master = GetMaster();
        if (master && !bot->InBattleGround() && (!group || master->GetGroup() != group))
            AddTargetsOf(master, targets, invalidTargets, getOne);
    }

    // Convert the targets to guids, ensuring distinctness
    std::list<ObjectGuid> distinctResult;
    std::set<ObjectGuid> seen;
    for (Unit* target : targets)
    {
        if (target && seen.insert(target->getObjectGuid()).second)
        {
            distinctResult.push_back(target->getObjectGuid());
        }
    }

    return distinctResult;
}

void AttackersValue::AddTargetsOf(Group* group, std::set<Unit*>& targets, std::set<ObjectGuid>& invalidTargets, bool getOne)
{
    Group::MemberSlotList const& groupSlot = group->GetMemberSlots();
    for (Group::member_citerator itr = groupSlot.begin(); itr != groupSlot.end(); itr++)
    {
        // Only add group member targets that are alive and near the player
        Player* member = sObjectMgr.GetPlayer(itr->guid);
        if (member && (member != bot) &&
           sServerFacade.IsAlive(member) &&
           member->IsInWorld() &&
           !member->IsBeingTeleported() &&
           (member->GetMapId() == bot->GetMapId()) &&
           (sServerFacade.getDistance2d(bot, member) <= GetRange()))
        {
            AddTargetsOf(member, targets, invalidTargets, getOne);

            // Finish early if we only need one target
            if (getOne && !targets.empty())
            {
                break;
            }
        }
    }
}

bool AttackersValue::IsValidForGroup(Unit* target)
{
    if (IsValid(target, bot, bot))
        return true;

    // Same membership rule as the fresh scan in AddTargetsOf(Group*): only
    // alive, nearby members on our map fight for each other.
    Group* group = bot->GetGroup();
    if (group && !bot->InBattleGround())
    {
        Group::MemberSlotList const& groupSlot = group->GetMemberSlots();
        for (Group::member_citerator itr = groupSlot.begin(); itr != groupSlot.end(); itr++)
        {
            Player* member = sObjectMgr.GetPlayer(itr->guid);
            if (member && (member != bot) &&
               sServerFacade.IsAlive(member) &&
               member->IsInWorld() &&
               !member->IsBeingTeleported() &&
               (member->GetMapId() == bot->GetMapId()) &&
               (sServerFacade.getDistance2d(bot, member) <= GetRange()))
            {
                if (IsValid(target, member, bot))
                    return true;
            }
        }
    }

    Player* master = GetMaster();
    if (master && !bot->InBattleGround() && (!group || master->GetGroup() != group))
    {
        if (IsValid(target, master, bot))
            return true;
    }

    return false;
}

void AttackersValue::AddTargetsOf(Player* player, std::set<Unit*>& targets, std::set<ObjectGuid>& invalidTargets, bool getOne)
{
    // If the player is available
    if (ai->IsSafe(player))
    {
        std::set<Unit*> units;

        // If the player is a bot
        PlayerbotAI* playerBot = PlayerbotAIStorage::Instance().GetAI(player);
        if (playerBot)
        {
            // Get all the units around the player
            // NOTE: We don't validate the value here because it will be validated later on
            const std::string ignoreValidate = std::to_string(true);
            const std::string range = std::to_string((int32)GetRange());
            const std::vector<std::string> qualifiers = { range, ignoreValidate };
            const std::list<ObjectGuid> possibleTargets = PAI_VALUE2(std::list<ObjectGuid>, "possible targets", Qualified::MultiQualify(qualifiers, ":"));
            for (const ObjectGuid& guid : possibleTargets)
            {
                if (Unit* unit = ai->GetUnit(guid))
                {
                    units.insert(unit);
                }
            }

            // Add the current target
            Unit* currentTarget = PAI_VALUE(Unit*, "current target");
            if (currentTarget)
            {
                units.insert(currentTarget);
            }

            // Add the previous target
            Unit* oldTarget = PAI_VALUE(Unit*, "old target");
            if (oldTarget)
            {
                units.insert(oldTarget);
            }

            // Add the pull and attack targets (Only consider the owner bot)
            if (bot == player)
            {
                Unit* attackTarget = playerBot->GetUnit(PAI_VALUE(ObjectGuid, "attack target"));
                if (attackTarget)
                {
                    units.insert(attackTarget);
                }

                Unit* pullTarget = PAI_VALUE(Unit*, "pull target");
                if (pullTarget)
                {
                    units.insert(pullTarget);
                }
            }
        }

        // Get the current attackers of the player
        for (Unit* attacker : player->GetAttackers())
        {
            units.insert(attacker);
        }

        // Add the duel opponent (Only consider the owner bot)
        if (bot == player && bot->m_duel && bot->m_duel->opponent)
        {
            // Penqle's DuelInfo::opponent is an ObjectGuid; resolve to Unit*.
            if (Unit* opp = ObjectAccessor::GetUnit(*bot, bot->m_duel->opponent))
                units.insert(opp);
        }

        // Add the pet attackers (if nearby)
        Pet* pet = player->GetPet();
        if (pet && sServerFacade.getDistance2d(bot, pet) <= GetRange())
        {
            for (Unit* attacker : pet->GetAttackers())
            {
                units.insert(attacker);
            }
        }

        // Filter the units that are valid
        for (Unit* unit : units)
        {
            if (!ai->IsSafe(unit))
                continue;

            // Prevent checking a target that has already been validated
            if((targets.find(unit) == targets.end()))
            {
                // Prevent checking a target that has already been invalidated
                if(InCombat(unit, player) || (invalidTargets.find(unit->getObjectGuid()) == invalidTargets.end()))
                {
                    if (IsValid(unit, player, bot))
                    {
                        // Add the target to the list of combat targets
                        targets.insert(unit);

                        // Add the target's pet/guardian too
                        unit->CallForAllControlledUnits(AddGuardiansHelper(targets), CONTROLLED_PET | CONTROLLED_GUARDIANS | CONTROLLED_CHARM | CONTROLLED_MINIPET | CONTROLLED_TOTEMS);

                        if (getOne)
                        {
                            break;
                        }
                    }
                    else
                    {
                        invalidTargets.insert(unit->getObjectGuid());
                    }
                }
            }
        }
    }
}

bool AttackersValue::InCombat(Unit* target, Player* player, bool checkPullTargets)
{
    // Check if the the target is attacking the player
    bool inCombat = (target->GetThreatManager().getThreat(player) > 0.0f) ||
                    (target->GetVictim() && (target->GetVictim() == player));

    // Check if the target is attacking the player's pet
    if(!inCombat)
    {
        Pet* pet = player->GetPet();
        if (pet)
        {
            inCombat = (target->GetThreatManager().getThreat(pet) > 0.0f) ||
                       (target->GetVictim() && (target->GetVictim() == pet));
        }
    }

    if(!inCombat && checkPullTargets && PlayerbotAIStorage::Instance().GetAI(player))
    {
        inCombat = (PAI_VALUE(ObjectGuid, "attack target") == target->getObjectGuid()) ||
                   (PAI_VALUE(Unit*, "pull target") == target);
    }

    return inCombat;
}

bool AttackersValue::CharmedAllyOffLimits(Unit* target, Player* player)
{
    if (!sPlayerbotAIConfig.dungeonRunCharmYield || !target || !player)
        return false;
    Player* ally = dynamic_cast<Player*>(target);
    if (!ally || ally == player)
        return false;
    Group* group = player->GetGroup();
    bool const sameGroup = group && ally->GetGroup() == group;
    ObjectGuid const charmer = ally->GetCharmerGuid();
    return ai::CharmedAllyIsOffLimits(true, sameGroup, !charmer.IsEmpty(), charmer.IsPlayer());
}

bool AttackersValue::IsValid(Unit* target, Player* player, Player* owner, bool checkInCombat, bool validatePossibleTarget)
{
    Player* playerToCheckAgainst = owner != nullptr ? owner : player;

    // AiPlayerbot.DungeonRunCharmYield: a group member a creature is charming is nobody's target, an explicit attack
    // order included (the order itself is refused on the attack path).
    if (target && CharmedAllyOffLimits(target, playerToCheckAgainst))
        return false;

    // An explicit attack order is the player's pull: the target is valid before either
    // side has threat, so it passes the combat-only gate below until the pull begins.
    // Every other check (friendly, evade, range, CC, immunity) still applies.
    bool explicitAttackTarget = false;
    if (target)
    {
        if (PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(playerToCheckAgainst))
            explicitAttackTarget = ai->GetAiObjectContext()->GetValue<ObjectGuid>("explicit attack target")->Get() == target->getObjectGuid();
    }
    // Validate possible target
    if (validatePossibleTarget && !PossibleTargetsValue::IsValid(target, playerToCheckAgainst))
    {
        return false;
    }

    // This will be used on both enemy player and npc checks
    const bool inPvPProhibitedZone = sPlayerbotAIConfig.IsInPvpProhibitedZone(sServerFacade.GetAreaId(target));

    // If the target is a player
    Player* enemyPlayer = dynamic_cast<Player*>(target);
    if (enemyPlayer)
    {
        // Don't consider enemy players if neither the battleground pvp
        // strategy nor the open-world seek role is set.
        if (PlayerbotAI* targetAi = PlayerbotAIStorage::Instance().GetAI(playerToCheckAgainst))
        {
            if (!targetAi->HasStrategy("pvp", BotState::BOT_STATE_COMBAT) &&
                !WorldPvpSeekChecks::IsSeeking(targetAi, playerToCheckAgainst))
            {
                return false;
            }
        }

        // A masterless random bot that died to this named player recently does not
        // INITIATE a fight with it again; the Ashenvale loop ran every ~60 s because
        // both sides kept re-selecting each other after each revive. Owned bots (real
        // master) are exempt. Same gate as EnemyPlayersValue: while the bot is out of
        // combat with no attackers the avoided killer is refused (duel/RTI paths below
        // still apply); once the bot is in combat, normal selection applies and
        // retaliation works against melee, ranged and casters with no new heuristics.
        if (PlayerbotAI* playerAi = PlayerbotAIStorage::Instance().GetAI(playerToCheckAgainst))
        {
            if (!playerAi->HasRealPlayerMaster() && playerAi->ShouldAvoidPlayerKiller(enemyPlayer->GetName()))
            {
                if (!player->IsInCombat() && player->GetAttackers().empty())
                {
                    Unit* rtiTarget = nullptr;
                    if (!playerAi->HasActivePlayerMaster())
                        rtiTarget = PAI_VALUE(Unit*, "rti target");
                    if (target != rtiTarget)
                        return false;
                }
            }
        }
        // If the enemy player is in a PVP Prohibited zone
        if (inPvPProhibitedZone)
        {
            return false;
        }

        const bool isDuelOpponent = player->m_duel && player->m_duel->opponent == target->getObjectGuid();

        // Don't check distance on duel opponents
        if (!isDuelOpponent)
        {
            // If the enemy player is not within sight distance
            if (!enemyPlayer->IsWithinDist(playerToCheckAgainst, EnemyPlayerValue::GetMaxAttackDistance(playerToCheckAgainst), false))
            {
                return false;
            }
        }

        // Unlike the NPC branch below, this had no actual-combat check at all: a stale
        // "current target"/"old target" reference to a player who stopped fighting long ago
        // (or was never fighting to begin with) stayed "valid" forever just by being nearby
        // and PvP-flagged, keeping "has attackers" true and the bot latched into combat
        // state with nothing to do (see AttackersValue::AddTargetsOf and InCombat()).
        if (checkInCombat && !explicitAttackTarget && !isDuelOpponent && !InCombat(target, player, (player == owner)))
        {
            bool isRtiTarget = false;
            if (PlayerbotAIStorage::Instance().GetAI(player) && !PlayerbotAIStorage::Instance().GetAI(player)->HasActivePlayerMaster())
            {
                Unit* rtiTarget = PAI_VALUE(Unit*, "rti target");
                if (target == rtiTarget)
                    isRtiTarget = true;
            }

            if (!isRtiTarget)
                return false;
        }
    }
    // If the target is a NPC
    else
    {
        // Some callers (GrindTargetValue::FindTargetForGrinding, PullRequestAction)
        // pass validatePossibleTarget=false to reuse this function purely for its
        // combat/evade checks, which skips the friendliness gate above entirely.
        // Re-check here so a friendly NPC referenced by a stale "current target"/
        // "attack target"/"pull target" (the same staleness this function already
        // guards against for players, above) is never treated as a valid attacker.
        if (sServerFacade.IsFriendlyTo(playerToCheckAgainst, target))
        {
            return false;
        }

        // If the target is not fighting the player (and if the owner bot is not pulling the target)
        if (checkInCombat && !explicitAttackTarget && !InCombat(target, player, (player == owner)))
        {
            bool isRtiTarget = false;
            if (PlayerbotAIStorage::Instance().GetAI(player) && !PlayerbotAIStorage::Instance().GetAI(player)->HasActivePlayerMaster())
            {
                Unit* rtiTarget = PAI_VALUE(Unit*, "rti target");
                if (target == rtiTarget)
                    isRtiTarget = true;
            }

            if(!isRtiTarget)
                return false;
        }

        // If the target is a player's pet and in a PvP prohibited zone
        if (target->getObjectGuid().IsPet() && inPvPProhibitedZone)
        {
            return false;
        }

        // If the target is evading
        const Creature* creature = dynamic_cast<Creature*>(target);
        if (creature)
        {
            if (creature->IsInEvadeMode())
            {
                return false;
            }
        }
    }

    if (IgnoreTarget(target, playerToCheckAgainst))
        return false;

    return true;
}

bool AttackersValue::IsKeepoutSetAside(Unit* target, Player* bot)
{
    PlayerbotAI* ai = target && bot ? PlayerbotAIStorage::Instance().GetAI(bot) : nullptr;
    if (!ai)
        return false;
    std::map<ObjectGuid, uint32>& setAside =
        ai->GetAiObjectContext()->GetValue<std::map<ObjectGuid, uint32>&>("keepout set aside")->Get();
    if (setAside.empty())
        return false;
    auto entry = setAside.find(target->getObjectGuid());
    if (entry == setAside.end())
        return false;
    bool const isKeepoutCreature = PossibleTriggersValue::KeepoutFor(target) != nullptr;
    bool const live = WorldTimer::getMSTime() < entry->second;
    bool const isTank = PlayerbotAI::IsTank(bot);
    // Line of sight and the ring test only for a mob that is no keep-out
    // creature, and only while the entry could still hold (no world scan: the
    // cached "possible triggers" list is the one the hold plans against).
    bool hitsBotInSight = false;
    bool stillRinged = false;
    if (live && !isTank && !isKeepoutCreature)
    {
        hitsBotInSight = target->GetVictim() == bot && bot->IsWithinLOSInMap(target, true);
        if (!hitsBotInSight)
        {
            // Ringed means: a listed creature of the cached list stands within
            // its release ring of the mob. A mob with none counts as unringed
            // at any distance from the bot (dragged out far, or run to a
            // healer): a wrong lift costs one walk and one 3 s hold, and the
            // set-aside then sticks again.
            float const tx = target->GetPositionX(), ty = target->GetPositionY();
            std::list<ObjectGuid> const& triggers =
                ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("possible triggers")->Get();
            for (ObjectGuid const& guid : triggers)
            {
                Unit* unit = ai->GetUnit(guid);
                CreatureAuraKeepout const* row = PossibleTriggersValue::KeepoutFor(unit);
                if (!row)
                    continue;
                float const dx = tx - unit->GetPositionX(), dy = ty - unit->GetPositionY();
                if (CreatureAuraSetAsideRinged(*row, std::sqrt(dx * dx + dy * dy)))
                {
                    stillRinged = true;
                    break;
                }
            }
        }
    }
    if (CreatureAuraSetAsideHolds(live, isKeepoutCreature, isTank, hitsBotInSight, stillRinged))
        return true;
    setAside.erase(entry);
    return false;
}

bool AttackersValue::IsRunningMob(Unit* unit)
{
    if (!unit)
        return false;
    if (unit->HasUnitState(UNIT_STAT_FLEEING))
        return true;
    switch (unit->GetMotionMaster()->GetCurrentMovementGeneratorType())
    {
        case FLEEING_MOTION_TYPE:
        case TIMED_FLEEING_MOTION_TYPE:
        case ASSISTANCE_MOTION_TYPE:
        case ASSISTANCE_DISTRACT_MOTION_TYPE:
            return true;
        default:
            return false;
    }
}

float AttackersValue::RunnerDropYd(Unit* target, Player* bot)
{
    // An uninitialized spline answers (0,0,0): never read as the end of a run.
    bool const runEndKnown = target->movespline->Initialized() && !target->movespline->Finalized();
    float const runEndZ = runEndKnown ? target->movespline->FinalDestination().z : 0.0f;
    return ai::RunnerDropYd(bot->GetPositionZ(), target->GetPositionZ(), runEndKnown, runEndZ);
}

// The way from `bot` to the lower of `target` and the end of its run: complete
// (PATHFIND_NORMAL) or not, and a cliff on it (ai::PathHasCliff). One navmesh
// path; the caller holds the verdict for kRunnerLedgeReadMs.
static void ReadRunnerWay(Unit* target, Player* bot, bool& complete, bool& cliff)
{
    float tx = target->GetPositionX(), ty = target->GetPositionY(), tz = target->GetPositionZ();
    if (target->movespline->Initialized() && !target->movespline->Finalized())
    {
        Vector3 const end = target->movespline->FinalDestination();
        if (end.z < tz)
        {
            tx = end.x;
            ty = end.y;
            tz = end.z;
        }
    }
    PathFinder path(bot);
    path.calculate(tx, ty, tz, false);
    complete = path.getPathType() == PATHFIND_NORMAL;
    std::vector<ai::RunnerPathPoint> points;
    ai::RunnerPathPoint front;
    front.x = bot->GetPositionX();
    front.y = bot->GetPositionY();
    front.z = bot->GetPositionZ();
    points.push_back(front);
    for (Vector3 const& v : path.getPath())
    {
        ai::RunnerPathPoint point;
        point.x = v.x;
        point.y = v.y;
        point.z = v.z;
        points.push_back(point);
    }
    cliff = ai::PathHasCliff(points);
}

bool AttackersValue::RunnerLetGoAtLedge(Unit* target, Player* bot)
{
    PlayerbotAI* ai = bot ? PlayerbotAIStorage::Instance().GetAI(bot) : nullptr;
    if (!ai)
        return false;
    AiObjectContext* context = ai->GetAiObjectContext();
    std::map<ObjectGuid, ai::RunnerLedgeEntry>& book =
        context->GetValue<std::map<ObjectGuid, ai::RunnerLedgeEntry>&>("runners let go")->Get();
    uint32 const now = WorldTimer::getMSTime();
    // Every access: what ran out goes, and the book never grows past its cap.
    ai::PurgeRunnerBook(book, now);
    if (!target || target->IsPlayer() || !target->IsInWorld() || !bot->IsInWorld() ||
        target->GetMapId() != bot->GetMapId())
        return false;
    bool const running = IsRunningMob(target);
    if (!running && book.empty())
        return false; // the common case: nothing to read
    ObjectGuid const guid = target->getObjectGuid();
    // An explicit attack order (the player's or the commander's) is chased:
    // it clears any let-go of that target too.
    if (context->GetValue<ObjectGuid>("explicit attack target")->Get() == guid)
    {
        book.erase(guid);
        return false;
    }
    ai::RunnerFacts facts;
    facts.alive = target->IsAlive();
    facts.running = running;
    facts.dropYd = RunnerDropYd(target, bot);
    auto entry = book.find(guid);
    if (entry != book.end() && entry->second.letGoUntil > now)
    {
        if (facts.alive && ai::RunnerStaysLetGo(true, now, entry->second.letGoUntil, facts.dropYd))
            return true;
        entry->second.letGoUntil = 0; // back up on the bot's floor (or dead): a target again
    }
    // The cheap filters first: a ledge candidate of a bot bound to an active
    // dungeon run (random bots and normal groups are never touched).
    if (!ai::RunnerOverLedge(facts) || !TortoiseBots::RunReleaseHold::HeldByRun(bot))
        return false;
    bool complete = true, cliff = false;
    if (entry != book.end() && entry->second.readUntil > now)
    {
        cliff = entry->second.cliff; // the verdict below: an incomplete way is stored as a cliff
    }
    else
    {
        ReadRunnerWay(target, bot, complete, cliff);
        cliff = cliff || !complete;
        ai::RunnerLedgeEntry& read = book[guid];
        read.readUntil = now + ai::kRunnerLedgeReadMs;
        read.cliff = cliff;
        entry = book.find(guid);
    }
    if (!ai::LetRunnerGoAtLedge(true, false, facts, true, cliff))
        return false;
    entry->second.letGoUntil = now + ai::kRunnerLetGoMs;
    ai::PurgeRunnerBook(book, now);
    return true;
}

bool AttackersValue::IgnoreTarget(Unit* target, Player* playerToCheckAgainst)
{
    if (!PlayerbotAIStorage::Instance().GetAI(playerToCheckAgainst))
        return false;

    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(playerToCheckAgainst);
    AiObjectContext* context = ai->GetAiObjectContext();

    // Chases stop at ledges: a runner more than 6 yd below the bot's floor
    // (or running there) is let go, and stays let go while it is down there.
    if (RunnerLetGoAtLedge(target, playerToCheckAgainst))
        return true;

    // A target the bot gave up on (out of line of sight with no way to close the
    // distance, see ReachTargetAction) is left alone for a while - unless it comes
    // after the bot itself, in which case it is plainly reachable. A creature that is
    // evading, or that the core has flagged as unable to reach the bot, is the
    // exception to that exception: it "attacks" the bot from a place it cannot be
    // reached from, so treating it as reachable erased the give-up on the next tick,
    // kept it in the bot's attacker list (combat never ended) and had the bot re-target
    // it forever (a Murloc Streamrunner held a bot for 15+ minutes at victim=1,
    // inLos=1, notreach=1, botmove=0).
    bool const cannotBeReached = target->IsCreature() &&
        (static_cast<Creature*>(target)->IsInEvadeMode() || static_cast<Creature*>(target)->IsEvadeBecauseTargetNotReachable());

    std::map<ObjectGuid, uint32>& unreachable = context->GetValue<std::map<ObjectGuid, uint32>&>("unreachable targets")->Get();
    auto givenUp = unreachable.find(target->getObjectGuid());
    if (givenUp != unreachable.end())
    {
        if (WorldTimer::getMSTime() < givenUp->second &&
            (cannotBeReached || target->GetVictim() != playerToCheckAgainst || !playerToCheckAgainst->IsWithinLOSInMap(target, true)))
            return true;
        unreachable.erase(givenUp);
    }

    // A target the creature aura keep-out set aside (its own book, see
    // IsKeepoutSetAside).
    if (IsKeepoutSetAside(target, playerToCheckAgainst))
        return true;

    // The same for the kind of creature: after a give-up the bot leaves that kind alone for
    // a while, so a spot full of them is not given up on one creature at a time.
    if (target->IsCreature())
    {
        std::map<uint32, uint32>& unreachableKinds = context->GetValue<std::map<uint32, uint32>&>("unreachable entries")->Get();
        auto givenUpKind = unreachableKinds.find(target->GetEntry());
        if (givenUpKind != unreachableKinds.end())
        {
            if (WorldTimer::getMSTime() < givenUpKind->second &&
                (cannotBeReached || target->GetVictim() != playerToCheckAgainst || !playerToCheckAgainst->IsWithinLOSInMap(target, true)))
                return true;
            unreachableKinds.erase(givenUpKind);
        }
    }

    // A creature that stands in a capital city is no grind target. Gamon and his kind are
    // attackable on purpose and sit at the auction house, inside the level window of the
    // level-10 bots that come to sell or train - two of them died to him in one morning.
    // Unprompted only: a creature that attacks the bot is fought like any other.
    if (ai->GetState() == BotState::BOT_STATE_NON_COMBAT && target->GetTypeId() == TYPEID_UNIT)
    {
        uint32 const zone = target->GetZoneId();
        bool capital = zone == 1637 || zone == 1519 || zone == 1537 || zone == 1657 || zone == 1497 || zone == 1638;
        if (!capital)
            if (AreaEntry const* area = AreaEntry::GetById(zone))
                capital = (area->Flags & AREA_FLAG_CAPITAL) != 0;
        if (capital)
            return true;
        // Hostile-town guard: target stands among guards hostile to this bot's team
        // (static spawn data, 60 yd around the target). Random masterless bots only;
        // retaliation preserved (NON_COMBAT gate above): a guard that attacks the bot
        // is fought like any other. Own-team guards never trigger the helper.
        if (sPlayerbotAIConfig.avoidHostileTowns && !ai->HasRealPlayerMaster() &&
            WorldPosition(target).IsGuardedHostileTownFor(playerToCheckAgainst))
            return true;
    }

    // Ignore hard hostiles while not already fighting - but never a mob that
    // is already fighting this bot. The old shape refused a +6 attacker on a
    // long trip outright, which kept it out of "attackers", "has attackers"
    // and the grind pick alike: no combat state, no revenge order, the bot
    // walked on while being hit. Answering pulls nothing new; flee and
    // outnumbered still decide whether the fight is winnable.
    bool const alreadyFightingBot = target->GetVictim() == playerToCheckAgainst ||
        target->GetThreatManager().getThreat(playerToCheckAgainst) > 0.0f;
    if (ai::ShouldIgnoreHardHostile((int)target->GetLevel(), (int)playerToCheckAgainst->GetLevel(), alreadyFightingBot) && ai->GetState() == BotState::BOT_STATE_NON_COMBAT)
    {
        // When traveling a long distance.
        if (AI_VALUE(bool, "travel target traveling") && AI_VALUE2(float, "distance", "travel target") > sPlayerbotAIConfig.reactDistance)
            return true;

        //When moving to master far away.
        if ((ai->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT) ||
            ai->HasStrategy("wander", BotState::BOT_STATE_NON_COMBAT)) &&
            AI_VALUE2(bool, "trigger active", "out of react range"))
            return true;

        if (ai->GetMaster() && !ai->HasActivePlayerMaster())
        {
            Player* player = ai->GetMaster();

            //When master is traveling a long distance.
            if (PAI_VALUE(bool, "travel target traveling") && PAI_VALUE2(float, "distance", "travel target") > sPlayerbotAIConfig.reactDistance)
                return true;
        }
    }

    Player* enemyPlayer = dynamic_cast<Player*>(target);

    if (!enemyPlayer)
    {
        bool isDummy = false;
        uint32 entry = target->GetEntry();

#define TRAINING_DUMMY_NPC_ENTRY1 190013
#define TRAINING_DUMMY_NPC_ENTRY2 190014
#define TRAINING_DUMMY_NPC_ENTRY3 190015
#define THERAMORE_COMBAT_DUMMY 4952
#define NAXXRAMAS_COMBAT_DUMMY 16211
// This server's custom capital-city training dummies (custom_npc_training_dummy,
// sql/base/tw_world_creature_template.sql) - previously unrecognized here, which let
// bots latch onto them permanently since their 1,000,000 HP never dies.
#define HEROIC_TRAINING_DUMMY 50514
#define APPRENTICE_TRAINING_DUMMY 50515
#define EXPERT_TRAINING_DUMMY 50516

        switch (entry)
        {
        case TRAINING_DUMMY_NPC_ENTRY1:
        case TRAINING_DUMMY_NPC_ENTRY2:
        case TRAINING_DUMMY_NPC_ENTRY3:
        case THERAMORE_COMBAT_DUMMY:
        case NAXXRAMAS_COMBAT_DUMMY:
        case HEROIC_TRAINING_DUMMY:
        case APPRENTICE_TRAINING_DUMMY:
        case EXPERT_TRAINING_DUMMY:
            isDummy = true;
        }

        if (isDummy && ai->GetFixedBotNumber(BotTypeNumber::DUMMY_ATTACK_NUMBER, 10, 0.2f)) //90% of bots, cycle every 5 min.
            return true;
    }

    return false;
}

std::string AttackersValue::Format()
{
    std::ostringstream out;

    for (auto& target : value)
    {
        if(target != value.front())
        out << ",";

        WorldObject* wo = ai->GetWorldObject(target);

        if (wo)
            out << wo->GetName();
        else
            out << target;
    }

    return out.str();
}

std::list<ObjectGuid> AttackersTargetingMeValue::Calculate()
{
    std::list<ObjectGuid> result;

    const std::list<ObjectGuid>& attackers = AI_VALUE(std::list<ObjectGuid>, "attackers");
    for (const ObjectGuid& attackerGuid : attackers)
    {
        Unit* attacker = ai->GetUnit(attackerGuid);
        if (attacker && (attacker->GetVictim() == bot || attacker->GetVictim() == bot))
        {
            result.push_back(attackerGuid);
        }
    }

    return result;
}
