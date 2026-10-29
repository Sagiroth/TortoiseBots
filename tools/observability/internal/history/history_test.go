package history

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"reflect"
	"strings"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"tortoise-observability/internal/combat"
)

// fakeClock is the writer's injected clock: the six-second wait and the daily rotation are driven by tests through it.
type fakeClock struct {
	mu sync.Mutex
	t  time.Time
}

func (c *fakeClock) Now() time.Time {
	c.mu.Lock()
	defer c.mu.Unlock()
	return c.t
}

func (c *fakeClock) Add(d time.Duration) {
	c.mu.Lock()
	c.t = c.t.Add(d)
	c.mu.Unlock()
}

// stats is the store's Metrics sink: the counters a deployment reads from Prometheus, here as plain numbers.
type stats struct {
	mu      sync.Mutex
	written int
	broken  int
	dropped map[string]int
}

func (s *stats) HistoryWritten() { s.mu.Lock(); s.written++; s.mu.Unlock() }

func (s *stats) HistoryBroken(n int) { s.mu.Lock(); s.broken += n; s.mu.Unlock() }

func (s *stats) HistoryDropped(reason string) {
	s.mu.Lock()
	s.dropped[reason]++
	s.mu.Unlock()
}

func (s *stats) get(reason string) int {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.dropped[reason]
}

var t0 = time.Date(2026, 10, 28, 12, 0, 0, 0, time.UTC)

// mkFight builds a fight the way the module sends it: a small one, so the byte caps stay easy to reason about.
func mkFight(id, commander string, start int64) combat.Fight {
	return combat.Fight{
		ID: id, Commander: commander, Group: 1, Map: 249, Plan: "onyxias_lair", Leg: "A", Step: 21, Kind: "commander",
		Start: start, DurMS: 60000, Outcome: "kill", Boss: 10184, BossName: "Onyxia",
		Engaged: []combat.EngagedEntry{{E: 12129, N: "Onyxian Warder", C: 2}},
		Bots:    []combat.FightBot{{Name: "Tank", GUID: 808, Class: "warrior", Role: "tank", Sub: -1}},
	}
}

func open(t *testing.T, cfg Config) *Store {
	t.Helper()
	s, err := Open(cfg, func(context.Context, string, combat.Fight) int { return 0 },
		func(commander string, f combat.Fight) (int64, int64, bool) { return 0, 0, false })
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { s.Close(context.Background()) })
	return s
}

// deliver enqueues one fight and lets the writer pass its six-second wait.
func deliver(t *testing.T, s *Store, clock *fakeClock, it Item) {
	t.Helper()
	if !s.Enqueue(it) {
		t.Fatal("Enqueue refused")
	}
	clock.Add(7 * time.Second)
	s.Flush()
}

func TestWriteThenListAndFight(t *testing.T) {
	clock := &fakeClock{t: t0}
	st := &stats{dropped: map[string]int{}}
	s := open(t, Config{Dir: t.TempDir(), Now: clock.Now})
	s.SetMetrics(st)
	f1 := mkFight("f1", "Glacienne", t0.Unix()-7200) // 10:00 that day
	f2 := mkFight("f2", "Glacienne", t0.Unix()-3600)
	deliver(t, s, clock, Item{Session: 7, Fight: f1, Warnings: 2, At: clock.Now()})
	deliver(t, s, clock, Item{Session: 7, Fight: f2, Warnings: 0, At: clock.Now()})

	days := s.Days("Glacienne")
	if len(days) != 1 || days[0].Day != "2026-10-28" || days[0].Fights != 2 {
		t.Fatalf("Days = %+v, want one day with two fights", days)
	}
	rows := s.List("Glacienne", "2026-10-28")
	if len(rows) != 2 || rows[0].ID != "f2" || rows[1].ID != "f1" {
		t.Fatalf("List = %+v, want f2 then f1", rows)
	}
	if rows[1].Key != combat.Key("Glacienne", 7, "f1") || rows[1].Warnings != 2 || rows[1].Deaths != 0 {
		t.Errorf("row = %+v", rows[1])
	}
	if st.written != 2 {
		t.Errorf("written = %d, want 2", st.written)
	}

	got, row, ok := s.Fight(rows[1].Key)
	if !ok {
		t.Fatal("Fight missed")
	}
	a, _ := json.Marshal(f1)
	b, _ := json.Marshal(got)
	if string(a) != string(b) {
		t.Fatalf("fight round trip differs:\n%s\n%s", a, b)
	}
	if row.Session != 7 || row.Start != f1.Start {
		t.Errorf("row = %+v", row)
	}
	if _, _, ok := s.Fight(combat.Key("Glacienne", 7, "nope")); ok {
		t.Error("Fight found a key that was never written")
	}
	if r, ok := s.Lookup(rows[0].Key); !ok || r.ID != "f2" {
		t.Errorf("Lookup = %+v, %v", r, ok)
	}
}

