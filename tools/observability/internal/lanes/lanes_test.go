package lanes

import (
	"encoding/json"
	"os"
	"path/filepath"
	"testing"

	"tortoise-observability/internal/combat"
	"tortoise-observability/internal/model"
	"tortoise-observability/internal/state"
)

// fixtureFrame is one frame of the tracked wire shape (internal/state TrackJSON), as the page and the attempt fixture store
// it: bots as [guid, x, y, hp %, alive, map], mobs as objects.
type fixtureFrame struct {
	T     int64        `json:"t"`
	Map   uint32       `json:"map"`
	Step  uint32       `json:"step"`
	Fight bool         `json:"fight"`
	Bots  [][]any      `json:"bots"`
	Mobs  []fixtureMob `json:"mobs"`
}

type fixtureMob struct {
	ID  string                `json:"id"`
	E   uint32                `json:"e"`
	X   float64               `json:"x"`
	Y   float64               `json:"y"`
	HP  uint8                 `json:"hp"`
	V   uint32                `json:"v"`
	VO  int                   `json:"vo"`
	RK  string                `json:"rk"`
	O   *float64              `json:"o"`
	CID uint32                `json:"cid"`
	CN  string                `json:"cn"`
	CT  uint32                `json:"ctot"`
	CTG string                `json:"ctg"`
	CH  int                   `json:"ch"`
	SH  *model.CommanderShape `json:"sh"`
}

type fixtureFight struct {
	Start int64  `json:"start"`
	DurMS uint32 `json:"dur_ms"`
	Map   uint32 `json:"map"`
	Bots  []struct {
		Name  string `json:"name"`
		GUID  uint32 `json:"guid"`
		Class string `json:"class"`
		Role  string `json:"role"`
	} `json:"bots"`
}

// loadAttempt loads internal/commandersjs/testdata/lanes-a2.json (fight debugger R5's committed attempt-2 fixture) as the
// frames state.Track returns and the fight the writer grades.
func loadAttempt(t *testing.T) ([]state.TrackFrame, combat.Fight) {
	t.Helper()
	return loadLanesFixture(t, "lanes-a2.json")
}

// loadLanesFixture loads a testdata fixture the page's aggrolanes_check.js reads too: the frames and the fight the writer grades.
func loadLanesFixture(t *testing.T, name string) ([]state.TrackFrame, combat.Fight) {
	t.Helper()
	raw, err := os.ReadFile(filepath.Join("..", "commandersjs", "testdata", name))
	if err != nil {
		t.Fatal(err)
	}
	var doc struct {
		Track struct {
			Frames []fixtureFrame `json:"frames"`
		} `json:"track"`
		Fight fixtureFight `json:"fight"`
	}
	if err := json.Unmarshal(raw, &doc); err != nil {
		t.Fatal(err)
	}
	frames := make([]state.TrackFrame, 0, len(doc.Track.Frames))
	for _, f := range doc.Track.Frames {
		out := state.TrackFrame{T: f.T, Map: f.Map, Step: f.Step, Fight: f.Fight}
		for _, r := range f.Bots {
			if len(r) < 6 {
				continue
			}
			out.Bots = append(out.Bots, state.TrackBot{
				GUID:  uint32(num(r[0])),
				X:     float32(num(r[1])),
				Y:     float32(num(r[2])),
				HP:    uint8(num(r[3])),
				Alive: num(r[4]) == 1,
				Map:   uint32(num(r[5])),
			})
		}
		for _, m := range f.Mobs {
			out.Mobs = append(out.Mobs, state.TrackMob{
				ID: m.ID, Entry: m.E, X: float32(m.X), Y: float32(m.Y), HP: m.HP, VictimBot: m.V, VictimOther: m.VO != 0,
				Rank: m.RK, O: m.O, CastID: m.CID, CastName: m.CN, CastTotal: m.CT, CastTarget: m.CTG, Channel: m.CH, Shape: m.SH,
			})
		}
		frames = append(frames, out)
	}
	f := combat.Fight{Start: doc.Fight.Start, DurMS: doc.Fight.DurMS, Map: doc.Fight.Map}
	for _, b := range doc.Fight.Bots {
		f.Bots = append(f.Bots, combat.FightBot{Name: b.Name, GUID: b.GUID, Class: b.Class, Role: b.Role})
	}
	return frames, f
}

func num(v any) float64 {
	switch x := v.(type) {
	case float64:
		return x
	case int:
		return float64(x)
	case int64:
		return float64(x)
	case uint32:
		return float64(x)
	}
	return 0
}

// TestNonTankAttempt pins the non-tank lane numbers of the recorded Onyxia attempt 2 (fight debugger R5's fixture): the same
// 23000 / 12000 the page's AggroLanes.lanes reports, so the daemon's stored seconds and the debugger's lane check agree.
func TestNonTankAttempt(t *testing.T) {
	frames, fight := loadAttempt(t)
	ntMs, longest, ok := NonTank(frames, fight)
	if !ok {
		t.Fatal("ok = false on a tracked fight")
	}
	if ntMs != 23000 || longest != 12000 {
		t.Fatalf("NonTank = (%d, %d), want (23000, 12000)", ntMs, longest)
	}
}

// TestNonTankWorstLaneBeyondTheDisplayCap pins the 14-lane parity fixture (internal/commandersjs/testdata/lanes-14.json, read by
// aggrolanes_check.js too): the page's strip shows 12 lanes, but both sides grade the worst non-tank lane over all 14.
func TestNonTankWorstLaneBeyondTheDisplayCap(t *testing.T) {
	frames, fight := loadLanesFixture(t, "lanes-14.json")
	ntMs, longest, ok := NonTank(frames, fight)
	if !ok || ntMs != 6000 || longest != 6000 {
		t.Fatalf("NonTank = (%d, %d, %v), want (6000, 6000, true) over all 14 lanes", ntMs, longest, ok)
	}
}

// TestNonTankNoFrames pins the "no replay for past nights" answer: a fight outside the kept track has no frames and the
// writer stores null.
func TestNonTankNoFrames(t *testing.T) {
	_, fight := loadAttempt(t)
	if ntMs, longest, ok := NonTank(nil, fight); ok || ntMs != 0 || longest != 0 {
		t.Fatalf("NonTank(nil) = (%d, %d, %v), want (0, 0, false)", ntMs, longest, ok)
	}
}
