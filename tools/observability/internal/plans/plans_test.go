package plans

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"flag"
	"io"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"reflect"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"testing"
	"time"
)

var update = flag.Bool("update", false, "rewrite the golden file")

type fixtureFile struct {
	Plan      PlanRow       `json:"plan"`
	Legs      []LegRow      `json:"legs"`
	Steps     []StepRow     `json:"steps"`
	Spawns    []SpawnRow    `json:"spawns"`
	Templates []TemplateRow `json:"templates"`
}

type memSource struct {
	planRow       PlanRow
	found         bool
	missing       map[string]bool
	srcErr        error
	legs          []LegRow
	steps         []StepRow
	spawns        []SpawnRow
	templates     []TemplateRow
	planCalls     int
	templateCalls int
	spawnCalls    int
	templateArgs  []uint32
	spawnArgs     []uint32
}

func (m *memSource) Plan(_ context.Context, name string) (PlanRow, bool, error) {
	m.planCalls++
	if m.srcErr != nil {
		return PlanRow{}, false, m.srcErr
	}
	if m.missing[name] {
		return PlanRow{}, false, nil
	}
	return m.planRow, m.found, nil
}

func (m *memSource) Legs(_ context.Context, name string) ([]LegRow, error) {
	if m.srcErr != nil {
		return nil, m.srcErr
	}
	return m.legs, nil
}

func (m *memSource) Steps(_ context.Context, name string) ([]StepRow, error) {
	if m.srcErr != nil {
		return nil, m.srcErr
	}
	return m.steps, nil
}

func (m *memSource) Templates(_ context.Context, entries []uint32) ([]TemplateRow, error) {
	m.templateCalls++
	m.templateArgs = append([]uint32(nil), entries...)
	if m.srcErr != nil {
		return nil, m.srcErr
	}
	return m.templates, nil
}

func (m *memSource) Spawns(_ context.Context, mapID uint32, entries []uint32) ([]SpawnRow, error) {
	m.spawnCalls++
	m.spawnArgs = append([]uint32(nil), entries...)
	if m.srcErr != nil {
		return nil, m.srcErr
	}
	return m.spawns, nil
}

func loadFixture(t *testing.T) *memSource {
	t.Helper()
	raw, err := os.ReadFile(filepath.Join("testdata", "onyxias_lair.rows.json"))
	if err != nil {
		t.Fatalf("read fixture: %v", err)
	}
	var fx fixtureFile
	if err := json.Unmarshal(raw, &fx); err != nil {
		t.Fatalf("decode fixture: %v", err)
	}
	return &memSource{
		planRow:   fx.Plan,
		found:     true,
		legs:      fx.Legs,
		steps:     fx.Steps,
		spawns:    fx.Spawns,
		templates: fx.Templates,
	}
}

func TestBuildOnyxia(t *testing.T) {
	src := loadFixture(t)
	p, err := Build(context.Background(), src, "onyxias_lair")
	if err != nil {
		t.Fatalf("Build: %v", err)
	}
	if p.StepsTotal != 29 || len(p.Steps) != 29 {
		t.Fatalf("steps_total = %d, steps = %d", p.StepsTotal, len(p.Steps))
	}
	var stands []int
	for _, s := range p.Steps {
		if s.Stand {
			stands = append(stands, s.I)
		}
	}
	if want := []int{1, 16, 20, 22, 24, 28}; !reflect.DeepEqual(stands, want) {
		t.Fatalf("stands = %v, want %v", stands, want)
	}
	boss := p.Steps[28]
	if boss.K != "boss" || boss.E != 10184 || boss.N != "Onyxia" || boss.R != 30 {
		t.Fatalf("boss step = %+v", boss)
	}
	pull := p.Steps[1]
	if pull.K != "pull" || pull.R != 1 || !reflect.DeepEqual(pull.Entries, []uint32{12129}) || pull.N != "Onyxian Warder" {
		t.Fatalf("pull step = %+v", pull)
	}
	type pair struct {
		step int
		guid uint32
	}
	var got []pair
	for _, m := range p.Mobs {
		got = append(got, pair{m.Step, m.GUID})
	}
	want := []pair{{2, 52050}, {17, 52049}, {21, 52052}, {23, 52048}, {25, 52053}, {29, 47572}}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("mobs = %v, want %v", got, want)
	}
	patrol := map[uint32]bool{}
	for _, m := range p.Mobs {
		patrol[m.GUID] = m.Patrol
		if m.GUID == 99901 || m.GUID == 99902 {
			t.Fatalf("decoy spawn %d was assigned", m.GUID)
		}
	}
	for _, guid := range []uint32{52050, 52049, 52052, 52048} {
		if !patrol[guid] {
			t.Fatalf("guid %d: patrol = false", guid)
		}
	}
	for _, guid := range []uint32{52053, 47572} {
		if patrol[guid] {
			t.Fatalf("guid %d: patrol = true", guid)
		}
	}
	var onyxia *Mob
	for i := range p.Mobs {
		if p.Mobs[i].GUID == 47572 {
			onyxia = &p.Mobs[i]
		}
	}
	if onyxia == nil {
		t.Fatal("no Onyxia mob")
	}
	if onyxia.Lvl != 63 || onyxia.Det != 20 || onyxia.Rank != 3 || onyxia.NoAggro {
		t.Fatalf("onyxia mob = %+v", *onyxia)
	}
	if len(p.Legs) != 1 || p.Legs[0].Leg != 0 || p.Legs[0].Name != "onyxia" {
		t.Fatalf("legs = %+v", p.Legs)
	}
}

