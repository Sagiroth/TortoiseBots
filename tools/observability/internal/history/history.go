// Package history keeps the dashboard's fights across nights: one JSONL file per commander per UTC day, a memory index over
// them for the list, day-picker and same-pack routes, and one writer goroutine so a finished fight never costs the UDP
// goroutine anything. The fight record itself is re-read from disk on demand; only its summary line stays in memory.
package history

import (
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"log"
	"net/http"
	"os"
	"sort"
	"strconv"
	"strings"
	"sync"
	"time"

	"tortoise-observability/internal/combat"
)

// The store's fixed policy: a month of nights, a quarter of a gigabyte across all commanders, sixteen megabytes per day file, a
// bounded index and writer queue. Over a cap whole day files go, oldest first; a full day file skips further fights that day.
const (
	HistoryDays         = 30
	HistoryMaxBytes     = 256 << 20
	HistoryMaxFileBytes = 16 << 20
	HistoryMaxRows      = 20000
	HistoryQueue        = 64

	// WriteDelay is how long a fight waits before it is written: the tracker has the fight's last frames by then, so the
	// non-tank numbers are computed over the whole fight instead of its first minute.
	WriteDelay = 6 * time.Second

	dayLayout = "2006-01-02"
	dayShape  = "0000-00-00"
)

// Config is the store's policy; zero fields take the package defaults. Dir empty is refused (main leaves the store off).
type Config struct {
	Dir          string
	Days         int
	MaxBytes     int64
	MaxFileBytes int64
	MaxRows      int
	Queue        int
	Now          func() time.Time
}

// Metrics counts the store's work; nil is a no-op. main wires the Prometheus registry.
type Metrics interface {
	HistoryWritten()
	HistoryDropped(reason string)
	HistoryBroken(n int)
}

// Row is one fight's index entry: its summary line plus where the full record sits on disk.
type Row struct {
	Key              string `json:"key"`
	Commander        string `json:"commander"`
	Session          uint64 `json:"session"`
	ID               string `json:"id"`
	Start            int64  `json:"start"`
	Dur              uint32 `json:"dur"`
	Outcome          string `json:"outcome"`
	BossName         string `json:"boss_name,omitempty"`
	Plan             string `json:"plan,omitempty"`
	Leg              string `json:"leg,omitempty"`
	Step             uint32 `json:"step,omitempty"`
	Map              uint32 `json:"map,omitempty"`
	Deaths           int    `json:"deaths"`
	Warnings         int    `json:"warnings"`
	PackKey          string `json:"pack_key,omitempty"`
	NonTankMs        *int64 `json:"nontank_ms,omitempty"`
	NonTankLongestMs *int64 `json:"nontank_longest_ms,omitempty"`
	Bots             int    `json:"bots,omitempty"`

	day  string
	file string
	off  int64
	size int
}

// DayCount is one row of the night picker.
type DayCount struct {
	Day    string `json:"day"`
	Fights int    `json:"fights"`
}

// Item is one finished fight handed to the writer.
type Item struct {
	Session  uint64
	Fight    combat.Fight
	Warnings int
	At       time.Time
}

// line is the file's record: what the index needs, the pack the fight belongs to, the non-tank numbers, and the fight exactly as
// the module sent it (no frames: a night of 3 KB frames does not belong in a month of files).
type line struct {
	V                int          `json:"v"`
	Session          uint64       `json:"session"`
	Key              string       `json:"key"`
	SavedAt          int64        `json:"saved_at"`
	PackKey          string       `json:"pack_key"`
	Pack             int          `json:"pack"`
	Engaged          []uint32     `json:"engaged"`
	NonTankMs        *int64       `json:"nontank_ms"`
	NonTankLongestMs *int64       `json:"nontank_longest_ms"`
	Warnings         int          `json:"warnings"`
	Fight            combat.Fight `json:"fight"`
}

// Store is the index plus the writer. rows and byKey sit under mu; the writer's bookkeeping (total, fileSize, skipped, rotate)
// belongs to the writer goroutine alone, which also runs the rotation.
type Store struct {
	cfg   Config
	pack  func(plan string, f combat.Fight) int
	lanes func(commander string, f combat.Fight) (int64, int64, bool)

	metricsMu sync.RWMutex
	metrics   Metrics
	broken    int

	mu    sync.RWMutex
	rows  []Row
	byKey map[string]int

	q     chan Item
	flush chan chan struct{}
	quit  chan struct{}
	once  sync.Once
	wg    sync.WaitGroup

	total      int64
	fileSize   map[string]int64
	skipped    map[string]bool
	lastRotate time.Time
}

