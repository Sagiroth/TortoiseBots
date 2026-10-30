// The COMMANDER_POS wire contract: the field names and JSON types of the datagram the module emits are pinned to the
// ones the observability daemon reads (CommanderPosPayload, PosBot and PosMob), so a field the daemon would silently
// ignore, or a type it cannot unmarshal, fails here instead of on the dashboard. Its integer fields take only integer
// literals (no '.', 'e' or 'E'), and its float fields take a number of either shape.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra -Werror=return-type tools/test_commander_pos_contract.cpp -o /tmp/test_commander_pos_contract
//   /tmp/test_commander_pos_contract

#include "../runtime/CommanderTelemetry.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using namespace TortoiseBots::CommanderTelemetry;

namespace {

int g_failures = 0;

void Check(bool ok, std::string const& what)
{
    if (!ok)
    {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        ++g_failures;
    }
}

struct Value
{
    enum Kind { Object, Array, String, Number, Boolean, Null } kind = Null;
    std::map<std::string, Value> object;
    std::vector<Value> array;
    std::string text; // a string's decoded contents; a number's literal text
    bool boolean = false;
};

class Parser
{
public:
    explicit Parser(std::string const& text) : m_text(text) {}

    Value Parse()
    {
        Value value = ValueAt();
        SkipSpace();
        if (m_pos != m_text.size())
            throw std::runtime_error("trailing text at offset " + std::to_string(m_pos));
        return value;
    }

private:
    void SkipSpace()
    {
        while (m_pos < m_text.size() && std::isspace(static_cast<unsigned char>(m_text[m_pos])))
            ++m_pos;
    }

    char Peek()
    {
        SkipSpace();
        if (m_pos >= m_text.size())
            throw std::runtime_error("unexpected end");
        return m_text[m_pos];
    }

    bool Consume(char c)
    {
        if (Peek() != c)
            return false;
        ++m_pos;
        return true;
    }

    void Expect(char c)
    {
        if (!Consume(c))
            throw std::runtime_error(std::string("expected '") + c + "' at offset " + std::to_string(m_pos));
    }

    void Literal(char const* word)
    {
        for (char const* p = word; *p; ++p)
            Expect(*p);
    }

    std::string StringText()
    {
        Expect('"');
        std::string out;
        for (;;)
        {
            if (m_pos >= m_text.size())
                throw std::runtime_error("unterminated string");
            char const c = m_text[m_pos++];
            if (c == '"')
                return out;
            if (c != '\\')
            {
                out += c;
                continue;
            }
            if (m_pos >= m_text.size())
                throw std::runtime_error("unterminated escape");
            char const escape = m_text[m_pos++];
            switch (escape)
            {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u':
                if (m_pos + 4 > m_text.size())
                    throw std::runtime_error("short \\u escape");
                m_pos += 4;
                out += '?'; // the payload never carries one; only its shape has to parse
                break;
            default:
                throw std::runtime_error(std::string("bad escape '\\") + escape + "'");
            }
        }
    }

    std::string NumberText()
    {
        size_t const start = m_pos;
        if (m_pos < m_text.size() && m_text[m_pos] == '-')
            ++m_pos;
        size_t const integer = m_pos;
        while (m_pos < m_text.size() && std::isdigit(static_cast<unsigned char>(m_text[m_pos])))
            ++m_pos;
        if (m_pos == integer)
            throw std::runtime_error("bad number at offset " + std::to_string(start));
        if (m_pos < m_text.size() && m_text[m_pos] == '.')
        {
            ++m_pos;
            size_t const fraction = m_pos;
            while (m_pos < m_text.size() && std::isdigit(static_cast<unsigned char>(m_text[m_pos])))
                ++m_pos;
            if (m_pos == fraction)
                throw std::runtime_error("bad fraction at offset " + std::to_string(start));
        }
        if (m_pos < m_text.size() && (m_text[m_pos] == 'e' || m_text[m_pos] == 'E'))
        {
            ++m_pos;
            if (m_pos < m_text.size() && (m_text[m_pos] == '+' || m_text[m_pos] == '-'))
                ++m_pos;
            size_t const exponent = m_pos;
            while (m_pos < m_text.size() && std::isdigit(static_cast<unsigned char>(m_text[m_pos])))
                ++m_pos;
            if (m_pos == exponent)
                throw std::runtime_error("bad exponent at offset " + std::to_string(start));
        }
        return m_text.substr(start, m_pos - start);
    }