func TestBuildOnyxiaGolden(t *testing.T) {
	src := loadFixture(t)
	p, err := Build(context.Background(), src, "onyxias_lair")
	if err != nil {
		t.Fatalf("Build: %v", err)
	}
	got, err := json.MarshalIndent(p, "", " ")
	if err != nil {
		t.Fatalf("marshal: %v", err)
	}
	path := filepath.Join("testdata", "onyxias_lair.plan.json")
	if *update {
		if err := os.WriteFile(path, got, 0o644); err != nil {
			t.Fatalf("write golden: %v", err)
		}
		return
	}
	want, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("read golden (run with -update): %v", err)
	}
	if !bytes.Equal(got, want) {
		t.Fatalf("golden mismatch\n got: %s\nwant: %s", got, want)
	}
}

func TestBuildDropsRowsTheModuleRefuses(t *testing.T) {
	src := loadFixture(t)
	bad := []StepRow{
		{Leg: 0, Seq: 3, Kind: "dance", X: 1, Y: 2, Z: 3},
		{Leg: 0, Seq: 3, Kind: "pull", Entries: "12x", X: 1, Y: 2, Z: 3},
	}
	steps := append([]StepRow{}, src.steps[:4]...)
	steps = append(steps, bad...)
	steps = append(steps, src.steps[4:]...)
	src.steps = steps

	p, err := Build(context.Background(), src, "onyxias_lair")
	if err != nil {
		t.Fatalf("Build: %v", err)
	}
	if p.StepsTotal != 29 {
		t.Fatalf("steps_total = %d, want 29", p.StepsTotal)
	}
	if p.Steps[4].K != "move" || p.Steps[4].Seq != 4 {
		t.Fatalf("step 5 = %+v, want the old seq 4 move", p.Steps[4])
	}

	// A valid extra move row shifts every later step by one.
	src = loadFixture(t)
	extra := StepRow{Leg: 0, Seq: 3, Kind: "move", X: 1, Y: 2, Z: 3}
	steps = append([]StepRow{}, src.steps[:4]...)
	steps = append(steps, extra)
	steps = append(steps, src.steps[4:]...)
	src.steps = steps

	p, err = Build(context.Background(), src, "onyxias_lair")
	if err != nil {
		t.Fatalf("Build: %v", err)
	}
	if p.StepsTotal != 30 {
		t.Fatalf("steps_total = %d, want 30", p.StepsTotal)
	}
	if p.Steps[4].Seq != 3 || p.Steps[4].I != 5 || p.Steps[5].Seq != 4 || p.Steps[5].I != 6 {
		t.Fatalf("extra row numbering: step 5 = %+v, step 6 = %+v", p.Steps[4], p.Steps[5])
	}
}

