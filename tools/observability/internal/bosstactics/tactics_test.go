package bosstactics

import (
	"context"
	"encoding/json"
	"errors"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"reflect"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/go-sql-driver/mysql"
)

// fakeSource answers the three reads from memory and counts one call per
// snapshot read (the first method every read calls).
type fakeSource struct {
	mu      sync.Mutex
	tactics []Tactic
	stands  []Stand
	plans   []PlanKill
	steps   []BossKill
	bErr    error
	bCut    bool
	tErr    error
	sErr    error
	pErr    error
	tCut    bool
	sCut    bool
	pCut    bool
	calls   int
}

func (f *fakeSource) Tactics(context.Context) ([]Tactic, bool, error) {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.calls++
	if f.tErr != nil {
		return nil, false, f.tErr
	}
	return f.tactics, f.tCut, nil
}

func (f *fakeSource) Stands(context.Context) ([]Stand, bool, error) {
	f.mu.Lock()
	defer f.mu.Unlock()
	if f.sErr != nil {
		return nil, false, f.sErr
	}
	return f.stands, f.sCut, nil
}

func (f *fakeSource) PlanKillFirst(context.Context) ([]PlanKill, bool, error) {
	f.mu.Lock()
	defer f.mu.Unlock()
	if f.pErr != nil {
		return nil, false, f.pErr
	}
	return f.plans, f.pCut, nil
}

func (f *fakeSource) BossKillFirst(context.Context) ([]BossKill, bool, error) {
	f.mu.Lock()
	defer f.mu.Unlock()
	if f.bErr != nil {
		return nil, false, f.bErr
	}
	return f.steps, f.bCut, nil
}

func (f *fakeSource) callCount() int {
	f.mu.Lock()
	defer f.mu.Unlock()
	return f.calls
}

func (f *fakeSource) setErrs(tErr, sErr, pErr error) {
	f.mu.Lock()
	f.tErr, f.sErr, f.pErr = tErr, sErr, pErr
	f.mu.Unlock()
}

func newFake() *fakeSource {
	return &fakeSource{
		tactics: []Tactic{
			{Map: 309, Boss: 90001, Kind: "interrupt", Target: 0, Spell: 91001, Comment: "interrupt row"},
			{Map: 249, Boss: 90002, Kind: "behind", Target: 0, Spell: 91002, Param: 10},
		},
		stands: []Stand{
			{Map: 249, Boss: 90002, WindowSpell: 91003, Aura: 0, Role: "ranged", Signal: "cast", Seats: 7},
		},
		plans: []PlanKill{
			{Plan: "some_plan", Map: 43, Entries: []uint32{3671, 61966}},
		},
	}
}

func TestServiceCache(t *testing.T) {
	now := time.Unix(1700000000, 0)
	src := newFake()
	srv := NewService(src)
	srv.SetClock(func() time.Time { return now })

	first, err := srv.Get(context.Background())
	if err != nil {
		t.Fatalf("first get: %v", err)
	}
	if src.callCount() != 1 {
		t.Fatalf("first get read the source %d times, want 1", src.callCount())
	}

	now = now.Add(CacheTTL - time.Second)
	second, err := srv.Get(context.Background())
	if err != nil {
		t.Fatalf("cached get: %v", err)
	}
	if src.callCount() != 1 {
		t.Fatalf("get inside the TTL read the source again (%d calls): the snapshot is not cached", src.callCount())
	}
	if second.LoadedAt != first.LoadedAt {
		t.Fatalf("cached snapshot loadedAt = %q, want %q", second.LoadedAt, first.LoadedAt)
	}

	now = now.Add(2 * time.Second)
	if _, err := srv.Get(context.Background()); err != nil {
		t.Fatalf("get after the TTL: %v", err)
	}
	if src.callCount() != 2 {
		t.Fatalf("get after the TTL read the source %d times, want 2 (the snapshot expired)", src.callCount())
	}
}

