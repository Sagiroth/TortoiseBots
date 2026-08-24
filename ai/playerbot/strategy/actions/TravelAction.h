#pragma once
#include "playerbot/PlayerbotAI.h"

#include "playerbot/strategy/Action.h"
#include "MovementActions.h"
#include "playerbot/strategy/values/LastMovementValue.h"

namespace ai
{
    class TravelAction : public MovementAction {
    public:
        TravelAction(PlayerbotAI* ai) : MovementAction(ai, "travel") {}

        virtual bool Execute(Event& event) override;
        virtual bool isUseful() override;
    };

}
