
#include "playerbot/playerbot.h"
#include "playerbot/GroupMembers.h"
#include "playerbot/strategy/generic/PullStrategy.h"
#include "playerbot/strategy/values/AttackersValue.h"
#include "PullActions.h"
#include "ReachTargetActions.h"
#include "playerbot/strategy/values/PositionValue.h"
#include "../../runtime/BotManager.h"
#include "../../runtime/PlayerbotAIStorage.h"

using namespace ai;

namespace
{
// The core fires the first ranged auto-shot up to 500 ms after the cast
// (Unit::_UpdateAutoRepeatSpell's first-cast grace) plus the bow's swing.
// Past this the pull goes on regardless: a pull that cannot land must not hold
// the tank - and with it the party's join window - at the pull spot.
constexpr uint32 kPullShotWaitMs = 4000;

// True once the pulled target has actually come for us: the arrow's damage put
// the bot or a party member on its threat list, it picked one of us as its
// victim, or it is in combat at all.
bool PullTargetEngaged(Unit* target, Player* bot)
{
    if (!target || !bot)
        return false;
    if (target->GetVictim() == bot)
        return true;
    if (target->GetThreatManager().getThreat(bot) > 0.0f)
        return true;

    // A body pull lands the moment the bot's attack request is accepted: the
    // core puts the bot on the target's attacker list (Unit::Attack) before the
    // first swing has set a victim or threat, and the list is emptied when the
    // creature resets, so it cannot keep a pull that fell apart alive. Ranged
    // pulls reach the list only through the arrow's damage, never the cast.
    if (target->GetAttackers().count(bot) || bot->GetVictim() == target)
        return true;

    // The pull engages the party, not only the tank: a mob that went for the
    // healer who healed too early is just as engaged as one that came for the
    // tank, and the pull must hand it over the same way.
    if (Group* group = bot->GetGroup())
    {
        for (Player* member : LiveGroupMembers(group))
        {
            if (!member || member == bot)
                continue;
            if (target->GetVictim() == member)
                return true;
            if (target->GetThreatManager().getThreat(member) > 0.0f)
                return true;
        }
    }

    return target->IsInCombat();
}

// Re-stamp every held party bot's combat-start clock, optionally narrowing
// its wait window. Shared by the pull landing (once the cast lands) and the
// pull end (when a pullback's tank is back and takes the fight over).
void RestampPullParty(Player* tank, uint32 waitSeconds, bool narrowWindow)
{
    if (!tank)
        return;
    for (Player* member : LiveGroupMembers(tank->GetGroup()))
    {
        if (!member || member == tank || !TortoiseBots::BotManager::Instance().IsBot(member->GetObjectGuid()))
            continue;
        PlayerbotAI* memberAi = PlayerbotAIStorage::Instance().GetAI(member);
        if (!memberAi || !memberAi->GetAiObjectContext())
            continue;
        // Only the bots this pull holds: a "wait for attack" of the player's
        // own on a bot the pull did not park keeps its own delay and clock.
        if (!memberAi->HasPullWindow())
            continue;
        if (narrowWindow)
            memberAi->GetAiObjectContext()->GetValue<uint8>("wait for attack time")->Set(static_cast<uint8>(waitSeconds));
        memberAi->GetAiObjectContext()->GetValue<time_t>("combat start time")->Set(time(0));
        // The window's own clock ("pull hold expired").
        memberAi->RestampPullWindow();
        memberAi->TracePullHold(narrowWindow ? "restamp-return" : "restamp-engagement");
    }
}
} // namespace

