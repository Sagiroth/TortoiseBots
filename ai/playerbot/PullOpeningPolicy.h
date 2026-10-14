#pragma once
#include <cstdlib>
#include <string>

// Pure policy for how a commanded pull opens: from range (the tank's ranged
// weapon, or the class's pull spell) or by walking in (a body pull).
//
// A plain pull command is a melee engagement - the tank walks in and builds
// threat while the party waits out the join delay - and a pullback opens from
// range. A tank with a gun, bow, crossbow or thrown weapon should also shoot
// when walking in is wrong: the mob stands where no path leads (a ledge), it is
// far enough that the walk would run into the next pack, or the commander asked
// for a ranged pull outright.
//
// Core-free, so tools/test_pull_opening_policy.cpp includes it directly.

namespace ai
{
    enum class PullOpeningReason
    {
        BodyPull,       // nothing asks for range: walk in (a plain pull's default)
        Explicit,       // the order said `ranged`
        Pullback,       // a pullback opens from range
        Unreachable,    // no navmesh path to the mob, but it is in sight and in range
        FarApproach,    // too far to walk in without meeting the next pack
    };

    // Why a wanted ranged opening cannot happen. Empty = nothing is missing.
    enum class PullRangedBlocker
    {
        None,
        NoWeapon,       // no ranged weapon equipped
        Untrained,      // the weapon skill is missing
        NoSpell,        // the skill is there but the shoot spell is not in the book
        NoAmmo,         // a bow, gun or crossbow with nothing to fire
        TooClose,       // inside the shot's minimum range
    };

    struct PullOpeningInput
    {
        bool pullback = false;          // the order is a pullback
        bool explicitRanged = false;    // the order carries `ranged`
        bool autoEnabled = true;        // AiPlayerbot.RangedPull
        bool inLos = false;
        bool pathReachable = true;      // a navmesh path leads to the mob
        float distance = 0.0f;
        float maxRange = 0.0f;          // longest shot, 0 = unknown
        float meleeApproach = 20.0f;    // AiPlayerbot.RangedPullMeleeApproach
    };

    struct PullOpeningDecision
    {
        bool wantRanged = false;
        PullOpeningReason reason = PullOpeningReason::BodyPull;
    };

    // Whether the order wants a ranged opening. The weapon is a separate
    // question (GetRangedBlocker): a tank that wants to shoot and cannot says so
    // and walks in, it does not stand still.
    //
    // The automatic reasons need the mob in sight and inside the shot's range;
    // a mob the tank cannot see or reach yet is approached the old way. An
    // explicit order and a pullback do not: both walk to a firing position.
    inline PullOpeningDecision DecidePullOpening(PullOpeningInput const& in)
    {
        PullOpeningDecision decision;
        if (in.explicitRanged)
        {
            decision.wantRanged = true;
            decision.reason = PullOpeningReason::Explicit;
            return decision;
        }
        if (in.pullback)
        {
            decision.wantRanged = true;
            decision.reason = PullOpeningReason::Pullback;
            return decision;
        }
        if (!in.autoEnabled)
            return decision;

        bool const inShot = in.inLos && in.maxRange > 0.0f && in.distance <= in.maxRange;
        if (!inShot)
            return decision;
        if (!in.pathReachable)
        {
            decision.wantRanged = true;
            decision.reason = PullOpeningReason::Unreachable;
        }
        else if (in.distance > in.meleeApproach)
        {
            decision.wantRanged = true;
            decision.reason = PullOpeningReason::FarApproach;
        }
        return decision;
    }

    inline char const* PullOpeningReasonName(PullOpeningReason reason)
    {
        switch (reason)
        {
            case PullOpeningReason::Explicit: return "order";
            case PullOpeningReason::Pullback: return "pullback";
            case PullOpeningReason::Unreachable: return "unreachable";
            case PullOpeningReason::FarApproach: return "far";
            default: return "body-pull";
        }
    }

    inline char const* PullRangedBlockerName(PullRangedBlocker blocker)
    {
        switch (blocker)
        {
            case PullRangedBlocker::NoWeapon: return "no-ranged-weapon";
            case PullRangedBlocker::Untrained: return "weapon-untrained";
            case PullRangedBlocker::NoSpell: return "shoot-spell-missing";
            case PullRangedBlocker::NoAmmo: return "no-ammo";
            case PullRangedBlocker::TooClose: return "too-close";
            default: return "";
        }
    }

    // The words of a pull order after the verb: `[seconds] [ranged]`, either or
    // both, in that order. seconds is 0-60. Returns false for anything else.
    struct PullOrderOption
    {
        bool hasSeconds = false;
        unsigned seconds = 0;
        bool ranged = false;
    };

    inline bool ParsePullOrderOption(std::string const& text, PullOrderOption& out)
    {
        out = PullOrderOption();
        size_t pos = 0;
        auto nextWord = [&](std::string& word) {
            while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t'))
                ++pos;
            size_t const start = pos;
            while (pos < text.size() && text[pos] != ' ' && text[pos] != '\t')
                ++pos;
            word = text.substr(start, pos - start);
            for (char& c : word)
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c - 'A' + 'a');
            return !word.empty();
        };

        std::string word;
        bool more = nextWord(word);
        if (more && !word.empty() && word.find_first_not_of("0123456789") == std::string::npos)
        {
            if (word.size() > 3)
                return false;
            unsigned const seconds = static_cast<unsigned>(std::atoi(word.c_str()));
            if (seconds > 60)
                return false;
            out.hasSeconds = true;
            out.seconds = seconds;
            more = nextWord(word);
        }
        if (more && word == "ranged")
        {
            out.ranged = true;
            more = nextWord(word);
        }
        return !more;
    }
}
