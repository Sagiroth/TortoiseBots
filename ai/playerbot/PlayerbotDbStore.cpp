
#include "playerbot/playerbot.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/PlayerbotFactory.h"
#include "PlayerbotDbStore.h"
#include <cstdlib>
#include <iostream>
#include <utility>
#include <vector>

#include "LootObjectStack.h"
#include "strategy/values/Formations.h"
#include "strategy/values/PositionValue.h"
INSTANTIATE_SINGLETON_1(PlayerbotDbStore);

using namespace ai;

void PlayerbotDbStore::Load(PlayerbotAI *ai, std::string preset)
{
    uint64 guid = ai->GetBot()->getObjectGuid().GetRawValue();
    CharacterDatabase.escape_string(preset);

    auto results = CharacterDatabase.PQuery("SELECT `key`,`value` FROM `ai_playerbot_db_store` WHERE `guid` = '%lu' AND `preset` = '%s'", guid, preset.c_str());
    if (results)
    {
        struct StoredRow
        {
            std::string key;
            std::string value;
        };

        std::vector<StoredRow> rows;
        bool hasStrategySnapshot = false;
        do
        {
            Field* fields = results->Fetch();
            StoredRow row{ fields[0].GetString(), fields[1].GetString() };
            hasStrategySnapshot = hasStrategySnapshot ||
                row.key == "co" || row.key == "nc" || row.key == "dead" || row.key == "react";
            rows.push_back(std::move(row));
        } while (results->NextRow());

        // A value-only preset is possible after a talent topology change. Do
        // not clear the freshly rebuilt defaults in that case. Complete
        // snapshots retain the historical clear-and-replay behavior.
        if (hasStrategySnapshot)
        {
            ai->ClearStrategies(BotState::BOT_STATE_COMBAT);
            ai->ClearStrategies(BotState::BOT_STATE_NON_COMBAT);
            ai->ChangeStrategy("+chat", BotState::BOT_STATE_COMBAT);
            ai->ChangeStrategy("+chat", BotState::BOT_STATE_NON_COMBAT);
        }

        std::list<std::string> values;
        for (StoredRow const& row : rows)
        {
            if (row.key == "value") values.push_back(row.value);
            else if (hasStrategySnapshot && row.key == "co") ai->ChangeStrategy(row.value, BotState::BOT_STATE_COMBAT);
            else if (hasStrategySnapshot && row.key == "nc") ai->ChangeStrategy(row.value, BotState::BOT_STATE_NON_COMBAT);
            else if (hasStrategySnapshot && row.key == "dead") ai->ChangeStrategy(row.value, BotState::BOT_STATE_DEAD);
            else if (hasStrategySnapshot && row.key == "react") ai->ChangeStrategy(row.value, BotState::BOT_STATE_REACTION);
        }

        ai->GetAiObjectContext()->Load(values);
    }
}

void PlayerbotDbStore::Save(PlayerbotAI *ai, std::string preset)
{
    uint64 guid = ai->GetBot()->getObjectGuid().GetRawValue();

    Reset(ai, preset);

    std::list<std::string> data = ai->GetAiObjectContext()->Save();
    for (std::list<std::string>::iterator i = data.begin(); i != data.end(); ++i)
    {
        SaveValue(guid, preset, "value", *i);
    }

    SaveValue(guid, preset, "co", FormatStrategies("co", ai->GetStrategies(BotState::BOT_STATE_COMBAT)));
    SaveValue(guid, preset, "nc", FormatStrategies("nc", ai->GetStrategies(BotState::BOT_STATE_NON_COMBAT)));
    SaveValue(guid, preset, "dead", FormatStrategies("dead", ai->GetStrategies(BotState::BOT_STATE_DEAD)));
    SaveValue(guid, preset, "react", FormatStrategies("react", ai->GetStrategies(BotState::BOT_STATE_REACTION)));
}

std::string PlayerbotDbStore::FormatStrategies(std::string type, std::list<std::string_view> strategies)
{
    std::ostringstream out;
    for(const auto& strategy : strategies)
        out << "+" << strategy << ",";

    std::string res = out.str();
    return res.substr(0, res.size() - 1);
}

void PlayerbotDbStore::Reset(PlayerbotAI *ai, std::string preset)
{
    uint64 guid = ai->GetBot()->getObjectGuid().GetRawValue();
    uint32 account = sObjectMgr.GetPlayerAccountIdByGUID(ObjectGuid(guid));
    CharacterDatabase.escape_string(preset);

    CharacterDatabase.PExecute("DELETE FROM `ai_playerbot_db_store` WHERE `guid` = '%lu' AND `preset` = '%s'", guid, preset.c_str());
}

void PlayerbotDbStore::InvalidateStrategySnapshots(PlayerbotAI *ai)
{
    if (!ai || !ai->GetBot())
        return;

    uint64 guid = ai->GetBot()->getObjectGuid().GetRawValue();
    CharacterDatabase.PExecute(
        "DELETE FROM `ai_playerbot_db_store` WHERE `guid` = '%lu' AND `key` IN ('co','nc','dead','react')",
        guid);
}

void PlayerbotDbStore::SaveValue(uint64 guid, std::string preset, std::string key, std::string value)
{
    CharacterDatabase.escape_string(preset);
    CharacterDatabase.escape_string(key);
    CharacterDatabase.escape_string(value);
    CharacterDatabase.PExecute("INSERT INTO `ai_playerbot_db_store` (`guid`, `preset`, `key`, `value`) VALUES ('%lu', '%s', '%s', '%s')", guid, preset.c_str(), key.c_str(), value.c_str());
}
