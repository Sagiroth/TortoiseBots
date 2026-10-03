#pragma once

#include <cstdint>
#include <ctime>
#include <string>

// Weighted RPG status mixer (issue #422, donor mod-playerbots
// `RpgStatusProbWeight` / `NewRpgBaseAction::RandomChangeStatus`).
//
// Donor rule (donor `src/PlayerbotAIConfig.cpp:730-737`, weights
// DoQuest 60, WanderNpc 20, WanderRandom/GoGrind/Flight 15, Camp/PvP 10,
// Rest 5; `NewRpgBaseAction.cpp:1083` `RandomChangeStatus`, `:1215`
// `CheckRpgStatusAvailable`): the next autonomous activity is drawn from a
// weighted table, but only among statuses available right now; an empty
// table falls back to rest.
//
// Mapping onto our architecture (no donor status machine, travel purposes
// only): the four leisure travel purposes - quest, grind, camp (GenericRpg
// inn hubs) and explore - compete through one weighted roll instead of
// separate triggers racing on static relevance. Service/forced journeys
// (vendor, repair, AH, mail, trainer, city, guild, ...) bypass the mixer:
// need-gated errands outrank leisure by design, and gating them would strand
// real business. All destination gates stay in force (#418/#428/#434, local
// picks #424/#441, stall parks #423/#442) - the mixer only decides which
// leisure purpose may request next.
//
// Pool bots only, verdict cached on the value store ("rpg mixer pick" +
// "rpg mixer until", 10 min like the other travel parks), so the roll runs
// once per trip, never per tick, with no world scan: availability reuses the
// already-cached need values the request gates read. A live verdict is
// revalidated on every read - a slot that went unavailable re-rolls at once,
// never parks the bot. Grind is the fallback: with quest/camp/explore all
// unavailable the verdict is grind, never a dead -1, so the mixer can never
// idle a bot or slow its levelling past main. The world-facing gate lives
// at the call site (`ChooseTravelTargetAction.cpp`, file-local
// `RpgMixerGateAllows`);
// this header holds only the pure decision rules so the standalone test
// never links the server.

namespace ai
{
    // Mixer slots in priority order (quest-heavy like the donor).
    enum class RpgMixerSlot : std::uint8_t
    {
        Quest = 0,
        Grind = 1,
        Camp = 2,
        Explore = 3,
        Count = 4
    };

    // Donor weight table, adapted: DoQuest 60, GoGrind 15, GoCamp 10,
    // Explore 5 (our wander/flight/rest equivalents - GenericRpg NPC
    // wandering, Explore zone walks - keep the donor's quest-heavy shape;
    // flight stays transport under #426/#447, not a mixer slot).
    constexpr std::uint32_t RPG_MIXER_WEIGHT_QUEST = 60;
    constexpr std::uint32_t RPG_MIXER_WEIGHT_GRIND = 15;
    constexpr std::uint32_t RPG_MIXER_WEIGHT_CAMP = 10;
    constexpr std::uint32_t RPG_MIXER_WEIGHT_EXPLORE = 5;

    // How long one mixer verdict stands: the fruitless-errand park order of
    // magnitude (10 min). In practice a verdict spans one request-to-pick
    // cycle - a successful pick and an empty-search park both clear it, so
    // the next trip rolls fresh; the window is only the backstop.
    constexpr time_t RPG_MIXER_PICK_WINDOW_SECONDS = 10 * 60;

    // Value-store keys for the cached verdict (slot index, timestamp).
    inline std::string RpgMixerPickKey() { return "rpg mixer pick"; }
    inline std::string RpgMixerUntilKey() { return "rpg mixer until"; }

    // Availability of each leisure slot, read by the caller from cached need
    // values only (no world scan): quest = free log slots, quest purpose
    // unparked, "rpg quest" strategy on; grind/camp/explore = purpose
    // unparked; camp additionally level 5+ and its rpg-phase window open
    // (the cheap NeedTravelPurposeValue(GenericRpg) gates); grind
    // additionally its own phase window (beginners grind all hours, others
    // the first 45 min - a Grind verdict while the plain grind row's need is
    // false would block runnable camp/quest while grind itself cannot fire);
    // explore additionally the "explore" strategy on. The transient
    // "is travel refresh" flag is not mirrored: it only holds during refresh
    // condition checks, and the real triggers still enforce every need.
    struct RpgMixerAvailability
    {
        bool quest = false;
        bool grind = false;
        bool camp = false;
        bool explore = false;
    };

    inline bool RpgMixerSlotAvailable(RpgMixerSlot slot, const RpgMixerAvailability& availability)
    {
        switch (slot)
        {
            case RpgMixerSlot::Quest: return availability.quest;
            case RpgMixerSlot::Grind: return availability.grind;
            case RpgMixerSlot::Camp: return availability.camp;
            case RpgMixerSlot::Explore: return availability.explore;
            default: return false;
        }
    }

