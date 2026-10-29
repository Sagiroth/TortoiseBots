package combat

import (
	"encoding/json"
	"testing"
	"time"
)

var liveNow = time.Date(2026, 10, 28, 20, 0, 0, 0, time.UTC)

func newLiveStore(t *testing.T) *Store {
	t.Helper()
	s := NewStore(nil)
	s.SetClock(func() time.Time { return liveNow })
	return s
}

// liveEvents builds a COMBAT_EVENTS datagram with the given raw events.
func liveEvents(header *LiveHeader, events ...string) EventsDatagram {
	dg := EventsDatagram{
		V: 12, FMT: 1, Session: 7, Seq: 4411, TS: liveNow.Unix(), TSMs: liveNow.UnixMilli(),
		Type: "COMBAT_EVENTS", Commander: "Gbmage", CommanderGUID: 9, Group: 55, Map: 409,
		Fight: header,
	}
	for _, e := range events {
		dg.Events = append(dg.Events, json.RawMessage(e))
	}
	return dg
}

// nextEvents builds the datagram that follows one from liveEvents: the same
// session, one seq later (the module's counter rises with every datagram).
func nextEvents(header *LiveHeader, events ...string) EventsDatagram {
	dg := liveEvents(header, events...)
	dg.Seq++
	return dg
}

func liveHeader() *LiveHeader {
	return &LiveHeader{
		ID: "55-6", Start: liveNow.Unix() - 30, StartMs: liveNow.UnixMilli() - 30000,
		Plan: "molten_core", Leg: "l2", Step: 24, Kind: "boss", Boss: 11502, BossName: "Magmadar",
		FightMs: 30000,
	}
}

func deathEvent(tMs int, name string) string {
	return `{"k":"death","t":` + jsonInt(tMs) + `,"g":700,"n":"` + name + `","by":"Magmadar","x":-12.3,"y":45.6}`
}

func jsonInt(n int) string {
	b, _ := json.Marshal(n)
	return string(b)
}

func TestEventsOpenLiveRow(t *testing.T) {
	s := newLiveStore(t)
	sum, changed, accepted := s.ApplyEvents(liveEvents(liveHeader(), `{"k":"start","t":0}`, deathEvent(1200, "Tanky")), liveNow)
	if !accepted {
		t.Fatal("a fresh datagram must be accepted")
	}
	if !changed {
		t.Fatal("a new fight must be reported as changed")
	}
	if !sum.Live || sum.Outcome != "live" || sum.ID != "55-6" || sum.Deaths != 1 || sum.BossName != "Magmadar" || sum.Leg != "l2" || sum.Step != 24 {
		t.Fatalf("summary: %+v", sum)
	}
	if sum.Key != Key("Gbmage", 7, "55-6") {
		t.Fatalf("key: %q", sum.Key)
	}
	list := s.List("Gbmage")
	if len(list) != 1 || list[0].ID != "55-6" || !list[0].Live || list[0].Deaths != 1 {
		t.Fatalf("list: %+v", list)
	}
	// A second datagram of the same fight updates it without a change edge.
	sum, changed, accepted = s.ApplyEvents(nextEvents(liveHeader(), deathEvent(2200, "Healy")), liveNow)
	if !accepted {
		t.Fatal("the next datagram must be accepted")
	}
	if changed {
		t.Fatal("an update of the open fight is not a state change")
	}
	if sum.Deaths != 2 {
		t.Fatalf("deaths after the second datagram: %d", sum.Deaths)
	}
}

func TestFinishedReplacesLive(t *testing.T) {
	s := newLiveStore(t)
	s.ApplyEvents(liveEvents(liveHeader(), `{"k":"start","t":0}`), liveNow)
	f := Fight{ID: "55-6", Commander: "Gbmage", Group: 55, Map: 409, Plan: "molten_core", Leg: "l2", Step: 24,
		Kind: "boss", Start: liveNow.Unix() - 30, DurMS: 30000, Outcome: "kill", Boss: 11502, BossName: "Magmadar",
		Deaths: []FightDeath{{Name: "Tanky", TMs: 1200}}}
	if !s.Add(7, f) {
		t.Fatal("Add refused")
	}
	list := s.List("Gbmage")
	if len(list) != 1 || list[0].Live || list[0].Outcome != "kill" || list[0].ID != "55-6" {
		t.Fatalf("list after the finished fight: %+v", list)
	}
	if live := s.LiveRows([]string{"Gbmage"}); len(live) != 0 {
		t.Fatalf("live rows after the finished fight: %+v", live)
	}
}

