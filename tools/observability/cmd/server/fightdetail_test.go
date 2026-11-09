package main

import (
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"net/url"
	"strings"
	"testing"
	"time"

	"tortoise-observability/internal/combat"
	"tortoise-observability/internal/history"
	"tortoise-observability/internal/state"
)

type fakeTrack struct {
	frames []state.TrackFrame
	asked  [][3]int64
}

func (f *fakeTrack) Track(commander string, from, to int64) ([]state.TrackFrame, map[uint32]string) {
	f.asked = append(f.asked, [3]int64{int64(len(commander)), from, to})
	return f.frames, nil
}

func meterFrames(start int64, guid uint32) []state.TrackFrame {
	var out []state.TrackFrame
	for s := int64(2); s <= 24; s += 2 {
		out = append(out, state.TrackFrame{T: start + s, Fight: true, WinS: 10, Bots: []state.TrackBot{{GUID: guid, DPS: 100, HPS: 50}}})
	}
	return out
}

func meterFight(id string) combat.Fight {
	return combat.Fight{ID: id, Commander: "G", Start: 1000, DurMS: 20000, Outcome: "kill",
		Bots: []combat.FightBot{{Name: "Pew", GUID: 3, Class: "mage", Role: "dps", Dmg: 2000, Heal: 1000}}}
}

func getDetail(h http.HandlerFunc, key string) (int, map[string]json.RawMessage) {
	rec := httptest.NewRecorder()
	h(rec, httptest.NewRequest(http.MethodGet, "/api/v1/combat/fight/"+url.PathEscape(key), nil))
	var out map[string]json.RawMessage
	_ = json.Unmarshal(rec.Body.Bytes(), &out)
	return rec.Code, out
}

// TestFightDetailCarriesMeters: the detail answer of a fight in memory has the damage and healing series built from the kept track
// (meters.n > 0, the last value the exact total), and the key is absent without track frames.
func TestFightDetailCarriesMeters(t *testing.T) {
	st := combat.NewStore(nil)
	if !st.Add(1, meterFight("with")) || !st.Add(1, meterFight("without")) {
		t.Fatal("Add refused")
	}
	trk := &fakeTrack{frames: meterFrames(1000, 3)}
	h := combatFightDetailHandler(st, nil, trk)
	code, got := getDetail(h, combat.Key("G", 1, "with"))
	if code != 200 {
		t.Fatalf("detail: %d", code)
	}
	var m struct {
		V, N   int
		Src    string
		StepMs uint32 `json:"step_ms"`
		Bots   []struct {
			G uint32
			D []uint64
			H []uint64
		}
	}
	if err := json.Unmarshal(got["meters"], &m); err != nil || m.N <= 0 || m.Src != "window" || len(m.Bots) != 1 {
		t.Fatalf("meters: %s (%v)", got["meters"], err)
	}
	if m.Bots[0].D[m.N-1] != 2000 || m.Bots[0].H[m.N-1] != 1000 {
		t.Fatalf("end totals %v %v", m.Bots[0].D, m.Bots[0].H)
	}
	if len(trk.asked) != 1 || trk.asked[0][0] != 1 || trk.asked[0][1] != 1000-state.TrackEdgeSec || trk.asked[0][2] != 1000+20+state.TrackEdgeSec+5 {
		t.Fatalf("the track window asked: %v", trk.asked)
	}
	if _, has := got["fight"]; !has {
		t.Fatal("the fight is missing")
	}
	// No frames: the key is absent (not null).
	trk.frames = nil
	code, got = getDetail(h, combat.Key("G", 1, "without"))
	if code != 200 {
		t.Fatalf("detail without frames: %d", code)
	}
	if _, has := got["meters"]; has {
		t.Fatalf("a fight without frames must carry no meters key: %s", got["meters"])
	}
	// A nil source (no state store) answers the same.
	h = combatFightDetailHandler(st, nil, nil)
	if _, got = getDetail(h, combat.Key("G", 1, "with")); got["meters"] != nil {
		t.Fatal("no source, no meters")
	}
	if code, _ := getDetail(h, combat.Key("G", 1, "nope")); code != 404 {
		t.Fatalf("unknown fight: %d", code)
	}
}

// TestFightDetailHistoryPassesStoredMeters: a past night's fight (not in memory) answers with the series its history line holds,
// and none when the line holds none.
func TestFightDetailHistoryPassesStoredMeters(t *testing.T) {
	now := time.Date(2026, 10, 28, 12, 0, 0, 0, time.UTC)
	clk := now
	hs, err := history.Open(history.Config{Dir: t.TempDir(), Now: func() time.Time { return clk }},
		func(context.Context, string, combat.Fight) int { return 0 }, nil)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { hs.Close(context.Background()) })
	hs.SetMeters(func(commander string, f combat.Fight) json.RawMessage {
		if f.ID == "with" {
			return metersJSON(&fakeTrack{frames: meterFrames(f.Start, 3)}, f)
		}
		return nil
	})
	for _, id := range []string{"with", "without"} {
		if !hs.Enqueue(history.Item{Session: 1, Fight: meterFight(id), At: now}) {
			t.Fatal("Enqueue refused")
		}
	}
	clk = now.Add(7 * time.Second)
	hs.Flush()
	h := combatFightDetailHandler(combat.NewStore(nil), hs, &fakeTrack{frames: meterFrames(1000, 3)})
	code, got := getDetail(h, combat.Key("G", 1, "with"))
	if code != 200 || !strings.Contains(string(got["hist"]), "true") || !strings.Contains(string(got["meters"]), `"src":"window"`) {
		t.Fatalf("history detail: %d %s / %s", code, got["hist"], got["meters"])
	}
	code, got = getDetail(h, combat.Key("G", 1, "without"))
	if code != 200 {
		t.Fatalf("history detail without: %d", code)
	}
	if _, has := got["meters"]; has {
		t.Fatalf("a line without meters answers without the key: %s", got["meters"])
	}
}