// Chat pullback's party hold: mirrors PausePartyDpsForPull
// (commands/BotCommandContext.cpp), which the chat path cannot reach (it
// lives in the command layer, not the AI). Same anchor (the requester's
// position, falling back to the tank), same stay + "wait for attack" join
// window with its own clock - except healers are held too: they get the
// window but no stay, so they stand free to heal while their damage rows
// stay vetoed (HealerShouldAttackTrigger / HealerWandAllowed, gated on
// HasPullWindow below). Member pets go passive for the window via their own
// AI (ParkPetForPullHold, restored by their own EndPullWindow); the tank's
// PullStartAction already pins its own pet and PullEndAction restores it.
// The tank is skipped here (its own pull drives it), and other tanks are
// left alone so an off-tank can still pick up loose mobs.
void ai::PausePartyPullHold(Player* tank, uint32 joinDelaySeconds)
{
    if (!tank || !tank->GetGroup())
        return;
    PlayerbotAI* tankAi = PlayerbotAIStorage::Instance().GetAI(tank);
    Player* requester = (tankAi && tankAi->GetMaster()) ? tankAi->GetMaster() : tank;
    WorldPosition anchor((requester && requester->IsInWorld() &&
        requester->GetMapId() == tank->GetMapId()) ? requester : tank);
    uint32 windowSeconds = std::min<uint32>(255,
        sPlayerbotAIConfig.pullBackMaxReturnTime + joinDelaySeconds);
    for (Player* member : LiveGroupMembers(tank->GetGroup()))
    {
        if (!member || member == tank)
            continue;
        PlayerbotAI* memberAi = PlayerbotAIStorage::Instance().GetAI(member);
        if (!memberAi || !memberAi->GetAiObjectContext())
            continue;
        if (PlayerbotAI::IsTank(member, true))
            continue;
        bool const healer = PlayerbotAI::IsHeal(member, true);
        if (!healer)
        {
            memberAi->SetMovementStrategy("stay");
            ai::PositionMap& posMap = memberAi->GetAiObjectContext()->GetValue<ai::PositionMap&>("position")->Get();
            ai::PositionEntry stayPos = posMap["stay"];
            stayPos.Set(anchor.getX(), anchor.getY(), anchor.getZ(), anchor.GetMapId());
            posMap["stay"] = stayPos;
            ai::PositionEntry holdPos = posMap["pull hold"];
            holdPos.Set(anchor.getX(), anchor.getY(), anchor.getZ(), anchor.GetMapId());
            posMap["pull hold"] = holdPos;
        }
        memberAi->StartPullWindow(static_cast<uint8>(windowSeconds));
        memberAi->GetAiObjectContext()->GetValue<time_t>("combat start time")->Set(time(0));
        // Member-owned stance (PlayerbotAI::ParkPetForPullHold): saved on the
        // member's own AI and restored by its own EndPullWindow, so a member
        // that leaves the group, logs out or dies mid-hold still gets its
        // pet back. Re-hold while passive keeps the original stance.
        memberAi->ParkPetForPullHold();
    }
}


Unit* PullNearestTargetAction::FindPullTarget(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    Unit* best = nullptr;

    // Same ceiling PullRequestAction::Execute enforces, so the trigger cannot
    // offer a target the action will then refuse.
    float bestDistance = sPlayerbotAIConfig.reactDistance * 3;

    for (const ObjectGuid& guid : ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("possible targets")->Get())
    {
        Unit* unit = ai->GetUnit(guid);
        if (!unit || !unit->IsAlive())
            continue;

        // "possible targets" carries neutrals too, and a neutral is not a pull.
        if (!bot->IsHostileTo(unit))
            continue;

        // Somebody is already on it - that is no longer a pull, it is a join.
        if (unit->IsInCombat())
            continue;

        if (!AttackersValue::IsValid(unit, bot, nullptr, false))
            continue;

        const float distance = unit->GetDistance(bot);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = unit;
        }
    }

    return best;
}

Unit* PullNearestTargetAction::GetTarget(Event& event)
{
    return FindPullTarget(ai);
}

