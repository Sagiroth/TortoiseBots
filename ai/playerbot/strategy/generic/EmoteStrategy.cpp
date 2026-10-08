
#include "playerbot/playerbot.h"
#include "EmoteStrategy.h"
#include "playerbot/PlayerbotAIConfig.h"

using namespace ai;

void EmoteStrategy::InitNonCombatTriggers(std::list<TriggerNode*> &triggers)
{
    // A bot answering to a real player stays quiet: unprompted suggestions and
    // idle talk read as chatter in a party. Replies to emotes and greetings
    // stay, and masterless pool bots keep the ambient life. (mod-playerbots
    // parity: RandomBotEmote/RandomBotTalk default off there.)
    bool quiet = ai && ai->HasRealPlayerMaster();
    if (!quiet)
    {
        triggers.push_back(new TriggerNode(
            "random",
            NextAction::array(0, new NextAction("emote", 0.5f), NULL)));

        triggers.push_back(new TriggerNode(
            "often",
            NextAction::array(0, new NextAction("suggest what to do", 0.5f), NULL)));

        triggers.push_back(new TriggerNode(
            "often",
            NextAction::array(0, new NextAction("suggest trade", 1.0f), NULL)));
    }

    if (sPlayerbotAIConfig.enableGreet)
    {
        triggers.push_back(new TriggerNode(
            "new player nearby",
            NextAction::array(0, new NextAction("greet", 1.0f), NULL)));
    }

    if (!quiet)
    {
        triggers.push_back(new TriggerNode(
            "seldom",
            NextAction::array(0, new NextAction("talk", 0.5f), NULL)));
    }

    triggers.push_back(new TriggerNode(
        "receive text emote",
        NextAction::array(0, new NextAction("emote", 10.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "receive emote",
        NextAction::array(0, new NextAction("emote", 10.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "random",
        NextAction::array(0, new NextAction("mount anim", 1.0f), NULL)));
}
