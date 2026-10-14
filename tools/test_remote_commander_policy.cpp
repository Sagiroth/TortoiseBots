// Standalone tests for the remote commander policy (issue #884, phase 1):
// the binding table and its release rule, the bind gate, the order auth gate
// (tactical orders for the bound commander, owner-or-GM for revive/summon),
// GUID and move-point parsing, and the target and move verdicts.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_remote_commander_policy.cpp -o /tmp/test_remote_commander_policy
//   /tmp/test_remote_commander_policy

#include "../runtime/RemoteCommanderPolicy.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include <cstdlib>

using namespace TortoiseBots::RemoteCommander;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

static Binding MakeBinding(uint64_t commander, uint64_t lead, uint32_t group)
{
    Binding binding;
    binding.commanderGuid = commander;
    binding.leadGuid = lead;
    binding.groupId = group;
    return binding;
}

static BindingState Standing(uint32_t group)
{
    BindingState state;
    state.enabled = true;
    state.commanderOnline = true;
    state.leadOnline = true;
    state.leadGroupId = group;
    state.leadIsLeader = true;
    state.leadGroupIsRaidOrBg = false;
    state.realPlayerInGroup = false;
    state.memberCount = 5;
    return state;
}

// One commander per group, one group per commander; unbind frees both.
static void TestBindingTable()
{
    BindingTable table;
    CHECK(table.Empty());
    CHECK(table.Bind(MakeBinding(1, 10, 100)) == BindResult::Bound);
    CHECK(table.FindByCommander(1) && table.FindByCommander(1)->leadGuid == 10);
    CHECK(table.FindByGroup(100) && table.FindByGroup(100)->commanderGuid == 1);
    // The same commander cannot take a second group.
    CHECK(table.Bind(MakeBinding(1, 20, 200)) == BindResult::CommanderAlreadyBound);
    // Another commander cannot take the bound group.
    CHECK(table.Bind(MakeBinding(2, 11, 100)) == BindResult::GroupAlreadyBound);
    // A second group for a second commander is fine.
    CHECK(table.Bind(MakeBinding(2, 20, 200)) == BindResult::Bound);
    CHECK(table.Size() == 2);
    // Malformed bindings are refused.
    CHECK(table.Bind(MakeBinding(0, 30, 300)) == BindResult::Invalid);
    CHECK(table.Bind(MakeBinding(3, 0, 300)) == BindResult::Invalid);
    CHECK(table.Bind(MakeBinding(3, 30, 0)) == BindResult::Invalid);
    CHECK(table.Bind(MakeBinding(3, 3, 300)) == BindResult::Invalid);

    Binding removed = table.Unbind(1);
    CHECK(removed.commanderGuid == 1 && removed.groupId == 100);
    CHECK(!table.FindByCommander(1));
    CHECK(!table.FindByGroup(100));
    // Unbinding twice is harmless and returns nothing.
    CHECK(table.Unbind(1).commanderGuid == 0);
    // The freed group can be bound again.
    CHECK(table.Bind(MakeBinding(4, 10, 100)) == BindResult::Bound);
    CHECK(!table.FindByGroup(0));
    table.Clear();
    CHECK(table.Empty());
}

// Followers are recorded once, never the lead bot.
static void TestFollowers()
{
    BindingTable table;
    CHECK(table.Bind(MakeBinding(1, 10, 100)) == BindResult::Bound);
    table.AddFollower(1, 11);
    table.AddFollower(1, 11);
    table.AddFollower(1, 12);
    table.AddFollower(1, 10); // the lead itself
    table.AddFollower(1, 0);
    table.AddFollower(9, 13); // no such commander
    CHECK(table.FindByCommander(1)->followers.size() == 2);
    Binding removed = table.Unbind(1);
    CHECK(removed.followers.size() == 2);
    CHECK(removed.followers[0] == 11 && removed.followers[1] == 12);
}

// The binding ends with its commander, its lead bot or its group, and when
// the group stops being a bot-only party led by the lead bot.
static void TestReleaseRule()
{
    Binding binding = MakeBinding(1, 10, 100);
    CHECK(CheckBinding(binding, Standing(100)) == ReleaseReason::None);

    BindingState state = Standing(100);
    state.enabled = false;
    CHECK(CheckBinding(binding, state) == ReleaseReason::Disabled);

    state = Standing(100);
    state.commanderOnline = false; // logout
    CHECK(CheckBinding(binding, state) == ReleaseReason::CommanderLeft);

    state = Standing(100);
    state.leadOnline = false;
    CHECK(CheckBinding(binding, state) == ReleaseReason::LeadLeft);

    state = Standing(100);
    state.leadGroupId = 0; // disband
    CHECK(CheckBinding(binding, state) == ReleaseReason::GroupChanged);

    state = Standing(100);
    state.leadGroupId = 101; // the lead bot joined another group
    CHECK(CheckBinding(binding, state) == ReleaseReason::GroupChanged);

    state = Standing(100);
    state.leadIsLeader = false;
    CHECK(CheckBinding(binding, state) == ReleaseReason::LeaderChanged);

    state = Standing(100);
    state.realPlayerInGroup = true;
    CHECK(CheckBinding(binding, state) == ReleaseReason::PlayerJoined);

    state = Standing(100);
    state.memberCount = 6;
    CHECK(CheckBinding(binding, state) == ReleaseReason::GroupTooLarge);

    // The group converted to a raid: released at once, even at two bots.
    state = Standing(100);
    state.leadGroupIsRaidOrBg = true;
    state.memberCount = 2;
    CHECK(CheckBinding(binding, state) == ReleaseReason::GroupRaid);
    CHECK(std::string(ReleaseReasonText(ReleaseReason::GroupRaid)) == "the group became a raid");

    // Commander logout wins over everything after the switch.
    state = Standing(0);
    state.commanderOnline = false;
    state.leadOnline = false;
    CHECK(CheckBinding(binding, state) == ReleaseReason::CommanderLeft);
    CHECK(std::string(ReleaseReasonText(ReleaseReason::CommanderLeft)) == "the commander logged out");
    CHECK(std::string(ReleaseReasonText(ReleaseReason::Unbound)) == "unbound by the commander");
}

