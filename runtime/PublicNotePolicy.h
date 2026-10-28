#pragma once

#include <cstdint>
#include <string>

// Guild public-note format: spec, talents and both primary professions with
// skill, e.g. "Arms 31/20/0 Min245 BS230".
//
// The client caps every guild note at 31 characters. Short spec labels plus
// short profession tokens keep spec + talents + BOTH professions within the
// cap for every spec at 300/300; the composer clamps the final string to the
// cap as a safety bound.
//
// Pure formatting rules over plain values, unit-tested on their own
// (tools/test_public_note_policy.cpp). TalentSpec feeds this function the
// bot's class, spec tab, talent trees and held primary professions; the note
// text is computed on any thread and applied on the world thread.

namespace TortoiseBots
{

// Core Guild.h GUILD_NOTE_MAX_LENGTH; the note must never exceed it.
inline constexpr uint32_t kPublicNoteMaxLength = 31;

// Skill-driven refresh cadence in seconds. Talent changes refresh the note at
// once; skill-ups are picked up by the AI tick at most this often.
inline constexpr uint32_t kPublicNoteSkillRefreshSec = 180;

struct PublicNoteProfession
{
    uint32_t skillId = 0;
    uint32_t skillValue = 0;
};

// Short abbreviation per primary profession. Skill ids are SharedDefines.h
// values, kept numeric so this header stays core-free. Returns nullptr for
// anything that is not a primary profession. Two letters each: with two
// max-skill tokens ("Mi300 En300") both professions always fit the cap.
inline char const* PublicNoteProfessionAbbreviation(uint32_t skillId)
{
    switch (skillId)
    {
        case 171: return "Al";   // Alchemy
        case 164: return "BS";   // Blacksmithing
        case 333: return "En";   // Enchanting
        case 202: return "Eg";   // Engineering
        case 182: return "Hb";   // Herbalism
        case 165: return "LW";   // Leatherworking
        case 186: return "Mi";   // Mining
        case 393: return "Sk";   // Skinning
        case 197: return "Ta";   // Tailoring
        default:  return nullptr;
    }
}

// Short spec label per class and talent tab. Class ids are SharedDefines.h
// values (warrior 1, paladin 2, hunter 3, rogue 4, priest 5, shaman 7,
// mage 8, warlock 9, druid 11). At most 5 characters: with " 31/20/0" plus
// two " XX300" tokens every spec fits the 31-char cap. Unknown classes and
// tabs fall back to "Spec".
inline char const* PublicNoteSpecLabel(uint32_t classId, uint32_t tab)
{
    switch (classId)
    {
        case 11: // druid
            switch (tab)
            {
                case 0: return "Bal";
                case 1: return "Feral";
                case 2: return "Resto";
                default: break;
            }
            break;
        case 3: // hunter
            switch (tab)
            {
                case 0: return "BM";
                case 1: return "MM";
                case 2: return "Surv";
                default: break;
            }
            break;
        case 8: // mage
            switch (tab)
            {
                case 0: return "Arc";
                case 1: return "Fire";
                case 2: return "Frost";
                default: break;
            }
            break;
        case 2: // paladin
            switch (tab)
            {
                case 0: return "Holy";
                case 1: return "Prot";
                case 2: return "Ret";
                default: break;
            }
            break;
        case 5: // priest
            switch (tab)
            {
                case 0: return "Disc";
                case 1: return "Holy";
                case 2: return "Shadow";
                default: break;
            }
            break;
        case 4: // rogue
            switch (tab)
            {
                case 0: return "Assn";
                case 1: return "Combat";
                case 2: return "Sub";
                default: break;
            }
            break;
        case 7: // shaman
            switch (tab)
            {
                case 0: return "Elem";
                case 1: return "Enh";
                case 2: return "Resto";
                default: break;
            }
            break;
        case 9: // warlock
            switch (tab)
            {
                case 0: return "Affl";
                case 1: return "Demo";
                case 2: return "Destro";
                default: break;
            }
            break;
        case 1: // warrior
            switch (tab)
            {
                case 0: return "Arms";
                case 1: return "Fury";
                case 2: return "Prot";
                default: break;
            }
            break;
        default:
            break;
    }
    return "Spec";
}

inline std::string ComposePublicNote(uint32_t classId, uint32_t tab, int tree0, int tree1, int tree2,
    PublicNoteProfession const* professions, uint32_t professionCount)
{
    std::string note = PublicNoteSpecLabel(classId, tab);
    note += " " + std::to_string(tree0) + "/" + std::to_string(tree1) + "/" + std::to_string(tree2);

    uint32_t appended = 0;
    for (uint32_t i = 0; i < professionCount && appended < 2; ++i)
    {
        char const* abbr = PublicNoteProfessionAbbreviation(professions[i].skillId);
        if (!abbr)
            continue;
        std::string token = std::string(abbr) + std::to_string(professions[i].skillValue);
        if (note.size() + 1 + token.size() > kPublicNoteMaxLength)
            break;
        note += " " + token;
        ++appended;
    }
    if (note.size() > kPublicNoteMaxLength)
        note.resize(kPublicNoteMaxLength);
    return note;
}

// Last-seen note inputs, so the throttled tick recomputes the text only when
// something it shows actually changed. Talent trees and the two profession
// pairs shown; skill ids 0 when the bot holds fewer than two primaries.
struct PublicNoteInputs
{
    uint32_t classId = 0;
    uint32_t tab = 0;
    int tree0 = 0;
    int tree1 = 0;
    int tree2 = 0;
    uint32_t skillId0 = 0;
    uint32_t skillValue0 = 0;
    uint32_t skillId1 = 0;
    uint32_t skillValue1 = 0;

    bool operator==(PublicNoteInputs const& other) const
    {
        return classId == other.classId && tab == other.tab &&
            tree0 == other.tree0 && tree1 == other.tree1 && tree2 == other.tree2 &&
            skillId0 == other.skillId0 && skillValue0 == other.skillValue0 &&
            skillId1 == other.skillId1 && skillValue1 == other.skillValue1;
    }

    bool operator!=(PublicNoteInputs const& other) const { return !(*this == other); }
};

// True when the throttled skill-driven refresh is due. Unsigned arithmetic
// keeps the comparison correct across clock wraps. lastRefreshMs == 0 means
// never refreshed: due, so the first pass seeds the baseline.
inline bool PublicNoteRefreshDue(uint32_t lastRefreshMs, uint32_t nowMs)
{
    if (lastRefreshMs == 0)
        return true;
    return nowMs - lastRefreshMs >= kPublicNoteSkillRefreshSec * 1000;
}

} // namespace TortoiseBots
