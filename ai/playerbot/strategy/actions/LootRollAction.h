#pragma once
#include "playerbot/PlayerbotAI.h"
#include "QueryItemUsageAction.h"
#include "LootAction.h"
#include "GenericActions.h"

namespace ai
{
    class LootStartRollAction : public ChatCommandAction
    {
    public:
        LootStartRollAction(PlayerbotAI* ai, std::string name = "loot start roll") : ChatCommandAction(ai, name) {}
        virtual bool Execute(Event& event) override;
        virtual bool isUsefulWhenStunned() override { return true; }

#ifdef GenerateBotHelp
        virtual std::string GetHelpName() { return "loot start roll"; } //Must equal iternal name
        virtual std::string GetHelpDescription()
        {
            return "This is a WorldPacket action which will stores rollable item to be rolled on later.\n";
        }
        virtual std::vector<std::string> GetUsedActions() { return {}; }
        virtual std::vector<std::string> GetUsedValues() { return { "active rolls" }; }
#endif
    };

    // SMSG_LOOT_ROLL (one per cast vote) reaches every roller, so a pool bot
    // waiting on its vote learns each live human vote as it lands (#967).
    // Every live vote (need/greed/pass) is remembered per voter in "seen
    // need rolls"; any NEED among them steps the bot's own NEED down to
    // GREED at vote time, and the per-voter record tells when every real
    // roller voted. Post-resolution echoes carry a real roll number and are
    // ignored.
    class LootVoteAction : public QueryItemUsageAction
    {
    public:
        LootVoteAction(PlayerbotAI* ai, std::string name = "loot vote") : QueryItemUsageAction(ai, name) {}
        virtual bool Execute(Event& event) override;
        virtual bool isUsefulWhenStunned() override { return true; }

#ifdef GenerateBotHelp
        virtual std::string GetHelpName() { return "loot vote"; } //Must equal internal name
        virtual std::string GetHelpDescription()
        {
            return "This action records a group-loot vote another roller just cast.\n";
        }
        virtual std::vector<std::string> GetUsedActions() { return {}; }
        virtual std::vector<std::string> GetUsedValues() { return { "seen need rolls" }; }
#endif
    };
    class RollAction : public QueryItemUsageAction
    {
    public:
        RollAction(PlayerbotAI* ai, std::string name = "roll") : QueryItemUsageAction(ai, name) {}
        virtual bool Execute(Event& event) override;

#ifdef GenerateBotHelp
        virtual std::string GetHelpName() { return "roll"; } //Must equal internal name
        virtual std::string GetHelpDescription()
        {
            return "This will make the bot roll a certain way on a specific item.\n"
                "Usage: roll <roll type> [itemlink].\n"
                "Types are need, greed, pass, emote and auto.\n"
                "When no itemlink is provided bots will roll on all current rollable loot.\n"
                "Examples:\n"
                "roll greed [item]\n"
                "roll auto\n"
                "See also strategies [h:strategy|roll] and [h:strategy|delayed roll].\n";
        }
        virtual std::vector<std::string> GetUsedActions() { return {}; }
        virtual std::vector<std::string> GetUsedValues() { return { "item usage", "force item usage"}; }
#endif

    protected:
        virtual ItemQualifier GetRollItem(ObjectGuid lootGuid, uint32 slot);
        virtual RollVote CalculateRollVote(ItemQualifier& itemQualifier, RollVote* finalVote = nullptr, std::string* reasonOut = nullptr, bool log = true);
        virtual bool RollOnItemInSlot(RollVote type, ObjectGuid lootGuid, uint32 slot);
        bool RollWithEtiquette(ObjectGuid lootGuid, uint32 slot, ObjectGuid seenVoter = ObjectGuid(), uint8 seenVote = ROLL_NOT_VALID);
        bool RealPlayerMayNeed(ObjectGuid lootGuid, ItemQualifier& itemQualifier, ObjectGuid seenVoter, uint8 seenVote);
        bool ShouldWaitForPlayers(ObjectGuid lootGuid, uint32 slot);
        bool AllRealRollersVoted(ObjectGuid lootGuid, uint32 slot);
        Player* FindBetterFittingMember(ObjectGuid lootGuid, uint32 slot, ItemQualifier& itemQualifier, ObjectGuid seenVoter = ObjectGuid(), uint8 seenVote = ROLL_NOT_VALID);
    };

    class LootRollAction : public RollAction
    {
    public:
        LootRollAction(PlayerbotAI* ai, std::string name = "loot roll") : RollAction(ai, name) {}
        virtual bool Execute(Event& event) override;
        virtual bool isUsefulWhenStunned() override { return true; }

#ifdef GenerateBotHelp
        virtual std::string GetHelpName() { return "loot roll"; } //Must equal internal name
        virtual std::string GetHelpDescription()
        {
            return "This action will make the bot roll on an item the master just rolled on.\n"
                "The type of roll will be automatically determined based on if the bot finds the item useful.";
        }
        virtual std::vector<std::string> GetUsedActions() { return { "roll" }; }
        virtual std::vector<std::string> GetUsedValues() { return { "item usage", "force item usage" }; }
#endif
    };
    class RollWonLogAction : public Action
    {
    public:
        RollWonLogAction(PlayerbotAI* ai, std::string name = "log roll won") : Action(ai, name) {}
        virtual bool Execute(Event& event) override;
        virtual bool isUsefulWhenStunned() override { return true; }

#ifdef GenerateBotHelp
        virtual std::string GetHelpName() { return "log roll won"; } //Must equal internal name
        virtual std::string GetHelpDescription()
        {
            return "This action logs an item the bot won from a group-loot roll.\n";
        }
        virtual std::vector<std::string> GetUsedActions() { return {}; }
        virtual std::vector<std::string> GetUsedValues() { return {}; }
#endif
    };

    class AutoLootRollAction : public RollAction
    {
    public:
        AutoLootRollAction(PlayerbotAI* ai, std::string name = "auto loot roll") : RollAction(ai, name) {}
        virtual bool Execute(Event& event) override;
        virtual bool isPossible() override;

#ifdef GenerateBotHelp
        virtual std::string GetHelpName() { return "auto loot roll"; } //Must equal internal name
        virtual std::string GetHelpDescription()
        {
            return "This action will make the bot roll on one item that it hasn't rolled for yet.\n"
                "The type of roll will be automatically determined based on if the bot finds the item useful.";
        }
        virtual std::vector<std::string> GetUsedActions() { return { "roll" }; }
        virtual std::vector<std::string> GetUsedValues() { return { "item usage", "force item usage" }; }
#endif
    };
}
