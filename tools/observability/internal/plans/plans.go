// Package plans reads the run leader's dungeon and raid plans (the
// ai_dungeon_plan / ai_dungeon_leg / ai_dungeon_step rows) from the world
// database and turns one into what the Commanders map's plan layer draws:
// the kept steps with their stands, the pack mobs (creature spawns matched to
// the pull/boss step that names their entry), the scripted wake rings of
// wake.json and the truncated flags. The module reads the same rows once at
// startup; the dashboard reads them read-only and caches the result.
package plans

import (
	"context"
	"database/sql"
	_ "embed"
	"encoding/json"
	"errors"
	"fmt"
	"log"
	"math"
	"net/http"
	"regexp"
	"sort"
	"strconv"
	"strings"
	"sync"
	"time"

	_ "github.com/go-sql-driver/mysql"
)

// PlanMobSlackYd widens a step's radius when a spawn is matched to it: a pack
// mob may stand slightly outside the row's own radius.
const PlanMobSlackYd = 5.0

// Default radii of the module for rows without one.
const (
	PlanDefaultPullYd = 15.0
	PlanDefaultBossYd = 30.0
)

const (
	PlanMaxEntries = 64
	PlanMaxSpawns  = 400
	PlanCacheTTL   = 10 * time.Minute
	PlanMissTTL    = time.Minute
	PlanErrorTTL   = 10 * time.Second
	PlanCacheMax   = 64

	creatureNoAggro = 0x2
	waypointMotion  = 2
)

// ErrNotFound means the world database has no plan row with that name (as
// opposed to the database being unavailable).
var ErrNotFound = errors.New("no such plan")

// Wake is one scripted wake: the radius at which a boss or an area trigger
// notices the party, and where the number comes from.
type Wake struct {
	Yd  float64 `json:"yd"`
	Why string  `json:"why"`
}

//go:embed wake.json
var wakeJSON []byte

// wakes is wake.json by creature entry. A unit that wakes by script or area
// trigger instead of its aggro radius belongs here: add a row with its entry,
// the radius in yards and the source of the number.
var wakes = loadWakes(wakeJSON)

// loadWakes decodes the wake table: decimal entries with a positive radius.
// A broken file leaves no wakes (the map then draws aggro rings only) and
// says so in the log.
func loadWakes(data []byte) map[uint32]Wake {
	out := make(map[uint32]Wake)
	var raw map[string]Wake
	if err := json.Unmarshal(data, &raw); err != nil {
		log.Printf("[Plans] wake.json ignored: %v", err)
		return out
	}
	for key, w := range raw {
		entry, err := strconv.ParseUint(key, 10, 32)
		if err != nil || entry == 0 || !(w.Yd > 0) {
			continue
		}
		out[uint32(entry)] = w
	}
	return out
}

func wakeOf(entry uint32) float64 {
	if w, ok := wakes[entry]; ok {
		return w.Yd
	}
	return 0
}

var nameRe = regexp.MustCompile("^[a-z0-9_]{1,32}$")

// Rows as the world database stores them. The JSON tags are the test
// fixture's keys, so a fixture decodes straight into these types.
type PlanRow struct {
	Plan    string `json:"plan"`
	Map     uint32 `json:"map"`
	Source  string `json:"source"`
	Comment string `json:"comment"`
}

type LegRow struct {
	Leg  uint8  `json:"leg"`
	Name string `json:"name"`
}

type StepRow struct {
	Leg     uint8   `json:"leg"`
	Seq     uint16  `json:"seq"`
	Kind    string  `json:"kind"`
	Entry   uint32  `json:"entry"`
	X       float64 `json:"x"`
	Y       float64 `json:"y"`
	Z       float64 `json:"z"`
	Radius  float64 `json:"radius"`
	Entries string  `json:"entries"`
	Comment string  `json:"comment"`
}

type SpawnRow struct {
	GUID         uint32  `json:"guid"`
	ID           uint32  `json:"id"`
	X            float64 `json:"x"`
	Y            float64 `json:"y"`
	Z            float64 `json:"z"`
	MovementType uint32  `json:"movement_type"`
}

type TemplateRow struct {
	Entry      uint32  `json:"entry"`
	Name       string  `json:"name"`
	LevelMax   uint32  `json:"level_max"`
	Detection  float64 `json:"detection_range"`
	Rank       uint32  `json:"rank"`
	FlagsExtra uint32  `json:"flags_extra"`
}

