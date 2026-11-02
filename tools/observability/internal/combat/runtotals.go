// Run totals: one run's finished fights summed per bot, for the dashboard's
// "Run total" view of the role tables (GET /api/v1/combat/run-totals).
package combat

import "sort"

// RunTotalsCap bounds how many fights one run-total answer sums.
const RunTotalsCap = 500

// RunInput is one finished fight with its public key (memory and history
// rows de-duplicate by it).
type RunInput struct {
	Key   string
	Fight Fight
}

// TotalBot is one bot's summed numbers in a run total. FightMS is the sum of
// the dur_ms of the fights the bot was in (the client's DPS/HPS divides by
// this per-bot time, not the run's whole fight time). Spells folds the Tank
// table's spell counts: Sunder Armor and the taunts the role tables read.
type TotalBot struct {
	Name    string            `json:"name"`
	GUID    uint32            `json:"guid"`
	Class   string            `json:"class"`
	Role    string            `json:"role"`
	Dmg     uint64            `json:"dmg"`
	Heal    uint64            `json:"heal"`
	Over    uint64            `json:"over"`
	Taken   uint64            `json:"taken"`
	Casts   uint32            `json:"casts"`
	Deaths  uint32            `json:"deaths"`
	Spells  map[string]uint32 `json:"spells"`
	FightMS uint64            `json:"fight_ms"`
	Fights  int               `json:"fights"`
}

// RunTotals is the answer of GET /api/v1/combat/run-totals: the asked since
// (unix seconds, the domain clock), how many fights were summed, their summed
// fight time, and one row per bot.
type RunTotals struct {
	Since  int64      `json:"since"`
	Fights int        `json:"fights"`
	DurMS  uint64     `json:"dur_ms"`
	Bots   []TotalBot `json:"bots"`
}

// SumRun sums finished fights per bot. Since is unix seconds, the domain
// clock (Fight.Start, ListQuery.Since). The rows are sorted newest first
// (start descending, then key ascending), so past the cap the oldest are
// dropped no matter what order the caller passed. A fight seen twice under
// the same key is summed once. A fight still running (outcome "live") or
// filed unfinished carries no per-bot numbers and is not summed. More
// reports the cap cut fights. The newest fight's name, class and role win
// when a guid reappears.
func SumRun(in []RunInput, since int64, cap int) (RunTotals, bool) {
	if cap <= 0 || cap > RunTotalsCap {
		cap = RunTotalsCap
	}
	rows := append([]RunInput(nil), in...)
	sort.Slice(rows, func(i, j int) bool {
		if rows[i].Fight.Start != rows[j].Fight.Start {
			return rows[i].Fight.Start > rows[j].Fight.Start
		}
		return rows[i].Key < rows[j].Key
	})
	more := false
	out := RunTotals{Since: since, Bots: []TotalBot{}}
	seen := map[string]bool{}
	byGUID := map[uint32]int{}
	count := 0
	for _, r := range rows {
		if r.Fight.Start < since {
			continue
		}
		if r.Fight.Outcome == "live" || r.Fight.Outcome == "unfinished" {
			continue
		}
		if seen[r.Key] {
			continue
		}
		seen[r.Key] = true
		if count >= cap {
			more = true
			continue
		}
		count++
		out.Fights++
		out.DurMS += uint64(r.Fight.DurMS)
		for _, b := range r.Fight.Bots {
			i, ok := byGUID[b.GUID]
			if !ok {
				i = len(out.Bots)
				byGUID[b.GUID] = i
				out.Bots = append(out.Bots, TotalBot{GUID: b.GUID, Spells: map[string]uint32{}})
				out.Bots[i].Name = b.Name
				out.Bots[i].Class = b.Class
				out.Bots[i].Role = b.Role
			}
			t := &out.Bots[i]
			t.Dmg += b.Dmg
			t.Heal += b.Heal
			t.Over += b.Over
			t.Taken += b.Taken
			t.Casts += b.Casts
			t.Deaths += b.Deaths
			t.FightMS += uint64(r.Fight.DurMS)
			t.Fights++
			for _, sp := range b.Spells {
				if sp.N == "Sunder Armor" || IsTaunt(sp.N) {
					t.Spells[sp.N] += sp.C
				}
			}
		}
	}
	return out, more
}

// IsTaunt reports whether a spell name is one of the taunts the Tank table reads.
func IsTaunt(name string) bool {
	switch name {
	case "Taunt", "Growl", "Hand of Reckoning", "Mocking Blow", "Challenging Shout", "Challenging Roar":
		return true
	}
	return false
}
