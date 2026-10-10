package state

import (
	"sort"
	"strconv"
	"strings"
	"time"

	"tortoise-observability/internal/model"
)

// Bounds for the activity rollup. Counters live per bot (bounded by
// maxActivityBots, the same ceiling the emitter uses for its own per-bot
// tables); the three feeds are pool-wide rings so a long uptime cannot grow
// memory without limit. Nothing here is written to the database: the daemon
// aggregates the events the module forwards over UDP.
const (
	maxActivityBots = 5000
	lootFeedLimit   = 600
	questFeedLimit  = 400
	levelFeedLimit  = 800
	// notableQuality mirrors ITEM_QUALITY_UNCOMMON: the "green or better"
	// filter the owner asked for.
	notableQuality = 2
	// flesh-skinning skill id (SKILL_SKINNING in SharedDefines.h).
	skillSkinning = 393
	// activityForgetAfter drops the counters of a bot neither the roster nor
	// an event has shown for this long: a pool reset deletes its characters,
	// and their counters must not pile up in memory and in the state file.
	activityForgetAfter = 3 * 24 * time.Hour
)

// botActivityState is the mutable per-bot accumulator. The exported counters
// are copied out under the store lock; lastMoney/deadAt/openQuests are
// bookkeeping the response never exposes.
type botActivityState struct {
	counters model.BotActivity
	name     string
	class    string
	level    uint32

	lastMoney uint64
	hasMoney  bool
	deadAt    time.Time
	lastLevel uint32
	openQuest map[uint32]bool
	// lastSeen is the unix time of the bot's last roster entry or event.
	lastSeen int64
}

// LootFilter selects rows from the pool-wide loot feed. Zero values mean "no
// constraint"; Bot is a case-insensitive substring match.
type LootFilter struct {
	MinQuality uint32
	MinLevel   uint32
	MaxLevel   uint32
	Class      string
	Bot        string
	Limit      int
}

// QuestFilter selects rows from the pool-wide quest feed.
type QuestFilter struct {
	Bot   string
	Limit int
}

// ApplyBotEvents folds one BOT_EVENTS datagram into the per-bot activity
// rollup and the pool-wide feeds.
func (s *Store) ApplyBotEvents(p *model.BotEventsPayload) {
	if p == nil || len(p.Events) == 0 {
		return
	}
	s.mu.Lock()
	defer s.mu.Unlock()

	now := s.now()
	s.beginSessionLocked(p.Session)
	if s.activity == nil {
		s.activity = make(map[uint32]*botActivityState)
	}
	for i := range p.Events {
		s.applyBotEventLocked(p.Events[i], now)
	}
	if len(s.activity) > maxActivityBots {
		s.activity = make(map[uint32]*botActivityState)
	}
}

