// Standalone tests for the remote commander's observability payload: the
// bounded order log, the clipping of free text, and the COMMANDERS datagram
// (fields, escaping, caps, size, the empty list that ends the last binding).
//
// The header includes the standard library only, so this file compiles it
// without a game header: it can only format what it is handed.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_commander_telemetry.cpp -o /tmp/test_commander_telemetry
//   /tmp/test_commander_telemetry

#include "../runtime/CommanderTelemetry.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_set>
#include <vector>

using namespace TortoiseBots::CommanderTelemetry;

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

bool Has(std::string const& hay, std::string const& needle)
{
    return hay.find(needle) != std::string::npos;
}

OrderRecord Order(int64_t at, std::string verb, bool ok, std::string result, std::string detail = "")
{
    OrderRecord r;
    r.at = at;
    r.verb = std::move(verb);
    r.ok = ok;
    r.result = std::move(result);
    r.detail = std::move(detail);
    return r;
}

BotInfo Bot(std::string name, bool lead)
{
    BotInfo b;
    b.name = std::move(name);
    b.guid = 700;
    b.className = "warrior";
    b.role = "tank";
    b.level = 46;
    b.hp = 2706;
    b.maxHp = 2706;
    b.power = 40;
    b.maxPower = 100;
    b.powerType = "rage";
    b.lead = lead;
    b.mapId = 209;
    b.zoneId = 1176;
    b.x = 1212.74f;
    b.y = 842.0f;
    b.z = 8.9f;
    return b;
}

void TestClip()
{
    Check(Clip("abc", 10) == "abc", "short text is kept");
    Check(Clip("abcdef", 3) == "abc", "long text is cut");
    // U+00E9 is two bytes; a cut between them must back up to the character start.
    std::string const accented = "ab\xC3\xA9" "cd";
    Check(Clip(accented, 3) == "ab", "a cut inside a multi-byte character backs up");
    Check(Clip(accented, 4) == "ab\xC3\xA9", "a cut after a whole character stays");
    Check(Clip("", 0).empty(), "empty stays empty");
}

void TestOrderLogRing()
{
    OrderLog log;
    for (int i = 0; i < 20; ++i)
        log.Note(1, Order(100 + i, "move", true, "party"));
    std::vector<OrderRecord> const recent = log.Recent(1);
    Check(recent.size() == kMaxOrdersPerCommander, "the ring keeps only the last orders");
    Check(recent.front().at == 112 && recent.back().at == 119, "oldest first, newest last");
    Check(log.Recent(2).empty(), "an unknown commander has no orders");
    log.Note(0, Order(1, "move", true, "x"));
    Check(log.Commanders() == 1, "commander 0 is not a commander");
    OrderRecord clipped = Order(1, "attack", false, std::string(500, 'r'), std::string(500, 'd'));
    log.Note(1, clipped);
    Check(log.Recent(1).back().detail.size() == kMaxOrderDetail, "detail is clipped");
    Check(log.Recent(1).back().result.size() == kMaxOrderResult, "result is clipped");
}

void TestOrderLogBoundsCommanders()
{
    OrderLog log;
    for (uint64_t c = 1; c <= kMaxTrackedCommanders + 5; ++c)
        log.Note(c, Order(static_cast<int64_t>(c), "stay", true, "party"));
    Check(log.Commanders() == kMaxTrackedCommanders, "the number of tracked commanders is bounded");
    Check(log.Recent(1).empty() && log.Recent(5).empty(), "the commanders noted longest ago are dropped");
    Check(!log.Recent(kMaxTrackedCommanders + 5).empty(), "the newest commander is kept");
    // A commander that is already tracked never evicts another.
    log.Note(kMaxTrackedCommanders + 5, Order(99, "follow", true, "party"));
    Check(log.Commanders() == kMaxTrackedCommanders, "a known commander adds no entry");
    // Retain drops released bindings and keeps the arrival order consistent.
    std::unordered_set<uint64_t> keep{kMaxTrackedCommanders + 5};
    log.Retain(keep);
    Check(log.Commanders() == 1, "retain forgets released commanders");
    for (uint64_t c = 100; c < 100 + kMaxTrackedCommanders; ++c)
        log.Note(c, Order(1, "stay", true, "party"));
    Check(log.Commanders() == kMaxTrackedCommanders, "after retain the bound still holds");
    log.Clear();
    Check(log.Commanders() == 0, "clear empties the log");
}