// Open prepares the directory, rebuilds the index from the day files (newest first), deletes expired days and starts the writer.
func Open(cfg Config, pack func(plan string, f combat.Fight) int, lanes func(commander string, f combat.Fight) (int64, int64, bool)) (*Store, error) {
	cfg = withDefaults(cfg)
	if cfg.Dir == "" {
		return nil, errors.New("no history directory")
	}
	if err := os.MkdirAll(cfg.Dir, 0700); err != nil {
		return nil, err
	}
	s := &Store{
		cfg:      cfg,
		pack:     pack,
		lanes:    lanes,
		byKey:    make(map[string]int),
		fileSize: make(map[string]int64),
		skipped:  make(map[string]bool),
	}
	s.lastRotate = cfg.Now()
	if err := s.load(); err != nil {
		return nil, err
	}
	s.q = make(chan Item, cfg.Queue)
	s.flush = make(chan chan struct{})
	s.quit = make(chan struct{})
	s.wg.Add(1)
	go s.run()
	return s, nil
}

func withDefaults(cfg Config) Config {
	if cfg.Days <= 0 {
		cfg.Days = HistoryDays
	}
	if cfg.MaxBytes <= 0 {
		cfg.MaxBytes = HistoryMaxBytes
	}
	if cfg.MaxFileBytes <= 0 {
		cfg.MaxFileBytes = HistoryMaxFileBytes
	}
	if cfg.MaxRows <= 0 {
		cfg.MaxRows = HistoryMaxRows
	}
	if cfg.Queue <= 0 {
		cfg.Queue = HistoryQueue
	}
	if cfg.Now == nil {
		cfg.Now = time.Now
	}
	return cfg
}

// SetMetrics attaches the counters; nil disables them. A scan that ran at Open reports its broken lines here, the counters
// being wired after the store is opened.
func (s *Store) SetMetrics(m Metrics) {
	if s == nil {
		return
	}
	s.metricsMu.Lock()
	s.metrics = m
	broken := s.broken
	s.metricsMu.Unlock()
	if m != nil && broken > 0 {
		m.HistoryBroken(broken)
	}
}

func (s *Store) written() {
	s.metricsMu.RLock()
	m := s.metrics
	s.metricsMu.RUnlock()
	if m != nil {
		m.HistoryWritten()
	}
}

func (s *Store) dropped(reason string) {
	s.metricsMu.RLock()
	m := s.metrics
	s.metricsMu.RUnlock()
	if m != nil {
		m.HistoryDropped(reason)
	}
}

func (s *Store) brokenLine() {
	s.metricsMu.Lock()
	s.broken++
	m := s.metrics
	s.metricsMu.Unlock()
	if m != nil {
		m.HistoryBroken(1)
	}
}

// DirName is the commander's directory: every byte outside [A-Za-z0-9_-] becomes _, always suffixed with the FNV-1a 32 of the
// raw name, so two names that sanitize alike still get a directory each.
func DirName(commander string) string {
	var b strings.Builder
	b.Grow(len(commander) + 9)
	for i := 0; i < len(commander); i++ {
		c := commander[i]
		switch {
		case c >= 'a' && c <= 'z', c >= 'A' && c <= 'Z', c >= '0' && c <= '9', c == '-', c == '_':
			b.WriteByte(c)
		default:
			b.WriteByte('_')
		}
	}
	return b.String() + "-" + fmt.Sprintf("%08x", fnv1a(commander))
}

// fnv1a is FNV-1a 32 of the raw commander name.
func fnv1a(s string) uint32 {
	h := uint32(2166136261)
	for i := 0; i < len(s); i++ {
		h ^= uint32(s[i])
		h *= 16777619
	}
	return h
}

// Enqueue hands one fight to the writer. False means the queue was full and the fight was dropped (and counted).
func (s *Store) Enqueue(it Item) bool {
	select {
	case s.q <- it:
		return true
	default:
		s.dropped("queue")
		return false
	}
}

// Close drains the queue, writes what is pending and stops the writer.
func (s *Store) Close() {
	if s == nil {
		return
	}
	s.once.Do(func() {
		close(s.quit)
		s.wg.Wait()
	})
}

