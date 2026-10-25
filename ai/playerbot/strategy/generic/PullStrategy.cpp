
#include "playerbot/playerbot.h"
#include "playerbot/strategy/actions/TwinsOpeningActions.h"
#include "playerbot/strategy/PassiveMultiplier.h"
#include "../../runtime/BotManager.h"
#include "../../runtime/PlayerbotAIStorage.h"
#include "playerbot/GroupMembers.h"
#include "playerbot/WorldPosition.h"
#include "playerbot/policy/pull/PullReachPolicy.h"
#include "PullStrategy.h"
#include "playerbot/strategy/actions/PatchwerkSoakerActions.h"

using namespace ai;

void ai::ReleaseHeldPartyNow(Player* tank)
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
        // Only the bots this pull holds. Zeroing the delay ends their window
        // on the next tick, which then puts their own delay back; a bot with a
        // "wait for attack" of the player's own and no pull window keeps it.
        if (!memberAi->HasPullWindow())
            continue;
        memberAi->TracePullHold("early-release-request");
        memberAi->GetAiObjectContext()->GetValue<uint8>("wait for attack time")->Set(0);
        memberAi->GetAiObjectContext()->GetValue<time_t>("combat start time")->Set(time(0));
    }
}

class PullStrategyActionNodeFactory : public NamedObjectFactory<ActionNode>
{
public:
    PullStrategyActionNodeFactory()
    {
        creators["pull start"] = &pull_start;
        creators["pull action"] = &pull_action;
    }

private:
    static ActionNode* pull_start(PlayerbotAI* ai)
    {
        return new ActionNode("pull start",
            /*P*/ NULL,
            /*A*/ NULL,
            /*C*/ NextAction::array(0, new NextAction("pull action", ACTION_MOVE), NULL));
    }

    // Movement freeze fix: when the target is outside pull range the queued
    // "pull action" must first run "reach pull" (mature movement into range)
    // instead of failing in place. The engine runs prerequisites before the
    // possibility check, so this covers the direct ExecuteQuietAction path too.
    static ActionNode* pull_action(PlayerbotAI* ai)
    {
        return new ActionNode("pull action",
            /*P*/ NextAction::array(0, new NextAction("reach pull", ACTION_MOVE), NULL),
            /*A*/ NULL,
            /*C*/ NULL);
    }
};

PullStrategy::PullStrategy(PlayerbotAI* ai, std::string pullAction, std::string prePullAction)
: Strategy(ai)
, pullActionName(pullAction)
, preActionName(prePullAction)
, pendingToStart(false)
, pullActionCompleted(false)
, pullStartTime(0)
, approachHeadwayTime(0)
, approachDistance(-1.0f)
, pullActionCastMs(0)
, commandPullback(false)
, commandActive(false)
, commandRangedOrder(false)
, commandWantsRanged(false)
, openingReason(PullOpeningReason::BodyPull)
, pathReachable(true)
, hadPullBack(false)
, pullBackIntent(false)
, commandJoinDelay(0)
, returnStartTime(0)
, petReactState(REACT_DEFENSIVE)
{
    actionNodeFactories.Add(std::make_unique<PullStrategyActionNodeFactory>());

    if (!ai->GetBot())
        return;
}

namespace
{
// The spell a ranged weapon fires. Built from the item, not from the strategy:
// GetSpellName() calls back into GetPullActionName(), so asking it from inside
// the ranged-weapon checks would recurse.
std::string ShootSpellOf(const ItemPrototype* proto)
{
    if (proto)
    {
        switch (proto->SubClass)
        {
            case ITEM_SUBCLASS_WEAPON_GUN: return "shoot gun";
            case ITEM_SUBCLASS_WEAPON_BOW: return "shoot bow";
            case ITEM_SUBCLASS_WEAPON_CROSSBOW: return "shoot crossbow";
            case ITEM_SUBCLASS_WEAPON_THROWN: return "throw";
            default: break;
        }
    }
    return "shoot";
}
} // namespace

