package state

import (
	"os"
	"path/filepath"
	"testing"
	"time"

	"tortoise-observability/internal/model"
)

func events(session uint64, evs ...model.BotEvent) *model.BotEventsPayload {
	return &model.BotEventsPayload{
		V: model.ProtocolVersion, Session: session, Seq: 1, Type: "BOT_EVENTS", Events: evs,
	}
}

// publishRoster drives one complete cycle so publishLocked runs (level
// tracking and activity pruning happen there, not on the event path).
func publishRoster(s *Store, seq uint64, bots ...model.BotSnapshot) {
	s.ApplyHeartbeat(heartbeat(seq, uint32(len(bots))))
	s.ApplyBatch(batch(seq, 0, 1, bots...))
}

func TestBotEventsFoldIntoCounters(t *testing.T) {
	c := newClock()
	s := newTestStore(c)

	s.ApplyBotEvents(events(1,
		model.BotEvent{Event: "AcceptQuestAction", Info1: "A Peon's Burden", Info2: "6391", Bot: "Braltur", GUID: 7, Class: "shaman", Level: 9, Money: 1000},
		model.BotEvent{Event: "QuestUpdateCompleteAction", Info2: "6391", Bot: "Braltur", GUID: 7, Class: "shaman", Level: 9, Money: 1000},
		model.BotEvent{Event: "TalkToQuestGiverAction", Info1: "A Peon's Burden", Info2: "6391", Bot: "Braltur", GUID: 7, Class: "shaman", Level: 9, Money: 1500},
		model.BotEvent{Event: "QuestRewarded", Info1: "A Peon's Burden", Info2: "6391", Bot: "Braltur", GUID: 7, Class: "shaman", Level: 9, Money: 1500},
		model.BotEvent{Event: "StoreLootAction", Info1: "Refreshing Spring Water", Info2: "159", Bot: "Braltur", GUID: 7, Class: "shaman", Level: 9, MapID: 1, ZoneID: 14, ItemID: 159, Quality: 3, Sell: 1, Buy: 4, Money: 1200},
		model.BotEvent{Event: "StoreLootAction", Info1: "Bent Large Shield", Info2: "2211", Bot: "Braltur", GUID: 7, Class: "shaman", Level: 9, ItemID: 2211, Quality: 1, Sell: 7, Money: 1300},
		model.BotEvent{Event: "GatherLoot", Info1: "393", Info2: "2318", Bot: "Braltur", GUID: 7, Class: "shaman", Level: 9, ItemID: 2318, Quality: 1, Sell: 15, Money: 1400},
		model.BotEvent{Event: "GatherLoot", Info1: "186", Info2: "2770", Bot: "Braltur", GUID: 7, Class: "shaman", Level: 9, ItemID: 2770, Quality: 1, Sell: 10, Money: 1450},
		model.BotEvent{Event: "LootMoney", Info1: "37", Info2: "2977", Bot: "Braltur", GUID: 7, Class: "shaman", Level: 9, Money: 1487},
		model.BotEvent{Event: "SellAction", Info1: "Bent Large Shield", Info2: "2211", Bot: "Braltur", GUID: 7, ItemID: 2211, Quality: 1, Sell: 7, Money: 1494},
		model.BotEvent{Event: "BuyAction", Info1: "Refreshing Spring Water", Info2: "159", Bot: "Braltur", GUID: 7, ItemID: 159, Buy: 4, Money: 1490},
		model.BotEvent{Event: "RepairAllAction", Info1: "60", Info2: "125", Bot: "Braltur", GUID: 7, Money: 1365},
		model.BotEvent{Event: "NearbyService", Info1: "trainer", Info2: "1234", Bot: "Braltur", GUID: 7, Money: 1365},
		model.BotEvent{Event: "TrainerAction", Info1: "Lightning Bolt", Info2: "403", Bot: "Braltur", GUID: 7, Money: 1365},
		model.BotEvent{Event: "NearbyService", Info1: "sell", Info2: "1234", Bot: "Braltur", GUID: 7, Money: 1600},
		model.BotEvent{Event: "AhAction", Info1: "Small Egg", Info2: "6889", Bot: "Braltur", GUID: 7, ItemID: 6889, Buy: 40, Money: 1600},
		model.BotEvent{Event: "AhBidAction", Info1: "Small Egg", Info2: "6889", Bot: "Braltur", GUID: 7, ItemID: 6889, Money: 1560},
		model.BotEvent{Event: "ReachGiveUp", Info1: "Wedge", Info2: "no-move|entry=1", Bot: "Braltur", GUID: 7, Money: 1560},
		model.BotEvent{Event: "Kill", Info1: "Plainstrider", Info2: "45", Bot: "Braltur", GUID: 7, Money: 1560},
		model.BotEvent{Event: "BotDeath", Info1: "Plainstrider", Info2: "12", Bot: "Braltur", GUID: 7, Money: 1560},
	))

	// A second bot with a distinct class, for the feed filters.
	s.ApplyBotEvents(events(1,
		model.BotEvent{Event: "StoreLootAction", Info1: "Linen Cloth", Info2: "2589", Bot: "Drarm", GUID: 9, Class: "warrior", Level: 4, ItemID: 2589, Quality: 1, Sell: 2, Money: 500},
		model.BotEvent{Event: "StoreLootAction", Info1: "Rare Thing", Info2: "9999", Bot: "Drarm", GUID: 9, Class: "warrior", Level: 4, ItemID: 9999, Quality: 4, Sell: 500, Money: 600},
	))

	c.Advance(90 * time.Second)
	s.ApplyBotEvents(events(1,
		model.BotEvent{Event: "ReviveFromSpiritHealerAction", Bot: "Braltur", GUID: 7, Money: 1560},
	))

	publishRoster(s, 1,
		model.BotSnapshot{Name: "Braltur", GUID: 7, Class: "shaman", Level: 9, State: "idle"},
		model.BotSnapshot{Name: "Drarm", GUID: 9, Class: "warrior", Level: 4, State: "idle"},
	)

	snap := s.Snapshot()
	var braltur *model.BotActivity
	for i := range snap.Bots {
		if snap.Bots[i].GUID == 7 {
			braltur = snap.Bots[i].Activity
		}
	}
	if braltur == nil {
		t.Fatal("expected Braltur activity embedded in the roster snapshot")
	}
	if braltur.QuestsRewarded != 1 || braltur.QuestsAccepted != 1 || braltur.QuestsCompleted != 1 ||
		braltur.QuestHandIns != 1 || braltur.OpenQuests != 0 {
		t.Fatalf("quest counters wrong: %+v", braltur)
	}
	if braltur.LootItems != 2 || braltur.NotableLoot != 1 || braltur.LootValue != 8 {
		t.Fatalf("loot counters wrong: %+v", braltur)
	}
	if braltur.Skinning != 1 || braltur.Gathering != 1 || braltur.SkillUps != 2 {
		t.Fatalf("gather counters wrong: %+v", braltur)
	}
	if braltur.ItemsSold != 1 || braltur.SoldValue != 7 || braltur.ItemsBought != 1 || braltur.BoughtValue != 4 {
		t.Fatalf("vendor counters wrong: %+v", braltur)
	}
	if braltur.Repairs != 1 || braltur.RepairCost != 125 {
		t.Fatalf("repair counters wrong: %+v", braltur)
	}
	if braltur.TrainerVisits != 1 || braltur.SpellsLearned != 1 || braltur.VendorVisits != 1 {
		t.Fatalf("service counters wrong: %+v", braltur)
	}
	if braltur.AHListings != 1 || braltur.AHBids != 1 {
		t.Fatalf("auction counters wrong: %+v", braltur)
	}
	if braltur.Kills != 1 || braltur.Deaths != 1 || braltur.GiveUps != 1 {
		t.Fatalf("kill/death/give-up counters wrong: %+v", braltur)
	}
	if braltur.GhostSeconds < 89 || braltur.GhostSeconds > 91 {
		t.Fatalf("ghost seconds = %v, want ~90", braltur.GhostSeconds)
	}
	// Money: 1000 -> 1500 earned 500, 1500 -> 1200 spent 300, then net small
	// moves; the deltas must net out to the sum of every observed delta.
	if braltur.MoneyEarned < 500 || braltur.MoneySpent < 300 {
		t.Fatalf("money deltas wrong: earned=%d spent=%d", braltur.MoneyEarned, braltur.MoneySpent)
	}
	if braltur.Levels != nil {
		t.Fatalf("roster copy must not carry the level timeline: %+v", braltur.Levels)
	}
}

