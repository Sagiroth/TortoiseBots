package state

import (
	"fmt"
	"sort"
	"time"

	"tortoise-observability/internal/model"
)

// Dungeon runs (issue #895): a read-only live view of every instance copy
// (dungeon or raid) that has bots in it. A run is one copy — map + instance
// id — so two players running two copies of the same dungeon show up as two
// runs. Real players riding the same snapshot appear in the run they are in;
// battlegrounds have their own PvP panel and are left out, as is the open
// world.
//
// The grouping is pure (GroupDungeonRuns) so a unit test can pin it down;
// the store only remembers when it first saw each run (DungeonRun.FirstSeen,
// how long a run has been up) and serves the view.

// DungeonKey joins a map id and an instance id into a run key.
func DungeonKey(mapID, instance uint32) string {
	return fmt.Sprintf("%d:%d", mapID, instance)
}

// continentMapIDs are the open-world maps of the 1.12 client (the two
// continents, plus the outland/northrend ids they would be on a later
// client). Only a pre-v12 module needs them: it reports no instance field,
// so a dungeon copy can only be told from the open world by the map not
// being a continent. A v12 module reports instance = 0 in the open world,
// which needs no guess.
var continentMapIDs = map[uint32]bool{
	0:   true, // Eastern Kingdoms
	1:   true, // Kalimdor
	530: true, // Outland
	571: true, // Northrend
}

// dungeonRunKey returns the run key of one roster unit, or "" when it is not
// in a dungeon/raid copy: open world (instance present and 0), a battleground
// (pvp "bg", whichever map it is), or a unit the roster keeps without a
// position. A pre-v12 module (Instance nil) falls back to one run per map —
// its copies of a dungeon then appear as one run.
func dungeonRunKey(b model.BotSnapshot) string {
	if b.Pvp == "bg" {
		return ""
	}
	if b.Instance != nil {
		if *b.Instance == 0 {
			return ""
		}
		return DungeonKey(b.MapID, *b.Instance)
	}
	if continentMapIDs[b.MapID] {
		return ""
	}
	return DungeonKey(b.MapID, 0)
}

// dungeonMemberDead reports whether a roster row is a dead unit: the state
// says dead, or the corpse reports 0 HP.
func dungeonMemberDead(b model.BotSnapshot) bool {
	return b.State == "dead" || (b.HP == 0 && b.MaxHP > 0)
}

// GroupDungeonRuns groups a roster snapshot into runs: units of the same
// (map, instance) copy in one run, runs without at least one bot dropped
// (a player alone in an instance is no run). Returns run key -> members.
func GroupDungeonRuns(bots []model.BotSnapshot) map[string][]model.BotSnapshot {
	runs := make(map[string][]model.BotSnapshot)
	for _, b := range bots {
		key := dungeonRunKey(b)
		if key == "" {
			continue
		}
		runs[key] = append(runs[key], b)
	}
	for key, members := range runs {
		botsOnly := false
		for _, m := range members {
			if !m.Player {
				botsOnly = true
				break
			}
		}
		if !botsOnly {
			delete(runs, key)
		}
	}
	return runs
}

// noteDungeonsLocked refreshes the first-seen times of the runs in the fresh
// roster and forgets the runs that are gone. Called from publishLocked, the
// roster lock held.
func (s *Store) noteDungeonsLocked(bots []model.BotSnapshot, now time.Time) {
	runs := GroupDungeonRuns(bots)
	next := make(map[string]time.Time, len(runs))
	for key := range runs {
		if seen, ok := s.dungeonSeen[key]; ok {
			next[key] = seen
		} else {
			next[key] = now
		}
	}
	s.dungeonSeen = next
}

// dungeonMember trims one roster row to what the Dungeons tab shows.
func dungeonMember(b model.BotSnapshot) model.DungeonMember {
	// Power rides along only when the roster row reports one (issue #1112):
	// a module older than the power fields sends max_power 0, and a class
	// with no resource does too. The pointers then stay nil, so the member
	// JSON has no power keys and the tab draws no bar instead of an empty one.
	var power, maxPower *uint32
	if b.MaxPower > 0 {
		power, maxPower = &b.Power, &b.MaxPower
	}
	return model.DungeonMember{
		Name:      b.Name,
		GUID:      b.GUID,
		Player:    b.Player,
		Kind:      b.Kind,
		Class:     b.Class,
		Role:      b.Role,
		Level:     b.Level,
		HP:        b.HP,
		MaxHP:     b.MaxHP,
		Power:     power,
		MaxPower:  maxPower,
		PowerType: b.PowerType,
		Dead:      dungeonMemberDead(b),
		SubGroup:  b.SubGroup,
		State:     b.State,
		MapID:     b.MapID,
		Zone:      b.ZoneID,
		X:         b.X,
		Y:         b.Y,
		Z:         b.Z,
		O:         b.O,
	}
}

