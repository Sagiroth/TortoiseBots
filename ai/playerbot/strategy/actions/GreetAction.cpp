
#include "playerbot/playerbot.h"
#include "GreetAction.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
using namespace ai;

GreetAction::GreetAction(PlayerbotAI* ai) : Action(ai, "greet")
{
}

bool GreetAction::Execute(Event& event)
{
    ObjectGuid guid = AI_VALUE(ObjectGuid, "new player nearby");
    if (!guid || !guid.IsPlayer()) return false;

    Player* player = dynamic_cast<Player*>(ai->GetUnit(guid));
    if (!player) return false;

    std::set<ObjectGuid>& alreadySeenPlayers = ai->GetAiObjectContext()->GetValue<std::set<ObjectGuid>& >("already seen players")->Get();
    std::list<ObjectGuid> nearestPlayers = ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid> >("nearest friendly players")->Get();

    // "Already seen" is an encounter memory, not a permanent ignore list: only
    // players still in sight stay in it, so someone who leaves and comes back
    // counts as a new meeting (subject to the greeting cooldown below). The set
    // therefore never grows past the players actually around the bot.
    for (std::set<ObjectGuid>::iterator i = alreadySeenPlayers.begin(); i != alreadySeenPlayers.end();)
    {
        if (std::find(nearestPlayers.begin(), nearestPlayers.end(), *i) == nearestPlayers.end())
            i = alreadySeenPlayers.erase(i);
        else
            ++i;
    }

    time_t now = time(0);

    // Greetings belong to a bot's own player. A masterless pool bot greeting
    // every real player it passes is opt-in (AiPlayerbot.RandomBotGreet):
    // hundreds of pool bots share one starting zone, so by default they greet
    // nobody and a visitor is not answered by a wall of waves.
    if (!ai->HasRealPlayerMaster() && !sPlayerbotAIConfig.randomBotGreet)
    {
        alreadySeenPlayers.insert(guid);
        return false;
    }

    // One greeting per player per cooldown window. Without it a bot standing
    // next to a busy road greets the same player again on every pass.
    std::map<ObjectGuid, time_t>::iterator lastGreet = greetTimes.find(guid);
    if (lastGreet != greetTimes.end() && now - lastGreet->second < sPlayerbotAIConfig.greetCooldown)
    {
        alreadySeenPlayers.insert(guid);
        return false;
    }

    // Entries can only ever be older than the cooldown, so this keeps the map
    // bounded by the number of players met in one window.
    for (std::map<ObjectGuid, time_t>::iterator i = greetTimes.begin(); i != greetTimes.end();)
    {
        if (now - i->second >= sPlayerbotAIConfig.greetCooldown)
            i = greetTimes.erase(i);
        else
            ++i;
    }
    greetTimes[guid] = now;

    if (!sServerFacade.isInFront(bot, player, sPlayerbotAIConfig.sightDistance, CAST_ANGLE_IN_FRONT))
        sServerFacade.SetFacingTo(bot, player);

    ObjectGuid oldSel = bot->GetSelectionGuid();
    bot->SetSelectionGuid(guid);
    //bot->HandleEmote(EMOTE_ONESHOT_WAVE);
    ai->PlayEmote(TEXTEMOTE_HELLO);
    bot->SetSelectionGuid(oldSel);

    alreadySeenPlayers.insert(guid);

    for (std::list<ObjectGuid>::iterator i = nearestPlayers.begin(); i != nearestPlayers.end(); ++i) {
        alreadySeenPlayers.insert(*i);
    }

    sPlayerbotAIConfig.logEvent(ai, "GreetAction", player->GetName());
    return true;
}