func TestLootFeedFiltersNewestFirst(t *testing.T) {
	c := newClock()
	s := newTestStore(c)
	s.ApplyBotEvents(events(1,
		model.BotEvent{Event: "StoreLootAction", Info1: "Common Cloth", Info2: "1", Bot: "Alpha", GUID: 1, Class: "warrior", Level: 3, ItemID: 1, Quality: 1, Sell: 2},
	))
	c.Advance(time.Second)
	s.ApplyBotEvents(events(1,
		model.BotEvent{Event: "StoreLootAction", Info1: "Blue Sword", Info2: "2", Bot: "Beta", GUID: 2, Class: "rogue", Level: 12, ItemID: 2, Quality: 3, Sell: 300},
		model.BotEvent{Event: "StoreLootAction", Info1: "Green Boots", Info2: "3", Bot: "Beta", GUID: 2, Class: "rogue", Level: 12, ItemID: 3, Quality: 2, Sell: 40},
	))

	all := s.LootFeed(LootFilter{})
	if len(all) != 3 {
		t.Fatalf("want 3 loot rows, got %d", len(all))
	}
	if all[0].Item != "Green Boots" || all[2].Item != "Common Cloth" {
		t.Fatalf("feed is not newest-first: %v", []string{all[0].Item, all[1].Item, all[2].Item})
	}
	if got := s.LootFeed(LootFilter{MinQuality: 2}); len(got) != 2 || got[0].Quality < 2 {
		t.Fatalf("min_quality filter wrong: %+v", got)
	}
	if got := s.LootFeed(LootFilter{Class: "ROGUE"}); len(got) != 2 {
		t.Fatalf("class filter must be case-insensitive by name: %+v", got)
	}
	if got := s.LootFeed(LootFilter{Bot: "alph"}); len(got) != 1 || got[0].Bot != "Alpha" {
		t.Fatalf("bot filter wrong: %+v", got)
	}
	if got := s.LootFeed(LootFilter{MinLevel: 10}); len(got) != 2 {
		t.Fatalf("min_level filter wrong: %+v", got)
	}
	if got := s.LootFeed(LootFilter{MaxLevel: 5}); len(got) != 1 {
		t.Fatalf("max_level filter wrong: %+v", got)
	}
	if got := s.LootFeed(LootFilter{Limit: 1}); len(got) != 1 {
		t.Fatalf("limit ignored: %+v", got)
	}
}

