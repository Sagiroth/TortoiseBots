#pragma once

#include <array>
#include <cstdint>

// Profession specialization grants (owner decision, issue: bots get a
// profession specialization). Players earn these through exclusive quest
// chains (Engineering Pledge of Secrecy / Show Your Work, Blacksmithing
// Art vs Way, Leatherworking Dragonscale / Elemental / Tribal), but pool
// bots never run those chains, so no bot on the realm holds any of them
// (live 2026-11-01: 291 engineers, 104 at 200+, zero with 20219/20222).
//
// Grant rules, pinned by tools/test_profession_specialization_policy.cpp:
// - Qualification mirrors the quest gates: Engineering 200+, Blacksmithing
//   200+, Leatherworking 225+. No level gate beyond what the skill implies
//   (bots hold level-appropriate skill, level*5).
// - Deterministic per bot (guid-based), never flips: guid parity picks one
//   branch where the core cannot hold both (see below).
// - Exclusivity verdict: the core enforces NOTHING between the branches.
//   spell_chain lists each branch as rank 5/6 of the same artisan parents
//   (9787/9788 prev 9785; 10656/10658/10660 prev 10662; 20219/20222 prev
//   12656; 17039-41 prev 9787) with req 0, so no chain blocks the sibling.
//   skill_line_ability rows carry learn_on_get 0 / forward 0, so gaining
//   skill never auto-adds or strips them. No trainer teaches the spec
//   spells or the gated recipes (npc_trainer(_template) has zero rows for
//   20219/20222/9787/9788/17039-41/10656-61 and for the gated recipes), so
//   the free recipe sweep cannot double-grant. The ONLY enforcement is the
//   quest ExclusiveGroups (3526, 5283/5301, 5141/5145), which bots bypass
//   because the grant is direct, and item required_spell (CanUseItem +
//   Player::LearnSpell path teach the gated schematic/pattern/plan only
//   when the matching spec spell is known). Both known together is safe:
//   each gated recipe needs exactly one of them; nothing unlearns the
//   sibling and nothing checks "not has sibling".
// - Config AiPlayerbot.BotProfessionSpecializations: 0 off, 1 one branch
//   per bot (guid parity, fixed forever), 2 all branches (default when
//   safe: it is safe, so 2).

namespace TortoiseBots
{

// Skill ids (SharedDefines.h values, same precedent as
// ProfessionGrantPolicy.h: kept numeric so this header stays core-free).
inline constexpr uint32_t SPEC_SKILL_ENGINEERING = 202;
inline constexpr uint32_t SPEC_SKILL_BLACKSMITHING = 164;
inline constexpr uint32_t SPEC_SKILL_LEATHERWORKING = 165;
inline constexpr uint32_t SPEC_SKILL_ALCHEMY = 171;

// Specialization spells (spell_template names verified 2026-11-01).
inline constexpr uint32_t SPELL_GNOMISH_ENGINEER = 20219;
inline constexpr uint32_t SPELL_GOBLIN_ENGINEER = 20222;
inline constexpr uint32_t SPELL_ARMORSMITH = 9788;
inline constexpr uint32_t SPELL_WEAPONSMITH = 9787;
inline constexpr uint32_t SPELL_MASTER_SWORDSMITH = 17039;
inline constexpr uint32_t SPELL_MASTER_HAMMERSMITH = 17040;
inline constexpr uint32_t SPELL_MASTER_AXESMITH = 17041;
inline constexpr uint32_t SPELL_DRAGONSCALE_LW = 10656;
inline constexpr uint32_t SPELL_ELEMENTAL_LW = 10658;
inline constexpr uint32_t SPELL_TRIBAL_LW = 10660;

// Skill gates mirror the quest RequiredSkillValue rows.
inline constexpr uint32_t SPEC_REQ_ENGINEERING = 200;
inline constexpr uint32_t SPEC_REQ_BLACKSMITHING = 200;
inline constexpr uint32_t SPEC_REQ_LEATHERWORKING = 225;
// Turtle-custom Jewelcrafting (skill_line 755): Gemology / Goldsmith are the
// only profession-specialization spells the core data adds beyond vanilla
// 1.12 (sweep of skill_line_ability TRADE_SKILL rows with learn_on_get 0:
// no tailoring, alchemy or enchanting specialization spells exist). Gate 225
// mirrors the Mastering quests 41275-41278.
inline constexpr uint32_t SPEC_SKILL_JEWELCRAFTING = 755;
inline constexpr uint32_t SPELL_GEMOLOGY = 57551;
inline constexpr uint32_t SPELL_GOLDSMITH = 57553;
inline constexpr uint32_t SPEC_REQ_JEWELCRAFTING = 225;

// A profession's specialization branches: spells granted together under
// mode 2, guid-parity pick under mode 1.
struct SpecializationGroup
{
    uint32_t skillId;
    uint32_t requiredValue;
    std::array<uint32_t, 3> spells; // unused slots are 0
    uint32_t spellCount;
    // A base spec spell that must already be known before this group's
    // spells are granted (0 = none). Only the blacksmithing weapon masters
    // use it: they extend Weaponsmith, so a mode-1 bot that drew Armorsmith
    // skips them instead of holding a master rank without its parent.
    uint32_t requiresSpell = 0;
};

inline constexpr SpecializationGroup kSpecializationGroups[] = {
    { SPEC_SKILL_ENGINEERING, SPEC_REQ_ENGINEERING, { { SPELL_GOBLIN_ENGINEER, SPELL_GNOMISH_ENGINEER, 0 } }, 2 },
    { SPEC_SKILL_BLACKSMITHING, SPEC_REQ_BLACKSMITHING, { { SPELL_ARMORSMITH, SPELL_WEAPONSMITH, 0 } }, 2 },
    // Weapon sub-specs extend Weaponsmith (quest The Way of the
    // Weaponsmith, spell_chain prev 9787, artisan step 9789): mode 1 grants
    // them only to the bot that drew Weaponsmith, never to an Armorsmith.
    { SPEC_SKILL_BLACKSMITHING, SPEC_REQ_BLACKSMITHING, { { SPELL_MASTER_SWORDSMITH, SPELL_MASTER_HAMMERSMITH, SPELL_MASTER_AXESMITH } }, 3, SPELL_WEAPONSMITH },
    { SPEC_SKILL_LEATHERWORKING, SPEC_REQ_LEATHERWORKING, { { SPELL_DRAGONSCALE_LW, SPELL_ELEMENTAL_LW, SPELL_TRIBAL_LW } }, 3 },
    { SPEC_SKILL_JEWELCRAFTING, SPEC_REQ_JEWELCRAFTING, { { SPELL_GEMOLOGY, SPELL_GOLDSMITH, 0 } }, 2 },
};

inline constexpr uint32_t kSpecializationGroupCount =
    sizeof(kSpecializationGroups) / sizeof(kSpecializationGroups[0]);

// Mode for AiPlayerbot.BotProfessionSpecializations.
enum class SpecializationMode : int
{
    Off = 0,   // no automatic specialization grants
    OnePerBot = 1, // one branch per bot, guid parity, fixed forever
    All = 2,   // every branch of each qualified profession
};

// Deterministic branch pick for mode 1: guid parity. Never flips for a bot.
inline uint32_t PickSpecializationIndex(uint32_t guidLow, uint32_t branchCount)
{
    return branchCount ? guidLow % branchCount : 0;
}

// Qualification gate: skill value at or above the quest's requirement.
inline bool QualifiesForSpecialization(uint32_t skillValue, uint32_t requiredValue)
{
    return skillValue >= requiredValue;
}

} // namespace TortoiseBots