// A full index keeps the newest fights: one more append forgets the oldest row instead of dropping the new fight, the same
// newest-kept policy the startup scan applies.
func TestFullIndexEvictsTheOldestRow(t *testing.T) {
	clock := &fakeClock{t: t0}
	st := &stats{dropped: map[string]int{}}
	s := open(t, Config{Dir: t.TempDir(), MaxRows: 2, Now: clock.Now})
	s.SetMetrics(st)
	f1 := mkFight("f1", "Glacienne", t0.Unix()-3*86400)
	f2 := mkFight("f2", "Glacienne", t0.Unix()-2*86400)
	f3 := mkFight("f3", "Glacienne", t0.Unix()-86400)
	for _, f := range []combat.Fight{f1, f2, f3} {
		deliver(t, s, clock, Item{Session: 7, Fight: f, At: clock.Now()})
	}
	if _, ok := s.Lookup(combat.Key("Glacienne", 7, "f1")); ok {
		t.Fatal("the oldest fight's index row survived a full index")
	}
	for _, id := range []string{"f2", "f3"} {
		if _, ok := s.Lookup(combat.Key("Glacienne", 7, id)); !ok {
			t.Fatalf("the newest fight %s was dropped from a full index", id)
		}
	}
	if days := s.Days("Glacienne"); len(days) != 2 {
		t.Fatalf("Days = %+v, want the two newest nights", days)
	}
	if n := st.get("rows"); n != 0 {
		t.Errorf("dropped rows = %d, want 0: a full index makes room", n)
	}
}

func TestReopenRebuildsIndex(t *testing.T) {
	dir := t.TempDir()
	clock := &fakeClock{t: t0}
	s, err := Open(Config{Dir: dir, Now: clock.Now}, nil, nil)
	if err != nil {
		t.Fatal(err)
	}
	deliver(t, s, clock, Item{Session: 3, Fight: mkFight("f1", "Glacienne", t0.Unix()-60), At: clock.Now()})
	s.Close(context.Background())

	clock2 := &fakeClock{t: t0.Add(time.Minute)}
	s2, err := Open(Config{Dir: dir, Now: clock2.Now}, nil, nil)
	if err != nil {
		t.Fatal(err)
	}
	defer s2.Close(context.Background())
	rows := s2.List("Glacienne", "2026-10-28")
	if len(rows) != 1 {
		t.Fatalf("rows after reopen = %+v", rows)
	}
	if _, _, ok := s2.Fight(rows[0].Key); !ok {
		t.Fatal("Fight after reopen missed")
	}
	if days := s2.Days("Glacienne"); len(days) != 1 || days[0].Fights != 1 {
		t.Fatalf("Days after reopen = %+v", days)
	}
}

// TestDaysRotation pins the age rule: a file exactly HistoryDays days old is kept, the one a day older is deleted at Open.
func TestDaysRotation(t *testing.T) {
	dir := t.TempDir()
	// Two fights written on two old days, through the store itself so the lines are the store's own.
	for _, day := range []time.Time{t0.AddDate(0, 0, -31), t0.AddDate(0, 0, -30)} {
		clock := &fakeClock{t: day}
		s, err := Open(Config{Dir: dir, Now: clock.Now}, nil, nil)
		if err != nil {
			t.Fatal(err)
		}
		deliver(t, s, clock, Item{Session: 1, Fight: mkFight("f-"+day.Format("2006-01-02"), "Glacienne", day.Unix()-60), At: clock.Now()})
		s.Close(context.Background())
	}
	clock := &fakeClock{t: t0}
	s, err := Open(Config{Dir: dir, Now: clock.Now}, nil, nil)
	if err != nil {
		t.Fatal(err)
	}
	defer s.Close(context.Background())
	days := s.Days("Glacienne")
	if len(days) != 1 || days[0].Day != "2026-09-28" {
		t.Fatalf("Days after rotation = %+v, want only the day exactly %d days old", days, HistoryDays)
	}
	if _, err := os.Stat(filepath.Join(dir, DirName("Glacienne"), "2026-09-27.jsonl")); !os.IsNotExist(err) {
		t.Fatalf("the 31-day-old file survived: %v", err)
	}
	if _, err := os.Stat(filepath.Join(dir, DirName("Glacienne"), "2026-09-28.jsonl")); err != nil {
		t.Fatalf("the 30-day-old file was deleted: %v", err)
	}
}

func TestTotalCapDeletesOldestDay(t *testing.T) {
	dir := t.TempDir()
	// Two old nights, written through the store so the lines are its own.
	for _, c := range []struct {
		back int
		id   string
	}{{2, "old"}, {1, "mid"}} {
		clock := &fakeClock{t: t0.AddDate(0, 0, -c.back)}
		s, err := Open(Config{Dir: dir, Now: clock.Now}, nil, nil)
		if err != nil {
			t.Fatal(err)
		}
		deliver(t, s, clock, Item{Session: 1, Fight: mkFight(c.id, "Glacienne", clock.Now().Unix()-60), At: clock.Now()})
		s.Close(context.Background())
	}
	oldPath := filepath.Join(dir, DirName("Glacienne"), "2026-10-26.jsonl")
	midPath := filepath.Join(dir, DirName("Glacienne"), "2026-10-27.jsonl")
	oldInfo, err := os.Stat(oldPath)
	if err != nil {
		t.Fatal(err)
	}
	midInfo, err := os.Stat(midPath)
	if err != nil {
		t.Fatal(err)
	}

	clock := &fakeClock{t: t0}
	s, err := Open(Config{Dir: dir, MaxBytes: oldInfo.Size() + midInfo.Size() + 200, Now: clock.Now}, nil, nil)
	if err != nil {
		t.Fatal(err)
	}
	defer s.Close(context.Background())
	deliver(t, s, clock, Item{Session: 1, Fight: mkFight("new", "Glacienne", clock.Now().Unix()-60), At: clock.Now()})
	if _, err := os.Stat(oldPath); !os.IsNotExist(err) {
		t.Fatalf("the oldest day file survived the total cap: %v", err)
	}
	if _, err := os.Stat(midPath); err != nil {
		t.Fatalf("the second day file was deleted: %v", err)
	}
	days := s.Days("Glacienne")
	if len(days) != 2 || days[0].Day != "2026-10-28" || days[1].Day != "2026-10-27" {
		t.Fatalf("Days = %+v, want the new and the 27th day", days)
	}
}