func TestServiceRefresh(t *testing.T) {
	now := time.Unix(1700000000, 0)
	src := newFake()
	srv := NewService(src)
	srv.SetClock(func() time.Time { return now })
	if _, err := srv.Get(context.Background()); err != nil {
		t.Fatalf("first get: %v", err)
	}
	now = now.Add(time.Second) // well inside the TTL
	if _, err := srv.Get(context.Background()); err != nil || src.callCount() != 1 {
		t.Fatalf("a get inside the TTL must stay cached (calls %d, err %v)", src.callCount(), err)
	}
	snap, err := srv.Refresh(context.Background())
	if err != nil {
		t.Fatalf("refresh: %v", err)
	}
	if src.callCount() != 2 {
		t.Fatalf("refresh inside the TTL read the source %d times, want 2", src.callCount())
	}
	if again, _ := srv.Get(context.Background()); again.LoadedAt != snap.LoadedAt || src.callCount() != 2 {
		t.Fatalf("the refreshed snapshot is the cached one afterwards")
	}
	h := Handler(srv)
	h(httptest.NewRecorder(), httptest.NewRequest("GET", "/api/v1/boss/tactics?refresh=1", nil))
	if src.callCount() != 3 {
		t.Fatalf("?refresh=1 read the source %d times in all, want 3", src.callCount())
	}
	h(httptest.NewRecorder(), httptest.NewRequest("GET", "/api/v1/boss/tactics", nil))
	if src.callCount() != 3 {
		t.Fatalf("a plain request must use the cache, source read %d times", src.callCount())
	}
}

func TestServiceErrorTTL(t *testing.T) {
	now := time.Unix(1700000000, 0)
	src := newFake()
	src.setErrs(errors.New("world database is down"), nil, nil)
	srv := NewService(src)
	srv.SetClock(func() time.Time { return now })

	if _, err := srv.Get(context.Background()); err == nil {
		t.Fatal("first get: want the source's error")
	}
	if src.callCount() != 1 {
		t.Fatalf("first get read the source %d times, want 1", src.callCount())
	}

	now = now.Add(ErrorTTL - time.Second)
	if _, err := srv.Get(context.Background()); err == nil {
		t.Fatal("get inside the error TTL: want the cached error")
	}
	if src.callCount() != 1 {
		t.Fatalf("get inside the error TTL retried the source (%d calls): the error is not cached", src.callCount())
	}

	now = now.Add(2 * time.Second)
	src.setErrs(nil, nil, nil)
	snap, err := srv.Get(context.Background())
	if err != nil {
		t.Fatalf("get after the error TTL: %v", err)
	}
	if src.callCount() != 2 {
		t.Fatalf("get after the error TTL read the source %d times, want 2 (the error expired)", src.callCount())
	}
	if len(snap.Tactics) != 2 {
		t.Fatalf("tactics after the retry = %d rows, want 2", len(snap.Tactics))
	}
}

// gatedSource blocks inside Tactics until gate closes, so a test can inspect
// the service while the read is in flight.
type gatedSource struct {
	gate    chan struct{}
	reached chan struct{}
	once    sync.Once
}

func (g *gatedSource) Tactics(context.Context) ([]Tactic, bool, error) {
	g.once.Do(func() { close(g.reached) })
	<-g.gate
	return []Tactic{{Map: 1, Boss: 2, Kind: "interrupt", Spell: 3}}, false, nil
}

func (g *gatedSource) Stands(context.Context) ([]Stand, bool, error) { return nil, false, nil }
func (g *gatedSource) PlanKillFirst(context.Context) ([]PlanKill, bool, error) {
	return nil, false, nil
}
func (g *gatedSource) BossKillFirst(context.Context) ([]BossKill, bool, error) {
	return nil, false, nil
}

