
#include "playerbot/playerbot.h"
#include "AttackersValue.h"
#include "PossibleTargetsValue.h"
#include "EnemyPlayerValue.h"

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

        if (rtiTarget && rtiTarget->IsInWorld() && rtiTarget->GetMapId() == bot->GetMapId())
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

            if (pAttackersValue->calculatePos.sqDistance2d(bot) > 100.0f)
                continue;

            // Make the value expire at the same time as the copied value.
            lastCheckTime = pAttackersValue->lastCheckTime;

            calculatePos = pAttackersValue->calculatePos;

            result = PAI_VALUE(std::list<ObjectGuid>, valueName);

            std::vector<std::string> specificTargetNames = { "current target","old target","attack target","pull target" };
            Unit* target;

            // Remove bot specific targets of the other bot.
            for (auto& targetName : specificTargetNames)
            {
                target = (targetName == "attack target") ? ai->GetUnit(PAI_VALUE(ObjectGuid, targetName)) : PAI_VALUE(Unit*, targetName);
                if (target)
                    result.remove(target->getObjectGuid());
            }

            // Append bot specific targets of this bot in natural priority order.
            for (auto& targetName : specificTargetNames)
            {
                target = (targetName == "attack target") ? ai->GetUnit(AI_VALUE(ObjectGuid, targetName)) : AI_VALUE(Unit*, targetName);
                if (target)
                    result.push_back(target->getObjectGuid());
            }

            // Validate these targets and enforce the invariant of distinct hostile units,
            // preserving original sequence (first-seen stable deduplication).
            std::list<ObjectGuid> distinctResult;
            std::set<ObjectGuid> seen;

            for (auto& guid : result)
            {
                if (!seen.insert(guid).second)
                    continue;

                target = ai->GetUnit(guid);
                if (IsValid(target, bot, bot))
                {
                    distinctResult.push_back(guid);
                    if (getOne)
                        break;
                }
            }

            return distinctResult;
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

bool AttackersValue::IsValid(Unit* target, Player* player, Player* owner, bool checkInCombat, bool validatePossibleTarget)
{
    Player* playerToCheckAgainst = owner != nullptr ? owner : player;

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
        // Don't consider enemy players if pvp strategy is not set
        if (PlayerbotAIStorage::Instance().GetAI(playerToCheckAgainst) && !PlayerbotAIStorage::Instance().GetAI(playerToCheckAgainst)->HasStrategy("pvp", BotState::BOT_STATE_COMBAT))
        {
            return false;
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
        if (checkInCombat && !isDuelOpponent && !InCombat(target, player, (player == owner)))
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
        if (checkInCombat && !InCombat(target, player, (player == owner)))
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

bool AttackersValue::IgnoreTarget(Unit* target, Player* playerToCheckAgainst)
{
    if (!PlayerbotAIStorage::Instance().GetAI(playerToCheckAgainst))
        return false;

    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(playerToCheckAgainst);
    AiObjectContext* context = ai->GetAiObjectContext();

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
    // walked on and died (27% of non-guard walk-past deaths are +6 or more,
    // Oct 2026 pool). Answering pulls nothing new; flee/outnumbered still
    // decide whether the fight is winnable.
    bool const alreadyFightingBot = target->GetVictim() == playerToCheckAgainst ||
        target->GetThreatManager().getThreat(playerToCheckAgainst) > 0.0f;
    if (!alreadyFightingBot && target->GetLevel() > (playerToCheckAgainst->GetLevel() + 5) && ai->GetState() == BotState::BOT_STATE_NON_COMBAT)
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