// TestFailedTrimKeepsTheFileAndDropsNewWrites pins the cap's accounting: a day file the retention pass cannot delete stays on
// disk and in the store's accounting, and while the store is over its cap with nothing removable new fights are refused.
func TestFailedTrimKeepsTheFileAndDropsNewWrites(t *testing.T) {
	dir := t.TempDir()
	oldClock := &fakeClock{t: t0.AddDate(0, 0, -2)}
	s, err := Open(Config{Dir: dir, Now: oldClock.Now}, nil, nil)
	if err != nil {
		t.Fatal(err)
	}
	deliver(t, s, oldClock, Item{Session: 1, Fight: mkFight("old", "Glacienne", oldClock.Now().Unix()-60), At: oldClock.Now()})
	s.Close(context.Background())
	oldPath := filepath.Join(dir, DirName("Glacienne"), "2026-10-26.jsonl")
	info, err := os.Stat(oldPath)
	if err != nil {
		t.Fatal(err)
	}

	clock := &fakeClock{t: t0}
	st := &stats{dropped: map[string]int{}}
	s2, err := Open(Config{Dir: dir, MaxBytes: info.Size() + 1, Now: clock.Now}, nil, nil)
	if err != nil {
		t.Fatal(err)
	}
	defer s2.Close(context.Background())
	s2.remove = func(string) error { return errors.New("device busy") }
	s2.SetMetrics(st)

	deliver(t, s2, clock, Item{Session: 1, Fight: mkFight("new", "Glacienne", t0.Unix()-60), At: clock.Now()})
	if _, err := os.Stat(oldPath); err != nil {
		t.Fatalf("the failed trim removed the file anyway: %v", err)
	}
	if rows := s2.List("Glacienne", "2026-10-26"); len(rows) != 1 {
		t.Errorf("the failed trim dropped the index rows: %+v", rows)
	}
	if rows := s2.List("Glacienne", "2026-10-28"); len(rows) != 0 {
		t.Errorf("a fight was written past the total cap: %+v", rows)
	}
	if got := st.get("total_cap"); got != 1 {
		t.Errorf("total_cap drops = %d, want 1", got)
	}

	// The store still counts the file it could not delete, so the next fight is refused too instead of growing past the cap.
	deliver(t, s2, clock, Item{Session: 1, Fight: mkFight("newer", "Glacienne", t0.Unix()-30), At: clock.Now()})
	if rows := s2.List("Glacienne", "2026-10-28"); len(rows) != 0 {
		t.Errorf("the second fight was written past the total cap: %+v", rows)
	}
	if got := st.get("total_cap"); got != 2 {
		t.Errorf("total_cap drops = %d, want 2", got)
	}
}

func TestFileCapSkips(t *testing.T) {
	dir := t.TempDir()
	clock := &fakeClock{t: t0}
	s, err := Open(Config{Dir: dir, Now: clock.Now}, nil, nil)
	if err != nil {
		t.Fatal(err)
	}
	deliver(t, s, clock, Item{Session: 1, Fight: mkFight("f1", "Glacienne", t0.Unix()-60), At: clock.Now()})
	s.Close(context.Background())
	path := filepath.Join(dir, DirName("Glacienne"), "2026-10-28.jsonl")
	info, err := os.Stat(path)
	if err != nil {
		t.Fatal(err)
	}

	st := &stats{dropped: map[string]int{}}
	clock2 := &fakeClock{t: t0}
	s2, err := Open(Config{Dir: dir, MaxFileBytes: info.Size() + 1, Now: clock2.Now}, nil, nil)
	if err != nil {
		t.Fatal(err)
	}
	defer s2.Close(context.Background())
	s2.SetMetrics(st)
	deliver(t, s2, clock2, Item{Session: 1, Fight: mkFight("f2", "Glacienne", t0.Unix()-30), At: clock2.Now()})
	rows := s2.List("Glacienne", "2026-10-28")
	if len(rows) != 1 || rows[0].ID != "f1" {
		t.Fatalf("rows = %+v, want only f1", rows)
	}
	if got := st.get("file_cap"); got != 1 {
		t.Errorf("file_cap drops = %d, want 1", got)
	}
	after, err := os.Stat(path)
	if err != nil {
		t.Fatal(err)
	}
	if after.Size() != info.Size() {
		t.Errorf("the file grew past its cap: %d -> %d", info.Size(), after.Size())
	}
}

