#ifndef _PlayerbotDbStore_H
#define _PlayerbotDbStore_H

#include "Common.h"
#include "PlayerbotAIBase.h"

class PlayerbotAI;

class PlayerbotDbStore
{
public:
    PlayerbotDbStore() {}
    virtual ~PlayerbotDbStore() {}
    static PlayerbotDbStore& instance()
    {
        static PlayerbotDbStore instance;
        return instance;
    }

    void Save(PlayerbotAI *ai, std::string preset = "");
    void Load(PlayerbotAI *ai, std::string preset = "");
    void Reset(PlayerbotAI *ai, std::string preset = "");
    // Talent topology changes invalidate complete co/nc/dead/react snapshots,
    // but independent value rows (formation, targets, and other durable AI
    // state) remain valid and must be retained.
    void InvalidateStrategySnapshots(PlayerbotAI *ai);

private:
    void SaveValue(uint64 guid, std::string preset, std::string key, std::string value);
    std::string FormatStrategies(std::string type, std::list<std::string_view> strategies);
};

#define sPlayerbotDbStore PlayerbotDbStore::instance()

#endif
