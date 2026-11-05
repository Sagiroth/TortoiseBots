#pragma once
#include "playerbot/PlayerbotAI.h"

#include "playerbot/strategy/Action.h"
#include "QuestAction.h"
#include "../../../../runtime/GuildOrderDisenchantPolicy.h"

class MasterPlayer;

namespace ai
{
    class CheckMailAction : public Action
    {
    public:
        CheckMailAction(PlayerbotAI* ai) : Action(ai, "check mail") {}

#ifdef GenerateBotHelp
        virtual std::string GetHelpName() { return "check mail"; }
        virtual std::string GetHelpDescription()
        {
            return "This action makes the bot automatically check its mailbox when not in combat or battlegrounds.\n"
                   "Items from guild members are taken into the bags; any other items are returned to sender unless they were specifically requested.\n"
                   "The bot only checks mail when it has no active master controlling it.";
        }
        virtual std::vector<std::string> GetUsedActions() { return {}; }
        virtual std::vector<std::string> GetUsedValues() { return {}; }
#endif
        virtual bool Execute(Event& event) override;
        virtual bool isUseful() override;

    private:
        void ProcessMail(Mail* mail, Player* owner);
        static Player* GetProcessableOwner(Mail* mail, Player* bot);
        static bool IsGuildMail(Mail* mail, Player* bot);
        void TakeGuildMail(Mail* mail);
        bool CanTakeGuildMail(Mail* mail, MasterPlayer* master);
        void QueueDisenchantMail(Mail* mail);
    };
}
