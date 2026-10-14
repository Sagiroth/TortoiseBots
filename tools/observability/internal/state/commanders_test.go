package state

import (
	"testing"
	"time"

	"tortoise-observability/internal/model"
)

func commandersPayload(session uint64, ts int64, names ...string) *model.CommandersPayload {
	p := &model.CommandersPayload{V: model.ProtocolVersion, Session: session, TS: ts, Type: "COMMANDERS"}
	for _, n := range names {
		p.Commanders = append(p.Commanders, model.CommanderBinding{
			Commander: n, Online: true, Group: 7, Lead: "Lead",
			Bots:   []model.CommanderBot{{Name: "Lead", Lead: true, MapID: 209, X: 1, Y: 2, Alive: true}},
			Orders: []model.CommanderOrder{{At: ts, Verb: "move", OK: true, Result: "party 5"}},
		})
	}
	return p
}

func TestCommandersAreFreshThenStale(t *testing.T) {
	c := newClock()
	s := newTestStore(c)
	if v := s.Commanders(); len(v.Commanders) != 0 || v.Fresh || v.UpdatedAt != 0 {
		t.Fatalf("before any datagram: %+v", v)
	}
	if !s.ApplyCommanders(commandersPayload(5, 100, "Zfmagea")) {
		t.Fatal("first payload refused")
	}
	v := s.Commanders()
	if !v.Fresh || len(v.Commanders) != 1 || v.Commanders[0].Commander != "Zfmagea" || v.Commanders[0].Bots[0].MapID != 209 {
		t.Fatalf("fresh view: %+v", v)
	}
	c.Advance(CommandersStaleAfter - time.Second)
	if v := s.Commanders(); !v.Fresh || len(v.Commanders) != 1 {
		t.Fatalf("just before the limit: %+v", v)
	}
	c.Advance(2 * time.Second)
	v = s.Commanders()
	if v.Fresh || len(v.Commanders) != 0 || v.UpdatedAt == 0 || v.AgeSec < CommandersStaleAfter.Seconds() {
		t.Fatalf("stale view must show no bindings: %+v", v)
	}
	// An empty list is a real answer: the last binding ended.
	s.ApplyCommanders(commandersPayload(5, 200))
	if v := s.Commanders(); !v.Fresh || len(v.Commanders) != 0 {
		t.Fatalf("empty list: %+v", v)
	}
}

func TestCommandersIgnoreOldDatagrams(t *testing.T) {
	s := newTestStore(newClock())
	s.ApplyCommanders(commandersPayload(5, 100, "New"))
	if s.ApplyCommanders(commandersPayload(5, 90, "Reordered")) {
		t.Error("an older datagram of the same session must be ignored")
	}
	if s.ApplyCommanders(commandersPayload(4, 500, "OldServer")) {
		t.Error("a datagram of an older server session must be ignored")
	}
	if v := s.Commanders(); v.Commanders[0].Commander != "New" {
		t.Fatalf("%+v", v)
	}
	// A restarted server (a newer session) starts over, even with a lower ts.
	if !s.ApplyCommanders(commandersPayload(6, 10, "Restarted")) {
		t.Error("a newer session must be taken")
	}
	if v := s.Commanders(); v.Commanders[0].Commander != "Restarted" {
		t.Fatalf("%+v", v)
	}
}

func TestCommandersAreBounded(t *testing.T) {
	s := newTestStore(newClock())
	p := commandersPayload(1, 1)
	for i := 0; i < maxCommanderBindings+10; i++ {
		b := model.CommanderBinding{Commander: "c"}
		for j := 0; j < maxCommanderBots+5; j++ {
			b.Bots = append(b.Bots, model.CommanderBot{Name: "b"})
		}
		for j := 0; j < maxCommanderMobs+5; j++ {
			b.Mobs = append(b.Mobs, model.CommanderMob{ID: "1"})
		}
		for j := 0; j < maxCommanderOrders+5; j++ {
			b.Orders = append(b.Orders, model.CommanderOrder{At: int64(j)})
		}
		p.Commanders = append(p.Commanders, b)
	}
	s.ApplyCommanders(p)
	v := s.Commanders()
	if len(v.Commanders) != maxCommanderBindings || len(v.Commanders[0].Bots) != maxCommanderBots || len(v.Commanders[0].Orders) != maxCommanderOrders {
		t.Fatalf("bounds: %d bindings, %d bots, %d orders", len(v.Commanders), len(v.Commanders[0].Bots), len(v.Commanders[0].Orders))
	}
	if len(v.Commanders[0].Mobs) != maxCommanderMobs {
		t.Errorf("mobs not bounded: %d", len(v.Commanders[0].Mobs))
	}
	// The newest orders are the ones kept.
	if last := v.Commanders[0].Orders[maxCommanderOrders-1]; last.At != int64(maxCommanderOrders+4) {
		t.Errorf("newest order dropped: %+v", last)
	}
	// Missing lists are empty arrays, never null, for the page.
	s.ApplyCommanders(&model.CommandersPayload{Session: 1, TS: 2, Commanders: []model.CommanderBinding{{Commander: "x"}}})
	b := s.Commanders().Commanders[0]
	if b.Bots == nil || b.Orders == nil {
		t.Errorf("nil lists: %+v", b)
	}
}

func TestViewIsACopy(t *testing.T) {
	s := newTestStore(newClock())
	s.ApplyCommanders(commandersPayload(1, 1, "A"))
	v := s.Commanders()
	v.Commanders[0] = model.CommanderBinding{Commander: "mutated"}
	if s.Commanders().Commanders[0].Commander != "A" {
		t.Error("the caller changed the store's list")
	}
}

func TestNormalizeCommanderURL(t *testing.T) {
	ok := map[string]string{
		"":                              "",
		"  ":                            "",
		"http://127.0.0.1:8790":         "http://127.0.0.1:8790",
		"http://127.0.0.1:8790/":        "http://127.0.0.1:8790",
		" https://cmd.example.org/x/ ":  "https://cmd.example.org/x",
		"http://localhost:8790/#frag":   "http://localhost:8790",
		"http://[::1]:8790":             "http://[::1]:8790",
		"http://127.0.0.1:8790/?view=a": "http://127.0.0.1:8790/?view=a",
	}
	for in, want := range ok {
		got, err := NormalizeCommanderURL(in)
		if err != nil || got != want {
			t.Errorf("NormalizeCommanderURL(%q) = %q, %v; want %q", in, got, err, want)
		}
	}
	for _, bad := range []string{"127.0.0.1:8790", "javascript:alert(1)", "ftp://x/", "file:///etc/passwd", "http://", "http://user:pw@host/", "//host/x", "data:text/html,<b>"} {
		if got, err := NormalizeCommanderURL(bad); err == nil {
			t.Errorf("NormalizeCommanderURL(%q) = %q, want an error", bad, got)
		}
	}
}