func TestQuestFeedAndActivitySummary(t *testing.T) {
	c := newClock()
	s := newTestStore(c)
	s.ApplyBotEvents(events(1,
		model.BotEvent{Event: "AcceptQuestAction", Info1: "Quest A", Info2: "10", Bot: "Alpha", GUID: 1, Class: "warrior", Level: 5},
	))
	c.Advance(time.Second)
	s.ApplyBotEvents(events(1,
		model.BotEvent{Event: "QuestUpdateCompleteAction", Info2: "10", Bot: "Alpha", GUID: 1, Class: "warrior", Level: 5},
	))
	c.Advance(time.Minute)
	// Level transition observed from the roster.
	publishRoster(s, 1, model.BotSnapshot{Name: "Alpha", GUID: 1, Class: "warrior", Level: 5, State: "idle"})
	c.Advance(2 * time.Minute)
	publishRoster(s, 2, model.BotSnapshot{Name: "Alpha", GUID: 1, Class: "warrior", Level: 6, State: "idle"})

	feed := s.QuestFeed(QuestFilter{})
	if len(feed) != 2 || feed[0].Event != "QuestUpdateCompleteAction" || feed[1].Quest != "Quest A" {
		t.Fatalf("quest feed wrong: %+v", feed)
	}
	if got := s.QuestFeed(QuestFilter{Bot: "alpha"}); len(got) != 2 {
		t.Fatalf("quest bot filter wrong: %+v", got)
	}
	if got := s.QuestFeed(QuestFilter{Bot: "zzz"}); len(got) != 0 {
		t.Fatalf("quest bot filter should exclude: %+v", got)
	}

	act := s.Activity()
	if act.Summary.BotsTracked != 1 {
		t.Fatalf("bots_tracked = %d", act.Summary.BotsTracked)
	}
	if act.Summary.Counters.QuestsAccepted != 1 || act.Summary.Counters.QuestsCompleted != 1 {
		t.Fatalf("summary counters wrong: %+v", act.Summary.Counters)
	}
	if act.Summary.Counters.LevelsGained != 1 {
		t.Fatalf("levels gained = %d, want 1", act.Summary.Counters.LevelsGained)
	}
	if len(act.Bots) != 1 || act.Bots[0].Name != "Alpha" || act.Bots[0].Level != 6 {
		t.Fatalf("activity bots wrong: %+v", act.Bots)
	}
	if len(act.LevelFeed) != 2 {
		t.Fatalf("level feed = %+v, want 2 entries", act.LevelFeed)
	}
	// Newest first: the L6 transition precedes the L5 one.
	if act.LevelFeed[0].Level != 6 || act.LevelFeed[1].Level != 5 {
		t.Fatalf("level feed order wrong: %+v", act.LevelFeed)
	}
	if act.Summary.ElapsedSec < 170 {
		t.Fatalf("elapsed_sec = %v, want >= 170", act.Summary.ElapsedSec)
	}
}