func TestEndNotKeptRemovesLive(t *testing.T) {
	s := newLiveStore(t)
	s.ApplyEvents(liveEvents(liveHeader(), `{"k":"start","t":0}`), liveNow)
	_, changed, accepted := s.ApplyEvents(nextEvents(nil, `{"k":"end","t":3000,"o":"wipe","dur":3000,"kept":false}`), liveNow)
	if !changed || !accepted {
		t.Fatal("the end of a live fight is an accepted change")
	}
	if list := s.List("Gbmage"); len(list) != 0 {
		t.Fatalf("a fight shorter than the minimum must leave nothing: %+v", list)
	}
	if live := s.LiveRows(nil); len(live) != 0 {
		t.Fatalf("live rows: %+v", live)
	}
}

func TestEndKeptWaitsForTheFight(t *testing.T) {
	s := newLiveStore(t)
	s.ApplyEvents(liveEvents(liveHeader(), `{"k":"start","t":0}`), liveNow)
	sum, changed, accepted := s.ApplyEvents(nextEvents(nil, `{"k":"end","t":31000,"o":"kill","dur":31000,"kept":true}`), liveNow)
	if !changed || !accepted {
		t.Fatal("the end of a live fight is an accepted change")
	}
	if !sum.Live || sum.Outcome != "kill" || sum.Dur != 31000 {
		t.Fatalf("summary after the end: %+v", sum)
	}
	list := s.List("Gbmage")
	if len(list) != 1 || !list[0].Live || list[0].Outcome != "kill" {
		t.Fatalf("the live row must stay until the finished fight arrives: %+v", list)
	}
}

func TestLiveExpiresUnfinished(t *testing.T) {
	s := newLiveStore(t)
	s.ApplyEvents(liveEvents(liveHeader(), `{"k":"start","t":0}`, deathEvent(1200, "Tanky")), liveNow)
	s.SetClock(func() time.Time { return liveNow.Add(LiveExpire + time.Second) })
	list := s.List("Gbmage")
	if len(list) != 1 || list[0].Live || list[0].Outcome != "unfinished" || list[0].ID != "55-6" {
		t.Fatalf("an expired live fight must be filed as unfinished: %+v", list)
	}
	if live := s.LiveRows(nil); len(live) != 0 {
		t.Fatalf("live rows: %+v", live)
	}
	// The debugger still opens it.
	f, _, ok := s.GetKey(Key("Gbmage", 7, "55-6"))
	if !ok || f.Outcome != "unfinished" || len(f.Deaths) != 1 {
		t.Fatalf("detail after the expiry: %+v %v", f, ok)
	}
}

func TestUnknownFmtIgnored(t *testing.T) {
	s := newLiveStore(t)
	dg := liveEvents(liveHeader(), `{"k":"start","t":0}`)
	dg.FMT = 2
	if _, changed, accepted := s.ApplyEvents(dg, liveNow); changed || accepted {
		t.Fatal("an unknown format must be ignored")
	}
	if list := s.List("Gbmage"); len(list) != 0 {
		t.Fatalf("list: %+v", list)
	}
}

func TestLiveEventsRingBounded(t *testing.T) {
	s := newLiveStore(t)
	events := make([]string, 0, 300)
	for i := range 300 {
		events = append(events, deathEvent(i, "Bot"))
	}
	s.ApplyEvents(liveEvents(liveHeader(), events...), liveNow)
	f, _, ok := s.GetKey(Key("Gbmage", 7, "55-6"))
	if !ok {
		t.Fatal("no live detail")
	}
	if len(f.Deaths) != LiveEventsMax {
		t.Fatalf("ring holds %d deaths, want %d", len(f.Deaths), LiveEventsMax)
	}
	// The newest survives.
	if last := f.Deaths[len(f.Deaths)-1]; last.TMs != 299 {
		t.Fatalf("newest death %+v", last)
	}
}