void TestPayload()
{
    BindingInfo b;
    b.commander = "Zfmagea";
    b.commanderGuid = 4242;
    b.commanderOnline = true;
    b.viewOn = true;
    b.groupId = 42;
    b.lead = "Forthur";
    b.bots.push_back(Bot("Forthur", true));
    b.bots.push_back(Bot("Pu\"vur", false));
    b.orders.push_back(Order(1791990000, "move", true, "party", "1517 833 9"));
    b.orders.push_back(Order(1791990005, "attack", false, "invalid-target: No unit\nwith that GUID", "0xF13000"));
    std::string const json = BuildPayload(9, 77, 5, 1791990010, {b});

    Check(Has(json, "\"v\":9"), "carries the protocol version");
    Check(Has(json, "\"session\":77") && Has(json, "\"seq\":5") && Has(json, "\"ts\":1791990010"), "carries session, seq and ts");
    Check(Has(json, "\"type\":\"COMMANDERS\""), "has its type");
    Check(Has(json, "\"commander\":\"Zfmagea\"") && Has(json, "\"online\":true") && Has(json, "\"view\":true") && Has(json, "\"group\":42"), "binding fields");
    Check(Has(json, "\"commander_guid\":4242"), "the commander's guid, for the dashboard's detail link");
    Check(Has(json, "\"lead\":\"Forthur\""), "lead bot name");
    Check(Has(json, "\"class\":\"warrior\"") && Has(json, "\"role\":\"tank\"") && Has(json, "\"power_type\":\"rage\""), "bot class, role, power type");
    Check(Has(json, "\"hp\":2706") && Has(json, "\"max_hp\":2706") && Has(json, "\"power\":40") && Has(json, "\"max_power\":100"), "bot vitals");
    Check(Has(json, "\"map\":209") && Has(json, "\"zone\":1176") && Has(json, "\"x\":1212.7") && Has(json, "\"y\":842.0"), "bot place");
    Check(Has(json, "\"alive\":true") && Has(json, "\"lead\":true") && Has(json, "\"lead\":false"), "bot flags");
    Check(Has(json, "Pu\\\"vur"), "quotes in a name are escaped");
    Check(Has(json, "\"verb\":\"move\"") && Has(json, "\"ok\":true") && Has(json, "\"result\":\"party\""), "an acknowledged order");
    Check(Has(json, "\"ok\":false") && Has(json, "invalid-target: No unit\\nwith that GUID"), "a refused order, newline escaped");
    Check(json.find('\n') == std::string::npos, "the datagram is one line");

    // The empty list is a valid payload.
    std::string const empty = BuildPayload(9, 77, 6, 1, {});
    Check(Has(empty, "\"commanders\":[]}"), "an empty list ends the bindings");
}

void TestPayloadCapsAndSize()
{
    BindingInfo b;
    b.commander = std::string(40, 'C');
    b.lead = std::string(12, 'L');
    for (int i = 0; i < 12; ++i)
        b.bots.push_back(Bot("Botname" + std::to_string(i), i == 0));
    for (int i = 0; i < 30; ++i)
        b.orders.push_back(Order(i, "ritual", i % 2 == 0, std::string(kMaxOrderResult, 'r'), std::string(kMaxOrderDetail, 'd')));
    std::vector<BindingInfo> many(kMaxBindings + 10, b);
    std::string const json = BuildPayload(9, 1, 1, 1, many);

    size_t commanders = 0;
    for (size_t pos = json.find("\"commander\":"); pos != std::string::npos; pos = json.find("\"commander\":", pos + 1))
        ++commanders;
    Check(commanders == kMaxBindings, "bindings are capped");
    size_t bots = 0;
    for (size_t pos = json.find("\"guid\":"); pos != std::string::npos; pos = json.find("\"guid\":", pos + 1))
        ++bots;
    Check(bots == kMaxBindings * kMaxBotsPerBinding, "bots per binding are capped");
    size_t orders = 0;
    for (size_t pos = json.find("\"verb\":"); pos != std::string::npos; pos = json.find("\"verb\":", pos + 1))
        ++orders;
    Check(orders == kMaxBindings * kMaxOrdersPerCommander, "orders per binding are capped");
    // One datagram: the worst case stays far below the 64 KiB UDP limit.
    Check(json.size() < 40000, "the worst case fits one datagram");
}

void TestControlCharacters()
{
    BindingInfo b;
    b.commander = std::string("a\x01" "b\tc");
    std::string const json = BuildPayload(9, 1, 1, 1, {b});
    Check(Has(json, "a\\u0001b\\tc"), "control characters are escaped");
}

} // namespace

int main()
{
    TestClip();
    TestOrderLogRing();
    TestOrderLogBoundsCommanders();
    TestPayload();
    TestPayloadCapsAndSize();
    TestControlCharacters();
    if (g_failures)
    {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }
    std::puts("test_commander_telemetry: all checks passed");
    return EXIT_SUCCESS;
}