// TestServiceReadsOutsideTheLock: a slow world-database read must not hold the
// service's lock, or the whole page waits behind it.
func TestServiceReadsOutsideTheLock(t *testing.T) {
	src := &gatedSource{gate: make(chan struct{}), reached: make(chan struct{})}
	srv := NewService(src)
	slow := make(chan error, 1)
	go func() {
		_, err := srv.Get(context.Background())
		slow <- err
	}()
	<-src.reached

	locked := make(chan struct{})
	go func() {
		srv.mu.Lock()
		close(locked)
		srv.mu.Unlock()
	}()
	select {
	case <-locked:
	case <-time.After(2 * time.Second):
		t.Fatal("the service lock was held across the world-database read")
	}
	close(src.gate)
	if err := <-slow; err != nil {
		t.Fatalf("gated get: %v", err)
	}
}

// TestMissingTableIsEmpty: a world database without ai_boss_stand answers
// MySQL 1146. That table reads as empty; the other two are still served.
func TestMissingTableIsEmpty(t *testing.T) {
	src := newFake()
	src.setErrs(nil, &mysql.MySQLError{Number: mysqlNoSuchTable, Message: "Table 'world.ai_boss_stand' doesn't exist"}, nil)
	snap, err := NewService(src).Get(context.Background())
	if err != nil {
		t.Fatalf("get: %v (a missing table is not a failure)", err)
	}
	if len(snap.Tactics) != 2 {
		t.Fatalf("tactics = %d rows, want 2", len(snap.Tactics))
	}
	if snap.Stands == nil || len(snap.Stands) != 0 {
		t.Fatalf("stands = %#v, want an empty list", snap.Stands)
	}
	body, err := json.Marshal(snap)
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(body), `"stands":[]`) {
		t.Fatalf("stands encode as %s, want []", body)
	}
}

func TestOtherErrorFails(t *testing.T) {
	src := newFake()
	src.setErrs(nil, errors.New("connection reset"), nil)
	if _, err := NewService(src).Get(context.Background()); err == nil {
		t.Fatal("get: want the source's error, a broken database must not read as empty rows")
	}
}

func TestPlanKillFirstParse(t *testing.T) {
	if got, ok := ParseKillFirst("3671, 61966"); !ok || !reflect.DeepEqual(got, []uint32{3671, 61966}) {
		t.Fatalf("ParseKillFirst(%q) = %v, %v; want [3671 61966], true", "3671, 61966", got, ok)
	}
	if got, ok := ParseKillFirst(""); !ok || len(got) != 0 {
		t.Fatalf("ParseKillFirst(%q) = %v, %v; want an empty list, true (the caller drops it)", "", got, ok)
	}
	if _, ok := ParseKillFirst("12x"); ok {
		t.Fatalf("ParseKillFirst(%q) accepted a token that is no creature entry", "12x")
	}

	rows := []planKillRow{
		{Plan: "kept", Map: 43, KillFirst: "3671, 61966"},
		{Plan: "empty", Map: 43, KillFirst: ""},
		{Plan: "bad", Map: 43, KillFirst: "12x"},
	}
	got := buildPlanKills(rows)
	if len(got) != 1 || got[0].Plan != "kept" || !reflect.DeepEqual(got[0].Entries, []uint32{3671, 61966}) {
		t.Fatalf("buildPlanKills = %#v, want only the parsed plan", got)
	}
}