bool PullRequestAction::Execute(Event& event)
{
    PullStrategy* strategy = PullStrategy::Get(ai);
    if (!strategy)
    {
        return false;
    }

    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();

    Unit* target = GetTarget(event);
    if (!target)
    {
        ai->TellPlayerNoFacing(requester, "You have no target");
        return false;
    }

    const float maxPullDistance = sPlayerbotAIConfig.reactDistance * 3;
    const float distanceToPullTarget = target->GetDistance(ai->GetBot());
    if (distanceToPullTarget > maxPullDistance)
    {
        ai->TellPlayerNoFacing(requester, "The target is too far away");
        return false;
    }

    // Only an explicit player pull retries the selected target's give-up cache.
    // Automatic pulls retain avoidance, and normal target/path checks still run.
    if (requester && requester->GetSession() && requester->GetSession()->HasNetworkTransport() &&
        (event.GetSource() == "pull" || event.GetSource() == "pullback"))
    {
        auto* unreachable = context->GetValue<std::map<ObjectGuid, uint32>&>("unreachable targets");
        auto* entries = context->GetValue<std::map<uint32, uint32>&>("unreachable entries");
        if (!unreachable || !entries)
        {
            ai->TellPlayerNoFacing(requester, "Can't pull: unreachable-target cache is unavailable");
            sLog.outError("TortoiseBots: pull retry cache lookup failed for %s", bot->GetName());
            return false;
        }
        unreachable->Get().erase(target->GetObjectGuid());
        if (target->IsCreature())
            entries->Get().erase(target->GetEntry());
        for (char const* name : { "reach pull", "reach melee", "reach spell" })
            if (auto* reach = dynamic_cast<ReachTargetAction*>(context->GetAction(name)))
                reach->ResetPullRetryProgress(target->GetObjectGuid());
    }

    char const* rejectionReason = "unspecified target validation failure";
    if (!AttackersValue::IsValid(target, bot, nullptr, false, true, &rejectionReason))
    {
        ai->TellPlayerNoFacing(requester, std::string("Can't pull: ") + rejectionReason);
        sLog.outBasic("TortoiseBots: PULL_REJECT bot=%s guid=%u target=%s entry=%u reason=%s distance=%.1f",
            bot->GetName(), bot->GetGUIDLow(), target->GetName(), target->GetEntry(), rejectionReason, distanceToPullTarget);
        return false;
    }

    if (!strategy->CanDoPullAction(target))
    {
        std::ostringstream out; out << "Can't perform pull action '" << strategy->GetPullActionName() << "'";
        ai->TellPlayerNoFacing(requester, out.str());
        return false;
    }

    //Set position to return to after pulling.
    PositionMap& posMap = AI_VALUE(PositionMap&, "position");
    PositionEntry pullPosition = posMap["pull"];

    if (requester && requester->IsInWorld() && requester->GetMapId() == bot->GetMapId())
    {
        pullPosition.Set(requester->getPositionX(), requester->getPositionY(), requester->getPositionZ(), requester->GetMapId());
    }
    else
    {
        pullPosition.Set(bot->getPositionX(), bot->getPositionY(), bot->getPositionZ(), bot->GetMapId());
    }
    posMap["pull"] = pullPosition;

    strategy->RequestPull(target);

    // Keep the pulled mob as the explicit attack target for the whole approach:
    // it is not in combat until the shot lands, and without this the combat
    // engine drops it every few ticks and re-paths the walk to the mob.
    AiObjectContext* pullContext = ai->GetAiObjectContext();
    pullContext->GetValue<ObjectGuid>("attack target")->Set(target->getObjectGuid());
    pullContext->GetValue<bool>("invalid target", "current target")->Reset();

    // Force change combat state to have a faster reaction time
    ai->OnCombatStarted();

    return true;
}

// "pullback [seconds]" from party chat: the event param is the "[seconds]"
// tail ("" for a bare "pullback"). Parsed like the .bot option: 0-60, or
// -1 when the tail is not a bare number (fall back to the configured
// pullBackDpsJoinDelay instead of refusing a chat order).
static int ParsePullbackJoinDelay(std::string const& param)
{
    std::string tail = param;
    size_t first = tail.find_first_not_of(" \t");
    if (first == std::string::npos)
        return -1;
    size_t last = tail.find_last_not_of(" \t");
    tail = tail.substr(first, last - first + 1);
    if (tail.empty() || tail.find_first_of(" \t") != std::string::npos)
        return -1;
    for (char character : tail)
    {
        if (character < '0' || character > '9')
            return -1;
    }
    long seconds = std::atol(tail.c_str());
    return (seconds >= 0 && seconds <= 60) ? static_cast<int>(seconds) : -1;
}

