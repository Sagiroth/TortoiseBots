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

// The module's path data rides in the binding: the game objects, the cast target. An older module sends none.
func TestCommandersCarryPathData(t *testing.T) {
	var objects string
	for i := 0; i < 40; i++ {
		if i > 0 {
			objects += ","
		}
		objects += `{"id":"9","entry":1,"name":"Door","x":1,"y":2,"type":"d","state":"r"}`
	}
	raw := `{"v":9,"session":3,"seq":1,"ts":50,"type":"COMMANDERS","commanders":[
{"commander":"A","online":true,"group":1,"lead":"W","bots":[{"name":"H","guid":5,"cast":"Flash Heal","ctgt":"77"}],"orders":[],
 "objects":[` + objects + `]},
{"commander":"B","online":true,"group":2,"lead":"X","bots":[],"orders":[],"objects":[]},
{"commander":"C","online":true,"group":3,"lead":"Y","bots":[],"orders":[]}]}`
	var p model.CommandersPayload
	if err := json.Unmarshal([]byte(raw), &p); err != nil {
		t.Fatal(err)
	}
	s := newTestStore(newClock())
	s.ApplyCommanders(&p)
	v := s.Commanders().Commanders
	if v[0].Bots[0].CastTarget != "77" {
		t.Fatalf("cast target lost: %+v", v[0].Bots[0])
	}
	if len(v[0].Objects) != maxCommanderObjects {
		t.Fatalf("objects %d: they must be capped", len(v[0].Objects))
	}
	if len(p.Commanders[0].Objects) != 40 {
		t.Fatal("the caller's payload must not be trimmed")
	}
	if v[1].Objects == nil || len(v[1].Objects) != 0 || v[2].Objects != nil {
		t.Fatalf("[] means reported, none seen; absent means not reported: %v / %v", v[1].Objects, v[2].Objects)
	}
	out, _ := json.Marshal(v[2])
	if !strings.Contains(string(out), `"objects":null`) {
		t.Fatalf("view of a binding without path data: %s", out)
	}
}

func trailPayload(ts int64, mapID uint32, x, y float64) *model.CommandersPayload {
	p := &model.CommandersPayload{V: model.ProtocolVersion, Session: 5, TS: ts, Type: "COMMANDERS"}
	p.Commanders = []model.CommanderBinding{{
		Commander: "A", Online: true, Lead: "L",
		Bots: []model.CommanderBot{{Name: "L", GUID: 1, Lead: true, MapID: mapID, X: x, Y: y, Alive: true}, {Name: "M", GUID: 2, MapID: mapID, X: 0, Y: 0, Alive: true}},
	}}
	return p
}

// The server keeps a short bounded path per bot: a point per few yards, a new trail on a new map, gone when the binding ends or the report is stale.
func TestCommandersKeepTrails(t *testing.T) {
	c := newClock()
	s := newTestStore(c)
	s.ApplyCommanders(trailPayload(100, 209, 0, 0))
	s.ApplyCommanders(trailPayload(102, 209, 1, 0)) // under the step: no point
	s.ApplyCommanders(trailPayload(104, 209, 5, 0))
	tr := s.Trails()["A"]
	if len(tr[1].Points) != 2 || tr[1].Points[1].X != 5 || tr[1].Points[1].T != 104 || tr[1].Map != 209 {
		t.Fatalf("lead trail: %+v", tr[1])
	}
	if len(tr[2].Points) != 1 {
		t.Fatalf("a bot that stands still keeps one point: %+v", tr[2])
	}
	s.ApplyCommanders(trailPayload(106, 70, 9, 9)) // another map: a new trail
	if tr := s.Trails()["A"][1]; tr.Map != 70 || len(tr.Points) != 1 {
		t.Fatalf("new map, new trail: %+v", tr)
	}
	for i := 0; i < TrailMaxPoints+50; i++ {
		s.ApplyCommanders(trailPayload(200+int64(i), 70, 9+float64(i+1)*3, 9))
	}
	if tr := s.Trails()["A"][1]; len(tr.Points) != TrailMaxPoints || tr.Points[len(tr.Points)-1].X != 9+float32(TrailMaxPoints+50)*3 {
		t.Fatalf("the trail is a ring of %d: %d points", TrailMaxPoints, len(tr.Points))
	}
	c.Advance(CommandersStaleAfter + time.Second)
	if len(s.Trails()) != 0 {
		t.Fatal("a stale report has no trails")
	}
	s.ApplyCommanders(&model.CommandersPayload{V: model.ProtocolVersion, Session: 5, TS: 9999, Type: "COMMANDERS"})
	c.Advance(time.Second)
	if len(s.Trails()) != 0 {
		t.Fatal("the trails of an ended binding are forgotten")
	}
}