// Source is the read side of the world database the builder needs. All
// queries filter one plan (or a set of creature entries) with ? arguments.
type Source interface {
	Plan(ctx context.Context, name string) (PlanRow, bool, error)
	Legs(ctx context.Context, name string) ([]LegRow, error)
	Steps(ctx context.Context, name string) ([]StepRow, error) // ORDER BY leg, seq
	Templates(ctx context.Context, entries []uint32) ([]TemplateRow, error)
	Spawns(ctx context.Context, mapID uint32, entries []uint32) ([]SpawnRow, error)
}

// Wire types of GET /api/v1/plans/{plan}.
type Plan struct {
	Plan       string `json:"plan"`
	Map        uint32 `json:"map"`
	Source     string `json:"source"`
	Comment    string `json:"comment"`
	StepsTotal int    `json:"steps_total"`
	Legs       []Leg  `json:"legs"`
	Steps      []Step `json:"steps"`
	Mobs       []Mob  `json:"mobs"`
	Truncated  bool   `json:"truncated,omitempty"`
}

type Leg struct {
	Leg  uint8  `json:"leg"`
	Name string `json:"name"`
}

type Step struct {
	I       int      `json:"i"`
	Leg     uint8    `json:"leg"`
	Seq     uint16   `json:"seq"`
	K       string   `json:"k"`
	X       float64  `json:"x"`
	Y       float64  `json:"y"`
	Z       float64  `json:"z"`
	R       float64  `json:"r"`
	E       uint32   `json:"e,omitempty"`
	Entries []uint32 `json:"entries,omitempty"`
	N       string   `json:"n,omitempty"`
	C       string   `json:"c,omitempty"`
	Stand   bool     `json:"stand,omitempty"`
	WakeYd  float64  `json:"wake_yd,omitempty"`
}

type Mob struct {
	Step    int     `json:"step"`
	GUID    uint32  `json:"guid"`
	E       uint32  `json:"e,omitempty"`
	N       string  `json:"n,omitempty"`
	X       float64 `json:"x"`
	Y       float64 `json:"y"`
	Z       float64 `json:"z"`
	Lvl     uint32  `json:"lvl"`
	Det     float64 `json:"det"`
	Rank    uint32  `json:"rank"`
	Patrol  bool    `json:"patrol,omitempty"`
	NoAggro bool    `json:"noaggro,omitempty"`
	WakeYd  float64 `json:"wake_yd,omitempty"`
	// The patrol paths of the optional folder (patrols.go): the loop in
	// yards, its round and where the numbers came from.
	Path    [][3]float64 `json:"path,omitempty"`
	PeriodS float64      `json:"period_s,omitempty"`
	PathEst bool         `json:"path_est,omitempty"`
	PathSrc string       `json:"path_src,omitempty"`
}

// Queries, exported so a test can pin their shape. Arguments go through ?;
// the only text ever spliced in is inMarks' placeholder list.
const (
	QueryPlan      = "SELECT `plan`, `map`, `source`, `comment` FROM `ai_dungeon_plan` WHERE `plan` = ?"
	QueryLegs      = "SELECT `leg`, `name` FROM `ai_dungeon_leg` WHERE `plan` = ? ORDER BY `leg`"
	QuerySteps     = "SELECT `leg`, `seq`, `kind`, `entry`, `x`, `y`, `z`, `radius`, `entries`, `comment` FROM `ai_dungeon_step` WHERE `plan` = ? ORDER BY `leg`, `seq`"
	QueryTemplates = "SELECT `entry`, `name`, `level_max`, `detection_range`, `rank`, `flags_extra` FROM `creature_template` WHERE `entry` IN (%s)"
	QuerySpawns    = "SELECT `guid`, `id`, `position_x`, `position_y`, `position_z`, `movement_type` FROM `creature` WHERE `map` = ? AND `id` IN (%s) LIMIT 400"
)

// kinds are the step kinds the module parses; any other row is dropped, as
// AddStep drops it at startup.
var kinds = map[string]bool{
	"move": true, "move_direct": true, "move_hop_exact": true, "pull": true, "boss": true,
	"loot": true, "open": true, "open_proof": true, "use_item": true, "wait_object": true,
	"wait_clear": true, "rest": true, "ritual": true, "hold": true, "gossip": true, "use_on": true,
}

