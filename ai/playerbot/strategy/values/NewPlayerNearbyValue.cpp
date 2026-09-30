
#include "playerbot/playerbot.h"
#include "NewPlayerNearbyValue.h"
#include "runtime/BotManager.h"

using namespace ai;

ObjectGuid NewPlayerNearbyValue::Calculate()
{
    std::list<ObjectGuid> players = ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid> >("nearest friendly players")->Get();
    std::set<ObjectGuid>& alreadySeenPlayers = ai->GetAiObjectContext()->GetValue<std::set<ObjectGuid>& >("already seen players")->Get();
    for (std::list<ObjectGuid>::iterator i = players.begin(); i != players.end(); ++i)
    {
        ObjectGuid guid = *i;

        // "New player" means a real player. Greeting is the only consumer of
        // this value, and a pool of hundreds of bots sharing a starting zone
        // would otherwise greet each other on every encounter - text emotes no
        // human reads, and the greet action marks every nearby bot as seen in
        // one pass, so the trigger fires constantly while the pool churns.
        if (TortoiseBots::BotManager::Instance().IsBot(guid))
            continue;

        if (alreadySeenPlayers.find(guid) == alreadySeenPlayers.end())
            return guid;
    }

    return ObjectGuid();
}