func TestLiveDetailHasDeaths(t *testing.T) {
	s := newLiveStore(t)
	s.ApplyEvents(liveEvents(liveHeader(), `{"k":"start","t":0}`, deathEvent(1200, "Tanky")), liveNow)
	f, warnings, ok := s.GetKey(Key("Gbmage", 7, "55-6"))
	if !ok {
		t.Fatal("no live detail")
	}
	if len(warnings) != 0 {
		t.Fatalf("a live fight is not judged: %+v", warnings)
	}
	if f.ID != "55-6" || f.Commander != "Gbmage" || f.Outcome != "live" || f.DurMS != 30000 || f.Map != 409 ||
		f.Plan != "molten_core" || f.Kind != "boss" || f.Boss != 11502 || f.BossName != "Magmadar" {
		t.Fatalf("header: %+v", f)
	}
	if len(f.Bots) != 0 {
		t.Fatalf("a live fight has no bot rows: %+v", f.Bots)
	}
	if len(f.Deaths) != 1 {
		t.Fatalf("deaths: %+v", f.Deaths)
	}
	d := f.Deaths[0]
	if d.Name != "Tanky" || d.TMs != 1200 || d.X != -12.3 || d.Y != 45.6 || d.Killer != "Magmadar" {
		t.Fatalf("death: %+v", d)
	}
}

// The live rows the snapshot frame carries: only the commanders asked for.
func TestLiveRowsFiltered(t *testing.T) {
	s := newLiveStore(t)
	s.ApplyEvents(liveEvents(liveHeader(), `{"k":"start","t":0}`), liveNow)
	other := liveEvents(liveHeader(), `{"k":"start","t":0}`)
	other.Commander = "Other"
	s.ApplyEvents(other, liveNow)
	if rows := s.LiveRows([]string{"Gbmage"}); len(rows) != 1 || rows[0].ID != "55-6" {
		t.Fatalf("rows: %+v", rows)
	}
	if rows := s.LiveRows(nil); len(rows) != 2 {
		t.Fatalf("all rows: %+v", rows)
	}
}

// A duplicated datagram (UDP may deliver one twice) must change neither the
// header nor the events of the row it already produced.
func TestEventsDuplicateIgnored(t *testing.T) {
	s := newLiveStore(t)
	dg := liveEvents(liveHeader(), `{"k":"start","t":0}`, deathEvent(1200, "Tanky"))
	dg.Seq = 50
	s.ApplyEvents(dg, liveNow)

	// The duplicate carries a fight_ms the row has already moved past, so an
	// early header update would show up as a regressed duration.
	dup := liveEvents(liveHeader(), `{"k":"start","t":0}`, deathEvent(1200, "Tanky"))
	dup.Seq = 50
	dup.Fight.FightMs = 90000
	sum, changed, accepted := s.ApplyEvents(dup, liveNow)
	if changed || accepted || sum != (FightSummary{}) {
		t.Fatalf("a duplicate datagram was accepted: %+v %v %v", sum, changed, accepted)
	}
	f, _, ok := s.GetKey(Key("Gbmage", 7, "55-6"))
	if !ok {
		t.Fatal("no live detail")
	}
	if len(f.Deaths) != 1 || f.Deaths[0].Name != "Tanky" {
		t.Fatalf("the duplicate death was counted twice: %+v", f.Deaths)
	}
	if f.DurMS != 30000 {
		t.Fatalf("the duplicate header overwrote the row: dur %d", f.DurMS)
	}
}

