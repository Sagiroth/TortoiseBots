#pragma once
#include <cstdint>
#include <string>

namespace ai
{
    class EbonrocSwapWindow
    {
    public:
        void Begin(std::uint64_t victim, std::int64_t applied)
        {
            if (victim == source && applied == application)
                return;
            source = victim;
            application = applied;
            replacement = 0;
            attempts = 0;
            lastAttempt = 0;
            confirmed = false;
        }
        bool ObserveVictim(std::uint64_t victim)
        {
            if (attempts && victim == replacement && victim != source)
                confirmed = true;
            else if (victim == source)
                confirmed = false;
            return confirmed;
        }
        bool Ready(std::uint32_t now) const
        {
            return !confirmed && attempts < 3 && (!attempts || now - lastAttempt >= 2000);
        }
        bool Claim(std::uint64_t caller, std::uint64_t chosen, std::uint32_t now)
        {
            if (!chosen || caller != chosen || !Ready(now))
                return false;
            replacement = chosen;
            ++attempts;
            lastAttempt = now;
            return true;
        }
    private:
        std::uint64_t source = 0;
        std::uint64_t replacement = 0;
        std::int64_t application = 0;
        std::uint32_t lastAttempt = 0;
        unsigned attempts = 0;
        bool confirmed = false;
    };

    inline bool EbonrocTauntAction(std::string const& name)
    {
        return name == "taunt" || name.compare(0, 6, "taunt ") == 0 || name == "growl" ||
            name == "hand of reckoning" || name == "righteous defense" ||
            name == "challenging shout" || name == "challenging roar" || name == "mocking blow";
    }

    inline bool EbonrocBlockAutomaticTaunt(bool selfShadow, bool victimShadow, bool otherHealthyTank)
    {
        return selfShadow || victimShadow || otherHealthyTank;
    }
}
