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
    Check(bots == kMaxBotsPerPayload, "bots are capped over the whole payload (a raid plus a party)");
    // One binding past the per-binding cap still writes only the cap.
    BindingInfo big = b;
    big.bots.clear();
    for (int i = 0; i < 50; ++i)
        big.bots.push_back(Bot("Botname" + std::to_string(i), i == 0));
    std::string const one = BuildPayload(9, 1, 1, 1, {big});
    size_t oneBots = 0;
    for (size_t pos = one.find("\"guid\":"); pos != std::string::npos; pos = one.find("\"guid\":", pos + 1))
        ++oneBots;
    Check(oneBots == kMaxBotsPerBinding, "bots per binding are capped");
    size_t orders = 0;
    for (size_t pos = json.find("\"verb\":"); pos != std::string::npos; pos = json.find("\"verb\":", pos + 1))
        ++orders;
    Check(orders == kMaxBindings * kMaxOrdersPerCommander, "orders per binding are capped");
    // One datagram: the worst case stays far below the 64 KiB UDP limit.
    Check(json.size() < 40000, "the worst case fits one datagram");
}

MobInfo Mob(uint64_t id, std::string name, char reaction = 'h', char kind = 0, uint64_t victim = 0)
{
    MobInfo m;
    m.id = id;
    m.entry = 4863;
    m.name = std::move(name);
    m.hpPct = 85;
    m.x = -1234.56f;
    m.y = 987.65f;
    m.victim = victim;
    m.reaction = reaction;
    m.kind = kind;
    return m;
}

size_t Count(std::string const& hay, std::string const& needle)
{
    size_t n = 0;
    for (size_t pos = hay.find(needle); pos != std::string::npos; pos = hay.find(needle, pos + 1))
        ++n;
    return n;
}

void TestMapData()
{
    BindingInfo b;
    b.commander = "Zfmagea";
    b.lead = "Forthur";
    BotInfo bot = Bot("Forthur", true);
    bot.id = 0x0000000000002A5Cull;
    bot.target = 0xF130001EDF000102ull; // a creature guid: does not fit a JSON number
    b.bots.push_back(bot);
    b.bots.push_back(Bot("Idle", false)); // no id, no target
    b.commanderPlaced = true;
    b.commanderMap = 209;
    b.commanderX = 1100.5f;
    b.commanderY = -50.5f;
    b.mobsReported = true;
    b.mobsMap = 209;
    b.mobs.push_back(Mob(0xF130001EDF000102ull, "Stonevault \"Rock\"chewer", 'h', 0, 0x2A5C));
    b.mobs.push_back(Mob(2, "Rat", 'n', 'k'));
    b.mobs.push_back(Mob(3, "Guard", 'f', 'g'));
    b.mobs.push_back(Mob(4, "Odd", 'x', 'z')); // unknown letters are cleaned
    std::string const json = BuildPayload(9, 1, 1, 1, {b});

    Check(Has(json, "\"commander_pos\":{\"map\":209,\"x\":1100.5,\"y\":-50.5}"), "the commander's position and map");
    Check(Has(json, "\"id\":\"10844\"") && Has(json, "\"target\":\"17379391094613082370\""),
        "bot id and target are decimal strings (64 bits do not fit a JSON number)");
    Check(Count(json, "\"target\":") == 1, "a bot with no target has no target field");
    Check(Has(json, "\"mobs_map\":209") && Has(json, "\"mobs\":["), "the mobs and their map");
    Check(Count(json, "\"entry\":4863") == 4, "every mob carries its entry");
    Check(Has(json, "Stonevault \\\"Rock\\\"chewer"), "a mob name is escaped");
    Check(Has(json, "\"victim\":\"10844\""), "whom a mob targets");
    Check(Count(json, "\"victim\":") == 1, "a mob with no victim has no victim field");
    Check(Has(json, "\"reaction\":\"n\",\"kind\":\"k\""), "neutral critter");
    Check(Has(json, "\"reaction\":\"f\",\"kind\":\"g\""), "friendly guard");
    Check(Count(json, "\"reaction\":\"h\"") == 2, "unknown reaction letters read as hostile");
    Check(Count(json, "\"kind\":") == 2, "unknown kinds are left out");
    Check(Has(json, "\"hp\":85") && Has(json, "\"x\":-1234.6") && Has(json, "\"y\":987.7"), "mob hp and place");

    // Old shape: nothing reported, nothing added.
    BindingInfo plain;
    plain.commander = "Old";
    plain.bots.push_back(Bot("A", true));
    std::string const old = BuildPayload(9, 1, 1, 1, {plain});
    Check(!Has(old, "commander_pos") && !Has(old, "\"mobs") && !Has(old, "\"target\"") && !Has(old, "\"id\""),
        "a binding without map data keeps the old shape");
    // An empty list that was read is still sent: the dashboard knows the module reports mobs.
    plain.mobsReported = true;
    Check(Has(BuildPayload(9, 1, 1, 1, {plain}), "\"mobs\":[]"), "an empty mob list is sent when it was read");
}

void TestMapDataCapsAndSize()
{
    BindingInfo b;
    b.commander = std::string(40, 'C');
    b.lead = std::string(12, 'L');
    for (int i = 0; i < 6; ++i)
    {
        BotInfo bot = Bot("Botname" + std::to_string(i), i == 0);
        bot.id = 0xFFFFFFFFFFFFFFFFull;
        bot.target = 0xFFFFFFFFFFFFFFFFull;
        b.bots.push_back(bot);
    }
    for (int i = 0; i < 8; ++i)
        b.orders.push_back(Order(i, "ritual", true, std::string(kMaxOrderResult, 'r'), std::string(kMaxOrderDetail, 'd')));
    b.commanderPlaced = true;
    b.mobsReported = true;
    for (int i = 0; i < 40; ++i)
        b.mobs.push_back(Mob(0xFFFFFFFFFFFFFFFFull, std::string(60, 'M'), 'h', 'k', 0xFFFFFFFFFFFFFFFFull));

    std::string const one = BuildPayload(9, 1, 1, 1, {b});
    Check(Count(one, "\"entry\":") == kMaxMobsPerBinding, "mobs per binding are capped");
    Check(!Has(one, std::string(kMaxMobName + 1, 'M')), "a mob name is clipped");
    std::printf("one full binding with %zu mobs: %zu bytes\n", kMaxMobsPerBinding, one.size());

    std::vector<BindingInfo> many(kMaxBindings + 3, b);
    std::string const json = BuildPayload(9, 1, 1, 1, many);
    Check(Count(json, "\"entry\":") == kMaxMobsPerPayload, "mobs per payload are capped");
    Check(json.size() < 60000, "the worst case with map data still fits one datagram");
    std::printf("worst case (%zu bindings, %zu mobs): %zu bytes\n", kMaxBindings, kMaxMobsPerPayload, json.size());
}

void TestUnitInfo()
{
    Check(CcKind(true, true, true, true) == 's' && CcKind(false, true, true, true) == 'c' && CcKind(false, false, true, true) == 'f' &&
              CcKind(false, false, false, true) == 'r' && CcKind(false, false, false, false) == 0,
        "crowd control is the first kind that holds");
    Check(RankKind(3, false, false) == 'b' && RankKind(1, true, false) == 'b' && RankKind(1, false, false) == 'e' &&
              RankKind(2, false, false) == 'r' && RankKind(4, false, false) == 'r' && RankKind(0, false, false) == 0,
        "a rank letter: boss, elite, rare, ordinary");
    // A five-man boss is an elite by rank (Lord Cobrahn 3669, Kresh 3653: rank 1, no instance bind); a dungeon plan's
    // boss step names it, and that makes it a boss.
    Check(RankKind(1, false, true) == 'b' && RankKind(0, false, true) == 'b' && RankKind(2, false, true) == 'b',
        "a dungeon plan's boss is a boss whatever its rank");

    BindingInfo b;
    b.commander = "Zfmagea";
    b.commanderGuid = 9;
    b.boundAt = 1791990000;
    BotInfo bot = Bot("Forthur", true);
    bot.id = 11;
    bot.hasExtra = true;
    bot.extra.state = 'c';
    bot.extra.action = "melee";
    bot.extra.result = 'O';
    bot.extra.cast = "Mortal \"Strike\"";
    bot.extra.cc = 's';
    bot.extra.rest = 'd';
    bot.extra.moving = true;
    bot.extra.targetName = "Revelosh";
    b.bots.push_back(bot);
    b.bots.push_back(Bot("Plain", false)); // no extra: no activity fields
    b.mobsReported = true;
    MobInfo boss = Mob(5, "Revelosh");
    boss.level = 47;
    boss.rank = 'b';
    boss.cc = 'f';
    boss.cast = "Chain Lightning";
    b.mobs.push_back(boss);
    MobInfo plain = Mob(6, "Rat");
    plain.rank = 'x';
    plain.cc = 'q';
    b.mobs.push_back(plain);
    std::string const json = BuildPayload(9, 1, 1, 1, {b});

    Check(Has(json, "\"bound_at\":1791990000"), "when the binding began");
    Check(Has(json, "\"st\":\"c\"") && Has(json, "\"act\":\"melee\"") && Has(json, "\"res\":\"O\""), "a bot's state and newest action");
    Check(Has(json, "\"cast\":\"Mortal \\\"Strike\\\"\""), "the spell a bot casts, escaped");
    Check(Has(json, "\"cc\":\"s\"") && Has(json, "\"rest\":\"d\"") && Has(json, "\"mv\":true") && Has(json, "\"tname\":\"Revelosh\""), "a bot's crowd control, rest, movement and target name");
    Check(Count(json, "\"st\":") == 1 && Count(json, "\"act\":") == 1, "a bot without extra keeps the old shape");
    Check(Has(json, "\"lvl\":47") && Has(json, "\"rank\":\"b\"") && Has(json, "\"cast\":\"Chain Lightning\""), "a mob's level, rank and cast");
    Check(Count(json, "\"rank\":") == 1 && Count(json, "\"lvl\":") == 1, "an ordinary mob has no rank or level field");
    Check(Count(json, "\"cc\":") == 2, "unknown crowd-control letters are left out");

    // Bounds: clipped names, and the worst case still fits one datagram.
    BindingInfo big;
    big.commander = "C";
    big.boundAt = 1791990000;
    for (int i = 0; i < 6; ++i)
    {
        BotInfo x = Bot("Botname" + std::to_string(i), i == 0);
        x.id = 0xFFFFFFFFFFFFFFFFull;
        x.target = 0xFFFFFFFFFFFFFFFFull;
        x.hasExtra = true;
        x.extra.state = 'c';
        x.extra.action = std::string(90, 'a');
        x.extra.result = 'O';
        x.extra.cast = std::string(90, 'c');
        x.extra.cc = 's';
        x.extra.rest = 'e';
        x.extra.moving = true;
        x.extra.targetName = std::string(90, 't');
        big.bots.push_back(x);
    }
    for (int i = 0; i < 8; ++i)
        big.orders.push_back(Order(i, "ritual", true, std::string(kMaxOrderResult, 'r'), std::string(kMaxOrderDetail, 'd')));
    big.mobsReported = true;
    for (int i = 0; i < 40; ++i)
    {
        MobInfo m = Mob(0xFFFFFFFFFFFFFFFFull, std::string(60, 'M'), 'h', 'k', 0xFFFFFFFFFFFFFFFFull);
        m.level = 63;
        m.rank = 'b';
        m.cc = 's';
        m.cast = std::string(90, 'c');
        m.hasO = true;
        m.o = -3.14159f;
        m.castId = 17086;
        m.castLeftMs = 5000;
        m.castTotalMs = 5000;
        m.castTarget = 0xFFFFFFFFFFFFFFFFull;
        m.hasShape = true;
        m.shape.kind = TortoiseBots::SpellShape::Kind::Line;
        m.shape.length = 151.53f;
        m.shape.width = 60.0f;
        m.shape.hasSegment = true;
        m.shape.x0 = -130.788300f;
        m.shape.y0 = -213.424026f;
        m.shape.x1 = 20.730539f;
        m.shape.y1 = -215.237610f;
        big.mobs.push_back(m);
    }
    std::vector<BindingInfo> many(kMaxBindings + 3, big);
    std::string const worst = BuildPayload(9, 1, 1, 1, many);
    Check(!Has(worst, std::string(kMaxActionName + 1, 'a')) && !Has(worst, std::string(kMaxCastName + 1, 'c')) &&
              !Has(worst, std::string(kMaxTargetName + 1, 't')),
        "action, cast and target names are clipped");
    Check(Count(worst, "\"cid\":") == kMaxMobOverlaysPerPayload, "the overlay rows per datagram are capped");
    Check(worst.size() < 60000 && worst.size() < 65000, "the worst case with unit info still fits one datagram");
    std::printf("worst case with unit info (%zu bindings, %zu mobs): %zu bytes\n", kMaxBindings, kMaxMobsPerPayload, worst.size());
}