// An older datagram that arrives after a newer one (UDP reordering) must not
// append its events or roll the header back.
func TestEventsReorderedIgnored(t *testing.T) {
	s := newLiveStore(t)
	first := liveEvents(liveHeader(), deathEvent(1000, "Tanky"))
	first.Seq = 50
	s.ApplyEvents(first, liveNow)
	later := liveEvents(liveHeader(), deathEvent(2000, "Healy"))
	later.Seq = 52
	s.ApplyEvents(later, liveNow)

	older := liveEvents(liveHeader(), deathEvent(1500, "Late"))
	older.Seq = 51
	older.Fight.FightMs = 99000
	if sum, changed, accepted := s.ApplyEvents(older, liveNow); changed || accepted || sum != (FightSummary{}) {
		t.Fatalf("a reordered datagram was accepted: %+v %v %v", sum, changed, accepted)
	}
	f, _, ok := s.GetKey(Key("Gbmage", 7, "55-6"))
	if !ok {
		t.Fatal("no live detail")
	}
	if len(f.Deaths) != 2 || f.Deaths[1].Name != "Healy" {
		t.Fatalf("the late death was appended or replaced a newer one: %+v", f.Deaths)
	}
	if f.DurMS != 30000 {
		t.Fatalf("the older header overwrote the row: dur %d", f.DurMS)
	}
}

// A new game-server session starts a fight with its own, lower seq; a late
// datagram of the previous session must not touch the new row.
func TestEventsSessionTransitionResets(t *testing.T) {
	s := newLiveStore(t)
	old := liveEvents(liveHeader(), deathEvent(1000, "Tanky"))
	old.Seq = 90
	s.ApplyEvents(old, liveNow)

	fresh := liveEvents(liveHeader(), deathEvent(100, "Fresh"))
	fresh.Session = 8
	fresh.Seq = 2
	fresh.Fight.ID = "55-7"
	sum, changed, accepted := s.ApplyEvents(fresh, liveNow)
	if !accepted || !changed || !sum.Live || sum.Session != 8 || sum.ID != "55-7" || sum.Deaths != 1 {
		t.Fatalf("the new session did not open its own row: %+v %v %v", sum, changed, accepted)
	}

	late := liveEvents(liveHeader(), deathEvent(2000, "Late"))
	late.Seq = 91
	if sum, changed, accepted := s.ApplyEvents(late, liveNow); changed || accepted || sum != (FightSummary{}) {
		t.Fatalf("a datagram of the older session was accepted: %+v %v %v", sum, changed, accepted)
	}
	f, _, ok := s.GetKey(Key("Gbmage", 8, "55-7"))
	if !ok {
		t.Fatal("the new session's live fight is gone")
	}
	if len(f.Deaths) != 1 || f.Deaths[0].Name != "Fresh" {
		t.Fatalf("deaths of the new session: %+v", f.Deaths)
	}
}

func castEvent(tMs int, name string) string {
	return `{"k":"cast","t":` + jsonInt(tMs) + `,"id":"814","e":14507,"n":"Venoxis","s":"` + name + `"}`
}

// A delayed COMBAT_FIGHT of a fight filed as unfinished must replace the
// provisional entry (keeping its log) instead of being dropped; a second
// COMBAT_FIGHT for the same key is still a duplicate. Expiry filings are
// provisional too: the same delayed fight replaces them as well.
func TestDelayedFightReplacesProvisional(t *testing.T) {
	s := newLiveStore(t)
	s.ApplyEvents(liveEvents(liveHeader(), castEvent(1000, "Poison Cloud"), castEvent(2000, "Frenzy")), liveNow)

	hb := liveHeader()
	hb.ID = "55-7"
	if _, _, accepted := s.ApplyEvents(nextEvents(hb, castEvent(3000, "Frost Breath")), liveNow); !accepted {
		t.Fatal("the new fight's datagram was not accepted")
	}
	if prov, _, ok := s.Get("Gbmage", 7, "55-6"); !ok || prov.Outcome != "unfinished" {
		t.Fatalf("fight A was not filed provisional: %+v %v", prov, ok)
	}

	a := Fight{ID: "55-6", Commander: "Gbmage", Group: 55, Map: 409, Start: liveNow.Unix() - 30, DurMS: 30000, Outcome: "kill"}
	if !s.Add(7, a) {
		t.Fatal("the delayed COMBAT_FIGHT of fight A was dropped")
	}
	got, _, ok := s.Get("Gbmage", 7, "55-6")
	if !ok {
		t.Fatal("fight A is missing after the delayed COMBAT_FIGHT")
	}
	if got.Outcome != "kill" {
		t.Fatalf("fight A outcome = %q, want kill", got.Outcome)
	}
	if len(got.Log) != 2 {
		t.Fatalf("fight A log = %d events, want 2 (the provisional log kept)", len(got.Log))
	}
	for i, want := range []uint32{1000, 2000} {
		if gotT := eventT(t, got.Log[i]); gotT != want {
			t.Fatalf("fight A log[%d] t = %d, want %d", i, gotT, want)
		}
	}

	if s.Add(7, a) {
		t.Fatal("a second COMBAT_FIGHT of the same key was kept")
	}
}