func TestHandler(t *testing.T) {
	// No world database handle: the route says so instead of answering with
	// an empty row set that would read as "no tactics anywhere".
	rec := httptest.NewRecorder()
	Handler(nil)(rec, httptest.NewRequest(http.MethodGet, "/api/v1/boss/tactics", nil))
	if rec.Code != http.StatusServiceUnavailable {
		t.Fatalf("nil service status = %d, want 503", rec.Code)
	}
	if body := rec.Body.String(); !strings.Contains(body, "world database unavailable") {
		t.Fatalf("nil service body = %q", body)
	}

	src := newFake()
	srv := NewService(src)
	h := Handler(srv)

	rec = httptest.NewRecorder()
	h(rec, httptest.NewRequest(http.MethodGet, "/api/v1/boss/tactics", nil))
	if rec.Code != http.StatusOK {
		t.Fatalf("status = %d, want 200", rec.Code)
	}
	var keys map[string]json.RawMessage
	if err := json.Unmarshal(rec.Body.Bytes(), &keys); err != nil {
		t.Fatalf("body is not JSON: %v", err)
	}
	for _, key := range []string{"tactics", "stands", "planKillFirst", "source", "loadedAt"} {
		if _, ok := keys[key]; !ok {
			t.Errorf("body has no %q key: %s", key, rec.Body.String())
		}
	}
	var full Snapshot
	if err := json.Unmarshal(rec.Body.Bytes(), &full); err != nil {
		t.Fatal(err)
	}
	if full.Source != SourceWorld {
		t.Fatalf("source = %q, want %q", full.Source, SourceWorld)
	}
	if _, err := time.Parse(time.RFC3339, full.LoadedAt); err != nil {
		t.Fatalf("loadedAt %q is not RFC 3339: %v", full.LoadedAt, err)
	}

	// The filter comes from the cached snapshot.
	rec = httptest.NewRecorder()
	h(rec, httptest.NewRequest(http.MethodGet, "/api/v1/boss/tactics?map=309", nil))
	if rec.Code != http.StatusOK {
		t.Fatalf("filtered status = %d, want 200", rec.Code)
	}
	var filtered Snapshot
	if err := json.Unmarshal(rec.Body.Bytes(), &filtered); err != nil {
		t.Fatal(err)
	}
	if len(filtered.Tactics) != 1 || filtered.Tactics[0].Map != 309 {
		t.Fatalf("filtered tactics = %#v, want the one row of map 309", filtered.Tactics)
	}
	if len(filtered.Stands) != 0 || len(filtered.PlanKillFirst) != 0 {
		t.Fatalf("filtered stands/plans = %d/%d, want 0/0", len(filtered.Stands), len(filtered.PlanKillFirst))
	}
	if src.callCount() != 1 {
		t.Fatalf("the map filter read the source again (%d calls): it must filter the cached snapshot", src.callCount())
	}

	for _, bad := range []string{"x", "0", "-1", "309x"} {
		rec = httptest.NewRecorder()
		h(rec, httptest.NewRequest(http.MethodGet, "/api/v1/boss/tactics?map="+bad, nil))
		if rec.Code != http.StatusBadRequest {
			t.Errorf("map=%s status = %d, want 400", bad, rec.Code)
		}
		if body := rec.Body.String(); !strings.Contains(body, "bad map") {
			t.Errorf("map=%s body = %q, want bad map", bad, body)
		}
	}
}

// TestPlanRowsCountedBeforeFiltering: the cap is a limit on the rows the query
// returned, not on the plans left after the invalid kill_first lists are
// dropped. A probe row over the cap marks the snapshot even when most of the
// rows above it are unusable.
func TestPlanRowsCountedBeforeFiltering(t *testing.T) {
	raw := make([]planKillRow, PlansLimit+1)
	for i := range raw {
		raw[i] = planKillRow{Plan: "plan", Map: 43, KillFirst: "3671"}
	}
	raw[0] = planKillRow{Plan: "empty", Map: 43, KillFirst: ""}
	raw[1] = planKillRow{Plan: "bad", Map: 43, KillFirst: "12x"}

	plans, truncated := planKillsWithinCap(raw)
	if !truncated {
		t.Fatalf("%d raw plan rows (cap %d, two dropped) are not marked truncated", len(raw), PlansLimit)
	}
	if len(plans) != PlansLimit-2 {
		t.Fatalf("plans = %d, want %d: the rows over the cap are trimmed, the unusable ones dropped", len(plans), PlansLimit-2)
	}
}

