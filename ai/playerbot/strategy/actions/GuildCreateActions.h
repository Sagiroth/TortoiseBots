#pragma once
#include "playerbot/PlayerbotAI.h"
#include "ChooseTravelTargetAction.h"
#include "playerbot/strategy/values/BudgetValues.h"
#include "playerbot/ServerFacade.h"
#include "runtime/BotActivityLease.h"

namespace ai
{
    class TravelTarget;

    class BuyPetitionAction : public Action
    {
    public:
        BuyPetitionAction(PlayerbotAI* ai) : Action(ai, "buy petition") {}
        virtual bool Execute(Event& event) override;
        virtual bool isUseful() override;
        static bool canBuyPetition(Player* bot);
    };

    class PetitionOfferAction : public Action
    {
    public:
        PetitionOfferAction(PlayerbotAI* ai, std::string name = "petition offer") : Action(ai, name) {}
        virtual bool Execute(Event& event) override;
        virtual bool isUseful() override
        {
            if (!sPlayerbotAIConfig.randomBotFormGuild || bot->GetGuildId())
                return false;
            // Lease gate (issue #89): never run guild errands while queued
            // for LFT/BG or trading at the AH. Idle/Grinding/PlayerMaster only.
            TortoiseBots::BotActivity activity = TortoiseBots::BotActivityLeaseManager::Instance().GetActivity(bot->GetGUIDLow());
            return activity == TortoiseBots::BotActivity::Idle || activity == TortoiseBots::BotActivity::Grinding ||
                activity == TortoiseBots::BotActivity::PlayerMaster;
        }
    };

    class PetitionOfferNearbyAction : public PetitionOfferAction
    {
    public:
        PetitionOfferNearbyAction(PlayerbotAI* ai) : PetitionOfferAction(ai, "petition offer nearby") {}
        virtual bool Execute(Event& event) override;
        virtual bool isUseful() override
        {
            if (!sPlayerbotAIConfig.randomBotFormGuild || bot->GetGuildId() ||
                !AI_VALUE2(uint32, "item count", chat->formatQItem(5863)) ||
                AI_VALUE(uint8, "petition signs") >= sWorld.getConfig(CONFIG_UINT32_MIN_PETITION_SIGNS))
                return false;
            TortoiseBots::BotActivity activity = TortoiseBots::BotActivityLeaseManager::Instance().GetActivity(bot->GetGUIDLow());
            return activity == TortoiseBots::BotActivity::Idle || activity == TortoiseBots::BotActivity::Grinding ||
                activity == TortoiseBots::BotActivity::PlayerMaster;
        }
    };

    class PetitionTurnInAction : public ChooseTravelTargetAction
    {
    public:
        PetitionTurnInAction(PlayerbotAI* ai) : ChooseTravelTargetAction(ai, "turn in petition") {}
        virtual bool Execute(Event& event) override;
        virtual bool isUseful() override;
    };

    class BuyTabardAction : public ChooseTravelTargetAction
    {
    public:
        BuyTabardAction(PlayerbotAI* ai) : ChooseTravelTargetAction(ai, "buy tabard") {}
        virtual bool Execute(Event& event) override;
        virtual bool isUseful() override;
    };
}