func TestDelayedFightReplacesExpiredProvisional(t *testing.T) {
	s := newLiveStore(t)
	s.ApplyEvents(liveEvents(liveHeader(), castEvent(1000, "Poison Cloud"), castEvent(2000, "Frenzy")), liveNow)
	s.SetClock(func() time.Time { return liveNow.Add(LiveExpire + time.Second) })
	if prov, _, ok := s.GetKey(Key("Gbmage", 7, "55-6")); !ok || prov.Outcome != "unfinished" {
		t.Fatalf("fight A was not filed provisional: %+v %v", prov, ok)
	}

	a := Fight{ID: "55-6", Commander: "Gbmage", Group: 55, Map: 409, Start: liveNow.Unix() - 30, DurMS: 30000, Outcome: "kill"}
	if !s.Add(7, a) {
		t.Fatal("the delayed COMBAT_FIGHT of fight A was dropped")
	}
	got, _, ok := s.GetKey(Key("Gbmage", 7, "55-6"))
	if !ok {
		t.Fatal("fight A is missing after the delayed COMBAT_FIGHT")
	}
	if got.Outcome != "kill" || len(got.Log) != 2 {
		t.Fatalf("fight A = outcome %q log %d events, want kill and 2", got.Outcome, len(got.Log))
	}
	if s.Add(7, a) {
		t.Fatal("a second COMBAT_FIGHT of the same key was kept")
	}
}

// eventT reads the time of a stored log event.
func eventT(t *testing.T, raw json.RawMessage) uint32 {
	t.Helper()
	var ev struct {
		K string `json:"k"`
		T uint32 `json:"t"`
	}
	if err := json.Unmarshal(raw, &ev); err != nil {
		t.Fatalf("log event %s: %v", raw, err)
	}
	return ev.T
}

// The fight that closes a live row keeps its casts, interrupts, taunts and deaths, in order, so the debugger can replay a
// finished fight. A COMBAT_FIGHT the module sends twice must not double them.
func TestFightLogKeepsTheLiveEvents(t *testing.T) {
	s := newLiveStore(t)
	s.ApplyEvents(liveEvents(liveHeader(), `{"k":"start","t":0}`, castEvent(1000, "Poison Cloud"),
		`{"k":"interrupt","t":1800,"n":"Healy","s":"Venoxis"}`, castEvent(2500, "Frenzy"),
		`{"k":"taunt","t":3000,"n":"Tanky"}`, deathEvent(3200, "Tanky")), liveNow)

	f := Fight{ID: "55-6", Commander: "Gbmage", Group: 55, Map: 409, Start: liveNow.Unix() - 30, DurMS: 30000, Outcome: "kill"}
	if !s.Add(7, f) {
		t.Fatal("Add refused")
	}
	got, _, ok := s.Get("Gbmage", 7, "55-6")
	if !ok {
		t.Fatal("the filed fight is missing")
	}
	if len(got.Log) != 5 {
		t.Fatalf("log = %d events, want 5 (the start is not a log kind): %s", len(got.Log), got.Log)
	}
	for i, want := range []uint32{1000, 1800, 2500, 3000, 3200} {
		if gotT := eventT(t, got.Log[i]); gotT != want {
			t.Fatalf("log[%d] t = %d, want %d", i, gotT, want)
		}
	}
	if got.LogDropped != 0 {
		t.Fatalf("log dropped = %d, want 0", got.LogDropped)
	}

	// The module's second take of the same fight: refused, and the log stays as it was.
	if s.Add(7, f) {
		t.Fatal("a duplicate COMBAT_FIGHT must be refused")
	}
	if again, _, ok := s.Get("Gbmage", 7, "55-6"); !ok || len(again.Log) != 5 {
		t.Fatalf("the duplicate changed the log: %d events", len(again.Log))
	}
}