// inMarks returns n comma-separated placeholders, never empty.
func inMarks(n int) string {
	if n < 1 {
		return ""
	}
	return strings.Repeat("?,", n-1) + "?"
}

// parseEntries parses a row's entries column exactly as the module does: ""
// is no list, otherwise comma-separated decimal tokens, and any token that is
// not 1-10 digits with a value > 0 drops the whole row.
func parseEntries(s string) ([]uint32, bool) {
	if s == "" {
		return nil, true
	}
	var out []uint32
	for _, tok := range strings.Split(s, ",") {
		tok = strings.TrimSpace(tok)
		if len(tok) == 0 || len(tok) > 10 {
			return nil, false
		}
		n, err := strconv.ParseUint(tok, 10, 32)
		if err != nil || n == 0 {
			return nil, false
		}
		out = append(out, uint32(n))
	}
	return out, true
}

type keptStep struct {
	i       int
	row     StepRow
	entries []uint32
	radius  float64
	stand   bool
}

func (k *keptStep) holds(entry uint32) bool {
	if k.row.Kind == "boss" && k.row.Entry == entry {
		return true
	}
	for _, e := range k.entries {
		if e == entry {
			return true
		}
	}
	return false
}

func effectiveRadius(kind string, radius float64) float64 {
	switch kind {
	case "pull":
		if radius > 0 {
			return radius
		}
		return PlanDefaultPullYd
	case "boss":
		if radius > 0 {
			return radius
		}
		return PlanDefaultBossYd
	case "wait_clear":
		return radius
	}
	return 0
}

func dist3(ax, ay, az, bx, by, bz float64) float64 {
	dx, dy, dz := ax-bx, ay-by, az-bz
	return math.Sqrt(dx*dx + dy*dy + dz*dz)
}

func round2(v float64) float64 { return math.Round(v*100) / 100 }

// Build turns one plan's rows into the wire shape. Rows the module refuses
// (unknown kind, bad entries list) are dropped and never numbered. Mobs are
// matched by 3D distance to the pull/boss step that names their entry.
func Build(ctx context.Context, src Source, name string) (*Plan, error) {
	row, found, err := src.Plan(ctx, name)
	if err != nil {
		return nil, err
	}
	if !found {
		return nil, ErrNotFound
	}
	legs, err := src.Legs(ctx, name)
	if err != nil {
		return nil, err
	}
	rows, err := src.Steps(ctx, name)
	if err != nil {
		return nil, err
	}

	p := &Plan{
		Plan:    name,
		Map:     row.Map,
		Source:  row.Source,
		Comment: row.Comment,
		Legs:    make([]Leg, 0, len(legs)),
		Steps:   make([]Step, 0, len(rows)),
		Mobs:    []Mob{},
	}
	for _, l := range legs {
		p.Legs = append(p.Legs, Leg{Leg: l.Leg, Name: l.Name})
	}

	kept := make([]keptStep, 0, len(rows))
	for _, r := range rows {
		if !kinds[r.Kind] {
			continue
		}
		entries, ok := parseEntries(r.Entries)
		if !ok {
			continue
		}
		kept = append(kept, keptStep{row: r, entries: entries, radius: effectiveRadius(r.Kind, r.Radius)})
	}
	for i := range kept {
		kept[i].i = i + 1
		if i+1 < len(kept) {
			next := kept[i+1].row
			kept[i].stand = next.Leg == kept[i].row.Leg && (next.Kind == "pull" || next.Kind == "boss")
		}
	}

	// Entry set for the two lookups: pull entries, boss entry + entries,
	// distinct in step order, capped.
	var set []uint32
	seen := make(map[uint32]bool, PlanMaxEntries)
	add := func(e uint32) {
		if e == 0 || seen[e] {
			return
		}
		seen[e] = true
		set = append(set, e)
	}
	for _, k := range kept {
		switch k.row.Kind {
		case "pull":
			for _, e := range k.entries {
				add(e)
			}
		case "boss":
			add(k.row.Entry)
			for _, e := range k.entries {
				add(e)
			}
		}
	}
	if len(set) > PlanMaxEntries {
		set = set[:PlanMaxEntries]
		p.Truncated = true
	}

	templates := map[uint32]TemplateRow{}
	if len(set) > 0 {
		tmplRows, err := src.Templates(ctx, set)
		if err != nil {
			return nil, err
		}
		for _, t := range tmplRows {
			templates[t.Entry] = t
		}
		spawns, err := src.Spawns(ctx, row.Map, set)
		if err != nil {
			return nil, err
		}
		if len(spawns) >= PlanMaxSpawns {
			p.Truncated = true
		}
		p.Mobs = matchMobs(kept, spawns, templates)
		attachPatrols(p.Mobs)
	}

	for i := range kept {
		k := &kept[i]
		out := Step{
			I:       i + 1,
			Leg:     k.row.Leg,
			Seq:     k.row.Seq,
			K:       k.row.Kind,
			X:       round2(k.row.X),
			Y:       round2(k.row.Y),
			Z:       round2(k.row.Z),
			R:       round2(k.radius),
			E:       k.row.Entry,
			Entries: k.entries,
			C:       k.row.Comment,
			Stand:   k.stand,
		}
		if len(k.entries) == 0 {
			out.Entries = nil
		}
		switch k.row.Kind {
		case "boss":
			out.N = templates[k.row.Entry].Name
			out.WakeYd = wakeOf(k.row.Entry)
		case "pull":
			var names []string
			have := make(map[string]bool, len(k.entries))
			for _, e := range k.entries {
				n := templates[e].Name
				if n == "" || have[n] {
					continue
				}
				have[n] = true
				names = append(names, n)
			}
			out.N = strings.Join(names, ", ")
		}
		p.Steps = append(p.Steps, out)
	}
	p.StepsTotal = len(p.Steps)
	return p, nil
}

