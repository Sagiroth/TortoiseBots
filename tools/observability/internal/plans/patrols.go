package plans

import (
	_ "embed"
	"encoding/json"
	"fmt"
	"log"
	"math"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"sync"
	"time"
)

// The plan layer's patrol paths: where a patrolling pack walks, how long its
// round takes and where the numbers came from. patrols.json is the committed
// data file (trash-check --dash-out shape, keyed by spawn guid); the optional
// folder named by -patrols-dir adds more files, either that shape or a
// turtle-agent patrol recording (tb-patrols). A row from the folder overrides
// the committed row, and a recording overrides a dash-out row.
const (
	// patrolMaxPoints caps a path on the wire; a longer one is decimated
	// evenly and keeps its first point.
	patrolMaxPoints = 64
	// patrolMaxPeriodS is the longest round the plan schema accepts.
	patrolMaxPeriodS = 900
	// patrolMaxSrcBytes caps path_src on the wire.
	patrolMaxSrcBytes = 80
	// patrolCheckEvery is how often a Get checks the folder for changes.
	patrolCheckEvery = time.Minute
	patrolFormat     = "tb-patrols"
	patrolVersion    = 1
)

// Patrol is one loop a creature walks: its path in yards, its round in
// seconds and where the numbers came from.
type Patrol struct {
	Path     [][3]float64
	PeriodS  float64
	Estimate bool
	Src      string
}

//go:embed patrols.json
var patrolsJSON []byte

// patrolRow is one parsed path and the spawn guids it belongs to, in file
// order (a recording's formation shares its lead's path).
type patrolRow struct {
	guids []uint32
	p     Patrol
}

// dashOutRow is a trash-check --dash-out row, keyed in the file by the
// leader's spawn guid as a decimal string.
type dashOutRow struct {
	Entries  []uint32     `json:"entries"`
	Path     [][3]float64 `json:"path"`
	PeriodS  float64      `json:"period_s"`
	Estimate bool         `json:"estimate"`
	Why      string       `json:"why"`
	Src      string       `json:"src"`
}

// patrolRecording is a turtle-agent patrol-record file (tb-patrols).
type patrolRecording struct {
	Format  string            `json:"format"`
	Version int               `json:"version"`
	Map     uint32            `json:"map"`
	Patrols []recordingPatrol `json:"patrols"`
}

type recordingPatrol struct {
	ID         string        `json:"id"`
	GUIDs      []string      `json:"guids"`
	SpawnGUIDs []uint32      `json:"spawn_guids"`
	Entries    []uint32      `json:"entries"`
	Name       string        `json:"name"`
	Path       []patrolPoint `json:"path"`
	PeriodS    float64       `json:"period_s"`
	PeriodEst  bool          `json:"period_estimate"`
	Source     string        `json:"source"`
}

type patrolPoint struct {
	X float64 `json:"x"`
	Y float64 `json:"y"`
	Z float64 `json:"z"`
}

// patrolStamp is what a check compares: a file keeping its size and
// modification time is not read again.
type patrolStamp struct {
	size int64
	mod  time.Time
}

// patrolFile is one parsed file of the folder, kept until its stamp changes.
// A file that could not be decoded keeps its stamp and no rows, so it is
// logged once rather than on every check.
type patrolFile struct {
	stamp     patrolStamp
	rows      []patrolRow
	rejected  int
	recording bool
}

// patrolTable is the effective patrol paths by spawn guid. embedded is the
// committed patrols.json, the floor every folder row overrides; byGUID is what
// Build reads.
type patrolTable struct {
	sync.RWMutex
	embedded map[uint32]Patrol
	byGUID   map[uint32]Patrol
	dir      string
	files    map[string]*patrolFile
	checked  time.Time
	loaded   bool
	rejected int
}

var patrolsState = newPatrolTable(loadEmbeddedPatrols(patrolsJSON))

func newPatrolTable(embedded map[uint32]Patrol) *patrolTable {
	return &patrolTable{
		embedded: embedded,
		byGUID:   copyPatrols(embedded),
		files:    map[string]*patrolFile{},
	}
}

// loadEmbeddedPatrols decodes the committed patrols.json, in the same shape as
// a folder file but with no folder in front of it.
func loadEmbeddedPatrols(data []byte) map[uint32]Patrol {
	rows, rejected, _, err := parsePatrolData(data)
	if err != nil {
		log.Printf("[Plans] patrols.json ignored: %v", err)
		return map[uint32]Patrol{}
	}
	logRejected("patrols.json", rejected, 0)
	return mergePatrols(nil, rows)
}