func TestQueueFullDrops(t *testing.T) {
	clock := &fakeClock{t: t0}
	entered := make(chan struct{}, 1)
	release := make(chan struct{})
	st := &stats{dropped: map[string]int{}}
	s, err := Open(Config{Dir: t.TempDir(), Now: clock.Now},
		func(_ context.Context, _ string, f combat.Fight) int {
			select {
			case entered <- struct{}{}:
			default:
			}
			<-release
			return 0
		}, nil)
	if err != nil {
		t.Fatal(err)
	}
	defer s.Close(context.Background())
	s.SetMetrics(st)
	if !s.Enqueue(Item{Session: 1, Fight: mkFight("block", "Glacienne", t0.Unix()-60), At: t0.Add(-time.Minute)}) {
		t.Fatal("the first Enqueue was refused")
	}
	<-entered // the writer is inside the pack lookup: the queue is what is left
	for i := range HistoryQueue {
		if !s.Enqueue(Item{Session: 1, Fight: mkFight(fmt.Sprintf("q%d", i), "Glacienne", t0.Unix()-60), At: t0}) {
			t.Fatalf("enqueue %d refused before the queue was full", i)
		}
	}
	if s.Enqueue(Item{Session: 1, Fight: mkFight("overflow", "Glacienne", t0.Unix()-60), At: t0}) {
		t.Fatal("the 65th Enqueue was accepted")
	}
	if got := st.get("queue"); got != 1 {
		t.Errorf("queue drops = %d, want 1", got)
	}
	close(release)
}

func TestBrokenLineSkipped(t *testing.T) {
	dir := t.TempDir()
	clock := &fakeClock{t: t0}
	s, err := Open(Config{Dir: dir, Now: clock.Now}, nil, nil)
	if err != nil {
		t.Fatal(err)
	}
	deliver(t, s, clock, Item{Session: 1, Fight: mkFight("f1", "Glacienne", t0.Unix()-60), At: clock.Now()})
	s.Close(context.Background())
	path := filepath.Join(dir, DirName("Glacienne"), "2026-10-28.jsonl")
	f, err := os.OpenFile(path, os.O_APPEND|os.O_WRONLY, 0600)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := f.WriteString("{not json\n"); err != nil {
		t.Fatal(err)
	}
	f.Close()

	st := &stats{dropped: map[string]int{}}
	s2, err := Open(Config{Dir: dir, Now: clock.Now}, nil, nil)
	if err != nil {
		t.Fatal(err)
	}
	defer s2.Close(context.Background())
	s2.SetMetrics(st)
	if rows := s2.List("Glacienne", "2026-10-28"); len(rows) != 1 {
		t.Fatalf("rows = %+v, want the one good line", rows)
	}
	if st.broken != 1 {
		t.Errorf("broken = %d, want 1", st.broken)
	}
}

func TestDirNameSanitizesAndSuffixes(t *testing.T) {
	for _, c := range []struct{ name, want string }{
		{"Glacienne", "Glacienne-3053d6df"},
		{"offline #12", "offline__12-c2ae3b66"},
		{"a/b", "a_b-3a8e75c1"},
		{"a_b", "a_b-1ba46871"},
	} {
		if got := DirName(c.name); got != c.want {
			t.Errorf("DirName(%q) = %q, want %q", c.name, got, c.want)
		}
	}
	if DirName("a/b") == DirName("a_b") {
		t.Error("two names that sanitize alike share a directory")
	}
}

func TestPackKeyRules(t *testing.T) {
	clock := &fakeClock{t: t0}
	packs := map[string]int{"boss": 29, "step": 0, "planboss": 0, "engaged": 0, "none": 0}
	s, err := Open(Config{Dir: t.TempDir(), Now: clock.Now},
		func(_ context.Context, _ string, f combat.Fight) int { return packs[f.ID] }, nil)
	if err != nil {
		t.Fatal(err)
	}
	defer s.Close(context.Background())
	mk := func(id, plan string, boss uint32, step uint32, engaged ...uint32) combat.Fight {
		f := mkFight(id, "Glacienne", t0.Unix()-60)
		f.Plan, f.Boss, f.Step, f.Engaged = plan, boss, step, nil
		for _, e := range engaged {
			f.Engaged = append(f.Engaged, combat.EngagedEntry{E: e})
		}
		return f
	}
	fights := []combat.Fight{
		mk("boss", "onyxias_lair", 10184, 26),
		mk("step", "onyxias_lair", 0, 18),
		mk("planboss", "", 10184, 0),
		mk("engaged", "", 0, 0, 12129, 7, 12129),
		mk("none", "", 0, 0),
	}
	for _, f := range fights {
		deliver(t, s, clock, Item{Session: 2, Fight: f, At: clock.Now()})
	}
	want := map[string]string{
		"boss":     "onyxias_lair#29",
		"step":     "onyxias_lair@18",
		"planboss": "boss:10184",
		"engaged":  "eng:7,12129",
		"none":     "",
	}
	rows := s.List("Glacienne", "2026-10-28")
	if len(rows) != len(want) {
		t.Fatalf("rows = %+v", rows)
	}
	for _, r := range rows {
		if r.PackKey != want[r.ID] {
			t.Errorf("%s: pack_key = %q, want %q", r.ID, r.PackKey, want[r.ID])
		}
	}
	if got := s.Pack("boss:10184", "", true, 50); len(got) != 1 || got[0].ID != "planboss" {
		t.Errorf("Pack across commanders = %+v", got)
	}
	if got := s.Pack("boss:10184", "Other", false, 50); len(got) != 0 {
		t.Errorf("Pack of another commander = %+v", got)
	}
}