static BindMember Member()
{
    BindMember member;
    member.controllable = true;
    member.liveBot = true;
    member.hired = false;
    member.inOtherGroup = false;
    return member;
}

static BindFacts GoodBind(size_t members)
{
    BindFacts facts;
    facts.enabled = true;
    facts.lead = Member();
    for (size_t i = 0; i < members; ++i)
        facts.members.push_back(Member());
    return facts;
}

static void TestBindGate()
{
    CHECK(CheckBind(GoodBind(1)) == BindRefusal::None);
    CHECK(CheckBind(GoodBind(4)) == BindRefusal::None);
    // Six bots is a raid, not a party.
    CHECK(CheckBind(GoodBind(5)) == BindRefusal::GroupTooLarge);
    // A group needs two.
    CHECK(CheckBind(GoodBind(0)) == BindRefusal::NoMembers);

    BindFacts facts = GoodBind(2);
    facts.enabled = false; // the toggle is off: nothing else is looked at
    facts.lead.controllable = false;
    CHECK(CheckBind(facts) == BindRefusal::Disabled);
    CHECK(std::string(BindRefusalCode(BindRefusal::Disabled)) == "disabled");

    facts = GoodBind(2);
    facts.commanderIsBot = true;
    CHECK(CheckBind(facts) == BindRefusal::CommanderIsBot);

    // A kept party is commanded as a whole: a bot in it that was not named is a
    // refusal (before its own hired/controllable verdict), with no change made.
    facts = GoodBind(2);
    facts.leadGrouped = true;
    facts.keptPartyHasUnnamedBot = true;
    facts.members[1].hired = true;
    CHECK(CheckBind(facts) == BindRefusal::PartyUnnamed);
    CHECK(std::string(BindRefusalCode(BindRefusal::PartyUnnamed)) == "party-unnamed");
    CHECK(std::string(BindRefusalText(BindRefusal::PartyUnnamed)).find("did not name") != std::string::npos);
    // Every bot named: the same hired bot is then judged as a named member.
    facts.keptPartyHasUnnamedBot = false;
    CHECK(CheckBind(facts) == BindRefusal::MemberHired);
    // Not a kept party (no leadGrouped): the flag is never looked at.
    facts = GoodBind(2);
    facts.keptPartyHasUnnamedBot = true;
    CHECK(CheckBind(facts) == BindRefusal::None);

    // The lead bot's own bind for the group's instance map would replace the
    // group's bind when it takes the lead: refused as lead-bound.
    facts = GoodBind(2);
    facts.leadGrouped = true;
    facts.leadBindWouldReplaceGroupBind = true;
    CHECK(CheckBind(facts) == BindRefusal::LeadBound);
    CHECK(std::string(BindRefusalCode(BindRefusal::LeadBound)) == "lead-bound");
    facts.leadBindWouldReplaceGroupBind = false;
    CHECK(CheckBind(facts) == BindRefusal::None);

    facts = GoodBind(2);
    facts.commanderAlreadyBound = true;
    CHECK(CheckBind(facts) == BindRefusal::AlreadyBound);

    facts = GoodBind(2);
    facts.lead.controllable = false; // not the commander's bot, not a GM
    CHECK(CheckBind(facts) == BindRefusal::LeadNotControllable);
    facts = GoodBind(2);
    facts.lead.liveBot = false;
    CHECK(CheckBind(facts) == BindRefusal::LeadNotControllable);

    facts = GoodBind(2);
    facts.lead.hired = true;
    CHECK(CheckBind(facts) == BindRefusal::LeadHired);

    facts = GoodBind(2);
    facts.leadGrouped = true;
    facts.leadGroupIsRaidOrBg = true;
    CHECK(CheckBind(facts) == BindRefusal::LeadGroupRaid);

    facts = GoodBind(2);
    facts.leadGrouped = true;
    facts.leadGroupHasRealPlayer = true;
    CHECK(CheckBind(facts) == BindRefusal::GroupHasPlayer);

    facts = GoodBind(2);
    facts.leadGrouped = true;
    facts.leadGroupBound = true; // one commander per group
    CHECK(CheckBind(facts) == BindRefusal::GroupBound);

    // Adopting a bot-only group the lead already leads.
    facts = GoodBind(3);
    facts.leadGrouped = true;
    CHECK(CheckBind(facts) == BindRefusal::None);

    // Every member must pass, not just the lead (owner of every bot).
    facts = GoodBind(3);
    facts.members[2].controllable = false;
    CHECK(CheckBind(facts) == BindRefusal::MemberNotControllable);
    facts = GoodBind(3);
    facts.members[1].liveBot = false;
    CHECK(CheckBind(facts) == BindRefusal::MemberNotControllable);
    facts = GoodBind(3);
    facts.members[0].hired = true;
    CHECK(CheckBind(facts) == BindRefusal::MemberHired);
    facts = GoodBind(3);
    facts.members[0].inOtherGroup = true;
    CHECK(CheckBind(facts) == BindRefusal::MemberInOtherGroup);
    CHECK(std::string(BindRefusalCode(BindRefusal::MemberInOtherGroup)) == "member-grouped");

    // A bot still held by another commander's binding is refused, so its
    // saved follow target belongs to one binding only.
    facts = GoodBind(3);
    facts.members[1].inOtherBinding = true;
    CHECK(CheckBind(facts) == BindRefusal::BotInOtherBinding);
    CHECK(std::string(BindRefusalCode(BindRefusal::BotInOtherBinding)) == "bot-bound");
    facts = GoodBind(2);
    facts.lead.inOtherBinding = true;
    CHECK(CheckBind(facts) == BindRefusal::BotInOtherBinding);
}