// SetPatrolsDir points the plan layer at a folder of patrol files (every
// *.json: a trash-check dash-out map or a tb-patrols recording). The next Get
// loads it; an empty dir keeps the committed rows only.
func SetPatrolsDir(dir string) {
	patrolsState.Lock()
	defer patrolsState.Unlock()
	if dir != patrolsState.dir {
		patrolsState.dir = dir
		patrolsState.files = map[string]*patrolFile{}
	}
	patrolsState.loaded = false
}

// refresh re-reads the folder when a file changed, at most once per
// patrolCheckEvery, and reports whether the effective paths changed.
func (t *patrolTable) refresh(now time.Time) bool {
	t.Lock()
	defer t.Unlock()
	if t.loaded && now.Before(t.checked.Add(patrolCheckEvery)) {
		return false
	}
	t.checked = now
	changed, rows := t.scan()
	if !changed {
		t.loaded = true
		return false
	}
	t.byGUID = mergePatrols(t.embedded, rows)
	t.loaded = true
	return true
}

// scan stats the folder and reads the files whose stamp changed; rows are nil
// when nothing changed. The row list is dash-out rows first, recordings last,
// each group in file name order, so a recording wins a shared spawn guid.
func (t *patrolTable) scan() (bool, []patrolRow) {
	if t.dir == "" {
		if len(t.files) == 0 {
			return false, nil
		}
		t.files = map[string]*patrolFile{}
		t.rejected = 0
		return true, nil
	}
	entries, err := os.ReadDir(t.dir)
	if err != nil {
		if len(t.files) == 0 {
			return false, nil
		}
		log.Printf("[Plans] patrols: %s: %v (no folder rows)", t.dir, err)
		t.files = map[string]*patrolFile{}
		t.rejected = 0
		return true, nil
	}
	next := make(map[string]*patrolFile, len(entries))
	changed := false
	for _, e := range entries {
		if e.IsDir() || !strings.HasSuffix(e.Name(), ".json") {
			continue
		}
		info, err := e.Info()
		if err != nil {
			continue
		}
		stamp := patrolStamp{size: info.Size(), mod: info.ModTime()}
		if old, ok := t.files[e.Name()]; ok && old.stamp == stamp {
			next[e.Name()] = old
			continue
		}
		changed = true
		f, err := readPatrolFile(filepath.Join(t.dir, e.Name()), stamp)
		if err != nil {
			log.Printf("[Plans] patrols: %s: %v", e.Name(), err)
			f = &patrolFile{stamp: stamp}
		}
		next[e.Name()] = f
	}
	if !changed {
		for name := range t.files {
			if _, ok := next[name]; !ok {
				changed = true
				break
			}
		}
	}
	if !changed {
		return false, nil
	}

	t.files = next
	names := make([]string, 0, len(next))
	for name := range next {
		names = append(names, name)
	}
	sort.Strings(names)
	var rows []patrolRow
	rejected := 0
	for _, recording := range []bool{false, true} {
		for _, name := range names {
			f := next[name]
			if f.recording != recording {
				continue
			}
			rows = append(rows, f.rows...)
			rejected += f.rejected
		}
	}
	logRejected(t.dir, rejected, t.rejected)
	t.rejected = rejected
	return true, rows
}

func logRejected(where string, rejected, before int) {
	if rejected > 0 && rejected != before {
		log.Printf("[Plans] patrols: %s: %d row(s) rejected (need 2+ points, finite numbers and a period in (0, 900] s)", where, rejected)
	}
}

func readPatrolFile(path string, stamp patrolStamp) (*patrolFile, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	rows, rejected, recording, err := parsePatrolData(data)
	if err != nil {
		return nil, err
	}
	return &patrolFile{stamp: stamp, rows: rows, rejected: rejected, recording: recording}, nil
}

