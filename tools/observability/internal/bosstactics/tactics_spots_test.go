package bosstactics

import (
	"context"
	"database/sql"
	"database/sql/driver"
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
	"sync"
	"testing"

	"github.com/go-sql-driver/mysql"
)

// spotDriver is a tiny database/sql driver: it answers a query by its column list. withSpots decides whether the
// spot/cycle columns exist; a query that names them on a database without them fails with MySQL's error 1054.
type spotDriver struct {
	mu        sync.Mutex
	withSpots bool
	queries   []string
}

type spotConn struct{ d *spotDriver }

func (c spotConn) Prepare(string) (driver.Stmt, error) { return nil, driver.ErrSkip }
func (c spotConn) Close() error                        { return nil }
func (c spotConn) Begin() (driver.Tx, error)           { return nil, driver.ErrSkip }

func (c spotConn) QueryContext(_ context.Context, query string, _ []driver.NamedValue) (driver.Rows, error) {
	c.d.mu.Lock()
	defer c.d.mu.Unlock()
	c.d.queries = append(c.d.queries, query)
	if strings.Contains(query, "`spot_x`") {
		if !c.d.withSpots {
			return nil, &mysql.MySQLError{Number: 1054, Message: "Unknown column 'spot_x' in 'field list'"}
		}
		return &spotRows{cols: []string{"map", "boss_entry", "kind", "target_entry", "spell_id", "priority", "param", "spot_x", "spot_y", "spot_z", "cycle_spell", "cycle_s", "comment"},
			rows: [][]driver.Value{
				{int64(309), int64(14834), "anchor", int64(0), int64(0), int64(25), float64(30), float64(-11787.4), float64(-1649.34), float64(54.1), int64(0), float64(0), "anchor"},
				{int64(309), int64(14834), "seek_aura", int64(11357), int64(24321), int64(30), float64(4), float64(-11787.4), float64(-1664.3), float64(54), int64(24324), float64(90), "seek"},
			}}, nil
	}
	return &spotRows{cols: []string{"map", "boss_entry", "kind", "target_entry", "spell_id", "priority", "param", "comment"},
		rows: [][]driver.Value{
			{int64(309), int64(14834), "anchor", int64(0), int64(0), int64(25), float64(30), "anchor"},
		}}, nil
}

type spotRows struct {
	cols []string
	rows [][]driver.Value
	i    int
}

func (r *spotRows) Columns() []string { return r.cols }
func (r *spotRows) Close() error      { return nil }
func (r *spotRows) Next(dest []driver.Value) error {
	if r.i >= len(r.rows) {
		return io.EOF
	}
	copy(dest, r.rows[r.i])
	r.i++
	return nil
}

func (d *spotDriver) Open(string) (driver.Conn, error) { return spotConn{d}, nil }

var spotDriverSeq int

func openSpotDB(t *testing.T, withSpots bool) (*sql.DB, *spotDriver) {
	t.Helper()
	d := &spotDriver{withSpots: withSpots}
	spotDriverSeq++
	name := "spot-test-" + string(rune('a'+spotDriverSeq))
	sql.Register(name, d)
	db, err := sql.Open(name, "")
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { db.Close() })
	return db, d
}

// TestTacticsSpotColumns: the tactics read carries the row's spot and the seek_aura cycle clock (the base Tactic has
// no such fields), and the JSON names them spotX/spotY/spotZ/cycleSpell/cycleS and omits them when zero.
func TestTacticsSpotColumns(t *testing.T) {
	db, drv := openSpotDB(t, true)
	src := &sqlSource{db: db}
	rows, cut, err := src.Tactics(context.Background())
	if err != nil || cut {
		t.Fatalf("Tactics: %v cut=%v", err, cut)
	}
	if len(rows) != 2 {
		t.Fatalf("rows = %d, want 2", len(rows))
	}
	a, s := rows[0], rows[1]
	if a.SpotX != float64(-11787.4) || a.SpotY != float64(-1649.34) || a.SpotZ != 54.1 || a.Param != 30 || a.Priority != 25 {
		t.Fatalf("anchor row = %+v", a)
	}
	if s.CycleSpell != 24324 || s.CycleS != 90 || s.SpotY != -1664.3 {
		t.Fatalf("seek_aura row = %+v", s)
	}
	if len(drv.queries) != 1 || !strings.Contains(drv.queries[0], "`spot_x`") {
		t.Fatalf("queries = %v, want one query naming the spot columns", drv.queries)
	}

	// Through the handler: the page reads these field names.
	svc := NewService(&fakeSource{tactics: rows})
	rec := httptest.NewRecorder()
	Handler(svc)(rec, httptest.NewRequest(http.MethodGet, "/api/v1/boss/tactics?map=309", nil))
	var got struct {
		Tactics []map[string]any `json:"tactics"`
	}
	if err := json.Unmarshal(rec.Body.Bytes(), &got); err != nil {
		t.Fatal(err)
	}
	if got.Tactics[1]["spotX"] != -11787.4 || got.Tactics[1]["cycleSpell"] != float64(24324) || got.Tactics[1]["cycleS"] != float64(90) || got.Tactics[1]["spotZ"] != float64(54) {
		t.Fatalf("seek_aura json = %v", got.Tactics[1])
	}
	if _, has := got.Tactics[0]["cycleSpell"]; has {
		t.Fatalf("a zero cycleSpell is not omitted: %v", got.Tactics[0])
	}
}

// TestTacticsOldSchemaFallback: a 1054 (unknown column) on the new query reruns QueryTacticsV1 and leaves the new
// fields zero; any other error still fails the read.
func TestTacticsOldSchemaFallback(t *testing.T) {
	db, drv := openSpotDB(t, false)
	src := &sqlSource{db: db}
	rows, _, err := src.Tactics(context.Background())
	if err != nil {
		t.Fatalf("Tactics on an old schema: %v", err)
	}
	if len(rows) != 1 || rows[0].Boss != 14834 || rows[0].Param != 30 {
		t.Fatalf("rows = %+v", rows)
	}
	if r := rows[0]; r.SpotX != 0 || r.SpotY != 0 || r.SpotZ != 0 || r.CycleSpell != 0 || r.CycleS != 0 {
		t.Fatalf("new fields are not zero: %+v", r)
	}
	if len(drv.queries) != 2 || !strings.Contains(drv.queries[0], "`spot_x`") || strings.Contains(drv.queries[1], "`spot_x`") {
		t.Fatalf("queries = %v, want the new one then the old one", drv.queries)
	}
	if !badColumn(&mysql.MySQLError{Number: 1054}) || badColumn(&mysql.MySQLError{Number: 1146}) || badColumn(io.EOF) {
		t.Fatal("badColumn matches the wrong errors")
	}
}

func TestSecondsBounds(t *testing.T) {
	for in, want := range map[float64]uint32{0: 0, 90: 90, 89.6: 90, -5: 0, 1e12: 0} {
		if got := seconds(in); got != want {
			t.Errorf("seconds(%v) = %d, want %d", in, got, want)
		}
	}
}
