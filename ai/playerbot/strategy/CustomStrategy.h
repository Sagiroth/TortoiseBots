#pragma once
#include "Strategy.h"

namespace ai
{
    class CustomStrategy : public Strategy, public Qualified
    {
    public:
        CustomStrategy(PlayerbotAI* ai) : Strategy(ai), Qualified() {}
        std::string getName() override { return "custom::" + qualifier; }
        void Reset();

    private:
        void InitNonCombatTriggers(std::list<TriggerNode*> &triggers) override;
        void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;

        void LoadActionLines(uint32 owner);

    private:
        std::list<std::string> actionLines;
        
    public:
        // Nothing in the tree ever writes to this, so the branch that reads it
        // is never taken. Left in place because it is public and something
        // outside may yet fill it; the real caching happens below.
        static std::map<std::string, std::string> actionLinesCache;

        // Drops the remembered lines for a qualifier after somebody edits them.
        static void ForgetCached(std::string const& forQualifier);

    private:
        typedef std::pair<std::string, uint32> CacheKey;
        static std::map<CacheKey, std::list<std::string>> loadedLines;
    };
}