// parsePatrolData decodes one patrol file: a tb-patrols recording, or the
// dash-out map keyed by spawn guid. Rows the map cannot draw are dropped and
// counted.
func parsePatrolData(data []byte) ([]patrolRow, int, bool, error) {
	var head struct {
		Format string `json:"format"`
	}
	if err := json.Unmarshal(data, &head); err != nil {
		return nil, 0, false, err
	}
	if head.Format == patrolFormat {
		var rec patrolRecording
		if err := json.Unmarshal(data, &rec); err != nil {
			return nil, 0, true, err
		}
		if rec.Version != patrolVersion {
			return nil, 0, true, fmt.Errorf("version %d, not %d", rec.Version, patrolVersion)
		}
		rows := make([]patrolRow, 0, len(rec.Patrols))
		bad := 0
		for _, p := range rec.Patrols {
			guids := append([]uint32(nil), p.SpawnGUIDs...)
			if id, err := strconv.ParseUint(p.ID, 10, 32); err == nil && id > 0 {
				guids = append(guids, uint32(id))
			}
			path := make([][3]float64, len(p.Path))
			for i, pt := range p.Path {
				path[i] = [3]float64{pt.X, pt.Y, pt.Z}
			}
			patrol, ok := newPatrol(path, p.PeriodS, p.PeriodEst, p.Source)
			if !ok || len(guids) == 0 {
				bad++
				continue
			}
			rows = append(rows, patrolRow{guids: guids, p: patrol})
		}
		return rows, bad, true, nil
	}

	var raw map[string]dashOutRow
	if err := json.Unmarshal(data, &raw); err != nil {
		return nil, 0, false, err
	}
	keys := make([]string, 0, len(raw))
	for key := range raw {
		keys = append(keys, key)
	}
	sort.Slice(keys, func(i, j int) bool { return dashKeyLess(keys[i], keys[j]) })
	rows := make([]patrolRow, 0, len(raw))
	bad := 0
	for _, key := range keys {
		guid, err := strconv.ParseUint(key, 10, 32)
		if err != nil || guid == 0 {
			bad++
			continue
		}
		r := raw[key]
		patrol, ok := newPatrol(r.Path, r.PeriodS, r.Estimate, r.Src)
		if !ok {
			bad++
			continue
		}
		rows = append(rows, patrolRow{guids: []uint32{uint32(guid)}, p: patrol})
	}
	return rows, bad, false, nil
}

// dashKeyLess orders a dash-out file's keys: the decimal guids first and in
// numeric order, any other key after them by name.
func dashKeyLess(a, b string) bool {
	na, aerr := strconv.ParseUint(a, 10, 64)
	nb, berr := strconv.ParseUint(b, 10, 64)
	switch {
	case aerr == nil && berr == nil:
		return na < nb
	case aerr == nil:
		return true
	case berr == nil:
		return false
	}
	return a < b
}

// newPatrol validates one recorded loop and shapes it for the wire: points
// rounded to 0.1 yd, at most patrolMaxPoints of them, the first kept.
func newPatrol(path [][3]float64, periodS float64, estimate bool, src string) (Patrol, bool) {
	if len(path) < 2 || !(periodS > 0) || periodS > patrolMaxPeriodS {
		return Patrol{}, false
	}
	for _, p := range path {
		for _, v := range p {
			if math.IsNaN(v) || math.IsInf(v, 0) {
				return Patrol{}, false
			}
		}
	}
	keep := len(path)
	if keep > patrolMaxPoints {
		keep = patrolMaxPoints
	}
	pts := make([][3]float64, keep)
	for k := range pts {
		p := path[k*len(path)/keep]
		pts[k] = [3]float64{round1(p[0]), round1(p[1]), round1(p[2])}
	}
	if len(src) > patrolMaxSrcBytes {
		src = src[:patrolMaxSrcBytes]
	}
	return Patrol{Path: pts, PeriodS: periodS, Estimate: estimate, Src: src}, true
}

func round1(v float64) float64 { return math.Round(v*10) / 10 }

func mergePatrols(embedded map[uint32]Patrol, rows []patrolRow) map[uint32]Patrol {
	out := make(map[uint32]Patrol, len(embedded)+len(rows))
	for guid, p := range embedded {
		out[guid] = p
	}
	for _, r := range rows {
		for _, guid := range r.guids {
			out[guid] = r.p
		}
	}
	return out
}

func copyPatrols(in map[uint32]Patrol) map[uint32]Patrol {
	out := make(map[uint32]Patrol, len(in))
	for guid, p := range in {
		out[guid] = p
	}
	return out
}

// attachPatrols puts the folder's paths on one plan's mobs, by spawn guid.
func attachPatrols(mobs []Mob) {
	patrolsState.RLock()
	defer patrolsState.RUnlock()
	if len(patrolsState.byGUID) == 0 {
		return
	}
	for i := range mobs {
		if p, ok := patrolsState.byGUID[mobs[i].GUID]; ok {
			mobs[i].Path = p.Path
			mobs[i].PeriodS = p.PeriodS
			mobs[i].PathEst = p.Estimate
			mobs[i].PathSrc = p.Src
		}
	}
}
