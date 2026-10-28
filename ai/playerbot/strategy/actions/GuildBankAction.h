#pragma once
#include "playerbot/PlayerbotAI.h"
#include "GenericActions.h"

namespace ai
{
    // Delivers a finished guild order: deposits the ordered items into the
    // guild bank at a nearby guild banker, or mails them to the guild master
    // when the bank refuses every tab. Runs while the stored order is
    // Delivering; only the ordered item moves, never anything else.
    class GuildBankAction : public Action
    {
    public:
        GuildBankAction(PlayerbotAI* ai) : Action(ai, "guild bank") {}
        virtual bool Execute(Event& event) override;
        virtual bool isUseful() override;

    private:
        Unit* FindNearbyGuildBanker();
        bool DepositAtBanker(Unit* banker, uint32 itemId);
        void MailToLeader(uint32 itemId);
    };
}
