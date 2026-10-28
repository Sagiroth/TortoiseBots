#pragma once

#include <cstdint>
#include <string>

// Guild public-note format: spec, talents and both primary professions with
// skill, e.g. "Arms 31/20/0 Min245 BS230".
//
// The client caps every guild note at 31 characters, and some spec names are
// long ("beast mastery"), so the note is built greedily: spec and talents
// always fit, then each "ABBR<skill>" token is appended only while the whole
// note still fits. A profession token is never cut in half; when the second
// profession would overflow, the note carries just the first.
//
// Pure formatting rules over plain values, unit-tested on their own
// (tools/test_public_note_policy.cpp). TalentSpec::SetPublicNote feeds this
// function the bot's spec name, talent trees and held primary professions.

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
// anything that is not a primary profession.
inline char const* PublicNoteProfessionAbbreviation(uint32_t skillId)
{
    switch (skillId)
    {
        case 171: return "Alc";   // Alchemy
        case 164: return "BS";    // Blacksmithing
        case 333: return "Enc";   // Enchanting
        case 202: return "Eng";   // Engineering
        case 182: return "Herb";  // Herbalism
        case 165: return "LW";    // Leatherworking
        case 186: return "Min";   // Mining
        case 393: return "Skin";  // Skinning
        case 197: return "Tail";  // Tailoring
        default:  return nullptr;
    }
}

inline std::string ComposePublicNote(std::string const& specName, int tree0, int tree1, int tree2,
    PublicNoteProfession const* professions, uint32_t professionCount)
{
    std::string note = specName;
    if (!note.empty() && note[0] >= 'a' && note[0] <= 'z')
        note[0] = char(note[0] - 'a' + 'A');
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
    return note;
}

// True when the throttled skill-driven refresh is due. Unsigned subtraction
// keeps the comparison correct across clock wraps.
inline bool PublicNoteRefreshDue(uint32_t lastRefreshSec, uint32_t nowSec)
{
    return nowSec - lastRefreshSec >= kPublicNoteSkillRefreshSec;
}

} // namespace TortoiseBots
