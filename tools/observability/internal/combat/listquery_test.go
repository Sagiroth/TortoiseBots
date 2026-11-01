package combat

import (
	"net/url"
	"strconv"
	"testing"
)

func lqRows() []FightSummary {
	// Newest first, as the lists answer; two rows share a start second (key order breaks the tie).
	return []FightSummary{
		{Key: "c-1-6", Start: 600, Outcome: "kill", BossName: "Onyxia", Deaths: 0},
		{Key: "c-1-5", Start: 500, Outcome: "wipe", Deaths: 5},
		{Key: "c-1-4a", Start: 400, Outcome: "kill", Deaths: 1},
		{Key: "c-1-4b", Start: 400, Outcome: "kill"},
		{Key: "c-1-3", Start: 300, Outcome: "kill"},
		{Key: "c-1-2", Start: 200, Outcome: "kill", BossName: "Lucifron", Deaths: 2},
	}
}

func keys(rows []FightSummary) string {
	s := ""
	for i, r := range rows {
		if i > 0 {
			s += ","
		}
		s += r.Key
	}
	return s
}

func mustQ(t *testing.T, raw string) ListQuery {
	t.Helper()
	v, err := url.ParseQuery(raw)
	if err != nil {
		t.Fatal(err)
	}
	q, err := ParseListQuery(v)
	if err != nil {
		t.Fatalf("%q: %v", raw, err)
	}
	return q
}

func TestListQueryAbsentIsUnchanged(t *testing.T) {
	q := mustQ(t, "commander=Glacienne")
	if q.Active() {
		t.Fatal("no params must not be active")
	}
	in := lqRows()
	out, more := q.Apply(in)
	if more || keys(out) != keys(in) {
		t.Fatalf("got %s more=%v", keys(out), more)
	}
}

func TestListQueryParams(t *testing.T) {
	cases := []struct {
		raw, want string
		more      bool
	}{
		{"limit=2", "c-1-6,c-1-5", true},
		{"limit=6", "c-1-6,c-1-5,c-1-4a,c-1-4b,c-1-3,c-1-2", false},
		{"limit=900", "c-1-6,c-1-5,c-1-4a,c-1-4b,c-1-3,c-1-2", false},
		{"before=500", "c-1-4a,c-1-4b,c-1-3,c-1-2", false},
		// The cursor is the last row of a page: start and key, so a shared second neither repeats nor drops a row.
		{"before=400,c-1-4a&limit=2", "c-1-4b,c-1-3", true},
		{"since=400", "c-1-6,c-1-5,c-1-4a,c-1-4b", false},
		{"since=300&before=500", "c-1-4a,c-1-4b,c-1-3", false},
		{"boss=1", "c-1-6,c-1-2", false},
		{"deaths=1", "c-1-5,c-1-4a,c-1-2", false},
		{"deaths=0", "c-1-6,c-1-5,c-1-4a,c-1-4b,c-1-3,c-1-2", false},
		{"outcome=wipe", "c-1-5", false},
		{"outcome=wipe,kill&boss=1&deaths=1", "c-1-2", false},
		{"deaths=1&limit=1", "c-1-5", true},
	}
	for _, c := range cases {
		out, more := mustQ(t, c.raw).Apply(lqRows())
		if keys(out) != c.want || more != c.more {
			t.Errorf("%s: got %s more=%v, want %s more=%v", c.raw, keys(out), more, c.want, c.more)
		}
	}
}

func TestListQueryBadInput(t *testing.T) {
	for _, raw := range []string{"limit=0", "limit=-1", "limit=x", "before=x", "before=0", "since=-5", "since=a", "boss=2", "deaths=yes", "outcome=,"} {
		v, _ := url.ParseQuery(raw)
		if _, err := ParseListQuery(v); err == nil {
			t.Errorf("%q: want an error", raw)
		}
	}
}

// TestListQuerySortsBeforeTheCut pins the order a cursor relies on: rows that arrive out of order (the memory list's live row
// first, a tie on the second) are put in start-descending, key-ascending order before limit or before cut them.
func TestListQuerySortsBeforeTheCut(t *testing.T) {
	in := []FightSummary{
		{Key: "live", Start: 150, Live: true},
		{Key: "k-3", Start: 300},
		{Key: "k-2b", Start: 200},
		{Key: "k-2a", Start: 200},
		{Key: "k-1", Start: 100},
	}
	out, more := mustQ(t, "limit=3").Apply(in)
	if keys(out) != "k-3,k-2a,k-2b" || !more {
		t.Fatalf("limit=3: got %s more=%v", keys(out), more)
	}
	out, _ = mustQ(t, "before=200,k-2a&limit=2").Apply(in)
	if keys(out) != "k-2b,live" {
		t.Fatalf("cursor page: got %s", keys(out))
	}
	if keys(in) != "live,k-3,k-2b,k-2a,k-1" {
		t.Fatalf("Apply reordered its input: %s", keys(in))
	}
	// No param: the rows come back untouched, in their own order.
	out, _ = mustQ(t, "").Apply(in)
	if keys(out) != keys(in) {
		t.Fatalf("no params reordered: %s", keys(out))
	}
}

// TestListQuerySpanDefaultCap pins the default page: since or before without limit is capped at MaxListLimit, with more set.
func TestListQuerySpanDefaultCap(t *testing.T) {
	rows := make([]FightSummary, MaxListLimit+5)
	for i := range rows {
		rows[i] = FightSummary{Key: "k" + string(rune('a'+i%26)) + itoa(i), Start: int64(10000 - i)}
	}
	for _, raw := range []string{"since=1", "before=99999"} {
		q := mustQ(t, raw)
		if q.Limit != MaxListLimit {
			t.Fatalf("%s: limit %d, want %d", raw, q.Limit, MaxListLimit)
		}
		out, more := q.Apply(rows)
		if len(out) != MaxListLimit || !more {
			t.Errorf("%s: %d rows more=%v", raw, len(out), more)
		}
	}
	if q := mustQ(t, "boss=1"); q.Limit != 0 {
		t.Errorf("a filter alone keeps no cap: %d", q.Limit)
	}
	if q := mustQ(t, "since=1&limit=7"); q.Limit != 7 {
		t.Errorf("an explicit limit stands: %d", q.Limit)
	}
}

func itoa(i int) string { return strconv.Itoa(i) }
