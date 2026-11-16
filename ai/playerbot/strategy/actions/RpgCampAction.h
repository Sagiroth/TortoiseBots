#pragma once

#include "MovementActions.h"

namespace ai
{
    class RpgCampAction : public MovementAction
    {
    public:
        RpgCampAction(PlayerbotAI* ai, std::string name = "rpg camp") : MovementAction(ai, name) {}

        virtual bool Execute(Event& event) override;
        virtual bool isUseful() override;
    };
}