bool PullbackMyTargetAction::Execute(Event& event)
{
    // Chat pullback: only the tank may start the shared pull path, like the
    // chat "pull" before it. Every other bot stays held by the trigger (its
    // "pullback my target" resolves to no action there). A plain
    // "pull my target" here would walk the whole party at the mob.
    if (!ai->IsTank(bot, true))
        return false;

    int seconds = ParsePullbackJoinDelay(event.GetParam());
    uint32 joinDelay = seconds >= 0 ? static_cast<uint32>(seconds)
        : sPlayerbotAIConfig.pullBackDpsJoinDelay;

    // The command path configures the pull mode before the request action
    // runs (BotCommands.cpp: ConfigurePullMode, then "pull my target"):
    // RequestPull snapshots the return-leg intent and DecideOpening reads
    // the command state, so the order must exist first. Do the same here:
    // flip "pull back" on with the tank's real prior state, record the
    // per-request mode, and roll everything back when the request refuses
    // (validation failures below place no hold and touch no party bot).
    bool hadPullBack = ai->HasStrategy("pull back", BotState::BOT_STATE_COMBAT) ||
        ai->HasStrategy("pull back", BotState::BOT_STATE_NON_COMBAT);
    PullStrategy::CommandState commandBefore;
    bool hasCommandBefore = false;
    if (PullStrategy* probe = PullStrategy::Get(ai))
    {
        commandBefore = probe->SaveCommand();
        hasCommandBefore = true;
    }
    ai->ChangeStrategy("+pull back", BotState::BOT_STATE_ALL);
    if (PullStrategy* strategy = PullStrategy::Get(ai))
        strategy->BeginCommand(true, hadPullBack, joinDelay, false);
    if (!PullRequestAction::Execute(event))
    {
        if (PullStrategy* strategy = PullStrategy::Get(ai))
        {
            if (hasCommandBefore)
                strategy->RestoreCommand(commandBefore);
            else
                strategy->OnPullEnded();
        }
        if (!hadPullBack)
            ai->ChangeStrategy("-pull back", BotState::BOT_STATE_ALL);
        return false;
    }
    PausePartyPullHold(bot, joinDelay);
    return true;
}

Unit* PullMyTargetAction::GetTarget(Event& event)
{
    // Prefer the GUID snapshotted by the command (event object): the live
    // selection can change between command validation and this tick.
    ObjectGuid snapshot = event.getObject();
    if (!snapshot.IsEmpty())
    {
        if (Unit* snapshotted = ai->GetUnit(snapshot))
            return snapshotted;
    }

    Unit* target = nullptr;

    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    if (event.GetSource() == "attack anything")
    {
        ObjectGuid guid = event.getObject();
        target = ai->GetCreature(guid);
    }
    else if (requester)
    {
        target = ai->GetUnit(requester->GetSelectionGuid());
    }

    return target;
}

Unit* PullbackMyTargetAction::GetTarget(Event& event)
{
    // Chat path only (the .bot commands snapshot the target GUID into the
    // event object and use "pull my target" directly): same target, from the
    // requester's live selection.
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    return requester ? ai->GetUnit(requester->GetSelectionGuid()) : nullptr;
}

Unit* PullRTITargetAction::GetTarget(Event& event)
{
    return AI_VALUE(Unit*, "rti target");
}

bool PullStartAction::Execute(Event& event)
{
    bool result = false;
    PullStrategy* strategy = PullStrategy::Get(ai);
    if (strategy)
    {
        Unit* target = strategy->GetTarget();
        if (target)
        {
            if (!strategy->PrepareBossPull(event, false))
                return true; // preparation made progress; do not back off the pull
            if (strategy->GetPreActionName().empty())
                result = true;
            else
            {
                result = ai->DoSpecificAction(strategy->GetPreActionName(), event, true);
                if(result)
                    SetDuration(ai->GetAIInternalUpdateDelay());
            }

            // Set the pet on passive mode during the pull. Save once: a
            // re-start while the pet is already passive must not overwrite
            // the pre-pull stance with REACT_PASSIVE (same guard as the
            // party hold above).
            Pet* pet = bot->GetPet();
            if (pet)
            {
                UnitAI* creatureAI = ((Creature*)pet)->AI();
                if (creatureAI)
                {
                    if (pet->GetReactState() != REACT_PASSIVE)
                        strategy->SetPetReactState(pet->GetReactState());
                    pet->SetReactState(REACT_PASSIVE);
                }
            }

            strategy->OnPullStarted();
        }
    }

    return result;
}


PullAction::PullAction(PlayerbotAI* ai, std::string name) : CastSpellAction(ai, name)
{
    InitPullAction();
}