PullRangedBlocker PullStrategy::GetRangedBlocker(float distance) const
{
    const Item* rangedWeapon = ai->GetBot()->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);
    if (!rangedWeapon)
        return PullRangedBlocker::NoWeapon;

    const ItemPrototype* proto = rangedWeapon->GetProto();
    if (proto)
    {
        // Untrained weapon skill: the shoot cast would fail CanCastSpell
        // forever, so body-pull instead of aborting the request.
        bool trained = true;
        switch (proto->SubClass)
        {
            case ITEM_SUBCLASS_WEAPON_BOW: trained = ai->HasSkill(SKILL_BOWS); break;
            case ITEM_SUBCLASS_WEAPON_GUN: trained = ai->HasSkill(SKILL_GUNS); break;
            case ITEM_SUBCLASS_WEAPON_CROSSBOW: trained = ai->HasSkill(SKILL_CROSSBOWS); break;
            case ITEM_SUBCLASS_WEAPON_THROWN: trained = ai->HasSkill(SKILL_THROWN); break;
            case ITEM_SUBCLASS_WEAPON_WAND: trained = ai->HasSkill(SKILL_WANDS); break;
            default: break;
        }
        if (!trained)
            return PullRangedBlocker::Untrained;

        // The skill alone does not let the bot shoot. The cast resolves the
        // shoot spell by name against the bot's own spellbook, and a bot that
        // owns the weapon skill without the spell (skills are written
        // directly, no trainer visit) fails CanCastSpell on every tick, so
        // the pull would stand still until its timeout.
        if (!ai->HasSpell(ShootSpellOf(proto)))
            return PullRangedBlocker::NoSpell;

        if (proto->SubClass != ITEM_SUBCLASS_WEAPON_THROWN && proto->SubClass != ITEM_SUBCLASS_WEAPON_WAND)
        {
            if (ai->GetBot()->GetUInt32Value(PLAYER_AMMO_ID) == 0)
            {
                std::list<Item*> ammo = ai->GetAiObjectContext()->GetValue<std::list<Item*>>("inventory items", "ammo")->Get();
                if (!ammo.empty() && ammo.front())
                    ai->GetBot()->SetAmmo(ammo.front()->GetEntry());
                else
                    return PullRangedBlocker::NoAmmo;
            }
        }
    }

    // Shoot deadzone: inside the 8-yard minimum range (or with no spell range
    // data) a shoot cast cannot land, so melee/body-pull instead.
    if (distance >= 0.0f)
    {
        float maxRange = 0.0f;
        float minRange = 0.0f;
        bool const hasRange = ai->GetSpellRange(ShootSpellOf(proto), &maxRange, &minRange);
        if ((!hasRange && distance < 8.0f) ||
            (hasRange && minRange > 0.0f && distance < minRange) ||
            (hasRange && minRange <= 0.0f && distance < 8.0f))
            return PullRangedBlocker::TooClose;
    }

    return PullRangedBlocker::None;
}

std::string PullStrategy::GetPullActionName() const
{
    std::string modPullActionName = pullActionName;

    // A plain pull command is a melee engagement: the tank walks in on its own
    // and builds threat while the party waits out the join delay. A pullback,
    // an order that says `ranged`, and a mob nobody can walk to (or that is
    // too far to walk to) open from range - see DecideOpening. The tank's own
    // automatic pulls always do.
    if (commandActive && !commandWantsRanged)
        return "reach pull";

    // Select the faerie fire based on druid strategy
    if (ai->GetBot()->GetClass() == CLASS_DRUID)
    {
        if (modPullActionName == "faerie fire")
        {
            if (ai->HasSpell("faerie fire (feral)") && (ai->HasStrategy("tank feral", BotState::BOT_STATE_COMBAT) || ai->HasStrategy("dps feral", BotState::BOT_STATE_COMBAT)))
            {
                modPullActionName = "faerie fire (feral)";
            }
        }
    }

    if (modPullActionName == "shoot")
    {
        // Distance for the shoot deadzone check; negative = no live target yet.
        Unit* pullTarget = GetTarget();
        float const distance = (pullTarget && pullTarget->IsInWorld())
            ? ai->GetBot()->GetDistance(pullTarget) : -1.0f;
        if (GetRangedBlocker(distance) != PullRangedBlocker::None)
            return "reach pull";
    }

    return modPullActionName;
}

std::string PullStrategy::GetSpellName() const
{
    std::string spellName = GetPullActionName();
    if (spellName == "reach pull")
        return "";

    if (spellName == "shoot")
    {
        const Item* equippedWeapon = ai->GetBot()->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);
        if (equippedWeapon)
        {
            const ItemPrototype* itemPrototype = equippedWeapon->GetProto();
            if (itemPrototype)
            {
                switch (itemPrototype->SubClass)
                {
                    case ITEM_SUBCLASS_WEAPON_GUN:
                    {
                        spellName += " gun";
                        break;
                    }

                    case ITEM_SUBCLASS_WEAPON_BOW:
                    {
                        spellName += " bow";
                        break;
                    }

                    case ITEM_SUBCLASS_WEAPON_CROSSBOW:
                    {
                        spellName += " crossbow";
                        break;
                    }
                    case ITEM_SUBCLASS_WEAPON_THROWN:
                    {
                        spellName = "throw";
                        break;
                    }

                    default: break;
                }
            }
        }
    }

    return spellName;
}