// One mob object, exactly as the writer emits it (both builders set the stream to fixed, precision 1).
std::string MobJson(MobInfo& m)
{
    std::ostringstream ss;
    ss.setf(std::ios::fixed);
    ss.precision(1);
    AppendMobJson(ss, m);
    return ss.str();
}

// The boss overlay (fight debugger phase 2): facing, the cast and its shape ride the mob row, only when they were read,
// and a spell with no shape sends none.
void TestBossOverlay()
{
    MobInfo boss = Mob(5, "Onyxia");
    boss.entry = 10184;
    boss.hpPct = 80;
    boss.x = 1.5f;
    boss.y = -2.5f;
    boss.level = 63;
    boss.rank = 'b';
    boss.cast = "Flame Breath";
    boss.hasO = true;
    boss.o = 1.5708f;
    boss.castId = 18435;
    boss.castLeftMs = 1200;
    boss.castTotalMs = 2000;
    boss.castTarget = 814;
    boss.hasShape = true;
    boss.shape.spell = 18435;
    boss.shape.kind = TortoiseBots::SpellShape::Kind::Cone;
    boss.shape.angle = 120;
    boss.shape.length = 45.0f;
    Check(MobJson(boss) ==
              "{\"id\":\"5\",\"entry\":10184,\"name\":\"Onyxia\",\"hp\":80,\"x\":1.5,\"y\":-2.5,\"reaction\":\"h\",\"lvl\":63,"
              "\"rank\":\"b\",\"cast\":\"Flame Breath\",\"o\":1.57,\"cid\":18435,\"cleft\":1200,\"ctot\":2000,\"ctg\":\"814\","
              "\"shape\":{\"k\":\"cone\",\"a\":120,\"l\":45.0}}",
        "a boss's facing, cast and cone are written exactly");

    boss.channel = true;
    boss.shape.kind = TortoiseBots::SpellShape::Kind::Line;
    boss.shape.angle = 0;
    boss.shape.length = 151.5f;
    boss.shape.width = 60.0f;
    Check(Has(MobJson(boss), "\"ch\":1,\"shape\":{\"k\":\"line\",\"l\":151.5,\"w\":60.0}"),
        "a channel is flagged and a line along the facing carries its width, with no ends");

    MobInfo plain = Mob(6, "Rat");
    plain.rank = 'b';
    plain.hasO = true;
    plain.o = -0.001f; // rounds to zero
    Check(MobJson(plain) == "{\"id\":\"6\",\"entry\":4863,\"name\":\"Rat\",\"hp\":85,\"x\":-1234.6,\"y\":987.7,"
                            "\"reaction\":\"h\",\"rank\":\"b\",\"o\":0.00}",
        "a facing of zero is sent as 0.00, never -0.00, and a mob without a cast has no cast fields");

    MobInfo old = Mob(7, "Rat");
    old.cast = "Chain Lightning";
    Check(!Has(MobJson(old), "\"o\"") && !Has(MobJson(old), "\"cid\""),
        "an older module's mob keeps the old shape: no facing and no cast id");
    old.hasShape = true; // a shape without a cast is never written
    Check(!Has(MobJson(old), "\"shape\""), "a shape without a cast is left out");
}

// The aura selectors are pure: these prove the passive skip, the caps and the debuff order without a game unit.
void TestAuraSelection()
{
    std::vector<AuraRead> auras;
    for (uint32_t i = 0; i < 10; ++i)
    {
        AuraRead r;
        r.aura.id = 24080 + i;
        r.aura.name = "Aura" + std::to_string(i);
        auras.push_back(r);
    }
    auras[3].passive = true;
    std::vector<AuraInfo> const up = SelectUnitAuras(auras);
    Check(up.size() == kMaxMobAuras, "a boss row carries at most 8 auras");
    Check(up[0].id == 24080 && up[2].id == 24082 && up[3].id == 24084,
        "a passive aura is skipped and the holder order is kept");
    Check(up.back().id == 24088, "the cap keeps the first eight live auras");

    // A player's debuffs on the boss cannot hide its phase buff: the positive auras are taken first, holder order kept inside
    // each group.
    std::vector<AuraRead> mixed;
    for (uint32_t i = 0; i < 10; ++i)
    {
        AuraRead r;
        r.aura.id = 25000 + i;
        r.aura.name = "Debuff" + std::to_string(i);
        r.negative = true;
        mixed.push_back(r);
    }
    AuraRead phase;
    phase.aura.id = 25100;
    phase.aura.name = "Spider Form";
    mixed.push_back(phase);
    std::vector<AuraInfo> const prio = SelectUnitAuras(mixed);
    Check(prio.size() == kMaxMobAuras, "the cap still holds with debuffs listed first");
    Check(prio[0].id == 25100, "the positive phase buff comes before ten negative holders");
    Check(prio[1].id == 25000 && prio.back().id == 25006,
        "the negative holders keep their order and fill the rest of the cap");

    std::vector<AuraRead> debuffs;
    auto add = [&debuffs](uint32_t id, char dispel, uint32_t leftMs, bool negative = true, bool hostile = true,
                          bool passive = false)
    {
        AuraRead r;
        r.aura.id = id;
        r.aura.name = "Debuff" + std::to_string(id);
        r.aura.dispelType = dispel;
        r.aura.leftMs = leftMs;
        r.negative = negative;
        r.hostileCaster = hostile;
        r.passive = passive;
        debuffs.push_back(r);
    };
    add(101, 'm', 3000);
    add(102, 0, 9000);
    add(103, 'p', 7000);
    add(104, 'c', 12000);
    add(105, 'p', 100, true, true, true);   // passive
    add(106, 'm', 8000, false);             // positive
    add(107, 'm', 8000, true, false);       // the bot's own group cast it
    add(108, 'd', 500);
    std::vector<AuraInfo> const out = SelectBotDebuffs(debuffs);
    Check(out.size() == kMaxBotDebuffs, "a bot carries at most 4 boss debuffs");
    Check(out[0].id == 104 && out[1].id == 103 && out[2].id == 101 && out[3].id == 108,
        "dispellable first, then the one that runs out last");
    for (AuraInfo const& a : out)
        Check(a.id != 105 && a.id != 106 && a.id != 107, "passive, positive and own-group auras are not boss debuffs");

    // Which auras on a bot count as a boss debuff: a creature's is shown, a player's only from outside the bot's group. The
    // set holds the bot (700), its healer bot (704) and a human group member (900).
    std::unordered_set<uint32_t> const members{700, 704, 900};
    Check(!IsBossDebuff(true, false, 704, members), "a debuff the group's own healer bot cast is not shown");
    Check(!IsBossDebuff(true, false, 900, members), "a debuff a human group member cast is not shown");
    Check(IsBossDebuff(true, true, 0, members), "a creature's debuff is shown");
    Check(IsBossDebuff(true, false, 999, members), "a debuff from a player outside the group is shown");
    Check(!IsBossDebuff(false, true, 0, members) && !IsBossDebuff(false, false, 999, members),
        "a positive aura is never a boss debuff");

    Check(UnitFlags(false, false, false, false, false).empty(), "a calm unit has no flags");
    Check(UnitFlags(true, true, true, true, true) == "nihde", "the flags are in their fixed order");
    Check(UnitFlags(false, false, true, false, true) == "he", "only the flags that hold are written");
}

// The boss state rides an overlay row only: its auras (capped) and flag letters; an ordinary mob carries neither.
void TestBossStatePayload()
{
    BindingInfo b;
    b.commander = "Zfmagea";
    b.lead = "Forthur";
    b.bots.push_back(Bot("Forthur", true));
    b.mobsReported = true;
    MobInfo boss = Mob(5, "Venoxis");
    boss.entry = 14507;
    boss.rank = 'b';
    boss.hasO = true;
    boss.o = 0.5f;
    for (uint32_t i = 0; i < 10; ++i)
    {
        AuraInfo a;
        a.id = 23849 + i;
        a.name = "Aura" + std::to_string(i);
        a.stacks = i == 0 ? 3 : 1;
        a.leftMs = i == 1 ? 8000 : 0;
        boss.auras.push_back(a);
    }
    boss.flags = "nhe";
    b.mobs.push_back(boss);
    MobInfo trash = Mob(6, "Rat");
    trash.auras.push_back(AuraInfo{24084, "Spider Form", 1, 0, 0});
    trash.flags = "n";
    b.mobs.push_back(trash);
    std::string const json = BuildPayload(9, 1, 1, 1, {b});

    Check(Count(json, "\"n\":\"Aura") == kMaxMobAuras, "a boss row writes at most 8 auras");
    Check(Has(json, "\"au\":[{\"id\":23849,\"n\":\"Aura0\",\"st\":3}"), "the first aura keeps its stacks");
    Check(Has(json, "\"id\":23850,\"n\":\"Aura1\",\"l\":8000"), "a running aura carries its time left");
    Check(Has(json, "\"uf\":\"nhe\""), "the boss flags ride the row");
    Check(!Has(json, "Spider Form") && !Has(json, "\"uf\":\"n\""), "an ordinary mob carries no auras and no flags");

    // A row without the overlay writes neither, even when the caller set them.
    std::ostringstream ss;
    ss.setf(std::ios::fixed);
    ss.precision(1);
    AppendMobJson(ss, boss, false);
    Check(!Has(ss.str(), "\"au\"") && !Has(ss.str(), "\"uf\""), "a non-overlay row leaves the boss state out");
}

