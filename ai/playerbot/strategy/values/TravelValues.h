#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/Value.h"
#include "playerbot/strategy/AiObjectContext.h"

namespace ai
{
    //Definition
    using DestinationEntry = int32;
    using EntryGuidps = std::unordered_map<DestinationEntry, std::vector<AsyncGuidPosition>>;

    //All creature and gameobject entries and reason a bot might want to travel to them.
    class EntryGuidpsValue : public SingleCalculatedValue<EntryGuidps>
    {
    public:
        EntryGuidpsValue(PlayerbotAI* ai, std::string name = "entry guidps") : SingleCalculatedValue(ai, name) {};

        virtual EntryGuidps Calculate() override;
    };

    using DestinationPurose = uint32;

    using EntryTravelPurposeMap = std::unordered_map<DestinationEntry, DestinationPurose>;

    //All creature and gameobject entries and reason a bot might want to travel to them.
    class EntryTravelPurposeMapValue : public SingleCalculatedValue<EntryTravelPurposeMap>
    {
    public:
        EntryTravelPurposeMapValue(PlayerbotAI* ai, std::string name = "entry travel purpose") : SingleCalculatedValue(ai, name) {};

        virtual EntryTravelPurposeMap Calculate() override;
    };

    enum class TravelDestinationPurpose : uint32
    {
        None = 0,
        QuestGiver = 1 << 0,
        QuestObjective1 = 1 << 1,
        QuestObjective2 = 1 << 2,
        QuestObjective3 = 1 << 3,
        QuestObjective4 = 1 << 4,
        QuestAllObjective = QuestObjective1 | QuestObjective2 | QuestObjective3 | QuestObjective4,
        QuestTaker = 1 << 5,
        GenericRpg = 1 << 6,
        Trainer = 1 << 7,
        Repair = 1 << 8,
        Vendor = 1 << 9,
        AH = 1 << 10,
        Mail = 1 << 11,
        Grind = 1 << 12,
        Boss = 1 << 13,
        GatherSkinning = 1 << 14,
        GatherMining = 1 << 15,
        GatherHerbalism = 1 << 16,
        GatherFishing = 1 << 17,
        Explore = 1 << 18,
        MaxFlag = 1 << 19
    };

    const std::unordered_map<TravelDestinationPurpose, std::string> TravelDestinationPurposeName =
    {
        {TravelDestinationPurpose::None, "None"},
        {TravelDestinationPurpose::QuestGiver, "QuestGiver"},
        {TravelDestinationPurpose::QuestObjective1, "QuestObjective1"},
        {TravelDestinationPurpose::QuestObjective2, "QuestObjective2"},
        {TravelDestinationPurpose::QuestObjective3 , "QuestObjective3"},
        {TravelDestinationPurpose::QuestObjective4, "QuestObjective4"},
        {TravelDestinationPurpose::QuestTaker, "QuestTaker"},
        {TravelDestinationPurpose::GenericRpg , "GenericRpg"},
        {TravelDestinationPurpose::Trainer, "Trainer"},
        {TravelDestinationPurpose::Repair, "Repair"},
        {TravelDestinationPurpose::Vendor, "Vendor"},
        {TravelDestinationPurpose::AH , "AH"},
        {TravelDestinationPurpose::Mail , "Mail"},
        {TravelDestinationPurpose::Grind , "Grind"},
        {TravelDestinationPurpose::Boss, "Boss"},
        {TravelDestinationPurpose::GatherSkinning , "GatherSkinning"},
        {TravelDestinationPurpose::GatherMining, "GatherMining"},
        {TravelDestinationPurpose::GatherHerbalism, "GatherHerbalism"},
        {TravelDestinationPurpose::GatherFishing, "GatherFishing"},
        {TravelDestinationPurpose::Explore, "Explore"},
        {TravelDestinationPurpose::MaxFlag, "MaxFlag"}
    };

    class TravelDestination;

    using DestinationList = std::vector<TravelDestination*>;

    //TypedDestinationMap[Purpose][QuestId/Entry]={TravelDestination}
    using EntryDestinationMap = std::unordered_map<int32, DestinationList>;
    using PurposeDestinationMap = std::unordered_map<TravelDestinationPurpose, EntryDestinationMap>;

    //Usage

    //TravelPoint[point, destination, distance]
    using TravelPoint = std::tuple<TravelDestination*, WorldPosition*, float>;
    using TravelPointList = std::vector<TravelPoint>;
    using PartitionedTravelList = std::map<uint32, TravelPointList>;
    using FutureDestinations = std::future<PartitionedTravelList>;

    typedef std::set<uint32> focusQuestTravelList;

    class FocusTravelTargetValue : public ManualSetValue<focusQuestTravelList>
    {
    public:
        FocusTravelTargetValue(PlayerbotAI* ai, focusQuestTravelList defaultValue = {}, std::string name = "focus travel target") : ManualSetValue<focusQuestTravelList>(ai, defaultValue, name) {};
    };