float PullStrategy::GetRange() const
{
    if (GetPullActionName() == "reach pull")
        return ATTACK_DISTANCE;

    float range;

    // Try to get the pull action range
    if (ai->GetSpellRange(GetSpellName(), &range))
    {
        range -= CONTACT_DISTANCE;
    }
    else
    {
        // Set the default range if the range was not found
        range = (pullActionName == "shoot") ? ai->GetRange("shoot") : ai->GetRange("spell");
    }

    return range;
}

std::string PullStrategy::GetPreActionName() const
{
    std::string modPullActionName = preActionName;

    // Select the faerie fire based on druid strategy
    if (ai->GetBot()->GetClass() == CLASS_DRUID)
    {
        if (modPullActionName == "dire bear form")
        {
            if (GetPullActionName() == "faerie fire")
            {
                modPullActionName.clear();
            }
        }
    }

    return modPullActionName;
}

void PullStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "pull start",
        NextAction::array(0, new NextAction("pull start", ACTION_MOVE), new NextAction("pull action", ACTION_MOVE), NULL)));

    // An accepted pull must survive a lost/deferred one-shot continuation.
    // Requeue only after start, until engagement, death, or the existing timeout.
    triggers.push_back(new TriggerNode(
        "pull continue",
        NextAction::array(0, new NextAction("pull action", ACTION_MOVE), NULL)));

    triggers.push_back(new TriggerNode(
        "pull end",
        NextAction::array(0, new NextAction("pull end", ACTION_MOVE), NULL)));

    // The puller's own anchor hold (a pullback parks the tank at the anchor
    // for the fight) is released by the same action the held DPS bots use,
    // once that fight is over.
    triggers.push_back(new TriggerNode(
        "pull anchor done",
        NextAction::array(0, new NextAction("release pull hold", ACTION_HIGH), NULL)));
}

void PullStrategy::InitDeadTriggers(std::list<TriggerNode*>& triggers)
{
    // A pull cannot outlive the puller. The dead engine has to run the pull
    // end too: a tank that dies mid-pull would otherwise keep the party held
    // until their wait window expires, with nobody left to bring them in.
    triggers.push_back(new TriggerNode(
        "pull end",
        NextAction::array(0, new NextAction("pull end", ACTION_HIGH), NULL)));
}

void PullStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    InitCombatTriggers(triggers);

    // ShouldPullTrigger is restrictive enough - dungeon, tank, grouped, nobody
    // fighting, healer with mana, a reachable target - that it can afford a high
    // relevance. Without one it loses to whatever the bot does while idle, which
    // is the behaviour this is meant to replace.
    triggers.push_back(new TriggerNode(
        "should pull",
        NextAction::array(0, new NextAction("pull nearest target", ACTION_HIGH), NULL)));
}

void PullStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new PullMultiplier(ai));
}

void PullStrategy::InitNonCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    InitCombatMultipliers(multipliers);
}

PullStrategy* PullStrategy::Get(PlayerbotAI* ai)
{
    return ai ? ai->GetStrategy<PullStrategy>("pull", BotState::BOT_STATE_COMBAT) : nullptr;
}

Unit* PullStrategy::GetTarget() const
{
    AiObjectContext* context = ai->GetAiObjectContext();
    return AI_VALUE(Unit*, "pull target");
}

void PullStrategy::SetTarget(Unit* target)
{
    AiObjectContext* context = ai->GetAiObjectContext();
    SET_AI_VALUE(Unit*, "pull target", target);
}

bool PullStrategy::CanDoPullAction(Unit* target)
{
    Player* bot = ai->GetBot();
    if (!bot || !target || !target->IsInWorld() || target->GetMapId() != bot->GetMapId())
        return false;

    // GetPullActionName() already falls back to "reach pull" when shoot is
    // blocked (no weapon, no ammo, untrained skill, or 8-yard deadzone), so a
    // melee/body pull is always available. Never return false here: the
    // command would abort with "Can't perform pull action 'shoot'".
    return true;
}

