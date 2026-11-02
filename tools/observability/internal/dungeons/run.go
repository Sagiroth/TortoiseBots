// Package dungeons indexes the run summaries the module's run trace recorder writes (`.bot trace`, runtime/RunTrace*,
// docs/server/run-trace.md) and answers the Dungeon runs tab (GET /api/v1/dungeon-runs): success rate, clear times, boss kill times, wipe spots.
//
// It only reads files. It never touches a database and never writes. The summaries are read tolerantly: the recorder on
// feat/dungeon-self-clear writes the run, status, map, duration and counters; the fields tracked in issue #986 (outcome, bosses,
// wipes and deaths with positions, clear_s, driver, build, mode) are used when present and left empty when not, so an older server
// still shows its runs (outcome "unknown") and a newer one fills in the rest.
package dungeons

import (
	"encoding/json"
	"errors"
	"fmt"
	"math"
	"sort"
	"strconv"
	"strings"
	"time"
)

// Outcomes, as issue #986 names them, plus two the dashboard needs for a summary that does not say.
const (
	OutcomeCleared   = "cleared"
	OutcomePartial   = "partial"
	OutcomeWipedOut  = "wiped_out"
	OutcomeStopped   = "stopped"
	OutcomeAbandoned = "abandoned"
	OutcomeRunning   = "running"
	OutcomeUnknown   = "unknown"
)

// DriverUnknown is what a run without a driver field shows (turtle-agent issue #186 adds `driver=model:effort:label`).
const DriverUnknown = "unknown"

// Boss is one boss of the plan. KilledAtS is seconds from the entry, nil when the boss was not reached.
type Boss struct {
	Entry     uint32   `json:"entry,omitempty"`
	Name      string   `json:"name"`
	KilledAtS *float64 `json:"killed_at_s"`
}

// Spot is a wipe or a death: where and when in the run. X and Y are game coordinates (x north, y west), nil when the recorder had none.
type Spot struct {
	T         *float64 `json:"t_s,omitempty"`
	Leg       string   `json:"leg,omitempty"`
	PullGroup string   `json:"pull_group,omitempty"`
	X         *float64 `json:"x,omitempty"`
	Y         *float64 `json:"y,omitempty"`
	Role      string   `json:"role,omitempty"`
}

// Member is one party member of the run.
type Member struct {
	Class string `json:"class,omitempty"`
	Role  string `json:"role,omitempty"`
	Level int    `json:"level,omitempty"`
}

// Driver says what drove the run. It is self-reported (the server never verifies it); Key groups runs, Text is what the page shows.
type Driver struct {
	Model  string `json:"model,omitempty"`
	Effort string `json:"effort,omitempty"`
	Label  string `json:"label,omitempty"`
	Key    string `json:"key"`
	Text   string `json:"text"`
}

// Run is one summary, normalised.
type Run struct {
	Run        string         `json:"run"`
	File       string         `json:"file"`
	Map        uint32         `json:"map"`
	Dungeon    string         `json:"dungeon"`
	Build      string         `json:"build,omitempty"`
	Mode       string         `json:"mode,omitempty"`
	Started    int64          `json:"started"` // unix seconds; the file's mtime when the summary has no start
	Ended      int64          `json:"ended,omitempty"`
	DurationS  float64        `json:"duration_s,omitempty"`
	Status     string         `json:"status,omitempty"`
	Outcome    string         `json:"outcome"`
	StopReason string         `json:"stop_reason,omitempty"`
	Plan       string         `json:"plan,omitempty"`
	Furthest   string         `json:"furthest,omitempty"`
	Bosses     []Boss         `json:"bosses"`
	Wipes      []Spot         `json:"wipes"`
	Deaths     []Spot         `json:"deaths"`
	WipeCount  int            `json:"wipe_count"`
	DeathCount int            `json:"death_count"`
	Party      []Member       `json:"party,omitempty"`
	GMAssists  map[string]int `json:"gm_assists,omitempty"`
	Driver     Driver         `json:"driver"`
	ClearS     *float64       `json:"clear_s"` // entry to the last boss; only for a cleared run
	TraceFile  string         `json:"trace_file,omitempty"`
}