func TestActivityClearedOnSessionChangeAndRosterWipe(t *testing.T) {
	c := newClock()
	s := newTestStore(c)
	s.ApplyBotEvents(events(1, model.BotEvent{Event: "Kill", Info1: "Boar", Info2: "20", Bot: "Alpha", GUID: 1, Class: "warrior", Level: 5}))
	if len(s.Activity().Bots) != 1 {
		t.Fatal("expected one tracked bot")
	}

	// A different game-server process (new session id) resets everything.
	s.ApplyBotEvents(events(2, model.BotEvent{Event: "Kill", Info1: "Boar", Info2: "20", Bot: "Beta", GUID: 2, Class: "rogue", Level: 7}))
	act := s.Activity()
	if len(act.Bots) != 1 || act.Bots[0].Name != "Beta" || act.Bots[0].Activity.Kills != 1 {
		t.Fatalf("session change did not reset activity: %+v", act.Bots)
	}

	// Roster TTL wipe clears counters and feeds too.
	publishRoster(s, 1, model.BotSnapshot{Name: "Beta", GUID: 2, Class: "rogue", Level: 7, State: "idle"})
	c.Advance(60 * time.Second)
	if !s.Evict() {
		t.Fatal("expected the roster to expire")
	}
	if got := s.Activity(); len(got.Bots) != 0 || len(got.LevelFeed) != 0 {
		t.Fatalf("roster wipe did not clear activity: %+v", got)
	}
}

func TestPruneDepartedBotActivity(t *testing.T) {
	c := newClock()
	s := newTestStore(c)
	s.ApplyBotEvents(events(1,
		model.BotEvent{Event: "Kill", Info1: "Boar", Info2: "20", Bot: "Alpha", GUID: 1, Class: "warrior", Level: 5},
		model.BotEvent{Event: "Kill", Info1: "Boar", Info2: "20", Bot: "Beta", GUID: 2, Class: "rogue", Level: 6},
	))
	// Beta logs out: the next published roster no longer carries it.
	publishRoster(s, 1, model.BotSnapshot{Name: "Alpha", GUID: 1, Class: "warrior", Level: 5, State: "idle"})
	act := s.Activity()
	if len(act.Bots) != 1 || act.Bots[0].Name != "Alpha" {
		t.Fatalf("departed bot activity not pruned: %+v", act.Bots)
	}
}

