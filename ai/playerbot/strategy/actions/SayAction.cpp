#include "playerbot/playerbot.h"
#include "SayAction.h"

using namespace ai;

bool SpeakAction::Execute(Event& event)
{
    bool botsTalkLikePlayers = true;

    std::string text = event.GetParam();
    if (text.find("/y ") == 0)
        ai->Yell(text.substr(3), botsTalkLikePlayers);
    else if (text.find("/p ") == 0)
        ai->SayToParty(text.substr(3), botsTalkLikePlayers);
    else if (text.find("/r ") == 0)
        ai->SayToRaid(text.substr(3));
    else if (text.find("/g ") == 0)
        ai->SayToGuild(text.substr(3), botsTalkLikePlayers);
    else if (text.find("/s ") == 0)
        ai->Say(text.substr(3), botsTalkLikePlayers);
    else if (text.find("/1 ") == 0)
        ai->SayToGeneral(text.substr(3));
    else if (text.find("/2 ") == 0)
        ai->SayToTrade(text.substr(3));
    else if (text.find("/3 ") == 0)
        ai->SayToLocalDefense(text.substr(3));
    else if (text.find("/4 ") == 0)
        ai->SayToLFG(text.substr(3));
    else
        ai->Say(text, botsTalkLikePlayers);

    return true;
}