func (s *Store) applyBotEventLocked(ev model.BotEvent, now time.Time) {
	if ev.GUID == 0 || ev.Event == "" {
		return
	}
	a := s.activity[ev.GUID]
	if a == nil {
		a = &botActivityState{openQuest: make(map[uint32]bool)}
		a.counters.FirstSeen = now.Unix()
		s.activity[ev.GUID] = a
	}
	a.lastSeen = now.Unix()
	if ev.Bot != "" {
		a.name = ev.Bot
	}
	if ev.Class != "" {
		a.class = ev.Class
	}
	if ev.Level != 0 {
		a.level = ev.Level
	}
	a.counters.Events++
	a.counters.LastEvent = now.Unix()

	// Net money flow between observed events: forward deltas are attributed
	// to earned/spent. Cheat gold is borrowed and restored around the vendor
	// call, so the next forwarded event still reports the real balance.
	if ev.Money > 0 {
		if a.hasMoney {
			if ev.Money > a.lastMoney {
				a.counters.MoneyEarned += ev.Money - a.lastMoney
			} else if ev.Money < a.lastMoney {
				a.counters.MoneySpent += a.lastMoney - ev.Money
			}
		}
		a.lastMoney = ev.Money
		a.hasMoney = true
	}

	questID := uint32(parseUint(ev.Info2))

	switch ev.Event {
	case "QuestRewarded":
		a.counters.QuestsRewarded++
		delete(a.openQuest, questID)
		s.appendQuestFeed(model.QuestFeedItem{At: now.Unix(), Bot: a.name, GUID: ev.GUID, Class: a.class, Level: a.level, Event: ev.Event, Quest: ev.Info1, QuestID: questID})
	case "AcceptQuestAction", "AcceptQuestShareAction":
		a.counters.QuestsAccepted++
		s.appendQuestFeed(model.QuestFeedItem{At: now.Unix(), Bot: a.name, GUID: ev.GUID, Class: a.class, Level: a.level, Event: "AcceptQuestAction", Quest: ev.Info1, QuestID: questID})
	// Quest objectives finished. The module emits QuestCompleted from its own
	// OnQuestComplete hook; QuestUpdateCompleteAction is the packet-driven
	// action of the same name, kept for cores that deliver it to a bot
	// session. Both mean "complete, not yet handed in", so they feed the same
	// counter and the same open-quest ledger.
	case "QuestUpdateCompleteAction", "QuestCompleted":
		a.counters.QuestsCompleted++
		if questID != 0 {
			a.openQuest[questID] = true
		}
		s.appendQuestFeed(model.QuestFeedItem{At: now.Unix(), Bot: a.name, GUID: ev.GUID, Class: a.class, Level: a.level, Event: ev.Event, Quest: ev.Info1, QuestID: questID})
	case "QuestDropped":
		delete(a.openQuest, questID)
		s.appendQuestFeed(model.QuestFeedItem{At: now.Unix(), Bot: a.name, GUID: ev.GUID, Class: a.class, Level: a.level, Event: ev.Event, Quest: ev.Info1, QuestID: questID})
	case "TalkToQuestGiverAction":
		a.counters.QuestHandIns++
		s.appendQuestFeed(model.QuestFeedItem{At: now.Unix(), Bot: a.name, GUID: ev.GUID, Class: a.class, Level: a.level, Event: ev.Event, Quest: ev.Info1, QuestID: questID})

	case "StoreLootAction":
		a.counters.LootItems++
		a.counters.LootValue += uint64(ev.Sell)
		if ev.Quality >= notableQuality {
			a.counters.NotableLoot++
		}
		s.appendLootFeed(model.LootFeedItem{At: now.Unix(), Bot: a.name, GUID: ev.GUID, Class: a.class, Level: a.level, MapID: ev.MapID, ZoneID: ev.ZoneID,
			Source: "loot", Item: ev.Info1, ItemID: ev.ItemID, Quality: ev.Quality, Value: uint64(ev.Sell)})
	case "GatherLoot":
		source := "gather"
		if uint32(parseUint(ev.Info1)) == skillSkinning {
			a.counters.Skinning++
			source = "skin"
		} else {
			a.counters.Gathering++
		}
		// A gather/skin drop is also a skill-up tick for the gathering
		// profession that produced it.
		a.counters.SkillUps++
		s.appendLootFeed(model.LootFeedItem{At: now.Unix(), Bot: a.name, GUID: ev.GUID, Class: a.class, Level: a.level, MapID: ev.MapID, ZoneID: ev.ZoneID,
			Source: source, Item: ev.Info1, ItemID: ev.ItemID, Quality: ev.Quality, Value: uint64(ev.Sell)})
	case "LootMoney":
		s.appendLootFeed(model.LootFeedItem{At: now.Unix(), Bot: a.name, GUID: ev.GUID, Class: a.class, Level: a.level, MapID: ev.MapID, ZoneID: ev.ZoneID,
			Source: "money", Money: uint64(parseUint(ev.Info1))})

	case "SellAction":
		a.counters.ItemsSold++
		a.counters.SoldValue += uint64(ev.Sell)
	case "BuyAction":
		a.counters.ItemsBought++
		a.counters.BoughtValue += uint64(ev.Buy)
	case "RepairAllAction":
		a.counters.Repairs++
		a.counters.RepairCost += uint64(parseUint(ev.Info2))
	case "TrainerAction":
		a.counters.SpellsLearned++
	case "NearbyService":
		switch ev.Info1 {
		case "trainer":
			a.counters.TrainerVisits++
		case "sell":
			a.counters.VendorVisits++
		}
	case "AhAction":
		a.counters.AHListings++
	case "AhBidAction":
		a.counters.AHBids++

	case "Kill":
		a.counters.Kills++
	case "BotDeath":
		a.counters.Deaths++
		a.deadAt = now
	case "ReviveFromCorpseAction", "ReviveFromSpiritHealerAction", "RepopAction":
		if !a.deadAt.IsZero() {
			a.counters.GhostSeconds += now.Sub(a.deadAt).Seconds()
			a.deadAt = time.Time{}
		}
	case "ReachGiveUp":
		a.counters.GiveUps++
	}

	a.counters.OpenQuests = len(a.openQuest)
}

// stampTime renders an unix second in the daemon's own time zone. Feed rows
// carry it so every table on the dashboard shares one time base (the incident
// rows already did): formatting the epoch client-side would use the viewer's
// zone and make the tabs disagree.
func stampTime(ts int64) string {
	if ts == 0 {
		return ""
	}
	return time.Unix(ts, 0).Format("2006-01-02 15:04:05")
}

