#include "../ai/playerbot/PullJoinWindowPolicy.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

namespace
{
constexpr unsigned kEngines = 4; // BotState: combat, non-combat, dead, reaction
constexpr unsigned kCombat = 1u << 0;
constexpr unsigned kNonCombat = 1u << 1;
constexpr unsigned kAll = 0xF;

// What PlayerbotDbStore::Save writes for the window's parts.
struct Snapshot
{
    bool wait[kEngines] = {};
    unsigned waitSeconds = 0;
    bool anchor = false;
};

// A held DPS bot reduced to the state the pull hold touches, wired the way
// PlayerbotAI (StartPullWindow / ChangeStrategy + SyncPullWindow /
// EndPullWindow), RestampPullParty, ReleaseHeldPartyNow and PlayerbotDbStore
// use the policy.
struct Bot
{
    bool wait[kEngines] = {};   // "wait for attack" per engine
    bool anchor = false;        // posMap "pull hold" + the stay it owns
    unsigned waitSeconds = 10;  // "wait for attack time" (value default)
    ai::PullJoinWindow window;

    // PlayerbotAI::ChangeStrategy: op is '+', '-' or '~'.
    void Change(char op, unsigned mask)
    {
        for (unsigned i = 0; i < kEngines; ++i)
        {
            if (!(mask & (1u << i)))
                continue;
            wait[i] = op == '+' ? true : op == '-' ? false : !wait[i];
        }
        if (op == '-')
            anchor = false; // the "-wait for attack" rule: ReleasePullHold
        Sync(mask);
    }

    // PlayerbotAI::SyncPullWindow
    void Sync(unsigned mask)
    {
        if (!window.Running())
            return;
        window.Forget(mask);
        if (!wait[0])
            End();
    }

    // PlayerbotAI::EndPullWindow
    void End()
    {
        ai::PullJoinWindow const old = window;
        window.Clear();
        for (unsigned i = 0; i < kEngines; ++i)
            if (old.Added(i) && wait[i])
                Change('-', 1u << i);
        if (old.Running())
            waitSeconds = old.savedWaitSeconds;
        anchor = false;
    }

    // PausePartyDpsForPull -> PlayerbotAI::StartPullWindow
    void Hold(std::time_t now, unsigned seconds, bool leadKeyed = false, unsigned minSeconds = 0)
    {
        anchor = true;
        unsigned const previous = waitSeconds;
        unsigned added = 0;
        for (unsigned i = 0; i < kEngines; ++i)
        {
            if (wait[i])
                continue;
            Change('+', 1u << i);
            added |= 1u << i;
        }
        waitSeconds = seconds;
        window.Start(now, added, previous, leadKeyed, minSeconds);
    }

    // RestampPullParty (landing / pullback tank back)
    void Land(std::time_t now) { if (window.Running()) window.Restamp(now, true); }

    // ReleaseHeldPartyNow
    void FailedPull() { if (window.Running()) waitSeconds = 0; }

    // `wait for attack time N` -> PlayerbotAI::SetPlayerWaitForAttackTime
    void PlayerSetsSeconds(unsigned seconds)
    {
        if (!window.DeferPlayerWaitSeconds(seconds))
            waitSeconds = seconds;
    }

    // OnCombatEnded, death, SetMovementStrategy: the anchor only.
    void AnchorReleased() { anchor = false; }

    // "pull hold expired" (in the combat and non-combat engines' "wait for
    // attack") -> "release pull hold" -> EndPullWindow
    bool Tick(std::time_t now)
    {
        if (!(wait[0] || wait[1]) || !window.Running() || !window.Expired(now, waitSeconds))
            return false;
        End();
        return true;
    }

    Snapshot Save() const
    {
        Snapshot s;
        for (unsigned i = 0; i < kEngines; ++i)
            s.wait[i] = wait[i] && !window.Added(i);
        s.waitSeconds = window.Running() ? window.savedWaitSeconds : waitSeconds;
        s.anchor = anchor;
        return s;
    }

