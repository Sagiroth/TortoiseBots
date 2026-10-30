// Package bosstactics reads the module's boss rows from the world database
// (ai_boss_tactic, the role seats of ai_boss_stand and the plans'
// kill_first lists) for the dashboard's raid coverage view. The module reads
// the same tables at startup and on `.bot run reloadplans`; the dashboard
// reads them read-only, caches one snapshot and filters it per map, so the
// page shows what the running server's bots have.
package bosstactics

import (
	"context"
	"database/sql"
	"encoding/json"
	"errors"
	"log"
	"net/http"
	"sort"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/go-sql-driver/mysql"
)

const (
	// CacheTTL is how long one read of the three tables is served.
	CacheTTL = 10 * time.Minute
	// ErrorTTL is how long a failed read is kept before it is retried.
	ErrorTTL = 10 * time.Second

	// Row caps of the three queries. Each query asks for one row over its cap:
	// a returned probe row means the table holds more rows than the cap, and
	// marks the snapshot truncated.
	TacticsLimit = 5000
	StandsLimit  = 5000
	PlansLimit   = 1000
	StepsLimit   = 20000

	// SourceWorld says the rows came from the running world database.
	SourceWorld = "world"

	// mysqlNoSuchTable is MySQL's "table doesn't exist": an older world
	// database (or one without the boss module) answers that for a table it
	// never got, and the coverage view treats it as "no rows", not as an
	// error.
	mysqlNoSuchTable = 1146
)

// The queries order and aggregate, so the snapshot needs no per-row work.
// They carry no quoted literal and no map id: the raid map filter goes in at
// mapFilterMarker through placeholders (queryWithMaps), and every other
// filter the page offers is applied to the cached snapshot, never in SQL.
const (
	QueryTactics = "SELECT `map`, `boss_entry`, `kind`, `target_entry`, `spell_id`, `priority`, `param`, `comment` FROM `ai_boss_tactic`/*maps*/ ORDER BY `map`, `boss_entry`, `kind`, `target_entry`, `spell_id` LIMIT 5001"

	QueryStands = "SELECT `map`, `boss_entry`, `window_spell`, `aura`, `role`, `window_signal`, SUM(`slots`) FROM `ai_boss_stand`/*maps*/ GROUP BY `map`, `boss_entry`, `window_spell`, `aura`, `role`, `window_signal` ORDER BY `map`, `boss_entry`, `window_spell`, `role` LIMIT 5001"

	QueryPlanKillFirst = "SELECT `plan`, `map`, `kill_first` FROM `ai_dungeon_plan`/*maps*/ ORDER BY `plan` LIMIT 1001"

	// QueryBossSteps reads every step of the raid maps' plans with the plan's map; the boss steps (kind boss: entry is the
	// boss, entries the adds it kills first) are picked in Go, so the query keeps its no-literal rule.
	QueryBossSteps = "SELECT s.`plan`, p.`map`, s.`kind`, s.`entry`, s.`entries` FROM `ai_dungeon_step` s JOIN `ai_dungeon_plan` p ON p.`plan` = s.`plan`/*maps*/ ORDER BY s.`plan`, s.`leg`, s.`seq` LIMIT 20001"
)

// Tactic is one ai_boss_tactic row as the page reads it.
type Tactic struct {
	Map      uint32  `json:"map"`
	Boss     uint32  `json:"boss"`
	Kind     string  `json:"kind"`
	Target   uint32  `json:"target"`
	Spell    uint32  `json:"spell"`
	Priority uint32  `json:"priority"`
	Param    float64 `json:"param"`
	Comment  string  `json:"comment"`
}

// Stand is the seats of one stand set, aggregated by (map, boss, window
// spell, aura, role, signal).
type Stand struct {
	Map         uint32 `json:"map"`
	Boss        uint32 `json:"boss"`
	WindowSpell uint32 `json:"windowSpell"`
	Aura        uint32 `json:"aura"`
	Role        string `json:"role"`
	Signal      string `json:"signal"`
	Seats       int    `json:"seats"`
}

// PlanKill is one plan's kill_first list, already parsed.
type PlanKill struct {
	Plan    string   `json:"plan"`
	Map     uint32   `json:"map"`
	Entries []uint32 `json:"entries"`
}

