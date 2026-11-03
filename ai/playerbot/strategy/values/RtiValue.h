#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"

namespace ai
{
    class RtiValue : public ManualSetValue<std::string>
	{
	public:
        RtiValue(PlayerbotAI* ai);
        virtual std::string Save() override { return value; }
        virtual bool Load(std::string text) override { value = text; return true; }
    };

    class RtiCcValue : public ManualSetValue<std::string>
    {
    public:
        RtiCcValue(PlayerbotAI* ai);

        virtual std::string Save() override { return value; }
        virtual bool Load(std::string text) override { value = text; return true; }
    };

    // The family an automatic run cc order named ("seduction", "fear":
    // runtime/CcOrderFamily.h). Saved beside "rti cc" so an AI rebuilt mid-hold
    // still fires only that spell; "none" (a hand-written order) gates nothing.
    class RunCcFamilyValue : public ManualSetValue<std::string>
    {
    public:
        RunCcFamilyValue(PlayerbotAI* ai);

        virtual std::string Save() override { return value; }
        virtual bool Load(std::string text) override { value = text; return true; }
    };
}
