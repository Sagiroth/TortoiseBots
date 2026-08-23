
#include "playerbot/playerbot.h"
#include "ChangeStrategyAction.h"
#include "playerbot/PlayerbotAIConfig.h"

using namespace ai;

bool ChangeCombatStrategyAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    std::string text = event.GetParam();
    text = text.empty() ? getName() : text;

    ai->ChangeStrategy(text, BotState::BOT_STATE_COMBAT);

    if (!sPlayerbotAIConfig.bExplicitDbStoreSave)
    {
       if (event.GetSource() == "co")
       {
          std::vector<std::string> splitted = split(text, ',');
          CharacterDatabase.BeginTransaction();
          for (std::vector<std::string>::iterator i = splitted.begin(); i != splitted.end(); i++)
          {
             const char* name = i->c_str();
             switch (name[0])
             {
             case '+':
             case '-':
             case '~':
                sPlayerbotDbStore.Save(ai);
                break;
             }
          }
          CharacterDatabase.CommitTransaction();
       }
    }

    if (text.find("?") != std::string::npos)
    {
        ai->PrintStrategies(requester, BotState::BOT_STATE_COMBAT);
    }
    
    return true;
}

bool ChangeNonCombatStrategyAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    std::string text = event.GetParam();
    text = text.empty() ? getName() : text;

    ai->ChangeStrategy(text, BotState::BOT_STATE_NON_COMBAT);

    if (!sPlayerbotAIConfig.bExplicitDbStoreSave)
    {
       if (event.GetSource() == "nc")
       {
          std::vector<std::string> splitted = split(text, ',');
          CharacterDatabase.BeginTransaction();
          for (std::vector<std::string>::iterator i = splitted.begin(); i != splitted.end(); i++)
          {
             const char* name = i->c_str();
             switch (name[0])
             {
             case '+':
             case '-':
             case '~':
                sPlayerbotDbStore.Save(ai);
                break;
             }
          }
          CharacterDatabase.CommitTransaction();
       }
    }

    if (text.find("?") != std::string::npos)
    {
        ai->PrintStrategies(requester, BotState::BOT_STATE_NON_COMBAT);
    }

    return true;
}

bool ChangeDeadStrategyAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    std::string text = event.GetParam();
    text = text.empty() ? getName() : text;

    ai->ChangeStrategy(text, BotState::BOT_STATE_DEAD);

    if (text.find("?") != std::string::npos)
    {
        ai->PrintStrategies(requester, BotState::BOT_STATE_DEAD);
    }

    return true;
}

bool ChangeReactionStrategyAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    std::string text = event.GetParam();
    text = text.empty() ? getName() : text;

    ai->ChangeStrategy(text, BotState::BOT_STATE_REACTION);

    if (text.find("?") != std::string::npos)
    {
        ai->PrintStrategies(requester, BotState::BOT_STATE_REACTION);
    }

    return true;
}

bool ChangeAllStrategyAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    std::string text = event.GetParam();
    std::string strategyName = text.empty() ? strategy : text;

    ai->ChangeStrategy(strategyName, BotState::BOT_STATE_ALL);

    if (!sPlayerbotAIConfig.bExplicitDbStoreSave)
    {
       if (event.GetSource() == "nc" || event.GetSource() == "co")
       {
          std::vector<std::string> splitted = split(text, ',');
          CharacterDatabase.BeginTransaction();
          for (std::vector<std::string>::iterator i = splitted.begin(); i != splitted.end(); i++)
          {
             const char* name = i->c_str();
             switch (name[0])
             {
             case '+':
             case '-':
             case '~':
             {
                sPlayerbotDbStore.Save(ai);
                break;
             }
             }
          }
          CharacterDatabase.CommitTransaction();
       }
    }

    if (text.find("?") != std::string::npos)
    {
        ai->PrintStrategies(requester, BotState::BOT_STATE_ALL);
    }

    return true;
}