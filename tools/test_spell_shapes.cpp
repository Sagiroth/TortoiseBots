// Standalone tests for runtime/SpellShape.h and the committed table in runtime/SpellShapeData.h: what parses, what is
// dropped, the rows the dashboard's boss overlay relies on (Flame Breath, Tail Sweep, the Deep Breath lanes), and that an
// unknown spell has no shape.
//
// Standard library only, no game header.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra -Werror=return-type tools/test_spell_shapes.cpp -o /tmp/test_spell_shapes
//   /tmp/test_spell_shapes

#include "../runtime/SpellShape.h"

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

void TestCommittedTable()
{
    std::vector<Row> const& table = Table();
    Check(table.size() == 16, "the committed table has every row");
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
    }
}

void TestKnownShapes()
{
    Shape const* flame = For(18435);
    Check(flame && flame->kind == Kind::Cone && flame->angle == 120 && Near(flame->length, 45.0f) && flame->width == 0.0f,
        "Flame Breath is a 120 degree cone of 45 yd");
    Shape const* sweep = For(15847);
    Check(sweep && sweep->kind == Kind::BackCone && sweep->angle == 75 && Near(sweep->length, 30.0f),
        "Tail Sweep is a 75 degree back cone of 30 yd");
    Shape const* buffet = For(18500);
    Check(buffet && buffet->kind == Kind::Cone && Near(buffet->length, 20.0f), "Wing Buffet is a 20 yd front cone");
    Shape const* lava = For(19272);
    Check(lava && lava->kind == Kind::Cone && lava->angle == 90 && Near(lava->length, 35.0f),
        "a target-54 cone is the core's 90 degree arc (Lava Breath, 35 yd)");
    Shape const* north = For(17086);
    Check(north && north->kind == Kind::Line && Near(north->length, 154.0f) && Near(north->width, 60.0f),
        "the north-to-south Deep Breath lane is a 154 yd line 60 yd wide");
    Shape const* east = For(18576);
    Check(east && east->kind == Kind::Line && Near(east->length, 62.0f), "the east-to-west lane is 62 yd long");
    for (uint32_t lane : {17086u, 18351u, 18576u, 18609u, 18564u, 18584u, 18596u, 18617u})
    {
        Shape const* shape = For(lane);
        Check(shape && shape->kind == Kind::Line, "every Deep Breath lane head has a line");
    }
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
    Check(ParsedRows("18435,cone,120,45,0,ok") == 1, "a valid row parses");
    Check(ParsedRows("18435,circle,120,45,0,src") == 0, "an unknown shape word is dropped");
    Check(ParsedRows("18435,cone,120,45") == 0, "a row without a width column is dropped");
    Check(ParsedRows("x,cone,120,45,0,src") == 0, "a spell that is not a number is dropped");
    Check(ParsedRows("0,cone,120,45,0,src") == 0, "spell 0 is dropped");
    Check(ParsedRows("18435,cone,-120,45,0,src") == 0, "a negative angle is dropped");
    Check(ParsedRows("18435,cone,120,45x,0,src") == 0, "trailing junk after a number is dropped");
    Check(ParsedRows("18435,cone,120,45,0,src\n999,cone,360,45,0,src") == 1, "a bad row does not take a good one with it");
    Check(ParsedRows("18435,cone,120,45,5,src") == 0, "a cone with a width is dropped");
    Check(ParsedRows("18435,line,120,45,60,src") == 0, "a line with an arc is dropped");
    Check(ParsedRows("18435,line,0,45,0,src") == 0, "a line without a width is dropped");
    Check(ParsedRows("18435,cone,120,0,0,src") == 0, "a cone without a reach is dropped");
    std::vector<Row> const mixed = Parse("18435,cone,120,45,0,first\n18435,back-cone,75,30,0,second\n");
    Check(mixed.size() == 2 && mixed[0].shape.kind == Kind::Cone && mixed[1].shape.kind == Kind::BackCone,
        "a repeated spell keeps both parsed rows (the committed table has no repeats)");
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