// sortRunMembers orders a run's members for the tab: the real player(s)
// first (they are marked apart, not lost between bots), then by raid
// subgroup, then by name.
func sortRunMembers(members []model.DungeonMember) {
	sort.Slice(members, func(i, j int) bool {
		a, b := members[i], members[j]
		if a.Player != b.Player {
			return a.Player
		}
		if a.SubGroup != b.SubGroup {
			return a.SubGroup < b.SubGroup
		}
		return a.Name < b.Name
	})
}

// instanceRun is one roster-derived run before the raid/dungeon split: IsRaidRun decides the tab.
type instanceRun struct {
	run  model.DungeonRun
	raid bool
}

// IsRaidRun reports whether a run is a raid run (D7): any member's roster
// row carries the raid flag, or its matched DUNGEON_MOBS entry does. Neither
// source means dungeon, so an unknown copy stays where it was (old modules
// keep today's behaviour). Never a size threshold, never raids.csv.
func IsRaidRun(members []model.BotSnapshot, mobsRaid bool) bool {
	for _, m := range members {
		if m.Raid {
			return true
		}
	}
	return mobsRaid
}

// instanceRunsLocked builds the roster-derived runs with their mob join and
// first-seen ages, without the raid/dungeon split. Called with the roster
// lock held; the fresh mob entries are passed in (never take the mobs mutex
// under the roster lock).
func (s *Store) instanceRunsLocked(bots map[uint32]*botEntry, mobs map[string]dungeonMobsEntry, now time.Time, stale *bool) ([]instanceRun, []string) {
	keys := make([]string, 0)
	grouped := make(map[string][]model.BotSnapshot, len(bots))
	for _, entry := range bots {
		key := dungeonRunKey(entry.snap)
		if key == "" {
			continue
		}
		if entry.snap.Instance == nil {
			*stale = true
		}
		if _, seen := grouped[key]; !seen {
			keys = append(keys, key)
		}
		grouped[key] = append(grouped[key], entry.snap)
	}
	sort.Strings(keys)

	mobRuns := make([]model.DungeonRun, 0, len(keys))
	for _, key := range keys {
		members := grouped[key]
		run := model.DungeonRun{Key: key, Members: make([]model.DungeonMember, 0, len(members))}
		// Every member of a run is on the same map; the instance id comes
		// off the first bot's snapshot (nil = pre-v12 module).
		run.MapID = members[0].MapID
		if members[0].Instance != nil {
			run.Instance = *members[0].Instance
		}
		for _, m := range members {
			dm := dungeonMember(m)
			if dm.Player {
				run.Players++
			} else {
				run.Bots++
			}
			if dm.Dead {
				run.Dead++
			}
			run.Members = append(run.Members, dm)
		}
		if run.Bots == 0 {
			continue // a player alone in an instance is no run
		}
		if first, ok := s.dungeonSeen[key]; ok {
			run.FirstSeen = first.Unix()
			run.SeenSec = now.Sub(first).Seconds()
		}
		sortRunMembers(run.Members)
		mobRuns = append(mobRuns, run)
	}

	// Match the mob entries onto the runs only now that every run is known:
	// the roster may report no instance (a module base without #895), where
	// the match needs the members, and ambiguity is judged per entry.
	matched := matchDungeonMobs(mobRuns, mobs)
	raid := make([]bool, len(mobRuns))
	for i, e := range matched {
		joinDungeonMobs(&mobRuns[i], e)
		raid[i] = e.run.Raid
	}
	out := make([]instanceRun, 0, len(mobRuns))
	for i, r := range mobRuns {
		// The raid flag may ride the roster rows instead of (or as well as)
		// the mob entry: either source marks a raid run.
		if !raid[i] {
			for _, m := range grouped[r.Key] {
				if m.Raid {
					raid[i] = true
					break
				}
			}
		}
		out = append(out, instanceRun{run: r, raid: raid[i]})
	}
	return out, keys
}

// Dungeons builds the view behind GET /api/v1/dungeons: the current
// non-raid runs with their members and first-seen ages. Raid copies leave
// for /api/v1/raids (the Raids tab).
func (s *Store) Dungeons() model.DungeonsView {
	// Copy the fresh mob entries first: the mobs mutex is never taken while
	// the roster lock is held.
	mobs := s.freshDungeonMobs()

	s.mu.RLock()
	defer s.mu.RUnlock()

	now := s.now()
	staleModule := false
	runs, _ := s.instanceRunsLocked(s.bots, mobs, now, &staleModule)

	out := make([]model.DungeonRun, 0, len(runs))
	for _, r := range runs {
		if r.raid {
			continue
		}
		out = append(out, r.run)
	}

	return model.DungeonsView{Runs: out, GeneratedAt: now.Unix(), StaleModule: staleModule}
}