func TestTruncatedFlag(t *testing.T) {
	below := newFake()
	snap, err := NewService(below).Get(context.Background())
	if err != nil {
		t.Fatal(err)
	}
	if snap.Truncated {
		t.Fatal("snapshot below every LIMIT is marked truncated")
	}

	cases := []struct {
		name string
		src  *fakeSource
		cap  int
	}{
		{"tactics", &fakeSource{tCut: true}, TacticsLimit},
		{"stands", &fakeSource{sCut: true}, StandsLimit},
		{"plans", &fakeSource{pCut: true}, PlansLimit},
		{"boss steps", &fakeSource{bCut: true}, StepsLimit},
	}
	for _, one := range cases {
		snap, err = NewService(one.src).Get(context.Background())
		if err != nil {
			t.Fatal(err)
		}
		if !snap.Truncated {
			t.Fatalf("%s: the source's cut is not marked truncated", one.name)
		}
		if snap.TruncatedAt != one.cap {
			t.Fatalf("%s: truncatedAt = %d, want the cap %d", one.name, snap.TruncatedAt, one.cap)
		}
	}
}

func TestQueries(t *testing.T) {
	for name, query := range map[string]string{
		"tactics": QueryTactics,
		"stands":  QueryStands,
		"plans":   QueryPlanKillFirst,
		"steps":   QueryBossSteps,
	} {
		if !strings.Contains(query, "ORDER BY") {
			t.Errorf("%s query has no ORDER BY: %s", name, query)
		}
		if !strings.Contains(query, "LIMIT ") {
			t.Errorf("%s query has no LIMIT: %s", name, query)
		}
		if strings.Contains(query, "'") || strings.Contains(query, `"`) {
			t.Errorf("%s query carries a quoted literal: %s", name, query)
		}
	}
	if !strings.Contains(QueryStands, "GROUP BY") || !strings.Contains(QueryStands, "SUM(`slots`)") {
		t.Errorf("stands query does not aggregate the seats: %s", QueryStands)
	}
	if !strings.HasSuffix(QueryTactics, "LIMIT 5001") || !strings.HasSuffix(QueryStands, "LIMIT 5001") {
		t.Errorf("tactics/stands queries do not ask one row over their caps (%d/%d): %s / %s", TacticsLimit, StandsLimit, QueryTactics, QueryStands)
	}
	if !strings.HasSuffix(QueryPlanKillFirst, "LIMIT 1001") {
		t.Errorf("plan query does not ask one row over its cap (%d): %s", PlansLimit, QueryPlanKillFirst)
	}
	if !strings.HasSuffix(QueryBossSteps, "LIMIT 20001") {
		t.Errorf("boss step query does not ask one row over its cap (%d): %s", StepsLimit, QueryBossSteps)
	}
	if TacticsLimit != 5000 || StandsLimit != 5000 || PlansLimit != 1000 {
		t.Errorf("row caps = %d/%d/%d, want the values the queries pin", TacticsLimit, StandsLimit, PlansLimit)
	}
}

func TestQueriesUseMapPlaceholders(t *testing.T) {
	maps := []uint32{249, 309, 531}
	for name, base := range map[string]string{
		"tactics": QueryTactics,
		"stands":  QueryStands,
		"plans":   QueryPlanKillFirst,
		"steps":   QueryBossSteps,
	} {
		query, args := queryWithMaps(base, maps)
		if strings.Contains(query, mapFilterMarker) {
			t.Errorf("%s query keeps the filter marker: %s", name, query)
		}
		if !strings.Contains(query, "`map` IN (?, ?, ?)") {
			t.Errorf("%s query does not filter the map list through placeholders: %s", name, query)
		}
		if strings.Contains(query, "309") {
			t.Errorf("%s query carries a map id as SQL text: %s", name, query)
		}
		if !reflect.DeepEqual(args, []any{uint32(249), uint32(309), uint32(531)}) {
			t.Errorf("%s query args = %v, want the map list", name, args)
		}
	}

	query, args := queryWithMaps(QueryTactics, nil)
	if strings.Contains(query, mapFilterMarker) || len(args) != 0 {
		t.Errorf("an empty map list leaves a filter behind: %s %v", query, args)
	}
}