// Tactical orders: the bound commander counts as a group member. Revive and
// summon: owner-or-GM, as for whispers. Nothing without a binding or the toggle.
static void TestOrderGate()
{
    for (char const* verb : { "attack", "pull", "pullback", "cc", "focus", "move", "stay", "follow", "rest", "lootmethod", "state" })
    {
        CHECK(TierForOrder(verb) == OrderTier::Tactical);
        CHECK(IsKnownOrder(verb));
        CHECK(MayIssueOrder(true, true, OrderTier::Tactical, false));
    }
    CHECK(TierForOrder("revive") == OrderTier::OwnerOrGm);
    CHECK(TierForOrder("summon") == OrderTier::OwnerOrGm);
    CHECK(IsKnownOrder("revive") && IsKnownOrder("summon"));
    // The per-bot player orders move items and open things for one bot: owner or GM.
    for (char const* verb : { "loot", "open", "use", "ritual" })
    {
        CHECK(TierForOrder(verb) == OrderTier::OwnerOrGm);
        CHECK(IsKnownOrder(verb));
        CHECK(!MayIssueOrder(true, true, OrderTier::OwnerOrGm, false));
    }
    CHECK(IsKnownOrder("lootmethod"));
    // Unknown verbs fail closed.
    CHECK(TierForOrder("teleport") == OrderTier::OwnerOrGm);
    CHECK(!IsKnownOrder("teleport"));
    CHECK(!IsKnownOrder(""));

    CHECK(MayIssueOrder(true, true, OrderTier::OwnerOrGm, true));
    CHECK(!MayIssueOrder(true, true, OrderTier::OwnerOrGm, false));
    CHECK(!MayIssueOrder(true, false, OrderTier::Tactical, true));
    CHECK(!MayIssueOrder(false, true, OrderTier::Tactical, true));
    CHECK(!MayIssueOrder(false, true, OrderTier::OwnerOrGm, true));
}

static void TestParseGuid()
{
    uint64_t guid = 0;
    CHECK(ParseGuid("0xF130000C5E0012AB", guid) && guid == 0xF130000C5E0012ABULL);
    CHECK(ParseGuid("0xf130000c5e0012ab", guid) && guid == 0xF130000C5E0012ABULL);
    CHECK(ParseGuid("0X1F", guid) && guid == 0x1F);
    CHECK(ParseGuid("17379391001845715627", guid) && guid == 17379391001845715627ULL);
    CHECK(ParseGuid("42", guid) && guid == 42);
    CHECK(!ParseGuid("", guid) && guid == 0);
    CHECK(!ParseGuid("0", guid));
    CHECK(!ParseGuid("0x0", guid));
    CHECK(!ParseGuid("0x", guid));
    CHECK(!ParseGuid("0x1G", guid));
    CHECK(!ParseGuid("12a", guid));
    CHECK(!ParseGuid("-5", guid));
    CHECK(!ParseGuid("0x1234567890ABCDEF0", guid)); // 17 hex digits
    CHECK(!ParseGuid("18446744073709551616", guid)); // UINT64_MAX + 1
    CHECK(ParseGuid("18446744073709551615", guid) && guid == UINT64_MAX);
}

static void TestParseMovePoint()
{
    MovePoint point;
    CHECK(ParseMovePoint("-1234.5 2234.25 -12.75", point));
    CHECK(point.x == -1234.5f && point.y == 2234.25f && point.z == -12.75f);
    CHECK(ParseMovePoint("1,2,3", point) && point.x == 1.0f && point.y == 2.0f && point.z == 3.0f);
    CHECK(ParseMovePoint("1;2;3", point) && point.z == 3.0f);
    CHECK(ParseMovePoint("  4\t5  6 ", point) && point.x == 4.0f && point.z == 6.0f);
    point = MovePoint();
    CHECK(!ParseMovePoint("1 2", point)); // the third axis is required
    CHECK(!ParseMovePoint("1 2 3 4", point));
    CHECK(!ParseMovePoint("1 2 z", point));
    CHECK(!ParseMovePoint("1 2 3abc", point));
    CHECK(!ParseMovePoint("nan 2 3", point));
    CHECK(!ParseMovePoint("inf 2 3", point));
    CHECK(!ParseMovePoint("1 2 1e30", point));
    CHECK(!ParseMovePoint("30000 2 3", point));
    CHECK(!ParseMovePoint("", point));
    // A refused parse leaves the output alone.
    CHECK(point.x == 0.0f && point.y == 0.0f && point.z == 0.0f);
}

static TargetFacts Enemy()
{
    TargetFacts facts;
    facts.found = true;
    facts.inWorld = true;
    facts.groupMemberOrBot = false;
    facts.alive = true;
    facts.friendly = false;
    facts.attackable = true;
    facts.visible = true;
    return facts;
}