func matchMobs(kept []keptStep, spawns []SpawnRow, templates map[uint32]TemplateRow) []Mob {
	var packs []*keptStep
	for i := range kept {
		if k := kept[i].row.Kind; k == "pull" || k == "boss" {
			packs = append(packs, &kept[i])
		}
	}
	mobs := make([]Mob, 0, len(spawns))
	for _, s := range spawns {
		best := -1
		bestDist := 0.0
		for pi, k := range packs {
			if !k.holds(s.ID) {
				continue
			}
			d := dist3(s.X, s.Y, s.Z, k.row.X, k.row.Y, k.row.Z)
			if d > k.radius+PlanMobSlackYd {
				continue
			}
			// packs are in step order, so a tie keeps the lower step.
			if best < 0 || d < bestDist {
				best, bestDist = pi, d
			}
		}
		if best < 0 {
			continue
		}
		t := templates[s.ID]
		m := Mob{
			Step:    packs[best].i,
			GUID:    s.GUID,
			E:       s.ID,
			N:       t.Name,
			X:       round2(s.X),
			Y:       round2(s.Y),
			Z:       round2(s.Z),
			Lvl:     t.LevelMax,
			Det:     20,
			Rank:    t.Rank,
			Patrol:  s.MovementType == waypointMotion,
			NoAggro: t.FlagsExtra&creatureNoAggro != 0,
			WakeYd:  wakeOf(s.ID),
		}
		if t.Entry != 0 {
			m.Det = round2(t.Detection)
		}
		mobs = append(mobs, m)
	}
	sort.SliceStable(mobs, func(i, j int) bool {
		if mobs[i].Step != mobs[j].Step {
			return mobs[i].Step < mobs[j].Step
		}
		return mobs[i].GUID < mobs[j].GUID
	})
	return mobs
}

// NewSQLSource reads the world database.
func NewSQLSource(db *sql.DB) Source { return &sqlSource{db: db} }

type sqlSource struct{ db *sql.DB }

func (s *sqlSource) Plan(ctx context.Context, name string) (PlanRow, bool, error) {
	var r PlanRow
	var source, comment sql.NullString
	err := s.db.QueryRowContext(ctx, QueryPlan, name).Scan(&r.Plan, &r.Map, &source, &comment)
	if errors.Is(err, sql.ErrNoRows) {
		return PlanRow{}, false, nil
	}
	if err != nil {
		return PlanRow{}, false, err
	}
	r.Source, r.Comment = source.String, comment.String
	return r, true, nil
}

func (s *sqlSource) Legs(ctx context.Context, name string) ([]LegRow, error) {
	rows, err := s.db.QueryContext(ctx, QueryLegs, name)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	var out []LegRow
	for rows.Next() {
		var r LegRow
		if err := rows.Scan(&r.Leg, &r.Name); err != nil {
			return nil, err
		}
		out = append(out, r)
	}
	return out, rows.Err()
}