// ErrNotJSON is returned for a file that is not one JSON object.
var ErrNotJSON = errors.New("not a JSON object")

// knownMaps names the instances a run can be on when the summary carries no dungeon name (map ids of the 1.12 client).
var knownMaps = map[uint32]string{
	33: "Shadowfang Keep", 34: "The Stockade", 36: "The Deadmines", 43: "Wailing Caverns", 47: "Razorfen Kraul",
	48: "Blackfathom Deeps", 70: "Uldaman", 90: "Gnomeregan", 109: "Sunken Temple", 129: "Razorfen Downs",
	189: "Scarlet Monastery", 209: "Zul'Farrak", 229: "Blackrock Spire", 230: "Blackrock Depths", 249: "Onyxia's Lair",
	289: "Scholomance", 309: "Zul'Gurub", 329: "Stratholme", 349: "Maraudon", 389: "Ragefire Chasm", 409: "Molten Core",
	429: "Dire Maul", 469: "Blackwing Lair", 509: "Ruins of Ahn'Qiraj", 531: "Temple of Ahn'Qiraj", 533: "Naxxramas",
}

// MapName is the dungeon's name for a map id.
func MapName(id uint32) string {
	if n, ok := knownMaps[id]; ok {
		return n
	}
	return "Map " + strconv.FormatUint(uint64(id), 10)
}

// ---- tolerant field readers ----------------------------------------------------------------------------------------

func rawString(r json.RawMessage) string {
	if len(r) == 0 {
		return ""
	}
	var s string
	if json.Unmarshal(r, &s) == nil {
		return strings.TrimSpace(s)
	}
	var f float64
	if json.Unmarshal(r, &f) == nil && !math.IsNaN(f) && !math.IsInf(f, 0) {
		return strconv.FormatFloat(f, 'f', -1, 64)
	}
	return ""
}

func rawNum(r json.RawMessage) (float64, bool) {
	if len(r) == 0 || string(r) == "null" {
		return 0, false
	}
	var f float64
	if json.Unmarshal(r, &f) == nil {
		if math.IsNaN(f) || math.IsInf(f, 0) {
			return 0, false
		}
		return f, true
	}
	var s string
	if json.Unmarshal(r, &s) == nil {
		if f, err := strconv.ParseFloat(strings.TrimSpace(s), 64); err == nil && !math.IsNaN(f) && !math.IsInf(f, 0) {
			return f, true
		}
	}
	return 0, false
}

func rawObj(r json.RawMessage) map[string]json.RawMessage {
	if len(r) == 0 {
		return nil
	}
	var m map[string]json.RawMessage
	if json.Unmarshal(r, &m) != nil {
		return nil
	}
	return m
}

func rawList(r json.RawMessage) []json.RawMessage {
	if len(r) == 0 {
		return nil
	}
	var l []json.RawMessage
	if json.Unmarshal(r, &l) != nil {
		return nil
	}
	return l
}

func numPtr(r json.RawMessage) *float64 {
	if f, ok := rawNum(r); ok {
		return &f
	}
	return nil
}

// rawTime reads a time as unix seconds: a number (seconds, or milliseconds when huge) or an ISO 8601 string.
// A zone-less string parses as UTC.
func rawTime(r json.RawMessage) int64 {
	if f, ok := rawNum(r); ok {
		if f > 1e11 {
			f /= 1000
		}
		if f < 0 {
			return 0
		}
		return int64(f)
	}
	s := rawString(r)
	if s == "" {
		return 0
	}
	for _, layout := range []string{time.RFC3339Nano, "2006-01-02T15:04:05", "2006-01-02 15:04:05"} {
		if t, err := time.Parse(layout, s); err == nil {
			return t.Unix()
		}
	}
	return 0
}