func TestBuildDefaultRadius(t *testing.T) {
	src := &memSource{
		planRow: PlanRow{Plan: "mini", Map: 1},
		found:   true,
		legs:    []LegRow{{Leg: 0, Name: "one"}},
		steps: []StepRow{
			{Leg: 0, Seq: 0, Kind: "pull", Entries: "5", X: 1, Y: 1, Z: 1},
			{Leg: 0, Seq: 1, Kind: "boss", Entry: 6, X: 2, Y: 2, Z: 2},
			{Leg: 0, Seq: 2, Kind: "move", X: 3, Y: 3, Z: 3},
			{Leg: 0, Seq: 3, Kind: "wait_clear", Radius: 7, X: 4, Y: 4, Z: 4},
		},
	}
	p, err := Build(context.Background(), src, "mini")
	if err != nil {
		t.Fatalf("Build: %v", err)
	}
	want := []float64{15, 30, 0, 7}
	for i, w := range want {
		if p.Steps[i].R != w {
			t.Fatalf("step %d r = %v, want %v", i+1, p.Steps[i].R, w)
		}
	}
}

func TestBuildNotFound(t *testing.T) {
	src := &memSource{found: false}
	_, err := Build(context.Background(), src, "missing")
	if !errors.Is(err, ErrNotFound) {
		t.Fatalf("err = %v, want ErrNotFound", err)
	}
}

func TestBuildEntryCap(t *testing.T) {
	src := &memSource{planRow: PlanRow{Plan: "wide", Map: 1}, found: true}
	for i := range 70 {
		src.steps = append(src.steps, StepRow{
			Leg: 0, Seq: uint16(i), Kind: "pull", Entries: strconv.Itoa(i + 1), X: float64(i), Y: 0, Z: 0,
		})
	}
	p, err := Build(context.Background(), src, "wide")
	if err != nil {
		t.Fatalf("Build: %v", err)
	}
	if !p.Truncated {
		t.Fatal("truncated = false")
	}
	if len(src.templateArgs) != PlanMaxEntries {
		t.Fatalf("templates got %d entries, want %d", len(src.templateArgs), PlanMaxEntries)
	}
	want := make([]uint32, PlanMaxEntries)
	for i := range want {
		want[i] = uint32(i + 1)
	}
	if !reflect.DeepEqual(src.templateArgs, want) {
		t.Fatalf("template entries = %v", src.templateArgs)
	}
	if len(src.spawnArgs) != PlanMaxEntries {
		t.Fatalf("spawns got %d entries, want %d", len(src.spawnArgs), PlanMaxEntries)
	}
}

func TestServiceCache(t *testing.T) {
	base := time.Date(2026, 10, 28, 12, 0, 0, 0, time.UTC)
	now := base
	clock := func() time.Time { return now }

	src := loadFixture(t)
	srv := NewService(src)
	srv.SetClock(clock)
	if _, err := srv.Get(context.Background(), "onyxias_lair"); err != nil {
		t.Fatalf("first get: %v", err)
	}
	if _, err := srv.Get(context.Background(), "onyxias_lair"); err != nil {
		t.Fatalf("second get: %v", err)
	}
	if src.planCalls != 1 {
		t.Fatalf("plan calls = %d, want 1", src.planCalls)
	}
	now = base.Add(11 * time.Minute)
	if _, err := srv.Get(context.Background(), "onyxias_lair"); err != nil {
		t.Fatalf("third get: %v", err)
	}
	if src.planCalls != 2 {
		t.Fatalf("plan calls after ttl = %d, want 2", src.planCalls)
	}

	missing := &memSource{found: false}
	now = base
	missSrv := NewService(missing)
	missSrv.SetClock(clock)
	if _, err := missSrv.Get(context.Background(), "ghost"); !errors.Is(err, ErrNotFound) {
		t.Fatalf("missing get: %v", err)
	}
	if _, err := missSrv.Get(context.Background(), "ghost"); !errors.Is(err, ErrNotFound) {
		t.Fatalf("missing get again: %v", err)
	}
	if missing.planCalls != 1 {
		t.Fatalf("miss plan calls = %d, want 1", missing.planCalls)
	}
	now = base.Add(61 * time.Second)
	if _, err := missSrv.Get(context.Background(), "ghost"); !errors.Is(err, ErrNotFound) {
		t.Fatalf("missing get after ttl: %v", err)
	}
	if missing.planCalls != 2 {
		t.Fatalf("miss plan calls after ttl = %d, want 2", missing.planCalls)
	}

	broken := &memSource{srcErr: errors.New("boom")}
	errSrv := NewService(broken)
	errSrv.SetClock(clock)
	now = base
	for range 2 {
		if _, err := errSrv.Get(context.Background(), "x"); err == nil {
			t.Fatal("erroring get returned nil error")
		}
	}
	if broken.planCalls != 1 {
		t.Fatalf("error plan calls = %d, want 1 (a failure is cached for PlanErrorTTL)", broken.planCalls)
	}
	now = base.Add(PlanErrorTTL + time.Second)
	if _, err := errSrv.Get(context.Background(), "x"); err == nil {
		t.Fatal("erroring get after the error ttl returned nil error")
	}
	if broken.planCalls != 2 {
		t.Fatalf("error plan calls after the error ttl = %d, want 2", broken.planCalls)
	}
}