    class HasFocusTravelTargetValue : public BoolCalculatedValue
    {
    public:
        HasFocusTravelTargetValue(PlayerbotAI* ai, std::string name = "has focus travel target", int checkInterval = 10) : BoolCalculatedValue(ai, name, checkInterval) {}

        virtual bool Calculate() override { return !AI_VALUE(focusQuestTravelList, "focus travel target").empty(); };
    };

    //Target fetching

    class FutureTravelDestinationsValue : public ManualSetValue<FutureDestinations*>
    {
    public:
        FutureTravelDestinationsValue(PlayerbotAI* ai, std::string name = "future travel destinations") : ManualSetValue<FutureDestinations*>(ai, new FutureDestinations, name) {}

        ~FutureTravelDestinationsValue() override { delete value; }
    };

    class NoActiveTravelDestinationsValue : public ManualSetValue<bool>, public Qualified
    {
    public:
        NoActiveTravelDestinationsValue(PlayerbotAI* ai, std::string name = "no active travel destinations") : ManualSetValue<bool>(ai, {}, name), Qualified() {}
    };

    class TravelTargetActiveValue : public BoolCalculatedValue
    {
    public:
        TravelTargetActiveValue(PlayerbotAI* ai, std::string name = "travel target active", int checkInterval = 5) : BoolCalculatedValue(ai, name, checkInterval) {};

        virtual bool Calculate() override;
    };

    class TravelTargetReadyValue : public BoolCalculatedValue
    {
    public:
        TravelTargetReadyValue(PlayerbotAI* ai, std::string name = "travel target ready", int checkInterval = 5) : BoolCalculatedValue(ai, name, checkInterval) {};

        virtual bool Calculate() override;
    };

    class TravelTargetTravelingValue : public BoolCalculatedValue
    {
    public:
        TravelTargetTravelingValue(PlayerbotAI* ai, std::string name = "travel target traveling", int checkInterval = 5) : BoolCalculatedValue(ai, name, checkInterval){};

        virtual bool Calculate() override;
    };

    class TravelTargetWorkingValue : public BoolCalculatedValue
    {
    public:
        TravelTargetWorkingValue(PlayerbotAI* ai, std::string name = "travel target working", int checkInterval = 5) : BoolCalculatedValue(ai, name, checkInterval) {};

        virtual bool Calculate() override;
    };

    // "Is any destination of this purpose in reach still active" walks every
    // destination in a window of up to 10000 yd and asks each IsActive. The
    // values below re-ask every 2 s per bot, which made it one of the costliest
    // parts of a bot turn, while the answer moves slowly (nodes respawn,
    // trainers stay put, the bot walks). Keep it for kTtlSec.
    struct TravelProbeCache
    {
        static constexpr time_t kTtlSec = 30;
        time_t at = 0;
        bool result = false;

        template <typename Probe>
        bool Get(Probe probe)
        {
            time_t const now = time(nullptr);
            if (!at || now - at >= kTtlSec)
            {
                result = probe();
                at = now;
            }
            return result;
        }
    };

    class NeedTravelPurposeValue : public BoolCalculatedValue, public Qualified
    {
    public:
        NeedTravelPurposeValue(PlayerbotAI* ai, std::string name = "need travel purpose", int checkInterval = 5) : BoolCalculatedValue(ai, name, checkInterval), Qualified() {};

        virtual bool Calculate() override;
    private:
        TravelProbeCache activeNodeProbe;
    };

    class ShouldTravelNamedValue : public BoolCalculatedValue, public Qualified
    {
    public:
        ShouldTravelNamedValue(PlayerbotAI* ai, std::string name = "should travel named", int checkInterval = 5) : BoolCalculatedValue(ai, name, checkInterval), Qualified() {};

        virtual bool Calculate() override;
    private:
        TravelProbeCache activeTrainerProbe;
    };

    class ShouldLeaveOutgrownZoneValue : public BoolCalculatedValue
    {
    public:
        // Short cache: once ordinary Grind/Gather searches come back empty
        // (1-min purpose parks), the leave errand is the only way out - a
        // 30 s stale "false" would idle the bot between parks. The value
        // itself is two cached lookups, no world scan.
        ShouldLeaveOutgrownZoneValue(PlayerbotAI* ai, std::string name = "should leave outgrown zone", int checkInterval = 5) : BoolCalculatedValue(ai, name, checkInterval) {};

        virtual bool Calculate() override;
    };

    class InOverworldValue : public BoolCalculatedValue
    {
    public:
        InOverworldValue(PlayerbotAI* ai, std::string name = "in overworld", int checkInterval = 10) : BoolCalculatedValue(ai, name, checkInterval) {};

        virtual bool Calculate() override {return WorldPosition(bot).isOverworld();}
    };

    //Keep using this target

    class QuestStageActiveValue : public BoolCalculatedValue, public Qualified
    {
    public:
        QuestStageActiveValue(PlayerbotAI* ai, std::string name = "quest stage active", int checkInterval = 1) : BoolCalculatedValue(ai, name, checkInterval), Qualified() {};

        virtual bool Calculate() override;
    };
}