func clip(s string, n int) string {
	if len(s) <= n {
		return s
	}
	// never cut a multi-byte rune in half
	for n > 0 && s[n]&0xC0 == 0x80 {
		n--
	}
	return s[:n]
}

// ---- parsing -------------------------------------------------------------------------------------------------------

const (
	maxBosses = 40
	maxSpots  = 200
	maxParty  = 40
)

func dungeonOf(raw map[string]json.RawMessage) (uint32, string, bool) {
	var id uint32
	name := ""
	present := false
	if d, ok := raw["dungeon"]; ok && string(d) != "null" {
		present = true
		if o := rawObj(d); o != nil {
			if f, ok := rawNum(o["map"]); ok && f > 0 {
				id = uint32(f)
			} else if f, ok := rawNum(o["id"]); ok && f > 0 {
				id = uint32(f)
			}
			name = rawString(o["name"])
		} else if f, ok := rawNum(d); ok && f > 0 {
			id = uint32(f)
		} else {
			name = rawString(d)
		}
	}
	if id == 0 {
		if f, ok := rawNum(raw["map"]); ok && f > 0 {
			id = uint32(f)
		}
	}
	return id, clip(name, 60), present
}

// shortSha cuts a 40-character git sha to 8; any other value (a version such as "2026-10-16-v2") is kept whole.
func shortSha(s string) string {
	if len(s) == 40 {
		for _, c := range s {
			if !(c >= '0' && c <= '9' || c >= 'a' && c <= 'f' || c >= 'A' && c <= 'F') {
				return clip(s, 60)
			}
		}
		return s[:8]
	}
	return clip(s, 60)
}

// buildOf reads `build`: a string, or {image, module, core}. The module is its version string or sha, core the core's build when given.
func buildOf(raw map[string]json.RawMessage) string {
	if o := rawObj(raw["build"]); o != nil {
		var parts []string
		for _, k := range []string{"image", "tag", "server"} {
			if s := rawString(o[k]); s != "" {
				parts = append(parts, clip(s, 60))
				break
			}
		}
		for _, k := range []string{"module", "sha", "module_sha"} {
			if s := rawString(o[k]); s != "" {
				parts = append(parts, shortSha(s))
				break
			}
		}
		if s := rawString(o["core"]); s != "" {
			parts = append(parts, "core "+shortSha(s))
		}
		return clip(strings.Join(parts, " / "), 100)
	}
	if s := rawString(raw["build"]); s != "" {
		return clip(s, 100)
	}
	return clip(rawString(raw["tb_version"]), 60)
}

func planOf(r json.RawMessage) string {
	if o := rawObj(r); o != nil {
		name := rawString(o["name"])
		if sha := rawString(o["sha"]); sha != "" {
			if name == "" {
				return clip(sha, 8)
			}
			return clip(name, 60) + " @" + clip(sha, 8)
		}
		return clip(name, 60)
	}
	return clip(rawString(r), 60)
}

func furthestOf(r json.RawMessage) string {
	if o := rawObj(r); o != nil {
		var parts []string
		if s := rawString(o["leg"]); s != "" {
			parts = append(parts, "leg "+clip(s, 20))
		}
		if s := rawString(o["pull_group"]); s != "" {
			parts = append(parts, "group "+clip(s, 20))
		}
		return strings.Join(parts, ", ")
	}
	return clip(rawString(r), 80)
}

// ParseDriver reads `driver`: a "model:effort:label" string (what `.bot trace start ... driver=` takes), "human", or an object.
func ParseDriver(r json.RawMessage) Driver {
	d := Driver{}
	if o := rawObj(r); o != nil {
		d.Model = clip(rawString(o["model"]), 40)
		d.Effort = clip(rawString(o["effort"]), 20)
		d.Label = clip(firstNonEmpty(rawString(o["label"]), rawString(o["run"])), 60)
	} else if s := rawString(r); s != "" {
		parts := strings.SplitN(s, ":", 3)
		d.Model = clip(strings.TrimSpace(parts[0]), 40)
		if len(parts) > 1 {
			d.Effort = clip(strings.TrimSpace(parts[1]), 20)
		}
		if len(parts) > 2 {
			d.Label = clip(strings.TrimSpace(parts[2]), 60)
		}
	}
	switch {
	case d.Model == "":
		d.Key, d.Text = DriverUnknown, DriverUnknown
	case d.Effort == "":
		d.Key, d.Text = d.Model, d.Model
	default:
		d.Key = d.Model + " · " + d.Effort
		d.Text = d.Key
	}
	return d
}