bool PullStrategy::IsProtectionBossCommand() const
{
    Player* bot = ai->GetBot();
    Unit* target = GetTarget();
    return commandActive && !commandPullback && bot && bot->IsAlive() &&
        bot->GetClass() == CLASS_WARRIOR && ai->IsTank(bot) &&
        ai->HasStrategy("protection", BotState::BOT_STATE_COMBAT) &&
        target && target->IsInWorld() && target->IsAlive() &&
        target->GetTypeId() == TYPEID_UNIT &&
        static_cast<Creature*>(target)->IsWorldBoss();
}

bool PullStrategy::NeedsBossPullStance() const
{
    return IsProtectionBossCommand() && !HasPullActionCast() &&
        !ai->HasAura("defensive stance", ai->GetBot());
}

bool PullStrategy::PrepareBossPull(Event& event, bool atFiringPosition)
{
    if (!PatchwerkSoakersReadyForPull(ai))
    {
        ai->StopMoving();
        TraceCommand("patchwerk-rendezvous", "waiting for soakers", 0);
        return false;
    }
    // Never cast during the ranged auto-shot windup: that cancels the arrow.
    if (!IsProtectionBossCommand() || HasPullActionCast())
        return true;

    if (NeedsBossPullStance())
    {
        bossPrepActed = true;
        bool cast = ai->DoSpecificAction("defensive stance", event, true);
        TraceCommand("boss-preparation", "defensive stance", cast ? 1 : 0);
        // Yield even after success; do not force another cast through its GCD.
        // The existing 15-second pull timeout bounds a failed preparation.
        return false;
    }

    // Do not spend a long cooldown while still walking to the firing spot.
    if (!atFiringPosition || bossWallChecked)
        return true;

    Player* bot = ai->GetBot();
    if (ai->HasAura("shield wall", bot) || ai->HasAura("last stand", bot))
    {
        bossWallChecked = true;
        TraceCommand("boss-preparation", "defensive already active", 1);
        return true;
    }

    uint32 wall = ai->GetAiObjectContext()->GetValue<uint32>("spell id", "shield wall")->Get();
    if (!wall || !sServerFacade.IsSpellReady(bot, wall))
    {
        bossWallChecked = true;
        TraceCommand("boss-preparation", "shield wall unavailable", 0);
        return true;
    }

    // A known, ready wall may still be waiting for stance/GCD/equipment.
    // Keep preparation pending instead of silently shooting unprotected.
    bossPrepActed = true;
    bool cast = ai->DoSpecificAction("shield wall", event, true);
    TraceCommand("boss-preparation", "shield wall", cast ? 1 : 0);
    if (cast)
        bossWallChecked = true;
    return false;
}

bool PullStrategy::ConsumeBossPrepRestamp()
{
    if (!bossPrepActed)
        return false;
    bossPrepActed = false;
    return true;
}

Unit* PullStrategy::GetBossOpeningTarget() const
{
    if (!bossOpeningTime || time(0) - bossOpeningTime >= 10 ||
        !ai->GetBot()->IsAlive() || !ai->HasStrategy("protection", BotState::BOT_STATE_COMBAT))
        return nullptr;
    Unit* target = ai->GetAiObjectContext()->GetValue<Unit*>("current target")->Get();
    if (!target || !target->IsInWorld() || !target->IsAlive() ||
        target->GetObjectGuid() != bossOpeningTarget)
        return nullptr;
    return target;
}

void PullStrategy::TraceCommand(char const* stage, char const* action, int result)
{
    if (!commandActive || commandTraceSeen.size() >= 48)
        return;
    std::string key = std::string(stage) + "|" + action + "|" + std::to_string(result);
    if (!commandTraceSeen.insert(key).second)
        return;
    Player* bot = ai->GetBot();
    Unit* target = GetTarget();
    sLog.outString("TBPULL bot=%s guid=%u stage=%s action=%s result=%d state=%u pending=%u completed=%u cast=%u age=%lld target=%u distance=%.1f moving=%u",
        bot->GetName(), bot->GetGUIDLow(), stage, action, result, unsigned(ai->GetState()),
        unsigned(pendingToStart), unsigned(pullActionCompleted), unsigned(pullActionCastMs != 0),
        pullStartTime ? static_cast<long long>(time(nullptr) - pullStartTime) : 0LL,
        target ? target->GetGUIDLow() : 0, target ? bot->GetDistance(target) : -1.0f,
        unsigned(bot->IsMoving()));
}