// A bot's boss debuffs and its charm state ride its row, with the dispel letter, and trim level 1 drops them first.
void TestBotDebuffPayload()
{
    BindingInfo b;
    b.commander = "Zfmagea";
    b.lead = "Healbot";
    BotInfo bot = Bot("Healbot", true);
    bot.hasExtra = true;
    bot.extra.mc = true;
    char const kinds[] = {'d', 'm', 'p', 'c', 0};
    for (int i = 0; i < 5; ++i)
    {
        AuraInfo a;
        a.id = 24328 + static_cast<uint32_t>(i);
        a.name = "Debuff" + std::to_string(i);
        a.leftMs = i == 0 ? 8000 : 0;
        a.dispelType = kinds[i];
        bot.extra.debuffs.push_back(a);
    }
    b.bots.push_back(bot);
    b.mobsReported = true;
    MobInfo boss = Mob(5, "Hakkar");
    boss.entry = 14834;
    boss.rank = 'b';
    boss.hasO = true;
    AuraInfo form;
    form.id = 24169;
    form.name = "Tiger Form";
    boss.auras.push_back(form);
    b.mobs.push_back(boss);
    std::string const json = BuildPayload(9, 1, 1, 1, {b});

    Check(Count(json, "\"dt\":") == kMaxBotDebuffs, "a bot row writes at most 4 boss debuffs");
    Check(Has(json, "\"db\":[{\"id\":24328,\"n\":\"Debuff0\",\"l\":8000,\"dt\":\"d\"}"),
        "a debuff carries its time left and its dispel letter");
    Check(Has(json, "\"mc\":1"), "a charmed bot is flagged");
    Check(!Has(json, "\"id\":24332"), "the fifth debuff is not written");

    std::vector<BindingInfo const*> refs{&b};
    std::vector<uint8_t> const level1{1};
    std::string const trimmed = BuildPayloadOf(9, 1, 1, 1, refs, 0, 0, 1, &level1);
    Check(!Has(trimmed, "\"db\":") && !Has(trimmed, "\"mc\":1"), "trim level 1 drops the bot debuffs");
    Check(Has(trimmed, "\"au\":[{\"id\":24169"), "trim level 1 keeps the boss's auras");
}

// An aura's name is free text from the spell data: a quote and a backslash in it must not break either list.
void TestAuraNameEscaping()
{
    BindingInfo b;
    b.commander = "Zfmagea";
    b.lead = "Healbot";
    b.mobsReported = true;
    MobInfo boss = Mob(5, "Hakkar");
    boss.entry = 14834;
    boss.rank = 'b';
    boss.hasO = true;
    boss.auras.push_back(AuraInfo{24169, "Q\"\\x", 1, 0, 0});
    b.mobs.push_back(boss);
    BotInfo bot = Bot("Healbot", true);
    bot.hasExtra = true;
    bot.extra.debuffs.push_back(AuraInfo{24328, "Q\"\\x", 1, 0, 'm'});
    b.bots.push_back(bot);

    std::string const json = BuildPayload(9, 1, 1, 1, {b});
    Check(Has(json, "\"au\":[{\"id\":24169,\"n\":\"Q\\\"\\\\x\"}"), "a quoted aura name in au is escaped");
    Check(Has(json, "\"db\":[{\"id\":24328,\"n\":\"Q\\\"\\\\x\",\"dt\":\"m\"}"), "a quoted aura name in db is escaped");
}

// The 0.5 s position report (task S3/R3m): a bot row in one decimal with its facing at two, a dead bot at hp 0 and
// alive 0, and the mob rows of the lead's last full report.
void TestPosPayload()
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
    PosBot dead = bot;
    dead.g = 4022;
    dead.alive = false;
    dead.hp = 0;
    ref.bots.push_back(dead);
    PosMob boss;
    boss.id = 17370386927380123ull;
    boss.x = -20.14f;
    boss.y = 40.26f;
    boss.hpPct = 87;
    boss.victim = 4021;
    ref.mobs.push_back(boss);

    std::string const json = BuildPosPayload(12, 77, 4412, 1793210001, 1793210001500, ref);
    Check(Has(json, "\"v\":12") && Has(json, "\"fmt\":1") && Has(json, "\"session\":77") && Has(json, "\"seq\":4412"),
        "carries the protocol version, fmt, session and seq");
    Check(Has(json, "\"ts\":1793210001") && Has(json, "\"ts_ms\":1793210001500"), "unix seconds and milliseconds");
    Check(Has(json, "\"type\":\"COMMANDER_POS\"") && Has(json, "\"commander\":\"Glacienne\"") &&
              Has(json, "\"commander_guid\":12"), "its type and commander");
    Check(Has(json, "\"combat\":true"), "the combat flag");
    Check(Has(json, "{\"g\":4021,\"m\":249,\"x\":-12.3,\"y\":45.7,\"z\":-80.1,\"o\":3.14,\"hp\":5321,\"mhp\":6000,\"a\":1}"),
        "a bot row: x/y/z in one decimal, the facing at two, alive 1");
    Check(Has(json, "{\"g\":4022,\"m\":249,\"x\":-12.3,\"y\":45.7,\"z\":-80.1,\"o\":3.14,\"hp\":0,\"mhp\":6000,\"a\":0}"),
        "a dead bot: hp 0 and alive 0");
    Check(Has(json, "{\"id\":\"17370386927380123\",\"x\":-20.1,\"y\":40.3,\"hp\":87,\"v\":\"4021\"}"),
        "a mob row with its place, health and victim");
    Check(json.find('\n') == std::string::npos, "the datagram is one line");

    PosRef plain;
    plain.commander = "Glacienne";
    PosMob ordinary;
    ordinary.id = 5;
    ordinary.x = 1.0f;
    ordinary.y = 2.0f;
    ordinary.hpPct = 60;
    plain.mobs.push_back(ordinary);
    std::string const old = BuildPosPayload(12, 77, 4413, 1, 1, plain);
    Check(Has(old, "{\"id\":\"5\",\"x\":1.0,\"y\":2.0,\"hp\":60,\"v\":\"\"}"), "a mob with no victim sends an empty v");
}

// The caps of the position report: 40 bots and 24 mobs per datagram, one decimal for a place and 20-digit ids, still
// inside its byte budget.
void TestPosCapsAndSize()
{
    PosRef ref;
    ref.commander = std::string(40, 'C');
    for (int i = 0; i < 50; ++i)
    {
        PosBot bot;
        bot.g = 4021 + i;
        bot.map = 249;
        bot.x = -1234.56f;
        bot.y = 987.65f;
        bot.z = -80.14f;
        bot.o = 3.14159f;
        bot.hp = 5321;
        bot.maxHp = 6000;
        bot.alive = i % 2 == 0;
        if (!bot.alive)
            bot.hp = 0;
        ref.bots.push_back(bot);
    }
    for (int i = 0; i < 30; ++i)
    {
        PosMob mob;
        mob.id = 17000000000000000000ull + i; // 20 digits, the widest a raw guid has
        mob.x = -1234.56f;
        mob.y = 987.65f;
        mob.hpPct = 100;
        mob.victim = 4021;
        ref.mobs.push_back(mob);
    }
    std::string const json = BuildPosPayload(12, 1, 1, 1, 1, ref);
    Check(Count(json, "\"g\":") == kMaxPosBots, "bots are capped at 40");
    Check(Count(json, "\"mhp\":") == kMaxPosBots, "only the capped bot rows are written");
    Check(Count(json, "\"id\":\"") == kMaxPosMobs, "mobs are capped at 24");
    Check(json.size() <= kPosByteBudget, "the worst case stays within the position budget");
    Check(json.size() < 16384, "and under 16 KiB at the caps");
}

void TestRunInfo()
{
    BindingInfo b;
    b.commander = "Glacienne";
    b.bots.push_back(Bot("Worwusorl", true));
    std::string const none = BuildPayload(9, 1, 1, 1, {b});
    Check(!Has(none, "\"run\""), "a binding without a run keeps the old shape");

    b.run.present = true;
    b.run.plan = "uldaman";
    b.run.state = "paused";
    b.run.step = 78;
    b.run.steps = 205;
    b.run.leg = "ancient_stone_keeper";
    b.run.kind = "pull";
    b.run.phase = "rest";
    b.run.note = "a unit survived \"3\" pulls";
    b.run.changedAt = 1791990100;
    std::string const json = BuildPayload(9, 1, 1, 1, {b});
    Check(Has(json, "\"run\":{\"plan\":\"uldaman\",\"state\":\"paused\",\"step\":78,\"steps\":205,\"leg\":\"ancient_stone_keeper\""), "the run's plan, state, step and leg");
    Check(Has(json, "\"kind\":\"pull\",\"phase\":\"rest\"") && Has(json, "\"changed\":1791990100"), "kind, phase and when it last changed");
    Check(Has(json, "survived \\\"3\\\" pulls"), "the note is escaped");
    Check(json.find('\n') == std::string::npos, "still one line");

    // Clipped, and the worst case with a run on every binding still fits one datagram.
    b.run.note = std::string(500, 'n');
    b.run.leg = std::string(90, 'l');
    std::string const clipped = BuildPayload(9, 1, 1, 1, {b});
    Check(!Has(clipped, std::string(kMaxRunNote + 1, 'n')) && !Has(clipped, std::string(kMaxRunWord + 1, 'l')), "the run's words are clipped");
    BindingInfo big = b;
    for (int i = 0; i < 6; ++i)
        big.bots.push_back(Bot("Botname" + std::to_string(i), false));
    std::vector<BindingInfo> many(kMaxBindings, big);
    Check(BuildPayload(9, 1, 1, 1, many).size() < 60000, "a run on every binding fits one datagram");
}

