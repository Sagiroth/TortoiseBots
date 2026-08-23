#pragma once
#include "playerbot/PlayerbotAI.h"

namespace ai
{
	class RememberTaxiAction : public Action {
	public:
		RememberTaxiAction(PlayerbotAI* ai) : Action(ai, "remember taxi") {}

    public:
        virtual bool Execute(Event& event) override;
    };

}