// TestServiceBuildsOutsideTheLock: a plan whose world-database read is slow must not hold the service's lock, or every other
// card's plan waits behind it (the map asks for its plan on every draw).
func TestServiceBuildsOutsideTheLock(t *testing.T) {
	src := &gatedSource{gate: make(chan struct{}), reached: make(chan struct{})}
	srv := NewService(src)
	slow := make(chan error, 1)
	go func() { _, err := srv.Get(context.Background(), "slow"); slow <- err }()
	<-src.reached
	fast := make(chan error, 1)
	go func() { _, err := srv.Get(context.Background(), "fast"); fast <- err }()
	select {
	case err := <-fast:
		if err != nil {
			t.Fatalf("fast get: %v", err)
		}
	case <-time.After(2 * time.Second):
		t.Fatal("Get of another plan waited behind the slow build: the lock is held across Build")
	}
	close(src.gate)
	if err := <-slow; err != nil {
		t.Fatalf("slow get: %v", err)
	}
	if _, err := srv.Get(context.Background(), "slow"); err != nil {
		t.Fatalf("cached slow get: %v", err)
	}
	if src.calls("slow") != 1 {
		t.Fatalf("slow builds = %d, want 1 (the second get is cached)", src.calls("slow"))
	}
}

// gatedSource answers any plan; "slow" waits in Plan until gate closes.
type gatedSource struct {
	gate    chan struct{}
	reached chan struct{}
	once    sync.Once
	mu      sync.Mutex
	built   map[string]int
}

func (g *gatedSource) calls(name string) int {
	g.mu.Lock()
	defer g.mu.Unlock()
	return g.built[name]
}

func (g *gatedSource) Plan(_ context.Context, name string) (PlanRow, bool, error) {
	if name == "slow" {
		g.once.Do(func() { close(g.reached) })
		<-g.gate
	}
	g.mu.Lock()
	if g.built == nil {
		g.built = map[string]int{}
	}
	g.built[name]++
	g.mu.Unlock()
	return PlanRow{Plan: name, Map: 249}, true, nil
}

func (g *gatedSource) Legs(context.Context, string) ([]LegRow, error) { return nil, nil }

func (g *gatedSource) Steps(context.Context, string) ([]StepRow, error) { return nil, nil }

func (g *gatedSource) Templates(context.Context, []uint32) ([]TemplateRow, error) { return nil, nil }

func (g *gatedSource) Spawns(context.Context, uint32, []uint32) ([]SpawnRow, error) { return nil, nil }