// Events of one live fight must never land on another fight the module finishes.
func TestFightLogStaysWithItsFight(t *testing.T) {
	s := newLiveStore(t)
	h := liveHeader()
	h.ID = "1-7"
	s.ApplyEvents(liveEvents(h, castEvent(1000, "Poison Cloud")), liveNow)

	f := Fight{ID: "1-8", Commander: "Gbmage", Group: 55, Map: 409, Start: liveNow.Unix(), DurMS: 5000, Outcome: "kill"}
	if !s.Add(7, f) {
		t.Fatal("Add refused")
	}
	got, _, ok := s.Get("Gbmage", 7, "1-8")
	if !ok {
		t.Fatal("fight 1-8 is missing")
	}
	if len(got.Log) != 0 {
		t.Fatalf("fight 1-8 grew the events of fight 1-7: %s", got.Log)
	}
	if rows := s.LiveRows([]string{"Gbmage"}); len(rows) != 1 || rows[0].ID != "1-7" {
		t.Fatalf("fight 1-7 must stay open: %+v", rows)
	}
}

// A live row that goes quiet is filed as unfinished, and it keeps its log too.
func TestExpiredLiveKeepsItsLog(t *testing.T) {
	s := newLiveStore(t)
	s.ApplyEvents(liveEvents(liveHeader(), castEvent(1000, "Poison Cloud"), castEvent(2000, "Frenzy")), liveNow)
	s.SetClock(func() time.Time { return liveNow.Add(LiveExpire + time.Second) })
	f, _, ok := s.GetKey(Key("Gbmage", 7, "55-6"))
	if !ok {
		t.Fatal("the expired fight is missing")
	}
	if f.Outcome != "unfinished" || len(f.Log) != 2 {
		t.Fatalf("expired fight: outcome %q, log %d events", f.Outcome, len(f.Log))
	}
	if got := eventT(t, f.Log[1]); got != 2000 {
		t.Fatalf("newest log event t = %d, want 2000", got)
	}
}

// A new fight that arrives while the previous fight's end was lost must not
// erase it: the replaced row is filed as unfinished with its own log, and
// the new row keeps only its own events.
func TestReplacedLiveRowFiledUnfinished(t *testing.T) {
	s := newLiveStore(t)
	s.ApplyEvents(liveEvents(liveHeader(), castEvent(1000, "Poison Cloud"), castEvent(2000, "Frenzy")), liveNow)

	hb := liveHeader()
	hb.ID = "55-7"
	sum, changed, accepted := s.ApplyEvents(nextEvents(hb, castEvent(3000, "Frost Breath")), liveNow)
	if !accepted || !changed || !sum.Live || sum.ID != "55-7" {
		t.Fatalf("the new fight did not open its own row: %+v %v %v", sum, changed, accepted)
	}

	a, _, ok := s.Get("Gbmage", 7, "55-6")
	if !ok {
		t.Fatal("the replaced fight is missing")
	}
	if a.Outcome != "unfinished" {
		t.Fatalf("replaced fight outcome = %q, want unfinished", a.Outcome)
	}
	if len(a.Log) != 2 {
		t.Fatalf("replaced fight log = %d events, want 2", len(a.Log))
	}
	for i, want := range []uint32{1000, 2000} {
		if got := eventT(t, a.Log[i]); got != want {
			t.Fatalf("replaced log[%d] t = %d, want %d", i, got, want)
		}
	}

	fb := Fight{ID: "55-7", Commander: "Gbmage", Group: 55, Map: 409, Start: liveNow.Unix(), DurMS: 5000, Outcome: "kill"}
	if !s.Add(7, fb) {
		t.Fatal("Add refused fight B")
	}
	b, _, ok := s.Get("Gbmage", 7, "55-7")
	if !ok {
		t.Fatal("fight B is missing")
	}
	if len(b.Log) != 1 {
		t.Fatalf("fight B log = %d events, want 1: %s", len(b.Log), b.Log)
	}
	if got := eventT(t, b.Log[0]); got != 3000 {
		t.Fatalf("fight B log t = %d, want 3000", got)
	}
}