bool PullAction::Execute(Event& event)
{
    InitPullAction();

    PullStrategy* strategy = PullStrategy::Get(ai);
    if (strategy)
    {
        Unit* target = strategy->GetTarget();
        if (target)
        {
            // Check if we are on pull range. The "pull action" node also carries
            // "reach pull" as a prerequisite, so the queued path moves first;
            // this explicit branch covers the direct DoSpecificAction path used
            // by the .bot command, which bypasses prerequisites.
            const float distanceToTarget = target->GetDistance(bot);
            if (distanceToTarget > strategy->GetRange())
            {
                // The approach is still running: sample headway so a
                // progressing cross-room walk (issue #910) keeps its target
                // past the flat pull window instead of timing out mid-reach.
                strategy->NotePullApproach(distanceToTarget, sServerFacade.isMoving(bot));
                // Retry the reach pull action
                strategy->RequestPull(target, false);
                ai::Event reachEvent(event.GetSource(), "", event.GetOwner());
                ai->DoSpecificAction("reach pull", reachEvent, true);
                return false;
            }

            if (sServerFacade.isMoving(bot))
            {
                // Force stop
                ai->StopMoving();
                strategy->RequestPull(target, false);
                return false;
            }

            if (!strategy->PrepareBossPull(event, true))
                return true; // wait on the normal clock, not failure backoff

            // Preparation (stance, wall) ran after the DPS hold began. Restart
            // the held party's join window once, just before the shot.
            if (strategy->ConsumeBossPrepRestamp())
                RestampPullParty(bot, 0, false);

            std::string actionName = strategy->GetPullActionName();

            // Execute the pull action. The AI's "current target" and the bot's
            // selection GUID must name the same unit: InvalidTargetValue reads
            // a mismatch as "invalid target", and the combat engine then wipes
            // the target and calls AttackStop() (the tank stood at the anchor
            // doing nothing while the mob beat on it). CastSpell restores the
            // pre-cast selection, so the selection is re-asserted after the
            // spell branch too.
            SET_AI_VALUE(Unit*, "current target", GetTarget());

            // The pulled mob is not in combat until the shot lands, and
            // InvalidTargetValue only lets a not-yet-engaged target through
            // when it is the bot's explicit "attack target". Without it the
            // combat engine drops the target every tick ("select new target"),
            // which cancels the shot before the arrow flies.
            AiObjectContext* pullContext = ai->GetAiObjectContext();
            pullContext->GetValue<ObjectGuid>("attack target")->Set(target->getObjectGuid());
            pullContext->GetValue<bool>("invalid target", "current target")->Reset();

            if (actionName == "reach pull")
            {
                // Body pull: the tank is in melee reach of the target, so the
                // pull has landed. Do not fail on the attack request itself -
                // a mounted tank cannot attack (Unit::Attack) and a repeated
                // request on the same victim returns false; failing here used
                // to leave the tank muted at the mob until the pull timeout.
                if (bot->IsMounted())
                    ai->Unmount();
                bot->Attack(target, true);
                bot->SetSelectionGuid(target->getObjectGuid());
                strategy->OnPullActionCompleted();
                return true;
            }
            else if (strategy->HasPullActionCast())
            {
                // The cast went out: wait for the arrow itself. In this core
                // "Shoot Bow" only opens the ranged auto-repeat - the arrow
                // flies a swing later (Unit::_UpdateAutoRepeatSpell), and the
                // next cast or any movement cancels it. Completing the pull on
                // the cast made the tank run back during the windup, so the
                // arrow never landed and the mob never aggroed.
                // Re-arm the pull request (no time reset): that is what keeps
                // this poll queued every tick while the tank stands still.
                strategy->RequestPull(target, false);

                if (PullTargetEngaged(target, bot) || strategy->GetPullActionCastAgeMs() >= kPullShotWaitMs)
                {
                    strategy->OnPullActionCompleted();
                    // Anchor the DPS join delay to the landing, not to the
                    // command: re-stamp every held party bot's combat-start
                    // clock so an ordinary pull holds for the full delay after
                    // the shot lands. Pullbacks keep the wide return-covering
                    // window here; the pull end narrows it to the join delay.
                    RestampPullParty(bot, 0, false);
                }
                return true;
            }
            else if (ai->DoSpecificAction(actionName, event, true))
            {
                bot->SetSelectionGuid(target->getObjectGuid());
                strategy->NotePullActionCast();
                return true;
            }
            else
                return false;
        }
    }

    return false;
}