func TestHandler(t *testing.T) {
	do := func(h http.Handler, path string) (*httptest.ResponseRecorder, string) {
		t.Helper()
		rec := httptest.NewRecorder()
		h.ServeHTTP(rec, httptest.NewRequest(http.MethodGet, path, nil))
		body, _ := io.ReadAll(rec.Body)
		return rec, string(body)
	}

	src := loadFixture(t)
	src.missing = map[string]bool{"no_such": true}
	mux := http.NewServeMux()
	mux.HandleFunc("GET /api/v1/plans/{plan}", Handler(NewService(src)))

	rec, body := do(mux, "/api/v1/plans/onyxias_lair")
	if rec.Code != http.StatusOK {
		t.Fatalf("ok case status = %d, body %s", rec.Code, body)
	}
	if ct := rec.Header().Get("Content-Type"); ct != "application/json" {
		t.Fatalf("content type = %q", ct)
	}
	if !strings.Contains(body, `"steps_total":29`) || !strings.Contains(body, `"stand":true`) {
		t.Fatalf("body lacks the plan: %s", body)
	}

	for _, path := range []string{"/api/v1/plans/Onyxia", "/api/v1/plans/a.b"} {
		rec, _ = do(mux, path)
		if rec.Code != http.StatusBadRequest {
			t.Fatalf("%s status = %d, want 400", path, rec.Code)
		}
	}
	rec, _ = do(mux, "/api/v1/plans/no_such")
	if rec.Code != http.StatusNotFound {
		t.Fatalf("missing plan status = %d, want 404", rec.Code)
	}

	nilMux := http.NewServeMux()
	nilMux.HandleFunc("GET /api/v1/plans/{plan}", Handler(nil))
	rec, _ = do(nilMux, "/api/v1/plans/onyxias_lair")
	if rec.Code != http.StatusServiceUnavailable {
		t.Fatalf("nil service status = %d, want 503", rec.Code)
	}

	errMux := http.NewServeMux()
	errMux.HandleFunc("GET /api/v1/plans/{plan}", Handler(NewService(&memSource{srcErr: errors.New("boom")})))
	rec, body = do(errMux, "/api/v1/plans/onyxias_lair")
	if rec.Code != http.StatusServiceUnavailable {
		t.Fatalf("erroring source status = %d, want 503", rec.Code)
	}
	if body != `{"error":"world database unavailable"}` {
		t.Fatalf("erroring source body = %q", body)
	}
}

func TestQueries(t *testing.T) {
	if !strings.Contains(QuerySteps, "ORDER BY `leg`, `seq`") {
		t.Fatalf("QuerySteps = %s", QuerySteps)
	}
	if !strings.HasSuffix(QueryPlan, "= ?") {
		t.Fatalf("QueryPlan = %s", QueryPlan)
	}
	if got := inMarks(3); got != "?,?,?" {
		t.Fatalf("inMarks(3) = %q", got)
	}
	if !strings.Contains(QuerySpawns, "LIMIT 400") {
		t.Fatalf("QuerySpawns = %s", QuerySpawns)
	}
	for _, q := range []string{QueryPlan, QueryLegs, QuerySteps, QueryTemplates, QuerySpawns} {
		if strings.Contains(q, "'") {
			t.Fatalf("query contains a quoted literal: %s", q)
		}
	}
}

// TestWakeTable pins the scripted wakes served beside the plan: the data file is the only place a boss's wake radius lives.
func TestWakeTable(t *testing.T) {
	if got := wakeOf(10184); got != 62 {
		t.Fatalf("wakeOf(10184) = %v, want 62", got)
	}
	if got := wakeOf(12129); got != 0 {
		t.Fatalf("wakeOf(12129) = %v, want 0", got)
	}
	table := loadWakes([]byte(`{"1": {"yd": 40, "why": "trigger"}, "2": {"yd": 0, "why": "none"}, "x": {"yd": 9}, "3": {"yd": -5}}`))
	if len(table) != 1 || table[1].Yd != 40 || table[1].Why != "trigger" {
		t.Fatalf("loadWakes = %+v", table)
	}
	if got := len(loadWakes([]byte("{"))); got != 0 {
		t.Fatalf("loadWakes of broken json = %d rows, want 0", got)
	}
}

// TestBuildWakeYd pins wake_yd on the wire: the Onyxia boss step and her spawn carry the scripted wake, the warders do not.
func TestBuildWakeYd(t *testing.T) {
	p, err := Build(context.Background(), loadFixture(t), "onyxias_lair")
	if err != nil {
		t.Fatalf("Build: %v", err)
	}
	if got := p.Steps[28].WakeYd; got != 62 {
		t.Fatalf("boss step wake_yd = %v, want 62", got)
	}
	if got := p.Steps[1].WakeYd; got != 0 {
		t.Fatalf("warder pull step wake_yd = %v, want 0", got)
	}
	for _, m := range p.Mobs {
		want := 0.0
		if m.E == 10184 {
			want = 62
		}
		if m.WakeYd != want {
			t.Fatalf("mob %d wake_yd = %v, want %v", m.GUID, m.WakeYd, want)
		}
	}
}

