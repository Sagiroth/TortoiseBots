package meters

import (
	"testing"

	"tortoise-observability/internal/combat"
	"tortoise-observability/internal/state"
)

// steadyFrames is the shared fixture of the Go and the node check: a bot doing 100 dps (and 50 hps) in a 10 s window, reports every
// 2 s from 2 s after a pull at start to 24 s after it, for a 20 s fight.
func steadyFrames(start int64, guid uint32) []state.TrackFrame {
	var out []state.TrackFrame
	for s := int64(2); s <= 24; s += 2 {
		out = append(out, state.TrackFrame{T: start + s, Fight: true, WinS: 10, Bots: []state.TrackBot{{GUID: guid, DPS: 100, HPS: 50}}})
	}
	return out
}

func fightOf(start int64, durMs uint32, bots ...combat.FightBot) combat.Fight {
	return combat.Fight{ID: "1-1", Commander: "Cmd", Start: start, DurMS: durMs, Bots: bots}
}

// TestBuildSteadyDPS is the failing case of this package (the package did not exist before it): a steady 100 dps for 20 s with an
// exact total of 2000 ends exactly on 2000, and the bucket in the middle holds about half.
func TestBuildSteadyDPS(t *testing.T) {
	f := fightOf(1000, 20000, combat.FightBot{GUID: 7, Dmg: 2000, Heal: 1000})
	s, ok := Build(steadyFrames(1000, 7), f)
	if !ok {
		t.Fatal("no series")
	}
	if s.V != 1 || s.Src != "window" || s.StepMs != 2000 || s.N != 10 || len(s.Bots) != 1 {
		t.Fatalf("header %+v", s)
	}
	b := s.Bots[0]
	if b.GUID != 7 || len(b.Dmg) != 10 || len(b.Heal) != 10 {
		t.Fatalf("bot %+v", b)
	}
	if b.Dmg[9] != 2000 || b.Heal[9] != 1000 {
		t.Fatalf("last values %d / %d, want 2000 / 1000", b.Dmg[9], b.Heal[9])
	}
	// The end of bucket 4 is 10 s: half the fight, within one bucket's worth (200).
	if d := int64(b.Dmg[4]) - 1000; d < -200 || d > 200 {
		t.Fatalf("middle bucket %d, want about 1000", b.Dmg[4])
	}
	// The whole curve, pinned: ten counted reports of 200 each. web/static/js/fightmeters.js fromSamples must give the same numbers
	// on the same fixture (fightmeters_check.js).
	for i, v := range b.Dmg {
		if v != uint64(200*(i+1)) || b.Heal[i] != uint64(100*(i+1)) {
			t.Fatalf("bucket %d: %d / %d, want %d / %d", i, v, b.Heal[i], 200*(i+1), 100*(i+1))
		}
	}
	for i := 1; i < len(b.Dmg); i++ {
		if b.Dmg[i] < b.Dmg[i-1] {
			t.Fatalf("not monotonic at %d: %v", i, b.Dmg)
		}
	}
	if b.Flat {
		t.Fatal("a bot with a real integral is not flat")
	}
}

// The window's centre is half a window before the report: the first report that counts (report T = 6 s, shifted to 1 s) puts its
// damage 5 s earlier than the report's own time.
func TestBuildHalfWindowShift(t *testing.T) {
	f := fightOf(1000, 20000, combat.FightBot{GUID: 7, Dmg: 1000})
	frames := []state.TrackFrame{{T: 1006, Fight: true, WinS: 10, Bots: []state.TrackBot{{GUID: 7, DPS: 100}}}}
	s, ok := Build(frames, f)
	if !ok {
		t.Fatal("no series")
	}
	d := s.Bots[0].Dmg
	if d[0] != 1000 {
		t.Fatalf("the only report (shifted to 1 s) lands in bucket 0: %v", d)
	}
	// A report at 6 s with no shift would still be bucket 3; at 14 s (shifted to 9 s) it is bucket 4.
	frames[0].T = 1014
	s, _ = Build(frames, f)
	d = s.Bots[0].Dmg
	if d[3] != 0 || d[4] != 1000 {
		t.Fatalf("a report at 14 s lands at 9 s (bucket 4): %v", d)
	}
	// A report whose centre is before the pull is not counted (T = 4 s shifts to -1 s).
	frames[0].T = 1004
	if _, ok := Build(frames, f); ok {
		t.Fatal("a report centred before the pull must not count")
	}
	// No window named: the shift is 5 s.
	frames[0].T, frames[0].WinS = 1006, 0
	s, ok = Build(frames, f)
	if !ok || s.Bots[0].Dmg[0] != 1000 {
		t.Fatalf("no window: %+v", s)
	}
}