func TestWriterWaitsForFrames(t *testing.T) {
	clock := &fakeClock{t: t0}
	var mu sync.Mutex
	calls := 0
	s, err := Open(Config{Dir: t.TempDir(), Now: clock.Now}, nil,
		func(commander string, f combat.Fight) (int64, int64, bool) {
			mu.Lock()
			calls++
			mu.Unlock()
			return 23000, 12000, true
		})
	if err != nil {
		t.Fatal(err)
	}
	defer s.Close(context.Background())
	if !s.Enqueue(Item{Session: 1, Fight: mkFight("f1", "Glacienne", t0.Unix()-60), At: t0}) {
		t.Fatal("Enqueue refused")
	}
	s.Flush()
	if len(s.List("Glacienne", "2026-10-28")) != 0 {
		t.Fatal("the fight was written before the six-second wait")
	}
	if calls != 0 {
		t.Fatalf("lanes ran %d times before the wait", calls)
	}
	clock.Add(WriteDelay)
	s.Flush()
	rows := s.List("Glacienne", "2026-10-28")
	if len(rows) != 1 || calls != 1 {
		t.Fatalf("rows = %d, lanes calls = %d, want 1 and 1", len(rows), calls)
	}
	if rows[0].NonTankMs == nil || *rows[0].NonTankMs != 23000 || *rows[0].NonTankLongestMs != 12000 {
		t.Errorf("nontank = %v, %v", rows[0].NonTankMs, rows[0].NonTankLongestMs)
	}
}

func TestHistoryHandlers(t *testing.T) {
	clock := &fakeClock{t: t0}
	s := open(t, Config{Dir: t.TempDir(), Now: clock.Now})
	deliver(t, s, clock, Item{Session: 1, Fight: mkFight("f1", "Glacienne", t0.Unix()-60), Warnings: 1, At: clock.Now()})

	do := func(h http.HandlerFunc, target string) (int, string) {
		rec := httptest.NewRecorder()
		h(rec, httptest.NewRequest(http.MethodGet, target, nil))
		return rec.Code, rec.Body.String()
	}
	if code, body := do(DaysHandler(s), "/api/v1/combat/history/days?commander=Glacienne"); code != 200 || !strings.Contains(body, `{"days":[{"day":"2026-10-28","fights":1}]}`) {
		t.Errorf("days: %d %s", code, body)
	}
	if code, body := do(DaysHandler(s), "/api/v1/combat/history/days?commander=Nobody"); code != 200 || !strings.Contains(body, `{"days":[]}`) {
		t.Errorf("days of nobody: %d %s", code, body)
	}
	if code, body := do(ListHandler(s), "/api/v1/combat/history?commander=Glacienne&day=2026-10-28"); code != 200 ||
		!strings.Contains(body, `"hist":true`) || !strings.Contains(body, `"warnings":1`) || !strings.Contains(body, `"pack_key":"onyxias_lair@21"`) {
		t.Errorf("list: %d %s", code, body)
	}

	// A disabled history answers the empty shapes, never an error.
	if code, body := do(DaysHandler(nil), "/api/v1/combat/history/days?commander=Glacienne"); code != 200 || !strings.Contains(body, `{"days":[]}`) {
		t.Errorf("disabled days: %d %s", code, body)
	}
	if code, body := do(ListHandler(nil), "/api/v1/combat/history?commander=Glacienne&day=2026-10-28"); code != 200 || strings.TrimSpace(body) != "[]" {
		t.Errorf("disabled list: %d %s", code, body)
	}
	if code, body := do(PackHandler(nil), "/api/v1/combat/packs?pack=a"); code != 200 || !strings.Contains(body, `"rows":[]`) {
		t.Errorf("disabled packs: %d %s", code, body)
	}
}

// TestHistoryDayBadInput pins the day rule: a strict YYYY-MM-DD, so a walk over the index cannot be asked for a day the files
// never hold.
func TestHistoryDayBadInput(t *testing.T) {
	do := func(target string) (int, string) {
		rec := httptest.NewRecorder()
		ListHandler(nil)(rec, httptest.NewRequest(http.MethodGet, target, nil))
		return rec.Code, rec.Body.String()
	}
	for _, day := range []string{"", "2026-1-1", "2026-10-32x", "2026/10/28", "20261028", "2026-10-28%00"} {
		if code, body := do("/api/v1/combat/history?commander=Glacienne&day=" + day); code != 400 || !strings.Contains(body, `{"error":"bad day"}`) {
			t.Errorf("day %q: %d %s", day, code, body)
		}
	}
	if code, _ := do("/api/v1/combat/history?commander=Glacienne&day=2026-10-28"); code != 200 {
		t.Errorf("good day: %d", code)
	}
}

// TestPackRouteBadInput pins the pack key rule: 1..200 bytes and no control byte.
func TestPackRouteBadInput(t *testing.T) {
	do := func(target string) int {
		rec := httptest.NewRecorder()
		PackHandler(nil)(rec, httptest.NewRequest(http.MethodGet, target, nil))
		return rec.Code
	}
	if code := do("/api/v1/combat/packs"); code != 400 {
		t.Errorf("missing pack: %d", code)
	}
	if code := do("/api/v1/combat/packs?pack="); code != 400 {
		t.Errorf("empty pack: %d", code)
	}
	if code := do("/api/v1/combat/packs?pack=" + strings.Repeat("x", 201)); code != 400 {
		t.Errorf("201-byte pack: %d", code)
	}
	if code := do("/api/v1/combat/packs?pack=a%00b"); code != 400 {
		t.Errorf("control byte in pack: %d", code)
	}
	if code := do("/api/v1/combat/packs?pack=" + strings.Repeat("x", 200)); code != 200 {
		t.Errorf("200-byte pack: %d", code)
	}
	if code := do("/api/v1/combat/packs?pack=onyxias_lair%231"); code != 200 {
		t.Errorf("good pack: %d", code)
	}
}

