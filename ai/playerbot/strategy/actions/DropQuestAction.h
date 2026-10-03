#pragma once
#include "playerbot/PlayerbotAI.h"
#include "GenericActions.h"

namespace ai
{
    class DropQuestAction : public ChatCommandAction
    {
    public:
        DropQuestAction(PlayerbotAI* ai) : ChatCommandAction(ai, "drop quest") {}
        virtual bool Execute(Event& event) override;
        virtual bool isUsefulWhenStunned() override { return true; }

#ifdef GenerateBotHelp
        virtual std::string GetHelpName() { return "drop quest"; }
        virtual std::string GetHelpDescription()
        {
            return "This command makes the bot drop a specific quest.\n"
                   "Usage: drop [quest_name or link]\n"
                   "Example: drop [The Missing Diplomat] (drops the specified quest)";
        }
        virtual std::vector<std::string> GetUsedActions() { return {}; }
        virtual std::vector<std::string> GetUsedValues() { return {}; }
#endif
    };

    class CleanQuestLogAction : public ChatCommandAction
    {
    public:
        CleanQuestLogAction(PlayerbotAI* ai) : ChatCommandAction(ai, "clean quest log") {}
        virtual bool Execute(Event& event) override;
        virtual bool isUsefulWhenStunned() override { return true; }

        virtual bool isUseful() override { return !ai->HasActivePlayerMaster(); }

    private:
        bool IsUpkeepBot();
        bool IsGreyIncomplete(Quest const* quest);
        bool HasRequiredDeliverItems(Quest const* quest);
        bool IsDroppable(Quest const* quest, QuestStatus status, bool upkeep, bool triage);
        void NoteCleanNoOp();

#ifdef GenerateBotHelp
        virtual std::string GetHelpName() { return "clean quest log"; }
        virtual std::string GetHelpDescription()
        {
            return "This command removes only safe stale quests to make room for new ones.\n"
                   "Incomplete quests at least eight levels below the bot are eligible; finished quests that can no longer be rewarded (except transient money blockers) are eligible for masterless random bots too; class quests are preserved, incomplete delivery quests with their items intact are preserved. When the log is nearly full (fewer than two free slots) failed quests and unfinishable solo picks (over-level, elite/dungeon/raid, group-suggested, wrong zone) are dropped too.\n";
        }
        virtual std::vector<std::string> GetUsedActions() { return {}; }
        virtual std::vector<std::string> GetUsedValues() { return {}; }
#endif

    };
}
