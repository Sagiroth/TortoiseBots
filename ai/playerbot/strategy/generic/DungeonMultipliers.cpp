#include "playerbot/playerbot.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/policy/raid/McGarrShazzrahPolicy.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "DungeonMultipliers.h"
#include "playerbot/strategy/actions/MoltenCoreDungeonActions.h"
#include "playerbot/strategy/actions/GeddonBombActions.h"
#include "playerbot/strategy/actions/DungeonActions.h"
#include "playerbot/strategy/actions/ReachTargetActions.h"
#include "playerbot/strategy/actions/ChooseTargetActions.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/GroupMembers.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/policy/raid/RazorgorePolicy.h"
#include "playerbot/strategy/actions/ChooseTargetActions.h"
#include "playerbot/strategy/actions/GenericActions.h"
#include "playerbot/strategy/actions/GenericSpellActions.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/GroupMembers.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/policy/raid/GolemaggPolicy.h"
#include "playerbot/strategy/values/PartyMemberToHeal.h"
#include "playerbot/policy/raid/GeddonInfernoPolicy.h"

using namespace ai;

float PreventMoveAwayFromCreatureOnReachToCastMultiplier::GetValue(Action* action)
{
    MoveAwayFromCreature* moveAwayAction = dynamic_cast<MoveAwayFromCreature*>(action);
    if (moveAwayAction)
    {
        const Action* lastExecutedAction = ai->GetLastExecutedAction(BotState::BOT_STATE_COMBAT);
        if (lastExecutedAction)
        {
            const ReachTargetAction* reachAction = dynamic_cast<const ReachTargetAction*>(lastExecutedAction);
            if (reachAction && !reachAction->GetSpellName().empty())
            {
                return 0.0f;
            }
        }
    }

    return 1.0f;
}

float GarrAoeOffMultiplier::GetValue(Action* action)
{
    if (!action)
        return 1.0f;
    // Cheap exit: no Garr fight, no suppression.
    if (!ai->HasStrategy("garr", BotState::BOT_STATE_COMBAT))
        return 1.0f;
    AiObjectContext* context = ai->GetAiObjectContext();
    const std::list<ObjectGuid>& attackers = AI_VALUE(std::list<ObjectGuid>, "attackers");
    bool garrAlive = false;
    for (const ObjectGuid& attackerGuid : attackers)
    {
        Unit* attacker = ai->GetUnit(attackerGuid);
        if (attacker && attacker->GetEntry() == kGarrEntry)
        {
            garrAlive = true;
            break;
        }
    }
    if (!garrAlive)
        return 1.0f;
    // DPS bots only: tanks and healers keep their (single-target) work.
    // Our role API exposes tank/heal/ranged; DPS = neither tank nor heal.
    Player* bot = ai->GetBot();
    bool botIsDps = !ai->IsTank(bot) && !ai->IsHeal(bot);
    // Name-matched AoE set (donor's explicit list): threat flags do not
    // mark our real AoE (Whirlwind etc. return SINGLE/NONE) and wrongly
    // flag heals plus single-target dots as AOE, so type/threat matching
    // is both under- and over-inclusive here.
    bool actionIsAoe = IsGarrSuppressedAoeAction(action->getName());
    if (ShouldSuppressGarrAoe(garrAlive, botIsDps, actionIsAoe))
        return 0.0f;
    return 1.0f;
}