func firstNonEmpty(ss ...string) string {
	for _, s := range ss {
		if s != "" {
			return s
		}
	}
	return ""
}

func spotsOf(r json.RawMessage) (spots []Spot, count int) {
	spots = []Spot{}
	if n, ok := rawNum(r); ok && n >= 0 && n < 1e6 {
		return spots, int(n)
	}
	list := rawList(r)
	count = len(list)
	for _, item := range list {
		o := rawObj(item)
		if o == nil {
			continue
		}
		if len(spots) >= maxSpots {
			break
		}
		s := Spot{T: numPtr(o["t_s"]), X: numPtr(o["x"]), Y: numPtr(o["y"]),
			Leg: clip(rawString(o["leg"]), 20), PullGroup: clip(rawString(o["pull_group"]), 20), Role: clip(rawString(o["role"]), 20)}
		spots = append(spots, s)
	}
	return spots, count
}

// Parse reads one summary file. ok is false for a summary that is not a dungeon run (a trace on an open-world map, say). A file
// that is not a JSON object is an error. file is the name inside the trace folder; it supplies the run id when the summary has none.
func Parse(data []byte, file string) (run *Run, ok bool, err error) {
	var raw map[string]json.RawMessage
	if err := json.Unmarshal(data, &raw); err != nil || raw == nil {
		if err == nil {
			err = ErrNotJSON
		}
		return nil, false, fmt.Errorf("%s: %w", file, err)
	}
	r := &Run{File: file, Bosses: []Boss{}, Wipes: []Spot{}, Deaths: []Spot{}}
	r.Run = clip(rawString(raw["run"]), 64)
	if r.Run == "" {
		r.Run = strings.TrimSuffix(file, ".summary.json")
	}
	id, name, hasDungeon := dungeonOf(raw)
	r.Map = id
	r.Dungeon = name
	if r.Dungeon == "" {
		r.Dungeon = MapName(id)
	}
	r.Build = buildOf(raw)
	r.Mode = clip(rawString(raw["mode"]), 20)
	r.Status = clip(rawString(raw["status"]), 20)
	r.StopReason = clip(firstNonEmpty(rawString(raw["stop_reason"]), rawString(raw["end_reason"])), 80)
	r.Plan = planOf(raw["plan"])
	r.Furthest = furthestOf(raw["furthest"])
	r.TraceFile = clip(rawString(raw["trace_file"]), 80)
	r.Driver = ParseDriver(raw["driver"])

	r.Started = firstNonZero(rawTime(raw["started"]), rawTime(raw["start_unix"]))
	r.Ended = firstNonZero(rawTime(raw["ended"]), rawTime(raw["end_unix"]))
	if ms, ok := rawNum(raw["duration_ms"]); ok && ms > 0 {
		r.DurationS = ms / 1000
	} else if r.Started > 0 && r.Ended > r.Started {
		r.DurationS = float64(r.Ended - r.Started)
	}

	for i, b := range rawList(raw["bosses"]) {
		o := rawObj(b)
		if o == nil || i >= maxBosses {
			continue
		}
		boss := Boss{Name: clip(rawString(o["name"]), 60), KilledAtS: numPtr(o["killed_at_s"])}
		if boss.KilledAtS != nil && *boss.KilledAtS < 0 {
			boss.KilledAtS = nil
		}
		if f, ok := rawNum(o["entry"]); ok && f > 0 {
			boss.Entry = uint32(f)
		}
		if boss.Name == "" {
			if boss.Entry == 0 {
				continue
			}
			boss.Name = "Boss " + strconv.FormatUint(uint64(boss.Entry), 10)
		}
		r.Bosses = append(r.Bosses, boss)
	}

	r.Wipes, r.WipeCount = spotsOf(raw["wipes"])
	r.Deaths, r.DeathCount = spotsOf(raw["deaths"])
	if len(raw["deaths"]) == 0 || string(raw["deaths"]) == "null" {
		// the recorder of #927 counts deaths in `counters`
		if c := rawObj(raw["counters"]); c != nil {
			if n, ok := rawNum(c["deaths"]); ok && n >= 0 && n < 1e6 {
				r.DeathCount = int(n)
			}
		}
	}

	for _, m := range rawList(raw["party"]) {
		o := rawObj(m)
		if o == nil || len(r.Party) >= maxParty {
			continue
		}
		mem := Member{Class: clip(rawString(o["class"]), 20), Role: clip(rawString(o["role"]), 20)}
		if lv, ok := rawNum(o["level"]); ok && lv > 0 && lv < 256 {
			mem.Level = int(lv)
		}
		r.Party = append(r.Party, mem)
	}
	if o := rawObj(raw["gm_assists"]); o != nil {
		r.GMAssists = map[string]int{}
		for k, v := range o {
			if n, ok := rawNum(v); ok && n >= 0 && n < 1e6 && len(r.GMAssists) < 16 {
				r.GMAssists[clip(k, 20)] = int(n)
			}
		}
	}

	r.Outcome = outcomeOf(rawString(raw["outcome"]), r)
	clear := numPtr(raw["clear_s"])
	if r.Outcome == OutcomeCleared {
		switch {
		case clear != nil && *clear > 0:
			r.ClearS = clear
		default:
			var last float64
			for _, b := range r.Bosses {
				if b.KilledAtS != nil && *b.KilledAtS > last {
					last = *b.KilledAtS
				}
			}
			if last == 0 {
				last = r.DurationS
			}
			if last > 0 {
				r.ClearS = &last
			}
		}
	}

	// A summary counts as a dungeon run when it says so (dungeon, outcome, bosses) or sits on an instance map we know.
	_, known := knownMaps[r.Map]
	ok = hasDungeon || len(raw["outcome"]) > 0 || len(r.Bosses) > 0 || known
	if r.Map == 0 && !hasDungeon && len(r.Bosses) == 0 {
		ok = false
	}
	return r, ok, nil
}

