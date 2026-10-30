#pragma once

// The area a boss or elite spell points into, for the dashboard's boss overlay (fight debugger phase 2): which spells
// hit a cone in front, a cone behind or a line along the caster's facing, and how far and wide. The table itself is
// DATA (runtime/SpellShapeData.h, a committed CSV with a source per row); this header only parses it and looks a spell
// up. A spell absent from the table has no shape and the module sends none: a new raid is rows, never a branch.
//
// A line row may carry the segment it covers in absolute map coordinates (`x0,y0` to `x1,y1`) instead of a length from
// the caster: a boss's damaging lane does not start at its own feet, so the two ends are truth and the reach is their
// distance.
//
// Standard library only, so tools/test_spell_shapes.cpp compiles it without a game header.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "SpellShapeData.h"

namespace TortoiseBots {
namespace SpellShape {

enum class Kind : uint8_t
{
    None = 0,
    Cone,     // in front of the caster (the core's PUSH_IN_FRONT, 120 degrees by default)
    BackCone, // behind the caster (PUSH_IN_BACK, 75 degrees)
    Line,     // along the caster's facing, width wide, or between an absolute start and end
};

struct Shape
{
    uint32_t spell = 0;
    Kind kind = Kind::None;
    uint16_t angle = 0;  // full arc in degrees (cone and back-cone); 0 for a line
    float length = 0.0f; // reach in yards: along the facing, or the distance between the segment's ends
    float width = 0.0f;  // the band across a line in yards; 0 for a cone
    bool hasSegment = false; // the line's ends are known in map coordinates (x0,y0) to (x1,y1)
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
};

struct Row
{
    Shape shape;
    std::string source; // where the row's numbers were read (a table, a script, a spell id)
};

inline char const* KindName(Kind kind)
{
    switch (kind)
    {
    case Kind::Cone:
        return "cone";
    case Kind::BackCone:
        return "back-cone";
    case Kind::Line:
        return "line";
    case Kind::None:
    default:
        return "";
    }
}

inline bool ParseKind(std::string const& word, Kind& out)
{
    if (word == "cone")
        out = Kind::Cone;
    else if (word == "back-cone")
        out = Kind::BackCone;
    else if (word == "line")
        out = Kind::Line;
    else
        return false;
    return true;
}

inline std::string Trim(std::string const& s)
{
    size_t const first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return std::string();
    size_t const last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

// A whole decimal number or nothing: "-1" and "12x" are not numbers (a negative radius must not wrap).
inline bool ParseU32(std::string const& text, uint32_t& out)
{
    std::string const s = Trim(text);
    if (s.empty() || s[0] == '-')
        return false;
    char* end = nullptr;
    unsigned long const v = std::strtoul(s.c_str(), &end, 10);
    if (end != s.c_str() + s.size() || v > 0xFFFFFFFFul)
        return false;
    out = static_cast<uint32_t>(v);
    return true;
}

inline bool ParseFloat(std::string const& text, float& out)
{
    std::string const s = Trim(text);
    if (s.empty())
        return false;
    char* end = nullptr;
    float const v = std::strtof(s.c_str(), &end);
    if (end != s.c_str() + s.size())
        return false;
    out = v;
    return true;
}

// Parse the committed table: one row per line, blank lines and `#` comments skipped, columns
// `spell,shape,angle,reach,width,x0,y0,x1,y1[,source]`. A row that does not carry a usable shape is dropped whole
// (never half-read), so a typo removes one shape instead of drawing a wrong one. A cone needs angle 1..359, a reach, no
// width and no ends; a line needs no arc and a width, and either a reach along the facing or (all four columns set,
// reach 0) its two ends, whose distance becomes its reach.
inline std::vector<Row> Parse(std::string const& text)
{
    std::vector<Row> out;
    size_t at = 0;
    while (at <= text.size())
    {
        size_t const end = text.find('\n', at);
        std::string const line = Trim(text.substr(at, end == std::string::npos ? std::string::npos : end - at));
        at = end == std::string::npos ? text.size() + 1 : end + 1;
        if (line.empty() || line[0] == '#')
            continue;
        std::vector<std::string> fields;
        size_t start = 0;
        while (start <= line.size())
        {
            size_t const comma = line.find(',', start);
            fields.push_back(Trim(line.substr(start, comma == std::string::npos ? std::string::npos : comma - start)));
            if (comma == std::string::npos)
                break;
            start = comma + 1;
        }
        if (fields.size() < 9)
            continue;
        Row row;
        uint32_t spell = 0, angle = 0;
        Kind kind = Kind::None;
        float length = 0.0f, width = 0.0f, x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
        if (!ParseU32(fields[0], spell) || spell == 0 || !ParseKind(fields[1], kind) || !ParseU32(fields[2], angle) ||
            !ParseFloat(fields[3], length) || !ParseFloat(fields[4], width) || !ParseFloat(fields[5], x0) ||
            !ParseFloat(fields[6], y0) || !ParseFloat(fields[7], x1) || !ParseFloat(fields[8], y1))
            continue;
        float const stated = length;
        // None of the four ends, or all four: half a segment is a typo (and a lane end on a map axis is written by
        // shifting the chain's ends, never by leaving a column empty).
        unsigned const set = (x0 != 0.0f ? 1u : 0u) + (y0 != 0.0f ? 1u : 0u) + (x1 != 0.0f ? 1u : 0u) +
                             (y1 != 0.0f ? 1u : 0u);
        bool const ends = set == 4;
        if (set != 0 && set != 4)
            continue;
        if (kind == Kind::Line)
        {
            if (angle != 0 || !(width > 0.0f))
                continue;
            if (ends)
            {
                // The two ends are the line; the reach column stays 0 so the length has one source.
                float const dx = x1 - x0;
                float const dy = y1 - y0;
                length = std::sqrt(dx * dx + dy * dy);
                if (!(length > 0.0f) || stated != 0.0f)
                    continue;
            }
            else if (!(length > 0.0f))
                continue;
        }
        else if (angle == 0 || angle >= 360 || !(length > 0.0f) || width != 0.0f || ends)
            continue;
        row.shape.spell = spell;
        row.shape.kind = kind;
        row.shape.angle = static_cast<uint16_t>(angle);
        row.shape.length = length;
        row.shape.width = width;
        row.shape.hasSegment = ends;
        row.shape.x0 = x0;
        row.shape.y0 = y0;
        row.shape.x1 = x1;
        row.shape.y1 = y1;
        for (size_t i = 9; i < fields.size(); ++i)
        {
            if (i > 9)
                row.source += ',';
            row.source += fields[i];
        }
        out.push_back(std::move(row));
    }
    return out;
}

// The committed table, parsed once.
inline std::vector<Row> const& Table()
{
    static std::vector<Row> const table = Parse(kSpellShapeCsv);
    return table;
}

// The shape of a spell, or null when it has none (an unknown spell sends nothing).
inline Shape const* For(uint32_t spell)
{
    if (!spell)
        return nullptr;
    for (Row const& row : Table())
        if (row.shape.spell == spell)
            return &row.shape;
    return nullptr;
}

} // namespace SpellShape
} // namespace TortoiseBots