void TestPathData()
{
    // The step window: a short section is listed whole, a long one keeps the current step a few steps from the window's start.
    auto w = RunStepWindow(10, 15, 12);
    Check(w.first == 10 && w.second == 15, "a short section is listed whole");
    w = RunStepWindow(0, 100, 0);
    Check(w.first == 0 && w.second == kMaxRunSteps, "a long section starts at its first step");
    w = RunStepWindow(0, 100, 50);
    Check(w.first == 50 - kMaxRunSteps / 4 && w.second == w.first + kMaxRunSteps && w.first <= 50 && 50 < w.second, "the window follows the current step");
    w = RunStepWindow(0, 100, 99);
    Check(w.second == 100 && w.second - w.first == kMaxRunSteps, "the window ends with the section");
    w = RunStepWindow(0, 100, 140);
    Check(w.second == 100 && w.first == 100 - kMaxRunSteps, "a finished run (step past the end) shows the last steps");
    w = RunStepWindow(5, 5, 5);
    Check(w.first == w.second, "an empty section lists nothing");
    w = RunStepWindow(40, 100, 10);
    Check(w.first == 40 && w.second == 40 + kMaxRunSteps, "a step before the section clamps to its start");

    BindingInfo b;
    b.commander = "Glacienne";
    BotInfo healer = Bot("Healer", false);
    healer.hasExtra = true;
    healer.extra.cast = "Flash Heal";
    healer.extra.castTarget = 0xF130001EDF000102ull;
    BotInfo idle = Bot("Idle", false);
    idle.hasExtra = true;
    idle.extra.castTarget = 77; // not casting: no target is sent
    b.bots.push_back(healer);
    b.bots.push_back(idle);
    b.run.present = true;
    b.run.plan = "uldaman";
    b.run.state = "running";
    for (uint32_t i = 0; i < 3; ++i)
    {
        RunStepInfo st;
        st.index = 40 + i;
        st.kind = i == 0 ? "move" : i == 1 ? "pull" : "boss";
        st.x = -6000.25f + i;
        st.y = -2900.5f;
        st.radius = i == 0 ? 0.0f : 15.0f;
        st.entry = i == 2 ? 4854 : 0;
        st.name = i == 2 ? "Grimlok \"the\" Stonebreaker" : "";
        st.skipped = i == 1;
        b.run.stepList.push_back(st);
    }
    b.objectsReported = true;
    ObjectInfo o1;
    o1.id = 0xF110000000000009ull;
    o1.entry = 2087;
    o1.name = "Ancient \"Treasure\" Chest";
    o1.x = -6001.0f;
    o1.y = -2899.0f;
    o1.type = 'c';
    o1.state = 'u';
    ObjectInfo o2 = o1;
    o2.type = 'z'; // unknown letters read as other
    o2.state = '?';
    b.objects = {o1, o2};
    std::string const json = BuildPayload(9, 1, 1, 1, {b});

    Check(Has(json, "\"cast\":\"Flash Heal\",\"ctgt\":\"17379391094613082370\""), "a cast names its unit target");
    Check(Count(json, "\"ctgt\":") == 1, "no cast, no cast target");
    Check(Has(json, "\"plan_steps\":[{\"i\":40,\"k\":\"move\",\"x\":-6000.2,\"y\":-2900.5}"), "the first step has no radius or entry");
    Check(Has(json, "{\"i\":41,\"k\":\"pull\",\"x\":-5999.2,\"y\":-2900.5,\"r\":15.0,\"sk\":true}"), "a pull with its radius, marked skipped");
    Check(Has(json, "\"e\":4854,\"n\":\"Grimlok \\\"the\\\" Stonebreak") && !Has(json, "Stonebreaker"), "a boss with its entry, an escaped name clipped");
    Check(Has(json, "\"objects\":[{\"id\":\"17370383762768003081\",\"entry\":2087"), "objects carry their guid as a decimal string");
    Check(Has(json, "\"type\":\"c\",\"state\":\"u\"}") && Has(json, "\"type\":\"o\",\"state\":\"r\"}"), "type and state letters, unknown ones cleaned");
    Check(json.find('\n') == std::string::npos, "still one line");

    BindingInfo plain;
    plain.commander = "Old";
    plain.run.present = true;
    std::string const old = BuildPayload(9, 1, 1, 1, {plain});
    Check(!Has(old, "plan_steps") && !Has(old, "objects") && !Has(old, "ctgt"), "a binding without path data keeps the old shape");
    plain.objectsReported = true;
    Check(Has(BuildPayload(9, 1, 1, 1, {plain}), "\"objects\":[]"), "an empty object list that was read is sent");

    // Caps and the worst case: every binding full of steps, objects, mobs and casting bots.
    BindingInfo big;
    big.commander = std::string(40, 'C');
    big.lead = std::string(12, 'L');
    for (int i = 0; i < 6; ++i)
    {
        BotInfo bot = Bot("Botname" + std::to_string(i), i == 0);
        bot.id = bot.target = 0xFFFFFFFFFFFFFFFFull;
        bot.hasExtra = true;
        bot.extra.action = std::string(60, 'a');
        bot.extra.cast = std::string(60, 'c');
        bot.extra.castTarget = 0xFFFFFFFFFFFFFFFFull;
        bot.extra.targetName = std::string(60, 't');
        big.bots.push_back(bot);
    }
    for (int i = 0; i < 8; ++i)
        big.orders.push_back(Order(i, "ritual", true, std::string(kMaxOrderResult, 'r'), std::string(kMaxOrderDetail, 'd')));
    big.commanderPlaced = true;
    big.mobsReported = true;
    for (int i = 0; i < 40; ++i)
    {
        MobInfo m = Mob(0xFFFFFFFFFFFFFFFFull, std::string(60, 'M'), 'h', 'k', 0xFFFFFFFFFFFFFFFFull);
        m.rank = 'b';
        m.cast = std::string(24, 'c');
        m.hasO = true;
        m.o = 3.14159f;
        m.castId = 18564;
        m.castLeftMs = 5000;
        m.castTotalMs = 5000;
        m.castTarget = 0xFFFFFFFFFFFFFFFFull;
        m.hasShape = true;
        m.shape.kind = TortoiseBots::SpellShape::Kind::BackCone;
        m.shape.angle = 75;
        m.shape.length = 91.0f;
        big.mobs.push_back(m);
    }
    big.run.present = true;
    big.run.plan = std::string(60, 'p');
    big.run.note = std::string(500, 'n');
    for (uint32_t i = 0; i < 60; ++i)
    {
        RunStepInfo st;
        st.index = 4000000000u + i;
        st.kind = "move_direct";
        st.x = st.y = -12345.67f;
        st.radius = 100.0f;
        st.entry = 4000000000u;
        st.name = std::string(60, 'S');
        st.skipped = true;
        big.run.stepList.push_back(st);
    }
    big.objectsReported = true;
    for (int i = 0; i < 40; ++i)
    {
        ObjectInfo o = o1;
        o.id = 0xFFFFFFFFFFFFFFFFull;
        o.name = std::string(60, 'O');
        big.objects.push_back(o);
    }
    std::string const one = BuildPayload(9, 1, 1, 1, {big});
    Check(Count(one, "\"i\":") == kMaxRunSteps, "steps per run are capped");
    Check(Count(one, "\"state\":\"u\"") == kMaxObjectsPerBinding, "objects per binding are capped");
    std::printf("one full binding with steps and objects: %zu bytes\n", one.size());
    std::vector<BindingInfo> many(kMaxBindings + 3, big);
    std::string const json2 = BuildPayload(9, 1, 1, 1, many);
    Check(Count(json2, "\"i\":") == kMaxRunStepsPerPayload, "steps per payload are capped");
    Check(Count(json2, "\"state\":\"u\"") == kMaxObjectsPerPayload, "objects per payload are capped");
    Check(!Has(json2, std::string(kMaxObjectName + 1, 'O')) && !Has(json2, std::string(kMaxMobName + 1, 'S')), "object and step names are clipped");
    Check(Count(json2, "\"cid\":") == kMaxMobOverlaysPerPayload, "the overlay rows are capped across bindings too");
    Check(json2.size() < 65507, "the worst case with paths, steps and objects still fits the UDP limit");
    std::printf("worst case with paths (%zu bindings): %zu bytes\n", kMaxBindings, json2.size());
}

// Every known boss of the lead's dungeon map with its state in the lead's instance (owner 2026-10-21: all bosses on the dashboard map,
// alive or dead): the state rule, the merge of several spawns of one entry, and the payload fields, caps and byte budget.
void TestBosses();

// A6: a raid binding carries up to 40 bots with their subgroup and away
// flag, and the run's last 30 status lines (notes) with the newest last.
void TestRaidPayload()
{
    BindingInfo raid;
    raid.commander = "Gbraid";
    raid.lead = "T0";
    for (int i = 0; i < 40; ++i)
    {
        BotInfo bot = Bot("Bot" + std::to_string(i), i == 0);
        bot.subgroup = i / 5; // subgroups 0-7
        if (i >= 37)
            bot.away = true;
        raid.bots.push_back(bot);
    }
    raid.run.present = true;
    raid.run.plan = "molten_core";
    raid.run.state = "running";
    for (int i = 0; i < 31; ++i)
    {
        RunNoteInfo note;
        note.at = 100 + i;
        note.step = 1;
        note.leg = "leg";
        note.kind = "pull";
        note.phase = "fight";
        note.state = "running";
        note.note = i == 0 ? "the oldest note" : ("note " + std::to_string(i));
        raid.run.notes.push_back(note);
    }
    // A second binding with 30 bots in the same payload: 40 + 30 = 70 > 64,
    // so the payload stops the second binding's bot list at 24.
    BindingInfo second = raid;
    second.bots.clear();
    for (int i = 0; i < 30; ++i)
        second.bots.push_back(Bot("B" + std::to_string(i), i == 0));
    std::string const json = BuildPayload(9, 1, 1, 1, {raid, second});
    Check(Count(json, "\"name\":") == 64, "a payload carries 64 bots over all bindings");
    Check(Has(json, "\"sub\":7"), "a subgroup 7 bot writes its sub");
    Check(Count(json, "\"away\":true") == 3, "three away bots write the flag");
    Check(Has(json, "\"notes\":["), "the run writes its notes");
    Check(!Has(json, "the oldest note"), "only the newest 30 notes are written");
    Check(Has(json, "note 30"), "the newest note is written");
    Check(json.size() <= kPayloadByteBudget, "a raid plus a party stays within the byte budget");
    // A party bot writes no sub; a present run without notes writes no notes list.
    BindingInfo party;
    party.commander = "P";
    party.bots.push_back(Bot("Solo", true));
    party.run.present = true;
    party.run.plan = "uldaman";
    party.run.state = "running";
    std::string const plain = BuildPayload(9, 1, 1, 1, {party});
    Check(!Has(plain, "\"sub\":"), "a party bot writes no sub");
    Check(!Has(plain, "\"notes\":["), "no notes pushed: no notes list");
    Check(!Has(plain, "\"away\":true"), "no away bot: no away flag");
}