func TestRaidMapsMatchTheCommittedCSV(t *testing.T) {
	committed, err := os.ReadFile(filepath.Join("..", "..", "..", "..", "data", "boss", "raids.csv"))
	if err != nil {
		t.Fatal(err)
	}
	if string(committed) != raidsCSV {
		t.Fatal("the embedded raids.csv copy drifted from data/boss/raids.csv")
	}
	maps, err := RaidMaps()
	if err != nil {
		t.Fatal(err)
	}
	if want := []uint32{109, 209, 229, 230, 249, 309, 409, 429, 469, 509, 531, 532, 533, 807, 814, 819}; !reflect.DeepEqual(maps, want) {
		t.Fatalf("RaidMaps() = %v, want %v", maps, want)
	}
}

// TestBossKillFirst pins the boss-step read: only kind boss rows count, the empty list gives no row, an unparsable one is
// dropped, the cap counts every raw row (not just the boss steps), and ForMap and the sort cover the new list.
func TestBossKillFirst(t *testing.T) {
	raw := []bossStepRow{
		{Plan: "zul_gurub", Map: 309, Kind: "move", Entry: 0, Entries: ""},
		{Plan: "zul_gurub", Map: 309, Kind: "boss", Entry: 14507, Entries: "14750, 14751"},
		{Plan: "zul_gurub", Map: 309, Kind: "pull", Entry: 0, Entries: "14750"},
		{Plan: "zul_gurub", Map: 309, Kind: "boss", Entry: 14510, Entries: ""},
		{Plan: "zul_gurub", Map: 309, Kind: "boss", Entry: 14515, Entries: "14750,abc"},
		{Plan: "aq20", Map: 509, Kind: "boss", Entry: 15348, Entries: "0,15350"},
	}
	got := buildBossKills(raw)
	want := []BossKill{
		{Plan: "zul_gurub", Map: 309, Boss: 14507, Entries: []uint32{14750, 14751}},
		{Plan: "aq20", Map: 509, Boss: 15348, Entries: []uint32{15350}},
	}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("boss kills = %+v, want %+v", got, want)
	}

	over := make([]bossStepRow, StepsLimit+1)
	for i := range over {
		over[i] = bossStepRow{Plan: "p", Map: 309, Kind: "move"}
	}
	over[0] = bossStepRow{Plan: "p", Map: 309, Kind: "boss", Entry: 1, Entries: "2"}
	steps, truncated := bossKillsWithinCap(over)
	if !truncated || len(steps) != 1 {
		t.Fatalf("%d raw steps over the cap: truncated=%v steps=%d, want true/1 (the cap counts non-boss rows too)", len(over), truncated, len(steps))
	}

	src := newFake()
	src.steps = []BossKill{
		{Plan: "zul_gurub", Map: 309, Boss: 14507, Entries: []uint32{14750}},
		{Plan: "aq20", Map: 509, Boss: 15348, Entries: []uint32{15350}},
	}
	snap, err := NewService(src).Get(context.Background())
	if err != nil {
		t.Fatal(err)
	}
	if len(snap.BossKillFirst) != 2 || snap.BossKillFirst[0].Plan != "aq20" {
		t.Fatalf("snapshot boss kills = %+v, want both, sorted by plan", snap.BossKillFirst)
	}
	if one := snap.ForMap(309); len(one.BossKillFirst) != 1 || one.BossKillFirst[0].Boss != 14507 {
		t.Fatalf("ForMap(309) boss kills = %+v", one.BossKillFirst)
	}
	if none := snap.ForMap(1); none.BossKillFirst == nil || len(none.BossKillFirst) != 0 {
		t.Fatalf("ForMap(1) boss kills = %#v, want an empty list (JSON [], not null)", none.BossKillFirst)
	}

	// A missing table reads as no steps; any other failure fails the snapshot (fail closed).
	src.bErr = &mysql.MySQLError{Number: mysqlNoSuchTable, Message: "Table 'world.ai_dungeon_step' doesn't exist"}
	missing, err := NewService(src).Get(context.Background())
	if err != nil || len(missing.BossKillFirst) != 0 {
		t.Fatalf("missing step table: %v %+v", err, missing)
	}
	src.bErr = errors.New("connection reset")
	if _, err := NewService(src).Get(context.Background()); err == nil {
		t.Fatal("a broken step read must fail the snapshot, not read as no steps")
	}
}