// BossKill is one boss step's kill list (the adds the run leader kills first at that boss), already parsed.
type BossKill struct {
	Plan    string   `json:"plan"`
	Map     uint32   `json:"map"`
	Boss    uint32   `json:"boss"`
	Entries []uint32 `json:"entries"`
}

// Snapshot is the whole read: the JSON body of GET /api/v1/boss/tactics with
// no map filter.
type Snapshot struct {
	Tactics       []Tactic   `json:"tactics"`
	Stands        []Stand    `json:"stands"`
	PlanKillFirst []PlanKill `json:"planKillFirst"`
	// BossKillFirst is the kill list of each plan's boss steps: the raid plans give their kill-first adds per boss, the
	// plan-level kill_first stays empty there.
	BossKillFirst []BossKill `json:"bossKillFirst"`
	Source        string     `json:"source"`
	LoadedAt      string     `json:"loadedAt"`
	Truncated     bool       `json:"truncated,omitempty"`
	// TruncatedAt is the row cap that was hit; the page names it in the
	// incomplete-coverage banner.
	TruncatedAt int `json:"truncatedAt,omitempty"`
}

// ForMap answers the snapshot filtered to one raid map. The counts and the
// loaded time are the whole read's.
func (s *Snapshot) ForMap(mapID uint32) *Snapshot {
	out := *s
	out.Tactics = make([]Tactic, 0, len(s.Tactics))
	for _, t := range s.Tactics {
		if t.Map == mapID {
			out.Tactics = append(out.Tactics, t)
		}
	}
	out.Stands = make([]Stand, 0, len(s.Stands))
	for _, st := range s.Stands {
		if st.Map == mapID {
			out.Stands = append(out.Stands, st)
		}
	}
	out.PlanKillFirst = make([]PlanKill, 0, len(s.PlanKillFirst))
	for _, p := range s.PlanKillFirst {
		if p.Map == mapID {
			out.PlanKillFirst = append(out.PlanKillFirst, p)
		}
	}
	out.BossKillFirst = make([]BossKill, 0, len(s.BossKillFirst))
	for _, b := range s.BossKillFirst {
		if b.Map == mapID {
			out.BossKillFirst = append(out.BossKillFirst, b)
		}
	}
	return &out
}

// Source is the read side of the three world tables. The bool on each read is
// true when the table held more rows than its cap; the rows over the cap are
// trimmed before they reach the service.
type Source interface {
	Tactics(ctx context.Context) ([]Tactic, bool, error)
	Stands(ctx context.Context) ([]Stand, bool, error)
	PlanKillFirst(ctx context.Context) ([]PlanKill, bool, error)
	BossKillFirst(ctx context.Context) ([]BossKill, bool, error)
}

// NewSQLSource reads the world database, limited to the raid maps of
// data/boss/raids.csv so the row caps are a guard and not the normal cut.
func NewSQLSource(db *sql.DB) Source {
	maps, err := RaidMaps()
	if err != nil {
		log.Printf("[BossTactics] embedded raids.csv: %v (reading every map)", err)
	}
	return &sqlSource{db: db, maps: maps}
}

type sqlSource struct {
	db   *sql.DB
	maps []uint32
}

func (s *sqlSource) Tactics(ctx context.Context) ([]Tactic, bool, error) {
	query, args := queryWithMaps(QueryTactics, s.maps)
	rows, err := s.db.QueryContext(ctx, query, args...)
	if err != nil {
		return nil, false, err
	}
	defer rows.Close()
	var out []Tactic
	for rows.Next() {
		var r Tactic
		if err := rows.Scan(&r.Map, &r.Boss, &r.Kind, &r.Target, &r.Spell, &r.Priority, &r.Param, &r.Comment); err != nil {
			return nil, false, err
		}
		out = append(out, r)
	}
	if err := rows.Err(); err != nil {
		return nil, false, err
	}
	out, truncated := capRows(out, TacticsLimit)
	return out, truncated, nil
}

func (s *sqlSource) Stands(ctx context.Context) ([]Stand, bool, error) {
	query, args := queryWithMaps(QueryStands, s.maps)
	rows, err := s.db.QueryContext(ctx, query, args...)
	if err != nil {
		return nil, false, err
	}
	defer rows.Close()
	var out []Stand
	for rows.Next() {
		var r Stand
		if err := rows.Scan(&r.Map, &r.Boss, &r.WindowSpell, &r.Aura, &r.Role, &r.Signal, &r.Seats); err != nil {
			return nil, false, err
		}
		out = append(out, r)
	}
	if err := rows.Err(); err != nil {
		return nil, false, err
	}
	out, truncated := capRows(out, StandsLimit)
	return out, truncated, nil
}