    Value ObjectValue()
    {
        Value value;
        value.kind = Value::Object;
        Expect('{');
        if (Consume('}'))
            return value;
        for (;;)
        {
            SkipSpace();
            std::string const key = StringText();
            Expect(':');
            value.object.emplace(key, ValueAt());
            if (Consume('}'))
                return value;
            Expect(',');
        }
    }

    Value ArrayValue()
    {
        Value value;
        value.kind = Value::Array;
        Expect('[');
        if (Consume(']'))
            return value;
        for (;;)
        {
            value.array.push_back(ValueAt());
            if (Consume(']'))
                return value;
            Expect(',');
        }
    }

    Value ValueAt()
    {
        switch (Peek())
        {
        case '{': return ObjectValue();
        case '[': return ArrayValue();
        case '"': { Value value; value.kind = Value::String; value.text = StringText(); return value; }
        case 't': { Literal("true"); Value value; value.kind = Value::Boolean; value.boolean = true; return value; }
        case 'f': {
            Literal("false");
            Value value;
            value.kind = Value::Boolean;
            return value;
        }
        case 'n': Literal("null"); return Value();
        default: {
            Value value;
            value.kind = Value::Number;
            value.text = NumberText();
            return value;
        }
        }
    }

    std::string const& m_text;
    size_t m_pos = 0;
};

char const* KindName(Value::Kind kind)
{
    switch (kind)
    {
    case Value::Object: return "an object";
    case Value::Array: return "an array";
    case Value::String: return "a string";
    case Value::Number: return "a number";
    case Value::Boolean: return "a boolean";
    case Value::Null: break;
    }
    return "null";
}

Value const* Field(Value const& value, std::string const& key)
{
    auto const it = value.object.find(key);
    return it == value.object.end() ? nullptr : &it->second;
}

void ExpectKind(Value const* value, Value::Kind kind, std::string const& what)
{
    Check(value != nullptr, what + ": present");
    if (value)
        Check(value->kind == kind, what + ": " + KindName(kind));
}

// The daemon's integer fields are uint32/uint64/int64; Go's decoder rejects a literal with a fraction or an exponent.
void ExpectInteger(Value const* value, std::string const& what)
{
    ExpectKind(value, Value::Number, what);
    if (value && value->kind == Value::Number)
        Check(value->text.find_first_of(".eE") == std::string::npos, what + ": an integer literal");
}

void ExpectNumber(Value const* value, std::string const& what)
{
    ExpectKind(value, Value::Number, what);
}

void ExpectKeys(Value const& value, std::initializer_list<char const*> keys, std::string const& what)
{
    std::set<std::string> const expected(keys.begin(), keys.end());
    std::set<std::string> actual;
    for (auto const& entry : value.object)
        actual.insert(entry.first);
    if (actual == expected)
        return;
    Check(false, what + ": exactly the fields the daemon reads");
    for (auto const& key : actual)
        if (!expected.count(key))
            std::fprintf(stderr, "    unexpected field '%s'\n", key.c_str());
    for (auto const& key : expected)
        if (!actual.count(key))
            std::fprintf(stderr, "    missing field '%s'\n", key.c_str());
}

PosRef SampleRef()
{
    PosRef ref;
    ref.commander = "Glacienne";
    ref.commanderGuid = 12;
    ref.combat = true;

    PosBot bot;
    bot.g = 4021;
    bot.map = 249;
    bot.x = -12.34f;
    bot.y = 45.65f;
    bot.z = -80.14f;
    bot.o = 3.14159f;
    bot.hp = 5321;
    bot.maxHp = 6000;
    bot.alive = true;
    ref.bots.push_back(bot);
    bot.g = 4022;
    bot.alive = false;
    bot.hp = 0;
    ref.bots.push_back(bot);

    PosMob mob;
    mob.id = 17370386927380123ull;
    mob.x = -20.14f;
    mob.y = 40.26f;
    mob.hpPct = 87;
    mob.victim = 4021;
    ref.mobs.push_back(mob);
    mob.id = 5;
    mob.x = 1.0f;
    mob.y = 2.0f;
    mob.hpPct = 60;
    mob.victim = 0;
    ref.mobs.push_back(mob);
    return ref;
}

void ExpectBotRow(Value const& row, int index)
{
    std::string const what = "bot row " + std::to_string(index);
    ExpectKeys(row, {"g", "m", "x", "y", "z", "o", "hp", "mhp", "a"}, what);
    for (char const* key : {"g", "m", "hp", "mhp", "a"})
        ExpectInteger(Field(row, key), what + "." + key);
    for (char const* key : {"x", "y", "z", "o"})
        ExpectNumber(Field(row, key), what + "." + key);
    Value const* const alive = Field(row, "a");
    if (alive && alive->kind == Value::Number)
        Check(alive->text == (index == 0 ? "1" : "0"), what + ".a: 1 alive and 0 dead");
}

void ExpectMobRow(Value const& row, int index)
{
    std::string const what = "mob row " + std::to_string(index);
    ExpectKeys(row, {"id", "x", "y", "hp", "v"}, what);
    ExpectInteger(Field(row, "hp"), what + ".hp");
    ExpectNumber(Field(row, "x"), what + ".x");
    ExpectNumber(Field(row, "y"), what + ".y");
    Value const* const id = Field(row, "id");
    ExpectKind(id, Value::String, what + ".id");
    if (id && id->kind == Value::String)
        Check(id->text == (index == 0 ? "17370386927380123" : "5"), what + ".id: the raw guid as a decimal string");
    Value const* const victim = Field(row, "v");
    ExpectKind(victim, Value::String, what + ".v");
    if (victim && victim->kind == Value::String)
        Check(victim->text == (index == 0 ? "4021" : ""), what + ".v: the victim's guid, empty for none");
}

void TestWireContract()
{
    PosRef const ref = SampleRef();
    std::string const json = BuildPosPayload(12, 77, 4412, 1793210001, 1793210001500, ref);
    Value const root = Parser(json).Parse();

    ExpectKeys(root, {"v", "fmt", "session", "seq", "ts", "ts_ms", "type", "commander", "commander_guid", "combat",
                      "bots", "mobs"}, "the datagram");
    for (char const* key : {"v", "fmt", "session", "seq", "ts", "ts_ms", "commander_guid"})
        ExpectInteger(Field(root, key), key);
    ExpectKind(Field(root, "commander"), Value::String, "commander");
    ExpectKind(Field(root, "combat"), Value::Boolean, "combat");
    ExpectKind(Field(root, "bots"), Value::Array, "bots");
    ExpectKind(Field(root, "mobs"), Value::Array, "mobs");

    Value const* const type = Field(root, "type");
    if (type && type->kind == Value::String)
        Check(type->text == "COMMANDER_POS", "type is the name the daemon switches on");
    Value const* const fmt = Field(root, "fmt");
    if (fmt && fmt->kind == Value::Number)
        Check(fmt->text == "1", "fmt 1: the only layout the daemon merges");
    Value const* const combat = Field(root, "combat");
    if (combat && combat->kind == Value::Boolean)
        Check(combat->boolean, "combat carries the group's state");

    Value const* const bots = Field(root, "bots");
    if (bots && bots->kind == Value::Array)
    {
        Check(bots->array.size() == ref.bots.size(), "one row per bot");
        if (bots->array.size() == ref.bots.size())
            for (size_t i = 0; i < bots->array.size(); ++i)
                ExpectBotRow(bots->array[i], static_cast<int>(i));
    }

    Value const* const mobs = Field(root, "mobs");
    if (mobs && mobs->kind == Value::Array)
    {
        Check(mobs->array.size() == ref.mobs.size(), "one row per enemy");
        if (mobs->array.size() == ref.mobs.size())
            for (size_t i = 0; i < mobs->array.size(); ++i)
                ExpectMobRow(mobs->array[i], static_cast<int>(i));
    }
}

void TestEmptyLists()
{
    PosRef ref;
    ref.commander = "Glacienne";
    std::string const json = BuildPosPayload(12, 1, 2, 3, 4, ref);
    Value const root = Parser(json).Parse();
    Value const* const bots = Field(root, "bots");
    Value const* const mobs = Field(root, "mobs");
    Check(bots && bots->kind == Value::Array && bots->array.empty(), "an empty bots list is still sent");
    Check(mobs && mobs->kind == Value::Array && mobs->array.empty(), "an empty mobs list is still sent");
}

} // namespace

int main()
{
    try
    {
        TestWireContract();
        TestEmptyLists();
    }
    catch (std::exception const& error)
    {
        std::fprintf(stderr, "FAIL: the payload is not JSON: %s\n", error.what());
        return EXIT_FAILURE;
    }
    if (g_failures)
    {
        std::fprintf(stderr, "test_commander_pos_contract: %d failure(s)\n", g_failures);
        return EXIT_FAILURE;
    }
    std::printf("test_commander_pos_contract: passed\n");
    return EXIT_SUCCESS;
}
