#pragma once
#include "Action.h"
#include "Multiplier.h"
#include "Trigger.h"
#include "NamedObjectContext.h"
#include "playerbot/BotState.h"

namespace ai
{
	class ActionNode;
	class NextAction;

	enum StrategyType
	{
		STRATEGY_TYPE_GENERIC = 0,
		STRATEGY_TYPE_COMBAT = 1,
		STRATEGY_TYPE_NONCOMBAT = 2,
		STRATEGY_TYPE_TANK = 4,
		STRATEGY_TYPE_DPS = 8,
		STRATEGY_TYPE_HEAL = 16,
		STRATEGY_TYPE_RANGED = 32,
		STRATEGY_TYPE_MELEE = 64,
		STRATEGY_TYPE_REACTION = 128
	};

	enum ActionPriority
	{
	    ACTION_IDLE = 1,
	    ACTION_DEFAULT = 5,
	    ACTION_NORMAL = 10,
	    ACTION_HIGH = 20,
	    ACTION_MOVE = 30,
	    ACTION_INTERRUPT = 40,
	    ACTION_DISPEL = 50,
	    ACTION_LIGHT_HEAL = 60,
	    ACTION_MEDIUM_HEAL = 70,
	    ACTION_CRITICAL_HEAL = 80,
	    ACTION_EMERGENCY = 90,
		ACTION_PASSTROUGH = 100
	};

    class Strategy : public PlayerbotAIAware
    {
    public:
        Strategy(PlayerbotAI* ai);
        virtual ~Strategy() {}

	public:
        void InitTriggers(std::list<TriggerNode*> &triggers, BotState state);
        void InitMultipliers(std::list<Multiplier*> &multipliers, BotState state);

		virtual NextAction** getDefaultActions(BotState state);
		// Compatibility surface for the modern vector-based strategy sources.
		// The engine consumes both representations during the transition so
		// existing Tortoise strategies and forward-ported class strategies can
		// coexist without weakening either implementation.
		virtual std::vector<NextAction> getDefaultActions() { return {}; }
		virtual void InitTriggers(std::vector<TriggerNode*>&) {}
		virtual void InitMultipliers(std::vector<Multiplier*>&) {}
		virtual int GetType() { return STRATEGY_TYPE_GENERIC; }
		virtual uint32 GetType() const { return STRATEGY_TYPE_GENERIC; }
        virtual ActionNode* GetAction(std::string name);
		virtual std::string getName() = 0;
		std::string GetName() { return getName(); }
        void Update() {} //Nonfunctional see AiObjectContext::Update() to enable.
        void Reset() {}

		virtual void OnStrategyAdded(BotState state) {}
		virtual void OnStrategyRemoved(BotState state) {}
#ifdef GenerateBotHelp
		virtual std::string GetHelpName() { return "dummy"; } //Must equal iternal name
		virtual std::string GetHelpDescription() { return "This is a strategy."; }
		virtual std::vector<std::string> GetRelatedStrategies() { return {}; }
#endif
	protected:
		virtual NextAction** GetDefaultCombatActions() { return nullptr; }
		virtual NextAction** GetDefaultNonCombatActions() { return nullptr; }
		virtual NextAction** GetDefaultDeadActions() { return nullptr; }
		virtual NextAction** GetDefaultReactionActions() { return nullptr; }

		virtual void InitCombatTriggers(std::list<TriggerNode*>& triggers) {}
		virtual void InitNonCombatTriggers(std::list<TriggerNode*>& triggers) {}
		virtual void InitDeadTriggers(std::list<TriggerNode*>& triggers) {}
		virtual void InitReactionTriggers(std::list<TriggerNode*>& triggers) {}

		virtual void InitCombatMultipliers(std::list<Multiplier*>& multipliers) {}
		virtual void InitNonCombatMultipliers(std::list<Multiplier*>& multipliers) {}
		virtual void InitDeadMultipliers(std::list<Multiplier*>& multipliers) {}
		virtual void InitReactionMultipliers(std::list<Multiplier*>& multipliers) {}

    protected:
        NamedObjectFactoryList<ActionNode> actionNodeFactories;
    };
}

// The class-strategy forward ports follow the donor's global-name convention,
// while the original Tortoise strategy core keeps its types in namespace ai.
// These aliases are intentionally confined to the PlayerBots module headers.
using ai::Action;
using ai::ActionNode;
using ai::Multiplier;
using ai::NamedObjectContext;
using ai::NextAction;
using ai::Strategy;
using ai::StrategyType;
using ai::Trigger;
using ai::TriggerNode;

using ai::NamedObjectFactory;
using ai::NamedObjectFactoryList;

using ai::ACTION_DEFAULT;
using ai::ACTION_IDLE;
using ai::ACTION_NORMAL;
using ai::ACTION_HIGH;
using ai::ACTION_MOVE;
using ai::ACTION_INTERRUPT;
using ai::ACTION_DISPEL;
using ai::ACTION_LIGHT_HEAL;
using ai::ACTION_MEDIUM_HEAL;
using ai::ACTION_CRITICAL_HEAL;
using ai::ACTION_EMERGENCY;
using ai::ACTION_PASSTROUGH;
using ai::STRATEGY_TYPE_GENERIC;
using ai::STRATEGY_TYPE_COMBAT;
using ai::STRATEGY_TYPE_NONCOMBAT;
using ai::STRATEGY_TYPE_TANK;
using ai::STRATEGY_TYPE_DPS;
using ai::STRATEGY_TYPE_HEAL;
using ai::STRATEGY_TYPE_RANGED;
using ai::STRATEGY_TYPE_MELEE;
using ai::STRATEGY_TYPE_REACTION;
