package state

import (
	"encoding/json"
	"os"
	"strconv"
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

// A whole datagram of a cycle already applied is a duplicate (UDP may deliver it twice): it must not replace the
// standing list, nor refresh the timestamp the staleness check reads. A datagram without a sequence (an older module)
// keeps the compatibility path and applies.
func TestCommandersIgnoreDuplicateWholeCycle(t *testing.T) {
	c := newClock()
	s := newTestStore(c)
	first := commandersPayload(5, 100, "First")
	first.Seq = 10
	if !s.ApplyCommanders(first) {
		t.Fatal("first payload refused")
	}
	before := s.Commanders()
	dup := commandersPayload(5, 100, "Duplicate")
	dup.Seq = 10
	if s.ApplyCommanders(dup) {
		t.Error("a whole datagram of the applied cycle was applied")
	}
	if v := s.Commanders(); len(v.Commanders) != 1 || v.Commanders[0].Commander != "First" || v.UpdatedAt != before.UpdatedAt {
		t.Fatalf("the duplicate changed the view: %+v", v)
	}
	// A duplicate that arrives after the standing list went stale must not make it fresh again.
	c.Advance(CommandersStaleAfter + time.Second)
	if s.ApplyCommanders(dup) {
		t.Error("a late duplicate of the applied cycle was applied")
	}
	if v := s.Commanders(); v.Fresh || v.UpdatedAt != before.UpdatedAt {
		t.Fatalf("the duplicate refreshed the timestamp: fresh %v, updated %d", v.Fresh, v.UpdatedAt)
	}
	// A newer cycle still applies.
	next := commandersPayload(5, 110, "Next")
	next.Seq = 11
	if !s.ApplyCommanders(next) {
		t.Fatal("a newer cycle was refused")
	}
	// A datagram without a sequence is an older module's: the compatibility path still applies it.
	legacy := commandersPayload(5, 120, "Legacy") // Seq stays 0
	if !s.ApplyCommanders(legacy) {
		t.Fatal("a datagram without a sequence was refused")
	}
	if v := s.Commanders(); len(v.Commanders) != 1 || v.Commanders[0].Commander != "Legacy" {
		t.Fatalf("legacy datagram did not apply: %+v", v.Commanders)
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

// The module's path data rides in the binding: the plan's steps, the game objects, the cast target. An older module sends none.
func TestCommandersCarryPathData(t *testing.T) {
	var steps, objects string
	for i := 0; i < 40; i++ {
		if i > 0 {
			steps += ","
			objects += ","
		}
		steps += `{"i":` + string(rune('0'+i%10)) + `,"k":"move","x":1.5,"y":2.5}`
		objects += `{"id":"9","entry":1,"name":"Door","x":1,"y":2,"type":"d","state":"r"}`
	}
	raw := `{"v":9,"session":3,"seq":1,"ts":50,"type":"COMMANDERS","commanders":[
{"commander":"A","online":true,"group":1,"lead":"W","bots":[{"name":"H","guid":5,"cast":"Flash Heal","ctgt":"77"}],"orders":[],
 "run":{"plan":"u","state":"running","step":1,"steps":9,"plan_steps":[` + steps + `,{"i":9,"k":"boss","x":3,"y":4,"r":30,"e":4854,"n":"Grimlok","sk":true}]},
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
	if len(v[0].Run.PlanSteps) != maxCommanderSteps || len(v[0].Objects) != maxCommanderObjects {
		t.Fatalf("steps %d, objects %d: they must be capped", len(v[0].Run.PlanSteps), len(v[0].Objects))
	}
	if len(p.Commanders[0].Run.PlanSteps) != 41 {
		t.Fatal("the caller's payload must not be trimmed")
	}
	if v[1].Objects == nil || len(v[1].Objects) != 0 || v[2].Objects != nil {
		t.Fatalf("[] means reported, none seen; absent means not reported: %v / %v", v[1].Objects, v[2].Objects)
	}
	out, _ := json.Marshal(v[2])
	if !strings.Contains(string(out), `"objects":null`) || strings.Contains(string(out), `plan_steps`) {
		t.Fatalf("view of a binding without path data: %s", out)
	}
}

// A6: a raid binding carries the bots' subgroups, away flags and the run's
// last status lines (oldest first); an older module sends none of them.
func TestCommandersCarryRaidNotes(t *testing.T) {
	raw := `{"v":9,"session":3,"seq":1,"ts":50,"type":"COMMANDERS","commanders":[
{"commander":"Gbraid","online":true,"group":1,"lead":"T0","bots":[
{"name":"T0","guid":1,"lead":true,"alive":true,"sub":0},
{"name":"H0","guid":2,"alive":true,"sub":0,"away":true},
{"name":"Old","guid":3,"alive":false}],
"orders":[],"run":{"plan":"molten_core","state":"running","step":5,"steps":20,"leg":"garr","kind":"boss","phase":"fight","note":"fighting Garr","changed":60,
"notes":[{"at":58,"step":4,"leg":"garr","kind":"move","phase":"walk","state":"running","note":"walking in"},{"at":59,"step":5,"leg":"garr","kind":"boss","phase":"fight","state":"running","note":"fighting Garr"}]}},
{"commander":"P","online":true,"group":2,"lead":"S","bots":[{"name":"Solo","alive":true}],"orders":[]}]}`
	var p model.CommandersPayload
	if err := json.Unmarshal([]byte(raw), &p); err != nil {
		t.Fatal(err)
	}
	s := newTestStore(newClock())
	s.ApplyCommanders(&p)
	v := s.Commanders().Commanders
	raid := v[0]
	if raid.Bots[0].Sub == nil || *raid.Bots[0].Sub != 0 || raid.Bots[2].Sub != nil {
		t.Fatalf("subgroups lost: %+v", raid.Bots)
	}
	if !raid.Bots[1].Away || raid.Bots[0].Away {
		t.Fatalf("away flags lost: %+v", raid.Bots)
	}
	if raid.Run == nil || len(raid.Run.Notes) != 2 || raid.Run.Notes[0].At != 58 || raid.Run.Notes[1].Note != "fighting Garr" {
		t.Fatalf("run notes lost: %+v", raid.Run)
	}
	party := v[1]
	if len(party.Bots) != 1 || party.Bots[0].Sub != nil || party.Bots[0].Away || party.Run != nil {
		t.Fatalf("a party binding must stay plain: %+v", party)
	}
	out, _ := json.Marshal(party.Bots[0])
	if strings.Contains(string(out), `"sub"`) || strings.Contains(string(out), `"away"`) {
		t.Fatalf("empty sub/away must stay out of the view: %s", out)
	}
}

// F5: a raid binding carries the layout the module applied (main tank, off-tanks, tank healers, raid healers) and the run's
// current step label; the dashboard would drop both without the model fields, so they must survive the store and reach the view.
func TestCommandersCarryLayoutAndLabel(t *testing.T) {
	raw := `{"v":9,"session":3,"seq":1,"ts":50,"type":"COMMANDERS","commanders":[
{"commander":"Gbraid","online":true,"group":1,"lead":"Ragemeister","bots":[{"name":"Ragemeister","guid":1,"lead":true,"sub":0}],"orders":[],
 "layout":{"mt":"Ragemeister","ot":["Irelanthe","Brun"],"th":[{"h":"Sera","t":"Ragemeister"}],"rh":["Prayer"]},
 "run":{"plan":"onyxias_lair","state":"running","step":28,"steps":40,"leg":"onyxia","kind":"move","phase":"walk","label":"boss stand outside her wake radius",
  "plan_steps":[{"i":28,"k":"move","x":-80,"y":-211.4,"c":"boss stand outside her wake radius"}]}},
{"commander":"P","online":true,"group":2,"lead":"S","bots":[{"name":"Solo","alive":true}],"orders":[]}]}`
	var p model.CommandersPayload
	if err := json.Unmarshal([]byte(raw), &p); err != nil {
		t.Fatal(err)
	}
	s := newTestStore(newClock())
	s.ApplyCommanders(&p)
	v := s.Commanders().Commanders
	raid := v[0]
	if raid.Layout == nil || raid.Layout.MainTank != "Ragemeister" || len(raid.Layout.OffTanks) != 2 ||
		len(raid.Layout.TankHealers) != 1 || raid.Layout.TankHealers[0].Healer != "Sera" ||
		raid.Layout.TankHealers[0].Tank != "Ragemeister" || len(raid.Layout.RaidHealers) != 1 {
		t.Fatalf("layout lost: %+v", raid.Layout)
	}
	if raid.Run == nil || raid.Run.Label != "boss stand outside her wake radius" || len(raid.Run.PlanSteps) != 1 ||
		raid.Run.PlanSteps[0].Label != "boss stand outside her wake radius" {
		t.Fatalf("run label lost: %+v", raid.Run)
	}
	out, _ := json.Marshal(raid)
	if !strings.Contains(string(out), `"layout":{"mt":"Ragemeister","ot":["Irelanthe","Brun"]`) ||
		!strings.Contains(string(out), `"th":[{"h":"Sera","t":"Ragemeister"}]`) ||
		!strings.Contains(string(out), `"label":"boss stand outside her wake radius"`) {
		t.Fatalf("layout or label not in the view: %s", out)
	}
	plain, _ := json.Marshal(v[1])
	if strings.Contains(string(plain), `"layout"`) {
		t.Fatalf("a binding without a layout must keep it out: %s", plain)
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

// A trail is never joined across a teleport: a jump no walking explains, a map change, a death. The point where it happened carries its kind.
func TestCommandersTrailBreaks(t *testing.T) {
	c := newClock()
	s := newTestStore(c)
	kinds := func() []byte {
		var out []byte
		for _, p := range s.Trails()["A"][1].Points {
			out = append(out, p.Kind)
		}
		return out
	}
	same := func(got []byte, want string) {
		t.Helper()
		if string(got) != want {
			t.Fatalf("point kinds %q, want %q", got, want)
		}
	}
	// walking and riding hard are not breaks: 2 s apart, 14 yd/s is 28 yd
	s.ApplyCommanders(trailPayload(100, 209, 0, 0))
	s.ApplyCommanders(trailPayload(102, 209, 28, 0))
	s.ApplyCommanders(trailPayload(104, 209, 56, 0))
	same(kinds(), "\x00\x00\x00")
	// a summon 300 yd away within one report
	s.ApplyCommanders(trailPayload(106, 209, 356, 0))
	same(kinds(), "\x00\x00\x00t")
	// the limit for a 2 s gap is 12 + 20*2 = 52 yd: exactly that is a walk, past it a break
	s.ApplyCommanders(trailPayload(108, 209, 356+52, 0))
	same(kinds(), "\x00\x00\x00t\x00")
	s.ApplyCommanders(trailPayload(110, 209, 356+52+53, 0))
	same(kinds(), "\x00\x00\x00t\x00t")
	// a long silence allows more (30 s: 612 yd) but does not hide a jump past it
	s.ApplyCommanders(trailPayload(140, 209, 461+600, 0))
	same(kinds(), "\x00\x00\x00t\x00t\x00")
	s.ApplyCommanders(trailPayload(170, 209, 461+600+700, 0))
	same(kinds(), "\x00\x00\x00t\x00t\x00t")
	// a map change starts a new trail whose first point is marked
	s.ApplyCommanders(trailPayload(200, 70, 5, 5))
	same(kinds(), "m")
}

func TestCommandersTrailDeathAndRelease(t *testing.T) {
	c := newClock()
	s := newTestStore(c)
	pay := func(ts int64, x float64, alive bool) {
		p := trailPayload(ts, 209, x, 0)
		p.Commanders[0].Bots[0].Alive = alive
		s.ApplyCommanders(p)
	}
	pay(100, 0, true)
	pay(102, 1, false)   // died where it stood (under the step distance, still recorded)
	pay(104, 1, false)   // lies there: nothing new
	pay(106, 400, false) // released: a jump while dead
	pay(108, 400, true)  // resurrected next to the corpse: same place, no break
	pay(110, 900, true)  // a jump while alive: a teleport
	var got []byte
	for _, p := range s.Trails()["A"][1].Points {
		got = append(got, p.Kind)
	}
	// the revive report at the same place adds no point (under the step); the last jump is a teleport
	if string(got) != "\x00drt" {
		t.Fatalf("kinds %q, want 0 d r t", got)
	}
}

// The module's live fight window (CombatMonitor) rides in the binding while the bound group fights, so the dashboard can show
// the fight as it happens; a binding outside a fight carries none, and an older module sends none at all.
func TestCommandersCarryLiveCombat(t *testing.T) {
	raw := `{"v":12,"session":3,"seq":1,"ts":50,"type":"COMMANDERS","commanders":[
{"commander":"Glacienne","online":true,"group":1,"lead":"Ragemeister","bots":[],"orders":[],
 "combat":{"win_s":10,"fight_ms":23000,"bots":[{"g":814,"dps":120,"hps":0,"c":3,"ls":"Maul"}]}},
{"commander":"Other","online":true,"group":2,"lead":"X","bots":[],"orders":[]}]}`
	var p model.CommandersPayload
	if err := json.Unmarshal([]byte(raw), &p); err != nil {
		t.Fatal(err)
	}
	s := newTestStore(newClock())
	s.ApplyCommanders(&p)
	v := s.Commanders().Commanders
	c := v[0].Combat
	if c == nil || c.WinS != 10 || c.FightMS != 23000 || len(c.Bots) != 1 ||
		c.Bots[0].GUID != 814 || c.Bots[0].DPS != 120 || c.Bots[0].HPS != 0 || c.Bots[0].Casts != 3 || c.Bots[0].Last != "Maul" {
		t.Fatalf("live window lost: %+v", c)
	}
	out, _ := json.Marshal(v[0])
	if !strings.Contains(string(out), `"combat":{"win_s":10,"fight_ms":23000`) {
		t.Fatalf("live window not in the view: %s", out)
	}
	if v[1].Combat != nil {
		t.Fatalf("a binding outside a fight must carry none: %+v", v[1].Combat)
	}
	plain, _ := json.Marshal(v[1])
	if strings.Contains(string(plain), `"combat"`) {
		t.Fatalf("no combat field expected: %s", plain)
	}
}

// The window is bounded like the bots it carries: a datagram past the bound keeps the first 40, and the caller's own payload is
// not trimmed (the emitter reuses it).
func TestCommandersCapLiveCombatBots(t *testing.T) {
	p := commandersPayload(1, 1, "A")
	win := &model.CommanderCombat{WinS: 10, FightMS: 5000}
	for i := 0; i < maxCommanderBots+5; i++ {
		win.Bots = append(win.Bots, model.CommanderCombatBot{GUID: uint32(i + 1)})
	}
	p.Commanders[0].Combat = win
	s := newTestStore(newClock())
	s.ApplyCommanders(p)
	c := s.Commanders().Commanders[0].Combat
	if c == nil || len(c.Bots) != maxCommanderBots || c.Bots[maxCommanderBots-1].GUID != maxCommanderBots {
		t.Fatalf("window bots not capped: %+v", c)
	}
	if len(p.Commanders[0].Combat.Bots) != maxCommanderBots+5 {
		t.Fatalf("the caller's payload must not be trimmed: %d", len(p.Commanders[0].Combat.Bots))
	}
}

// The boss overlay (fight debugger phase 2): facing, the current cast and the spell's area survive the datagram's JSON and
// the view; a facing of 0 survives (it is a real direction), absent fields stay absent, and the daemon fills the cast's
// end from the report's ts.
func TestCommandersCarryBossOverlay(t *testing.T) {
	raw := `{"v":12,"session":3,"seq":1,"ts":50,"type":"COMMANDERS","commanders":[{"commander":"Z","online":true,"group":1,"lead":"Lead",
"bots":[{"name":"Lead","lead":true,"alive":true}],"mobs_map":249,"mobs":[
{"id":"5","entry":10184,"name":"Onyxia","hp":80,"x":1,"y":2,"reaction":"h","rank":"b","cast":"Flame Breath","o":0,"cid":18435,"cleft":1200,"ctot":2000,"ctg":"814","shape":{"k":"cone","a":120,"l":45}},
{"id":"6","entry":12129,"name":"Onyxian Warder","hp":50,"x":3,"y":4,"reaction":"h","rank":"e","o":1.57,"cid":18564,"cleft":5000,"ctot":5000,"ctg":"814","ch":1,"shape":{"k":"line","l":62,"w":60}},
{"id":"7","entry":1,"name":"Rat","hp":10,"x":5,"y":6,"reaction":"h","cast":"Bite"}],"orders":[]}]}`
	var p model.CommandersPayload
	if err := json.Unmarshal([]byte(raw), &p); err != nil {
		t.Fatal(err)
	}
	s := newTestStore(newClock())
	if !s.ApplyCommanders(&p) {
		t.Fatal("payload refused")
	}
	mobs := s.Commanders().Commanders[0].Mobs
	if len(mobs) != 3 {
		t.Fatalf("mobs = %d, want 3", len(mobs))
	}
	boss, warder, rat := mobs[0], mobs[1], mobs[2]
	if boss.O == nil || *boss.O != 0 || boss.CastID != 18435 || boss.CastLeftMS != 1200 || boss.CastTotalMS != 2000 ||
		boss.CastTarget != "814" || boss.Channel != 0 || boss.CastEndMS != 51200 {
		t.Fatalf("boss overlay lost: %+v", boss)
	}
	if boss.Shape == nil || boss.Shape.Kind != "cone" || boss.Shape.Angle != 120 || boss.Shape.Length != 45 || boss.Shape.Width != 0 {
		t.Fatalf("boss shape lost: %+v", boss.Shape)
	}
	if warder.O == nil || *warder.O != 1.57 || warder.Channel != 1 || warder.CastEndMS != 55000 ||
		warder.Shape == nil || warder.Shape.Kind != "line" || warder.Shape.Width != 60 {
		t.Fatalf("warder overlay lost: %+v (shape %+v)", warder, warder.Shape)
	}
	if rat.O != nil || rat.CastID != 0 || rat.Shape != nil || rat.CastEndMS != 0 {
		t.Fatalf("an ordinary mob must carry no overlay: %+v", rat)
	}
	out, err := json.Marshal(rat)
	if err != nil {
		t.Fatal(err)
	}
	for _, key := range []string{`"o"`, `"cid"`, `"cleft"`, `"ctot"`, `"ctg"`, `"ch"`, `"shape"`, `"cend_ms"`} {
		if strings.Contains(string(out), key) {
			t.Fatalf("an ordinary mob must not grow %s: %s", key, out)
		}
	}
	out, _ = json.Marshal(boss)
	if !strings.Contains(string(out), `"o":0`) || !strings.Contains(string(out), `"cend_ms":51200`) {
		t.Fatalf("a facing of 0 and the cast's end must be reported: %s", out)
	}
	if p.Commanders[0].Mobs[0].CastEndMS != 0 {
		t.Fatalf("the caller's payload must not be changed: %d", p.Commanders[0].Mobs[0].CastEndMS)
	}
}

// R8: a cycle above the payload caps arrives in parts. Nothing changes until every part of the cycle is in, one view
// change happens then, and the bindings follow part order whatever the datagram order was.
func TestCommandersPartsAssemble(t *testing.T) {
	s := newTestStore(newClock())
	part := func(seq uint64, idx, total int, names ...string) *model.CommandersPayload {
		p := commandersPayload(5, 100, names...)
		p.Seq, p.Part, p.Parts = seq, idx, total
		return p
	}
	if s.ApplyCommanders(part(5, 1, 2, "B")) {
		t.Fatal("a part of an incomplete cycle must not apply")
	}
	if v := s.Commanders(); len(v.Commanders) != 0 || v.Fresh {
		t.Fatalf("an incomplete cycle changed the view: %+v", v)
	}
	// A duplicate part number replaces the earlier copy instead of counting twice.
	if s.ApplyCommanders(part(5, 1, 2, "C")) {
		t.Fatal("a duplicate part must not apply the cycle")
	}
	if !s.ApplyCommanders(part(5, 0, 2, "A")) {
		t.Fatal("the last part must apply the cycle")
	}
	v := s.Commanders()
	if len(v.Commanders) != 2 || v.Commanders[0].Commander != "A" || v.Commanders[1].Commander != "C" {
		t.Fatalf("assembled view = %+v", v.Commanders)
	}
	if !v.Fresh || v.UpdatedAt == 0 {
		t.Fatalf("the assembled cycle must refresh the view: %+v", v)
	}
}

// R8: when a newer cycle starts before the older one completed, the older one is applied as a partial: its bindings
// plus the standing bindings it did not report, carried over once and dropped at the second carry in a row.
func TestCommandersPartialCarriesOver(t *testing.T) {
	s := newTestStore(newClock())
	part := func(seq uint64, ts int64, idx, total int, names ...string) *model.CommandersPayload {
		p := commandersPayload(5, ts, names...)
		p.Seq, p.Part, p.Parts = seq, idx, total
		return p
	}
	if s.ApplyCommanders(part(5, 100, 0, 2, "A", "B", "C", "D", "E")) {
		t.Fatal("part 0 alone must not apply")
	}
	if !s.ApplyCommanders(part(5, 100, 1, 2, "F", "G", "H", "I")) {
		t.Fatal("the complete cycle must apply")
	}
	// seq 6 loses its second part (a dropped datagram); seq 7 starts 2 s later.
	if s.ApplyCommanders(part(6, 102, 0, 2, "A", "B", "C", "D", "E", "F", "G", "H")) {
		t.Fatal("an incomplete cycle must not apply at once")
	}
	if !s.ApplyCommanders(part(7, 104, 0, 2, "A", "B", "C", "D", "E", "F", "G", "H")) {
		t.Fatal("a newer cycle must flush the older one as a partial")
	}
	got := viewNames(s)
	want := []string{"A", "B", "C", "D", "E", "F", "G", "H", "I"}
	if strings.Join(got, ",") != strings.Join(want, ",") {
		t.Fatalf("partial view = %v, want the fresh bindings plus the carried I", got)
	}
	// I is absent from seq 7 too: the second carry in a row drops it.
	if !s.ApplyCommanders(part(8, 106, 0, 2, "A", "B", "C", "D", "E", "F", "G", "H")) {
		t.Fatal("seq 8 must flush seq 7 as a partial")
	}
	got = viewNames(s)
	want = []string{"A", "B", "C", "D", "E", "F", "G", "H"}
	if strings.Join(got, ",") != strings.Join(want, ",") {
		t.Fatalf("second partial view = %v, want I dropped", got)
	}
	if n := s.CommandersPartials(); n != 2 {
		t.Fatalf("partials counted = %d, want 2", n)
	}
}

// R8: a cycle in parts belongs to one server session. A part of another session never joins the pending cycle, never
// completes it, and the session a part arrived for is the newest from then on, so an old session's delayed packets are
// refused even before its successor's list applies.
func TestCommandersPartsStayInSession(t *testing.T) {
	s := newTestStore(newClock())
	part := func(session, seq uint64, idx, total int, names ...string) *model.CommandersPayload {
		p := commandersPayload(session, 100, names...)
		p.Seq, p.Part, p.Parts = seq, idx, total
		return p
	}
	// Session 6 opens its cycle while session 5's part of the same seq is still in flight.
	if s.ApplyCommanders(part(6, 7, 0, 2, "New")) {
		t.Fatal("a part alone must not apply")
	}
	if s.ApplyCommanders(part(5, 7, 1, 2, "Old")) {
		t.Fatal("a part of the session before the restart applied")
	}
	if v := s.Commanders(); len(v.Commanders) != 0 {
		t.Fatalf("a binding of the restarted session's predecessor reached the view: %+v", v.Commanders)
	}
	if !s.ApplyCommanders(part(6, 7, 1, 2, "New2")) {
		t.Fatal("the part of the new session did not complete its cycle")
	}
	if got := viewNames(s); strings.Join(got, ",") != "New,New2" {
		t.Fatalf("assembled view = %v, want New,New2", got)
	}
	// A delayed whole report of the old session is refused too, even though its list never applied.
	if s.ApplyCommanders(commandersPayload(5, 400, "OldServer")) {
		t.Fatal("a whole report of an older session applied after a restart")
	}
	if got := viewNames(s); strings.Join(got, ",") != "New,New2" {
		t.Fatalf("view = %v, want the new session only", got)
	}
}

// R8: a part of a cycle already applied is refused: it must not open a new cycle that the next report would apply as a
// partial (stale bindings over the standing view), and it must not disturb the cycle that follows.
func TestCommandersStalePartAfterCompletion(t *testing.T) {
	s := newTestStore(newClock())
	part := func(seq uint64, ts int64, idx, total int, names ...string) *model.CommandersPayload {
		p := commandersPayload(5, ts, names...)
		p.Seq, p.Part, p.Parts = seq, idx, total
		return p
	}
	if s.ApplyCommanders(part(5, 100, 0, 2, "A")) {
		t.Fatal("a part alone must not apply")
	}
	if !s.ApplyCommanders(part(5, 100, 1, 2, "B")) {
		t.Fatal("the complete cycle did not apply")
	}
	// A whole list of an older cycle is stale too, even when its ts is newer (the cycle number is the order).
	old := commandersPayload(5, 110, "W")
	old.Seq = 4
	if s.ApplyCommanders(old) {
		t.Fatal("a whole list of an older cycle applied")
	}
	// A duplicate of the part that just applied.
	if s.ApplyCommanders(part(5, 100, 0, 2, "Z")) {
		t.Fatal("a duplicate part applied")
	}
	if got := viewNames(s); strings.Join(got, ",") != "A,B" {
		t.Fatalf("view after the duplicate = %v, want A,B", got)
	}
	// The duplicate must not turn the next cycle's first part into the flush of a stale partial.
	if s.ApplyCommanders(part(6, 102, 0, 2, "A")) {
		t.Fatal("the next cycle's first part applied a stale partial")
	}
	if !s.ApplyCommanders(part(6, 102, 1, 2, "C")) {
		t.Fatal("the next cycle did not apply")
	}
	if got := viewNames(s); strings.Join(got, ",") != "A,C" {
		t.Fatalf("view after the next cycle = %v, want A,C", got)
	}
	// The same after its own cycle applied; then a cycle whose parts arrive reversed still assembles.
	if s.ApplyCommanders(part(6, 102, 0, 2, "Q")) {
		t.Fatal("a duplicate part of the applied cycle applied")
	}
	if s.ApplyCommanders(part(7, 104, 1, 2, "D")) {
		t.Fatal("a part alone must not apply")
	}
	if !s.ApplyCommanders(part(7, 104, 0, 2, "A")) {
		t.Fatal("the reversed cycle did not assemble")
	}
	if got := viewNames(s); strings.Join(got, ",") != "A,D" {
		t.Fatalf("view after the reversed cycle = %v, want A,D", got)
	}
}

// viewNames is the commander names of the standing view, in order.
func viewNames(s *Store) []string {
	out := []string{}
	for _, b := range s.Commanders().Commanders {
		out = append(out, b.Commander)
	}
	return out
}

// R8: a payload without part keys is the whole list, byte for byte as before parts existed (the golden is the view this
// daemon produced for the walk-2 report before the change).
func TestCommandersSinglePartUnchanged(t *testing.T) {
	raw, err := os.ReadFile("testdata/walk2-commanders-walking.json")
	if err != nil {
		t.Fatal(err)
	}
	var snap struct {
		Commanders []model.CommanderBinding `json:"commanders"`
		UpdatedAt  int64                    `json:"updated_at"`
	}
	if err := json.Unmarshal(raw, &snap); err != nil {
		t.Fatal(err)
	}
	s := newTestStore(newClock())
	if !s.ApplyCommanders(&model.CommandersPayload{Session: 1, TS: snap.UpdatedAt, Type: "COMMANDERS", Commanders: snap.Commanders}) {
		t.Fatal("payload refused")
	}
	got, err := json.Marshal(s.Commanders().Commanders)
	if err != nil {
		t.Fatal(err)
	}
	want, err := os.ReadFile("testdata/walk2-commanders-view.json")
	if err != nil {
		t.Fatal(err)
	}
	if string(got)+"\n" != string(want) {
		t.Fatalf("single-part view changed:\n got %s\nwant %s", got, want)
	}
	if strings.Contains(string(got), `"part"`) || strings.Contains(string(got), `"parts"`) {
		t.Fatalf("part keys must not reach the view: %s", got)
	}
}

// R8: a part number outside its cycle is refused and changes nothing; a cycle of the 20 allowed parts assembles.
func TestCommandersPartBounds(t *testing.T) {
	s := newTestStore(newClock())
	bad := []struct{ part, parts int }{{0, 21}, {-1, 2}, {2, 2}, {0, -1}}
	for _, c := range bad {
		p := commandersPayload(1, 10, "A")
		p.Seq, p.Part, p.Parts = 1, c.part, c.parts
		if s.ApplyCommanders(p) {
			t.Errorf("part %d of %d accepted", c.part, c.parts)
		}
		if v := s.Commanders(); len(v.Commanders) != 0 {
			t.Errorf("part %d of %d changed the view: %+v", c.part, c.parts, v.Commanders)
		}
	}
	for i := 0; i < 20; i++ {
		p := commandersPayload(1, 20, "C"+strconv.Itoa(i))
		p.Seq, p.Part, p.Parts = 2, i, 20
		got := s.ApplyCommanders(p)
		if want := i == 19; got != want {
			t.Fatalf("part %d of 20 applied = %v, want %v", i, got, want)
		}
	}
	if v := s.Commanders(); len(v.Commanders) != 20 {
		t.Fatalf("20 bindings assembled: %+v", v.Commanders)
	}
}

// R8: the daemon follows 20 commanders; a 21st binding of one datagram is cut.
func TestCommandersTwentyBindings(t *testing.T) {
	s := newTestStore(newClock())
	names := make([]string, 0, 21)
	for i := 0; i < 21; i++ {
		names = append(names, "C"+strconv.Itoa(i))
	}
	s.ApplyCommanders(commandersPayload(1, 1, names...))
	v := s.Commanders()
	if len(v.Commanders) != 20 || v.Commanders[19].Commander != "C19" {
		t.Fatalf("%d bindings kept, last %s; want 20, C19", len(v.Commanders), v.Commanders[len(v.Commanders)-1].Commander)
	}
}
