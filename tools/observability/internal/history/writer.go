// The writer half of the history store: startup scan, the six-second wait, the caps, the hourly rotation and the one goroutine
// that does all of it so the UDP goroutine only enqueues.
package history

import (
	"bufio"
	"bytes"
	"encoding/json"
	"log"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"time"

	"tortoise-observability/internal/combat"
)

// dayFile is one <commander dir>/<day>.jsonl on disk.
type dayFile struct {
	path string
	day  string
	size int64
}

// load deletes the expired days, rebuilds the index and enforces the total cap, in that order: an index row never outlives its
// file.
func (s *Store) load() error {
	files := s.dayFiles()
	s.rotate(files, s.cfg.Now())
	files = s.dayFiles()
	for _, f := range files {
		s.scan(f)
	}
	s.enforceTotal("", 0)
	return nil
}

// dayFiles lists the store's day files, newest day first (same day: path order).
func (s *Store) dayFiles() []dayFile {
	dirs, err := os.ReadDir(s.cfg.Dir)
	if err != nil {
		return nil
	}
	var out []dayFile
	for _, d := range dirs {
		if !d.IsDir() {
			continue
		}
		dir := filepath.Join(s.cfg.Dir, d.Name())
		entries, err := os.ReadDir(dir)
		if err != nil {
			continue
		}
		for _, e := range entries {
			name := e.Name()
			if e.IsDir() || !strings.HasSuffix(name, ".jsonl") || !validDay(strings.TrimSuffix(name, ".jsonl")) {
				continue
			}
			info, err := e.Info()
			if err != nil {
				continue
			}
			out = append(out, dayFile{path: filepath.Join(dir, name), day: strings.TrimSuffix(name, ".jsonl"), size: info.Size()})
		}
	}
	sort.Slice(out, func(i, j int) bool {
		if out[i].day != out[j].day {
			return out[i].day > out[j].day
		}
		return out[i].path < out[j].path
	})
	return out
}

// rotate deletes every day file older than the configured days and the index rows that pointed at them. A file exactly Days days
// old is kept; the one a day older goes.
func (s *Store) rotate(files []dayFile, now time.Time) {
	cutoff := now.UTC().AddDate(0, 0, -s.cfg.Days).Format(dayLayout)
	for _, f := range files {
		if f.day >= cutoff {
			continue
		}
		if err := s.remove(f.path); err != nil {
			log.Printf("[History] rotating %s: %v", f.path, err)
			continue
		}
		if size, ok := s.fileSize[f.path]; ok {
			s.total -= size
		}
		delete(s.fileSize, f.path)
		delete(s.skipped, f.path)
		s.dropRowsOf(f.path)
	}
	s.lastRotate = now
}

// scan indexes one day file, recording its size and counting the lines it cannot read.
func (s *Store) scan(f dayFile) {
	fh, err := os.Open(f.path)
	if err != nil {
		log.Printf("[History] reading %s: %v", f.path, err)
		return
	}
	defer fh.Close()
	br := bufio.NewReaderSize(fh, 64<<10)
	var off int64
	for {
		raw, err := br.ReadBytes('\n')
		if len(raw) > 0 {
			s.indexLine(f.path, off, len(raw), bytes.TrimRight(raw, "\r\n"))
			off += int64(len(raw))
		}
		if err != nil {
			break
		}
	}
	s.fileSize[f.path] = off
	s.total += off
}

// indexLine files one line into the index; a broken line or a duplicate key is skipped.
func (s *Store) indexLine(path string, off int64, size int, raw []byte) {
	if len(bytes.TrimSpace(raw)) == 0 {
		return
	}
	var ln line
	if err := json.Unmarshal(raw, &ln); err != nil || ln.Key == "" || ln.Fight.ID == "" {
		s.brokenLine()
		log.Printf("[History] %s: skipping a broken line at offset %d", path, off)
		return
	}
	row := rowOf(ln, path, off, size)
	s.mu.Lock()
	defer s.mu.Unlock()
	if _, ok := s.byKey[row.Key]; ok {
		return
	}
	if len(s.rows) >= s.cfg.MaxRows {
		s.dropped("rows")
		return
	}
	s.byKey[row.Key] = len(s.rows)
	s.rows = append(s.rows, row)
}