float RazorgoreOffTankMultiplier::GetValue(Action* action)
{
    if (!action)
        return 1.0f;
    // Only TankAssistAction is ever vetoed: skip every scan for the ~99%
    // of other actions (donor's egg-phase veto is TankAssistAction-only;
    // its post-egg TankFaceAction veto is deliberately not ported — no
    // TankFaceAction exists in this codebase).
    if (dynamic_cast<TankAssistAction*>(action) == nullptr)
        return 1.0f;
    if (!ai->HasStrategy("razorgore", BotState::BOT_STATE_COMBAT))
        return 1.0f;
    if (!ai->IsTank(bot))
        return 1.0f;
    AiObjectContext* context = ai->GetAiObjectContext();
    // Explicit player orders win: never veto a tank-assist carrying an
    // explicit target (TankTargetValue returns explicit first).
    if (!AI_VALUE(ObjectGuid, "explicit attack target").IsEmpty())
        return 1.0f;
    const std::list<ObjectGuid>& attackers = AI_VALUE(std::list<ObjectGuid>, "attackers");
    Unit* boss = nullptr;
    for (const ObjectGuid& attackerGuid : attackers)
    {
        Unit* attacker = ai->GetUnit(attackerGuid);
        if (attacker && attacker->GetEntry() == kRazorgoreEntry && sServerFacade.IsAlive(attacker))
        {
            boss = attacker;
            break;
        }
    }
    if (!boss)
        return 1.0f;
    // Donor's victim guard (BWLMultipliers.cpp:32): the off-tank must still
    // ACQUIRE the boss — veto only once it holds something.
    if (bot->GetVictim() == nullptr)
        return 1.0f;
    // Real egg check (donor AreRazorgoreEggsAlive): cached "nearest game
    // objects" value, entry 177807. Veto lifts when the eggs die.
    bool eggsAlive = false;
    const std::list<ObjectGuid> nearestGos = AI_VALUE(std::list<ObjectGuid>, "nearest game objects");
    for (const ObjectGuid& goGuid : nearestGos)
    {
        GameObject* go = ai->GetGameObject(goGuid);
        if (go && go->GetEntry() == kBlackDragonEggEntry)
        {
            eggsAlive = true;
            break;
        }
    }
    // Off-tank = the first assist tank in the shared tank order (raid Main
    // Tank flag, else tank Main Assist, else living same-map tanks by slot:
    // GetRaidTankRoles), the lone tank when there is only one; the main
    // tank keeps the adds. While eggs live the off-tank holds the boss:
    // veto tank-assist retargets so adds don't pull it off.
    bool botIsOffTank = true;
    if (bot->GetGroup())
    {
        const RaidTankRoles tankRoles = GetRaidTankRoles(ai);
        botIsOffTank = tankRoles.botIndex >= 0 &&
            static_cast<std::size_t>(tankRoles.botIndex) == RazorgoreHolderTankIndex(tankRoles.tanks.size());
    }
    if (ShouldHoldRazorgore(eggsAlive, botIsOffTank))
        return 0.0f;
    return 1.0f;
}

float GolemaggFightMultiplier::GetValue(Action* action)
{
    if (!action)
        return 1.0f;
    if (!ai->HasStrategy("golemagg", BotState::BOT_STATE_COMBAT))
        return 1.0f;
    AiObjectContext* context = ai->GetAiObjectContext();
    const std::list<ObjectGuid> attackers = AI_VALUE(std::list<ObjectGuid>, "attackers");
    Unit* boss = nullptr;
    for (const ObjectGuid& attackerGuid : attackers)
    {
        Unit* attacker = ai->GetUnit(attackerGuid);
        if (attacker && attacker->GetEntry() == kGolemaggEntry)
        {
            boss = attacker;
            break;
        }
    }
    if (!boss)
        return 1.0f;
    bool burnPhase = boss->GetHealthPercent() <= kGolemaggBurnPct;
    // Tank jobs in the group: same order as GolemaggTankHoldAction (raid
    // Main Tank flag, else tank Main Assist, else slot order), living
    // same-map members only. Only tanks read them, so only tanks pay.
    RaidTankRoles tankRoles;
    if (ai->IsTank(bot))
        tankRoles = GetRaidTankRoles(ai);
    const unsigned livingTanks = static_cast<unsigned>(tankRoles.tanks.size());
    // Single tank picks up everything: role-hold actions would only fight
    // the normal target selection. Type check: MoveToAction hardcodes its
    // name to "name" (MovementActions.h), so a getName() match never fires.
    if (IsSingleLivingTank(livingTanks) && ai->IsTank(bot))
    {
        if (dynamic_cast<GolemaggTankHoldAction*>(action))
            return 0.0f;
    }
    // Assist tanks (every tank but the main tank) never follow tank-assist
    // retargets: they stay glued to their rager.
    if (ai->IsTank(bot) && livingTanks > 1 &&
        !ai->HasStrategy("golemagg boss tank", BotState::BOT_STATE_COMBAT) &&
        !ai->HasStrategy("golemagg add tank", BotState::BOT_STATE_COMBAT))
    {
        if (!tankRoles.BotIsMainTank() && dynamic_cast<TankAssistAction*>(action))
            return 0.0f;
    }
    // DPS AoE stays off for the whole fight (rager Trust + splash).
    // Name-matched set (same list as the Garr veto): threat flags do not
    // mark our real AoE and wrongly flag heals + single-target dots.
    if (!ai->IsTank(bot) && !ai->IsHeal(bot))
    {
        if (IsGolemaggSuppressedAoeAction(action->getName()) && ShouldExcludeRager(true))
            return 0.0f;
    }
    if (burnPhase)
        return 1.0f;
    // Ranged never melee-fallbacks onto the boss (splash stacks).
    if (ai->IsRanged(bot) && dynamic_cast<MeleeAction*>(action))
        return 0.0f;
    // Backed-off non-tanks stay out at 20+ stacks (donor
    // MCMultipliers.cpp:136 shape: whole-stack-expiry re-engage has no
    // core evidence — stacking auras expire whole, so 20→19 decay never
    // happens tick-by-tick anyway).
    Aura* splash = ai->GetAura(kMagmaSplashSpellId, bot);
    int splashStacks = splash ? (int)splash->GetStackAmount() : 0;
    bool backedOff = ShouldBackOffSplash(ai->IsTank(bot), splashStacks,
        (float)boss->GetHealthPercent());
    // Boss-only engages: adds and totems stay attackable. Reach actions
    // ("reach melee", "reach spell") count — they are how the bot walks
    // back into splash range.
    Unit* currentTarget = AI_VALUE(Unit*, "current target");
    bool targetsBoss = currentTarget && currentTarget == boss;
    bool engagesBoss = targetsBoss &&
        (dynamic_cast<AttackAction*>(action) != nullptr ||
         dynamic_cast<MeleeAction*>(action) != nullptr ||
         dynamic_cast<ReachTargetAction*>(action) != nullptr ||
         dynamic_cast<CastReachTargetSpellAction*>(action) != nullptr);
    if (backedOff && engagesBoss)
    {
        // The back-off move itself and the healer spot always pass.
        std::string name = action->getName();
        if (name != "back off golemagg" && name != "golemagg healer position")
            return 0.0f;
    }

    return 1.0f;
}