func (s *sqlSource) Steps(ctx context.Context, name string) ([]StepRow, error) {
	rows, err := s.db.QueryContext(ctx, QuerySteps, name)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	var out []StepRow
	for rows.Next() {
		var r StepRow
		var entries, comment sql.NullString
		if err := rows.Scan(&r.Leg, &r.Seq, &r.Kind, &r.Entry, &r.X, &r.Y, &r.Z, &r.Radius, &entries, &comment); err != nil {
			return nil, err
		}
		r.Entries, r.Comment = entries.String, comment.String
		out = append(out, r)
	}
	return out, rows.Err()
}

func (s *sqlSource) Templates(ctx context.Context, entries []uint32) ([]TemplateRow, error) {
	if len(entries) == 0 {
		return nil, nil
	}
	args := make([]any, len(entries))
	for i, e := range entries {
		args[i] = e
	}
	rows, err := s.db.QueryContext(ctx, fmt.Sprintf(QueryTemplates, inMarks(len(entries))), args...)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	var out []TemplateRow
	for rows.Next() {
		var r TemplateRow
		var name sql.NullString
		if err := rows.Scan(&r.Entry, &name, &r.LevelMax, &r.Detection, &r.Rank, &r.FlagsExtra); err != nil {
			return nil, err
		}
		r.Name = name.String
		out = append(out, r)
	}
	return out, rows.Err()
}

func (s *sqlSource) Spawns(ctx context.Context, mapID uint32, entries []uint32) ([]SpawnRow, error) {
	if len(entries) == 0 {
		return nil, nil
	}
	args := make([]any, 0, len(entries)+1)
	args = append(args, mapID)
	for _, e := range entries {
		args = append(args, e)
	}
	rows, err := s.db.QueryContext(ctx, fmt.Sprintf(QuerySpawns, inMarks(len(entries))), args...)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	var out []SpawnRow
	for rows.Next() {
		var r SpawnRow
		if err := rows.Scan(&r.GUID, &r.ID, &r.X, &r.Y, &r.Z, &r.MovementType); err != nil {
			return nil, err
		}
		out = append(out, r)
	}
	return out, rows.Err()
}

// OpenWorld opens the small pool the plan reads use on the world database. The driver's own read and write timeouts bound a
// query the server stops answering, so a stuck read cannot outlive its caller's patience (the fight history's writer closes on
// a deadline).
func OpenWorld(host string, port int, user, pass, db string) (*sql.DB, error) {
	dsn := fmt.Sprintf("%s:%s@tcp(%s:%d)/%s?timeout=5s&readTimeout=10s&writeTimeout=10s&parseTime=false", user, pass, host, port, db)
	conn, err := sql.Open("mysql", dsn)
	if err != nil {
		return nil, fmt.Errorf("open world database: %w", err)
	}
	conn.SetMaxOpenConns(2)
	conn.SetMaxIdleConns(1)
	conn.SetConnMaxLifetime(5 * time.Minute)
	return conn, nil
}

// Service caches built plans, so repeated map draws do not re-read the world
// database.
type Service struct {
	src      Source
	mu       sync.Mutex
	now      func() time.Time
	seq      uint64
	byID     map[string]*cacheEntry
	inflight map[string]*buildWaiter
}

type cacheEntry struct {
	plan    *Plan
	err     error
	expires time.Time
	at      uint64
}

// buildWaiter is the one build of a plan in flight; its callers wait on done for the same answer.
type buildWaiter struct {
	plan *Plan
	err  error
	done chan struct{}
}

func NewService(src Source) *Service {
	return &Service{src: src, now: time.Now, byID: make(map[string]*cacheEntry, PlanCacheMax), inflight: make(map[string]*buildWaiter)}
}

// SetClock replaces the clock (tests only).
func (s *Service) SetClock(now func() time.Time) {
	if now == nil {
		now = time.Now
	}
	s.mu.Lock()
	s.now = now
	s.mu.Unlock()
}