func TestPackRouteRows(t *testing.T) {
	clock := &fakeClock{t: t0}
	s, err := Open(Config{Dir: t.TempDir(), Now: clock.Now}, func(context.Context, string, combat.Fight) int { return 21 }, nil)
	if err != nil {
		t.Fatal(err)
	}
	defer s.Close(context.Background())
	f1 := mkFight("f1", "Glacienne", t0.Unix()-120)
	f1.Deaths = []combat.FightDeath{{Name: "Tank"}}
	deliver(t, s, clock, Item{Session: 1, Fight: f1, Warnings: 2, At: clock.Now()})
	deliver(t, s, clock, Item{Session: 1, Fight: mkFight("f2", "Glacienne", t0.Unix()-60), At: clock.Now()})

	rec := httptest.NewRecorder()
	PackHandler(s)(rec, httptest.NewRequest(http.MethodGet, "/api/v1/combat/packs?pack=onyxias_lair%2321&commander=Glacienne", nil))
	var doc struct {
		PackKey string `json:"pack_key"`
		Rows    []struct {
			Key              string `json:"key"`
			Commander        string `json:"commander"`
			Start            int64  `json:"start"`
			Dur              uint32 `json:"dur"`
			Outcome          string `json:"outcome"`
			Deaths           int    `json:"deaths"`
			Warnings         int    `json:"warnings"`
			NonTankMs        *int64 `json:"nontank_ms"`
			NonTankLongestMs int64  `json:"nontank_longest_ms"`
			Bots             int    `json:"bots"`
		} `json:"rows"`
	}
	if err := json.Unmarshal(rec.Body.Bytes(), &doc); err != nil {
		t.Fatalf("%v: %s", err, rec.Body.String())
	}
	if doc.PackKey != "onyxias_lair#21" || len(doc.Rows) != 2 || doc.Rows[0].Key != combat.Key("Glacienne", 1, "f2") || doc.Rows[1].Deaths != 1 {
		t.Fatalf("rows = %+v", doc)
	}
	if doc.Rows[1].Warnings != 2 || doc.Rows[1].Bots != 1 || doc.Rows[0].Commander != "Glacienne" || doc.Rows[0].Dur != 60000 {
		t.Errorf("row fields = %+v", doc.Rows[1])
	}
	if !reflect.DeepEqual(doc.Rows[0].NonTankMs, (*int64)(nil)) {
		t.Errorf("nontank_ms of a fight without lanes = %v", *doc.Rows[0].NonTankMs)
	}
}

// TestEnqueueDoesNotBlock pins the UDP goroutine's cost: the pack lookup and the write run on the writer, so a slow lookup (the
// world database in a deployment) never delays the listener.
func TestEnqueueDoesNotBlock(t *testing.T) {
	clock := &fakeClock{t: t0}
	started := make(chan struct{}, 1)
	s, err := Open(Config{Dir: t.TempDir(), Now: clock.Now}, func(_ context.Context, _ string, f combat.Fight) int {
		select {
		case started <- struct{}{}:
		default:
		}
		time.Sleep(time.Second)
		return 0
	}, nil)
	if err != nil {
		t.Fatal(err)
	}
	defer s.Close(context.Background())
	first := make(chan bool, 1)
	go func() {
		first <- s.Enqueue(Item{Session: 1, Fight: mkFight("f1", "Glacienne", t0.Unix()-60), At: t0.Add(-time.Minute)})
	}()
	select {
	case ok := <-first:
		if !ok {
			t.Fatal("the first Enqueue was refused")
		}
	case <-time.After(100 * time.Millisecond):
		t.Fatal("Enqueue blocked: the pack lookup ran on the caller's goroutine")
	}
	<-started // the writer is now inside the slow lookup
	began := time.Now()
	if !s.Enqueue(Item{Session: 1, Fight: mkFight("f2", "Glacienne", t0.Unix()-30), At: t0}) {
		t.Fatal("the second Enqueue was refused")
	}
	if waited := time.Since(began); waited > 100*time.Millisecond {
		t.Fatalf("Enqueue waited %v behind the writer", waited)
	}
}

