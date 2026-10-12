#pragma once
#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/ServerFacade.h"

namespace ai
{
    // trainableSpellMap[TrainerType][required class/race/skill][TrainerSpell] = {trainer entry};
    typedef std::unordered_map<TrainerSpell const*, std::vector<int32>>  spellTrainerMap;
    typedef std::unordered_map<uint32, spellTrainerMap>          trainableSpellList;
    typedef std::unordered_map<TrainerType, trainableSpellList>         trainableSpellMap;

    // Trainer affordability: one rule for every reader. The trip decision
    // ("should travel named::trainer *" and the broke-bot sell rules read the
    // "trainable spells" list), the rpg/nearby trainer trigger
    // (RpgTrainTrigger::TeachesAffordableSpell) and the visit itself
    // (TrainerAction) must agree on which rows count and what they cost, or a
    // bot walks to a trainer that then teaches it nothing (batch-8 audit:
    // 1,020 TrainerNoMoney on 164 bots in 16 h).

    // A row the visit would actually teach now: the trainer shows it green and
    // at least one spell it teaches passes the level gate (SpellRankPolicy.h).
    // Some green rows teach a spell above the bot's level (warrior row 1229
    // teaches Elemental Fury, level 35, from level 6 on); they are never
    // learnable, so they must not count as a rank to travel for or pay for.
    bool TrainerSpellLearnableNow(Player* bot, TrainerSpell const* tSpell);

    // The price the trainer charges for a row: list price times the
    // reputation discount, rounded down like the core's buy handler.
    uint32 TrainerSpellPrice(TrainerSpell const* tSpell, float discountMod);

    // True when the visit charges nothing (AutoTrainSpells = free, gold cheat).
    bool TrainerTrainsForFree(PlayerbotAI* ai);

    // What the bot may spend at a trainer of this type: the "free money for"
    // budget of that type (class: spells, trade: skilltraining, mount: mount,
    // anything else: anything), or UINT32_MAX when training is free.
    uint32 TrainerSpendableMoney(PlayerbotAI* ai, TrainerType type);

    class TrainableSpellMapValue : public SingleCalculatedValue<trainableSpellMap*>
    {
    public:
        TrainableSpellMapValue(PlayerbotAI* ai) : SingleCalculatedValue<trainableSpellMap*>(ai, "trainable spell map") {}

        virtual trainableSpellMap* Calculate() override;
        virtual ~TrainableSpellMapValue() override { delete value; }
    };

    // Everything Player::GetTrainerSpellState reads from the bot: level,
    // known spells, skill values and free profession points. While it stays
    // the same, so does the trainable set, and the scan over every trainer
    // spell in the world (thousands of GetTrainerSpellState calls, re-run
    // every 2 s per bot) can return the last result instead.
    struct TrainerStateSignature
    {
        uint32 level = 0;
        uint32 knownSpells = 0;
        uint32 freeProfessionPoints = 0;
        uint64 skills = 0;

        static TrainerStateSignature Of(Player* bot);
        bool operator==(TrainerStateSignature const& other) const
        {
            return level == other.level && knownSpells == other.knownSpells &&
                freeProfessionPoints == other.freeProfessionPoints && skills == other.skills;
        }
    };

    class TrainableSpellsValue : public CalculatedValue<std::vector< TrainerSpell const*>>, public Qualified
    {
    public:
        TrainableSpellsValue(PlayerbotAI* ai, std::string name = "trainable spells", int checkInterval = 5) : CalculatedValue<std::vector<TrainerSpell const*>>(ai, name, checkInterval), Qualified(){}

        virtual std::vector<TrainerSpell const*> Calculate() override;

        virtual std::string Format() override;
    private:
        bool calculated = false;
        TrainerStateSignature signature;
    };

    class AvailableTrainersValue : public CalculatedValue<std::vector<int32>>, public Qualified
    {
    public:
        AvailableTrainersValue(PlayerbotAI* ai, std::string name = "available trainers", int checkInterval = 5) : CalculatedValue<std::vector<int32>>(ai, name, checkInterval), Qualified() {}

        virtual std::vector<int32> Calculate() override;
    private:
        bool calculated = false;
        TrainerStateSignature signature;
    };

    class TrainCostValue : public Uint32CalculatedValue, public Qualified
    {
    public:
        TrainCostValue(PlayerbotAI* ai) : Uint32CalculatedValue(ai, "train cost", 60), Qualified() {}

        virtual uint32 Calculate() override;
    };
}
