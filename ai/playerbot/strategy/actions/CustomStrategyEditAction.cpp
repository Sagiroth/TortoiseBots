
#include "playerbot/playerbot.h"
#include "CustomStrategyEditAction.h"
#include "playerbot/strategy/CustomStrategy.h"

using namespace ai;

bool CustomStrategyEditAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    std::string text = event.GetParam();
    int pos = text.find(" ");
    if (pos == std::string::npos) return PrintHelp(requester);
    std::string name = text.substr(0, pos);
    text = text.substr(pos + 1);

    pos = text.find(" ");
    if (pos == std::string::npos) pos = text.size();
    std::string idx = text.substr(0, pos);
    text = pos >= text.size() ? "" : text.substr(pos + 1);

    return idx == "?" ? Print(name, requester) : Edit(name, atoi(idx.c_str()), text, requester);
}

bool CustomStrategyEditAction::PrintHelp(Player* requester)
{
    ai->TellPlayer(requester, "=== Custom strategies ===");
    uint32 owner = (uint32)ai->GetBot()->GetGUIDLow();
    auto results = CharacterDatabase.PQuery("SELECT distinct name FROM ai_playerbot_custom_strategy WHERE owner = '%u'", owner);
    if (results)
    {
        do
        {
            Field* fields = results->Fetch();
            std::string name = fields[0].GetString();
            ai->TellPlayer(requester, name);
        }
        while (results->NextRow());
    }

    ai->TellPlayer(requester, "Usage: cs <name> <idx> <command>");
    return false;
}

bool CustomStrategyEditAction::Print(std::string name, Player* requester)
{
    if (name.size() > 255)
        return false;

    std::string safeName = name;
    CharacterDatabase.escape_string(safeName);

    std::ostringstream out; out << "=== " << name << " ===";
    ai->TellPlayer(requester, out.str());

    uint32 owner = (uint32)ai->GetBot()->GetGUIDLow();
    auto results = CharacterDatabase.PQuery("SELECT idx, action_line FROM ai_playerbot_custom_strategy WHERE name = '%s' and owner = '%u' order by idx", safeName.c_str(), owner);
    if (results)
    {
        do
        {
            Field* fields = results->Fetch();
            uint32 idx = fields[0].GetUInt32();
            std::string action = fields[1].GetString();
            PrintActionLine(idx, action, requester);
        }
        while (results->NextRow());
    }

    return true;
}

bool CustomStrategyEditAction::Edit(std::string name, uint32 idx, std::string command, Player* requester)
{
    if (name.empty() || name.size() > 255 || command.size() > 1024)
        return false;

    std::string safeName = name;
    std::string safeCommand = command;
    CharacterDatabase.escape_string(safeName);
    CharacterDatabase.escape_string(safeCommand);

    uint32 owner = (uint32)ai->GetBot()->GetGUIDLow();
    auto results = CharacterDatabase.PQuery("SELECT action_line FROM ai_playerbot_custom_strategy WHERE name = '%s' and owner = '%u' and idx = '%u'", safeName.c_str(), owner, idx);
    if (results)
    {
        if (command.empty())
        {
            CharacterDatabase.DirectPExecute("DELETE FROM ai_playerbot_custom_strategy WHERE name = '%s' and owner = '%u' and idx = '%u'", safeName.c_str(), owner, idx);
        }
        else
        {
            CharacterDatabase.DirectPExecute("UPDATE ai_playerbot_custom_strategy SET action_line = '%s' WHERE name = '%s' and owner = '%u' and idx = '%u'", safeCommand.c_str(), safeName.c_str(), owner, idx);
        }
    }
    else
    {
        CharacterDatabase.DirectPExecute("INSERT INTO ai_playerbot_custom_strategy (name, owner, idx, action_line) VALUES ('%s', '%u', '%u', '%s')", safeName.c_str(), owner, idx, safeCommand.c_str());
    }

    PrintActionLine(idx, command, requester);

    std::ostringstream ss; ss << "custom::" << name;
    Strategy* strategy = ai->GetAiObjectContext()->GetStrategy(ss.str());
    if (strategy)
    {
        CustomStrategy *cs = dynamic_cast<CustomStrategy*>(strategy);
        if (cs)
        {
            cs->Reset();
            ai->ReInitCurrentEngine();
        }
    }
    return true;
}

bool CustomStrategyEditAction::PrintActionLine(uint32 idx, std::string command, Player* requester)
{
    std::ostringstream out; out << "#" << idx << " " << command;
    ai->TellPlayer(requester, out.str());
    return true;
}