// appendLootFeed keeps the pool-wide loot feed bounded (newest last).
func (s *Store) appendLootFeed(item model.LootFeedItem) {
	item.TimeStr = stampTime(item.At)
	s.lootFeed = append(s.lootFeed, item)
	if len(s.lootFeed) > lootFeedLimit {
		s.lootFeed = append([]model.LootFeedItem(nil), s.lootFeed[len(s.lootFeed)-lootFeedLimit:]...)
	}
}

func (s *Store) appendQuestFeed(item model.QuestFeedItem) {
	item.TimeStr = stampTime(item.At)
	s.questFeed = append(s.questFeed, item)
	if len(s.questFeed) > questFeedLimit {
		s.questFeed = append([]model.QuestFeedItem(nil), s.questFeed[len(s.questFeed)-questFeedLimit:]...)
	}
}

// observeLevelsLocked records per-bot level transitions from a published
// roster: the first level seen, and every later one. The gap between two
// entries is the time the bot spent on the earlier level.
func (s *Store) observeLevelsLocked(bots []model.BotSnapshot, now time.Time) {
	for i := range bots {
		b := &bots[i]
		if b.GUID == 0 || b.Level == 0 {
			continue
		}
		a := s.activity[b.GUID]
		if a == nil {
			a = &botActivityState{openQuest: make(map[uint32]bool)}
			a.counters.FirstSeen = now.Unix()
			s.activity[b.GUID] = a
		}
		a.lastSeen = now.Unix()
		if b.Name != "" {
			a.name = b.Name
		}
		if b.Class != "" {
			a.class = b.Class
		}
		a.level = b.Level
		if a.lastLevel == b.Level {
			continue
		}
		if a.lastLevel != 0 && b.Level > a.lastLevel {
			a.counters.LevelsGained += int(b.Level - a.lastLevel)
		}
		a.lastLevel = b.Level
		a.counters.Levels = append(a.counters.Levels, model.LevelEvent{Level: b.Level, At: now.Unix()})
		if len(a.counters.Levels) > 300 {
			a.counters.Levels = append([]model.LevelEvent(nil), a.counters.Levels[len(a.counters.Levels)-300:]...)
		}
		s.levelFeed = append(s.levelFeed, model.ActivityLevelItem{At: now.Unix(), TimeStr: stampTime(now.Unix()), Bot: a.name, GUID: b.GUID, Class: a.class, Level: b.Level})
		if len(s.levelFeed) > levelFeedLimit {
			s.levelFeed = append([]model.ActivityLevelItem(nil), s.levelFeed[len(s.levelFeed)-levelFeedLimit:]...)
		}
	}
	s.forgetGoneBotsLocked(now)
}

// forgetGoneBotsLocked drops bots unseen for activityForgetAfter. It runs at
// most once an hour, and the first run waits an hour after a restore so the
// pool has logged back in before anyone counts as gone.
func (s *Store) forgetGoneBotsLocked(now time.Time) {
	if now.Sub(s.activityForgotAt) < time.Hour {
		return
	}
	s.activityForgotAt = now
	cutoff := now.Add(-activityForgetAfter).Unix()
	for guid, a := range s.activity {
		if a.lastSeen < cutoff {
			delete(s.activity, guid)
		}
	}
}

// activityForLocked returns the counters to embed in a roster snapshot (no
// level timeline: that is only served by the activity API).
func (s *Store) activityForLocked(guid uint32) *model.BotActivity {
	a := s.activity[guid]
	if a == nil {
		return nil
	}
	cp := a.counters
	cp.Levels = nil
	return &cp
}

// LootFeed returns the pool-wide loot feed, newest first, filtered.
func (s *Store) LootFeed(f LootFilter) []model.LootFeedItem {
	s.mu.RLock()
	defer s.mu.RUnlock()

	out := make([]model.LootFeedItem, 0, 64)
	for i := len(s.lootFeed) - 1; i >= 0; i-- {
		it := s.lootFeed[i]
		if f.MinQuality > 0 && it.Quality < f.MinQuality {
			continue
		}
		if f.MinLevel > 0 && it.Level < f.MinLevel {
			continue
		}
		if f.MaxLevel > 0 && it.Level > f.MaxLevel {
			continue
		}
		if f.Class != "" && !strings.EqualFold(it.Class, f.Class) {
			continue
		}
		if f.Bot != "" && !strings.Contains(strings.ToLower(it.Bot), strings.ToLower(f.Bot)) {
			continue
		}
		out = append(out, it)
		if f.Limit > 0 && len(out) >= f.Limit {
			break
		}
	}
	return out
}