bool PullAction::isPossible()
{
    InitPullAction();

    PullStrategy* strategy = PullStrategy::Get(ai);
    if (strategy)
    {
        // Waiting for the arrow: the spell cannot be cast again (that would
        // restart the ranged swing) and there is nothing left to check - the
        // action only has to observe the shot.
        if (strategy->HasPullActionCast())
            return true;

        if (strategy->GetPullActionName() == "reach pull")
            return true;

        std::string spellName = strategy->GetSpellName();
        Unit* target = strategy->GetTarget();
        if (!spellName.empty() && target)
        {
            if (!ai->CanCastSpell(spellName, target, true, nullptr, true))
            {
                return false;
            }
        }
    }

    return true;
}

void PullAction::InitPullAction()
{
    // Get the pull action spell name from the strategy
    PullStrategy* strategy = PullStrategy::Get(ai);
    if (strategy)
    {
        std::string spellName = strategy->GetSpellName();
        if (!spellName.empty())
        {
            SetSpellName(spellName);

            float spellRange;
            if (ai->GetSpellRange(spellName, &spellRange))
            {
                range = spellRange;
            }
        }
    }
}

bool PullAction::isUseful()
{
    // Waiting for the arrow: the spell's own usefulness no longer applies, and
    // the action has to stay eligible or the wait would never be observed.
    PullStrategy* strategy = PullStrategy::Get(ai);
    if (strategy && strategy->HasPullActionCast())
        return true;

    return CastSpellAction::isUseful();
}

bool EndPullAction::isUseful()
{
    PullStrategy* strategy = PullStrategy::Get(ai);
    return strategy && strategy->HasPullStarted();
}

