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

static void TestFeatureGates()
{
    FeatureGates const off{};
    FeatureGates const remote{true, false, false};
    FeatureGates const self{false, true, false};
    FeatureGates const both{true, true, false};
    FeatureGates const lfg{false, true, true};
    FeatureGates const lfgNoSelf{false, false, true};
    CHECK(BindingKindEnabled(BindingKind::Remote, remote) && !BindingKindEnabled(BindingKind::Remote, self));
    CHECK(BindingKindEnabled(BindingKind::InParty, self) && BindingKindEnabled(BindingKind::InParty, remote) &&
        !BindingKindEnabled(BindingKind::InParty, off));
    CHECK(BindingKindEnabled(BindingKind::PlayerLed, self) && !BindingKindEnabled(BindingKind::PlayerLed, remote));
    CHECK(BindingKindEnabled(BindingKind::System, lfg) && !BindingKindEnabled(BindingKind::System, self) &&
        !BindingKindEnabled(BindingKind::System, lfgNoSelf));
    CHECK(CommanderSurfaceOn(self) && CommanderSurfaceOn(remote) && !CommanderSurfaceOn(off) && !CommanderSurfaceOn(lfgNoSelf));
    CHECK(RunSurfaceOn(self) && !RunSurfaceOn(remote));
    CHECK(ViewAllowed(remote) && !ViewAllowed(self));
    CHECK(!RunAllowed(BindingKind::Remote, remote) && RunAllowed(BindingKind::Remote, both) && !RunAllowed(BindingKind::Remote, self));
    CHECK(RunAllowed(BindingKind::InParty, self) && RunAllowed(BindingKind::PlayerLed, self));
    CHECK(RunAllowed(BindingKind::System, lfg) && !RunAllowed(BindingKind::System, self));
    // A player's own orders without RemoteCommander: only enter and summon; the run's own orders always.
    CHECK(OrderAllowed(BindingKind::InParty, self, false, "enter") && OrderAllowed(BindingKind::InParty, self, false, "summon"));
    CHECK(!OrderAllowed(BindingKind::InParty, self, false, "move") && OrderAllowed(BindingKind::InParty, self, true, "move"));
    CHECK(OrderAllowed(BindingKind::InParty, both, false, "move"));
    CHECK(!OrderAllowed(BindingKind::PlayerLed, self, false, "attack") && OrderAllowed(BindingKind::PlayerLed, self, false, "summon"));
    CHECK(!OrderAllowed(BindingKind::System, lfg, false, "enter") && OrderAllowed(BindingKind::System, lfg, true, "move"));
    CHECK(!OrderAllowed(BindingKind::System, self, true, "move"));
    CHECK(!OrderAllowed(BindingKind::Remote, self, true, "move") && OrderAllowed(BindingKind::Remote, remote, false, "move"));
    CHECK(IsInGameOrder("enter") && IsInGameOrder("summon") && !IsInGameOrder("revive"));
    Binding b = MakeBinding(1, 10, 100);
    CHECK(KindOf(b) == BindingKind::Remote);
    b.inParty = true;
    CHECK(KindOf(b) == BindingKind::InParty);
    b.playerLed = true;
    CHECK(KindOf(b) == BindingKind::PlayerLed);
    Binding s = MakeBinding(10, 10, 100);
    s.system = true;
    CHECK(KindOf(s) == BindingKind::System);
    CHECK(std::string(RunSwitchedOffNote(BindingKind::System, self)) ==
        "stopped: bot LFG groups were switched off (AiPlayerbot.BotLfgGroups)");
    CHECK(std::string(RunSwitchedOffNote(BindingKind::System, off)) ==
        "stopped: the run leader was switched off (AiPlayerbot.DungeonSelfClear)");
    CHECK(std::string(RunSwitchedOffNote(BindingKind::Remote, self)) ==
        "stopped: the remote commander was switched off (AiPlayerbot.RemoteCommander)");
    CHECK(std::string(OrderOffText(BindingKind::System)) == "Only the run leader orders a server-owned party.");
}

