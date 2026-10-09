package state

import (
	"encoding/json"
	"os"
	"time"

	"tortoise-observability/internal/model"
)

// The activity rollup is in-memory, so a dashboard restart used to reset every
// counter and the owner could not tell "19 loot" from "19 loot since the
// dashboard came up". The daemon snapshots the rollup to one small JSON file
// every minute (see cmd/server) and restores it at startup.
//
// The counters are cumulative: they continue across a dashboard restart and
// across a game-server restart (a new session only re-anchors money tracking
// in beginSessionLocked), so the file is restored whatever session it names.
const activityStateVersion = 1

type persistedActivity struct {
	V         int                       `json:"v"`
	Session   uint64                    `json:"session"`
	SavedAt   int64                     `json:"saved_at"`
	Since     int64                     `json:"since"`
	Bots      []persistedBot            `json:"bots"`
	LootFeed  []model.LootFeedItem      `json:"loot_feed,omitempty"`
	QuestFeed []model.QuestFeedItem     `json:"quest_feed,omitempty"`
	LevelFeed []model.ActivityLevelItem `json:"level_feed,omitempty"`
}

// persistedBot mirrors botActivityState (unexported, and its bookkeeping has
// no JSON shape of its own).
type persistedBot struct {
	GUID       uint32            `json:"guid"`
	Name       string            `json:"name"`
	Class      string            `json:"class"`
	Level      uint32            `json:"level"`
	Counters   model.BotActivity `json:"counters"`
	LastMoney  uint64            `json:"last_money"`
	HasMoney   bool              `json:"has_money"`
	DeadAt     int64             `json:"dead_at,omitempty"`
	LastLevel  uint32            `json:"last_level"`
	OpenQuests []uint32          `json:"open_quests,omitempty"`
}

// SaveActivity writes the activity rollup to path. The write is atomic
// (temp file + rename) so a crash mid-write cannot leave a truncated
// snapshot behind. An empty path disables persistence.
func (s *Store) SaveActivity(path string) error {
	if path == "" {
		return nil
	}

	s.mu.RLock()
	snap := persistedActivity{
		V:         activityStateVersion,
		Session:   s.session,
		SavedAt:   s.now().Unix(),
		LootFeed:  s.lootFeed,
		QuestFeed: s.questFeed,
		LevelFeed: s.levelFeed,
		Bots:      make([]persistedBot, 0, len(s.activity)),
	}
	if !s.sessionSince.IsZero() {
		snap.Since = s.sessionSince.Unix()
	}
	for guid, a := range s.activity {
		pb := persistedBot{
			GUID:      guid,
			Name:      a.name,
			Class:     a.class,
			Level:     a.level,
			Counters:  a.counters,
			LastMoney: a.lastMoney,
			HasMoney:  a.hasMoney,
			LastLevel: a.lastLevel,
		}
		if !a.deadAt.IsZero() {
			pb.DeadAt = a.deadAt.Unix()
		}
		if len(a.openQuest) > 0 {
			pb.OpenQuests = make([]uint32, 0, len(a.openQuest))
			for id := range a.openQuest {
				pb.OpenQuests = append(pb.OpenQuests, id)
			}
		}
		snap.Bots = append(snap.Bots, pb)
	}
	s.mu.RUnlock()

	data, err := json.Marshal(snap)
	if err != nil {
		return err
	}
	tmp := path + ".tmp"
	if err := os.WriteFile(tmp, data, 0644); err != nil {
		return err
	}
	return os.Rename(tmp, path)
}

// RestoreActivity loads a snapshot written by SaveActivity and returns the
// number of restored bots. A missing file is not an error; a corrupt one is
// reported and leaves the store unchanged.
func (s *Store) RestoreActivity(path string) (int, error) {
	if path == "" {
		return 0, nil
	}
	data, err := os.ReadFile(path)
	if err != nil {
		if os.IsNotExist(err) {
			return 0, nil
		}
		return 0, err
	}
	var snap persistedActivity
	if err := json.Unmarshal(data, &snap); err != nil {
		return 0, err
	}
	if snap.V != activityStateVersion {
		return 0, nil
	}

	activity := make(map[uint32]*botActivityState, len(snap.Bots))
	for _, b := range snap.Bots {
		if b.GUID == 0 {
			continue
		}
		a := &botActivityState{
			counters:  b.Counters,
			name:      b.Name,
			class:     b.Class,
			level:     b.Level,
			lastMoney: b.LastMoney,
			hasMoney:  false, // re-anchor on first live event
			lastLevel: b.LastLevel,
			openQuest: make(map[uint32]bool, len(b.OpenQuests)),
		}
		if b.DeadAt > 0 {
			a.deadAt = time.Unix(b.DeadAt, 0)
		}
		for _, id := range b.OpenQuests {
			a.openQuest[id] = true
		}
		activity[b.GUID] = a
	}

	s.mu.Lock()
	defer s.mu.Unlock()
	s.session = snap.Session
	s.sessionSince = time.Time{}
	if snap.Since > 0 {
		s.sessionSince = time.Unix(snap.Since, 0)
	}
	s.activity = activity
	s.lootFeed = snap.LootFeed
	s.questFeed = snap.QuestFeed
	s.levelFeed = snap.LevelFeed
	return len(activity), nil
}