// Flush writes every pending fight that is due and returns: the tests' way to run the writer against a fake clock.
func (s *Store) Flush() {
	fc := make(chan struct{})
	select {
	case s.flush <- fc:
		<-fc
	case <-s.quit:
	}
}

// Days lists a commander's days, newest first, with the fights indexed per day.
func (s *Store) Days(commander string) []DayCount {
	s.mu.RLock()
	counts := map[string]int{}
	for i := range s.rows {
		if s.rows[i].Commander == commander {
			counts[s.rows[i].day]++
		}
	}
	s.mu.RUnlock()
	out := make([]DayCount, 0, len(counts))
	for day, n := range counts {
		out = append(out, DayCount{Day: day, Fights: n})
	}
	sort.Slice(out, func(i, j int) bool { return out[i].Day > out[j].Day })
	return out
}

// List returns a commander's fights of one UTC day, newest first.
func (s *Store) List(commander, day string) []Row {
	s.mu.RLock()
	out := make([]Row, 0, 16)
	for i := range s.rows {
		if s.rows[i].Commander == commander && s.rows[i].day == day {
			out = append(out, s.rows[i])
		}
	}
	s.mu.RUnlock()
	sortRows(out)
	return out
}

// Pack returns the fights of one pack key, newest first; all widens it to every commander. limit is clamped to 50.
func (s *Store) Pack(packKey, commander string, all bool, limit int) []Row {
	if limit <= 0 || limit > 50 {
		limit = 50
	}
	s.mu.RLock()
	out := make([]Row, 0, limit)
	for i := range s.rows {
		r := s.rows[i]
		if r.PackKey != packKey || (!all && r.Commander != commander) {
			continue
		}
		out = append(out, r)
	}
	s.mu.RUnlock()
	sortRows(out)
	if len(out) > limit {
		out = out[:limit]
	}
	return out
}

// Lookup returns one fight's index row (the live list annotates its rows with the stored pack key and non-tank seconds).
func (s *Store) Lookup(key string) (Row, bool) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	i, ok := s.byKey[key]
	if !ok {
		return Row{}, false
	}
	return s.rows[i], true
}

// Fight re-reads one stored fight from disk; the line's own key must match, so a file rewritten around the index never answers
// with the wrong fight.
func (s *Store) Fight(key string) (combat.Fight, Row, bool) {
	row, ok := s.Lookup(key)
	if !ok || row.file == "" || row.size <= 0 {
		return combat.Fight{}, Row{}, false
	}
	buf := make([]byte, row.size)
	f, err := os.Open(row.file)
	if err != nil {
		return combat.Fight{}, Row{}, false
	}
	n, _ := f.ReadAt(buf, row.off)
	f.Close()
	if n != len(buf) {
		return combat.Fight{}, Row{}, false
	}
	var ln line
	if err := json.Unmarshal(bytes.TrimRight(buf, "\r\n"), &ln); err != nil || ln.Key != key {
		return combat.Fight{}, Row{}, false
	}
	return ln.Fight, row, true
}

// Summary is the row as the fights list shows it.
func (r Row) Summary() combat.FightSummary {
	return combat.FightSummary{
		ID: r.ID, Key: r.Key, Session: r.Session, Start: r.Start, Dur: r.Dur, Outcome: r.Outcome, BossName: r.BossName,
		Leg: r.Leg, Step: r.Step, Deaths: r.Deaths, Warnings: r.Warnings,
		PackKey: r.PackKey, NonTankMs: r.NonTankMs, Hist: true,
	}
}

func sortRows(rows []Row) {
	sort.Slice(rows, func(i, j int) bool {
		if rows[i].Start != rows[j].Start {
			return rows[i].Start > rows[j].Start
		}
		return rows[i].Key < rows[j].Key
	})
}

func dayOf(start int64) string {
	return time.Unix(start, 0).UTC().Format(dayLayout)
}

// packKey is what "the same pack" means across nights: a plan pack step, a plan step the run waited at, a boss fight without a
// plan, else the engaged entries; nothing engaged is not grouped at all.
func packKey(f combat.Fight, pack int) string {
	if f.Plan != "" && pack > 0 {
		return f.Plan + "#" + strconv.Itoa(pack)
	}
	if f.Plan != "" {
		return f.Plan + "@" + strconv.FormatUint(uint64(f.Step), 10)
	}
	if f.Boss > 0 {
		return "boss:" + strconv.FormatUint(uint64(f.Boss), 10)
	}
	entries := engagedSorted(f.Engaged)
	if len(entries) == 0 {
		return ""
	}
	parts := make([]string, len(entries))
	for i, e := range entries {
		parts[i] = strconv.FormatUint(uint64(e), 10)
	}
	return "eng:" + strings.Join(parts, ",")
}

