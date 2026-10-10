#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"

namespace ai
{
    class RangeValue : public ManualSetValue<float>, public Qualified
	{
	public:
        RangeValue(PlayerbotAI* ai);
        virtual std::string Save() override;
        virtual bool Load(std::string value) override;
    };

    // Opt-in combat spread knob (donor "disperse distance" shape):
    // unset (-1, default) keeps the role default; a set value (> 0) is the
    // "too close" radius in yards. Persisted like RangeValue.
    class SpreadDistanceValue : public ManualSetValue<float>, public Qualified
	{
	public:
        SpreadDistanceValue(PlayerbotAI* ai);
        virtual std::string Save() override;
        virtual bool Load(std::string value) override;
    };
}
