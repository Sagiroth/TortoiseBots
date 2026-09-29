
#include "playerbot/playerbot.h"
#include "playerbot/strategy/PassiveMultiplier.h"
#include "PullStrategy.h"

using namespace ai;

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
            /*C*/ NextAction::array(0, new NextAction("pull action", ACTION_NORMAL), NULL));
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
, commandPullback(false)
, commandActive(false)
, hadPullBack(false)
, commandJoinDelay(0)
, returnStartTime(0)
, petReactState(REACT_DEFENSIVE)
{
    actionNodeFactories.Add(std::make_unique<PullStrategyActionNodeFactory>());

    if (!ai->GetBot())
        return;
}

std::string PullStrategy::GetPullActionName() const
{
    std::string modPullActionName = pullActionName;

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
        const Item* rangedWeapon = ai->GetBot()->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);
        if (!rangedWeapon)
            return "reach pull";

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
                return "reach pull";

            if (proto->SubClass != ITEM_SUBCLASS_WEAPON_THROWN && proto->SubClass != ITEM_SUBCLASS_WEAPON_WAND)
            {
                if (ai->GetBot()->GetUInt32Value(PLAYER_AMMO_ID) == 0)
                {
                    std::list<Item*> ammo = ai->GetAiObjectContext()->GetValue<std::list<Item*>>("inventory items", "ammo")->Get();
                    if (!ammo.empty() && ammo.front())
                    {
                        ai->GetBot()->SetAmmo(ammo.front()->GetEntry());
                    }
                    else
                    {
                        return "reach pull";
                    }
                }
            }
        }

        // Shoot deadzone: inside the 8-yard minimum range (or with no spell
        // range data) a shoot cast cannot land, so melee/body-pull instead.
        // Build the shoot spell name locally: GetSpellName() calls back into
        // GetPullActionName(), so querying it here would recurse.
        Unit* pullTarget = GetTarget();
        if (pullTarget && pullTarget->IsInWorld())
        {
            std::string shootSpell = "shoot";
            if (proto)
            {
                switch (proto->SubClass)
                {
                    case ITEM_SUBCLASS_WEAPON_GUN: shootSpell = "shoot gun"; break;
                    case ITEM_SUBCLASS_WEAPON_BOW: shootSpell = "shoot bow"; break;
                    case ITEM_SUBCLASS_WEAPON_CROSSBOW: shootSpell = "shoot crossbow"; break;
                    case ITEM_SUBCLASS_WEAPON_THROWN: shootSpell = "throw"; break;
                    default: break;
                }
            }
            float maxRange = 0.0f;
            float minRange = 0.0f;
            bool hasRange = ai->GetSpellRange(shootSpell, &maxRange, &minRange);
            float distance = ai->GetBot()->GetDistance(pullTarget);
            if ((!hasRange && distance < 8.0f) ||
                (hasRange && minRange > 0.0f && distance < minRange) ||
                (hasRange && minRange <= 0.0f && distance < 8.0f))
                return "reach pull";
        }
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

    triggers.push_back(new TriggerNode(
        "pull end",
        NextAction::array(0, new NextAction("pull end", ACTION_MOVE), NULL)));
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

void PullStrategy::OnPullStarted()
{
    pendingToStart = false;
}

void PullStrategy::OnPullActionCompleted()
{
    pendingToStart = false;
    pullActionCompleted = true;
    pullStartTime = time(0);
    if (commandActive && commandPullback && !returnStartTime)
        returnStartTime = time(0);
}
void PullStrategy::OnPullEnded()
{
    pendingToStart = false;
    pullActionCompleted = false;
    pullStartTime = 0;
    commandActive = false;
    commandPullback = false;
    hadPullBack = false;
    commandJoinDelay = 0;
    returnStartTime = 0;
    SetTarget(nullptr);
}

void PullStrategy::BeginCommand(bool pullback, bool hadPullBackStrategy, uint32 joinDelaySeconds)
{
    commandActive = true;
    commandPullback = pullback;
    hadPullBack = hadPullBackStrategy;
    commandJoinDelay = joinDelaySeconds;
    returnStartTime = 0;
}

void PullStrategy::RequestPull(Unit* target, bool resetTime)
{
    SetTarget(target);
    pendingToStart = true;
    if(resetTime)
    {
        pullActionCompleted = false;
        pullStartTime = time(0);
    }
}
float PullMultiplier::GetValue(Action* action)
{
    const PullStrategy* strategy = PullStrategy::Get(ai);
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
