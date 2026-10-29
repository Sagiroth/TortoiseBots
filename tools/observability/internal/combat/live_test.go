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
	sum, changed := s.ApplyEvents(liveEvents(liveHeader(), `{"k":"start","t":0}`, deathEvent(1200, "Tanky")), liveNow)
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
	sum, changed = s.ApplyEvents(nextEvents(liveHeader(), deathEvent(2200, "Healy")), liveNow)
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
	_, changed := s.ApplyEvents(nextEvents(nil, `{"k":"end","t":3000,"o":"wipe","dur":3000,"kept":false}`), liveNow)
	if !changed {
		t.Fatal("the end of a live fight is a change")
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
	sum, changed := s.ApplyEvents(nextEvents(nil, `{"k":"end","t":31000,"o":"kill","dur":31000,"kept":true}`), liveNow)
	if !changed {
		t.Fatal("the end of a live fight is a change")
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
	if _, changed := s.ApplyEvents(dg, liveNow); changed {
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
	sum, changed := s.ApplyEvents(dup, liveNow)
	if changed || sum != (FightSummary{}) {
		t.Fatalf("a duplicate datagram reported a change: %+v %v", sum, changed)
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
	if sum, changed := s.ApplyEvents(older, liveNow); changed || sum != (FightSummary{}) {
		t.Fatalf("a reordered datagram reported a change: %+v %v", sum, changed)
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
	sum, changed := s.ApplyEvents(fresh, liveNow)
	if !changed || !sum.Live || sum.Session != 8 || sum.ID != "55-7" || sum.Deaths != 1 {
		t.Fatalf("the new session did not open its own row: %+v %v", sum, changed)
	}

	late := liveEvents(liveHeader(), deathEvent(2000, "Late"))
	late.Seq = 91
	if sum, changed := s.ApplyEvents(late, liveNow); changed || sum != (FightSummary{}) {
		t.Fatalf("a datagram of the older session touched the new row: %+v %v", sum, changed)
	}
	f, _, ok := s.GetKey(Key("Gbmage", 8, "55-7"))
	if !ok {
		t.Fatal("the new session's live fight is gone")
	}
	if len(f.Deaths) != 1 || f.Deaths[0].Name != "Fresh" {
		t.Fatalf("deaths of the new session: %+v", f.Deaths)
	}
}
