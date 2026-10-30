#pragma once
#include "playerbot/PlayerbotAI.h"
#include "GenericActions.h"

namespace ai
{
    // Queues a guild-order delivery for the world thread. Runs only while the
    // stored order is Delivering and a guild banker is within interaction
    // range; the world-thread drain does the deposit and the mail. Once
    // repeated banker searches offer nothing, a mailbox does instead: the
    // drain mails the guild master from there.
    class GuildBankAction : public Action
    {
    public:
        GuildBankAction(PlayerbotAI* ai) : Action(ai, "guild bank") {}
        virtual bool Execute(Event& event) override;
        virtual bool isUseful() override;

    private:
        Unit* FindNearbyGuildBanker();
        bool MailLegActive() const;
    };
}
