#include "playerbot/playerbot.h"
#include "PriestMultipliers.h"
#include "ShadowPriestStrategy.h"

using namespace ai;

class ShadowPriestStrategyActionNodeFactory : public NamedObjectFactory<ActionNode>
{
public:
    ShadowPriestStrategyActionNodeFactory()
    {
        creators["dispersion"] = &dispersion;
    }

private:
    ACTION_NODE_A(dispersion, "dispersion", "mana potion");
};

ShadowPriestStrategy::ShadowPriestStrategy(PlayerbotAI* ai) : PriestStrategy(ai)
{
    actionNodeFactories.Add(std::make_unique<ShadowPriestStrategyActionNodeFactory>());
}

#ifdef MANGOSBOT_ZERO // Vanilla

NextAction** ShadowPriestStrategy::GetDefaultCombatActions()
{
    return NextAction::array(0, new NextAction("mind flay", ACTION_IDLE), NULL);
}

void ShadowPriestStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    PriestStrategy::InitCombatTriggers(triggers);

    triggers.push_back(new TriggerNode(
        "mind blast",
        NextAction::array(0, new NextAction("mind blast", ACTION_HIGH), NULL)));

    triggers.push_back(new TriggerNode(
        "vampiric embrace",
        NextAction::array(0, new NextAction("vampiric embrace", ACTION_NORMAL + 3), NULL)));

    triggers.push_back(new TriggerNode(
        "shadow word: pain",
        NextAction::array(0, new NextAction("shadow word: pain", ACTION_NORMAL + 2), NULL)));

    triggers.push_back(new TriggerNode(
        "devouring plague",
        NextAction::array(0, new NextAction("devouring plague", ACTION_NORMAL + 2), NULL)));

    triggers.push_back(new TriggerNode(
        "hex of weakness",
        NextAction::array(0, new NextAction("hex of weakness", ACTION_NORMAL + 2), NULL)));

    triggers.push_back(new TriggerNode(
        "no mana",
        NextAction::array(0, new NextAction("shoot", ACTION_NORMAL), NULL)));

    // TurtleWoW build additions:
    //
    // Vampiric Touch (Shadow 5/2, 2/2): debuff that boosts Vampiric Embrace
    // healing and adds party mana feedback (% of Shadow damage as mana).
    // The "mana battery" half of the Shadow Priest's role — without this,
    // VE alone heals but doesn't restore mana.
    triggers.push_back(new TriggerNode(
        "vampiric touch",
        NextAction::array(0, new NextAction("vampiric touch", ACTION_NORMAL + 3), NULL)));

    // Silence (Shadow 5/1, 1/1): caster-mob interrupt. 5-sec silence,
    // 45-sec CD. Pairs with priority-target tracking so we silence
    // healers / heavy-spell mobs over melee.
    triggers.push_back(new TriggerNode(
        "silence",
        NextAction::array(0, new NextAction("silence", ACTION_INTERRUPT), NULL)));

    // Shadowfiend mana refill on long fights. Baseline class ability in
    // Turtle 1.18 (verify in-game). 5-min CD; pet attacks target and
    // returns mana to caster on each hit. ~50% of mana pool refill.
    triggers.push_back(new TriggerNode(
        "shadowfiend",
        NextAction::array(0, new NextAction("shadowfiend", ACTION_HIGH + 5), NULL)));

    // Inner Focus → Mind Blast: pre-buff for guaranteed Mind Blast crit
    // when mana is low. The crit triggers Spirit Tap (+100% Spirit + 50%
    // mana regen while casting for ~30 sec). 5-min CD on Inner Focus.
    triggers.push_back(new TriggerNode(
        "inner focus",
        NextAction::array(0, new NextAction("inner focus", ACTION_HIGH + 1), NULL)));

    // TurtleWoW Shadow redesign awareness:
    //
    // Shadow Weaving rebuild: when target is missing the Shadow Vulnerability
    // debuff (e.g. just after a target swap / boss phase change), prioritize
    // Mind Flay to rebuild stacks fast. With 5/5 Shadow Weaving = 100% proc
    // rate on Shadow spells, stacks rebuild in ~3-4 GCDs of Mind Flay/SWP.
    triggers.push_back(new TriggerNode(
        "shadow weaving missing",
        NextAction::array(0, new NextAction("mind flay", ACTION_NORMAL + 2), NULL)));

    // Spirit Tap proc window — informational trigger (no action). The bot's
    // existing rotation just keeps firing; this is here so we have the
    // value registered in the engine for future use (e.g., pace-up during
    // proc window, or post-window Inner Focus to re-proc).
    //
    // Currently no associated NextAction — the trigger evaluates but does
    // not change rotation. This is intentional: the proc-window adjustments
    // are subtle enough that misfiring would be worse than no-op.
}

void ShadowPriestStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    PriestStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{
    PriestStrategy::InitReactionTriggers(triggers);
}

void ShadowPriestStrategy::InitDeadTriggers(std::list<TriggerNode*>& triggers)
{
    PriestStrategy::InitDeadTriggers(triggers);
}

void ShadowPriestPveStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestStrategy::InitCombatTriggers(triggers);
    PriestPveStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestPveStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestStrategy::InitNonCombatTriggers(triggers);
    PriestPveStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestPveStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestStrategy::InitReactionTriggers(triggers);
    PriestPveStrategy::InitReactionTriggers(triggers);
}

void ShadowPriestPveStrategy::InitDeadTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestStrategy::InitDeadTriggers(triggers);
    PriestPveStrategy::InitDeadTriggers(triggers);
}

void ShadowPriestPvpStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestStrategy::InitCombatTriggers(triggers);
    PriestPvpStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestPvpStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestStrategy::InitNonCombatTriggers(triggers);
    PriestPvpStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestPvpStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestStrategy::InitReactionTriggers(triggers);
    PriestPvpStrategy::InitReactionTriggers(triggers);
}

void ShadowPriestPvpStrategy::InitDeadTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestStrategy::InitDeadTriggers(triggers);
    PriestPvpStrategy::InitDeadTriggers(triggers);
}

void ShadowPriestRaidStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestStrategy::InitCombatTriggers(triggers);
    PriestRaidStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestRaidStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestStrategy::InitNonCombatTriggers(triggers);
    PriestRaidStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestRaidStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestStrategy::InitReactionTriggers(triggers);
    PriestRaidStrategy::InitReactionTriggers(triggers);
}

void ShadowPriestRaidStrategy::InitDeadTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestStrategy::InitDeadTriggers(triggers);
    PriestRaidStrategy::InitDeadTriggers(triggers);
}

void ShadowPriestAoeStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    PriestAoeStrategy::InitCombatTriggers(triggers);

    triggers.push_back(new TriggerNode(
        "shadow word: pain on attacker",
        NextAction::array(0, new NextAction("shadow word: pain on attacker", ACTION_HIGH + 1), NULL)));
}

void ShadowPriestAoeStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    PriestAoeStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestAoePveStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestAoeStrategy::InitCombatTriggers(triggers);
    PriestAoePveStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestAoePveStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestAoeStrategy::InitNonCombatTriggers(triggers);
    PriestAoePveStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestAoePvpStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestAoeStrategy::InitCombatTriggers(triggers);
    PriestAoePvpStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestAoePvpStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestAoeStrategy::InitNonCombatTriggers(triggers);
    PriestAoePvpStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestAoeRaidStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestAoeStrategy::InitCombatTriggers(triggers);
    PriestAoeRaidStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestAoeRaidStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestAoeStrategy::InitNonCombatTriggers(triggers);
    PriestAoeRaidStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestBuffStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    PriestBuffStrategy::InitCombatTriggers(triggers);

    triggers.push_back(new TriggerNode(
        "shadowform",
        NextAction::array(0, new NextAction("shadowform", ACTION_MOVE), NULL)));

    triggers.push_back(new TriggerNode(
        "feedback",
        NextAction::array(0, new NextAction("feedback", ACTION_HIGH), NULL)));
}

void ShadowPriestBuffStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    PriestBuffStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestBuffPveStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestBuffStrategy::InitCombatTriggers(triggers);
    PriestBuffPveStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestBuffPveStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestBuffStrategy::InitNonCombatTriggers(triggers);
    PriestBuffPveStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestBuffPvpStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestBuffStrategy::InitCombatTriggers(triggers);
    PriestBuffPvpStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestBuffPvpStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestBuffStrategy::InitNonCombatTriggers(triggers);
    PriestBuffPvpStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestBuffRaidStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestBuffStrategy::InitCombatTriggers(triggers);
    PriestBuffRaidStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestBuffRaidStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestBuffStrategy::InitNonCombatTriggers(triggers);
    PriestBuffRaidStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestBoostStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    PriestBoostStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestBoostStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    PriestBoostStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestBoostPveStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestBoostStrategy::InitCombatTriggers(triggers);
    PriestBoostPveStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestBoostPveStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestBoostStrategy::InitNonCombatTriggers(triggers);
    PriestBoostPveStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestBoostPvpStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestBoostStrategy::InitCombatTriggers(triggers);
    PriestBoostPvpStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestBoostPvpStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestBoostStrategy::InitNonCombatTriggers(triggers);
    PriestBoostPvpStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestBoostRaidStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestBoostStrategy::InitCombatTriggers(triggers);
    PriestBoostRaidStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestBoostRaidStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestBoostStrategy::InitNonCombatTriggers(triggers);
    PriestBoostRaidStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestCcStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    PriestCcStrategy::InitCombatTriggers(triggers);

    triggers.push_back(new TriggerNode(
        "silence",
        NextAction::array(0, new NextAction("silence", ACTION_INTERRUPT + 1), NULL)));

    triggers.push_back(new TriggerNode(
        "silence on enemy healer",
        NextAction::array(0, new NextAction("silence on enemy healer", ACTION_INTERRUPT), NULL)));
}

void ShadowPriestCcStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    PriestCcStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestCcPveStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestCcStrategy::InitCombatTriggers(triggers);
    PriestCcPveStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestCcPveStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestCcStrategy::InitNonCombatTriggers(triggers);
    PriestCcPveStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestCcPvpStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestCcStrategy::InitCombatTriggers(triggers);
    PriestCcPvpStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestCcPvpStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestCcStrategy::InitNonCombatTriggers(triggers);
    PriestCcPvpStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestCcRaidStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestCcStrategy::InitCombatTriggers(triggers);
    PriestCcRaidStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestCcRaidStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestCcStrategy::InitNonCombatTriggers(triggers);
    PriestCcRaidStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestCureStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    PriestCureStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestCureStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    PriestCureStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestCurePveStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestCureStrategy::InitCombatTriggers(triggers);
    PriestCurePveStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestCurePveStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestCureStrategy::InitNonCombatTriggers(triggers);
    PriestCurePveStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestCurePvpStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestCureStrategy::InitCombatTriggers(triggers);
    PriestCurePvpStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestCurePvpStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestCureStrategy::InitNonCombatTriggers(triggers);
    PriestCurePvpStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestCureRaidStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestCureStrategy::InitCombatTriggers(triggers);
    PriestCureRaidStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestCureRaidStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestCureStrategy::InitNonCombatTriggers(triggers);
    PriestCureRaidStrategy::InitNonCombatTriggers(triggers);
}

#endif
#ifdef MANGOSBOT_ONE // TBC

NextAction** ShadowPriestStrategy::GetDefaultCombatActions()
{
    return NextAction::array(0, new NextAction("mind flay", ACTION_IDLE), NULL);
}

void ShadowPriestStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    PriestStrategy::InitCombatTriggers(triggers);

    triggers.push_back(new TriggerNode(
        "target critical health",
        NextAction::array(0, new NextAction("shadow word: death", ACTION_HIGH + 1), NULL)));

    triggers.push_back(new TriggerNode(
        "mind blast",
        NextAction::array(0, new NextAction("mind blast", ACTION_HIGH), NULL)));

    triggers.push_back(new TriggerNode(
        "vampiric embrace",
        NextAction::array(0, new NextAction("vampiric embrace", ACTION_NORMAL + 3), NULL)));

    triggers.push_back(new TriggerNode(
        "shadow word: pain",
        NextAction::array(0, new NextAction("shadow word: pain", ACTION_NORMAL + 2), NULL)));

    triggers.push_back(new TriggerNode(
        "devouring plague",
        NextAction::array(0, new NextAction("devouring plague", ACTION_NORMAL + 2), NULL)));

    triggers.push_back(new TriggerNode(
        "hex of weakness",
        NextAction::array(0, new NextAction("hex of weakness", ACTION_NORMAL + 2), NULL)));

    triggers.push_back(new TriggerNode(
        "vampiric touch",
        NextAction::array(0, new NextAction("vampiric touch", ACTION_NORMAL + 1), NULL)));

    triggers.push_back(new TriggerNode(
        "no mana",
        NextAction::array(0, new NextAction("shoot", ACTION_NORMAL), NULL)));
}

void ShadowPriestStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    PriestStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{
    PriestStrategy::InitReactionTriggers(triggers);
}

void ShadowPriestStrategy::InitDeadTriggers(std::list<TriggerNode*>& triggers)
{
    PriestStrategy::InitDeadTriggers(triggers);
}

void ShadowPriestPveStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestStrategy::InitCombatTriggers(triggers);
    PriestPveStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestPveStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestStrategy::InitNonCombatTriggers(triggers);
    PriestPveStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestPveStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestStrategy::InitReactionTriggers(triggers);
    PriestPveStrategy::InitReactionTriggers(triggers);
}

void ShadowPriestPveStrategy::InitDeadTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestStrategy::InitDeadTriggers(triggers);
    PriestPveStrategy::InitDeadTriggers(triggers);
}

void ShadowPriestPvpStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestStrategy::InitCombatTriggers(triggers);
    PriestPvpStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestPvpStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestStrategy::InitNonCombatTriggers(triggers);
    PriestPvpStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestPvpStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestStrategy::InitReactionTriggers(triggers);
    PriestPvpStrategy::InitReactionTriggers(triggers);
}

void ShadowPriestPvpStrategy::InitDeadTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestStrategy::InitDeadTriggers(triggers);
    PriestPvpStrategy::InitDeadTriggers(triggers);
}

void ShadowPriestRaidStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestStrategy::InitCombatTriggers(triggers);
    PriestRaidStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestRaidStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestStrategy::InitNonCombatTriggers(triggers);
    PriestRaidStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestRaidStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestStrategy::InitReactionTriggers(triggers);
    PriestRaidStrategy::InitReactionTriggers(triggers);
}

void ShadowPriestRaidStrategy::InitDeadTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestStrategy::InitDeadTriggers(triggers);
    PriestRaidStrategy::InitDeadTriggers(triggers);
}

void ShadowPriestAoeStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    PriestAoeStrategy::InitCombatTriggers(triggers);

    triggers.push_back(new TriggerNode(
        "shadow word: pain on attacker",
        NextAction::array(0, new NextAction("shadow word: pain on attacker", ACTION_HIGH + 1), NULL)));

    triggers.push_back(new TriggerNode(
        "vampiric touch on attacker",
        NextAction::array(0, new NextAction("vampiric touch on attacker", ACTION_HIGH), NULL)));
}

void ShadowPriestAoeStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    PriestAoeStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestAoePveStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestAoeStrategy::InitCombatTriggers(triggers);
    PriestAoePveStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestAoePveStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestAoeStrategy::InitNonCombatTriggers(triggers);
    PriestAoePveStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestAoePvpStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestAoeStrategy::InitCombatTriggers(triggers);
    PriestAoePvpStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestAoePvpStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestAoeStrategy::InitNonCombatTriggers(triggers);
    PriestAoePvpStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestAoeRaidStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestAoeStrategy::InitCombatTriggers(triggers);
    PriestAoeRaidStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestAoeRaidStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestAoeStrategy::InitNonCombatTriggers(triggers);
    PriestAoeRaidStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestBuffStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    PriestBuffStrategy::InitCombatTriggers(triggers);

    triggers.push_back(new TriggerNode(
        "shadowform",
        NextAction::array(0, new NextAction("shadowform", ACTION_MOVE), NULL)));

    triggers.push_back(new TriggerNode(
        "feedback",
        NextAction::array(0, new NextAction("feedback", ACTION_HIGH), NULL)));
}

void ShadowPriestBuffStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    PriestBuffStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestBuffPveStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestBuffStrategy::InitCombatTriggers(triggers);
    PriestBuffPveStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestBuffPveStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestBuffStrategy::InitNonCombatTriggers(triggers);
    PriestBuffPveStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestBuffPvpStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestBuffStrategy::InitCombatTriggers(triggers);
    PriestBuffPvpStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestBuffPvpStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestBuffStrategy::InitNonCombatTriggers(triggers);
    PriestBuffPvpStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestBuffRaidStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestBuffStrategy::InitCombatTriggers(triggers);
    PriestBuffRaidStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestBuffRaidStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestBuffStrategy::InitNonCombatTriggers(triggers);
    PriestBuffRaidStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestBoostStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    PriestBoostStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestBoostStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    PriestBoostStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestBoostPveStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestBoostStrategy::InitCombatTriggers(triggers);
    PriestBoostPveStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestBoostPveStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestBoostStrategy::InitNonCombatTriggers(triggers);
    PriestBoostPveStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestBoostPvpStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestBoostStrategy::InitCombatTriggers(triggers);
    PriestBoostPvpStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestBoostPvpStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestBoostStrategy::InitNonCombatTriggers(triggers);
    PriestBoostPvpStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestBoostRaidStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestBoostStrategy::InitCombatTriggers(triggers);
    PriestBoostRaidStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestBoostRaidStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestBoostStrategy::InitNonCombatTriggers(triggers);
    PriestBoostRaidStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestCcStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    PriestCcStrategy::InitCombatTriggers(triggers);

    triggers.push_back(new TriggerNode(
        "silence",
        NextAction::array(0, new NextAction("silence", ACTION_INTERRUPT + 1), NULL)));

    triggers.push_back(new TriggerNode(
        "silence on enemy healer",
        NextAction::array(0, new NextAction("silence on enemy healer", ACTION_INTERRUPT), NULL)));
}

void ShadowPriestCcStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    PriestCcStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestCcPveStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestCcStrategy::InitCombatTriggers(triggers);
    PriestCcPveStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestCcPveStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestCcStrategy::InitNonCombatTriggers(triggers);
    PriestCcPveStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestCcPvpStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestCcStrategy::InitCombatTriggers(triggers);
    PriestCcPvpStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestCcPvpStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestCcStrategy::InitNonCombatTriggers(triggers);
    PriestCcPvpStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestCcRaidStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestCcStrategy::InitCombatTriggers(triggers);
    PriestCcRaidStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestCcRaidStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestCcStrategy::InitNonCombatTriggers(triggers);
    PriestCcRaidStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestCureStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    PriestCureStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestCureStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    PriestCureStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestCurePveStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestCureStrategy::InitCombatTriggers(triggers);
    PriestCurePveStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestCurePveStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestCureStrategy::InitNonCombatTriggers(triggers);
    PriestCurePveStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestCurePvpStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestCureStrategy::InitCombatTriggers(triggers);
    PriestCurePvpStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestCurePvpStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestCureStrategy::InitNonCombatTriggers(triggers);
    PriestCurePvpStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestCureRaidStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestCureStrategy::InitCombatTriggers(triggers);
    PriestCureRaidStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestCureRaidStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestCureStrategy::InitNonCombatTriggers(triggers);
    PriestCureRaidStrategy::InitNonCombatTriggers(triggers);
}

#endif
#ifdef MANGOSBOT_TWO // WOTLK

NextAction** ShadowPriestStrategy::GetDefaultCombatActions()
{
    return NextAction::array(0, new NextAction("mind flay", ACTION_IDLE), NULL);
}

void ShadowPriestStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    PriestStrategy::InitCombatTriggers(triggers);

    triggers.push_back(new TriggerNode(
        "target critical health",
        NextAction::array(0, new NextAction("shadow word: death", ACTION_HIGH + 1), NULL)));

    triggers.push_back(new TriggerNode(
        "mind blast",
        NextAction::array(0, new NextAction("mind blast", ACTION_HIGH), NULL)));

    triggers.push_back(new TriggerNode(
        "vampiric embrace",
        NextAction::array(0, new NextAction("vampiric embrace", ACTION_NORMAL + 3), NULL)));

    triggers.push_back(new TriggerNode(
        "shadow word: pain",
        NextAction::array(0, new NextAction("shadow word: pain", ACTION_NORMAL + 2), NULL)));

    triggers.push_back(new TriggerNode(
        "devouring plague",
        NextAction::array(0, new NextAction("devouring plague", ACTION_NORMAL + 2), NULL)));

    triggers.push_back(new TriggerNode(
        "hex of weakness",
        NextAction::array(0, new NextAction("hex of weakness", ACTION_NORMAL + 2), NULL)));

    triggers.push_back(new TriggerNode(
        "vampiric touch",
        NextAction::array(0, new NextAction("vampiric touch", ACTION_NORMAL + 1), NULL)));

    triggers.push_back(new TriggerNode(
        "no mana",
        NextAction::array(0, new NextAction("shoot", ACTION_NORMAL), NULL)));
}

void ShadowPriestStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    PriestStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{
    PriestStrategy::InitReactionTriggers(triggers);
}

void ShadowPriestStrategy::InitDeadTriggers(std::list<TriggerNode*>& triggers)
{
    PriestStrategy::InitDeadTriggers(triggers);
}

void ShadowPriestPveStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestStrategy::InitCombatTriggers(triggers);
    PriestPveStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestPveStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestStrategy::InitNonCombatTriggers(triggers);
    PriestPveStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestPveStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestStrategy::InitReactionTriggers(triggers);
    PriestPveStrategy::InitReactionTriggers(triggers);
}

void ShadowPriestPveStrategy::InitDeadTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestStrategy::InitDeadTriggers(triggers);
    PriestPveStrategy::InitDeadTriggers(triggers);
}

void ShadowPriestPvpStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestStrategy::InitCombatTriggers(triggers);
    PriestPvpStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestPvpStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestStrategy::InitNonCombatTriggers(triggers);
    PriestPvpStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestPvpStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestStrategy::InitReactionTriggers(triggers);
    PriestPvpStrategy::InitReactionTriggers(triggers);
}

void ShadowPriestPvpStrategy::InitDeadTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestStrategy::InitDeadTriggers(triggers);
    PriestPvpStrategy::InitDeadTriggers(triggers);
}

void ShadowPriestRaidStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestStrategy::InitCombatTriggers(triggers);
    PriestRaidStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestRaidStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestStrategy::InitNonCombatTriggers(triggers);
    PriestRaidStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestRaidStrategy::InitReactionTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestStrategy::InitReactionTriggers(triggers);
    PriestRaidStrategy::InitReactionTriggers(triggers);
}

void ShadowPriestRaidStrategy::InitDeadTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestStrategy::InitDeadTriggers(triggers);
    PriestRaidStrategy::InitDeadTriggers(triggers);
}

void ShadowPriestAoeStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    PriestAoeStrategy::InitCombatTriggers(triggers);

    triggers.push_back(new TriggerNode(
        "shadow word: pain on attacker",
        NextAction::array(0, new NextAction("shadow word: pain on attacker", ACTION_HIGH + 1), NULL)));

    triggers.push_back(new TriggerNode(
        "vampiric touch on attacker",
        NextAction::array(0, new NextAction("vampiric touch on attacker", ACTION_HIGH), NULL)));
}

void ShadowPriestAoeStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    PriestAoeStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestAoePveStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestAoeStrategy::InitCombatTriggers(triggers);
    PriestAoePveStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestAoePveStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestAoeStrategy::InitNonCombatTriggers(triggers);
    PriestAoePveStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestAoePvpStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestAoeStrategy::InitCombatTriggers(triggers);
    PriestAoePvpStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestAoePvpStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestAoeStrategy::InitNonCombatTriggers(triggers);
    PriestAoePvpStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestAoeRaidStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestAoeStrategy::InitCombatTriggers(triggers);
    PriestAoeRaidStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestAoeRaidStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestAoeStrategy::InitNonCombatTriggers(triggers);
    PriestAoeRaidStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestBuffStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    PriestBuffStrategy::InitCombatTriggers(triggers);

    triggers.push_back(new TriggerNode(
        "shadowform",
        NextAction::array(0, new NextAction("shadowform", ACTION_MOVE), NULL)));

    triggers.push_back(new TriggerNode(
        "feedback",
        NextAction::array(0, new NextAction("feedback", ACTION_HIGH), NULL)));

    /*
    triggers.push_back(new TriggerNode(
        "low mana",
        NextAction::array(0, new NextAction("dispersion", ACTION_EMERGENCY + 5), NULL)));
    */
}

void ShadowPriestBuffStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    PriestBuffStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestBuffPveStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestBuffStrategy::InitCombatTriggers(triggers);
    PriestBuffPveStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestBuffPveStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestBuffStrategy::InitNonCombatTriggers(triggers);
    PriestBuffPveStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestBuffPvpStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestBuffStrategy::InitCombatTriggers(triggers);
    PriestBuffPvpStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestBuffPvpStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestBuffStrategy::InitNonCombatTriggers(triggers);
    PriestBuffPvpStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestBuffRaidStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestBuffStrategy::InitCombatTriggers(triggers);
    PriestBuffRaidStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestBuffRaidStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestBuffStrategy::InitNonCombatTriggers(triggers);
    PriestBuffRaidStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestBoostStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    PriestBoostStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestBoostStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    PriestBoostStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestBoostPveStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestBoostStrategy::InitCombatTriggers(triggers);
    PriestBoostPveStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestBoostPveStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestBoostStrategy::InitNonCombatTriggers(triggers);
    PriestBoostPveStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestBoostPvpStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestBoostStrategy::InitCombatTriggers(triggers);
    PriestBoostPvpStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestBoostPvpStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestBoostStrategy::InitNonCombatTriggers(triggers);
    PriestBoostPvpStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestBoostRaidStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestBoostStrategy::InitCombatTriggers(triggers);
    PriestBoostRaidStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestBoostRaidStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestBoostStrategy::InitNonCombatTriggers(triggers);
    PriestBoostRaidStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestCcStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    PriestCcStrategy::InitCombatTriggers(triggers);

    triggers.push_back(new TriggerNode(
        "silence",
        NextAction::array(0, new NextAction("silence", ACTION_INTERRUPT + 1), NULL)));

    triggers.push_back(new TriggerNode(
        "silence on enemy healer",
        NextAction::array(0, new NextAction("silence on enemy healer", ACTION_INTERRUPT), NULL)));
}

void ShadowPriestCcStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    PriestCcStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestCcPveStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestCcStrategy::InitCombatTriggers(triggers);
    PriestCcPveStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestCcPveStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestCcStrategy::InitNonCombatTriggers(triggers);
    PriestCcPveStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestCcPvpStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestCcStrategy::InitCombatTriggers(triggers);
    PriestCcPvpStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestCcPvpStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestCcStrategy::InitNonCombatTriggers(triggers);
    PriestCcPvpStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestCcRaidStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestCcStrategy::InitCombatTriggers(triggers);
    PriestCcRaidStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestCcRaidStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestCcStrategy::InitNonCombatTriggers(triggers);
    PriestCcRaidStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestCureStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    PriestCureStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestCureStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    PriestCureStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestCurePveStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestCureStrategy::InitCombatTriggers(triggers);
    PriestCurePveStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestCurePveStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestCureStrategy::InitNonCombatTriggers(triggers);
    PriestCurePveStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestCurePvpStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestCureStrategy::InitCombatTriggers(triggers);
    PriestCurePvpStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestCurePvpStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestCureStrategy::InitNonCombatTriggers(triggers);
    PriestCurePvpStrategy::InitNonCombatTriggers(triggers);
}

void ShadowPriestCureRaidStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestCureStrategy::InitCombatTriggers(triggers);
    PriestCureRaidStrategy::InitCombatTriggers(triggers);
}

void ShadowPriestCureRaidStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    ShadowPriestCureStrategy::InitNonCombatTriggers(triggers);
    PriestCureRaidStrategy::InitNonCombatTriggers(triggers);
}

#endif