    // PlayerbotDbStore::Load: EndPullWindow first, then the engines are
    // replaced only when the snapshot has their rows (ClearStrategies does not
    // go through ChangeStrategy), values are loaded, and a restored anchor is
    // released.
    void Load(Snapshot const& s, bool strategyRows)
    {
        End();
        if (strategyRows)
        {
            for (unsigned i = 0; i < kEngines; ++i)
            {
                wait[i] = false;
                if (s.wait[i])
                    Change('+', 1u << i);
            }
        }
        waitSeconds = s.waitSeconds;
        anchor = s.anchor;
        anchor = false; // ReleasePullHold
    }

    // PlayerbotAI::ResetStrategies without a load: EndPullWindow if one
    // runs, then every engine is wiped without ChangeStrategy.
    void ResetStrategies()
    {
        if (window.Running())
            End();
        for (unsigned i = 0; i < kEngines; ++i)
            wait[i] = false;
    }

    // A fresh AI after a relog: nothing in memory but the snapshot.
    static Bot Relog(Snapshot const& s)
    {
        Bot bot;
        bot.Load(s, true);
        return bot;
    }

    bool AnyWait() const { return wait[0] || wait[1] || wait[2] || wait[3]; }
};
}

int main()
{
    std::cout << "Starting TortoiseBots pull join window policy tests...\n";

    // Plain pull: held from the command, the delay restarts at the landing.
    {
        Bot bot;
        bot.Hold(100, 10);
        CHECK(bot.wait[0] && bot.wait[1] && bot.anchor && bot.window.Running());
        bot.Land(104);
        CHECK(!bot.Tick(113));
        CHECK(bot.Tick(114));
        CHECK(!bot.AnyWait() && !bot.anchor && !bot.window.Running());
        CHECK(bot.waitSeconds == 10);
    }
    std::cout << "  [PASS] plain pull: window ends join delay after the landing\n";

    // The leak (headless repro L1b): the mob dies inside the window. Combat
    // end releases the anchor; the window must still end on its own clock.
    {
        Bot bot;
        bot.Hold(100, 60);
        bot.Land(102);
        bot.AnchorReleased();
        CHECK(bot.wait[0]);
        CHECK(!bot.Tick(150));
        CHECK(bot.Tick(162));
        CHECK(!bot.AnyWait());
    }
    std::cout << "  [PASS] window ends after combat end released the anchor\n";

    // A movement order or a death during the hold drops the anchor only.
    {
        Bot bot;
        bot.Hold(100, 10);
        bot.AnchorReleased();
        CHECK(bot.Tick(110));
        CHECK(!bot.AnyWait() && !bot.window.Running());
    }
    std::cout << "  [PASS] window ends after a movement order or death\n";

    // Chain pull: B ordered while A is still dying; B's delay runs from B's
    // landing, and the value from before A comes back.
    {
        Bot bot;
        bot.waitSeconds = 7;
        bot.Hold(100, 10);
        bot.Land(102);
        bot.Hold(105, 12);
        CHECK(bot.window.Added(0) && bot.window.savedWaitSeconds == 7);
        bot.AnchorReleased();
        CHECK(!bot.Tick(108));
        bot.Land(109);
        CHECK(!bot.Tick(120));
        CHECK(bot.Tick(121));
        CHECK(!bot.AnyWait() && bot.waitSeconds == 7);
    }
    std::cout << "  [PASS] chain pull keeps its join delay across A's death\n";

    // A failed pull frees the party at once (ReleaseHeldPartyNow), and the
    // zeroed delay does not stay.
    {
        Bot bot;
        bot.waitSeconds = 8;
        bot.Hold(100, 10);
        bot.FailedPull();
        CHECK(bot.Tick(100));
        CHECK(!bot.AnyWait() && bot.waitSeconds == 8);
    }
    std::cout << "  [PASS] failed pull releases the hold and keeps the player's delay\n";

    // The player's own `co +wait for attack` (combat engine only) and delay:
    // the pull adds only the other engines and takes only those back.
    {
        Bot bot;
        bot.Change('+', kCombat);
        bot.PlayerSetsSeconds(8);
        bot.Hold(100, 10);
        CHECK(!bot.window.Added(0) && bot.window.Added(1));
        CHECK(bot.waitSeconds == 10);
        bot.AnchorReleased();
        CHECK(bot.Tick(110));
        CHECK(bot.wait[0] && !bot.wait[1] && !bot.wait[2] && !bot.wait[3]);
        CHECK(bot.waitSeconds == 8 && !bot.anchor && !bot.window.Running());
    }
    std::cout << "  [PASS] player's own co-only wait for attack and delay outlive the window\n";

    // The player adds it, or sets a delay, while the pull's window runs.
    {
        Bot bot;
        bot.Hold(100, 10);
        bot.Change('+', kAll);
        bot.PlayerSetsSeconds(15);
        CHECK(bot.waitSeconds == 10); // the pull keeps its delay meanwhile
        CHECK(bot.Tick(110));
        CHECK(bot.wait[0] && bot.wait[1] && bot.waitSeconds == 15);
    }
    std::cout << "  [PASS] wait for attack or delay set during the window stay the player's\n";

    // Attack / flee / `co -wait for attack` end the window at once.
    {
        Bot bot;
        bot.waitSeconds = 9;
        bot.Hold(100, 10);
        bot.Change('-', kAll);
        CHECK(!bot.AnyWait() && !bot.anchor && !bot.window.Running());
        CHECK(bot.waitSeconds == 9);
        CHECK(!bot.Tick(200));
    }
    std::cout << "  [PASS] explicit attack/flee ends the window\n";

    // The `~` toggle goes through the same read-back: toggling the combat
    // copy off ends the hold; toggling a pull copy off and on makes it the
    // player's.
    {
        Bot bot;
        bot.Hold(100, 10);
        bot.Change('~', kCombat);
        CHECK(!bot.wait[0] && !bot.window.Running() && !bot.AnyWait());

        Bot other;
        other.Hold(100, 10);
        other.Change('~', kNonCombat);
        other.Change('~', kNonCombat);
        CHECK(other.window.Running() && !other.window.Added(1));
        CHECK(other.Tick(110));
        CHECK(!other.wait[0] && other.wait[1]);
    }
    std::cout << "  [PASS] toggled wait for attack is tracked like + and -\n";

    // Relog / DB restore: the pull's copies and delay are never stored, and a
    // reload ends the window - also when the snapshot has no strategy rows
    // and the live engines are left as they were.
    {
        Bot bot;
        bot.waitSeconds = 6;
        bot.Hold(100, 10);
        Snapshot const s = bot.Save();
        CHECK(!s.wait[0] && !s.wait[1] && s.waitSeconds == 6);
        Bot relogged = Bot::Relog(s);
        CHECK(!relogged.AnyWait() && !relogged.anchor && relogged.waitSeconds == 6);

        Bot reset;
        reset.Hold(100, 10);
        reset.Load(Snapshot{}, false); // value-only preset
        CHECK(!reset.AnyWait() && !reset.anchor && !reset.window.Running());

        Bot own;
        own.Change('+', kCombat);
        own.Hold(100, 10);
        Snapshot const o = own.Save();
        CHECK(o.wait[0] && !o.wait[1]);
        Bot ownRelogged = Bot::Relog(o);
        CHECK(ownRelogged.wait[0] && !ownRelogged.wait[1] && !ownRelogged.anchor);
    }
    std::cout << "  [PASS] nothing of a pull survives a relog or reload\n";

    // A full reset without a load (`reset` with loadStrats off, a BG join, a
    // bot without a player relation) wipes the engines behind the window's
    // back: the window must not stay running with the pull's delay.
    {
        Bot bot;
        bot.waitSeconds = 5;
        bot.Hold(100, 10);
        bot.ResetStrategies();
        CHECK(!bot.window.Running() && !bot.AnyWait() && !bot.anchor);
        CHECK(bot.waitSeconds == 5);
    }
    std::cout << "  [PASS] a full reset ends the window and restores the delay\n";

    // A named preset loaded mid-hold: the preset's own delay wins over the
    // value from before the pull.
    {
        Bot bot;
        bot.waitSeconds = 5;
        bot.Hold(100, 10);
        Snapshot preset;
        preset.wait[0] = true;
        preset.waitSeconds = 4;
        bot.Load(preset, true);
        CHECK(!bot.window.Running() && bot.wait[0] && !bot.wait[1]);
        CHECK(bot.waitSeconds == 4);
    }
    // lead_keyed_holds_until_the_pack_is_on_the_tank
    {
        ai::PullLeadFacts f;
        f.landed = true; f.elapsedSeconds = 6; f.minSeconds = 5;
        f.tankAlive = true; f.packUnits = 3; f.packOnTank = 2;
        CHECK(!ai::PullLeadReleases(f));
        f.packOnTank = 3;
        CHECK(ai::PullLeadReleases(f));
    }
    std::cout << "  [PASS] lead_keyed_holds_until_the_pack_is_on_the_tank\n";

    // lead_keyed_never_releases_before_landing
    {
        ai::PullLeadFacts f;
        f.landed = false; f.elapsedSeconds = 30; f.minSeconds = 5;
        f.tankAlive = true; f.packUnits = 0; f.packOnTank = 0;
        CHECK(!ai::PullLeadReleases(f));
    }
    std::cout << "  [PASS] lead_keyed_never_releases_before_landing\n";

    // lead_keyed_cap_releases
    {
        ai::PullLeadFacts f;
        f.landed = true; f.minSeconds = 5; f.tankAlive = true;
        f.packUnits = 3; f.packOnTank = 0;
        f.elapsedSeconds = 10;
        CHECK(ai::PullLeadReleases(f));
        f.elapsedSeconds = 9;
        CHECK(!ai::PullLeadReleases(f));
    }
    std::cout << "  [PASS] lead_keyed_cap_releases\n";

    // lead_keyed_waits_the_join_delay
    {
        ai::PullLeadFacts f;
        f.landed = true; f.elapsedSeconds = 3; f.minSeconds = 5;
        f.tankAlive = true; f.packUnits = 2; f.packOnTank = 2;
        CHECK(!ai::PullLeadReleases(f));
    }
    std::cout << "  [PASS] lead_keyed_waits_the_join_delay\n";

    // held_bot_hit_releases_at_once
    {
        ai::PullLeadFacts f;
        f.landed = true; f.elapsedSeconds = 1; f.minSeconds = 5;
        f.tankAlive = true; f.botIsVictim = true;
        f.packUnits = 3; f.packOnTank = 0;
        CHECK(ai::PullLeadReleases(f));
    }
    std::cout << "  [PASS] held_bot_hit_releases_at_once\n";

    // tank_low_or_dead_releases
    {
        ai::PullLeadFacts f;
        f.landed = true; f.elapsedSeconds = 1; f.minSeconds = 5;
        f.tankAlive = true; f.tankLow = true;
        CHECK(ai::PullLeadReleases(f));
        f.tankLow = false; f.tankAlive = false;
        CHECK(ai::PullLeadReleases(f));
    }
    std::cout << "  [PASS] tank_low_or_dead_releases\n";

    // empty_pack_releases_after_the_join_delay
    {
        ai::PullLeadFacts f;
        f.landed = true; f.elapsedSeconds = 5; f.minSeconds = 5;
        f.tankAlive = true; f.packUnits = 0; f.packOnTank = 0;
        CHECK(ai::PullLeadReleases(f));
    }
    std::cout << "  [PASS] empty_pack_releases_after_the_join_delay\n";

    // restamp_marks_landed_only_when_told
    {
        ai::PullJoinWindow w;
        w.Start(100, 1, 10, true, 5);
        w.Restamp(103, false);
        CHECK(!w.landed && w.start == 103);
        w.Restamp(109, true);
        CHECK(w.landed);
        w.Clear();
        CHECK(!w.leadKeyed && !w.landed && w.minSeconds == 0);
    }
    std::cout << "  [PASS] restamp_marks_landed_only_when_told\n";

    // chain_pull_takes_the_latest_lead_key_and_starts_unlanded
    {
        ai::PullJoinWindow w;
        w.Start(100, 1, 10, true, 5);
        w.Restamp(102, true);
        CHECK(w.landed);
        w.Start(105, 1, 12, true, 7);
        CHECK(!w.landed && w.minSeconds == 7 && w.leadKeyed);
        CHECK(w.Added(0) && w.savedWaitSeconds == 10);
    }
    std::cout << "  [PASS] chain_pull_takes_the_latest_lead_key_and_starts_unlanded\n";
    // reason_names_each_rule
    {
        ai::PullLeadFacts f;
        f.tankAlive = true;
        CHECK(ai::PullLeadReleaseReason(f) == nullptr);
        f.landed = true; f.elapsedSeconds = 10;
        CHECK(std::string(ai::PullLeadReleaseReason(f)) == "cap");
        f.elapsedSeconds = 0; f.tankAlive = false;
        CHECK(std::string(ai::PullLeadReleaseReason(f)) == "tank-dead");
        f.tankAlive = true; f.botIsVictim = true;
        CHECK(std::string(ai::PullLeadReleaseReason(f)) == "hit");
        f.botIsVictim = false; f.tankLow = true;
        CHECK(std::string(ai::PullLeadReleaseReason(f)) == "tank-low");
        f.tankLow = false; f.elapsedSeconds = 5; f.minSeconds = 5;
        f.packUnits = 2; f.packOnTank = 2;
        CHECK(std::string(ai::PullLeadReleaseReason(f)) == "tank-first");
        f.packOnTank = 1;
        CHECK(ai::PullLeadReleaseReason(f) == nullptr);
    }
    std::cout << "  [PASS] reason_names_each_rule\n";

    // hit_wins_over_tank_low
    {
        ai::PullLeadFacts f;
        f.landed = true; f.elapsedSeconds = 1; f.minSeconds = 5;
        f.tankAlive = true; f.botIsVictim = true; f.tankLow = true;
        CHECK(std::string(ai::PullLeadReleaseReason(f)) == "hit");
    }
    std::cout << "  [PASS] hit_wins_over_tank_low\n";

    // releases_wraps_the_reason
    {
        ai::PullLeadFacts cases[7] = {};
        cases[1].landed = true; cases[1].elapsedSeconds = 10; cases[1].tankAlive = true;
        cases[2].landed = true; cases[2].tankAlive = false;
        cases[3].landed = true; cases[3].tankAlive = true; cases[3].botIsVictim = true;
        cases[4].landed = true; cases[4].tankAlive = true; cases[4].tankLow = true;
        cases[5].landed = true; cases[5].elapsedSeconds = 5; cases[5].minSeconds = 5;
        cases[5].tankAlive = true; cases[5].packUnits = 2; cases[5].packOnTank = 2;
        cases[6].landed = true; cases[6].elapsedSeconds = 5; cases[6].minSeconds = 5;
        cases[6].tankAlive = true; cases[6].packUnits = 2; cases[6].packOnTank = 1;
        for (int i = 0; i < 7; ++i)
            CHECK(ai::PullLeadReleases(cases[i]) == (ai::PullLeadReleaseReason(cases[i]) != nullptr));
    }
    std::cout << "  [PASS] releases_wraps_the_reason\n";

    // hired_run_member_list_opens: the live hired case (5 bots; the
    // real-player hirer commands from outside the party, so nobody in the
    // list is a real player).
    auto HiredRunMembers = []() -> std::vector<ai::FightStartMember>
    {
        std::vector<ai::FightStartMember> members;
        ai::FightStartMember self;
        self.self = true; self.alive = true; self.inWorld = true; self.sameMap = true;
        members.push_back(self);
        ai::FightStartMember tank;
        tank.alive = true; tank.inWorld = true; tank.sameMap = true; tank.tank = true;
        members.push_back(tank);
        ai::FightStartMember healer;
        healer.alive = true; healer.inWorld = true; healer.sameMap = true;
        members.push_back(healer);
        for (int i = 0; i < 2; ++i)
        {
            ai::FightStartMember dps;
            dps.alive = true; dps.inWorld = true; dps.sameMap = true;
            members.push_back(dps);
        }
        return members;
    };
    {
        std::vector<ai::FightStartMember> members = HiredRunMembers();
        ai::FightStartHoldFacts f;
        f.runningRun = true;
        ai::FillFightStartGroupFacts(f, members);
        CHECK(f.playerInGroup == false);
        CHECK(f.tankInGroup == true);
        CHECK(ai::FightStartHoldOpens(f));
        CHECK(ai::FightStartHoldSkipReason(f) == nullptr);
    }
    std::cout << "  [PASS] hired_run_member_list_opens\n";

    // player_in_group_skips: a real player in the group skips even dead and
    // off-map (still that player's party).
    {
        std::vector<ai::FightStartMember> members = HiredRunMembers();
        ai::FightStartMember player;
        player.realPlayer = true; player.inWorld = true;
        members.push_back(player);
        ai::FightStartHoldFacts f;
        f.runningRun = true;
        ai::FillFightStartGroupFacts(f, members);
        CHECK(f.playerInGroup == true);
        CHECK(f.tankInGroup == true);
        CHECK(!ai::FightStartHoldOpens(f));
        const char* skip = ai::FightStartHoldSkipReason(f);
        CHECK(skip != nullptr);
        CHECK(std::string(skip) == "player-in-group");
    }
    std::cout << "  [PASS] player_in_group_skips\n";

    // self_and_out_of_world_members_ignored
    {
        std::vector<ai::FightStartMember> members;
        ai::FightStartMember self;
        self.self = true; self.alive = true; self.inWorld = true; self.sameMap = true;
        self.tank = true; self.realPlayer = true;
        members.push_back(self);
        ai::FightStartMember ghost;
        ghost.realPlayer = true; ghost.alive = true; ghost.sameMap = true; ghost.tank = true;
        members.push_back(ghost);
        ai::FightStartHoldFacts f;
        f.runningRun = true;
        ai::FillFightStartGroupFacts(f, members);
        CHECK(f.playerInGroup == false);
        CHECK(f.tankInGroup == false);
    }
    std::cout << "  [PASS] self_and_out_of_world_members_ignored\n";

    // dead_or_other_map_tank_is_no_tank
    {
        ai::FightStartHoldFacts f;
        f.runningRun = true;
        std::vector<ai::FightStartMember> dead;
        ai::FightStartMember tank;
        tank.alive = false; tank.inWorld = true; tank.sameMap = true; tank.tank = true;
        dead.push_back(tank);
        ai::FillFightStartGroupFacts(f, dead);
        CHECK(f.tankInGroup == false);
        CHECK(!ai::FightStartHoldOpens(f));
        CHECK(ai::FightStartHoldSkipReason(f) != nullptr);
        CHECK(std::string(ai::FightStartHoldSkipReason(f)) == "no-tank");
        ai::FightStartHoldFacts g;
        g.runningRun = true;
        std::vector<ai::FightStartMember> away;
        ai::FightStartMember farTank;
        farTank.alive = true; farTank.inWorld = true; farTank.sameMap = false; farTank.tank = true;
        away.push_back(farTank);
        ai::FillFightStartGroupFacts(g, away);
        CHECK(g.tankInGroup == false);
        CHECK(!ai::FightStartHoldOpens(g));
        CHECK(ai::FightStartHoldSkipReason(g) != nullptr);
        CHECK(std::string(ai::FightStartHoldSkipReason(g)) == "no-tank");
    }
    std::cout << "  [PASS] dead_or_other_map_tank_is_no_tank\n";

    // skip_reason_order: each fact alone names its rule, in fixed order.
    {
        ai::FightStartHoldFacts base;
        base.runningRun = true; base.tankInGroup = true;
        CHECK(ai::FightStartHoldSkipReason(base) == nullptr);
        ai::FightStartHoldFacts f = base;
        f.runningRun = false;
        CHECK(std::string(ai::FightStartHoldSkipReason(f)) == "no-run");
        f = base; f.playerInGroup = true;
        CHECK(std::string(ai::FightStartHoldSkipReason(f)) == "player-in-group");
        f = base; f.pullWindow = true;
        CHECK(std::string(ai::FightStartHoldSkipReason(f)) == "pull-window");
        f = base; f.isTank = true;
        CHECK(std::string(ai::FightStartHoldSkipReason(f)) == "tank");
        f = base; f.isHealer = true;
        CHECK(std::string(ai::FightStartHoldSkipReason(f)) == "healer");
        f = base; f.tankInGroup = false;
        CHECK(std::string(ai::FightStartHoldSkipReason(f)) == "no-tank");
        f = base; f.botIsVictim = true;
        CHECK(std::string(ai::FightStartHoldSkipReason(f)) == "hit");
        f = base; f.ccMark = true;
        CHECK(std::string(ai::FightStartHoldSkipReason(f)) == "cc-mark");
        ai::FightStartHoldFacts all;
        all.runningRun = false; all.playerInGroup = true; all.pullWindow = true;
        all.isTank = true; all.isHealer = true; all.tankInGroup = false;
        all.botIsVictim = true; all.ccMark = true;
        CHECK(std::string(ai::FightStartHoldSkipReason(all)) == "no-run");
        all.runningRun = true;
        CHECK(std::string(ai::FightStartHoldSkipReason(all)) == "player-in-group");
    }
    std::cout << "  [PASS] skip_reason_order\n";

    // opens_wraps_skip_reason
    {
        ai::FightStartHoldFacts cases[10] = {};
        cases[0].runningRun = true; cases[0].tankInGroup = true;
        cases[1] = cases[0]; cases[1].runningRun = false;
        cases[2] = cases[0]; cases[2].playerInGroup = true;
        cases[3] = cases[0]; cases[3].pullWindow = true;
        cases[4] = cases[0]; cases[4].isTank = true;
        cases[5] = cases[0]; cases[5].isHealer = true;
        cases[6] = cases[0]; cases[6].tankInGroup = false;
        cases[7] = cases[0]; cases[7].botIsVictim = true;
        cases[8] = cases[0]; cases[8].ccMark = true;
        cases[9] = cases[0]; cases[9].pullWindow = true; cases[9].isTank = true; cases[9].ccMark = true;
        for (int i = 0; i < 10; ++i)
            CHECK(ai::FightStartHoldOpens(cases[i]) == (ai::FightStartHoldSkipReason(cases[i]) == nullptr));
    }
    std::cout << "  [PASS] opens_wraps_skip_reason\n";

    // fight_start_opens_only_unheld_bot_only_dps
    {
        ai::FightStartHoldFacts base;
        base.runningRun = true; base.tankInGroup = true;
        CHECK(ai::FightStartHoldOpens(base));
        ai::FightStartHoldFacts f = base;
        f.playerInGroup = true;
        CHECK(!ai::FightStartHoldOpens(f));
        f = base; f.pullWindow = true;
        CHECK(!ai::FightStartHoldOpens(f));
        f = base; f.isTank = true;
        CHECK(!ai::FightStartHoldOpens(f));
        f = base; f.isHealer = true;
        CHECK(!ai::FightStartHoldOpens(f));
        f = base; f.botIsVictim = true;
        CHECK(!ai::FightStartHoldOpens(f));
        f = base; f.ccMark = true;
        CHECK(!ai::FightStartHoldOpens(f));
        f = base; f.runningRun = false;
        CHECK(!ai::FightStartHoldOpens(f));
        f = base; f.tankInGroup = false;
        CHECK(!ai::FightStartHoldOpens(f));
    }
    std::cout << "  [PASS] fight_start_opens_only_unheld_bot_only_dps\n";

    // fight_start_window_flag
    {
        ai::PullJoinWindow w;
        w.Start(100, 1, 3, true, 0, true);
        CHECK(w.fightStart);
        w.Clear();
        CHECK(!w.fightStart);
        w.Start(200, 1, 3, true, 5);
        CHECK(!w.fightStart && w.minSeconds == 5 && !w.landed);
    }
    std::cout << "  [PASS] fight_start_window_flag\n";

    // fight_start_min_zero_releases_at_once_when_held
    {
        ai::PullLeadFacts f;
        f.landed = true; f.elapsedSeconds = 0; f.minSeconds = 0;
        f.tankAlive = true; f.packUnits = 1; f.packOnTank = 1;
        CHECK(std::string(ai::PullLeadReleaseReason(f)) == "tank-first");
        f.packUnits = 0; f.packOnTank = 0;
        CHECK(std::string(ai::PullLeadReleaseReason(f)) == "tank-first");
    }
    std::cout << "  [PASS] fight_start_min_zero_releases_at_once_when_held\n";

    // fight_start_minimum_keeps_tank_first: a fight-start hold keeps the DPS
    // until its minimum even with the whole pack on the tank (a mob's victim
    // is the tank from its first swing, when its threat lead is one hit); hit,
    // tank-low and tank-dead still release at once.
    {
        ai::PullLeadFacts f;
        f.landed = true; f.tankAlive = true;
        f.packUnits = 2; f.packOnTank = 2;
        f.minSeconds = ai::kFightStartMinSeconds;
        f.elapsedSeconds = 1;
        CHECK(ai::PullLeadReleaseReason(f) == nullptr);
        f.elapsedSeconds = 2;
        CHECK(ai::PullLeadReleaseReason(f) == nullptr);
        f.elapsedSeconds = 3;
        CHECK(std::string(ai::PullLeadReleaseReason(f)) == "tank-first");
        f.elapsedSeconds = 1; f.botIsVictim = true;
        CHECK(std::string(ai::PullLeadReleaseReason(f)) == "hit");
        f.botIsVictim = false; f.tankLow = true;
        CHECK(std::string(ai::PullLeadReleaseReason(f)) == "tank-low");
        f.tankLow = false; f.tankAlive = false;
        CHECK(std::string(ai::PullLeadReleaseReason(f)) == "tank-dead");
    }
    std::cout << "  [PASS] fight_start_minimum_keeps_tank_first\n";

    // chain_run_leader_onto_fight_start_takes_the_latest_kind
    {
        // A run-leader pull chained onto a fight-start hold with no Clear
        // between: the latest Start wins, so the logged kind is "pull"
        // (lead-keyed, not fight-start) and the clock restarts unlanded.
        ai::PullJoinWindow w;
        w.Start(100, 1, 3, true, 0, true);
        CHECK(w.fightStart && w.leadKeyed);
        w.Restamp(101, true);
        CHECK(w.landed);
        w.Start(102, 1, 3, true, 5);
        CHECK(!w.fightStart && w.leadKeyed && !w.landed && w.minSeconds == 5);
    }
    std::cout << "  [PASS] chain_run_leader_onto_fight_start_takes_the_latest_kind\n";

    std::cout << "All TortoiseBots pull join window policy tests passed.\n";
    return 0;
}
