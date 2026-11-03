// Package lanes ports the fight debugger's non-tank lane rule to the daemon: how long an enemy stood on a living healer or
// damage dealer within reach, summed and in its longest uninterrupted stretch. web/static/js/aggrolanes.js holds the same rule
// for the page; the committed attempt fixture pins both to the same seconds.
package lanes

import (
	"math"

	"tortoise-observability/internal/combat"
	"tortoise-observability/internal/state"
)

// The rule's numbers (fight debugger B4, from three recorded Onyxia attempts): an enemy on a living non-tank within ReachYd for
// 8 s is a warning and 12 s a failure; 1-3 samples are the taunt bounce 2 s frames cannot resolve. Segments last until the
// enemy's next frame, capped at HoldS.
const (
	ReachYd = 30.0
	HoldS   = 3
)

// laneState is one enemy's running lane: its held total and stint so far.
type laneState struct {
	id      string
	boss    bool
	nt      int64
	longest int64
	stint   int64
	heldTo  int64
	held    bool
}

// NonTank grades one fight against its track frames: the non-tank milliseconds and the longest held stint of the worst lane
// (the longest stint; ties go to the boss lane, then the larger total, then the lower id). ok is false without frames: a fight
// outside the kept track has nothing to grade and the history stores null.
func NonTank(frames []state.TrackFrame, f combat.Fight) (ntMs, longestMs int64, ok bool) {
	if len(frames) == 0 {
		return 0, 0, false
	}
	startS := float64(f.Start)
	durMs := float64(f.DurMS)
	endS := startS + durMs/1000

	// One frame before the start (the last one) closes the first segment; nothing after the end is used.
	edge := -1
	for i := range frames {
		if float64(frames[i].T) < startS {
			edge = i
		}
	}
	used := make([]state.TrackFrame, 0, len(frames))
	for i := range frames {
		t := float64(frames[i].T)
		if t <= endS && (t >= startS || i == edge) {
			used = append(used, frames[i])
		}
	}

	// The lanes are the enemies seen engaged inside the fight: on a unit, or below full health.
	engaged := map[string]bool{}
	for i := range used {
		t := float64(used[i].T)
		if t < startS || t > endS {
			continue
		}
		for _, m := range used[i].Mobs {
			if m.ID == "" {
				continue
			}
			if m.VictimBot != 0 || m.VictimOther || m.HP < 100 {
				engaged[m.ID] = true
			}
		}
	}

	roles := make(map[uint32]string, len(f.Bots))
	for _, b := range f.Bots {
		roles[b.GUID] = roleKind(b.Role)
	}

	lanes := map[string]*laneState{}
	var order []string
	for i := range used {
		fr := &used[i]
		nextT := float64(0)
		if i+1 < len(used) {
			nextT = float64(used[i+1].T)
		}
		for _, m := range fr.Mobs {
			if m.ID == "" || !engaged[m.ID] {
				continue
			}
			lane := lanes[m.ID]
			if lane == nil {
				lane = &laneState{id: m.ID}
				lanes[m.ID] = lane
				order = append(order, m.ID)
			}
			if m.Rank == "b" {
				lane.boss = true
			}
			fromMs := math.Max(0, (float64(fr.T)-startS)*1000)
			endT := float64(fr.T) + 2
			if i+1 < len(used) {
				endT = math.Min(nextT, float64(fr.T)+HoldS)
			}
			toMs := math.Min(durMs, (endT-startS)*1000)
			if !(toMs > fromMs) {
				continue
			}
			kind := kindOf(m, roles)
			bad := kind == "healer" || kind == "dps"
			held := bad && heldBy(m, fr)
			ms := int64(toMs) - int64(fromMs)
			if held {
				lane.nt += ms
				if lane.held && lane.heldTo == int64(fromMs) {
					lane.stint += ms
				} else {
					lane.stint = ms
				}
				if lane.stint > lane.longest {
					lane.longest = lane.stint
				}
				lane.heldTo = int64(toMs)
			} else {
				lane.stint = 0
			}
			lane.held = held
		}
	}

	var worst *laneState
	for _, id := range order {
		lane := lanes[id]
		if lane == nil || lane.nt == 0 {
			continue
		}
		if worst == nil || worse(lane, worst) {
			worst = lane
		}
	}
	if worst == nil {
		return 0, 0, true
	}
	return worst.nt, worst.longest, true
}

// worse orders two lanes: the longer held stint wins (the boss lane on a tie, then the larger held total, then the lower id).
func worse(a, b *laneState) bool {
	if a.longest != b.longest {
		return a.longest > b.longest
	}
	if a.boss != b.boss {
		return a.boss
	}
	if a.nt != b.nt {
		return a.nt > b.nt
	}
	return a.id < b.id
}

// roleKind maps a fight bot's role to a lane kind.
func roleKind(role string) string {
	switch role {
	case "tank":
		return "tank"
	case "healer", "heal":
		return "healer"
	case "dps":
		return "dps"
	}
	return "other"
}

// kindOf is the kind of one enemy frame: a kill is dead, then a bound bot's role, then anything else it faces.
func kindOf(m state.TrackMob, roles map[uint32]string) string {
	if m.HP == 0 {
		return "dead"
	}
	if m.VictimBot != 0 {
		if r, ok := roles[m.VictimBot]; ok {
			return r
		}
		return "other"
	}
	if m.VictimOther {
		return "other"
	}
	return "none"
}

// heldBy reports whether the enemy is on a living bot that stands within ReachYd of it in this frame.
func heldBy(m state.TrackMob, fr *state.TrackFrame) bool {
	if m.VictimBot == 0 {
		return false
	}
	for _, b := range fr.Bots {
		if b.GUID != m.VictimBot || !b.Alive {
			continue
		}
		dx := float64(b.X) - float64(m.X)
		dy := float64(b.Y) - float64(m.Y)
		return math.Hypot(dx, dy) <= ReachYd
	}
	return false
}
