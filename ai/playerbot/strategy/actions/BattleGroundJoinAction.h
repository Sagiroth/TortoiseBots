#pragma once

#include "playerbot/PlayerbotAI.h"

namespace ai
{
    // The core owns battleground queues and invitations. These actions only
    // interpret the normal Vanilla client status flow for a headless player.
    class BGLeaveAction : public Action
    {
    public:
        BGLeaveAction(PlayerbotAI* ai, std::string name = "bg leave") : Action(ai, name) {}
        bool Execute(Event& event) override;
    };

    class BGStatusAction : public Action
    {
    public:
        BGStatusAction(PlayerbotAI* ai) : Action(ai, "bg status") {}
        bool Execute(Event& event) override;
        bool isUseful() override;
    };

    class BGStatusCheckAction : public Action
    {
    public:
        BGStatusCheckAction(PlayerbotAI* ai, std::string name = "bg status check") : Action(ai, name) {}
        bool Execute(Event& event) override;
        bool isUseful() override;
    };
}