// TestConcurrentReaders runs the read routes against the writer: go test -race ./internal/history/ is the point.
func TestConcurrentReaders(t *testing.T) {
	clock := &fakeClock{t: t0}
	dir := t.TempDir()
	s, err := Open(Config{Dir: dir, Now: clock.Now}, func(context.Context, string, combat.Fight) int { return 21 },
		func(commander string, f combat.Fight) (int64, int64, bool) { return 1000, 1000, true })
	if err != nil {
		t.Fatal(err)
	}
	defer s.Close(context.Background())
	var wg sync.WaitGroup
	stop := make(chan struct{})
	for range 3 {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for {
				select {
				case <-stop:
					return
				default:
				}
				s.List("Glacienne", "2026-10-28")
				s.Days("Glacienne")
				s.Pack("onyxias_lair#21", "Glacienne", false, 50)
				if rows := s.List("Glacienne", "2026-10-28"); len(rows) > 0 {
					s.Lookup(rows[0].Key)
					s.Fight(rows[0].Key)
				}
			}
		}()
	}
	for i := range 40 {
		s.Enqueue(Item{Session: uint64(i % 3), Fight: mkFight(fmt.Sprintf("f%d", i), "Glacienne", t0.Unix()-int64(i*60)), At: clock.Now()})
		clock.Add(time.Second)
		s.Flush()
	}
	clock.Add(7 * time.Second)
	s.Flush()
	close(stop)
	wg.Wait()
	if rows := s.List("Glacienne", "2026-10-28"); len(rows) != 40 {
		t.Fatalf("%d rows, want 40", len(rows))
	}
}

// TestCloseReturnsAtTheDeadline pins the shutdown bound: a pack lookup that never returns (a hung world-database read) must not
// hold the process open, and Close reports the fight it left unwritten.
func TestCloseReturnsAtTheDeadline(t *testing.T) {
	clock := &fakeClock{t: t0}
	entered := make(chan struct{}, 1)
	release := make(chan struct{})
	s, err := Open(Config{Dir: t.TempDir(), Now: clock.Now},
		func(_ context.Context, _ string, _ combat.Fight) int {
			select {
			case entered <- struct{}{}:
			default:
			}
			<-release
			return 0
		}, nil)
	if err != nil {
		t.Fatal(err)
	}
	// Released only after the bound below: a Close that waits for the writer takes those two seconds and fails.
	time.AfterFunc(2*time.Second, func() { close(release) })
	if !s.Enqueue(Item{Session: 1, Fight: mkFight("f1", "Glacienne", t0.Unix()-60), At: t0.Add(-time.Minute)}) {
		t.Fatal("Enqueue refused")
	}
	<-entered // the writer is inside the lookup
	deadline, cancel := context.WithTimeout(context.Background(), 100*time.Millisecond)
	defer cancel()
	began := time.Now()
	left := s.Close(deadline)
	if waited := time.Since(began); waited > time.Second {
		t.Fatalf("Close waited %v for a stuck lookup, want the deadline", waited)
	}
	if left != 1 {
		t.Fatalf("Close reported %d unflushed fights, want 1", left)
	}
}

// TestCloseCancelsTheWriterLookup: the lookup gets the writer's context, so the deadline releases it instead of leaving a
// goroutine behind on the world database.
func TestCloseCancelsTheWriterLookup(t *testing.T) {
	clock := &fakeClock{t: t0}
	entered := make(chan struct{}, 1)
	observed := make(chan error, 1)
	s, err := Open(Config{Dir: t.TempDir(), Now: clock.Now},
		func(ctx context.Context, _ string, _ combat.Fight) int {
			select {
			case entered <- struct{}{}:
			default:
			}
			<-ctx.Done()
			observed <- ctx.Err()
			return 0
		}, nil)
	if err != nil {
		t.Fatal(err)
	}
	if !s.Enqueue(Item{Session: 1, Fight: mkFight("f1", "Glacienne", t0.Unix()-60), At: t0.Add(-time.Minute)}) {
		t.Fatal("Enqueue refused")
	}
	<-entered
	deadline, cancel := context.WithTimeout(context.Background(), 100*time.Millisecond)
	defer cancel()
	if left := s.Close(deadline); left != 1 {
		t.Fatalf("Close reported %d unflushed fights, want 1", left)
	}
	select {
	case err := <-observed:
		if !errors.Is(err, context.Canceled) {
			t.Fatalf("lookup context ended with %v, want a cancelled writer", err)
		}
	case <-time.After(2 * time.Second):
		t.Fatal("the lookup was not cancelled at the deadline: the writer stays on the database read")
	}
}

// TestShortAppendIsTruncated pins the repair: a write that stops halfway leaves a fragment, and the next fight must not be glued
// to it.
func TestShortAppendIsTruncated(t *testing.T) {
	dir := t.TempDir()
	clock := &fakeClock{t: t0}
	s, err := Open(Config{Dir: dir, Now: clock.Now}, nil, nil)
	if err != nil {
		t.Fatal(err)
	}
	defer s.Close(context.Background())
	var short atomic.Bool
	short.Store(true)
	s.writeFn = func(f *os.File, p []byte) (int, error) {
		if short.Load() {
			return f.Write(p[:len(p)/2])
		}
		return f.Write(p)
	}
	deliver(t, s, clock, Item{Session: 1, Fight: mkFight("half", "Glacienne", t0.Unix()-120), At: clock.Now()})
	short.Store(false)
	deliver(t, s, clock, Item{Session: 2, Fight: mkFight("whole", "Glacienne", t0.Unix()-60), At: clock.Now()})

	path := filepath.Join(dir, DirName("Glacienne"), "2026-10-28.jsonl")
	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	if n := bytes.Count(data, []byte("\n")); n != 1 {
		t.Fatalf("the file holds %d lines, want the one whole line: %q", n, data)
	}
	var ln line
	if err := json.Unmarshal(bytes.TrimSuffix(data, []byte("\n")), &ln); err != nil {
		t.Fatalf("the line is not whole: %q: %v", data, err)
	}
	if ln.Fight.ID != "whole" {
		t.Fatalf("line = %q, want the fight after the fragment", ln.Fight.ID)
	}
	if rows := s.List("Glacienne", "2026-10-28"); len(rows) != 1 || rows[0].ID != "whole" {
		t.Fatalf("rows = %+v, want only the whole fight", rows)
	}

	s.Close(context.Background())
	s2, err := Open(Config{Dir: dir, Now: clock.Now}, nil, nil)
	if err != nil {
		t.Fatal(err)
	}
	defer s2.Close(context.Background())
	if rows := s2.List("Glacienne", "2026-10-28"); len(rows) != 1 || rows[0].ID != "whole" {
		t.Fatalf("rows after reopen = %+v, want the whole fight", rows)
	}
}