func loadFixtureNamed(t *testing.T, name string) *memSource {
	t.Helper()
	raw, err := os.ReadFile(filepath.Join("testdata", name+".rows.json"))
	if err != nil {
		t.Fatalf("read fixture: %v", err)
	}
	var fx fixtureFile
	if err := json.Unmarshal(raw, &fx); err != nil {
		t.Fatalf("decode fixture: %v", err)
	}
	return &memSource{
		planRow:   fx.Plan,
		found:     true,
		legs:      fx.Legs,
		steps:     fx.Steps,
		spawns:    fx.Spawns,
		templates: fx.Templates,
	}
}

// TestBuildMoltenCore is the non-Onyxia case: a raid plan with several legs and several bosses keeps them all, numbers its
// steps across the legs and marks each leg's own stands and packs.
func TestBuildMoltenCore(t *testing.T) {
	p, err := Build(context.Background(), loadFixtureNamed(t, "molten_core"), "molten_core")
	if err != nil {
		t.Fatalf("Build: %v", err)
	}
	if p.Map != 409 || p.StepsTotal != 45 || len(p.Steps) != 45 {
		t.Fatalf("map/steps = %d/%d", p.Map, p.StepsTotal)
	}
	if len(p.Legs) != 2 || p.Legs[0].Name != "lucifron" || p.Legs[1].Name != "magmadar" {
		t.Fatalf("legs = %+v", p.Legs)
	}
	var bosses, stands, packs []int
	for _, s := range p.Steps {
		switch s.K {
		case "boss":
			bosses = append(bosses, s.I)
			packs = append(packs, s.I)
		case "pull":
			packs = append(packs, s.I)
		}
		if s.Stand {
			stands = append(stands, s.I)
		}
		if s.I != p.Steps[s.I-1].I || s.Leg != p.Steps[s.I-1].Leg {
			t.Fatalf("step %d out of order: %+v", s.I, s)
		}
	}
	if !reflect.DeepEqual(bosses, []int{38, 44}) || len(packs) != 20 {
		t.Fatalf("bosses = %v, packs = %d", bosses, len(packs))
	}
	if got := len(stands); got != 20 {
		t.Fatalf("stands = %v", stands)
	}
	if p.Steps[37].E != 12118 || p.Steps[37].N != "" || !reflect.DeepEqual(p.Steps[37].Entries, []uint32{12119}) {
		t.Fatalf("boss step 38 = %+v", p.Steps[37])
	}
	if p.Steps[38].Leg != 1 || p.Steps[38].Seq != 0 || p.Steps[38].I != 39 {
		t.Fatalf("first step of leg 1 = %+v", p.Steps[38])
	}
	if p.Steps[44].K != "open" || p.Steps[44].Leg != 1 {
		t.Fatalf("last step = %+v", p.Steps[44])
	}
	if len(p.Mobs) != 0 {
		t.Fatalf("mobs = %+v, want none (the prep carries no spawns)", p.Mobs)
	}
}

func TestBuildMoltenCoreGolden(t *testing.T) {
	p, err := Build(context.Background(), loadFixtureNamed(t, "molten_core"), "molten_core")
	if err != nil {
		t.Fatalf("Build: %v", err)
	}
	got, err := json.MarshalIndent(p, "", " ")
	if err != nil {
		t.Fatalf("marshal: %v", err)
	}
	path := filepath.Join("testdata", "molten_core.plan.json")
	if *update {
		if err := os.WriteFile(path, got, 0o644); err != nil {
			t.Fatalf("write golden: %v", err)
		}
		return
	}
	want, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("read golden (run with -update): %v", err)
	}
	if !bytes.Equal(got, want) {
		t.Fatalf("golden mismatch\n got: %s\nwant: %s", got, want)
	}
}

// waitSource holds a read until its context ends while wait is set, the way a database read does when its caller gives up.
type waitSource struct {
	wait atomic.Bool
	seen chan error
}

func (w *waitSource) Plan(ctx context.Context, name string) (PlanRow, bool, error) {
	if w.wait.Load() {
		<-ctx.Done()
		w.seen <- ctx.Err()
		return PlanRow{}, false, ctx.Err()
	}
	return PlanRow{Plan: name, Map: 249}, true, nil
}

func (w *waitSource) Legs(context.Context, string) ([]LegRow, error) { return nil, nil }