void PullStrategy::OnPullStarted()
{
    TraceCommand("start");
    pendingToStart = false;
}

void PullStrategy::OnPullActionCompleted()
{
    if (IsProtectionBossCommand() && !bossOpeningTime)
    {
        bossOpeningTarget = GetTarget()->GetObjectGuid();
        bossOpeningTime = time(0);
    }
    TraceCommand("engagement-complete");
    pendingToStart = false;
    pullActionCompleted = true;
    pullStartTime = time(0);
    if (IsPullBackIntent() && !returnStartTime)
        returnStartTime = time(0);
}

void PullStrategy::NotePullActionCast()
{
    if (IsProtectionBossCommand())
    {
        bossOpeningTarget = GetTarget()->GetObjectGuid();
        bossOpeningTime = time(0);
    }
    TraceCommand("cast-issued");
    pullActionCastMs = WorldTimer::getMSTime();
    if (!pullActionCastMs)
        pullActionCastMs = 1; // 0 is "no cast yet"
    // The pull time cap is for the approach; the wait for the arrow gets its own
    // window, so a long walk to the mob must not eat it.
    pullStartTime = time(0);
}

uint32 PullStrategy::GetPullActionCastAgeMs() const
{
    if (!pullActionCastMs)
        return 0;
    return WorldTimer::getMSTimeDiff(pullActionCastMs, WorldTimer::getMSTime());
}

void PullStrategy::NotePullApproach(float distanceNow, bool moving)
{
    // The reach re-requests every tick while out of range, before the walk
    // has moved: the policy's first sample always anchors, so this never
    // expires a pull that just started.
    if (PullApproachHeadway(distanceNow, approachDistance, approachDistance >= 0.0f, moving))
    {
        approachHeadwayTime = time(0);
        approachDistance = distanceNow;
    }
}

bool PullStrategy::HasPullApproachExpired() const
{
    PullApproachFacts facts;
    facts.elapsedOrderSec = pullStartTime ? (long)(time(0) - pullStartTime) : 0;
    facts.elapsedHeadwaySec = approachHeadwayTime ? (long)(time(0) - approachHeadwayTime) : facts.elapsedOrderSec;
    facts.baseWindowSec = GetMaxPullTime();
    return PullApproachExpired(facts);
}

void PullStrategy::OnPullEnded()
{
    TraceCommand("end");
    pendingToStart = false;
    pullActionCompleted = false;
    pullStartTime = 0;
    approachHeadwayTime = 0;
    approachDistance = -1.0f;
    pullActionCastMs = 0;
    commandActive = false;
    commandPullback = false;
    commandRangedOrder = false;
    commandWantsRanged = false;
    openingReason = PullOpeningReason::BodyPull;
    pathReachable = true;
    hadPullBack = false;
    pullBackIntent = false;
    commandJoinDelay = 0;
    returnStartTime = 0;
    SetTarget(nullptr);
}

void PullStrategy::BeginCommand(bool pullback, bool hadPullBackStrategy, uint32 joinDelaySeconds, bool rangedOrder)
{
    bossOpeningTarget.Clear();
    bossOpeningTime = 0;
    bossWallChecked = false;
    bossPrepActed = false;
    commandTraceSeen.clear();
    commandActive = true;
    commandPullback = pullback;
    commandRangedOrder = rangedOrder;
    // Until the target is known (RequestPull) the order's own words decide:
    // a pullback and a `ranged` order open from range, a plain pull walks in.
    commandWantsRanged = pullback || rangedOrder;
    openingReason = rangedOrder ? PullOpeningReason::Explicit
        : pullback ? PullOpeningReason::Pullback : PullOpeningReason::BodyPull;
    pathReachable = true;
    hadPullBack = hadPullBackStrategy;
    commandJoinDelay = joinDelaySeconds;
    returnStartTime = 0;
}

void PullStrategy::RequestPull(Unit* target, bool resetTime)
{
    SetTarget(target);
    pendingToStart = true;
    TraceCommand("request", "", resetTime ? 1 : 0);

    // Return-leg intent, fixed for the whole pull: the command's own mode when
    // it owns this pull, else the tank's configured "pull back" behaviour (the
    // fallback for its automatic dungeon pulls). Snapshotting it here keeps the
    // decision out of reach of later strategy changes - the pull end restores
    // the tank's default "pull back", and the return trigger must not read that
    // as "this pull was a pullback".
    pullBackIntent = commandActive ? commandPullback
        : ai->HasStrategy("pull back", BotState::BOT_STATE_COMBAT);

    if(resetTime)
    {
        pullActionCompleted = false;
        pullStartTime = time(0);
        pullActionCastMs = 0;
        // Anchor the approach-headway clock with the order: the first
        // approach sample always counts (PullReachPolicy.h), so a pull whose
        // walk starts late still gets its full first window.
        approachHeadwayTime = pullStartTime;
        approachDistance = -1.0f;
        // The opening is decided once per pull, on the first request: a mob
        // that moves while the tank approaches must not flip the pull between
        // walking in and shooting.
        if (commandActive)
            DecideOpening(target);
    }
}