void TestBosses()
{
    // the state of one spawn: on the map -> alive or a corpse; not on the map -> killed while its respawn time runs, else not known
    Check(BossState(true, true, false) == 'a', "a living creature is alive");
    Check(BossState(true, true, true) == 'a', "found alive beats an old respawn time");
    Check(BossState(true, false, false) == 'd', "a corpse on the map is dead");
    Check(BossState(true, false, true) == 'd', "a corpse with its respawn time running is dead");
    Check(BossState(false, false, true) == 'd', "gone with its respawn time running: killed, corpse despawned");
    Check(BossState(false, false, false) == 'u', "not on the map, no respawn time: not loaded or not spawned, unknown");
    Check(BossState(false, true, false) == 'u', "not found never says alive");

    auto boss = [](uint32_t e, char st, bool plan = false, std::string n = "B") {
        BossInfo b; b.entry = e; b.name = n; b.x = static_cast<float>(e); b.y = -1.0f; b.state = st; b.inPlan = plan; return b; };
    // the cache groups the spawns of a map by entry once (no search of the output per spawn): one row per entry in first-seen order,
    // the first spawn's place and name, in the plan when any spawn is, every spawn row's guid kept, a zero entry and a zero guid dropped
    auto row = [&](uint32_t g, uint32_t e, bool plan = false, std::string n = "B") { BossSpawnRow r; r.dbGuid = g; r.info = boss(e, 'u', plan, n); return r; };
    std::vector<BossEntrySpawns> g = GroupBossSpawns({row(11, 5, false, "first"), row(12, 7), row(13, 5, true, "second"), row(0, 9), row(14, 7), row(15, 0)});
    Check(g.size() == 3, "one row per entry, a zero entry dropped");
    Check(g[0].info.entry == 5 && g[0].info.name == "first" && g[0].info.x == 5.0f, "the first spawn's name and place");
    Check(g[0].info.inPlan, "in the plan when any spawn of the entry is");
    Check(g[0].dbGuids == std::vector<uint32_t>({11, 13}) && g[1].dbGuids == std::vector<uint32_t>({12, 14}), "every spawn row of the entry kept");
    Check(g[2].info.entry == 9 && g[2].dbGuids.empty(), "a plan boss with no spawn row: a row, no guid");
    std::vector<BossSpawnRow> many;
    for (uint32_t i = 1; i <= 40; ++i)
        many.push_back(row(i, i));
    Check(GroupBossSpawns(many).size() == 40, "the grouping never cuts a roster short (the payload leaves a long one out whole)");
    // several spawns of one entry make one state: alive beats dead beats unknown, a stray letter is unknown
    Check(StrongerBossState('u', 'd') == 'd' && StrongerBossState('d', 'u') == 'd', "dead beats unknown");
    Check(StrongerBossState('d', 'a') == 'a' && StrongerBossState('a', 'd') == 'a', "alive beats dead");
    Check(StrongerBossState('u', 'a') == 'a' && StrongerBossState('u', 'u') == 'u', "alive beats unknown; unknown stays unknown");
    Check(StrongerBossState('u', 'z') == 'u' && StrongerBossState('z', 'u') == 'u', "an unknown letter becomes unknown");

    // the state from what the game hands over: on the map (alive or a corpse), else the instance's respawn time against now
    Check(BossSpawnState(true, true, 0, 100) == 'a' && BossSpawnState(true, false, 0, 100) == 'd', "found: alive or a corpse");
    Check(BossSpawnState(false, false, 101, 100) == 'd', "gone, respawn time still ahead: killed");
    Check(BossSpawnState(false, false, 100, 100) == 'u' && BossSpawnState(false, false, 50, 100) == 'u', "a respawn time that has passed: not killed");
    Check(BossSpawnState(false, false, 0, 100) == 'u', "no respawn time: not known");
    Check(BossSpawnState(false, true, 500, 100) == 'd', "the respawn time decides for a creature not found");

    // the payload: absent when not reported, [] when reported with none, the fields, the letters sanitized, the name clipped
    BindingInfo b;
    b.commander = "Gb";
    std::string json = BuildPayload(9, 1, 1, 1, {b});
    Check(!Has(json, "\"bosses\"") && !Has(json, "\"bosses_map\""), "not reported: no bosses fields");
    b.bossesReported = true;
    b.bossesMap = 70;
    json = BuildPayload(9, 1, 1, 1, {b});
    Check(Has(json, ",\"bosses_map\":70,\"bosses\":[]"), "reported, no boss known: []");
    BossInfo rev = boss(6910, 'a', true, "Revelosh");
    rev.x = -123.42f; rev.y = 56.78f;
    BossInfo arch = boss(2748, 'd', false, std::string(60, 'A'));
    BossInfo odd = boss(1, 'q', false, "Q\"x");
    b.bosses = {rev, arch, odd};
    json = BuildPayload(9, 1, 1, 1, {b});
    Check(Has(json, "{\"e\":6910,\"n\":\"Revelosh\",\"x\":-123.4,\"y\":56.8,\"s\":\"a\",\"p\":1}"), "a boss of the plan, alive");
    Check(Has(json, "{\"e\":2748,\"n\":\"" + std::string(kMaxBossName, 'A') + "\",\"x\":2748.0,\"y\":-1.0,\"s\":\"d\"}"), "outside the plan: no p; name clipped");
    Check(Has(json, "\"n\":\"Q\\\"x\"") && Has(json, "\"s\":\"u\"}"), "names escaped, a stray letter sent as unknown");
    Check(Has(json, "\"bosses\":[") && Has(json, "],\"orders\":["), "the list sits before the orders");

    // per binding: a roster of up to kMaxBossesPerBinding goes whole (Naxxramas: 18 boss-rule entries, AQ40: 16); a longer one not at all
    auto roster = [&](size_t n) { BindingInfo r = b; r.bosses.clear(); for (uint32_t i = 1; i <= n; ++i) r.bosses.push_back(boss(i, 'u')); return r; };
    json = BuildPayload(9, 1, 1, 1, {roster(18)});
    Check(Count(json, "\"e\":") == 18, "an 18-boss raid roster is sent whole");
    json = BuildPayload(9, 1, 1, 1, {roster(kMaxBossesPerBinding)});
    Check(Count(json, "\"e\":") == kMaxBossesPerBinding, "a roster at the cap is sent whole");
    json = BuildPayload(9, 1, 1, 1, {roster(kMaxBossesPerBinding + 1)});
    Check(!Has(json, "\"bosses\"") && !Has(json, "\"bosses_map\""), "a roster over the cap is left out whole, never cut short");
    // per payload: the lists go in whole while the count allows; a list that does not fit is left out, a later one that fits goes in
    json = BuildPayload(9, 1, 1, 1, std::vector<BindingInfo>(kMaxBindings, roster(20)));
    Check(Count(json, "\"bosses\":[") == kMaxBossesPerPayload / 20 && Count(json, "\"e\":") == (kMaxBossesPerPayload / 20) * 20,
        "whole lists while the per-datagram count allows");
    json = BuildPayload(9, 1, 0, 1, {roster(30), roster(30), roster(18)});
    Check(Count(json, "\"bosses\":[") == 2 && Count(json, "\"e\":") == 48, "the second list does not fit the count, the third does");
}

// The boss lists never push a datagram past the byte budget: with every other cap reached, the bindings that still fit keep their list
// (the first ones) and the rest leave it out (the dashboard then falls back to the plan and what it sees); the other fields are untouched.
void TestBossesByteBudget()
{
    auto boss = [](uint32_t e) {
        BossInfo b; b.entry = 4000000000u + e; b.name = std::string(60, 'N'); b.x = b.y = -12345.67f; b.state = 'd'; b.inPlan = true; return b; };
    BindingInfo big;
    big.commander = std::string(60, 'C');
    for (int i = 0; i < 6; ++i)
    {
        BotInfo bot = Bot("Botname" + std::to_string(i), i == 0);
        bot.id = bot.target = 0xFFFFFFFFFFFFFFFFull;
        bot.hasExtra = true;
        bot.extra.action = std::string(60, 'a');
        bot.extra.cast = std::string(60, 'c');
        bot.extra.castTarget = 0xFFFFFFFFFFFFFFFFull;
        bot.extra.targetName = std::string(60, 't');
        big.bots.push_back(bot);
    }
    for (int i = 0; i < 8; ++i)
        big.orders.push_back(Order(i, "ritual", true, std::string(kMaxOrderResult, 'r'), std::string(kMaxOrderDetail, 'd')));
    big.mobsReported = true;
    for (int i = 0; i < 40; ++i)
        big.mobs.push_back(Mob(0xFFFFFFFFFFFFFFFFull, std::string(60, 'M'), 'h', 'k', 0xFFFFFFFFFFFFFFFFull));
    big.run.present = true;
    big.run.plan = std::string(60, 'p');
    big.run.note = std::string(500, 'n');
    for (uint32_t i = 0; i < 60; ++i)
    {
        RunStepInfo st;
        st.index = 4000000000u + i;
        st.kind = "move_direct";
        st.x = st.y = -12345.67f;
        st.radius = 100.0f;
        st.entry = 4000000000u;
        st.name = std::string(60, 'S');
        st.skipped = true;
        big.run.stepList.push_back(st);
    }
    big.objectsReported = true;
    for (int i = 0; i < 40; ++i)
    {
        ObjectInfo o;
        o.id = 0xFFFFFFFFFFFFFFFFull;
        o.name = std::string(60, 'O');
        big.objects.push_back(o);
    }
    std::string const without = BuildPayload(9, 1, 1, 1, std::vector<BindingInfo>(kMaxBindings, big));
    big.bossesReported = true;
    big.bossesMap = 4000000000u;
    for (uint32_t i = 0; i < 16; ++i)
        big.bosses.push_back(boss(i));
    std::string const one = BuildPayload(9, 1, 1, 1, {big});
    Check(Count(one, "\"s\":\"d\"") == 16, "one full binding keeps its whole list");
    std::printf("one full binding with bosses: %zu bytes\n", one.size());
    std::string const json = BuildPayload(9, 1, 1, 1, std::vector<BindingInfo>(kMaxBindings, big));
    Check(json.size() <= kPayloadByteBudget, "the worst case with bosses stays within the byte budget");
    Check(json.size() < 60000, "and fits one datagram");
    size_t const lists = Count(json, "\"bosses\":[");
    Check(lists < kMaxBindings, "the bindings past the budget leave their list out");
    Check(Count(json, "\"bosses_map\":") == lists, "a binding without its list has no bosses_map either");
    Check(Count(json, "\"s\":\"d\"") == lists * 16, "a binding that keeps its list keeps all of it");
    Check(json.size() - without.size() == lists * (one.size() - BuildPayload(9, 1, 1, 1, {[&] { BindingInfo c = big; c.bossesReported = false; return c; }()}).size()),
        "nothing but the lists changes");
    std::printf("worst case with bosses (%zu bindings, %zu lists kept): %zu bytes\n", kMaxBindings, lists, json.size());
    // a heavy but ordinary report (no plan steps, no objects): the lists fit, the first bindings keep theirs (the payload cap stops the rest)
    BindingInfo dungeon = big;
    dungeon.run.stepList.clear();
    dungeon.objects.clear();
    std::string const usual = BuildPayload(9, 1, 0, 1, std::vector<BindingInfo>(kMaxBindings, dungeon));
    Check(Count(usual, "\"bosses\":[") == kMaxBossesPerPayload / 16, "the lists that the payload count allows all fit");
    Check(usual.find("\"bosses\":[") < usual.find("\"commander\":\"" + big.commander + "\"", usual.find("\"commander\":\"") + 1),
        "the first binding keeps its list");
    Check(usual.size() <= kPayloadByteBudget, "within the budget");
    std::printf("8 dungeon bindings with bosses: %zu bytes\n", usual.size());
}

// A list the byte budget leaves out does not use up the per-datagram count: a later list that fits both still goes in.
void TestBossesBudgetDoesNotCount()
{
    auto mk = [](size_t n, size_t nameLen) {
        BindingInfo r;
        r.commander = "C";
        r.bossesReported = true;
        r.bossesMap = 70;
        for (uint32_t i = 1; i <= n; ++i)
        {
            BossInfo b; b.entry = i; b.name = std::string(nameLen, 'N'); b.state = 'u'; r.bosses.push_back(b);
        }
        return r;
    };
    BindingInfo pad = mk(0, 0);
    pad.bossesReported = false;
    BindingInfo big = mk(30, kMaxBossName);   // ~30 x 60 bytes
    BindingInfo small = mk(20, 1);            // ~20 x 36 bytes
    std::string const bigList = BuildPayload(9, 1, 1, 1, {big}), smallList = BuildPayload(9, 1, 1, 1, {small});
    std::string const bare = BuildPayload(9, 1, 1, 1, {pad, mk(0, 0), mk(0, 0)});
    size_t const bigLen = bigList.size() - BuildPayload(9, 1, 1, 1, {[&] { BindingInfo c = big; c.bossesReported = false; return c; }()}).size();
    size_t const smallLen = smallList.size() - BuildPayload(9, 1, 1, 1, {[&] { BindingInfo c = small; c.bossesReported = false; return c; }()}).size();
    Check(bigLen > smallLen + 200, "the test needs a big and a small list");
    // pad the first binding so that the small list fits the budget and the big one does not
    pad.commander = std::string(kPayloadByteBudget - bare.size() - (bigLen + smallLen) / 2 - 2 * 20, 'P');
    std::string const json = BuildPayload(9, 1, 0, 1, {pad, big, small}); // seq 0: the big list is tried first
    Check(json.size() <= kPayloadByteBudget, "within the budget");
    Check(Count(json, "\"bosses\":[") == 1 && Count(json, "\"e\":") == 20,
        "the big list is left out by the budget and the small one still goes in (30 + 20 would pass the count of 48)");
}