func (w *waitSource) Steps(context.Context, string) ([]StepRow, error) { return nil, nil }

func (w *waitSource) Templates(context.Context, []uint32) ([]TemplateRow, error) { return nil, nil }

func (w *waitSource) Spawns(context.Context, uint32, []uint32) ([]SpawnRow, error) { return nil, nil }

// cancelOnceSource blocks its first Plan until that caller's context ends, then answers every later Plan: the shape of a
// leader giving up on a slow read while a waiter stays.
type cancelOnceSource struct {
	calls   atomic.Int32
	started chan struct{}
}

func (s *cancelOnceSource) Plan(ctx context.Context, name string) (PlanRow, bool, error) {
	if s.calls.Add(1) == 1 {
		close(s.started)
		<-ctx.Done()
		return PlanRow{}, false, ctx.Err()
	}
	return PlanRow{Plan: name, Map: 249}, true, nil
}

func (s *cancelOnceSource) Legs(context.Context, string) ([]LegRow, error) { return nil, nil }

func (s *cancelOnceSource) Steps(context.Context, string) ([]StepRow, error) { return nil, nil }

func (s *cancelOnceSource) Templates(context.Context, []uint32) ([]TemplateRow, error) {
	return nil, nil
}

func (s *cancelOnceSource) Spawns(context.Context, uint32, []uint32) ([]SpawnRow, error) {
	return nil, nil
}

// TestGetRetriesAWaiterWhoseLeaderWasCancelled: the leader's own context ending cancels the shared build, which says nothing
// about the world database. A waiter whose own context is live asks once more instead of taking that cancellation.
func TestGetRetriesAWaiterWhoseLeaderWasCancelled(t *testing.T) {
	src := &cancelOnceSource{started: make(chan struct{})}
	srv := NewService(src)
	leaderCtx, cancelLeader := context.WithCancel(context.Background())
	defer cancelLeader()
	leader := make(chan error, 1)
	go func() {
		_, err := srv.Get(leaderCtx, "onyxias_lair")
		leader <- err
	}()
	<-src.started
	type answer struct {
		p   *Plan
		err error
	}
	waiter := make(chan answer, 1)
	go func() {
		p, err := srv.Get(context.Background(), "onyxias_lair")
		waiter <- answer{p, err}
	}()
	time.Sleep(50 * time.Millisecond) // the waiter is in the in-flight select
	cancelLeader()
	if err := <-leader; !errors.Is(err, context.Canceled) {
		t.Fatalf("leader Get = %v, want its own cancellation", err)
	}
	select {
	case got := <-waiter:
		if got.err != nil || got.p == nil {
			t.Fatalf("waiter Get = %v, %v; a live waiter took the leader's cancellation", got.p, got.err)
		}
	case <-time.After(2 * time.Second):
		t.Fatal("the waiter never answered after its leader was cancelled")
	}
	if _, ok := srv.inflight["onyxias_lair"]; ok {
		t.Fatal("the retry left its build in flight")
	}
}

// TestGetStopsWithItsCaller pins the cancellation chain: the caller's context ends the world-database read, and a read the
// caller abandoned is not cached as a database failure.
func TestGetStopsWithItsCaller(t *testing.T) {
	src := &waitSource{seen: make(chan error, 1)}
	src.wait.Store(true)
	srv := NewService(src)
	ctx, cancel := context.WithTimeout(context.Background(), 50*time.Millisecond)
	defer cancel()
	done := make(chan error, 1)
	go func() {
		_, err := srv.Get(ctx, "onyxias_lair")
		done <- err
	}()
	select {
	case err := <-done:
		if !errors.Is(err, context.DeadlineExceeded) {
			t.Fatalf("Get = %v, want the caller's deadline", err)
		}
	case <-time.After(2 * time.Second):
		t.Fatal("Get waited for a read its caller had given up on")
	}
	if err := <-src.seen; !errors.Is(err, context.DeadlineExceeded) {
		t.Fatalf("the read saw %v, want the caller's deadline", err)
	}
	src.wait.Store(false)
	if p, err := srv.Get(context.Background(), "onyxias_lair"); err != nil || p == nil {
		t.Fatalf("Get after the cancelled read = %v, %v; the cancellation was cached", p, err)
	}
}