// A system binding: keyed by its own lead bot, no commander to lose, a joined player does not end it.
static void TestSystemBinding()
{
    BindingTable table;
    Binding s = MakeBinding(10, 10, 100);
    s.system = true;
    CHECK(table.Bind(s) == BindResult::Bound);
    CHECK(table.FindByBot(10) && table.FindByBot(10)->system);
    Binding other = MakeBinding(11, 12, 200);
    other.system = true;
    CHECK(table.Bind(other) == BindResult::Invalid);      // a system key is its own lead
    Binding inParty = MakeBinding(13, 13, 300);
    inParty.system = true;
    inParty.inParty = true;
    CHECK(table.Bind(inParty) == BindResult::Invalid);
    CHECK(table.Bind(MakeBinding(14, 14, 400)) == BindResult::Invalid); // a plain binding still needs another commander
    CHECK(table.Bind(MakeBinding(20, 21, 100)) == BindResult::GroupAlreadyBound);

    BindingState state = Standing(100);
    state.commanderOnline = false;
    CHECK(CheckBinding(s, state) == ReleaseReason::None);
    state.realPlayerInGroup = true;
    CHECK(CheckBinding(s, state) == ReleaseReason::None);
    state.leadIsLeader = false;
    CHECK(CheckBinding(s, state) == ReleaseReason::LeaderChanged);
    state = Standing(100);
    state.leadOnline = false;
    CHECK(CheckBinding(s, state) == ReleaseReason::LeadLeft);
    state = Standing(100);
    state.enabled = false;
    CHECK(CheckBinding(s, state) == ReleaseReason::Disabled);
    state = Standing(100);
    state.leadGroupIsRaidOrBg = true;
    CHECK(CheckBinding(s, state) == ReleaseReason::GroupRaid);
    state = Standing(100);
    state.commanderOnline = false;
    CHECK(CheckBinding(MakeBinding(1, 10, 100), state) == ReleaseReason::CommanderLeft); // classic unchanged
    CHECK(std::string(ReleaseReasonText(ReleaseReason::Disabled)) == "switched off in the config");
}