static void TestTargetVerdict()
{
    CHECK(CheckOrderTarget(Enemy()) == TargetVerdict::Ok);

    TargetFacts facts = Enemy();
    facts.found = false; // another map, another instance, or no such unit
    CHECK(CheckOrderTarget(facts) == TargetVerdict::NotFound);
    CHECK(std::string(TargetVerdictCode(TargetVerdict::NotFound)) == "invalid-target");

    facts = Enemy();
    facts.inWorld = false;
    CHECK(CheckOrderTarget(facts) == TargetVerdict::NotFound);

    facts = Enemy();
    facts.groupMemberOrBot = true;
    facts.friendly = true;
    CHECK(CheckOrderTarget(facts) == TargetVerdict::NotAnEnemy);

    facts = Enemy();
    facts.alive = false;
    CHECK(CheckOrderTarget(facts) == TargetVerdict::Dead);
    CHECK(std::string(TargetVerdictCode(TargetVerdict::Dead)) == "dead-target");

    facts = Enemy();
    facts.friendly = true;
    CHECK(CheckOrderTarget(facts) == TargetVerdict::Friendly);
    CHECK(std::string(TargetVerdictCode(TargetVerdict::Friendly)) == "friendly");

    facts = Enemy();
    facts.attackable = false;
    CHECK(CheckOrderTarget(facts) == TargetVerdict::NotAttackable);

    // Stealthed, GM-invisible or untargetable: the GUID does not let the
    // group see it.
    facts = Enemy();
    facts.visible = false;
    CHECK(CheckOrderTarget(facts) == TargetVerdict::NotVisible);
    CHECK(std::string(TargetVerdictCode(TargetVerdict::NotVisible)) == "no-target");
    // A dead invisible unit is still reported as dead first.
    facts.alive = false;
    CHECK(CheckOrderTarget(facts) == TargetVerdict::Dead);
}

// A follower that leaves the group (kicked, invited by its owner elsewhere)
// gets its own follow target back while the rest of the binding stands.
static void TestDepartedFollowers()
{
    BindingTable table;
    CHECK(table.Bind(MakeBinding(1, 10, 100)) == BindResult::Bound);
    table.AddFollower(1, 11);
    table.AddFollower(1, 12);
    table.AddFollower(1, 13);
    Binding const* binding = table.FindByCommander(1);

    // Everyone still there.
    CHECK(DepartedFollowers(*binding, { 10, 11, 12, 13 }).empty());
    // 12 left; a new member 14 joined (not a recorded follower).
    std::vector<uint64_t> departed = DepartedFollowers(*binding, { 10, 11, 13, 14 });
    CHECK(departed.size() == 1 && departed[0] == 12);
    table.RemoveFollower(1, 12);
    CHECK(table.FindByCommander(1)->followers.size() == 2);
    CHECK(!table.FindByBot(12));
    CHECK(table.FindByBot(11) && table.FindByBot(11)->commanderGuid == 1);
    CHECK(table.FindByBot(10) && table.FindByBot(10)->commanderGuid == 1); // the lead
    // Removing an unknown follower or commander is harmless.
    table.RemoveFollower(1, 99);
    table.RemoveFollower(9, 11);
    CHECK(table.FindByCommander(1)->followers.size() == 2);
    // Every follower gone: all of them come back, the binding still stands.
    departed = DepartedFollowers(*table.FindByCommander(1), { 10 });
    CHECK(departed.size() == 2);
    CHECK(table.FindByCommander(1));
    CHECK(!table.FindByBot(0));
}

static MoveFacts Walkable()
{
    MoveFacts facts;
    facts.distanceFromLead = 40.0f;
    facts.maxDistance = 150.0f;
    facts.pathType = 0x0001; // PATHFIND_NORMAL
    facts.endOffset = 0.5f;
    return facts;
}

static void TestMoveVerdict()
{
    CHECK(CheckMove(Walkable()) == MoveVerdict::Ok);

    MoveFacts facts = Walkable();
    facts.distanceFromLead = 150.0f; // exactly ReactDistance is still fine
    CHECK(CheckMove(facts) == MoveVerdict::Ok);
    facts.distanceFromLead = 150.5f;
    CHECK(CheckMove(facts) == MoveVerdict::TooFar);
    CHECK(std::string(MoveVerdictCode(MoveVerdict::TooFar)) == "too-far");

    facts = Walkable();
    facts.maxDistance = 0.0f;
    CHECK(CheckMove(facts) == MoveVerdict::TooFar);

    facts = Walkable();
    facts.pathType = kPathNoPath;
    CHECK(CheckMove(facts) == MoveVerdict::NoPath);
    CHECK(std::string(MoveVerdictCode(MoveVerdict::NoPath)) == "unreachable");

    // A complete path that ends far from the point: the point is not ground.
    facts = Walkable();
    facts.endOffset = 12.0f;
    CHECK(CheckMove(facts) == MoveVerdict::OffMesh);

    // An incomplete path is walked in legs, however far its end is.
    facts = Walkable();
    facts.pathType = 0x0001 | kPathIncomplete;
    facts.endOffset = 60.0f;
    CHECK(CheckMove(facts) == MoveVerdict::Ok);

    // No mmaps for the map (PATHFIND_NOT_USING_PATH): a straight line.
    facts = Walkable();
    facts.pathType = 0x0010;
    CHECK(CheckMove(facts) == MoveVerdict::Ok);
}

static std::vector<std::string> Args(std::string const& text)
{
    return SplitOrderArgs(text);
}