// rowOf builds one index row from a stored line.
func rowOf(ln line, path string, off int64, size int) Row {
	return Row{
		Key: ln.Key, Commander: ln.Fight.Commander, Session: ln.Session, ID: ln.Fight.ID,
		Start: ln.Fight.Start, Dur: ln.Fight.DurMS, Outcome: ln.Fight.Outcome, BossName: ln.Fight.BossName,
		Plan: ln.Fight.Plan, Leg: ln.Fight.Leg, Step: ln.Fight.Step, Map: ln.Fight.Map,
		Deaths: len(ln.Fight.Deaths), Warnings: ln.Warnings, PackKey: ln.PackKey,
		NonTankMs: ln.NonTankMs, NonTankLongestMs: ln.NonTankLongestMs, Bots: len(ln.Fight.Bots),
		day: dayOf(ln.Fight.Start), file: path, off: off, size: size,
	}
}

// run is the writer goroutine: it holds one FIFO of fights, writes each when its six-second wait is over, and answers Flush.
func (s *Store) run() {
	defer s.wg.Done()
	var pending []Item
	for {
		pending = s.drain(pending)
		for len(pending) > 0 && !s.cfg.Now().Before(s.dueAt(pending[0])) {
			it := pending[0]
			pending = pending[1:]
			s.write(it)
		}
		var timerC <-chan time.Time
		var timer *time.Timer
		if len(pending) > 0 {
			wait := time.Until(s.dueAt(pending[0]))
			if wait < 0 {
				wait = 0
			}
			timer = time.NewTimer(wait)
			timerC = timer.C
		}
		select {
		case it := <-s.q:
			pending = append(pending, it)
		case fc := <-s.flush:
			pending = s.drain(pending)
			for len(pending) > 0 && !s.cfg.Now().Before(s.dueAt(pending[0])) {
				it := pending[0]
				pending = pending[1:]
				s.write(it)
			}
			close(fc)
		case <-timerC:
		case <-s.quit:
			if timer != nil {
				timer.Stop()
			}
			pending = s.drain(pending)
			for _, it := range pending {
				s.write(it)
			}
			return
		}
		if timer != nil {
			timer.Stop()
		}
	}
}

// drain moves every queued fight into the writer's pending list: a flush must see a fight whose Enqueue has already returned.
func (s *Store) drain(pending []Item) []Item {
	for {
		select {
		case it := <-s.q:
			pending = append(pending, it)
		default:
			return pending
		}
	}
}

// dueAt is when a fight may be written: its arrival plus the wait for its last frames.
func (s *Store) dueAt(it Item) time.Time { return it.At.Add(WriteDelay) }

