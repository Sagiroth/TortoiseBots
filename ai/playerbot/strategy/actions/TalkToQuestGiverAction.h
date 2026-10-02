#pragma once
#include "playerbot/PlayerbotAI.h"

#include "playerbot/strategy/Action.h"
#include "QuestAction.h"

namespace ai
{
    class TalkToQuestGiverAction : public QuestAction
    {
    public:
        TalkToQuestGiverAction(PlayerbotAI* ai) : QuestAction(ai, "talk to quest giver") {}

        // Hand a finished quest in without a real talk: the stuck-hand-in fallback
        // in MoveToTravelTargetAction. Runs the same reward choice and the same
        // core Player::RewardQuest call a talk-in does; only the distance/facing
        // gates of ProcessQuests and the TalkToQuestGiverAction event are skipped
        // (the caller owns that event and writes QuestAutoHandIn instead).
        // Returns true only when the quest ended up rewarded.
        static bool RewardFinishedQuest(PlayerbotAI* ai, Quest const* quest, WorldObject* questGiver);

    protected:
        virtual bool ProcessQuest(Player* requester, Quest const* quest, WorldObject* questGiver) override;

    private:
        bool TurnInQuest(Player* requester, Quest const* quest, WorldObject* questGiver, std::string& out, bool autoHandIn = false);
        void RewardNoItem(Quest const* quest, WorldObject* questGiver, std::string& out);
        void RewardSingleItem(Quest const* quest, WorldObject* questGiver, std::string& out);
        std::set<uint32> BestRewards(Quest const* quest);
        void RewardMultipleItem(Player* requester, Quest const* quest, WorldObject* questGiver, std::string& out);
        void AskToSelectReward(Player* requester, Quest const* quest, std::string& out, bool forEquip);
    };
}