func (s *sqlSource) PlanKillFirst(ctx context.Context) ([]PlanKill, bool, error) {
	query, args := queryWithMaps(QueryPlanKillFirst, s.maps)
	rows, err := s.db.QueryContext(ctx, query, args...)
	if err != nil {
		return nil, false, err
	}
	defer rows.Close()
	var raw []planKillRow
	for rows.Next() {
		var r planKillRow
		if err := rows.Scan(&r.Plan, &r.Map, &r.KillFirst); err != nil {
			return nil, false, err
		}
		raw = append(raw, r)
	}
	if err := rows.Err(); err != nil {
		return nil, false, err
	}
	plans, truncated := planKillsWithinCap(raw)
	return plans, truncated, nil
}

func (s *sqlSource) BossKillFirst(ctx context.Context) ([]BossKill, bool, error) {
	query, args := queryWithMaps(QueryBossSteps, s.maps)
	rows, err := s.db.QueryContext(ctx, query, args...)
	if err != nil {
		return nil, false, err
	}
	defer rows.Close()
	var raw []bossStepRow
	for rows.Next() {
		var r bossStepRow
		if err := rows.Scan(&r.Plan, &r.Map, &r.Kind, &r.Entry, &r.Entries); err != nil {
			return nil, false, err
		}
		raw = append(raw, r)
	}
	if err := rows.Err(); err != nil {
		return nil, false, err
	}
	steps, truncated := bossKillsWithinCap(raw)
	return steps, truncated, nil
}

// bossStepRow is one ai_dungeon_step row before the boss steps are picked.
type bossStepRow struct {
	Plan    string
	Map     uint32
	Kind    string
	Entry   uint32
	Entries string
}

// bossKillsWithinCap counts the raw step rows (every kind) before the boss steps are picked, as the plan read does: a cap
// reached only after the filter could look complete.
func bossKillsWithinCap(raw []bossStepRow) ([]BossKill, bool) {
	raw, truncated := capRows(raw, StepsLimit)
	return buildBossKills(raw), truncated
}

// buildBossKills keeps the boss steps with a usable kill list: an empty list gives no row, and a list that does not parse is
// dropped and logged, as the loader drops it.
func buildBossKills(rows []bossStepRow) []BossKill {
	out := make([]BossKill, 0, 64)
	for _, r := range rows {
		if r.Kind != "boss" {
			continue
		}
		entries, ok := ParseKillFirst(r.Entries)
		if !ok {
			log.Printf("[BossTactics] plan %q boss %d: entries %q is not a list of creature entries, dropped", r.Plan, r.Entry, r.Entries)
			continue
		}
		if len(entries) == 0 {
			continue
		}
		out = append(out, BossKill{Plan: r.Plan, Map: r.Map, Boss: r.Entry, Entries: entries})
	}
	return out
}

// planKillsWithinCap counts the raw plan rows before buildPlanKills drops the
// empty and unparsable lists: a cap reached only after the drop can look
// complete when the rows above it were the unusable ones.
func planKillsWithinCap(raw []planKillRow) ([]PlanKill, bool) {
	raw, truncated := capRows(raw, PlansLimit)
	return buildPlanKills(raw), truncated
}

// capRows trims a read to its cap and reports whether the probe row over the
// cap came back.
func capRows[T any](rows []T, limit int) ([]T, bool) {
	if len(rows) <= limit {
		return rows, false
	}
	return rows[:limit], true
}

// planKillRow is one ai_dungeon_plan row before its kill_first list is parsed.
type planKillRow struct {
	Plan      string
	Map       uint32
	KillFirst string
}

// ParseKillFirst splits a plan's kill_first list on commas and keeps the
// decimal entries above zero. ok is false when a token is no decimal entry:
// the loader drops such a plan whole, so the dashboard does not show a list
// the bots would not use. An empty list is not an error; the caller drops the
// plan.
func ParseKillFirst(list string) ([]uint32, bool) {
	if strings.TrimSpace(list) == "" {
		return nil, true
	}
	out := make([]uint32, 0, 8)
	for _, token := range strings.Split(list, ",") {
		token = strings.TrimSpace(token)
		if token == "" {
			continue
		}
		n, err := strconv.ParseUint(token, 10, 32)
		if err != nil {
			return nil, false
		}
		if n == 0 {
			continue
		}
		out = append(out, uint32(n))
	}
	return out, true
}