// engagedSorted is the fight's distinct engaged entries, smallest first.
func engagedSorted(engaged []combat.EngagedEntry) []uint32 {
	seen := make(map[uint32]bool, len(engaged))
	out := make([]uint32, 0, len(engaged))
	for _, e := range engaged {
		if e.E > 0 && !seen[e.E] {
			seen[e.E] = true
			out = append(out, e.E)
		}
	}
	sort.Slice(out, func(i, j int) bool { return out[i] < out[j] })
	return out
}

// ---- routes ---------------------------------------------------------------------------------------------------------

func writeJSON(w http.ResponseWriter, v any) {
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(v)
}

func bad(w http.ResponseWriter, msg string) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(http.StatusBadRequest)
	_, _ = w.Write([]byte(`{"error":"` + msg + `"}`))
}

// validDay is the strict YYYY-MM-DD the list takes.
func validDay(day string) bool {
	if len(day) != len(dayShape) {
		return false
	}
	for i := 0; i < len(dayShape); i++ {
		if dayShape[i] == '-' {
			if day[i] != '-' {
				return false
			}
		} else if day[i] < '0' || day[i] > '9' {
			return false
		}
	}
	return true
}

// hasControl reports a byte a pack key never has: the query is echoed in a walk over the index, and control bytes belong in no
// URL.
func hasControl(s string) bool {
	for i := 0; i < len(s); i++ {
		if s[i] < 0x20 || s[i] == 0x7f {
			return true
		}
	}
	return false
}

// DaysHandler serves GET /api/v1/combat/history/days?commander=.
func DaysHandler(s *Store) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		days := []DayCount{}
		if s != nil {
			days = s.Days(r.URL.Query().Get("commander"))
		}
		writeJSON(w, struct {
			Days []DayCount `json:"days"`
		}{days})
	}
}

// ListHandler serves GET /api/v1/combat/history?commander=&day=, newest first.
func ListHandler(s *Store) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		q := r.URL.Query()
		day := q.Get("day")
		if !validDay(day) {
			bad(w, "bad day")
			return
		}
		out := []combat.FightSummary{}
		if s != nil {
			for _, row := range s.List(q.Get("commander"), day) {
				out = append(out, row.Summary())
			}
		}
		writeJSON(w, out)
	}
}

// packRow is one row of the same-pack table.
type packRow struct {
	Key              string `json:"key"`
	Commander        string `json:"commander"`
	Start            int64  `json:"start"`
	Dur              uint32 `json:"dur"`
	Outcome          string `json:"outcome"`
	Deaths           int    `json:"deaths"`
	Warnings         int    `json:"warnings"`
	NonTankMs        *int64 `json:"nontank_ms"`
	NonTankLongestMs *int64 `json:"nontank_longest_ms"`
	Bots             int    `json:"bots"`
}

// PackHandler serves GET /api/v1/combat/packs?pack=&commander=&all=0|1.
func PackHandler(s *Store) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		q := r.URL.Query()
		pack := q.Get("pack")
		if len(pack) < 1 || len(pack) > 200 || hasControl(pack) {
			bad(w, "bad pack")
			return
		}
		rows := []packRow{}
		if s != nil {
			for _, row := range s.Pack(pack, q.Get("commander"), q.Get("all") == "1", 50) {
				rows = append(rows, packRow{
					Key: row.Key, Commander: row.Commander, Start: row.Start, Dur: row.Dur, Outcome: row.Outcome,
					Deaths: row.Deaths, Warnings: row.Warnings, NonTankMs: row.NonTankMs,
					NonTankLongestMs: row.NonTankLongestMs, Bots: row.Bots,
				})
			}
		}
		writeJSON(w, struct {
			PackKey string    `json:"pack_key"`
			Rows    []packRow `json:"rows"`
		}{pack, rows})
	}
}

// logOnce logs a file's first cap skip; the counter carries the rest.
func (s *Store) logOnce(path, msg string) {
	if s.skipped[path] {
		return
	}
	s.skipped[path] = true
	log.Printf("[History] %s: %s", path, msg)
}