// A 10 s hole between two reports stands for 4 s, not 10: with the exact total left out of the scaling the ratio between the two
// buckets shows it (first report 2 s, second report 4 s).
func TestBuildGapCappedAtFourSeconds(t *testing.T) {
	f := fightOf(1000, 40000, combat.FightBot{GUID: 7, Dmg: 600})
	frames := []state.TrackFrame{
		{T: 1006, Fight: true, WinS: 10, Bots: []state.TrackBot{{GUID: 7, DPS: 100}}}, // shifted 1 s: bucket 0, 2 s
		{T: 1016, Fight: true, WinS: 10, Bots: []state.TrackBot{{GUID: 7, DPS: 100}}}, // shifted 11 s: bucket 5, gap 10 s -> 4 s
	}
	s, ok := Build(frames, f)
	if !ok {
		t.Fatal("no series")
	}
	d := s.Bots[0].Dmg
	// 200 then 400 of 600: bucket 0 holds a third of the total, not a twelfth (2 s of 12 s).
	if d[0] != 200 {
		t.Fatalf("first bucket %d, want 200 (2 s of 6 s): %v", d[0], d)
	}
}

// A bot that did something per the exact totals but has no rate in any report gets a linear ramp and the Flat flag; a bot with an
// exact total of 0 gets zeros; bots keep the fight's own order and only the fight's bots are listed.
func TestBuildFlatRampZeroAndOrder(t *testing.T) {
	f := fightOf(1000, 20000,
		combat.FightBot{GUID: 9, Dmg: 500},
		combat.FightBot{GUID: 7, Dmg: 0},
		combat.FightBot{GUID: 8, Dmg: 2000},
	)
	frames := steadyFrames(1000, 8)
	for i := range frames {
		frames[i].Bots = append(frames[i].Bots, state.TrackBot{GUID: 99, DPS: 1000}) // not in the fight: not listed
	}
	s, ok := Build(frames, f)
	if !ok || len(s.Bots) != 3 {
		t.Fatalf("series %+v ok=%v", s, ok)
	}
	if s.Bots[0].GUID != 9 || s.Bots[1].GUID != 7 || s.Bots[2].GUID != 8 {
		t.Fatalf("order %d %d %d, want 9 7 8", s.Bots[0].GUID, s.Bots[1].GUID, s.Bots[2].GUID)
	}
	ramp := s.Bots[0]
	if !ramp.Flat || ramp.Dmg[9] != 500 || ramp.Dmg[4] != 250 {
		t.Fatalf("ramp %+v", ramp)
	}
	for _, v := range s.Bots[1].Dmg {
		if v != 0 {
			t.Fatalf("exact 0 must be all zeros: %v", s.Bots[1].Dmg)
		}
	}
	if s.Bots[1].Flat || s.Bots[2].Flat {
		t.Fatal("only the bot with no integral is flat")
	}
}

// A 30 minute fight is cut into at most 150 buckets (a 12 s step); the end is still exact.
func TestBuildLongFightBounded(t *testing.T) {
	const dur = 30 * 60 * 1000
	f := fightOf(0, dur, combat.FightBot{GUID: 7, Dmg: 123456789})
	var frames []state.TrackFrame
	for s := int64(6); s <= 1805; s += 2 {
		frames = append(frames, state.TrackFrame{T: s, Fight: true, WinS: 10, Bots: []state.TrackBot{{GUID: 7, DPS: 70000}}})
	}
	s, ok := Build(frames, f)
	if !ok {
		t.Fatal("no series")
	}
	if s.N > 150 || s.N != len(s.Bots[0].Dmg) || s.StepMs != 12000 {
		t.Fatalf("n=%d step=%d len=%d", s.N, s.StepMs, len(s.Bots[0].Dmg))
	}
	if s.Bots[0].Dmg[s.N-1] != 123456789 {
		t.Fatalf("end %d", s.Bots[0].Dmg[s.N-1])
	}
	if StepFor(1) != 2000 || StepFor(300000) != 2000 || StepFor(300001) != 3000 {
		t.Fatalf("StepFor %d %d %d", StepFor(1), StepFor(300000), StepFor(300001))
	}
}

// Nothing to describe: no duration, no bots, no report in combat inside the fight.
func TestBuildNoFightFrames(t *testing.T) {
	f := fightOf(1000, 20000, combat.FightBot{GUID: 7, Dmg: 2000})
	frames := steadyFrames(1000, 7)
	for i := range frames {
		frames[i].Fight = false
	}
	if _, ok := Build(frames, f); ok {
		t.Fatal("frames not in a fight must not build a series")
	}
	if _, ok := Build(nil, f); ok {
		t.Fatal("no frames")
	}
	if _, ok := Build(steadyFrames(1000, 7), fightOf(1000, 0, combat.FightBot{GUID: 7, Dmg: 1})); ok {
		t.Fatal("no duration")
	}
	if _, ok := Build(steadyFrames(1000, 7), fightOf(1000, 20000)); ok {
		t.Fatal("no bots")
	}
}