// The module's own quest-complete signal is QuestCompleted (OnQuestComplete).
// QuestUpdateCompleteAction is packet-driven and never reaches a headless bot
// session, so without this mapping the completed/open-quest numbers were
// structurally zero on the live dashboard.
func TestQuestCompletedEventFeedsCountersAndFeed(t *testing.T) {
	c := newClock()
	s := newTestStore(c)

	s.ApplyBotEvents(events(1,
		model.BotEvent{Event: "AcceptQuestAction", Info1: "Vile Familiars", Info2: "792", Bot: "Kraejar", GUID: 4, Class: "rogue", Level: 8},
	))
	c.Advance(time.Minute)
	s.ApplyBotEvents(events(1,
		model.BotEvent{Event: "QuestCompleted", Info1: "Vile Familiars", Info2: "792", Bot: "Kraejar", GUID: 4, Class: "rogue", Level: 8},
	))

	act := s.Activity()
	if act.Summary.Counters.QuestsCompleted != 1 || act.Summary.Counters.OpenQuests != 1 {
		t.Fatalf("QuestCompleted did not count as complete/open: %+v", act.Summary.Counters)
	}
	feed := s.QuestFeed(QuestFilter{})
	if len(feed) != 2 || feed[0].Event != "QuestCompleted" || feed[0].Quest != "Vile Familiars" {
		t.Fatalf("quest feed wrong: %+v", feed)
	}

	// Handing the quest in closes the open-quest ledger.
	c.Advance(time.Minute)
	s.ApplyBotEvents(events(1,
		model.BotEvent{Event: "TalkToQuestGiverAction", Info1: "Vile Familiars", Info2: "792", Bot: "Kraejar", GUID: 4, Class: "rogue", Level: 8},
		model.BotEvent{Event: "QuestRewarded", Info1: "Vile Familiars", Info2: "792", Bot: "Kraejar", GUID: 4, Class: "rogue", Level: 8},
	))
	if got := s.Activity().Summary.Counters.OpenQuests; got != 0 {
		t.Fatalf("open quests = %d after the reward, want 0", got)
	}
}

// Feed rows carry the daemon's own rendered timestamp so every table on the
// dashboard shares one time base (the incident rows already did).
func TestFeedRowsCarryDaemonTimestamp(t *testing.T) {
	c := newClock()
	s := newTestStore(c)
	s.ApplyBotEvents(events(1,
		model.BotEvent{Event: "StoreLootAction", Info1: "Malachite", Info2: "774", Bot: "Alpha", GUID: 1, Class: "warrior", Level: 4, ItemID: 774, Quality: 2, Sell: 15},
		model.BotEvent{Event: "QuestCompleted", Info1: "Quest A", Info2: "10", Bot: "Alpha", GUID: 1, Class: "warrior", Level: 4},
	))
	publishRoster(s, 1, model.BotSnapshot{Name: "Alpha", GUID: 1, Class: "warrior", Level: 4, State: "idle"})

	want := "2026-09-10 12:00:00"
	if rows := s.LootFeed(LootFilter{}); len(rows) != 1 || rows[0].TimeStr != want {
		t.Fatalf("loot feed timestamp = %+v, want %s", rows, want)
	}
	if rows := s.QuestFeed(QuestFilter{}); len(rows) != 1 || rows[0].TimeStr != want {
		t.Fatalf("quest feed timestamp = %+v, want %s", rows, want)
	}
	if rows := s.Activity().LevelFeed; len(rows) != 1 || rows[0].TimeStr != want {
		t.Fatalf("level feed timestamp = %+v, want %s", rows, want)
	}
}

// Every counter window must be reported: the dashboard labels "since 12:00",
// never an unlabelled total.
func TestActivitySummaryReportsWindowStart(t *testing.T) {
	c := newClock()
	s := newTestStore(c)
	s.ApplyBotEvents(events(1, model.BotEvent{Event: "Kill", Info1: "Boar", Info2: "20", Bot: "Alpha", GUID: 1, Class: "warrior", Level: 5}))

	act := s.Activity()
	if act.Summary.Since != c.Now().Unix() || act.Summary.SinceStr != "2026-09-10 12:00:00" {
		t.Fatalf("activity window = %d/%q, want the session start", act.Summary.Since, act.Summary.SinceStr)
	}
}