// buildPlanKills parses the plan rows: an empty list or one that does not
// parse gives no row, and a list that does not parse is logged once per read.
func buildPlanKills(rows []planKillRow) []PlanKill {
	out := make([]PlanKill, 0, len(rows))
	for _, r := range rows {
		entries, ok := ParseKillFirst(r.KillFirst)
		if !ok {
			log.Printf("[BossTactics] plan %q: kill_first %q is not a list of creature entries, dropped", r.Plan, r.KillFirst)
			continue
		}
		if len(entries) == 0 {
			continue
		}
		out = append(out, PlanKill{Plan: r.Plan, Map: r.Map, Entries: entries})
	}
	return out
}

// missingTable reports MySQL's "table doesn't exist" (an older world database
// without the boss module): that table reads as empty, never as a failure.
func missingTable(err error) bool {
	var myErr *mysql.MySQLError
	return errors.As(err, &myErr) && myErr.Number == mysqlNoSuchTable
}

// Service caches one snapshot of the three tables: one read per CacheTTL, one
// failed read kept for ErrorTTL, and one read in flight at a time. It holds no
// lock while reading, so a slow world database cannot block a cached answer.
type Service struct {
	src Source

	mu       sync.Mutex
	now      func() time.Time
	snap     *Snapshot
	err      error
	expires  time.Time
	inflight *readWaiter
}

// readWaiter is the one read in flight; callers that arrive meanwhile wait on
// done for its answer.
type readWaiter struct {
	snap *Snapshot
	err  error
	done chan struct{}
}