// Get answers from the cache, else builds the plan outside the lock (a slow world database read must not block the other
// plans), one build per name. A failure is kept for PlanErrorTTL and a miss for PlanMissTTL, so a page drawing maps at 30 fps
// does not query a broken or empty database on every draw. ctx cancels the wait and the build's own reads, so a caller that
// stops needing the plan (a closed request, a shutting-down writer) is not held by a slow database.
func (s *Service) Get(ctx context.Context, name string) (*Plan, error) {
	return s.get(ctx, name, false)
}

// get is Get with the one-retry flag: a waiter that took the build leader's own cancellation asks again once, as long as its own
// ctx is still live.
func (s *Service) get(ctx context.Context, name string, retried bool) (*Plan, error) {
	s.mu.Lock()
	// The paths are built into every plan, so a changed patrol file drops the
	// whole cache; the check itself is at most once a minute.
	if patrolsState.refresh(s.now()) {
		s.byID = make(map[string]*cacheEntry, PlanCacheMax)
	}
	if c, ok := s.byID[name]; ok && s.now().Before(c.expires) {
		p, err := c.plan, c.err
		s.mu.Unlock()
		return p, err
	}
	if w, ok := s.inflight[name]; ok {
		s.mu.Unlock()
		select {
		case <-w.done:
		case <-ctx.Done():
			return nil, ctx.Err()
		}
		if !retried && canceled(w.err) && ctx.Err() == nil {
			return s.get(ctx, name, true)
		}
		if w.plan == nil && w.err == nil {
			return nil, errBuildAborted
		}
		return w.plan, w.err
	}
	if err := ctx.Err(); err != nil {
		s.mu.Unlock()
		return nil, err
	}
	w := &buildWaiter{done: make(chan struct{})}
	s.inflight[name] = w
	s.mu.Unlock()

	var p *Plan
	var err error
	func() {
		// The cleanup is deferred so a panicking build still clears the in-flight entry and releases the callers waiting on it.
		defer func() {
			s.mu.Lock()
			w.plan, w.err = p, err
			delete(s.inflight, name)
			now := s.now() // the build's own time, so a slow read does not shorten the entry's life
			switch {
			case err == nil && p != nil:
				s.put(name, &cacheEntry{plan: p, expires: now.Add(PlanCacheTTL)})
			case errors.Is(err, ErrNotFound):
				s.put(name, &cacheEntry{err: ErrNotFound, expires: now.Add(PlanMissTTL)})
			case err != nil && !canceled(err):
				s.put(name, &cacheEntry{err: err, expires: now.Add(PlanErrorTTL)})
			}
			s.mu.Unlock()
			close(w.done)
		}()
		p, err = Build(ctx, s.src, name)
	}()
	return p, err
}

// canceled reports a read the caller stopped waiting for: it says nothing about the world database, so it is not cached.
func canceled(err error) bool {
	return errors.Is(err, context.Canceled) || errors.Is(err, context.DeadlineExceeded)
}

var errBuildAborted = errors.New("plan build aborted")

func (s *Service) put(name string, c *cacheEntry) {
	s.seq++
	c.at = s.seq
	s.byID[name] = c
	for len(s.byID) > PlanCacheMax {
		oldestName := ""
		var oldest uint64
		for n, e := range s.byID {
			if oldestName == "" || e.at < oldest {
				oldestName, oldest = n, e.at
			}
		}
		delete(s.byID, oldestName)
	}
}

// Handler serves GET /api/v1/plans/{plan}: 400 for a malformed name, 404 for
// an unknown plan, 503 when the world database is missing or failing.
func Handler(s *Service) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		name := r.PathValue("plan")
		if !nameRe.MatchString(name) {
			w.WriteHeader(http.StatusBadRequest)
			_, _ = w.Write([]byte(`{"error":"bad plan name"}`))
			return
		}
		if s == nil {
			w.WriteHeader(http.StatusServiceUnavailable)
			_, _ = w.Write([]byte(`{"error":"world database unavailable"}`))
			return
		}
		p, err := s.Get(r.Context(), name)
		if err != nil {
			if errors.Is(err, ErrNotFound) {
				w.WriteHeader(http.StatusNotFound)
				_, _ = w.Write([]byte(`{"error":"no such plan"}`))
				return
			}
			log.Printf("[Plans] %s: %v", name, err)
			w.WriteHeader(http.StatusServiceUnavailable)
			_, _ = w.Write([]byte(`{"error":"world database unavailable"}`))
			return
		}
		_ = json.NewEncoder(w).Encode(p)
	}
}