// TestUnrepairableAppendStartsANewFile: when even the truncate fails, that day's later fights go to a fresh file beside the
// broken one, the broken one is counted once, and a restart indexes both.
func TestUnrepairableAppendStartsANewFile(t *testing.T) {
	dir := t.TempDir()
	clock := &fakeClock{t: t0}
	s, err := Open(Config{Dir: dir, Now: clock.Now}, nil, nil)
	if err != nil {
		t.Fatal(err)
	}
	defer s.Close(context.Background())
	st := &stats{dropped: map[string]int{}}
	s.SetMetrics(st)
	s.writeFn = func(*os.File, []byte) (int, error) { return 0, errors.New("no space left on device") }
	s.truncate = func(string, int64) error { return errors.New("read-only file system") }
	deliver(t, s, clock, Item{Session: 1, Fight: mkFight("lost", "Glacienne", t0.Unix()-120), At: clock.Now()})
	if got := st.get("file_broken"); got != 1 {
		t.Fatalf("file_broken = %d, want 1", got)
	}

	s.writeFn = func(f *os.File, p []byte) (int, error) { return f.Write(p) }
	s.truncate = os.Truncate
	deliver(t, s, clock, Item{Session: 2, Fight: mkFight("kept", "Glacienne", t0.Unix()-60), At: clock.Now()})

	commanderDir := filepath.Join(dir, DirName("Glacienne"))
	alt := filepath.Join(commanderDir, "2026-10-28-1.jsonl")
	data, err := os.ReadFile(alt)
	if err != nil {
		t.Fatalf("the day's later fight did not go to a new file: %v", err)
	}
	var ln line
	if err := json.Unmarshal(bytes.TrimSuffix(data, []byte("\n")), &ln); err != nil || ln.Fight.ID != "kept" {
		t.Fatalf("fallback file = %q (%v), want the kept fight", data, err)
	}
	if info, err := os.Stat(filepath.Join(commanderDir, "2026-10-28.jsonl")); err != nil || info.Size() != 0 {
		t.Errorf("the abandoned file changed: %v, %d bytes", err, info.Size())
	}
	if rows := s.List("Glacienne", "2026-10-28"); len(rows) != 1 || rows[0].ID != "kept" {
		t.Fatalf("rows = %+v, want only the kept fight", rows)
	}

	s.Close(context.Background())
	s2, err := Open(Config{Dir: dir, Now: clock.Now}, nil, nil)
	if err != nil {
		t.Fatal(err)
	}
	defer s2.Close(context.Background())
	if rows := s2.List("Glacienne", "2026-10-28"); len(rows) != 1 || rows[0].ID != "kept" {
		t.Fatalf("rows after reopen = %+v, want the fallback's fight", rows)
	}
}

// The fight log the daemon attaches when a live fight is filed survives the writer and a restart: it is part of the fight
// JSON the day file stores.
func TestFightLogRoundTrips(t *testing.T) {
	dir := t.TempDir()
	clock := &fakeClock{t: t0}
	s, err := Open(Config{Dir: dir, Now: clock.Now}, nil, nil)
	if err != nil {
		t.Fatal(err)
	}
	f := mkFight("f1", "Glacienne", t0.Unix())
	f.Log = []json.RawMessage{
		json.RawMessage(`{"k":"cast","t":1000,"id":"814","e":14507,"n":"Venoxis","s":"Poison Cloud"}`),
		json.RawMessage(`{"k":"death","t":2000,"n":"Tanky","by":"Venoxis"}`),
	}
	f.LogDropped = 3
	deliver(t, s, clock, Item{Session: 7, Fight: f, At: clock.Now()})
	s.Close(context.Background())

	clock2 := &fakeClock{t: t0.Add(time.Minute)}
	s2, err := Open(Config{Dir: dir, Now: clock2.Now}, nil, nil)
	if err != nil {
		t.Fatal(err)
	}
	defer s2.Close(context.Background())
	got, _, ok := s2.Fight(combat.Key("Glacienne", 7, "f1"))
	if !ok {
		t.Fatal("Fight after reopen missed")
	}
	if len(got.Log) != len(f.Log) || got.LogDropped != 3 {
		t.Fatalf("log after reopen: %d events (dropped %d), want %d (dropped 3)", len(got.Log), got.LogDropped, len(f.Log))
	}
	for i := range f.Log {
		if string(got.Log[i]) != string(f.Log[i]) {
			t.Fatalf("log[%d] = %s, want %s", i, got.Log[i], f.Log[i])
		}
	}
}