// A fight that closes a full ring through COMBAT_FIGHT keeps its newest log
// and reports what the cap dropped, same as the expiry path.
func TestFiledFightReportsDroppedCasts(t *testing.T) {
	s := newLiveStore(t)
	events := make([]string, 0, LiveLogMax+200)
	for i := range LiveLogMax + 200 {
		events = append(events, castEvent(i, "Poison Cloud"))
	}
	s.ApplyEvents(liveEvents(liveHeader(), events...), liveNow)

	f := Fight{ID: "55-6", Commander: "Gbmage", Group: 55, Map: 409, Start: liveNow.Unix() - 30, DurMS: 30000, Outcome: "kill"}
	if !s.Add(7, f) {
		t.Fatal("Add refused")
	}
	got, _, ok := s.Get("Gbmage", 7, "55-6")
	if !ok {
		t.Fatal("the filed fight is missing")
	}
	if len(got.Log) != LiveLogMax {
		t.Fatalf("log = %d events, want %d", len(got.Log), LiveLogMax)
	}
	if got.LogDropped != 200 {
		t.Fatalf("log dropped = %d, want 200", got.LogDropped)
	}
	if gotT := eventT(t, got.Log[0]); gotT != 200 {
		t.Fatalf("oldest kept cast t = %d, want 200", gotT)
	}
}

func TestLiveLogRingCappedPerKind(t *testing.T) {
	s := newLiveStore(t)
	events := make([]string, 0, 1500)
	for i := 0; i < 1200; i++ {
		events = append(events, castEvent(i, "Poison Cloud"))
	}
	for i := 0; i < 300; i++ {
		events = append(events, `{"k":"engage","t":`+jsonInt(i)+`,"e":1,"n":"Trash"}`)
	}
	s.ApplyEvents(liveEvents(liveHeader(), events...), liveNow)

	lf := s.live["Gbmage"]
	if lf == nil {
		t.Fatal("the live row is missing")
	}
	if lf.logged != LiveLogMax || lf.other != LiveEventsMax || len(lf.events) != LiveLogMax+LiveEventsMax {
		t.Fatalf("ring = %d logged, %d other, %d total; want %d, %d and %d",
			lf.logged, lf.other, len(lf.events), LiveLogMax, LiveEventsMax, LiveLogMax+LiveEventsMax)
	}

	s.SetClock(func() time.Time { return liveNow.Add(LiveExpire + time.Second) })
	f, _, ok := s.GetKey(Key("Gbmage", 7, "55-6"))
	if !ok {
		t.Fatal("the expired fight is missing")
	}
	if len(f.Log) != LiveLogMax {
		t.Fatalf("log = %d events, want %d", len(f.Log), LiveLogMax)
	}
	if f.LogDropped != 200 {
		t.Fatalf("log dropped = %d, want 200 (1200 casts past the %d cap)", f.LogDropped, LiveLogMax)
	}
	if got := eventT(t, f.Log[0]); got != 200 {
		t.Fatalf("oldest kept cast t = %d, want 200 (the newest %d survive)", got, LiveLogMax)
	}
	if got := eventT(t, f.Log[len(f.Log)-1]); got != 1199 {
		t.Fatalf("newest cast t = %d, want 1199", got)
	}
}