// entry:<id>[@x,y,z]: the by-entry target of cmd open, loot, use and state.
static void TestParseTargetSpec()
{
    TargetSpec spec;
    CHECK(ParseTargetSpec("entry:124372", spec) && spec.ByEntry() && spec.entry == 124372 && !spec.hasPoint && spec.guid == 0);
    CHECK(ParseTargetSpec("ENTRY:7@-235.5,295,-48", spec) && spec.entry == 7 && spec.hasPoint);
    CHECK(spec.point.x == -235.5f && spec.point.y == 295.0f && spec.point.z == -48.0f);
    CHECK(ParseTargetSpec("entry:7@1;2;3", spec) && spec.hasPoint && spec.point.z == 3.0f);   // semicolons too
    CHECK(ParseTargetSpec("entry:16777215", spec) && spec.entry == 16777215);                  // the 24-bit maximum
    // The GUID form stays as it was.
    CHECK(ParseTargetSpec("0xF130000C5E0012AB", spec) && !spec.ByEntry() && spec.guid == 0xF130000C5E0012ABULL);
    CHECK(ParseTargetSpec("42", spec) && spec.guid == 42 && !spec.hasPoint);
    // Refused.
    CHECK(!ParseTargetSpec("entry:", spec));
    CHECK(!ParseTargetSpec("entry:0", spec));
    CHECK(!ParseTargetSpec("entry:16777216", spec));       // past 24 bits
    CHECK(!ParseTargetSpec("entry:123456789", spec));
    CHECK(!ParseTargetSpec("entry:abc", spec));
    CHECK(!ParseTargetSpec("entry:12x", spec));
    CHECK(!ParseTargetSpec("entry:-5", spec));
    CHECK(!ParseTargetSpec("entry:7@1,2", spec));          // two coordinates
    CHECK(!ParseTargetSpec("entry:7@1,2,3,4", spec));
    CHECK(!ParseTargetSpec("entry:7@", spec));
    CHECK(!ParseTargetSpec("entry:7@a,b,c", spec));
    CHECK(!ParseTargetSpec("entry:7@1,2,99999", spec));    // off the map
    CHECK(!ParseTargetSpec("0x1F@1,2,3", spec));           // a point does not go with a GUID
    CHECK(!ParseTargetSpec("", spec));
}

// The point's commas must not split the token; everything else splits as before.
static void TestSplitKeepsEntryPoint()
{
    std::vector<std::string> parts = Args("entry:124372@-235,295,-48 Lcwarlock");
    CHECK(parts.size() == 2 && parts[0] == "entry:124372@-235,295,-48" && parts[1] == "Lcwarlock");
    parts = Args("entry:7@1;2;3  Lcwarra");
    CHECK(parts.size() == 2 && parts[0] == "entry:7@1;2;3");
    parts = Args("5 Lcwarra entry:7@1,2,3");
    CHECK(parts.size() == 3 && parts[2] == "entry:7@1,2,3");
    parts = Args("1,2,3");                                  // move points are untouched
    CHECK(parts.size() == 3);
    parts = Args("entry:7 1,2,3");
    CHECK(parts.size() == 4 && parts[0] == "entry:7");
    CHECK(IsEntryTargetPrefix("Entry:5") && !IsEntryTargetPrefix("entry") && !IsEntryTargetPrefix("0xentry:"));
}

static void TestOrdersByEntry()
{
    LootOrder loot;
    CHECK(ParseLootOrder(Args("entry:6910@-120.5,40,-47 Lcwarlock 7741"), loot));
    CHECK(loot.spec.ByEntry() && loot.spec.entry == 6910 && loot.spec.hasPoint && loot.guid == 0);
    CHECK(loot.bot == "Lcwarlock" && loot.itemId == 7741);
    CHECK(ParseLootOrder(Args("entry:6910 Lcwarlock"), loot) && !loot.spec.hasPoint);
    CHECK(ParseLootOrder(Args("0x1F Lcwarlock"), loot) && loot.guid == 0x1F && !loot.spec.ByEntry());
    CHECK(!ParseLootOrder(Args("entry:0 Lcwarlock"), loot));

    OpenOrder open;
    CHECK(ParseOpenOrder(Args("entry:124371@-235,262,-48 Lcwarlock"), open));
    CHECK(open.spec.entry == 124371 && open.guid == 0 && open.bot == "Lcwarlock");
    CHECK(!ParseOpenOrder(Args("entry:124371@-235,262 Lcwarlock"), open));   // a point of two
    CHECK(!ParseOpenOrder(Args("entry:124371"), open));                        // the bot is required

    UseOrder use;
    CHECK(ParseUseOrder(Args("7740 Lcwarra entry:124372@-235,295,-48"), use));
    CHECK(use.itemId == 7740 && use.spec.entry == 124372 && use.target == 0);
    CHECK(ParseUseOrder(Args("7740 Lcwarra 0x20"), use) && use.target == 0x20 && !use.spec.ByEntry());
    CHECK(ParseUseOrder(Args("7740 Lcwarra"), use) && use.target == 0 && !use.spec.ByEntry());
    CHECK(!ParseUseOrder(Args("7740 Lcwarra entry:x"), use));

    // The bot is the second token in every form.
    CHECK(OwnerOrderBotName("open", Args("entry:7@1,2,3 Lcwarlock")) == "Lcwarlock");
    CHECK(OwnerOrderBotName("loot", Args("entry:7 Lcwarlock 7741")) == "Lcwarlock");
}

