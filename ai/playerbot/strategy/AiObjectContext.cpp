#include "playerbot/playerbot.h"
#include "AiObjectContext.h"
#include "runtime/VerticalSliceObjects.h"

using namespace ai;

AiObjectContext::AiObjectContext(PlayerbotAI* ai) : PlayerbotAIAware(ai)
{
    strategyContexts.Add(new VerticalStrategyContext());
    actionContexts.Add(new VerticalActionContext());
    triggerContexts.Add(new VerticalTriggerContext());
    valueContexts.Add(new VerticalValueContext());
}

void AiObjectContext::ClearValues(std::string findName)
{
    for (std::string const& name : valueContexts.GetCreated())
        if (findName.empty() || name.find(findName) != std::string::npos)
            valueContexts.Erase(name);
}

void AiObjectContext::ClearExpiredValues(std::string findName, uint32 interval)
{
    std::vector<std::string> expired;
    for (std::string const& name : valueContexts.GetCreated())
    {
        UntypedValue* value = GetUntypedValue(name);
        if (value && !value->Protected() && (findName.empty() || name.find(findName) != std::string::npos) &&
            ((!interval && value->Expired()) || (interval && value->Expired(interval))))
            expired.push_back(name);
    }
    for (std::string const& name : expired) valueContexts.Erase(name);
}

std::string AiObjectContext::FormatValues(std::string findName)
{
    std::ostringstream out;
    bool first = true;
    for (std::string const& name : valueContexts.GetCreated())
    {
        UntypedValue* value = GetUntypedValue(name);
        if (!value || (!findName.empty() && name.find(findName) == std::string::npos)) continue;
        std::string text = value->Format();
        if (text == "?") continue;
        if (!first) out << "|";
        out << "{" << name << "=" << text << "}";
        first = false;
    }
    return out.str();
}

void AiObjectContext::Update() {}

void AiObjectContext::Reset()
{
    strategyContexts.Reset();
    triggerContexts.Reset();
    actionContexts.Reset();
    valueContexts.Reset();
}

std::list<std::string> AiObjectContext::Save() { return {}; }
void AiObjectContext::Load(std::list<std::string>) {}