float GeddonInfernoMultiplier::GetValue(Action* action)
{
    if (!action)
        return 1.0f;

    // Donor shape: only movement (except the two runouts) and
    // reach-to-cast spells are vetoed — heals, DPS, threat and consumables
    // always pass. This gate also skips the attacker/aura scan for ~90%
    // of evaluated actions.
    bool actionMovesOrReaches = dynamic_cast<MovementAction*>(action) != nullptr ||
        dynamic_cast<CastReachTargetSpellAction*>(action) != nullptr;
    if (!actionMovesOrReaches)
        return 1.0f;

    // Cheap checks first: no aura lookups unless a Geddon fight is live.
    bool bombOnSelf = bot->HasAura(kLivingBombSpellId);
    if (!bombOnSelf && !ai->HasStrategy("geddon", BotState::BOT_STATE_COMBAT))
        return 1.0f;

    bool infernoActive = false;
    if (ai->HasStrategy("geddon", BotState::BOT_STATE_COMBAT))
    {
        AiObjectContext* context = ai->GetAiObjectContext();
        const std::list<ObjectGuid> attackers = AI_VALUE(std::list<ObjectGuid>, "attackers");
        for (const ObjectGuid& attackerGuid : attackers)
        {
            Unit* attacker = ai->GetUnit(attackerGuid);
            if (attacker && attacker->GetEntry() == kGeddonEntry &&
                ai->HasAura(kInfernoSpellId, attacker))
            {
                infernoActive = true;
                break;
            }
        }
    }

    if (ShouldBlockGeddonMove(actionMovesOrReaches, action->getName(), infernoActive, bombOnSelf))
        return 0.0f;
    return 1.0f;
}

float RaidBombMovementMultiplier::GetValue(Action* action)
{
    // Fixed mode is explicitly armed by the player and also owns the return.
    if (action && ai->GetBot()->GetMapId() == 409 && UpdateGeddonBombMovement(ai))
        return GeddonBombAllowsAction(action->getName()) ? 1.0f : 0.0f;
    // The multiplier is installed globally; inspect the encounter aura before
    // doing any group/hold-order work for ordinary combat actions.
    if (!action || !GetRaidBombAura(ai) || !RaidBombMovementAllowed(ai))
        return 1.0f;
    std::string const& name = action->getName();
    if (name == "raid bomb runout" || name == "geddon inferno retreat" || name == "move away from hazard" || name == "grobbulus cloud escape")
        return 1.0f;
    // Preserve the existing emergency escape from damaging ground effects.
    // The bomb action revalidates group clearance on the next reaction.
    if (name == "flee" && AI_VALUE2(bool, "has area debuff", "self target"))
        return 1.0f;
    // Blocking reach prerequisites also keeps healers from walking back
    // into the group to cure while carrying an encounter bomb.
    return dynamic_cast<MovementAction*>(action) || dynamic_cast<CastReachTargetSpellAction*>(action)
        ? 0.0f : 1.0f;
}
