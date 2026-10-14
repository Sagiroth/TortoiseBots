package state

import (
	"encoding/json"
	"strings"
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

// The module's unit info (what a bot is doing, a unit's level, rank, cast and crowd control, when the binding began) survives the
// datagram's JSON, the store and the view the dashboard fetches; an older module's datagram simply leaves the fields empty.
func TestCommandersCarryUnitInfo(t *testing.T) {
	raw := `{"v":9,"session":3,"seq":1,"ts":50,"type":"COMMANDERS","commanders":[{"commander":"Z","bound_at":40,"online":true,"group":1,"lead":"Lead",
"bots":[{"name":"Lead","lead":true,"alive":true,"st":"c","act":"melee","res":"O","cast":"Mortal Strike","cc":"s","rest":"d","mv":true,"tname":"Revelosh"},{"name":"Old","alive":true}],
"mobs_map":70,"mobs":[{"id":"5","entry":1,"name":"Revelosh","hp":80,"x":1,"y":2,"reaction":"h","lvl":47,"rank":"b","cc":"f","cast":"Chain Lightning"}],"orders":[]}]}`
	var p model.CommandersPayload
	if err := json.Unmarshal([]byte(raw), &p); err != nil {
		t.Fatal(err)
	}
	s := newTestStore(newClock())
	if !s.ApplyCommanders(&p) {
		t.Fatal("payload refused")
	}
	b := s.Commanders().Commanders[0]
	lead, old, mob := b.Bots[0], b.Bots[1], b.Mobs[0]
	if b.BoundAt != 40 || lead.State != "c" || lead.Action != "melee" || lead.Result != "O" || lead.Cast != "Mortal Strike" ||
		lead.CC != "s" || lead.Rest != "d" || !lead.Moving || lead.TargetName != "Revelosh" {
		t.Fatalf("bot unit info lost: %+v (bound_at %d)", lead, b.BoundAt)
	}
	if mob.Level != 47 || mob.Rank != "b" || mob.CC != "f" || mob.Cast != "Chain Lightning" {
		t.Fatalf("mob unit info lost: %+v", mob)
	}
	if old.State != "" || old.Action != "" || old.Moving {
		t.Fatalf("a bot of an older module must read as empty: %+v", old)
	}
	out, err := json.Marshal(b.Bots[1])
	if err != nil || strings.Contains(string(out), `"st"`) || strings.Contains(string(out), `"mv"`) {
		t.Fatalf("empty unit info must stay out of the view: %s", out)
	}
}

// The run leader's run rides in the binding; a binding without one has none.
func TestCommandersCarryTheRun(t *testing.T) {
	raw := `{"v":9,"session":3,"seq":1,"ts":50,"type":"COMMANDERS","commanders":[
{"commander":"Glacienne","online":true,"group":1,"lead":"W","bots":[],"orders":[],"run":{"plan":"uldaman","state":"paused","step":78,"steps":205,"leg":"ancient_stone_keeper","kind":"pull","phase":"rest","note":"a unit survived 3 pulls","changed":40}},
{"commander":"Other","online":true,"group":2,"lead":"X","bots":[],"orders":[]}]}`
	var p model.CommandersPayload
	if err := json.Unmarshal([]byte(raw), &p); err != nil {
		t.Fatal(err)
	}
	s := newTestStore(newClock())
	s.ApplyCommanders(&p)
	v := s.Commanders().Commanders
	r := v[0].Run
	if r == nil || r.Plan != "uldaman" || r.State != "paused" || r.Step != 78 || r.Steps != 205 || r.Leg != "ancient_stone_keeper" || r.Note == "" || r.Changed != 40 {
		t.Fatalf("run lost: %+v", r)
	}
	if v[1].Run != nil {
		t.Fatalf("a binding without a run must have none: %+v", v[1].Run)
	}
	out, _ := json.Marshal(v[1])
	if strings.Contains(string(out), `"run"`) {
		t.Fatalf("no run field expected: %s", out)
	}
}
