#pragma once

#include "playerbot/PlayerbotAIAware.h"
#include "Action.h"
#include "Value.h"
#include "NamedObjectContext.h"
#include "Strategy.h"
#include <set>

namespace ai
{
    class UntypedValue;
    template<class T> class Value;
}

namespace ai
{
    class AiObjectContext : public PlayerbotAIAware
    {
    public:
        AiObjectContext(PlayerbotAI* ai);
        virtual ~AiObjectContext() {}

    public:
        virtual Strategy* GetStrategy(const std::string& name) { return strategyContexts.GetObject(name, ai); }
        virtual std::set<std::string> GetSiblingStrategy(const std::string& name) { return strategyContexts.GetSiblings(name); }
        virtual Trigger* GetTrigger(const std::string& name) { return triggerContexts.GetObject(name, ai); }
        virtual Action* GetAction(const std::string& name) { return actionContexts.GetObject(name, ai); }
        virtual UntypedValue* GetUntypedValue(const std::string& name) { return valueContexts.GetObject(name, ai); }

        template<class T>
        Value<T>* GetValue(const std::string& name)
        {
            return dynamic_cast<Value<T>*>(GetUntypedValue(name));
        }

        template<class T>
        Value<T>* GetValue(const std::string& name, const std::string& param)
        {
            return GetValue<T>((std::string(name) + "::" + param));
        }

        template<class T>
        Value<T>* GetValue(const std::string& name, int32 param)
        {
        	std::ostringstream out; out << param;
            return GetValue<T>(name, out.str());
        }

        bool HasValue(const std::string& name)
        {
            return valueContexts.IsCreated(name);
        }

        bool HasValue(const std::string& name, const std::string& param)
        {
            return HasValue((std::string(name) + "::" + param));
        }

        bool HasValue(const std::string& name, int32 param)
        {
            std::ostringstream out; out << param;
            return HasValue(name, out.str());
        }


        std::set<std::string> GetValues()
        {
            return valueContexts.GetCreated();
        }

        void GetSupportedStrategies(std::set<std::string>& strategies)
        {
            return strategyContexts.GetSupportedKeys(strategies);
        }

        void GetSupportedTriggers(std::set<std::string>& triggers)
        {
            return strategyContexts.GetSupportedKeys(triggers);
        }

        void GetSupportedActions(std::set<std::string>& actions)
        {
            return actionContexts.GetSupportedKeys(actions);
        }

        void GetSupportedValues(std::set<std::string>& values)
        {
            return valueContexts.GetSupportedKeys(values);
        }

        void ClearValues(std::string findName = "");

        void ClearExpiredValues(std::string findName = "", uint32 interval = 0);

        std::string FormatValues(std::string findName = "");

    public:
        virtual void Update();
        virtual void Reset();
        virtual void AddShared(NamedObjectContext<UntypedValue>* sharedValues)
        {
            valueContexts.Add(sharedValues);
        }
        std::list<std::string> Save();
        void Load(std::list<std::string> data);

        std::vector<std::string> performanceStack;
    protected:
        NamedObjectContextList<Strategy> strategyContexts;
        NamedObjectContextList<Action> actionContexts;
        NamedObjectContextList<Trigger> triggerContexts;
        NamedObjectContextList<UntypedValue> valueContexts;
    };
}


#include "ValueMacros.h"