static void TestInPartyOnlyRefusal()
{
    BindFacts facts;
    facts.enabled = false;
    facts.inPartyAllowed = true;
    CHECK(CheckBind(facts) == BindRefusal::InPartyOnly);
    CHECK(std::string(BindRefusalCode(BindRefusal::InPartyOnly)) == "inparty-only");
    facts.inParty = true;
    CHECK(CheckBind(facts) == BindRefusal::Disabled);
    facts.inParty = false;
    facts.inPartyAllowed = false;
    CHECK(CheckBind(facts) == BindRefusal::Disabled);
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
    for (char const* verb : { "loot", "open", "use", "ritual", "enter" })
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

static void TestParseMoveOrder()
{
    MoveOrder order;
    CHECK(ParseMoveOrder("-155.7 279.6 -46.8", order) && !order.direct);
    CHECK(order.point.x == -155.7f && order.point.y == 279.6f && order.point.z == -46.8f);
    order = MoveOrder();
    CHECK(ParseMoveOrder("-155.7 279.6 -46.8 direct", order) && order.direct && order.point.z == -46.8f);
    order = MoveOrder();
    CHECK(ParseMoveOrder("1,2,3 DIRECT", order) && order.direct && order.point.x == 1.0f);
    CHECK(ParseMoveOrder("1;2;3;Direct", order) && order.direct);
    order = MoveOrder();
    CHECK(!ParseMoveOrder("1 2 3 straight", order));
    CHECK(!ParseMoveOrder("1 2 3 directly", order));
    CHECK(!ParseMoveOrder("1 2 direct", order));
    CHECK(!ParseMoveOrder("direct 1 2 3", order));
    CHECK(!ParseMoveOrder("1 2 3 direct direct", order));
    CHECK(!ParseMoveOrder("1 2 nan direct", order));
    CHECK(!ParseMoveOrder("", order));
    CHECK(!order.direct && order.point.x == 0.0f); // a refused parse leaves the output alone
}

static void TestDirectVerdict()
{
    CHECK(CheckDirectStep(0.0f, 0.0f) == DirectVerdict::Ok);
    CHECK(CheckDirectStep(25.0f, 8.0f) == DirectVerdict::Ok);   // on the limit
    CHECK(CheckDirectStep(25.0f, -8.0f) == DirectVerdict::Ok);
    CHECK(CheckDirectStep(25.1f, 0.0f) == DirectVerdict::TooFar);
    CHECK(CheckDirectStep(10.0f, 8.1f) == DirectVerdict::TooFar);
    CHECK(CheckDirectStep(10.0f, -8.1f) == DirectVerdict::TooFar);
    CHECK(CheckDirectStep(std::nanf(""), 0.0f) == DirectVerdict::TooFar);
    CHECK(CheckDirectStep(5.0f, std::nanf("")) == DirectVerdict::TooFar);
    // The scorpid-pit lip to the east plateau (about 20.6 yd across, about 5 yd up).
    CHECK(CheckDirectStep(std::sqrt(7.8f * 7.8f + 19.1f * 19.1f), 5.0f) == DirectVerdict::Ok);
    CHECK(std::string(DirectVerdictCode(DirectVerdict::TooFar)) == "too-far");

    CHECK(CheckDirectGround(-46.8f, -46.8f) == DirectVerdict::Ok);
    CHECK(CheckDirectGround(-49.8f, -46.8f) == DirectVerdict::Ok);   // 3 yd below: on the limit
    CHECK(CheckDirectGround(-46.3f, -46.8f) == DirectVerdict::Ok);   // 0.5 yd above: the slack
    CHECK(CheckDirectGround(-50.0f, -46.8f) == DirectVerdict::NoGround); // too far below: the bots would hang
    CHECK(CheckDirectGround(-46.2f, -46.8f) == DirectVerdict::NoGround); // a floor above the point: inside the geometry
    CHECK(CheckDirectGround(-43.8f, -46.8f) == DirectVerdict::NoGround);
    CHECK(CheckDirectGround(-100000.0f, -46.8f) == DirectVerdict::NoGround); // INVALID_HEIGHT
    CHECK(CheckDirectGround(std::nanf(""), 0.0f) == DirectVerdict::NoGround);
    CHECK(std::string(DirectVerdictCode(DirectVerdict::NoGround)) == "no-ground");

    CHECK(CheckDirectLos(true) == DirectVerdict::Ok);
    CHECK(CheckDirectLos(false) == DirectVerdict::NoLos);
    CHECK(std::string(DirectVerdictCode(DirectVerdict::NoLos)) == "no-los");

    // A follower joins the lead bot's line: on its complete navmesh path when it has one, else straight when it stands on the lead
    // bot's floor nearby and sees it.
    CHECK(ChooseDirectJoin(0.0f, 0.0f, true, false, -1.0f) == DirectJoin::Straight);
    CHECK(ChooseDirectJoin(5.0f, 1.0f, true, false, -1.0f) == DirectJoin::Straight);   // on the limits
    CHECK(ChooseDirectJoin(5.0f, -1.0f, true, false, -1.0f) == DirectJoin::Straight);
    CHECK(ChooseDirectJoin(5.1f, 0.0f, true, false, -1.0f) == DirectJoin::None);       // too far for straight, no path
    // 3 yd out on the pit floor, 2.4 yd under the ledge the lead bot stands on, sees it over the edge: never straight through the face.
    CHECK(ChooseDirectJoin(3.0f, -2.4f, true, false, -1.0f) == DirectJoin::None);
    CHECK(ChooseDirectJoin(3.0f, -2.4f, true, true, 9.0f) == DirectJoin::Path);
    CHECK(ChooseDirectJoin(3.0f, -1.1f, true, false, -1.0f) == DirectJoin::None);
    CHECK(ChooseDirectJoin(3.0f, 0.0f, false, false, -1.0f) == DirectJoin::None);      // around a corner, no path
    CHECK(ChooseDirectJoin(2.0f, 0.0f, true, true, 2.5f) == DirectJoin::Path);         // a path wins over straight
    // Uldaman run 13: a follower 7 yd back on the pit floor, 0.4 yd lower, with a path along the pit to the lip.
    CHECK(ChooseDirectJoin(7.1f, -0.4f, true, true, 8.0f) == DirectJoin::Path);
    CHECK(ChooseDirectJoin(30.0f, 0.0f, false, true, 40.0f) == DirectJoin::Path);      // on the path limit
    CHECK(ChooseDirectJoin(30.0f, 0.0f, false, true, 40.1f) == DirectJoin::None);
    CHECK(ChooseDirectJoin(3.0f, 0.0f, true, true, 40.1f) == DirectJoin::Straight);    // path too long, near and on its floor
    CHECK(ChooseDirectJoin(10.0f, 0.0f, false, false, 10.0f) == DirectJoin::None);     // incomplete path
    CHECK(ChooseDirectJoin(std::nanf(""), 0.0f, true, false, -1.0f) == DirectJoin::None);
    CHECK(ChooseDirectJoin(10.0f, 0.0f, false, true, std::nanf("")) == DirectJoin::None);
    CHECK(kDirectMaxSleepMs == 4000);
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

    // Issue #999: the run leader's gather on the lead bot's own spot (within
    // kMoveHereYd of it) is Ok, even when the path finder answers no path or
    // an off-mesh end at 0 yd. A player's order is Ok only within
    // kAtLeadDistance (issue #1026, "hold here").
    facts = Walkable();
    facts.distanceFromLead = 0.0f;
    facts.pathType = kPathNoPath;
    CHECK(CheckMove(facts) == MoveVerdict::Ok);
    facts.distanceFromLead = 2.0f;
    CHECK(CheckMove(facts) == MoveVerdict::NoPath);
    facts.leadSpotGather = true;
    CHECK(CheckMove(facts) == MoveVerdict::Ok);
    facts.distanceFromLead = kMoveHereYd;
    CHECK(CheckMove(facts) == MoveVerdict::Ok);
    facts.distanceFromLead = kMoveHereYd + 0.5f;
    CHECK(CheckMove(facts) == MoveVerdict::NoPath);
    facts = Walkable();
    facts.distanceFromLead = 2.0f;
    facts.endOffset = 12.0f;
    CHECK(CheckMove(facts) == MoveVerdict::OffMesh); // a player's sub-3 yd off-mesh point
    facts.leadSpotGather = true;
    CHECK(CheckMove(facts) == MoveVerdict::Ok);
    facts.maxDistance = 0.0f; // no ReactDistance: still too far
    CHECK(CheckMove(facts) == MoveVerdict::TooFar);

    // A complete path that ends far from the point: the point is not ground.
    facts = Walkable();
    facts.endOffset = 12.0f;
    CHECK(CheckMove(facts) == MoveVerdict::OffMesh);

    // An incomplete path to a far point is walked in legs, however far its end is.
    facts = Walkable();
    facts.distanceFromLead = 100.0f;
    facts.pathType = 0x0001 | kPathIncomplete;
    facts.endOffset = 60.0f;
    facts.endFromLead = 40.0f;
    CHECK(CheckMove(facts) == MoveVerdict::Ok);
    facts.endFromLead = -1.0f; // not measured
    CHECK(CheckMove(facts) == MoveVerdict::Ok);

    // Ragefire 2026-10-17: a point 6 yd from the lead bot, the path finder
    // answered incomplete (the point is on another part of the mesh), the old
    // verdict was Ok and the lead bot stood still after three acks.
    facts = Walkable();
    facts.distanceFromLead = 6.4f;
    facts.pathType = 0x0001 | kPathIncomplete;
    facts.endOffset = 6.4f;
    facts.endFromLead = 0.3f;
    CHECK(CheckMove(facts) == MoveVerdict::NoPath);
    // A short incomplete path that ends far from the point, with some progress.
    facts.endFromLead = 3.0f;
    CHECK(CheckMove(facts) == MoveVerdict::NoPath);
    facts.distanceFromLead = kIncompleteShortYd;
    CHECK(CheckMove(facts) == MoveVerdict::NoPath);
    facts.distanceFromLead = kIncompleteShortYd + 1.0f; // beyond: the length cap may explain it
    CHECK(CheckMove(facts) == MoveVerdict::Ok);
    // ... but a path that goes nowhere is refused at any distance.
    facts.endFromLead = kNoProgressYd;
    CHECK(CheckMove(facts) == MoveVerdict::NoPath);
    // The run leader's own hops and gathers (fromRunLeader) are not refused for an end far from the
    // point: that would pause the run on legs 0-3. A path that goes nowhere is refused for it too.
    facts = Walkable();
    facts.distanceFromLead = 12.0f;
    facts.pathType = 0x0001 | kPathIncomplete;
    facts.endOffset = 9.0f;
    facts.endFromLead = 8.0f;
    CHECK(CheckMove(facts) == MoveVerdict::NoPath); // a player's order
    facts.fromRunLeader = true;
    CHECK(CheckMove(facts) == MoveVerdict::Ok);     // the run leader's hop
    facts.endFromLead = 0.5f;
    CHECK(CheckMove(facts) == MoveVerdict::NoPath); // no progress at all
    // Only that no-progress refusal is the lead bot's own spot's (the run leader's island sign, Uldaman r16-19z34);
    // a NOPATH or a path that made progress says the point is the trouble.
    CHECK(NoProgressFromLead(facts));
    facts.endFromLead = kNoProgressYd + 0.1f;
    CHECK(!NoProgressFromLead(facts));
    facts.endFromLead = -1.0f;
    CHECK(!NoProgressFromLead(facts));
    facts.endFromLead = 0.5f;
    facts.pathType = kPathNoPath;
    CHECK(CheckMove(facts) == MoveVerdict::NoPath && !NoProgressFromLead(facts));
    facts.pathType = kPathNoPath | kPathIncomplete;
    CHECK(!NoProgressFromLead(facts));
    // A short incomplete path that ends near the point is walkable.
    facts = Walkable();
    facts.distanceFromLead = 20.0f;
    facts.pathType = 0x0001 | kPathIncomplete;
    facts.endOffset = kMaxMoveEndOffset;
    facts.endFromLead = 15.0f;
    CHECK(CheckMove(facts) == MoveVerdict::Ok);

    // No mmaps for the map (PATHFIND_NOT_USING_PATH): a straight line.
    facts = Walkable();
    facts.pathType = 0x0010;
    CHECK(CheckMove(facts) == MoveVerdict::Ok);

    // A move to the lead bot's own spot ("hold here"): the path finder says
    // NOPATH for a zero-length path, and that is not a refusal.
    facts = Walkable();
    facts.distanceFromLead = 0.0f;
    facts.pathType = kPathNoPath;
    CHECK(IsAtLead(facts.distanceFromLead));
    CHECK(CheckMove(facts) == MoveVerdict::Ok);
    facts.distanceFromLead = 1.0f;
    CHECK(CheckMove(facts) == MoveVerdict::Ok);
    facts.distanceFromLead = 1.5f; // beyond the hold-here radius: NOPATH stands
    CHECK(!IsAtLead(facts.distanceFromLead));
    CHECK(CheckMove(facts) == MoveVerdict::NoPath);
}

static void TestSnapToFloorZ()
{
    // A point 4.5 yd under the floor snaps up to it (Ragefire Chasm).
    CHECK(SnapToFloorZ(-10.5f, -6.0f, true) == -6.0f);
    // A point above the floor snaps down.
    CHECK(SnapToFloorZ(-2.0f, -6.0f, true) == -6.0f);
    // Exactly the range still snaps; beyond it the point stays (another level).
    CHECK(SnapToFloorZ(0.0f, 6.0f, true) == 6.0f);
    CHECK(SnapToFloorZ(0.0f, -6.0f, true) == -6.0f);
    CHECK(SnapToFloorZ(0.0f, 6.5f, true) == 0.0f);
    CHECK(SnapToFloorZ(0.0f, -6.5f, true) == 0.0f);
    // No floor found: unchanged.
    CHECK(SnapToFloorZ(3.0f, -100000.0f, false) == 3.0f);
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

// `.bot run for <Commander> <verb>`: the first word is the commander's name
// (letters only, 2-12), the second the run verb, the rest its arguments; a
// start never carries the player-led word (that is the commander's own run).
static void TestParseRunFor()
{
    std::string commander, verb, rest;
    CHECK(ParseRunFor("Gbmage pause", commander, verb, rest));
    CHECK(commander == "Gbmage" && verb == "pause" && rest.empty());
    CHECK(ParseRunFor("Gbmage START lower_karazhan_halls from 0", commander, verb, rest));
    CHECK(commander == "Gbmage" && verb == "start" && rest == "lower_karazhan_halls from 0");
    CHECK(!ParseRunFor("Gbmage start x player", commander, verb, rest));
    CHECK(!ParseRunFor("Gbmage here", commander, verb, rest));
    CHECK(!ParseRunFor("G1 pause", commander, verb, rest));
    CHECK(!ParseRunFor("", commander, verb, rest));
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

// A bot without control of itself (stun, Hex, fear) keeps the attack order
// until it can act; a healer is never sent at the mob, so it is not held.
static void TestHeldAttack()
{
    CHECK(HoldsAttackUntilControl(false, true));
    CHECK(!HoldsAttackUntilControl(false, false));
    CHECK(!HoldsAttackUntilControl(true, true));

    // zf-hires-2 run 6: the hexed tank keeps the order and runs it when the Hex ends.
    CHECK(StepHeldAttack(true, true, true, 3000) == HeldAttackStep::Wait);
    CHECK(StepHeldAttack(true, true, false, 10500) == HeldAttackStep::Run);
    CHECK(StepHeldAttack(true, true, true, kHeldAttackMaxMs) == HeldAttackStep::Wait);
    CHECK(StepHeldAttack(true, true, true, kHeldAttackMaxMs + 1) == HeldAttackStep::Drop);
    CHECK(StepHeldAttack(true, true, false, kHeldAttackMaxMs + 1) == HeldAttackStep::Drop);
    CHECK(StepHeldAttack(false, true, false, 0) == HeldAttackStep::Drop);
    CHECK(StepHeldAttack(true, false, false, 0) == HeldAttackStep::Drop);
    CHECK(kHeldAttackMaxMs > 10000); // outlasts a Hex
    // The mage sheeped the target during the hold: the attack must not break it.
    CHECK(StepHeldAttack(true, true, false, 3000, true) == HeldAttackStep::Drop);
    CHECK(StepHeldAttack(true, true, true, 3000, true) == HeldAttackStep::Drop);
    CHECK(StepHeldAttack(true, true, false, 3000, false) == HeldAttackStep::Run);

    CHECK(AttackAckDetail("", {}).empty());
    CHECK(AttackAckDetail("Lcwarra", {}) == "tank-stays:Lcwarra");
    CHECK(AttackAckDetail("", {"Forthur"}) == "held:Forthur");
    CHECK(AttackAckDetail("", {"Forthur", "Puvur"}) == "held:Forthur,Puvur");
    CHECK(AttackAckDetail("Lcwarra", {"Puvur"}) == "tank-stays:Lcwarra;held:Puvur");
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

// ZF-1 run 5: the bound lead bot walked off by itself ("move to rpg target",
// travel's grind target) after a rest gate. A binding strips the open-world
// kit and gives back on release exactly what it took; a reset while bound
// re-notes the rebuilt kit, a load or map change adds to it, and an order
// (which changes no strategy) never gives anything back.
static void TestCommanderAutonomyLedger()
{
    auto bit = [](char const* name) -> uint32_t {
        for (unsigned i = 0; i < kCommanderAutonomyCount; ++i)
            if (std::string(CommanderAutonomyStrategy(i)) == name)
                return 1u << i;
        return 0;
    };
    uint32_t const grind = bit("grind"), travel = bit("travel"), rpg = bit("rpg"), tfish = bit("tfish");
    CHECK(grind && travel && rpg && tfish && bit("explore") && bit("travel once"));
    CHECK(!bit("wander") && !bit("stay") && !bit("follow")); // movement modes stay with the binding's orders

    CHECK(CommanderAutonomyChange(grind | rpg, '-') == "-grind,-rpg");
    CHECK(CommanderAutonomyChange(grind | travel | rpg, '+') == "+grind,+travel,+rpg");
    CHECK(CommanderAutonomyChange(0, '+').empty());

    CommanderAutonomyLedger ledger;
    // Bind: the lead bot (group leader kit) loses grind/travel/tfish/rpg.
    CHECK(ledger.Hold(10, grind | travel | tfish | rpg, false) == (grind | travel | tfish | rpg));
    CHECK(ledger.Withheld(10) == (grind | travel | tfish | rpg));
    // A follower with nothing to strip is noted with an empty mask.
    CHECK(ledger.Hold(11, 0, false) == 0 && ledger.Withheld(11) == 0);
    // The bound bot leaves an instance: the instance guard gave rpg back; held again, still one note.
    CHECK(ledger.Hold(11, rpg, false) == rpg && ledger.Withheld(11) == rpg);
    // A strategy reset while bound rebuilt the kit without tfish: the note is the rebuilt kit.
    CHECK(ledger.Hold(10, grind | travel | rpg, true) == (grind | travel | rpg));
    CHECK(ledger.Withheld(10) == (grind | travel | rpg));
    // A snapshot load right after the reset adds what the snapshot carried.
    ledger.Hold(10, tfish, false);
    CHECK(ledger.Withheld(10) == (grind | travel | tfish | rpg));
    // Unbind: exactly what was taken comes back, once.
    CHECK(ledger.Release(10) == (grind | travel | tfish | rpg));
    CHECK(ledger.Release(10) == 0);
    CHECK(ledger.Withheld(10) == 0);
    CHECK(ledger.Hold(0, rpg, false) == 0);
    ledger.Clear();
    CHECK(ledger.Withheld(11) == 0);

    // The config list a bound bot is given drops its open-world adds; what it
    // would have added is noted, so the release gives that back too.
    uint32_t const craft = bit("rpg craft");
    CHECK(CommanderAutonomyAdds("+grind,+loot,+custom::say,+tfish,+wander,+rpg craft") == (grind | tfish | craft));
    CHECK(CommanderAutonomyAdds("-rpg,+travel") == travel);
    CHECK(CommanderAutonomyAdds("") == 0 && CommanderAutonomyAdds("+wander,+stay") == 0);
    CHECK(CommanderAutonomyBit("rpg") == rpg && CommanderAutonomyBit("rpg quest") == 0);
}

// Review of #974: bots bound in the open world and unbound inside Zul'Farrak
// must not get grind/travel/rpg back there (they would walk to rpg and grind
// targets in the dungeon, past the #218 guard, which skips group leaders).
// The release keeps the kit until the bot is on an open-world map again.
static void TestCommanderAutonomyRestoreInsideInstance()
{
    uint32_t const grind = CommanderAutonomyBit("grind"), travel = CommanderAutonomyBit("travel"),
        rpg = CommanderAutonomyBit("rpg"), explore = CommanderAutonomyBit("explore");
    CHECK(!RestoresAutonomyNow(true));
    CHECK(RestoresAutonomyNow(false));

    CommanderAutonomyLedger ledger;
    ledger.Hold(10, grind | travel | rpg | explore, false);   // bound in the open world
    CHECK(ledger.Holds(10));
    uint32_t const released = ledger.Release(10);             // unbind inside ZF
    CHECK(released == (grind | travel | rpg | explore));
    CHECK(!ledger.Holds(10));
    ledger.Defer(10, released);                               // nothing applied inside
    CHECK(ledger.Withheld(10) == released);                   // a save still keeps it as the bot's own
    CHECK(ledger.TakeDeferred(10) == released);               // leaving the instance gives it back, once
    CHECK(ledger.TakeDeferred(10) == 0);
    CHECK(ledger.Withheld(10) == 0);

    // Bound again before leaving: a later release in the open world gives
    // back both what waited and what the new binding took.
    ledger.Defer(11, rpg);
    ledger.Hold(11, grind, false);
    CHECK(ledger.Release(11) == (grind | rpg));
    CHECK(ledger.TakeDeferred(11) == 0);

    // The sweep sees the held bots, not the waiting ones.
    ledger.Hold(12, travel, false);
    ledger.Defer(13, rpg);
    std::vector<uint64_t> held;
    ledger.HeldBots(held);
    CHECK(held.size() == 1 && held[0] == 12);
    ledger.Defer(0, rpg);
    ledger.Defer(14, 0);
    CHECK(ledger.Withheld(0) == 0 && ledger.Withheld(14) == 0);
    ledger.Clear();
    CHECK(ledger.Empty() && ledger.Withheld(13) == 0);
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

static BindFacts RaidBind(size_t bots)
{
    BindFacts f;
    f.enabled = true; f.raid = true; f.raidAllowed = true; f.leadIsTank = true;
    f.lead.controllable = true; f.lead.liveBot = true;
    f.leadInCommanderParty = true; f.leadGrouped = true; f.leadGroupIsRaidOrBg = true;
    BindMember m; m.controllable = true; m.liveBot = true;
    f.members.assign(bots - 1, m);
    return f;
}

static void TestRaidBindRefusals()
{
    CHECK(CheckBind(RaidBind(10)) == BindRefusal::None);
    CHECK(CheckBind(RaidBind(40)) == BindRefusal::None);
    CHECK(CheckBind(RaidBind(6)) == BindRefusal::None);
    CHECK(CheckBind(RaidBind(5)) == BindRefusal::RaidTooSmall);
    CHECK(CheckBind(RaidBind(41)) == BindRefusal::GroupTooLarge);
    auto with = [](void (*edit)(BindFacts&)) { BindFacts f = RaidBind(10); edit(f); return CheckBind(f); };
    CHECK(with([](BindFacts& f) { f.raidAllowed = false; }) == BindRefusal::RaidOff);
    CHECK(with([](BindFacts& f) { f.enabled = false; }) == BindRefusal::Disabled);
    CHECK(with([](BindFacts& f) { f.inParty = true; }) == BindRefusal::RaidInParty);
    CHECK(with([](BindFacts& f) { f.leadGroupIsRaidOrBg = false; }) == BindRefusal::RaidNotInRaid);
    CHECK(with([](BindFacts& f) { f.leadGroupIsBg = true; }) == BindRefusal::RaidNotInRaid);
    CHECK(with([](BindFacts& f) { f.leadInCommanderParty = false; }) == BindRefusal::RaidNotInRaid);
    CHECK(with([](BindFacts& f) { f.leadIsTank = false; }) == BindRefusal::RaidLeadNotTank);
    // A raid with another real player in it is refused (never command a human).
    CHECK(with([](BindFacts& f) { f.leadGroupHasRealPlayer = true; }) == BindRefusal::GroupHasPlayer);
    CHECK(with([](BindFacts& f) { f.lead.hired = true; }) == BindRefusal::LeadHired);
    CHECK(with([](BindFacts& f) { f.members[3].hired = true; }) == BindRefusal::MemberHired);
    // first failing check wins: raid off before a hired lead
    CHECK(with([](BindFacts& f) { f.raidAllowed = false; f.lead.hired = true; }) == BindRefusal::RaidOff);
    // a party bind is untouched by the raid fields
    BindFacts party = RaidBind(10); party.raid = false; party.leadGroupIsRaidOrBg = true;
    CHECK(CheckBind(party) == BindRefusal::LeadGroupRaid);
    CHECK(std::string(BindRefusalCode(BindRefusal::RaidLeadNotTank)) == "raid-lead-not-tank");
    CHECK(std::string(BindRefusalCode(BindRefusal::RaidOff)) == "raid-off");
}

static void TestRaidBindingRelease()
{
    Binding raid = MakeBinding(1, 2, 7); raid.raid = true;
    BindingState s = Standing(7);
    s.raidAllowed = true; s.leadGroupIsRaidOrBg = true; s.memberCount = 40;
    CHECK(CheckBinding(raid, s) == ReleaseReason::None);
    BindingState big = s; big.memberCount = 41;
    CHECK(CheckBinding(raid, big) == ReleaseReason::GroupTooLarge);
    BindingState bg = s; bg.leadGroupIsBg = true;
    CHECK(CheckBinding(raid, bg) == ReleaseReason::GroupRaid);
    BindingState off = s; off.raidAllowed = false;
    CHECK(CheckBinding(raid, off) == ReleaseReason::Disabled);
    BindingState player = s; player.realPlayerInGroup = true;
    CHECK(CheckBinding(raid, player) == ReleaseReason::PlayerJoined);
    // a party binding still ends on a raid conversion and above five
    Binding party = MakeBinding(1, 2, 7);
    CHECK(CheckBinding(party, s) == ReleaseReason::GroupRaid);
    BindingState six = Standing(7); six.memberCount = 6;
    CHECK(CheckBinding(party, six) == ReleaseReason::GroupTooLarge);
}

static void TestRaidBindPlan()
{
    BindPlanFacts f; f.raidBind = true; f.leadInCommanderParty = true; f.partyIsRaidOrBg = true;
    CHECK(DecideBindGroup(f) == BindGroupPlan::KeepCommanderRaid);
    // A raid binds with another real player in it too: the commander is
    // taken out of the raid, the kept group standing as it is.
    f.partyHasOtherPlayer = true;
    CHECK(DecideBindGroup(f) == BindGroupPlan::KeepCommanderRaid);
    BindPlanFacts party; party.leadInCommanderParty = true; party.partyIsRaidOrBg = true;
    CHECK(DecideBindGroup(party) == BindGroupPlan::ReformFromCommanderParty);
    std::vector<std::string> names{"Tanky", "RAID"};
    CHECK(TakeBindRaid(names) && names.size() == 1);
    std::vector<std::string> plain{"Tanky", "Bob"};
    CHECK(!TakeBindRaid(plain) && plain.size() == 2);
}

int main()
{
    TestHiresOfTheCommander();
    TestBindingTable();
    TestFollowers();
    TestDepartedFollowers();
    TestReleaseRule();
    TestBindGate();
    TestFeatureGates();
    TestSystemBinding();
    TestInPartyOnlyRefusal();
    TestBindGroupPlan();
    TestMasterLeftRule();
    TestCommandedThreatBackoff();
    TestPartyAttackTankStays();
    TestAttackLeavesPullRunning();
    TestPartyAttackAckDetail();
    TestHeldAttack();
    TestCommandedBackoffYields();
    TestCommanderThreatLedger();
    TestCommanderAutonomyLedger();
    TestCommanderAutonomyRestoreInsideInstance();
    TestOrderGate();
    TestParseGuid();
    TestParseMovePoint();
    TestParseMoveOrder();
    TestTargetVerdict();
    TestMoveVerdict();
    TestSnapToFloorZ();
    TestDirectVerdict();
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
    TestRaidBindRefusals();
    TestRaidBindingRelease();
    TestRaidBindPlan();
    TestParseRunFor();
    std::printf("remote commander policy: %d checks passed\n", checks);
    return 0;
}
