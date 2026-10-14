
#include "playerbot/playerbot.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/PlayerbotFactory.h"
#include "PlayerbotDbStore.h"
#include "MovementModePolicy.h"
#include <cstdlib>
#include <iostream>
#include <utility>
#include <vector>

#include "LootObjectStack.h"
#include "../../runtime/CommandedGroupQuery.h"
#include "../../runtime/RemoteCommanderPolicy.h"
#include "strategy/values/Formations.h"
#include "strategy/values/PositionValue.h"
INSTANTIATE_SINGLETON_1(PlayerbotDbStore);

using namespace ai;

void PlayerbotDbStore::Load(PlayerbotAI *ai, std::string preset)
{
    uint64 guid = ai->GetBot()->getObjectGuid().GetRawValue();
    CharacterDatabase.escape_string(preset);

    // No pull survives a reload (relog, reset, preset). Ended before the
    // replay: its own copies of "wait for attack" go and the player's delay
    // comes back, so the stored set and values below are what is left (a
    // snapshot without "co"/"nc" rows leaves the live engines alone, and a
    // preset's own "wait for attack time" must win over the pre-pull one).
    ai->EndPullWindow();

    auto results = CharacterDatabase.PQuery("SELECT `key`,`value` FROM `ai_playerbot_db_store` WHERE `guid` = '%lu' AND `preset` = '%s'", guid, preset.c_str());
    if (results)
    {
        struct StoredRow
        {
            std::string key;
            std::string value;
        };

        std::vector<StoredRow> rows;
        StoredStrategyRows stored;
        do
        {
            Field* fields = results->Fetch();
            StoredRow row{ fields[0].GetString(), fields[1].GetString() };
            stored.Note(row.key.c_str());
            rows.push_back(std::move(row));
        } while (results->NextRow());

        // Clear and replay only the engines whose own row was read
        // (StoredStrategyRows). A value-only preset (after a talent topology
        // change) clears nothing, and a snapshot missing its "nc" row can no
        // longer wipe the non-combat engine down to the baseline below with
        // no row to replay: that engine keeps its fresh defaults, follow
        // included.
        if (stored.combat)
        {
            ai->ClearStrategies(BotState::BOT_STATE_COMBAT);
            ai->ChangeStrategy("+chat", BotState::BOT_STATE_COMBAT);
        }
        if (stored.nonCombat)
        {
            ai->ClearStrategies(BotState::BOT_STATE_NON_COMBAT);
            // Baseline first: +loot/+delayed roll survive stale snapshots missing
            // them (#222), +quest survives snapshots predating it (#277). Replay
            // below then re-applies the stored snapshot verbatim, so a stored
            // "-quest" (explicit opt-out) still wins over this default.
            ai->ChangeStrategy("+chat,+loot,+delayed roll,+quest", BotState::BOT_STATE_NON_COMBAT);
        }

        std::list<std::string> values;
        for (StoredRow const& row : rows)
        {
            if (row.key == "value") values.push_back(row.value);
            else if (row.key == "co") ai->ChangeStrategy(row.value, BotState::BOT_STATE_COMBAT);
            else if (row.key == "nc") ai->ChangeStrategy(row.value, BotState::BOT_STATE_NON_COMBAT);
            else if (row.key == "dead") ai->ChangeStrategy(row.value, BotState::BOT_STATE_DEAD);
            else if (row.key == "react") ai->ChangeStrategy(row.value, BotState::BOT_STATE_REACTION);
        }

        ai->GetAiObjectContext()->Load(values);

        // The snapshot never holds a binding's "threat" (Save below): a bot
        // that is still bound gets it back for the binding.
        Player* bot = ai->GetBot();
        if (stored.combat && TortoiseBots::BotCommands::IsInCommandedGroup(bot) &&
            !ai->HasRealPlayerMaster() && !bot->InBattleGround() &&
            TortoiseBots::RemoteCommander::KeepsCommandedThreatBackoff(true, PlayerbotAI::IsTank(bot)) &&
            !ai->HasStrategy("threat", BotState::BOT_STATE_COMBAT))
        {
            ai->ChangeStrategy("+threat", BotState::BOT_STATE_COMBAT);
            TortoiseBots::BotCommands::NoteCommanderThreat(bot);
        }
    }

    // An anchor ("pull hold") restored from the snapshot has no pull left to
    // end it.
    ai->ReleasePullHold();
}