// cmd ritual <target> <bot1> <bot2> [<bot3>...]: two to five different bots.
static void TestParseRitualOrder()
{
    RitualOrder ritual;
    CHECK(ParseRitualOrder(Args("entry:130511@-247,81,-47 Lcwarra Lcmage Lcpriest"), ritual));
    CHECK(ritual.spec.entry == 130511 && ritual.spec.hasPoint && ritual.bots.size() == 3);
    CHECK(ritual.bots[0] == "Lcwarra" && ritual.bots[2] == "Lcpriest");
    CHECK(ParseRitualOrder(Args("0x20 A B"), ritual) && ritual.spec.guid == 0x20 && ritual.bots.size() == 2);
    CHECK(ParseRitualOrder(Args("entry:7 A B C D E"), ritual) && ritual.bots.size() == 5);
    CHECK(!ParseRitualOrder(Args("entry:7 A B C D E F"), ritual));      // more than a party
    CHECK(!ParseRitualOrder(Args("entry:7 A"), ritual));                // a ritual needs several players
    CHECK(!ParseRitualOrder(Args("entry:7"), ritual));
    CHECK(!ParseRitualOrder(Args("entry:x A B C"), ritual));            // a malformed target
    CHECK(!ParseRitualOrder(Args("entry:7 A B a"), ritual));            // the same bot twice (any case)
    CHECK(!ParseRitualOrder(Args("A B C"), ritual));                    // the target comes first
}

static void TestStateOrder()
{
    StateOrder state;
    CHECK(ParseStateOrder(Args("entry:124372@-235,295,-48"), state) && state.spec.entry == 124372 && state.spec.hasPoint);
    CHECK(ParseStateOrder(Args("entry:124372"), state) && !state.spec.hasPoint);
    CHECK(ParseStateOrder(Args("0xF1100000000000AA"), state) && state.spec.guid == 0xF1100000000000AAULL);
    CHECK(!ParseStateOrder(Args(""), state));
    CHECK(!ParseStateOrder(Args("entry:1 extra"), state));
    CHECK(!ParseStateOrder(Args("entry:0"), state));
}

// The nearest candidate within the radius wins; a tie goes to the lowest GUID.
static void TestPickNearestEntry()
{
    uint64_t guid = 0;
    std::vector<EntryCandidate> none;
    CHECK(!PickNearestEntry(none, 30.0f, guid) && guid == 0);

    std::vector<EntryCandidate> candidates = { {0xA3, 12.0f}, {0xA1, 4.5f}, {0xA2, 29.9f} };
    CHECK(PickNearestEntry(candidates, 30.0f, guid) && guid == 0xA1);
    // Out of the radius: nothing (and the answer is left alone).
    guid = 77;
    candidates = { {0xA1, 30.5f}, {0xA2, 45.0f} };
    CHECK(!PickNearestEntry(candidates, 30.0f, guid) && guid == 77);
    // Exactly on the radius counts.
    candidates = { {0xA1, 30.0f} };
    CHECK(PickNearestEntry(candidates, 30.0f, guid) && guid == 0xA1);
    // Equally near: the lowest GUID, whatever the order.
    candidates = { {0xB5, 8.0f}, {0xB2, 8.0f}, {0xB9, 8.0f} };
    CHECK(PickNearestEntry(candidates, 30.0f, guid) && guid == 0xB2);
    candidates = { {0xB9, 8.0f}, {0xB2, 8.0f}, {0xB5, 8.0f} };
    CHECK(PickNearestEntry(candidates, 30.0f, guid) && guid == 0xB2);
    // A nearer one beats a lower GUID.
    candidates = { {0x01, 9.0f}, {0xFF, 2.0f} };
    CHECK(PickNearestEntry(candidates, 30.0f, guid) && guid == 0xFF);
    // A zero GUID is no candidate; NaN distance is never picked.
    candidates = { {0, 1.0f}, {0xC1, std::nanf("")} };
    CHECK(!PickNearestEntry(candidates, 30.0f, guid));
    // A point must lie within ReactDistance of the bot, else the order is too-far before any search.
    CHECK(EntryPointInReach(0.0f, 150.0f) && EntryPointInReach(149.9f, 150.0f) && EntryPointInReach(150.0f, 150.0f));
    CHECK(!EntryPointInReach(150.1f, 150.0f));
    CHECK(!EntryPointInReach(900.0f, 150.0f));          // a point across the map
    CHECK(!EntryPointInReach(1.0f, 0.0f));              // no react distance, nothing is in reach
    CHECK(!EntryPointInReach(std::nanf(""), 150.0f));
    // The radius: 30 yd around a point, ReactDistance around the bot.
    CHECK(EntrySearchRadius(true, 150.0f) == kEntryPointRadius && kEntryPointRadius == 30.0f);
    CHECK(EntrySearchRadius(false, 150.0f) == 150.0f);
}

static void TestParseItemId()
{
    uint32_t id = 0;
    CHECK(ParseItemId("7741", id) && id == 7741);
    CHECK(ParseItemId("999999", id) && id == 999999);
    CHECK(!ParseItemId("", id) && id == 0);
    CHECK(!ParseItemId("0", id));
    CHECK(!ParseItemId("1000000", id));
    CHECK(!ParseItemId("77a1", id));
    CHECK(!ParseItemId("-5", id));
    CHECK(!ParseItemId("0x1F", id));
}

// cmd loot <guid> <bot> [itemId]
static void TestParseLootOrder()
{
    LootOrder order;
    CHECK(ParseLootOrder(Args("0xF130000C5E0012AB Lcpriest"), order));
    CHECK(order.guid == 0xF130000C5E0012ABULL && order.bot == "Lcpriest" && order.itemId == 0);
    CHECK(ParseLootOrder(Args("42 Lcpriest 7741"), order));
    CHECK(order.guid == 42 && order.itemId == 7741);
    CHECK(!ParseLootOrder(Args("42"), order));                  // the bot is required
    CHECK(!ParseLootOrder(Args(""), order));
    CHECK(!ParseLootOrder(Args("42 Lcpriest 7741 extra"), order));
    CHECK(!ParseLootOrder(Args("nonsense Lcpriest"), order));    // not a guid
    CHECK(!ParseLootOrder(Args("0 Lcpriest"), order));
    CHECK(!ParseLootOrder(Args("42 Lcpriest 0"), order));        // not an item
    CHECK(!ParseLootOrder(Args("42 Lcpriest abc"), order));
}