// write stores one fight: pack and non-tank numbers first (both services outside this store's lock), then the caps, the append
// and the index row.
func (s *Store) write(it Item) {
	now := s.cfg.Now()
	if now.Sub(s.lastRotate) >= time.Hour {
		s.rotate(s.dayFiles(), now)
	}
	f := it.Fight
	dir := filepath.Join(s.cfg.Dir, DirName(f.Commander))
	if err := os.MkdirAll(dir, 0700); err != nil {
		log.Printf("[History] %s: %v", dir, err)
		return
	}
	path := filepath.Join(dir, dayOf(f.Start)+".jsonl")

	pack := 0
	if s.pack != nil {
		pack = s.pack(f.Plan, f)
	}
	var ntMs, longest *int64
	if s.lanes != nil {
		if a, b, ok := s.lanes(f.Commander, f); ok {
			ntMs, longest = &a, &b
		}
	}
	key := combat.Key(f.Commander, it.Session, f.ID)
	stored := line{
		V: 1, Session: it.Session, Key: key, SavedAt: now.Unix(), PackKey: packKey(f, pack), Pack: pack,
		Engaged: engagedSorted(f.Engaged), NonTankMs: ntMs, NonTankLongestMs: longest, Warnings: it.Warnings, Fight: f,
	}
	data, err := json.Marshal(stored)
	if err != nil {
		log.Printf("[History] %s: %v", key, err)
		return
	}
	data = append(data, '\n')

	size := s.fileSize[path]
	if size+int64(len(data)) > s.cfg.MaxFileBytes {
		s.logOnce(path, "the day file is full; later fights of that day are not kept")
		s.dropped("file_cap")
		return
	}
	// Room is made before the append: a day file that cannot be deleted stays on disk, and counting it as gone would let the
	// store grow past its cap while the file is never retried.
	if s.total+int64(len(data)) > s.cfg.MaxBytes && !s.enforceTotal(path, int64(len(data))) {
		s.logCap(now)
		s.dropped("total_cap")
		return
	}
	fh, err := os.OpenFile(path, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0600)
	if err != nil {
		log.Printf("[History] %s: %v", path, err)
		return
	}
	n, err := fh.Write(data)
	fh.Close()
	if err != nil || n != len(data) {
		log.Printf("[History] %s: %v", path, err)
		return
	}
	s.fileSize[path] = size + int64(n)
	s.total += int64(n)
	s.appendRow(rowOf(stored, path, size, n))
	s.written()
}

// appendRow files one index row; a duplicate key or a full index is dropped without a log line (the counter carries it).
func (s *Store) appendRow(row Row) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if _, ok := s.byKey[row.Key]; ok {
		return
	}
	if len(s.rows) >= s.cfg.MaxRows {
		s.dropped("rows")
		return
	}
	s.byKey[row.Key] = len(s.rows)
	s.rows = append(s.rows, row)
}

// enforceTotal deletes whole day files, the oldest day first across commanders, until the store plus extra fits its byte cap.
// keep is the file about to be written and is never the one deleted. A file that cannot be deleted keeps its size and index
// rows: it is still on disk, and a later pass may free it. False means nothing more could be freed.
func (s *Store) enforceTotal(keep string, extra int64) bool {
	tried := map[string]bool{}
	for s.total+extra > s.cfg.MaxBytes {
		path := s.oldestFile(keep, tried)
		if path == "" {
			return false
		}
		tried[path] = true
		if err := s.remove(path); err != nil {
			log.Printf("[History] trimming %s: %v", path, err)
			continue
		}
		s.total -= s.fileSize[path]
		delete(s.fileSize, path)
		delete(s.skipped, path)
		s.dropRowsOf(path)
	}
	return true
}

// oldestFile is the oldest day file on record, the plan-path order breaking same-day ties; skip holds the files this pass
// already tried to delete.
func (s *Store) oldestFile(keep string, skip map[string]bool) string {
	best, bestDay := "", ""
	for path := range s.fileSize {
		if path == keep || skip[path] {
			continue
		}
		day := strings.TrimSuffix(filepath.Base(path), ".jsonl")
		if best == "" || day < bestDay || (day == bestDay && path < best) {
			best, bestDay = path, day
		}
	}
	return best
}

// logCap reports a stalled total cap at most once a minute: with nothing left to delete, every write would otherwise log.
func (s *Store) logCap(now time.Time) {
	if now.Sub(s.lastCapLog) < time.Minute {
		return
	}
	s.lastCapLog = now
	log.Printf("[History] over the %d MiB total cap and nothing can be deleted; later fights are not kept", s.cfg.MaxBytes>>20)
}

// dropRowsOf forgets every index row of one deleted file.
func (s *Store) dropRowsOf(path string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	kept := s.rows[:0]
	for _, r := range s.rows {
		if r.file != path {
			kept = append(kept, r)
		}
	}
	s.rows = kept
	s.byKey = make(map[string]int, len(s.rows))
	for i := range s.rows {
		s.byKey[s.rows[i].Key] = i
	}
}