// A dashboard restart must not reset the counters; a game-server restart must.
func TestActivitySurvivesDashboardRestart(t *testing.T) {
	path := filepath.Join(t.TempDir(), "activity-state.json")
	c := newClock()
	s := newTestStore(c)
	s.ApplyBotEvents(events(1,
		model.BotEvent{Event: "StoreLootAction", Info1: "Malachite", Info2: "774", Bot: "Alpha", GUID: 1, Class: "warrior", Level: 4, ItemID: 774, Quality: 2, Sell: 15},
		model.BotEvent{Event: "QuestCompleted", Info1: "Quest A", Info2: "10", Bot: "Alpha", GUID: 1, Class: "warrior", Level: 4},
		model.BotEvent{Event: "Kill", Info1: "Boar", Info2: "20", Bot: "Alpha", GUID: 1, Class: "warrior", Level: 4},
	))
	if err := s.SaveActivity(path); err != nil {
		t.Fatalf("save failed: %v", err)
	}

	// New daemon process, same game server.
	restarted := newTestStore(c)
	n, err := restarted.RestoreActivity(path)
	if err != nil || n != 1 {
		t.Fatalf("restore = %d, %v; want 1 bot", n, err)
	}
	act := restarted.Activity()
	if len(act.Bots) != 1 || act.Bots[0].Activity.LootItems != 1 || act.Bots[0].Activity.Kills != 1 ||
		act.Bots[0].Activity.QuestsCompleted != 1 || act.Bots[0].Activity.OpenQuests != 1 {
		t.Fatalf("counters did not survive the restart: %+v", act.Bots)
	}
	if act.Summary.SinceStr != "2026-09-10 12:00:00" || len(act.LevelFeed) != 0 {
		t.Fatalf("window/feeds not restored: since=%q levels=%+v", act.Summary.SinceStr, act.LevelFeed)
	}
	if len(restarted.LootFeed(LootFilter{})) != 1 || len(restarted.QuestFeed(QuestFilter{})) != 1 {
		t.Fatalf("feeds did not survive the restart")
	}

	// The restored session continues: a datagram with the same session id
	// must add to the counters instead of wiping them.
	restarted.ApplyBotEvents(events(1, model.BotEvent{Event: "Kill", Info1: "Boar", Info2: "20", Bot: "Alpha", GUID: 1, Class: "warrior", Level: 4}))
	if got := restarted.Activity().Bots[0].Activity.Kills; got != 2 {
		t.Fatalf("kills = %d after restore + same-session event, want 2", got)
	}

	// A restarted game server invalidates the restored counters.
	restarted.ApplyBotEvents(events(2, model.BotEvent{Event: "Kill", Info1: "Boar", Info2: "20", Bot: "Beta", GUID: 2, Class: "rogue", Level: 7}))
	after := restarted.Activity()
	if len(after.Bots) != 1 || after.Bots[0].Name != "Beta" || after.Bots[0].Activity.Kills != 1 {
		t.Fatalf("new session must reset the restored counters: %+v", after.Bots)
	}
	if after.Summary.Since != c.Now().Unix() {
		t.Fatalf("window start not moved to the new session: %d", after.Summary.Since)
	}
}

func TestRestoreActivityIgnoresMissingAndCorruptFiles(t *testing.T) {
	dir := t.TempDir()
	s := newTestStore(newClock())

	if n, err := s.RestoreActivity(filepath.Join(dir, "absent.json")); n != 0 || err != nil {
		t.Fatalf("missing file: n=%d err=%v, want 0/nil", n, err)
	}

	corrupt := filepath.Join(dir, "corrupt.json")
	if err := os.WriteFile(corrupt, []byte("{not json"), 0644); err != nil {
		t.Fatal(err)
	}
	if _, err := s.RestoreActivity(corrupt); err == nil {
		t.Fatal("corrupt snapshot must be reported, not silently ignored")
	}
	if got := s.Activity(); len(got.Bots) != 0 {
		t.Fatalf("corrupt restore must leave the store empty: %+v", got.Bots)
	}
}
