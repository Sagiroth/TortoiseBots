#pragma once
#include "playerbot/PlayerbotAI.h"
#include "GenericActions.h"

namespace ai
{
    class MailProcessor
    {
    public:
        virtual bool Before(Player* requester, PlayerbotAI* ai) { return true; }
        virtual bool Process(Player* requester, int index, Mail* mail, PlayerbotAI* ai, Event& event) = 0;
        virtual bool After(Player* requester, PlayerbotAI* ai) { return true; }

    public:
        static ObjectGuid FindMailbox(PlayerbotAI* ai);

    protected:
        void RemoveMail(Player* bot, uint32 id, ObjectGuid mailbox);
    };

    // Takes one order item id from waiting mail, fitting that single item
    // instead of demanding free slots for the whole mailbox. Returns how
    // many items left the mail for the bags. Implemented in MailAction.cpp;
    // the take-mail order action owns the call.
    uint32_t TakeOrderMail(PlayerbotAI* ai, uint32_t itemId);

    // Settles the guild relist ledger against the waiting auction mail: a
    // sale pays its gold into the broker's hands, an expired listing returns
    // the stack, and the ledger row waits for the bank trip either way.
    // Reads the mailbox at any order state (or with no order at all).
    // Returns how many ledger rows this read matched. Implemented in
    // MailAction.cpp; the mail take path and the order mail action call it.
    uint32_t TakeRelistMail(PlayerbotAI* ai);

    class MailAction : public ChatCommandAction
    {
    public:
        MailAction(PlayerbotAI* ai) : ChatCommandAction(ai, "mail") {}
        virtual bool Execute(Event& event) override;

    private:
        bool CheckMailbox();

    private:
        static std::map<std::string, MailProcessor*> processors;
    };

}