// QuestFeed returns the pool-wide quest feed, newest first, filtered.
func (s *Store) QuestFeed(f QuestFilter) []model.QuestFeedItem {
	s.mu.RLock()
	defer s.mu.RUnlock()

	out := make([]model.QuestFeedItem, 0, 64)
	bot := strings.ToLower(f.Bot)
	for i := len(s.questFeed) - 1; i >= 0; i-- {
		it := s.questFeed[i]
		if bot != "" && !strings.Contains(strings.ToLower(it.Bot), bot) {
			continue
		}
		out = append(out, it)
		if f.Limit > 0 && len(out) >= f.Limit {
			break
		}
	}
	return out
}

// Activity builds the pool-wide activity view for GET /api/v1/activity.
func (s *Store) Activity() model.ActivityResponse {
	s.mu.RLock()
	defer s.mu.RUnlock()

	now := s.now()
	out := model.ActivityResponse{Bots: []model.ActivityBot{}, LevelFeed: []model.ActivityLevelItem{}}
	earliest := int64(0)
	for guid, a := range s.activity {
		entry := a.counters
		cp := entry
		cp.Levels = nil
		bot := model.ActivityBot{GUID: guid, Name: a.name, Class: a.class, Level: a.level, Activity: cp}
		if e, ok := s.bots[guid]; ok {
			if bot.Name == "" {
				bot.Name = e.snap.Name
			}
			if bot.Class == "" {
				bot.Class = e.snap.Class
			}
			if bot.Level == 0 {
				bot.Level = e.snap.Level
			}
		}
		out.Bots = append(out.Bots, bot)
		out.Summary.Counters = addCounters(out.Summary.Counters, entry)
		if earliest == 0 || (a.counters.FirstSeen != 0 && a.counters.FirstSeen < earliest) {
			earliest = a.counters.FirstSeen
		}
	}
	out.Summary.BotsTracked = len(out.Bots)
	if !s.sessionSince.IsZero() {
		out.Summary.Since = s.sessionSince.Unix()
		out.Summary.SinceStr = stampTime(out.Summary.Since)
	}
	if earliest != 0 {
		out.Summary.ElapsedSec = now.Sub(time.Unix(earliest, 0)).Seconds()
	}
	sort.Slice(out.Bots, func(i, j int) bool {
		if out.Bots[i].Activity.LootItems != out.Bots[j].Activity.LootItems {
			return out.Bots[i].Activity.LootItems > out.Bots[j].Activity.LootItems
		}
		return out.Bots[i].Name < out.Bots[j].Name
	})
	for i := len(s.levelFeed) - 1; i >= 0; i-- {
		out.LevelFeed = append(out.LevelFeed, s.levelFeed[i])
	}
	return out
}

func addCounters(a, b model.BotActivity) model.BotActivity {
	a.QuestsRewarded += b.QuestsRewarded
	a.QuestsAccepted += b.QuestsAccepted
	a.QuestsCompleted += b.QuestsCompleted
	a.QuestHandIns += b.QuestHandIns
	a.OpenQuests += b.OpenQuests
	a.LootItems += b.LootItems
	a.LootValue += b.LootValue
	a.NotableLoot += b.NotableLoot
	a.ItemsSold += b.ItemsSold
	a.SoldValue += b.SoldValue
	a.ItemsBought += b.ItemsBought
	a.BoughtValue += b.BoughtValue
	a.MoneyEarned += b.MoneyEarned
	a.MoneySpent += b.MoneySpent
	a.Kills += b.Kills
	a.Deaths += b.Deaths
	a.GhostSeconds += b.GhostSeconds
	a.TrainerVisits += b.TrainerVisits
	a.SpellsLearned += b.SpellsLearned
	a.VendorVisits += b.VendorVisits
	a.Repairs += b.Repairs
	a.RepairCost += b.RepairCost
	a.GiveUps += b.GiveUps
	a.Skinning += b.Skinning
	a.Gathering += b.Gathering
	a.SkillUps += b.SkillUps
	a.AHListings += b.AHListings
	a.AHBids += b.AHBids
	a.Events += b.Events
	a.LevelsGained += b.LevelsGained
	if a.FirstSeen == 0 || (b.FirstSeen != 0 && b.FirstSeen < a.FirstSeen) {
		a.FirstSeen = b.FirstSeen
	}
	if b.LastEvent > a.LastEvent {
		a.LastEvent = b.LastEvent
	}
	return a
}

// parseUint is a tolerant strconv.ParseUint for the free-text event fields
// the module forwards (they are numeric for the events that use them).
func parseUint(s string) uint64 {
	s = strings.TrimSpace(s)
	if s == "" {
		return 0
	}
	if i := strings.IndexAny(s, " |"); i >= 0 {
		s = s[:i]
	}
	v, err := strconv.ParseUint(s, 10, 40)
	if err != nil {
		return 0
	}
	return v
}