// Codex review 2026-10-22: with the lists tried in a fixed order, a binding late in the list could lose its roster every snapshot. The
// first list tried moves on with the snapshot sequence, so over as many snapshots as there are lists every binding is sent whole at least
// once (each list is the first one tried once), and a list still never goes in short.
void TestBossRotation()
{
    auto mk = [](uint32_t map, size_t n) {
        BindingInfo r;
        r.commander = "C" + std::to_string(map);
        r.bossesReported = true;
        r.bossesMap = map;
        for (uint32_t i = 1; i <= n; ++i)
        {
            BossInfo b; b.entry = i; b.name = "B"; b.state = 'u'; r.bosses.push_back(b);
        }
        return r;
    };
    // five bindings of 30 bosses: only one list fits the 48 of a datagram
    std::vector<BindingInfo> five;
    for (uint32_t m = 101; m <= 105; ++m)
        five.push_back(mk(m, 30));
    std::vector<int> sent(5, 0);
    for (uint64_t seq = 1000; seq < 1000 + five.size(); ++seq)
    {
        std::string const json = BuildPayload(9, 1, seq, 1, five);
        Check(Count(json, "\"bosses\":[") == 1 && Count(json, "\"e\":") == 30, "one whole list per datagram, never a short one");
        for (size_t i = 0; i < five.size(); ++i)
            sent[i] += Has(json, "\"bosses_map\":" + std::to_string(101 + i) + ",") ? 1 : 0;
    }
    for (size_t i = 0; i < five.size(); ++i)
        Check(sent[i] >= 1, "over as many snapshots as lists, every binding's roster is sent at least once");
    // a binding whose roster is over the cap is no candidate: the others still take turns
    std::vector<BindingInfo> mixed = {mk(201, 30), mk(202, kMaxBossesPerBinding + 1), mk(203, 30)};
    bool got201 = false, got203 = false;
    for (uint64_t seq = 0; seq < 2; ++seq)
    {
        std::string const json = BuildPayload(9, 1, seq, 1, mixed);
        Check(!Has(json, "\"bosses_map\":202,"), "an over-cap roster is never sent");
        got201 = got201 || Has(json, "\"bosses_map\":201,");
        got203 = got203 || Has(json, "\"bosses_map\":203,");
    }
    Check(got201 && got203, "the candidates take turns");
}

// Codex review 2026-10-22 (lead's decision): a killed boss whose respawn timer ran out while its grid was unloaded read 'u' again. The
// latch keeps it killed for that instance until a living creature of the entry is seen, the instance id changes (a reset gives a new id)
// or no binding has stood on the instance for kForgetAfterS; bounded at kMaxLatches (the oldest is dropped).
void TestBossKillLatch()
{
    BossKillLatch l;
    Check(l.Apply(7, 100, 'u', 0) == 'u', "nothing latched: unknown stays unknown");
    Check(l.Apply(7, 100, 'd', 10) == 'd' && l.Holds(7, 100), "read killed: latched");
    Check(l.Apply(7, 100, 'u', 20) == 'd', "killed, then the timer ran out with the grid unloaded (not found): still killed");
    Check(l.Apply(7, 101, 'u', 20) == 'u', "another entry of the instance is not latched");
    Check(l.Apply(8, 100, 'u', 20) == 'u', "a new instance id (a reset) starts fresh");
    Check(l.Apply(7, 100, 'a', 30) == 'a' && !l.Holds(7, 100), "a living creature of the entry clears the latch");
    Check(l.Apply(7, 100, 'u', 40) == 'u', "cleared: unknown again");
    Check(l.Apply(0, 100, 'd', 40) == 'd' && !l.Holds(0, 100), "no instance id: nothing latched");
    // forgotten once no binding has stood on the instance for kForgetAfterS
    l.Apply(9, 300, 'd', 1000);
    l.Expire(1000 + BossKillLatch::kForgetAfterS - 1);
    Check(l.Holds(9, 300), "kept while a binding stood there recently");
    l.Apply(9, 300, 'u', 1000 + BossKillLatch::kForgetAfterS - 1); // still seen: the clock restarts
    l.Expire(1000 + 2 * BossKillLatch::kForgetAfterS - 2);
    Check(l.Holds(9, 300), "every report on the instance keeps it");
    l.Expire(1000 + 2 * BossKillLatch::kForgetAfterS);
    Check(!l.Holds(9, 300), "no binding on the instance for the forget time: dropped");
    // the size bound: the oldest latch goes first
    BossKillLatch b;
    for (uint32_t i = 1; i <= BossKillLatch::kMaxLatches + 5; ++i)
        b.Apply(1, i, 'd', 0);
    Check(b.Size() == BossKillLatch::kMaxLatches, "at most kMaxLatches latches");
    Check(!b.Holds(1, 1) && !b.Holds(1, 5) && b.Holds(1, 6) && b.Holds(1, BossKillLatch::kMaxLatches + 5), "the oldest dropped first");
    b.Apply(1, 6, 'd', 1); // read killed again: not a new latch, the size stays
    Check(b.Size() == BossKillLatch::kMaxLatches, "a latch read again is not added twice");
}

void TestControlCharacters()
{
    BindingInfo b;
    b.commander = std::string("a\x01" "b\tc");
    std::string const json = BuildPayload(9, 1, 1, 1, {b});
    Check(Has(json, "a\\u0001b\\tc"), "control characters are escaped");
}

// F5: the dashboard's raid layout (what TBM:RAID_LAYOUT applied) and the run's step label (the plan row's comment).
void TestDashLayoutAndLabel()
{
    BindingInfo b;
    b.commander = "Glacienne";
    b.run.present = true;
    b.run.plan = "onyxias_lair";
    b.run.step = 28;
    b.run.steps = 40;
    b.run.label = "boss stand outside her \"62\" yd wake";
    RunStepInfo st;
    st.index = 28;
    st.kind = "move";
    st.label = "boss stand outside her ~62 yd 3D wake radius and the raid's follow ring";
    b.run.stepList.push_back(st);
    std::string const json = BuildPayload(9, 1, 1, 1, {b});
    Check(Has(json, ",\"label\":\"boss stand outside her \\\"62\\\" yd wake\""), "the current step's label, escaped");
    Check(Has(json, ",\"c\":\"boss stand outside her ~62 yd 3D wake radius and the raid's follow ring\""), "a listed step carries its own label");
    Check(!Has(json, "\"layout\""), "no layout: no layout key");

    b.layout.present = true;
    b.layout.mainTank = "Ragemeister";
    b.layout.offTanks = {"Irelanthe", "Brun"};
    b.layout.tankHealers = {{"Sera", "Ragemeister"}};
    b.layout.raidHealers = {"Prayer"};
    std::string const laid = BuildPayload(9, 1, 1, 1, {b});
    Check(Has(laid, "\"layout\":{\"mt\":\"Ragemeister\",\"ot\":[\"Irelanthe\",\"Brun\"],\"th\":[{\"h\":\"Sera\",\"t\":\"Ragemeister\"}],\"rh\":[\"Prayer\"]}"),
        "the raid layout, with its tank healer pairs");

    // F5: the layout follows the group. Off-tank Brun left, so the next datagram no longer names it.
    BindingInfo departed = b;
    departed.layout = CurrentRaidLayout(b.layout, {"Ragemeister", "Irelanthe", "Sera", "Prayer"});
    std::string const shrunk = BuildPayload(9, 1, 1, 1, {departed});
    Check(Has(shrunk, "\"layout\":{\"mt\":\"Ragemeister\",\"ot\":[\"Irelanthe\"],\"th\":[{\"h\":\"Sera\",\"t\":\"Ragemeister\"}],\"rh\":[\"Prayer\"]}"),
        "a departed off-tank is gone from the next layout");
    Check(!Has(shrunk, "Brun"), "the departed off-tank's name is nowhere in the datagram");
}

// The report's millisecond timestamp, so the daemon can order positions
// against the last full report (additive; 0 omits it).
void TestPayloadTsMs()
{
    BindingInfo b;
    b.commander = "Glacienne";
    b.commanderGuid = 12;
    b.bots.push_back(Bot("Forthur", true));
    std::string const json = BuildPayload(9, 77, 5, 1791990010, {b}, 1791990010123);
    Check(Has(json, "\"ts\":1791990010,\"ts_ms\":1791990010123,\"type\":\"COMMANDERS\""), "ts_ms follows ts");
    std::string const plain = BuildPayload(9, 77, 5, 1791990010, {b});
    Check(!Has(plain, "\"ts_ms\""), "a zero ms timestamp omits the key");
}

// The ready check's wire shape (R4m): the bits, the cooldown counts and the
// longest time left, on the bots whose facts were read; nothing without them.
void TestReadyCheckPayload()
{
    BindingInfo b;
    b.commander = "Zfmagea";
    b.commanderGuid = 9;
    BotInfo bot = Bot("Forthur", true);
    bot.hasExtra = true;
    bot.extra.hasReady = true;
    bot.extra.readyBuffs = 13;
    bot.extra.readyCons = 6;
    bot.extra.cdReady = 2;
    bot.extra.cdKnown = 3;
    bot.extra.cdLongestS = 142;
    b.bots.push_back(bot);
    b.bots.push_back(Bot("Plain", false));
    std::string const json = BuildPayload(9, 1, 1, 1, {b});
    Check(Has(json, "\"rdy\":{\"b\":13,\"c\":6,\"cr\":2,\"cn\":3,\"cl\":142}"),
        "a bot's ready check writes the bits, the counts and the longest left");

    b.bots[0].extra.cdLongestS = 0;
    std::string const noLeft = BuildPayload(9, 1, 1, 1, {b});
    Check(Has(noLeft, "\"rdy\":{\"b\":13,\"c\":6,\"cr\":2,\"cn\":3}"), "a longest left of 0 omits cl");
    Check(!Has(noLeft, "\"cl\":0"), "no zero cl key");

    b.bots[0].extra.hasReady = false;
    Check(!Has(BuildPayload(9, 1, 1, 1, {b}), "\"rdy\""), "no ready facts: no rdy key");

    // A full raid with the facts: the 40-bot binding with everything written stays under the byte budget.
    BindingInfo raid;
    raid.commander = "C";
    for (int i = 0; i < 40; ++i)
    {
        BotInfo x = Bot("Botname" + std::to_string(i), i == 0);
        x.id = 0xFFFFFFFFFFFFFFFFull;
        x.hasExtra = true;
        x.extra.state = 'c';
        x.extra.action = std::string(90, 'a');
        x.extra.result = 'O';
        x.extra.cast = std::string(90, 'c');
        x.extra.cc = 's';
        x.extra.rest = 'e';
        x.extra.moving = true;
        x.extra.targetName = std::string(90, 't');
        x.extra.hasReady = true;
        x.extra.readyBuffs = 0xFFFFFFFFu;
        x.extra.readyCons = 0xFFFFFFFFu;
        x.extra.cdReady = 8;
        x.extra.cdKnown = 8;
        x.extra.cdLongestS = 0xFFFFFFFFu;
        raid.bots.push_back(x);
    }
    std::string const size = BuildPayload(9, 1, 1, 1, {raid});
    Check(Count(size, "\"rdy\":") == 40, "every bot of the raid carries its facts");
    Check(size.size() < kPayloadByteBudget, "a 40-bot raid with ready facts stays under the payload budget");
    std::printf("40 bots with ready facts: %zu bytes\n", size.size());
}