void PlayerbotDbStore::Save(PlayerbotAI *ai, std::string preset)
{
    uint64 guid = ai->GetBot()->getObjectGuid().GetRawValue();

    // One transaction for the delete and every insert. Loose async statements
    // are shared out over all character-database workers (4 by default), so
    // the delete could run after some of the inserts and drop them, and a
    // rebuild could read a half-written snapshot. Every save uses the same
    // serial id (DB_SERIAL_ID), so all of them run on one worker in order:
    // one bot's saves never reorder, and two bots' saves never overlap and
    // deadlock on the guid index gap lock. A caller that already holds a
    // transaction opened with the same id (ChangeStrategyAction) keeps ours
    // inside its own; a foreign serial id would route the save to another
    // worker (and a core nested-begin would refuse), so that nesting is
    // rejected loudly rather than saved half-ordered.
    if (CharacterDatabase.InTransaction() && CharacterDatabase.GetTransactionSerialId() != DB_SERIAL_ID)
    {
        sLog.outError("PlayerbotDbStore::Save for guid %lu preset '%s' called inside a foreign transaction (serial %u); snapshot not saved",
            guid, preset.c_str(), CharacterDatabase.GetTransactionSerialId());
        return;
    }
    bool const ownTransaction = !CharacterDatabase.InTransaction() &&
        CharacterDatabase.BeginTransaction(DB_SERIAL_ID);

    Reset(ai, preset);

    std::list<std::string> data = ai->GetAiObjectContext()->Save();
    // While a pull's window runs, "wait for attack time" holds the pull's
    // delay: store the player's own value from before the pull instead.
    std::string const waitTimeRow = "wait for attack time>";
    for (std::list<std::string>::iterator i = data.begin(); i != data.end(); ++i)
    {
        if (ai->HasPullWindow() && i->compare(0, waitTimeRow.size(), waitTimeRow) == 0)
            *i = waitTimeRow + std::to_string(ai->PullWindowSavedSeconds());
        SaveValue(guid, preset, "value", *i);
    }

    // A pull's own join window is never stored: it belongs to that pull, and a
    // snapshot restored after a relog has no pull left to end it, so the bot
    // would hold back at the start of every fight from then on. Only the
    // engines the pull added "wait for attack" to are left out; a copy the
    // player set is stored as usual (PullJoinWindowPolicy.h).
    auto stored = [ai](BotState state)
    {
        std::list<std::string_view> strategies = ai->GetStrategies(state);
        if (ai->PullWindowAdded(state))
            strategies.remove(std::string_view("wait for attack"));
        // A commander binding's "threat" is the binding's, not the bot's
        // (RemoteCommander::CommanderThreatLedger): never stored, so a save
        // while bound does not keep the back-off after the binding ends.
        if (state == BotState::BOT_STATE_COMBAT && TortoiseBots::BotCommands::IsCommanderThreat(ai->GetBot()))
            strategies.remove(std::string_view("threat"));
        return strategies;
    };
    SaveValue(guid, preset, "co", FormatStrategies("co", stored(BotState::BOT_STATE_COMBAT)));
    SaveValue(guid, preset, "nc", FormatStrategies("nc", stored(BotState::BOT_STATE_NON_COMBAT)));
    SaveValue(guid, preset, "dead", FormatStrategies("dead", stored(BotState::BOT_STATE_DEAD)));
    SaveValue(guid, preset, "react", FormatStrategies("react", stored(BotState::BOT_STATE_REACTION)));

    if (ownTransaction)
        CharacterDatabase.CommitTransaction();
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
