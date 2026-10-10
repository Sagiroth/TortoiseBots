#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/Action.h"

namespace ai
{
    class ReadyCheckAction : public Action
    {
    public:
        ReadyCheckAction(PlayerbotAI* ai, std::string name = "ready check") : Action(ai, name) {}
        virtual bool Execute(Event& event) override;

    protected:
        bool ReadyCheck(Player* requester);
        void ReportReadiness(Player* requester);
        void SendReadyConfirm();
    };

    class FinishReadyCheckAction : public ReadyCheckAction
    {
    public:
        FinishReadyCheckAction(PlayerbotAI* ai) : ReadyCheckAction(ai, "finish ready check") {}
        virtual bool Execute(Event& event) override;
    };

    // Deferred ready-check confirm (SOC-S5): fires from the
    // "force rebuff pending" trigger once buffs settle or the cap hits.
    // Extends ReadyCheckAction for the confirm helper only.
    class ReadyReplyAction : public ReadyCheckAction
    {
    public:
        ReadyReplyAction(PlayerbotAI* ai) : ReadyCheckAction(ai, "ready reply") {}
        virtual bool Execute(Event& event) override;
        virtual bool isUseful() override;
    };
}