// `count` bindings of `size` bots each, every commander, lead and bot named apart.
std::vector<BindingInfo> Parties(size_t count, size_t size)
{
    std::vector<BindingInfo> out;
    out.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        BindingInfo b;
        b.commander = "Party" + std::to_string(i);
        b.commanderGuid = static_cast<uint32_t>(100 + i);
        b.groupId = static_cast<uint32_t>(500 + i);
        b.lead = "Lead" + std::to_string(i);
        for (size_t j = 0; j < size; ++j)
            b.bots.push_back(Bot("Bot" + std::to_string(i) + "_" + std::to_string(j), j == 0));
        out.push_back(std::move(b));
    }
    return out;
}

// More than eight bindings (task R8m): a cycle goes out in parts, each part built exactly as a whole cycle is today,
// so a single-part cycle (eight bindings, 64 bots) is byte-identical to the payload before the split.
void TestCommandersPartsSingle()
{
    std::vector<BindingInfo> const parties = Parties(8, 5);
    std::vector<std::string> const parts = BuildPayloadParts(9, 7, 3, 1791990010, parties, 1791990010123);
    Check(parts.size() == 1, "eight parties are one part");
    Check(!Has(parts[0], "\"parts\""), "a single part writes no parts key");
    Check(parts[0] == BuildPayload(9, 7, 3, 1791990010, parties, 1791990010123),
        "a single part is byte-identical to the plain payload");
    Check(Count(parts[0], "\"commander\":") == 8, "every binding is in it");
}

void TestCommandersPartsSplit()
{
    std::vector<std::string> const parts = BuildPayloadParts(9, 7, 3, 1791990010, Parties(9, 5), 1791990010123);
    Check(parts.size() == 2, "nine parties are two parts");
    if (parts.size() != 2)
        return;
    Check(Has(parts[0], "\"seq\":3,\"part\":0,\"parts\":2,\"ts\":1791990010,\"ts_ms\":1791990010123,\"type\":\"COMMANDERS\""),
        "part 0 carries the cycle's seq and ts");
    Check(Has(parts[1], "\"seq\":3,\"part\":1,\"parts\":2,\"ts\":1791990010,\"ts_ms\":1791990010123,\"type\":\"COMMANDERS\""),
        "part 1 carries the same seq and ts");
    Check(Count(parts[0], "\"commander\":") == 8 && Count(parts[1], "\"commander\":") == 1, "eight bindings, then the ninth");
    Check(Count(parts[0], "\"guid\":") == 40 && Count(parts[1], "\"guid\":") == 5, "the bots follow their binding");
}

void TestCommandersPartsCaps()
{
    // Eight small parties at the bot cap still fit one part.
    std::vector<std::string> const full = BuildPayloadParts(9, 1, 43, 1, Parties(8, 8), 0);
    Check(full.size() == 1, "eight bindings of eight bots are one part");
    if (!full.empty())
        Check(Count(full[0], "\"guid\":") == 64, "and 64 bots in it");
    // Two raids of 40: 64 bots per part, so each raid goes alone.
    std::vector<std::string> const two = BuildPayloadParts(9, 1, 40, 1, Parties(2, 40), 0);
    Check(two.size() == 2, "two 40-bot raids are two parts");
    if (two.size() == 2)
        Check(Count(two[0], "\"guid\":") == 40 && Count(two[1], "\"guid\":") == 40,
            "each raid keeps its whole roster");
    // A raid of 40 and five parties of 5: the raid and four parties (60 bots) go together.
    std::vector<BindingInfo> mixed = Parties(1, 40);
    std::vector<BindingInfo> const parties = Parties(5, 5);
    mixed.insert(mixed.end(), parties.begin(), parties.end());
    std::vector<std::string> const parts = BuildPayloadParts(9, 1, 41, 1, mixed, 0);
    Check(parts.size() == 2, "a raid and five parties are two parts");
    if (parts.size() == 2)
    {
        Check(Count(parts[0], "\"commander\":") == 5 && Count(parts[0], "\"guid\":") == 60, "the raid and four parties in part 0");
        Check(Count(parts[1], "\"commander\":") == 1 && Count(parts[1], "\"guid\":") == 5, "the fifth party alone");
    }
    // Twenty 40-bot bindings: one part each, and no binding is ever split.
    std::vector<std::string> const many = BuildPayloadParts(9, 1, 42, 1, Parties(20, 40), 0);
    Check(many.size() == 20, "twenty raids are twenty parts");
    bool whole = true;
    for (std::string const& part : many)
        whole = whole && Count(part, "\"commander\":") == 1 && Count(part, "\"guid\":") == 40;
    Check(whole, "a binding is never split across parts");
    // At most one part per binding, so no more than kMaxCommandersReported of them.
    std::vector<std::string> const beyond = BuildPayloadParts(9, 1, 44, 1, Parties(21, 40), 0);
    Check(beyond.size() == kMaxCommandersReported, "twenty-one bindings are twenty parts");
    size_t reported = 0;
    for (std::string const& part : beyond)
        reported += Count(part, "\"commander\":");
    Check(reported == kMaxCommandersReported, "the twenty-first binding is not reported");
}

void TestCommandersPartsBudget()
{
    // The worst datagram this module writes (the shape of TestUnitInfo's caps case, with the ready facts): 8 bindings,
    // 48 bots and 64 mobs, ids at 20 digits and every name at its clip.
    std::vector<BindingInfo> many;
    for (int i = 0; i < 8; ++i)
    {
        BindingInfo b;
        b.commander = std::string(40, 'C');
        b.commanderGuid = 0xFFFFFFFFu;
        b.boundAt = 1791990000;
        b.lead = std::string(12, 'L');
        for (int j = 0; j < 6; ++j)
        {
            BotInfo bot = Bot("Botname" + std::to_string(i * 8 + j), j == 0);
            bot.id = 0xFFFFFFFFFFFFFFFFull;
            bot.target = 0xFFFFFFFFFFFFFFFFull;
            bot.hasExtra = true;
            bot.extra.state = 'c';
            bot.extra.action = std::string(kMaxActionName, 'a');
            bot.extra.result = 'O';
            bot.extra.cast = std::string(kMaxCastName, 'c');
            bot.extra.targetName = std::string(kMaxTargetName, 't');
            bot.extra.hasReady = true;
            bot.extra.readyBuffs = 0xFFFFFFFFu;
            bot.extra.readyCons = 0xFFFFFFFFu;
            bot.extra.cdReady = 8;
            bot.extra.cdKnown = 8;
            bot.extra.cdLongestS = 0xFFFFFFFFu;
            b.bots.push_back(bot);
        }
        b.mobsReported = true;
        b.mobsMap = 249;
        for (int j = 0; j < 8; ++j)
        {
            MobInfo m = Mob(0xFFFFFFFFFFFFFFFFull, std::string(kMaxMobName, 'M'), 'h', 'k', 0xFFFFFFFFFFFFFFFFull);
            m.level = 63;
            m.rank = 'b';
            m.cast = std::string(kMaxCastName, 'c');
            m.hasO = true;
            m.o = -3.14159f;
            m.castId = 4294967295u;
            m.castLeftMs = 0xFFFFFFFFu;
            m.castTotalMs = 0xFFFFFFFFu;
            m.castTarget = 0xFFFFFFFFFFFFFFFFull;
            m.hasShape = true;
            m.shape.kind = TortoiseBots::SpellShape::Kind::Line;
            m.shape.length = 154.0f;
            m.shape.width = 60.0f;
            b.mobs.push_back(m);
        }
        for (int j = 0; j < 8; ++j)
            b.orders.push_back(Order(j, std::string(kMaxOrderResult / 15, 'v'), true, std::string(kMaxOrderResult, 'r'),
                std::string(kMaxOrderDetail, 'd')));
        many.push_back(std::move(b));
    }
    std::vector<std::string> const parts = BuildPayloadParts(9, 1, 77, 1791990010, many, 1791990010123);
    Check(parts.size() == 1, "eight fat bindings with their mobs are still one part");
    if (!parts.empty())
    {
        Check(Count(parts[0], "\"guid\":") == 48, "every bot is in it");
        Check(Count(parts[0], "\"entry\":") == 64, "the part sits at the mob cap");
    }
    bool under = true;
    size_t largest = 0;
    for (std::string const& part : parts)
    {
        under = under && part.size() < kPayloadByteBudget;
        largest = std::max(largest, part.size());
    }
    Check(under, "every part stays under the payload budget");
    std::printf("worst part (%zu parts): %zu bytes\n", parts.size(), largest);

    // Twenty full bindings: twenty parts, each under the budget.
    std::vector<std::string> const raidParts = BuildPayloadParts(9, 1, 78, 1, Parties(20, 40), 0);
    bool raidUnder = true;
    for (std::string const& part : raidParts)
        raidUnder = raidUnder && part.size() < kPayloadByteBudget;
    Check(raidUnder, "every part of twenty raids stays under the payload budget");
}

// One bot carrying every per-bot section at its cap: unit info, cast target, boss debuffs, ready facts.
BotInfo CombinedBot(std::string name, bool lead)
{
    BotInfo b = Bot(std::move(name), lead);
    b.id = 0xFFFFFFFFFFFFFFFFull;
    b.target = 0xFFFFFFFFFFFFFFFFull;
    b.hasExtra = true;
    b.extra.state = 'c';
    b.extra.action = std::string(kMaxActionName, 'a');
    b.extra.result = 'O';
    b.extra.cast = std::string(kMaxCastName, 'c');
    b.extra.castTarget = 0xFFFFFFFFFFFFFFFFull;
    b.extra.cc = 's';
    b.extra.rest = 'e';
    b.extra.moving = true;
    b.extra.targetName = std::string(kMaxTargetName, 't');
    for (uint32_t i = 0; i < kMaxBotDebuffs; ++i)
    {
        AuraInfo a;
        a.id = 4294967295u - i;
        a.name = std::string(kMaxCastName, 'd');
        a.stacks = 4294967295u;
        a.leftMs = 4294967295u;
        a.dispelType = i % 2 ? 'm' : 0;
        b.extra.debuffs.push_back(a);
    }
    b.extra.mc = true;
    b.extra.hasReady = true;
    b.extra.readyBuffs = 0xFFFFFFFFu;
    b.extra.readyCons = 0xFFFFFFFFu;
    b.extra.cdReady = 8;
    b.extra.cdKnown = 8;
    b.extra.cdLongestS = 0xFFFFFFFFu;
    b.subgroup = 0;
    return b;
}

