#pragma once

#include <cstdint>

// Trade-trainer matching policy: which trade (profession) trainers a bot may
// walk to. Pure decision rules over durable facts (skill ids the bot holds,
// free primary-profession slots, the skill a green spell belongs to),
// unit-tested on their own (tools/test_trade_trainer_policy.cpp).
//
// Problem: `AvailableTrainersValue` used to list any trade trainer teaching
// any GREEN rank, whatever the bot's professions - and core
// `Player::GetTrainerSpellState` returns GREEN for a first-rank primary the
// bot does not have. So a level-5 miner/blacksmith saw every herbalism,
// alchemy and tailoring trainer in the window as "available" and walked to
// whichever was nearest, learning nothing there (live: ~33 trade picks/h at
// level 5 against 0 profession learns below 10).
//
// Rule: a green trade spell justifies a trip only when it teaches the bot's
// OWN craft - a skill the bot already holds - or, when the bot still has a
// free primary slot, a first-rank primary it could take up. Secondaries the
// factory already grants (cooking, first aid, fishing) never justify a trip
// on their own; recipe ranks for an unheld primary don't either. Callers pass
// the spell's skill id, which `TrainableSpellMapValue` already resolves per
// spell (reqSkill, else EffectMiscValue[1]).

namespace ai
{

// A green trade spell worth walking to: for a held skill (next rank, recipe),
// or a first-rank primary the bot could still learn (free slot, no re-roll of
// held primaries - the factory never grants a third).
inline bool TradeSpellJustifiesTrip(uint32_t spellSkillId, bool botHasSkill,
    bool isFirstRankPrimary, bool botHasFreePrimarySlot)
{
    if (spellSkillId == 0)
        return false;
    if (botHasSkill)
        return true;
    return isFirstRankPrimary && botHasFreePrimarySlot;
}

// A trainer row's taught spells, resolved by the caller from the teaching
// spell's SPELL_EFFECT_LEARN_SPELL effects (core Player::GetTrainerSpellState
// loops the taught spells because a teaching spell like 2581 Apprentice Miner
// is not itself the rank-1 profession spell 2575 Mining). At most 3 effects
// per SpellEntry; count says how many trigger ids follow.
struct TaughtSpellIds
{
    uint32_t ids[3] = { 0, 0, 0 };
    uint32_t count = 0;
};

// True when the row teaches a first-rank primary: the row spell itself (some
// rows teach directly) or any taught spell, each tested by the caller-side
// predicate (core sSpellMgr.IsPrimaryProfessionFirstRankSpell). Pure scan
// shape over plain ids, unit-tested on its own.
template <typename IsFirstRank>
inline bool TrainerRowTeachesFirstRankPrimary(uint32_t rowSpellId, TaughtSpellIds const& taught, IsFirstRank isFirstRank)
{
    if (rowSpellId && isFirstRank(rowSpellId))
        return true;
    for (uint32_t i = 0; i < taught.count && i < 3; ++i)
        if (taught.ids[i] && isFirstRank(taught.ids[i]))
            return true;
    return false;
}

// A visit must never add a 3rd primary profession: a first-rank primary row
// teaches only when the bot holds fewer than two primaries (counted from
// held skills upstream, so a legacy bare-skill pair without rank spells
// still blocks). Ranks and recipes for held professions are unaffected.
inline bool FirstRankPrimaryTeachableNow(bool rowTeachesFirstRank, uint32_t heldPrimaries)
{
    return !rowTeachesFirstRank || heldPrimaries < 2;
}

} // namespace ai
