package state

import (
	"testing"

	"tortoise-observability/internal/model"
)

// posCommanders is one COMMANDERS report with one binding, one bot (guid 7)
// and one mob ("100"), so a position datagram has something to merge into.
func posCommanders(session, tsMs int64) *model.CommandersPayload {
	p := commandersPayload(uint64(session), tsMs/1000, "Zfmagea")
	p.TSMs = tsMs
	p.Commanders[0].MobsMap = 209
	p.Commanders[0].Bots = []model.CommanderBot{{Name: "Lead", GUID: 7, Lead: true, MapID: 209, X: 1, Y: 2, HP: 10, MaxHP: 20, Alive: true}}
	p.Commanders[0].Mobs = []model.CommanderMob{{ID: "100", Name: "Wolf", HP: 90, X: 5, Y: 6, Reaction: "h"}}
	return p
}

func posPayload(session, tsMs int64) *model.CommanderPosPayload {
	return &model.CommanderPosPayload{
		V: 12, FMT: 1, Session: uint64(session), TSMs: tsMs, Type: "COMMANDER_POS", Commander: "Zfmagea",
		Combat: true,
		Bots:   []model.PosBot{{G: 7, M: 209, X: 9, Y: 8, Z: 1, O: 2, HP: 5, MHP: 20, A: 1}},
		Mobs:   []model.PosMob{{ID: "100", X: 6, Y: 7, HP: 50, V: "7"}},
	}
}

func TestPosMergesByGuid(t *testing.T) {
	s := newTestStore(newClock())
	if !s.ApplyCommanders(posCommanders(5, 5000)) {
		t.Fatal("COMMANDERS refused")
	}
	frame, ok := s.ApplyCommanderPos(posPayload(5, 6000))
	if !ok {
		t.Fatal("pos refused")
	}
	if frame.Commander != "Zfmagea" || frame.TSMs != 6000 || !frame.Combat || len(frame.Bots) != 1 || len(frame.Mobs) != 1 {
		t.Fatalf("frame: %+v", frame)
	}
	v := s.Commanders()
	b := v.Commanders[0].Bots[0]
	if b.X != 9 || b.Y != 8 || b.Z != 1 || b.HP != 5 || b.MaxHP != 20 || !b.Alive || b.MapID != 209 {
		t.Fatalf("bot: %+v", b)
	}
	m := v.Commanders[0].Mobs[0]
	if m.X != 6 || m.Y != 7 || m.HP != 50 || m.Victim != "7" {
		t.Fatalf("mob: %+v", m)
	}
	// An unknown guid or id in the same datagram is ignored, and a dead flag
	// turns the bot dead.
	next := posPayload(5, 7000)
	next.Bots = append(next.Bots, model.PosBot{G: 999, X: 100, Y: 100})
	next.Mobs = append(next.Mobs, model.PosMob{ID: "999", X: 100, Y: 100})
	next.Bots[0].A = 0
	next.Bots[0].HP = 0
	if _, ok := s.ApplyCommanderPos(next); !ok {
		t.Fatal("second pos refused")
	}
	v = s.Commanders()
	if len(v.Commanders[0].Bots) != 1 || len(v.Commanders[0].Mobs) != 1 {
		t.Fatalf("unknown units were added: %+v", v.Commanders[0])
	}
	if b := v.Commanders[0].Bots[0]; b.Alive || b.HP != 0 || b.X != 9 {
		t.Fatalf("dead bot: %+v", b)
	}
}

func TestPosOlderThanReportIgnored(t *testing.T) {
	s := newTestStore(newClock())
	s.ApplyCommanders(posCommanders(5, 5000))
	if _, ok := s.ApplyCommanderPos(posPayload(5, 4999)); ok {
		t.Fatal("a position older than the last full report must be ignored")
	}
	if _, ok := s.ApplyCommanderPos(posPayload(5, 5000)); !ok {
		t.Fatal("a position of the report's own millisecond must be taken")
	}
}

func TestPosReorderIgnored(t *testing.T) {
	s := newTestStore(newClock())
	s.ApplyCommanders(posCommanders(5, 5000))
	if _, ok := s.ApplyCommanderPos(posPayload(5, 6000)); !ok {
		t.Fatal("first pos refused")
	}
	if _, ok := s.ApplyCommanderPos(posPayload(5, 6000)); ok {
		t.Fatal("a repeated pos must be ignored")
	}
	if _, ok := s.ApplyCommanderPos(posPayload(5, 5500)); ok {
		t.Fatal("an older pos must be ignored")
	}
	if _, ok := s.ApplyCommanderPos(posPayload(5, 7000)); !ok {
		t.Fatal("a newer pos refused")
	}
}

func TestPosUnknownCommanderIgnored(t *testing.T) {
	s := newTestStore(newClock())
	s.ApplyCommanders(posCommanders(5, 5000))
	p := posPayload(5, 6000)
	p.Commander = "Someone"
	if _, ok := s.ApplyCommanderPos(p); ok {
		t.Fatal("a binding that is not standing must be ignored")
	}
}

func TestPosOldSessionIgnored(t *testing.T) {
	s := newTestStore(newClock())
	s.ApplyCommanders(posCommanders(5, 5000))
	if _, ok := s.ApplyCommanderPos(posPayload(4, 9000000)); ok {
		t.Fatal("a pos of an older server session must be ignored")
	}
}

// TestPosDoesNotMutateHandedOutView: Commanders() shares the bots and mobs
// slices with the store, so a merge must copy them; a view handed out before
// the merge keeps its positions.
func TestPosDoesNotMutateHandedOutView(t *testing.T) {
	s := newTestStore(newClock())
	s.ApplyCommanders(posCommanders(5, 5000))
	before := s.Commanders()
	if _, ok := s.ApplyCommanderPos(posPayload(5, 6000)); !ok {
		t.Fatal("pos refused")
	}
	// Read through the view now: a struct copied before the merge would not
	// show an in-place change of the shared slice.
	b := before.Commanders[0].Bots[0]
	m := before.Commanders[0].Mobs[0]
	if b.X != 1 || b.Y != 2 || b.HP != 10 || m.X != 5 || m.Y != 6 || m.HP != 90 {
		t.Fatalf("an earlier view changed: bot %+v mob %+v", b, m)
	}
	after := s.Commanders()
	if a := after.Commanders[0].Bots[0]; a.X != 9 {
		t.Fatalf("fresh view: %+v", a)
	}
}