void PullStrategy::DecideOpening(Unit* target)
{
    Player* bot = ai->GetBot();
    if (!bot || !target)
        return;

    PullOpeningInput input;
    input.pullback = commandPullback;
    input.explicitRanged = commandRangedOrder;
    input.autoEnabled = sPlayerbotAIConfig.rangedPull;
    input.meleeApproach = sPlayerbotAIConfig.rangedPullMeleeApproach;
    input.distance = bot->GetDistance(target);
    input.inLos = bot->IsWithinLOSInMap(target, true);

    // What the tank would fire, and how far. The weapon classes shoot with the
    // spell their weapon teaches; hunters and casters use their pull spell.
    std::string spell = pullActionName;
    if (spell == "shoot")
    {
        const Item* weapon = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);
        spell = ShootSpellOf(weapon ? weapon->GetProto() : nullptr);
    }
    float maxRange = 0.0f;
    if (!spell.empty() && ai->GetSpellRange(spell, &maxRange))
        input.maxRange = maxRange - CONTACT_DISTANCE;
    else
        input.maxRange = (pullActionName == "shoot") ? ai->GetRange("shoot") : ai->GetRange("spell");

    // One navmesh query per pull, and only when a path could change the answer.
    pathReachable = true;
    if (!input.explicitRanged && !input.pullback && input.autoEnabled && input.inLos &&
        input.distance <= input.maxRange)
        pathReachable = WorldPosition(bot).canPathTo(WorldPosition(target), bot);
    input.pathReachable = pathReachable;

    PullOpeningDecision const decision = DecidePullOpening(input);
    commandWantsRanged = decision.wantRanged;
    openingReason = decision.reason;
}
float PullMultiplier::GetValue(Action* action)
{
    if (TwinsOpeningTank(ai) && (action->getName()=="twins position" ||
        action->getName()=="twins engage" || action->getName()=="twins searing pain" ||
        action->getName()=="defensive stance")) return 1.0f;
    const PullStrategy* strategy = PullStrategy::Get(ai);
    // Keep this Protection puller's opening in Defensive Stance. This short
    // window affects neither Fury nor ordinary trash/automatic pulls.
    if (strategy && strategy->GetBossOpeningTarget())
    {
        const std::string name = action->getName();
        if (name == "battle stance" || name == "berserker stance" ||
            name == "charge" || name == "intercept" || name == "overpower")
            return 0.0f;
    }
    if (strategy && strategy->HasTarget())
    {
        if ((action->getName() == "pull my target") ||
            (action->getName() == "pull rti target") ||
            (action->getName() == "reach pull") ||
            (action->getName() == "pull start") ||
            (action->getName() == "pull action") ||
            (action->getName() == "return to pull position") ||
            (action->getName() == "pull end"))
        {
            return 1.0f;
        }

        // The returning tank is under fire: let emergency, defensive and
        // taunt-grade actions through (taunt, defensives, self-heal, potions)
        // while keeping the damage rotation muted until the pull ends.
        if (action->getRelevance() >= ACTION_EMERGENCY)
        {
            return 1.0f;
        }

        // Taunt-grade actions (ACTION_INTERRUPT and above: taunt, shield
        // bash, earth shock) stay eligible at low priority; emergency and
        // above (defensives, self-heal, potions) run at full priority.
        if (action->getRelevance() >= ACTION_INTERRUPT)
        {
            return 0.01f;
        }

        return 0.0f;
    }

    return 1.0f;
}

void PossibleAdsStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "possible ads",
        NextAction::array(0, new NextAction("flee with pet", ACTION_EMERGENCY), NULL)));
}

void PullBackStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode(
        "return to pull position",
        NextAction::array(0, new NextAction("return to pull position", static_cast<float>(ACTION_MOVE) + 5.0f), NULL)));
}

void PullBackStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    InitCombatTriggers(triggers);
}