func firstNonZero(vs ...int64) int64 {
	for _, v := range vs {
		if v != 0 {
			return v
		}
	}
	return 0
}

// outcomeOf decides a run's outcome. A summary still being written (status "running") is running whatever else it says, so a live run
// never counts as a success or a failure. Otherwise the summary's own outcome is taken when it is one of #986's five; without one it is
// worked out from the bosses.
func outcomeOf(given string, r *Run) string {
	if strings.EqualFold(r.Status, "running") {
		return OutcomeRunning
	}
	switch o := strings.ToLower(strings.TrimSpace(given)); o {
	case OutcomeCleared, OutcomePartial, OutcomeWipedOut, OutcomeStopped, OutcomeAbandoned, OutcomeRunning, OutcomeUnknown:
		return o
	}
	if len(r.Bosses) > 0 {
		killed := 0
		for _, b := range r.Bosses {
			if b.KilledAtS != nil {
				killed++
			}
		}
		switch {
		case killed == len(r.Bosses):
			return OutcomeCleared
		case killed > 0:
			return OutcomePartial
		}
	}
	return OutcomeUnknown
}

// SortRuns orders runs newest first (by start, then run id so the order is stable).
func SortRuns(runs []*Run) {
	sort.SliceStable(runs, func(i, j int) bool {
		if runs[i].Started != runs[j].Started {
			return runs[i].Started > runs[j].Started
		}
		return runs[i].Run < runs[j].Run
	})
}