    inline std::uint32_t RpgMixerSlotWeight(RpgMixerSlot slot)
    {
        switch (slot)
        {
            case RpgMixerSlot::Quest: return RPG_MIXER_WEIGHT_QUEST;
            case RpgMixerSlot::Grind: return RPG_MIXER_WEIGHT_GRIND;
            case RpgMixerSlot::Camp: return RPG_MIXER_WEIGHT_CAMP;
            case RpgMixerSlot::Explore: return RPG_MIXER_WEIGHT_EXPLORE;
            default: return 0;
        }
    }

    // Pool-only scope: owned/hired bots keep player control, instances and
    // battlegrounds keep the existing blocks.
    inline bool RpgMixerAppliesToBot(bool masterlessRandom)
    {
        return masterlessRandom;
    }

    // A cached verdict is live while its timestamp is in the future.
    inline bool RpgMixerVerdictLive(time_t mixerUntil, time_t now)
    {
        return mixerUntil != 0 && mixerUntil > now;
    }

    // A live verdict stays only while its slot is still available: re-check
    // on every read, and re-roll the moment the winner parked. Never a
    // sticky dead verdict - grind is the fallback below, so the gate below
    // still admits grind when everything else parked.

    // Weighted roll over the available slots (donor `RandomChangeStatus`):
    // `roll01` in [0, 1) selects proportionally to weight among available
    // slots only. A dead -1 below only covers the never-reached all-four-
    // unavailable corner (the grind fallback above always fires first):
    // the gate below admits it so the real need triggers decide.
    inline int PickRpgMixerSlot(const RpgMixerAvailability& availability, double roll01)
    {
        // Grind is the fallback: with quest/camp/explore all unavailable
        // the verdict is grind, never a dead -1, so the mixer admits the
        // grind request and its real gates (need, park, level band) decide -
        // exactly main's behaviour. The mixer can never idle a bot or slow
        // its levelling past main.
        if (!availability.quest && !availability.camp && !availability.explore)
            return (int)RpgMixerSlot::Grind;
        std::uint32_t total = 0;
        for (std::uint8_t i = 0; i < (std::uint8_t)RpgMixerSlot::Count; ++i)
        {
            RpgMixerSlot slot = (RpgMixerSlot)i;
            if (RpgMixerSlotAvailable(slot, availability))
                total += RpgMixerSlotWeight(slot);
        }
        if (total == 0)
            return -1;

        double point = roll01 * (double)total;
        double walk = 0.0;
        for (std::uint8_t i = 0; i < (std::uint8_t)RpgMixerSlot::Count; ++i)
        {
            RpgMixerSlot slot = (RpgMixerSlot)i;
            if (!RpgMixerSlotAvailable(slot, availability))
                continue;
            walk += (double)RpgMixerSlotWeight(slot);
            if (point < walk)
                return (int)i;
        }
        // Rounding edge: hand the last available slot.
        for (int i = (int)RpgMixerSlot::Count - 1; i >= 0; --i)
            if (RpgMixerSlotAvailable((RpgMixerSlot)i, availability))
                return i;
        return -1;
    }

    // Does the winning slot admit a request for this travel purpose? The
    // qualifier arrives as the request actions carry it: the quest errand
    // (`request quest travel target`, no qualifier - it stores "quest" and
    // arrives empty or "quest"), every other purpose under its numeric
    // `TravelDestinationPurpose` id. Quest is a leisure slot and gated when
    // another slot won - otherwise quest (6.30) pre-empts a camp (6.28) or
    // explore (6.29) win and the mixer only steals from grind. Grind is the
    // fallback: a grind request is always admitted, so a bot with nothing
    // else available keeps levelling the way main did. A dead verdict (-1)
    // only covers the never-reached all-four-unavailable corner and admits
    // everything still runnable - its real need triggers decide.
    inline bool RpgMixerSlotAllowsRequest(int slotIndex, const std::string& qualifier,
        const std::string& grindPurposeId, const std::string& campPurposeId,
        const std::string& explorePurposeId)
    {
        if (slotIndex < 0 || slotIndex >= (int)RpgMixerSlot::Count)
            return true;
        switch ((RpgMixerSlot)slotIndex)
        {
            case RpgMixerSlot::Quest:
                return qualifier.empty() || qualifier == "quest";
            case RpgMixerSlot::Grind:
                return qualifier == grindPurposeId;
            case RpgMixerSlot::Camp:
                return qualifier == campPurposeId;
            case RpgMixerSlot::Explore:
                return qualifier == explorePurposeId;
            default:
                return true;
        }
    }
}
