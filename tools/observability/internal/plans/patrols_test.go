package plans

import (
	"bytes"
	"encoding/json"
	"fmt"
	"math"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

// installPatrols points the patrol table at dir (and at embedded, when
// non-nil) and restores the committed state when the test ends.
func installPatrols(t *testing.T, embedded map[uint32]Patrol, dir string) {
	t.Helper()
	patrolsState.Lock()
	origDir, origEmbedded, origRejected := patrolsState.dir, patrolsState.embedded, patrolsState.rejected
	if embedded != nil {
		patrolsState.embedded = embedded
		patrolsState.byGUID = copyPatrols(embedded)
	}
	patrolsState.Unlock()
	t.Cleanup(func() {
		patrolsState.Lock()
		patrolsState.dir = origDir
		patrolsState.embedded = origEmbedded
		patrolsState.byGUID = copyPatrols(origEmbedded)
		patrolsState.files = map[string]*patrolFile{}
		patrolsState.checked = time.Time{}
		patrolsState.loaded = false
		patrolsState.rejected = origRejected
		patrolsState.Unlock()
	})
	SetPatrolsDir(dir)
}

func writePatrolFile(t *testing.T, dir, name, body string) string {
	t.Helper()
	path := filepath.Join(dir, name)
	if err := os.WriteFile(path, []byte(body), 0o644); err != nil {
		t.Fatalf("write %s: %v", name, err)
	}
	return path
}

func getPlan(t *testing.T, src *memSource, name string) *Plan {
	t.Helper()
	p, err := NewService(src).Get(name)
	if err != nil {
		t.Fatalf("Get(%s): %v", name, err)
	}
	return p
}

func mobByGUID(t *testing.T, p *Plan, guid uint32) *Mob {
	t.Helper()
	for i := range p.Mobs {
		if p.Mobs[i].GUID == guid {
			return &p.Mobs[i]
		}
	}
	t.Fatalf("plan %s has no mob with guid %d", p.Plan, guid)
	return nil
}

// A trash-check --dash-out row is keyed by the leader's spawn guid and reaches that spawn only.
func TestPatrolsDashOutAttachesByGUID(t *testing.T) {
	dir := t.TempDir()
	writePatrolFile(t, dir, "onyxia.json", `{
 "52050": {"entries": [12129], "path": [[52.2852, -105.334, -24.8035], [52.4425, -97.069, -25.1109], [50.325, -90.0214, -25.7882]],
   "period_s": 36.21, "estimate": true, "why": "creature_movement 52050 loop", "src": "turtle-agent trash-check"}
}`)
	installPatrols(t, nil, dir)
	p := getPlan(t, loadFixture(t), "onyxias_lair")

	m := mobByGUID(t, p, 52050)
	if len(m.Path) != 3 {
		t.Fatalf("guid 52050 path points = %d, want 3", len(m.Path))
	}
	if m.Path[0] != [3]float64{52.3, -105.3, -24.8} {
		t.Fatalf("first point = %v, want [52.3 -105.3 -24.8]", m.Path[0])
	}
	if m.PeriodS != 36.21 || !m.PathEst || m.PathSrc != "turtle-agent trash-check" {
		t.Fatalf("guid 52050 patrol = period %v, est %v, src %q", m.PeriodS, m.PathEst, m.PathSrc)
	}
	for _, guid := range []uint32{52049, 52052, 52048, 52053, 47572} {
		o := mobByGUID(t, p, guid)
		if len(o.Path) != 0 || o.PeriodS != 0 || o.PathSrc != "" || o.PathEst {
			t.Fatalf("guid %d carries a patrol without a row: %+v", guid, *o)
		}
	}
}

// A tb-patrols recording reaches every spawn guid of the formation, and also its id when that id is decimal.
func TestPatrolsRecordingAttachesToEverySpawnGUID(t *testing.T) {
	dir := t.TempDir()
	writePatrolFile(t, dir, "record.json", `{"format": "tb-patrols", "version": 1, "map": 249, "source": "record", "patrols": [
 {"id": "52052", "guids": ["0x0000000000001234"], "spawn_guids": [52050, 52049], "entries": [12129], "name": "Warders",
  "path": [{"x": 1, "y": 2, "z": 3}, {"x": 4, "y": 5, "z": 6}], "period_s": 30, "period_estimate": true, "source": "patrol-record"},
 {"id": "warder_alpha", "spawn_guids": [52048], "path": [{"x": 7, "y": 8, "z": 9}, {"x": 10, "y": 11, "z": 12}],
  "period_s": 40, "source": "patrol-record"}]}`)
	installPatrols(t, nil, dir)
	p := getPlan(t, loadFixture(t), "onyxias_lair")

	for _, guid := range []uint32{52050, 52049, 52052} {
		m := mobByGUID(t, p, guid)
		if len(m.Path) != 2 || m.PeriodS != 30 || !m.PathEst || m.PathSrc != "patrol-record" {
			t.Fatalf("guid %d patrol = %+v, want the formation's loop", guid, *m)
		}
		if m.Path[0] != [3]float64{1, 2, 3} {
			t.Fatalf("guid %d first point = %v", guid, m.Path[0])
		}
	}
	m := mobByGUID(t, p, 52048)
	if len(m.Path) != 2 || m.PeriodS != 40 || m.PathEst || m.PathSrc != "patrol-record" {
		t.Fatalf("guid 52048 patrol = %+v, want its own loop", *m)
	}
	if n := len(p.Mobs); n != 6 {
		t.Fatalf("mobs = %d, want 6 (a recording adds no mobs)", n)
	}
}

// A folder recording beats a folder dash-out row, which beats the embedded row.
func TestPatrolsPrecedence(t *testing.T) {
	dir := t.TempDir()
	embedded := map[uint32]Patrol{52050: {
		Path:    [][3]float64{{1, 1, 1}, {2, 2, 2}},
		PeriodS: 11, Src: "embedded",
	}}
	installPatrols(t, embedded, dir)

	period := func() float64 {
		t.Helper()
		p := getPlan(t, loadFixture(t), "onyxias_lair")
		return mobByGUID(t, p, 52050).PeriodS
	}
	src := func() string {
		t.Helper()
		p := getPlan(t, loadFixture(t), "onyxias_lair")
		return mobByGUID(t, p, 52050).PathSrc
	}
	if got := period(); got != 11 || src() != "embedded" {
		t.Fatalf("embedded row = %v s, src %q, want 11 s, embedded", got, src())
	}

	writePatrolFile(t, dir, "a_dashout.json", `{"52050": {"path": [[1, 2, 3], [4, 5, 6]], "period_s": 20, "src": "dash-out"}}`)
	SetPatrolsDir(dir)
	if got := period(); got != 20 || src() != "dash-out" {
		t.Fatalf("dash-out row = %v s, src %q, want 20 s, dash-out", got, src())
	}

	writePatrolFile(t, dir, "b_record.json", `{"format": "tb-patrols", "version": 1, "map": 249, "patrols": [
 {"id": "52050", "spawn_guids": [52050], "path": [{"x": 1, "y": 2, "z": 3}, {"x": 4, "y": 5, "z": 6}], "period_s": 30, "source": "recording"}]}`)
	SetPatrolsDir(dir)
	if got := period(); got != 30 || src() != "recording" {
		t.Fatalf("recording = %v s, src %q, want 30 s, recording", got, src())
	}

	if err := os.Remove(filepath.Join(dir, "b_record.json")); err != nil {
		t.Fatalf("remove recording: %v", err)
	}
	SetPatrolsDir(dir)
	if got := period(); got != 20 || src() != "dash-out" {
		t.Fatalf("after the recording was removed = %v s, src %q, want the dash-out row", got, src())
	}
}

// A long path is decimated evenly to the cap, keeping the first point.
func TestPatrolsDecimateTo64KeepingTheFirst(t *testing.T) {
	dir := t.TempDir()
	pts := make([]string, 65)
	for i := range pts {
		pts[i] = fmt.Sprintf("[%d, 0, 0]", i)
	}
	writePatrolFile(t, dir, "long.json", fmt.Sprintf(`{"52050": {"path": [%s], "period_s": 36.2, "src": "long"}}`, strings.Join(pts, ", ")))
	installPatrols(t, nil, dir)
	p := getPlan(t, loadFixture(t), "onyxias_lair")

	m := mobByGUID(t, p, 52050)
	if len(m.Path) != patrolMaxPoints {
		t.Fatalf("points = %d, want %d", len(m.Path), patrolMaxPoints)
	}
	for k := range m.Path {
		want := float64(k * 65 / patrolMaxPoints)
		if m.Path[k][0] != want {
			t.Fatalf("point %d x = %v, want %v", k, m.Path[k][0], want)
		}
	}
}

// Rows the data cannot hold are rejected and counted: fewer than two points, a period outside 0..900 s, a non-guid key,
// a non-finite coordinate.
func TestPatrolsRejectBadRows(t *testing.T) {
	dir := t.TempDir()
	writePatrolFile(t, dir, "bad.json", `{
 "1": {"path": [[0, 0, 0], [1, 0, 0]], "period_s": 10, "src": "good"},
 "2": {"path": [[0, 0, 0]], "period_s": 10, "src": "one point"},
 "3": {"path": [[0, 0, 0], [1, 0, 0]], "period_s": 0, "src": "no period"},
 "4": {"path": [[0, 0, 0], [1, 0, 0]], "period_s": 901, "src": "period too long"},
 "not-a-guid": {"path": [[0, 0, 0], [1, 0, 0]], "period_s": 10, "src": "key"}
}`)
	installPatrols(t, nil, dir)
	getPlan(t, loadFixture(t), "onyxias_lair")

	patrolsState.RLock()
	table := copyPatrols(patrolsState.byGUID)
	rejected := patrolsState.rejected
	patrolsState.RUnlock()
	if len(table) != 1 {
		t.Fatalf("table = %v, want the one good row", table)
	}
	if p, ok := table[1]; !ok || p.PeriodS != 10 || p.Src != "good" {
		t.Fatalf("guid 1 = %+v", p)
	}
	if rejected != 4 {
		t.Fatalf("rejected = %d, want 4", rejected)
	}
	if _, ok := newPatrol([][3]float64{{0, 0, 0}, {math.Inf(1), 0, 0}}, 10, false, ""); ok {
		t.Fatal("a non-finite coordinate was accepted")
	}
}

// The four Onyxia warders patrol; with the recorded loops the plan's patrol mobs carry their paths and the rest do not.
func TestPatrolsOnyxiaWarders(t *testing.T) {
	installPatrols(t, nil, filepath.Join("testdata", "patrols"))
	p := getPlan(t, loadFixture(t), "onyxias_lair")

	want := map[uint32]float64{52050: 36.21, 52049: 49.74, 52052: 71.49, 52048: 44.24}
	for guid, period := range want {
		m := mobByGUID(t, p, guid)
		if !m.Patrol {
			t.Fatalf("guid %d: patrol = false, the fixture is for a patrolling warder", guid)
		}
		if len(m.Path) < 2 {
			t.Fatalf("guid %d: %d path points", guid, len(m.Path))
		}
		if m.PeriodS != period {
			t.Fatalf("guid %d: period %v, want %v", guid, m.PeriodS, period)
		}
		if !m.PathEst || !strings.HasPrefix(m.PathSrc, "turtle-agent ") {
			t.Fatalf("guid %d: est %v, src %q", guid, m.PathEst, m.PathSrc)
		}
	}
	for _, guid := range []uint32{52053, 47572} {
		if m := mobByGUID(t, p, guid); len(m.Path) != 0 || m.PeriodS != 0 || m.PathSrc != "" {
			t.Fatalf("guid %d patrols: %+v", guid, *m)
		}
	}
	if first := mobByGUID(t, p, 52050).Path[0]; first != [3]float64{52.3, -105.3, -24.8} {
		t.Fatalf("52050 first point = %v", first)
	}
}

// A file whose size or modification time changed is read again at the next check, and the plan cache is cleared for it.
func TestPatrolsReloadOnChange(t *testing.T) {
	base := time.Date(2026, 10, 29, 12, 0, 0, 0, time.UTC)
	now := base
	dir := t.TempDir()
	path := writePatrolFile(t, dir, "p.json", `{"52050": {"path": [[1, 2, 3], [4, 5, 6]], "period_s": 10, "src": "one"}}`)
	installPatrols(t, nil, dir)

	src := loadFixture(t)
	srv := NewService(src)
	srv.SetClock(func() time.Time { return now })

	period := func() float64 {
		t.Helper()
		p, err := srv.Get("onyxias_lair")
		if err != nil {
			t.Fatalf("Get: %v", err)
		}
		return mobByGUID(t, p, 52050).PeriodS
	}
	if got := period(); got != 10 {
		t.Fatalf("first read = %v s, want 10", got)
	}
	if src.planCalls != 1 {
		t.Fatalf("plan calls = %d, want 1", src.planCalls)
	}

	if err := os.WriteFile(path, []byte(`{"52050": {"path": [[1, 2, 3], [4, 5, 6]], "period_s": 20, "src": "two"}}`), 0o644); err != nil {
		t.Fatalf("rewrite: %v", err)
	}
	later := base.Add(2 * time.Second)
	if err := os.Chtimes(path, later, later); err != nil {
		t.Fatalf("chtimes: %v", err)
	}
	now = base.Add(30 * time.Second)
	if got := period(); got != 10 {
		t.Fatalf("inside the check window = %v s, want the old 10", got)
	}
	if src.planCalls != 1 {
		t.Fatalf("plan calls inside the window = %d, want the cache hit", src.planCalls)
	}

	now = base.Add(61 * time.Second)
	if got := period(); got != 20 {
		t.Fatalf("after the check window = %v s, want the new 20", got)
	}
	if src.planCalls != 2 {
		t.Fatalf("plan calls after the change = %d, want a rebuild", src.planCalls)
	}
}

// A plan whose spawns have no patrol row gains no path keys, and the committed goldens do not move.
func TestPatrolsLeavePlansWithoutDataUnchanged(t *testing.T) {
	installPatrols(t, map[uint32]Patrol{1: {
		Path:    [][3]float64{{1, 2, 3}, {4, 5, 6}},
		PeriodS: 10, Src: "elsewhere",
	}}, "")

	p := getPlan(t, loadFixture(t), "onyxias_lair")
	if len(p.Mobs) != 6 {
		t.Fatalf("onyxia mobs = %d", len(p.Mobs))
	}
	got, err := json.Marshal(p)
	if err != nil {
		t.Fatalf("marshal: %v", err)
	}
	if strings.Contains(string(got), `"path":`) {
		t.Fatalf("a plan without patrol rows carries path keys: %s", got)
	}

	mc := getPlan(t, loadFixtureNamed(t, "molten_core"), "molten_core")
	got, err = json.MarshalIndent(mc, "", " ")
	if err != nil {
		t.Fatalf("marshal: %v", err)
	}
	want, err := os.ReadFile(filepath.Join("testdata", "molten_core.plan.json"))
	if err != nil {
		t.Fatalf("read golden: %v", err)
	}
	if !bytes.Equal(got, want) {
		t.Fatalf("molten core golden changed\n got: %s", got)
	}
}