// One binding with the sections that can coexist, each at its cap: bots with ready facts, the run with its step list and
// its notes ring, orders, objects, an overlay on every mob, and a fight window of `combat` bytes (the window itself is
// not one of the sections a trim drops).
BindingInfo CombinedBinding(size_t bots, size_t notes, size_t mobs, std::string combat = "")
{
    BindingInfo b;
    b.commander = std::string(40, 'C');
    b.commanderGuid = 0xFFFFFFFFu;
    b.boundAt = 1791990000;
    b.commanderOnline = true;
    b.viewOn = true;
    b.groupId = 0xFFFFFFFFu;
    b.lead = std::string(12, 'L');
    b.commanderPlaced = true;
    b.commanderMap = 249;
    b.commanderX = b.commanderY = -12345.67f;
    for (size_t i = 0; i < bots; ++i)
        b.bots.push_back(CombinedBot("Botname" + std::to_string(i), i == 0));
    b.run.present = true;
    b.run.plan = std::string(kMaxRunWord, 'p');
    b.run.state = "running";
    b.run.step = 4000000000u;
    b.run.steps = 4000000000u;
    b.run.leg = std::string(kMaxRunWord, 'l');
    b.run.kind = std::string(kMaxRunWord, 'k');
    b.run.phase = std::string(kMaxRunWord, 'h');
    b.run.label = std::string(kMaxRunNote, 'L');
    b.run.note = std::string(kMaxRunNote, 'n');
    b.run.changedAt = 1791990000;
    for (size_t i = 0; i < notes; ++i)
    {
        RunNoteInfo n;
        n.at = 1791990000;
        n.step = 4000000000u;
        n.leg = std::string(kMaxRunWord, 'l');
        n.kind = std::string(kMaxRunWord, 'k');
        n.phase = std::string(kMaxRunWord, 'h');
        n.state = std::string(kMaxRunWord, 's');
        n.note = std::string(kMaxRunNote, 'n');
        b.run.notes.push_back(n);
    }
    for (uint32_t i = 0; i < kMaxRunSteps; ++i)
    {
        RunStepInfo st;
        st.index = 4000000000u + i;
        st.kind = "move_direct";
        st.x = st.y = -12345.67f;
        st.radius = 100.0f;
        st.entry = 4000000000u;
        st.name = std::string(kMaxMobName, 'S');
        st.label = std::string(kMaxRunNote, 'c');
        st.skipped = true;
        b.run.stepList.push_back(st);
    }
    b.mobsReported = true;
    b.mobsMap = 249;
    for (size_t i = 0; i < mobs; ++i)
    {
        MobInfo m = Mob(0xFFFFFFFFFFFFFFFFull, std::string(kMaxMobName, 'M'), 'h', 'k', 0xFFFFFFFFFFFFFFFFull);
        m.level = 63;
        m.rank = 'b';
        m.cast = std::string(kMaxCastName, 'c');
        m.hasO = true;
        m.o = -3.14159f;
        m.castId = 4294967295u;
        m.castLeftMs = 0xFFFFFFFFu;
        m.castTotalMs = 0xFFFFFFFFu;
        m.castTarget = 0xFFFFFFFFFFFFFFFFull;
        m.hasShape = true;
        m.shape.kind = TortoiseBots::SpellShape::Kind::Line;
        m.shape.length = 154.0f;
        m.shape.width = 60.0f;
        for (uint32_t j = 0; j < kMaxMobAuras; ++j)
        {
            AuraInfo a;
            a.id = 4294967295u - j;
            a.name = std::string(kMaxCastName, 'a');
            a.stacks = 4294967295u;
            a.leftMs = 4294967295u;
            m.auras.push_back(a);
        }
        m.flags = "nihde";
        b.mobs.push_back(m);
    }
    b.objectsReported = true;
    for (size_t i = 0; i < kMaxObjectsPerBinding; ++i)
    {
        ObjectInfo o;
        o.id = 0xFFFFFFFFFFFFFFFFull;
        o.entry = 4294967295u;
        o.name = std::string(kMaxObjectName, 'O');
        o.x = o.y = -12345.67f;
        o.type = 'c';
        o.state = 'u';
        b.objects.push_back(o);
    }
    for (int i = 0; i < 8; ++i)
        b.orders.push_back(Order(1791990000, std::string(10, 'v'), true, std::string(kMaxOrderResult, 'r'),
                                 std::string(kMaxOrderDetail, 'd')));
    b.combatJson = std::move(combat);
    return b;
}

// The byte cap holds on the fully serialized part, not on its counts: every section a binding can carry rides along,
// and a part that would go past kPartByteCap moves bindings to a new part (never splits one).
void TestCommandersPartsCombinedCap()
{
    // Twenty commanders, 40 bots each: every section at its cap, and each part (one binding) under the cap.
    std::vector<BindingInfo> raids;
    for (int i = 0; i < 20; ++i)
        raids.push_back(CombinedBinding(40, kMaxRunNotes, kMaxMobsPerBinding, std::string(4000, 'x')));
    std::vector<std::string> const raidParts = BuildPayloadParts(9, 1, 91, 1, raids, 0);
    Check(raidParts.size() == 20, "twenty 40-bot bindings are twenty parts");
    bool under = true;
    for (std::string const& part : raidParts)
        under = under && part.size() <= kPartByteCap && part.size() <= kDatagramByteLimit;
    Check(under, "every part of twenty full commanders is at or under the datagram cap");

    // Twenty commanders, 40 bots in all (2 each) with the payload's 64 mobs and 16 overlays filled: the counts alone
    // packed eight such bindings (173,034 bytes) into one datagram.
    std::vector<BindingInfo> combined;
    for (int i = 0; i < 20; ++i)
        combined.push_back(CombinedBinding(2, kMaxRunNotes, 8, std::string(4000, 'x')));
    std::vector<BindingInfo> firstEight(combined.begin(), combined.begin() + 8);
    std::string const countsOnly = BuildPayload(9, 1, 91, 1, firstEight);
    std::printf("eight combined bindings by counts alone: %zu bytes\n", countsOnly.size());
    Check(countsOnly.size() > kDatagramByteLimit, "the counts-only packing would exceed the UDP limit");
    std::vector<TrimEvent> packTrims;
    std::vector<std::string> const parts = BuildPayloadParts(9, 1, 91, 1, combined, 0, &packTrims);
    size_t largest = 0;
    size_t listed = 0;
    bool fit = true;
    for (std::string const& part : parts)
    {
        fit = fit && part.size() <= kPartByteCap && part.size() <= kDatagramByteLimit;
        largest = std::max(largest, part.size());
        listed += Count(part, "\"commander\":");
    }
    Check(fit, "every combined part is at or under the datagram cap");
    Check(listed == 20, "every commander is still reported");
    Check(parts.size() > 2, "the byte cap moved bindings to new parts");
    Check(packTrims.empty(), "a part over the cap moves bindings to a new part instead of trimming them");
    std::printf("twenty combined commanders (40 bots): %zu parts, largest %zu bytes\n", parts.size(), largest);

    // A part that is one binding still over the cap drops its optional sections in the fixed order, and keeps the rest.
    std::vector<BindingInfo> single{CombinedBinding(40, kMaxRunNotes, kMaxMobsPerBinding, std::string(15000, 'x'))};
    std::vector<BindingInfo const*> refs{&single[0]};
    static char const* const kSectionNames[] = {"none", "bot debuffs", "overlays", "objects", "step notes", "ready facts"};
    for (uint8_t level = 0; level <= kMaxTrimLevel; ++level)
    {
        std::vector<uint8_t> const levels{level};
        std::string const json = BuildPayloadOf(9, 1, 91, 1, refs, 0, 0, 1, &levels);
        bool const debuffs = Has(json, "\"db\":[");
        bool const overlay = Has(json, "\"cid\":");
        bool const objects = Has(json, "\"objects\":[");
        bool const notes = Has(json, "\"notes\":[");
        bool const ready = Has(json, "\"rdy\":{");
        Check(debuffs == (level < 1), "the bot debuffs are the first section a trim drops");
        Check(overlay == (level < 2), "overlays go after the bot debuffs");
        Check(objects == (level < 3), "objects go after the overlays");
        Check(notes == (level < 4), "the step notes go after the objects");
        Check(ready == (level < 5), "the ready facts go last");
        Check(Has(json, "\"au\":[") == (level < 2), "the boss auras ride the overlays and go with them");
        Check(Has(json, "\"plan_steps\":["), "the run's step list stays at every trim level");
        Check(Has(json, "\"trimmed\":true") == (level > 0), "a trimmed binding says so");
        Check(std::string(TrimSectionName(level)) == kSectionNames[level], "the trim level names the section it dropped");
    }

    // The cap picks the first level that fits, and reports it once in the trim log.
    std::vector<TrimEvent> trimLog;
    std::vector<std::string> const trimmed = BuildPayloadParts(9, 1, 91, 1, single, 0, &trimLog);
    Check(trimmed.size() == 1, "a binding alone over the cap is still one part");
    Check(trimmed[0].size() <= kPartByteCap, "the trimmed part fits the datagram cap");
    Check(Has(trimmed[0], "\"trimmed\":true"), "the trimmed binding is marked");
    Check(!Has(trimmed[0], "\"db\":[") && !Has(trimmed[0], "\"au\":[") && !Has(trimmed[0], "\"cid\":") &&
              !Has(trimmed[0], "\"objects\":[") && !Has(trimmed[0], "\"notes\":["),
        "the bot debuffs and overlays went first, and with them the objects and the notes");
    Check(Has(trimmed[0], "\"rdy\":{"), "the ready facts are kept when the earlier trims were enough");
    Check(trimLog.size() == 1 && trimLog[0].level == 4,
        "the log names the level that fit (the debuffs, overlays, objects and notes went)");
    Check(trimLog.size() == 1 && trimLog[0].commanderGuid == 0xFFFFFFFFu && trimLog[0].commander == std::string(40, 'C'),
        "the log names the binding");

    // A binding that fits is not trimmed and logs nothing.
    std::vector<BindingInfo> const small{CombinedBinding(2, 3, 2)};
    trimLog.clear();
    std::vector<std::string> const plain = BuildPayloadParts(9, 1, 91, 1, small, 0, &trimLog);
    Check(plain.size() == 1 && !Has(plain[0], "\"trimmed\":true"), "a binding under the cap is not trimmed");
    Check(trimLog.empty(), "no trim, no log line");
}

void TestCommandersPartsEmpty()
{
    std::vector<std::string> const parts = BuildPayloadParts(9, 1, 2, 3, {}, 4);
    Check(parts.size() == 1, "no binding is still one payload");
    Check(parts[0] == BuildPayload(9, 1, 2, 3, {}, 4), "the last binding ended payload is unchanged");
    Check(!Has(parts[0], "\"part\""), "no parts key on it");
}

} // namespace

int main()
{
    TestClip();
    TestOrderLogRing();
    TestOrderLogBoundsCommanders();
    TestPayload();
    TestPayloadCapsAndSize();
    TestMapData();
    TestMapDataCapsAndSize();
    TestUnitInfo();
    TestBossOverlay();
    TestAuraSelection();
    TestBossStatePayload();
    TestBotDebuffPayload();
    TestAuraNameEscaping();
    TestPosPayload();
    TestPosCapsAndSize();
    TestRunInfo();
    TestPathData();
    TestBosses();
    TestBossesByteBudget();
    TestBossesBudgetDoesNotCount();
    TestBossRotation();
    TestBossKillLatch();
    TestControlCharacters();
    TestDashLayoutAndLabel();
    TestPayloadTsMs();
    TestReadyCheckPayload();
    TestCommandersPartsSingle();
    TestCommandersPartsSplit();
    TestCommandersPartsCaps();
    TestCommandersPartsBudget();
    TestCommandersPartsCombinedCap();
    TestCommandersPartsEmpty();
    TestRaidPayload();
    if (g_failures)
    {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }
    std::puts("test_commander_telemetry: all checks passed");
    return EXIT_SUCCESS;
}