bool PullEndAction::Execute(Event& event)
{
    PullStrategy* strategy = PullStrategy::Get(ai);
    if (strategy)
    {
        // Restore the tank's own pet stance. Member pets restore themselves:
        // each held member parked its stance on its own AI and its own
        // EndPullWindow puts it back when its window ends - including a
        // member that left the group, logged out or died mid-hold, which
        // this loop (current members only) can never reach.
        ai->RestorePullHoldPet();
        {
            Pet* pet = bot->GetPet();
            if (pet)
            {
                UnitAI* creatureAI = ((Creature*)pet)->AI();
                if (creatureAI)
                {
                    if (pet->GetReactState() == REACT_PASSIVE)
                        pet->SetReactState(strategy->GetPetReactState());
                }
            }
        }

        // Per-command return mode: restore the tank's default before erasing
        // anything, so an ordinary pull re-arms the tank kit and a pullback
        // never leaks into the next command.
        bool wasCommand = strategy->IsCommandActive();
        bool wasPullback = strategy->IsCommandPullback();
        bool hadPullBack = strategy->HadPullBack();
        if (wasCommand && (wasPullback != hadPullBack))
        {
            ai->ChangeStrategy((hadPullBack ? "+" : "-") + std::string("pull back"),
                BotState::BOT_STATE_ALL);
        }

        // A pull cannot outlive its tank (the dead-engine "pull end" trigger
        // has already released the held party): finish the bookkeeping, but do
        // not park a corpse on the anchor or re-stamp the party's join window.
        const bool alive = bot->IsAlive();

        // Did the pull land at all? Only a mob that came for the party can be
        // handed to the tank's rotation; a pull that never landed (blocked
        // shot, target in evade, approach timeout) leaves it untouched.
        Unit* pullTarget = alive ? strategy->GetTarget() : nullptr;
        const bool landed = pullTarget && PullTargetEngaged(pullTarget, bot) &&
            pullTarget->IsAlive() && pullTarget->IsInWorld() &&
            pullTarget->GetMapId() == bot->GetMapId() && bot->IsValidAttackTarget(pullTarget);

        // The tank is back and takes the fight over here: this is the moment a
        // pullback's join window counts from. Narrow every held party bot's
        // wait window to the join delay and restart its clock, so the DPS
        // arrive joinDelay after the tank is back instead of after the wide
        // return-covering window placed at the command.
        if (landed && wasCommand && wasPullback)
            RestampPullParty(bot, strategy->GetCommandJoinDelay(), true);

        // Remove the saved pull position
        AiObjectContext* context = ai->GetAiObjectContext();

        if (landed)
        {
            // Hand the pulled mob over to the tank's own rotation. OnPullEnded
            // below drops the pull target, and a target that only lived there
            // left the tank idle: InvalidTargetValue reads an AI "current
            // target" that does not match the bot selection GUID as "invalid
            // target", so the combat engine wiped it and called AttackStop()
            // instead of fighting. Keep the mob as the live target (selection,
            // current target, attack target) and the tank's normal rotation
            // takes it from here - for a pullback it does so at the anchor,
            // where the mob comes to it.
            context->GetValue<ObjectGuid>("attack target")->Set(pullTarget->getObjectGuid());
            SET_AI_VALUE(Unit*, "current target", pullTarget);
            bot->SetSelectionGuid(pullTarget->getObjectGuid());
        }
        else
        {
            // The pull failed: nothing was engaged by the request itself. When
            // the tank is not fighting, that means there is no fight to hand
            // over: drop every trace of the target the request stamped on the
            // bot. A live, hostile "attack target" is what CombatEndTrigger
            // waits on, so leaving it behind kept the tank inside the combat
            // engine - no fight, no follow - for good, and (in a dungeon) also
            // blocked every later "should pull". Clearing it lets the combat
            // state end and the tank go back to following the party.
            //
            // When the tank is already in a live fight (intercepted by the rest
            // of the pack while the shot was in the air), the wipe would end a
            // fight that is happening: drop only what still names the unengaged
            // pull target - the combat engine re-picks an attacker on its own
            // tick (mod-playerbots' pull end clears just the current target).
            // The held party releases either way: the fight is now, not after
            // a join window.
            const bool fighting = !bot->GetAttackers().empty();
            if (fighting)
            {
                if (pullTarget)
                {
                    if (AI_VALUE(Unit*, "current target") == pullTarget)
                        SET_AI_VALUE(Unit*, "current target", nullptr);
                    if (AI_VALUE(ObjectGuid, "attack target") == pullTarget->getObjectGuid())
                        context->GetValue<ObjectGuid>("attack target")->Reset();
                    if (bot->GetSelectionGuid() == pullTarget->getObjectGuid())
                        bot->SetSelectionGuid(ObjectGuid());
                }
            }
            else
            {
                context->GetValue<ObjectGuid>("attack target")->Reset();
                SET_AI_VALUE(Unit*, "current target", nullptr);
                bot->SetSelectionGuid(ObjectGuid());
            }

            // Free the party now: the wait window they were given exists to
            // cover the pull that is coming, and it is not. They release
            // themselves on their next tick ("pull hold expired").
            ReleaseHeldPartyNow(bot);
        }

        PositionMap& posMap = AI_VALUE(PositionMap&, "position");
        PositionEntry stayPosition = posMap["pull"];
        if (stayPosition.isSet())
        {
            // After a pullback that landed, the tank holds the anchor for the
            // join window: drop a stay at the anchor position so the tank
            // fights the incoming mob in the corner instead of drifting back
            // out. The same anchor goes into "pull hold" as the tank's own
            // copy, so the hold can be released once that fight is over -
            // without it the tank would stand there for good. A pull that never
            // landed parks nobody: there is no fight at the anchor.
            if (landed && wasCommand && wasPullback)
            {
                PositionEntry tankStay = posMap["stay"];
                tankStay.Set(stayPosition.x, stayPosition.y, stayPosition.z, stayPosition.mapId);
                ai->SetMovementStrategy("stay");
                posMap["stay"] = tankStay;
                posMap["pull hold"] = tankStay;
            }
            posMap.erase("pull");
        }
        // The held party releases itself per bot once its own join window
        // elapses ("pull hold expired" trigger): the tank must not drop the
        // wait window here, or the join delay would be zero.

        // Back to a normal movement mode. The tactical request that started the
        // pull dropped the tank's previous hold (RelaxTacticalMovement), and a
        // bot that was parked before it would otherwise be left with no
        // movement strategy at all - standing where the pull ended until the
        // next order. The anchor hold (a landed pullback) is a movement
        // strategy of its own, so this leaves it alone.
        if (!ai->HasActiveMovementStrategy())
            ai->EnsureDefaultMovementStrategy();

        strategy->OnPullEnded();
        return true;
    }

    return false;
}

bool ReleasePullHoldAction::Execute(Event& event)
{
    (void)event;
    ai->TracePullHold("expiry-action");
    // Ending the pull's window is the release: dropping its own "wait for
    // attack" frees the anchor hold (marker + the stay it owns) with it
    // (PlayerbotAI::ChangeStrategy), and a "wait for attack" the player set
    // stays while only the anchor goes.
    ai->EndPullWindow();
    return true;
}