// cmd open <guid> <bot>
static void TestParseOpenOrder()
{
    OpenOrder order;
    CHECK(ParseOpenOrder(Args("0x1F Lcwarra"), order) && order.guid == 0x1F && order.bot == "Lcwarra");
    CHECK(!ParseOpenOrder(Args("0x1F"), order));
    CHECK(!ParseOpenOrder(Args("0x1F Lcwarra 7"), order));
    CHECK(!ParseOpenOrder(Args("Lcwarra 0x1F"), order));
}

// cmd use <itemId> <bot> [target-guid]
static void TestParseUseOrder()
{
    UseOrder order;
    CHECK(ParseUseOrder(Args("7733 Lcpriest"), order));
    CHECK(order.itemId == 7733 && order.bot == "Lcpriest" && order.target == 0);
    CHECK(ParseUseOrder(Args("7733 Lcpriest 0xF1300000010203AB"), order));
    CHECK(order.target == 0xF1300000010203ABULL);
    CHECK(!ParseUseOrder(Args("7733"), order));
    CHECK(!ParseUseOrder(Args("Lcpriest 7733"), order));
    CHECK(!ParseUseOrder(Args("0 Lcpriest"), order));
    CHECK(!ParseUseOrder(Args("7733 Lcpriest zz"), order));
    CHECK(!ParseUseOrder(Args("7733 Lcpriest 1 2"), order));
}

// Which argument is the bot: revive/summon take it alone, loot/open/use second.
static void TestOwnerOrderBotName()
{
    CHECK(OwnerOrderBotName("revive", Args("Lcpriest")) == "Lcpriest");
    CHECK(OwnerOrderBotName("summon", Args("Lcpriest")) == "Lcpriest");
    CHECK(OwnerOrderBotName("revive", Args("Lcpriest extra")).empty());
    CHECK(OwnerOrderBotName("revive", Args("")).empty());
    CHECK(OwnerOrderBotName("loot", Args("42 Lcpriest 7741")) == "Lcpriest");
    CHECK(OwnerOrderBotName("open", Args("42 Lcpriest")) == "Lcpriest");
    CHECK(OwnerOrderBotName("use", Args("7733 Lcpriest")) == "Lcpriest");
    CHECK(OwnerOrderBotName("loot", Args("42")).empty());
    CHECK(OwnerOrderBotName("teleport", Args("a b")).empty());
}

// The bind keeps the party the lead bot is in (the instance bind lives on the
// group), and only re-forms one it cannot keep.
static void TestBindGroupPlan()
{
    BindPlanFacts facts;
    CHECK(DecideBindGroup(facts) == BindGroupPlan::FormNew);

    facts = BindPlanFacts();
    facts.leadInOwnGroup = true;
    CHECK(DecideBindGroup(facts) == BindGroupPlan::AdoptLeadGroup);

    // Lead bot in the commander's party: kept, whether the party is the
    // commander plus bots (the 19i case) or holds more bots than were named.
    facts = BindPlanFacts();
    facts.leadInCommanderParty = true;
    CHECK(DecideBindGroup(facts) == BindGroupPlan::KeepCommanderParty);

    // A raid / battleground party, or one with another player, is re-formed.
    facts.partyIsRaidOrBg = true;
    CHECK(DecideBindGroup(facts) == BindGroupPlan::ReformFromCommanderParty);
    facts.partyIsRaidOrBg = false;
    facts.partyHasOtherPlayer = true;
    CHECK(DecideBindGroup(facts) == BindGroupPlan::ReformFromCommanderParty);

    // The commander's party wins over "some other group" when both are set.
    facts = BindPlanFacts();
    facts.leadInCommanderParty = true;
    facts.leadInOwnGroup = true;
    CHECK(DecideBindGroup(facts) == BindGroupPlan::KeepCommanderParty);

    // The core disbands a party left with fewer than two members: the
    // commander leaves a kept party only with at least two bots staying.
    CHECK(!CanDropCommanderFromParty(0));
    CHECK(!CanDropCommanderFromParty(2));   // commander + 1 bot
    CHECK(CanDropCommanderFromParty(3));    // commander + 2 bots
    CHECK(CanDropCommanderFromParty(5));
}

// A bot in a commanded group does not leave when its master does; every other
// bot keeps the general rule.
static void TestMasterLeftRule()
{
    CHECK(!LeavesWhenMasterLeaves(true));
    CHECK(LeavesWhenMasterLeaves(false));
}

// Commanded damage bots and healers back off near the tank's threat; the tank
// and bots outside a commanded group are left to the general rule.
static void TestCommandedThreatBackoff()
{
    CHECK(KeepsCommandedThreatBackoff(true, false));
    CHECK(!KeepsCommandedThreatBackoff(true, true));
    CHECK(!KeepsCommandedThreatBackoff(false, false));
    CHECK(!KeepsCommandedThreatBackoff(false, true));
}

