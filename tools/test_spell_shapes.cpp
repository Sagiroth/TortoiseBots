// Standalone tests for runtime/SpellShape.h and the committed table in runtime/SpellShapeData.h: what parses, what is
// dropped, the rows the dashboard's boss overlay relies on (Flame Breath, Tail Sweep, the Deep Breath lanes), and that an
// unknown spell has no shape.
//
// The Deep Breath pins are the core's own chain data, not the module's stand anchors: every row's two ends are the first
// and the last point of that spell's tw_world.spell_target_position chain, as the block at the end of
// src/scripts/dungeons/onyxias_lair/boss_onyxia.cpp (~/tortoise-wow) writes it. A lane that runs on past the room's
// hover point keeps its tunnel tail.
//
// Standard library only, no game header.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra -Werror=return-type tools/test_spell_shapes.cpp -o /tmp/test_spell_shapes
//   /tmp/test_spell_shapes

#include "../runtime/SpellShape.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_set>
#include <vector>

using namespace TortoiseBots::SpellShape;

namespace {

int g_failures = 0;

void Check(bool ok, char const* what)
{
    if (!ok)
    {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

bool Near(float a, float b)
{
    float const d = a - b;
    return d > -0.05f && d < 0.05f;
}

// One line of a table, as the parser reads it (a fresh table each call, so a bad row cannot hide behind a good one).
size_t ParsedRows(std::string const& line)
{
    return Parse(line).size();
}

// One Deep Breath lane as the core's chain block writes it: the first and the last damage point, and the distance
// between them.
struct Lane
{
    uint32_t spell;
    float x0, y0, x1, y1;
    float length;
};

// Every lane head and its chain's first and last point (spell_target_position, map 249). 17086 is the one that leaves
// the room: its chain runs from the north point's head into the tunnel (20 links), which is why it is the longest.
Lane const kLanes[] = {
    {17086, 20.730539f, -215.237610f, -130.788300f, -213.424026f, 151.53f},
    {18351, -68.834236f, -215.036163f, 20.730539f, -215.237610f, 89.565f},
    {18576, -37.743851f, -243.667923f, -37.728523f, -188.616806f, 55.051f},
    {18609, -37.728523f, -188.616806f, -37.743851f, -243.667923f, 55.051f},
    {18564, -56.559654f, -241.223923f, 6.016711f, -181.305771f, 86.637f},
    {18584, 6.016711f, -181.305771f, -56.559654f, -241.223923f, 86.637f},
    {18596, -58.250900f, -189.020004f, 12.120926f, -243.439407f, 88.959f},
    {18617, 12.120926f, -243.439407f, -58.250900f, -189.020004f, 88.959f},
};

void TestCommittedTable()
{
    std::vector<Row> const& table = Table();
    Check(table.size() == 18, "the committed table has every row");
    std::unordered_set<uint32_t> ids;
    for (Row const& row : table)
    {
        Check(ids.insert(row.shape.spell).second, "a spell id appears once");
        Check(row.shape.kind != Kind::None, "every row carries a shape");
        Check(!row.source.empty(), "every row says where its numbers came from");
        if (row.shape.kind == Kind::Line)
            Check(row.shape.angle == 0 && row.shape.length > 0.0f && row.shape.width > 0.0f,
                "a line has no arc, a reach and a width");
        else
            Check(row.shape.angle > 0 && row.shape.angle < 360 && row.shape.length > 0.0f && row.shape.width == 0.0f,
                "a cone has an arc and a reach and no width");
        Check(row.shape.kind == Kind::Line ? row.shape.hasSegment : !row.shape.hasSegment,
            "the committed lines carry their two ends and the cones do not");
    }
}

void TestKnownShapes()
{
    Shape const* flame = For(18435);
    Check(flame && flame->kind == Kind::Cone && flame->angle == 120 && Near(flame->length, 45.0f) && flame->width == 0.0f,
        "Flame Breath is a 120 degree cone of 45 yd");
    Check(flame && !flame->hasSegment, "a cone has no absolute ends");
    Shape const* sweep = For(15847);
    Check(sweep && sweep->kind == Kind::BackCone && sweep->angle == 75 && Near(sweep->length, 30.0f),
        "Tail Sweep is a 75 degree back cone of 30 yd");
    Shape const* buffet = For(18500);
    Check(buffet && buffet->kind == Kind::Cone && Near(buffet->length, 20.0f), "Wing Buffet is a 20 yd front cone");
    Shape const* lava = For(19272);
    Check(lava && lava->kind == Kind::Cone && lava->angle == 90 && Near(lava->length, 35.0f),
        "a target-54 cone is the core's 90 degree arc (Lava Breath, 35 yd)");
    Shape const* glare = For(26029);
    Check(glare && glare->kind == Kind::Cone && glare->angle == 15 && Near(glare->length, 100.0f) && glare->width == 0.0f,
        "Dark Glare is the core's 15 degree front cone of 100 yd (spellVisual 7441)");
    Shape const* gyth = For(16390);
    Check(gyth && gyth->kind == Kind::Cone && gyth->angle == 120 && Near(gyth->length, 20.0f) && gyth->width == 0.0f,
        "Gyth's Flame Breath is a cast-time target-24 cone of 20 yd (radius 9, default front 120)");
}

// Every Deep Breath row is the complete damaging footprint of its chain: a line from the first to the last damage point,
// 30 yd of effect either side. The stand anchors are not the source (they are where the bots stand: the lane continues
// past them, and 17086's chain runs 65 yd further into the tunnel).
void TestDeepBreathLanes()
{
    for (Lane const& lane : kLanes)
    {
        Shape const* shape = For(lane.spell);
        Check(shape && shape->kind == Kind::Line, "every Deep Breath lane head is a line");
        if (!shape || shape->kind != Kind::Line)
            continue;
        Check(shape->angle == 0, "a lane has no arc");
        Check(Near(shape->width, 60.0f), "a lane is 60 yd wide (radius index 10 = 30 yd each side)");
        Check(shape->hasSegment, "a lane carries the two ends of its chain");
        Check(Near(shape->x0, lane.x0) && Near(shape->y0, lane.y0), "a lane starts at its chain's first damage point");
        Check(Near(shape->x1, lane.x1) && Near(shape->y1, lane.y1), "a lane ends at its chain's last damage point");
        float const dx = shape->x1 - shape->x0;
        float const dy = shape->y1 - shape->y0;
        Check(Near(shape->length, std::sqrt(dx * dx + dy * dy)), "a lane's reach is the distance between its ends");
        Check(Near(shape->length, lane.length), "a lane's reach is its chain's length");
    }
    Shape const* north = For(17086);
    Check(north && Near(north->length, 151.53f),
        "the north-to-south lane keeps its 151.53 yd damaging chain into the tunnel");
    Check(north && Near(north->x1, -130.788300f), "the north-to-south lane ends at its last tunnel point");
}

void TestUnknownSpells()
{
    Check(For(1337) == nullptr, "an unknown spell has no shape");
    Check(For(0) == nullptr, "spell 0 has no shape");
    Check(For(19983) == nullptr, "a single-target cleave is not a cone");
}

void TestParserRejects()
{
    Check(ParsedRows("") == 0, "an empty table has no rows");
    Check(ParsedRows("# a comment\n\n") == 0, "comments and blank lines are skipped");
    Check(ParsedRows("18435,cone,120,45,0,0,0,0,0,ok") == 1, "a valid row parses");
    Check(ParsedRows("18435,circle,120,45,0,0,0,0,0,src") == 0, "an unknown shape word is dropped");
    Check(ParsedRows("18435,cone,120,45,0,0,0,0") == 0, "a row without a source column is dropped");
    Check(ParsedRows("x,cone,120,45,0,0,0,0,0,src") == 0, "a spell that is not a number is dropped");
    Check(ParsedRows("0,cone,120,45,0,0,0,0,0,src") == 0, "spell 0 is dropped");
    Check(ParsedRows("18435,cone,-120,45,0,0,0,0,0,src") == 0, "a negative angle is dropped");
    Check(ParsedRows("18435,cone,120,45x,0,0,0,0,0,src") == 0, "trailing junk after a number is dropped");
    Check(ParsedRows("18435,cone,120,45,0,0,0,0,0,src\n999,cone,360,45,0,0,0,0,0,src") == 1,
        "a bad row does not take a good one with it");
    Check(ParsedRows("18435,cone,120,45,5,0,0,0,0,src") == 0, "a cone with a width is dropped");
    Check(ParsedRows("18435,cone,120,45,0,1,2,3,4,src") == 0, "a cone with absolute ends is dropped (it faces the caster)");
    Check(ParsedRows("18435,line,120,45,60,0,0,0,0,src") == 0, "a line with an arc is dropped");
    Check(ParsedRows("18435,line,0,45,0,0,0,0,0,src") == 0, "a line without a width is dropped");
    Check(ParsedRows("18435,line,0,0,60,0,0,0,0,src") == 0, "a line without a reach and without ends is dropped");
    Check(ParsedRows("18435,cone,120,0,0,0,0,0,0,src") == 0, "a cone without a reach is dropped");
    Check(ParsedRows("18435,line,0,45,60,1,2,0,0,src") == 0, "a line with half a segment is dropped");
    Check(ParsedRows("18435,line,0,45,60,1,2,3,4,src") == 0, "a line with a segment and a reach (two lengths) is dropped");
    Check(ParsedRows("18435,line,0,0,60,0,0,0,0,src") == 0, "a degenerate segment is dropped");
    std::vector<Row> const mixed = Parse("18435,cone,120,45,0,0,0,0,0,first\n18435,back-cone,75,30,0,0,0,0,0,second\n");
    Check(mixed.size() == 2 && mixed[0].shape.kind == Kind::Cone && mixed[1].shape.kind == Kind::BackCone,
        "a repeated spell keeps both parsed rows (the committed table has no repeats)");
    std::vector<Row> const lane = Parse("17086,line,0,0,60,20.5,-215.25,-130.75,-213.5,spell_target_position 17086 chain, map 249\n");
    Check(lane.size() == 1 && lane[0].shape.hasSegment && Near(lane[0].shape.length, 151.25f) &&
              lane[0].source == "spell_target_position 17086 chain,map 249",
        "a lane's reach is computed from its ends and a comma in the source survives");
}

void TestNames()
{
    Check(std::string(KindName(Kind::Cone)) == "cone", "cone is named on the wire");
    Check(std::string(KindName(Kind::BackCone)) == "back-cone", "a back cone is named on the wire");
    Check(std::string(KindName(Kind::Line)) == "line", "a line is named on the wire");
    Check(std::string(KindName(Kind::None)).empty(), "no shape has no name");
    Kind parsed = Kind::None;
    Check(ParseKind("back-cone", parsed) && parsed == Kind::BackCone, "back-cone parses");
    Check(!ParseKind("back", parsed), "an unknown word does not parse");
}

} // namespace

int main()
{
    TestCommittedTable();
    TestKnownShapes();
    TestDeepBreathLanes();
    TestUnknownSpells();
    TestParserRejects();
    TestNames();
    if (g_failures)
    {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }
    std::puts("test_spell_shapes: all checks passed");
    return EXIT_SUCCESS;
}