// NewService answers from src (nil src is unusable; the handler checks the
// service pointer).
func NewService(src Source) *Service {
	return &Service{src: src, now: time.Now}
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

// Get answers from the cache, else reads the three tables outside the lock.
func (s *Service) Get(ctx context.Context) (*Snapshot, error) {
	s.mu.Lock()
	now := s.now()
	if now.Before(s.expires) {
		snap, err := s.snap, s.err
		s.mu.Unlock()
		return snap, err
	}
	if w := s.inflight; w != nil {
		s.mu.Unlock()
		select {
		case <-w.done:
			return w.snap, w.err
		case <-ctx.Done():
			return nil, ctx.Err()
		}
	}
	if err := ctx.Err(); err != nil {
		s.mu.Unlock()
		return nil, err
	}
	w := &readWaiter{done: make(chan struct{})}
	s.inflight = w
	s.mu.Unlock()

	snap, err := s.read(ctx, now)

	s.mu.Lock()
	w.snap, w.err = snap, err
	s.inflight = nil
	done := s.now() // the read's own time, so a slow query does not shorten the entry's life
	switch {
	case err == nil && snap != nil:
		s.snap, s.err, s.expires = snap, nil, done.Add(CacheTTL)
	case err != nil && !canceled(err):
		s.snap, s.err, s.expires = nil, err, done.Add(ErrorTTL)
	}
	s.mu.Unlock()
	close(w.done)
	return snap, err
}

// Refresh drops the cached answer and reads again (the page's Reload button), so a changed tactic table shows at once
// instead of after CacheTTL. Concurrent callers still share the one read in flight.
func (s *Service) Refresh(ctx context.Context) (*Snapshot, error) {
	s.mu.Lock()
	s.expires = time.Time{}
	s.mu.Unlock()
	return s.Get(ctx)
}

// canceled reports a read its caller stopped waiting for: it says nothing
// about the world database, so it is not cached.
func canceled(err error) bool {
	return errors.Is(err, context.Canceled) || errors.Is(err, context.DeadlineExceeded)
}

// read reads and orders the three tables. A table that does not exist is
// empty; any other failure fails the whole snapshot, because a page that
// cannot tell a missing table from a broken database would report every
// ability as unknown.
func (s *Service) read(ctx context.Context, started time.Time) (*Snapshot, error) {
	tactics, tacticsCut, err := s.src.Tactics(ctx)
	if err != nil {
		if !missingTable(err) {
			return nil, err
		}
		tactics, tacticsCut = nil, false
	}
	stands, standsCut, err := s.src.Stands(ctx)
	if err != nil {
		if !missingTable(err) {
			return nil, err
		}
		stands, standsCut = nil, false
	}
	plans, plansCut, err := s.src.PlanKillFirst(ctx)
	if err != nil {
		if !missingTable(err) {
			return nil, err
		}
		plans, plansCut = nil, false
	}
	bossKills, bossCut, err := s.src.BossKillFirst(ctx)
	if err != nil {
		if !missingTable(err) {
			return nil, err
		}
		bossKills, bossCut = nil, false
	}
	truncatedAt := 0
	switch {
	case tacticsCut:
		truncatedAt = TacticsLimit
	case standsCut:
		truncatedAt = StandsLimit
	case plansCut:
		truncatedAt = PlansLimit
	case bossCut:
		truncatedAt = StepsLimit
	}
	return &Snapshot{
		Tactics:       sortedTactics(emptyIfNil(tactics)),
		Stands:        sortedStands(emptyIfNil(stands)),
		PlanKillFirst: sortedPlanKills(emptyIfNil(plans)),
		BossKillFirst: sortedBossKills(emptyIfNil(bossKills)),
		Source:        SourceWorld,
		LoadedAt:      started.UTC().Format(time.RFC3339),
		Truncated:     truncatedAt != 0,
		TruncatedAt:   truncatedAt,
	}, nil
}

func emptyIfNil[T any](rows []T) []T {
	if rows == nil {
		return []T{}
	}
	return rows
}

func sortedTactics(rows []Tactic) []Tactic {
	sort.SliceStable(rows, func(i, j int) bool {
		a, b := rows[i], rows[j]
		if a.Map != b.Map {
			return a.Map < b.Map
		}
		if a.Boss != b.Boss {
			return a.Boss < b.Boss
		}
		if a.Kind != b.Kind {
			return a.Kind < b.Kind
		}
		if a.Target != b.Target {
			return a.Target < b.Target
		}
		return a.Spell < b.Spell
	})
	return rows
}

func sortedStands(rows []Stand) []Stand {
	sort.SliceStable(rows, func(i, j int) bool {
		a, b := rows[i], rows[j]
		if a.Map != b.Map {
			return a.Map < b.Map
		}
		if a.Boss != b.Boss {
			return a.Boss < b.Boss
		}
		if a.WindowSpell != b.WindowSpell {
			return a.WindowSpell < b.WindowSpell
		}
		return a.Role < b.Role
	})
	return rows
}

func sortedPlanKills(rows []PlanKill) []PlanKill {
	sort.SliceStable(rows, func(i, j int) bool { return rows[i].Plan < rows[j].Plan })
	return rows
}

func sortedBossKills(rows []BossKill) []BossKill {
	sort.SliceStable(rows, func(i, j int) bool {
		if rows[i].Plan != rows[j].Plan {
			return rows[i].Plan < rows[j].Plan
		}
		return rows[i].Boss < rows[j].Boss
	})
	return rows
}

// Handler serves GET /api/v1/boss/tactics[?map=<id>]: 503 when the world
// database is missing or failing, 400 for a malformed map.
func Handler(s *Service) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		if s == nil {
			w.WriteHeader(http.StatusServiceUnavailable)
			_, _ = w.Write([]byte(`{"error":"world database unavailable"}`))
			return
		}
		var mapID uint32
		if raw := r.URL.Query().Get("map"); raw != "" {
			n, err := strconv.ParseUint(raw, 10, 32)
			if err != nil || n == 0 {
				w.WriteHeader(http.StatusBadRequest)
				_, _ = w.Write([]byte(`{"error":"bad map"}`))
				return
			}
			mapID = uint32(n)
		}
		get := s.Get
		if r.URL.Query().Get("refresh") == "1" {
			get = s.Refresh
		}
		snap, err := get(r.Context())
		if err != nil {
			log.Printf("[BossTactics] world database: %v", err)
			w.WriteHeader(http.StatusServiceUnavailable)
			_, _ = w.Write([]byte(`{"error":"world database unavailable"}`))
			return
		}
		if mapID != 0 {
			snap = snap.ForMap(mapID)
		}
		_ = json.NewEncoder(w).Encode(snap)
	}
}