// Only a party-wide attack leaves a tank on the mob it holds; a named attack
// and a tank holding nothing go as ordered, and damage bots always go.
static void TestPartyAttackTankStays()
{
    CHECK(PartyAttackLeavesTankOnItsMob(true, true, true));
    CHECK(!PartyAttackLeavesTankOnItsMob(false, true, true));
    CHECK(!PartyAttackLeavesTankOnItsMob(true, true, false));
    CHECK(!PartyAttackLeavesTankOnItsMob(true, false, true));
}

// An attack on the mob a bot is still pulling leaves the pull running; an
// attack on another mob, or with no pull running, goes as ordered.
static void TestAttackLeavesPullRunning()
{
    CHECK(AttackLeavesPullRunning(true, true));
    CHECK(!AttackLeavesPullRunning(true, false));
    CHECK(!AttackLeavesPullRunning(false, false));
}

// The ACK says when the tank stayed, so the client can name it for the add.
static void TestPartyAttackAckDetail()
{
    CHECK(PartyAttackAckDetail("").empty());
    CHECK(PartyAttackAckDetail("Lcwarra") == "tank-stays:Lcwarra");
}

// The back-off yields on a mob hitting the healer or a damage bot of a
// commanded party, never on a mob hitting the tank, and never outside one.
static void TestCommandedBackoffYields()
{
    CHECK(CommandedBackoffYields(true, true, false));
    CHECK(!CommandedBackoffYields(true, true, true));
    CHECK(!CommandedBackoffYields(true, false, false));
    CHECK(!CommandedBackoffYields(false, true, false));
}

// Lifecycle of a binding's "threat": given only to an eligible bot that
// lacks it, never stored while held, taken off on release unless still due;
// a strategy reset or a load while bound notes it too (a late joiner).
static void TestCommanderThreatLedger()
{
    CommanderThreatLedger ledger;
    CHECK(ledger.Give(1, true, false));          // damage bot: added
    CHECK(!ledger.Give(2, false, false));        // tank: not eligible
    CHECK(!ledger.Give(3, true, true));          // owner's own toggle: not ours
    CHECK(!ledger.Give(0, true, false));
    CHECK(!ledger.Persists(1));                  // never written while held
    CHECK(ledger.Persists(2) && ledger.Persists(3));
    CHECK(ledger.Release(1, false));             // unbind: taken off
    CHECK(ledger.Persists(1));
    CHECK(!ledger.Release(1, false));            // only once
    CHECK(!ledger.Release(3, false));            // never ours: left alone
    ledger.Note(4);                              // joined after the bind
    CHECK(ledger.Holds(4) && !ledger.Persists(4));
    CHECK(!ledger.Release(4, true));             // a real master still gives it
    CHECK(!ledger.Holds(4));
    ledger.Note(5);
    ledger.Clear();
    CHECK(!ledger.Holds(5));
}

// A hire may be bound by its own hirer only: the bind gate sees "hired" for a
// hire of anybody else, and the usual flow (the commander's five hires) passes.
static void TestHiresOfTheCommander()
{
    uint64_t const commander = 0x1001, other = 0x2002;
    // Not a hire: never blocks. A hire of the commander: bindable. A hire of
    // somebody else, or of nobody known: refused.
    CHECK(!HireBlocksBind(false, 0, commander));
    CHECK(!HireBlocksBind(false, other, commander));
    CHECK(!HireBlocksBind(true, commander, commander));
    CHECK(HireBlocksBind(true, other, commander));
    CHECK(HireBlocksBind(true, 0, commander));
    // No commander guid: nothing is "its own".
    CHECK(HireBlocksBind(true, 0, 0));

    // Five hires of the commander (lead + four): the gate lets them through.
    BindFacts facts = GoodBind(4);
    facts.lead.hired = HireBlocksBind(true, commander, commander);
    for (BindMember& member : facts.members)
        member.hired = HireBlocksBind(true, commander, commander);
    CHECK(CheckBind(facts) == BindRefusal::None);

    // The same bind with one hire of somebody else is refused, as lead or member.
    facts.members[2].hired = HireBlocksBind(true, other, commander);
    CHECK(CheckBind(facts) == BindRefusal::MemberHired);
    CHECK(std::string(BindRefusalCode(BindRefusal::MemberHired)) == "hired");
    facts.members[2].hired = false;
    facts.lead.hired = HireBlocksBind(true, other, commander);
    CHECK(CheckBind(facts) == BindRefusal::LeadHired);
    CHECK(std::string(BindRefusalText(BindRefusal::LeadHired)).find("Another player's") != std::string::npos);
    CHECK(std::string(BindRefusalText(BindRefusal::MemberHired)).find("Another player's") != std::string::npos);
}

int main()
{
    TestHiresOfTheCommander();
    TestBindingTable();
    TestFollowers();
    TestDepartedFollowers();
    TestReleaseRule();
    TestBindGate();
    TestBindGroupPlan();
    TestMasterLeftRule();
    TestCommandedThreatBackoff();
    TestPartyAttackTankStays();
    TestAttackLeavesPullRunning();
    TestPartyAttackAckDetail();
    TestCommandedBackoffYields();
    TestCommanderThreatLedger();
    TestOrderGate();
    TestParseGuid();
    TestParseMovePoint();
    TestTargetVerdict();
    TestMoveVerdict();
    TestParseItemId();
    TestParseTargetSpec();
    TestSplitKeepsEntryPoint();
    TestOrdersByEntry();
    TestStateOrder();
    TestPickNearestEntry();
    TestParseLootOrder();
    TestParseOpenOrder();
    TestParseUseOrder();
    TestParseRitualOrder();
    TestOwnerOrderBotName();
    std::printf("remote commander policy: %d checks passed\n", checks);
    return 0;
}
