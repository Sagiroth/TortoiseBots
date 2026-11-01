package combat

import (
	"net/url"
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
