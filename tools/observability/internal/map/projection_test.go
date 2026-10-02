package zoneproject

import (
	"math"
	"testing"

	"tortoise-observability/web"
)

// landmark ties a creature spawn (world x/y) to the in-game zone-map coordinate
// players see for it. The world positions come from tw_world.creature; the
// expected percentages are the published coordinates for those NPCs. The
// projection is shared by the dashboard's zone and world overlays, so a swapped
// axis, a wrong bounding-box source or a drifted zones.json all fail here.
var landmarks = []struct {
	name          string
	mapID, zoneID uint32
	x, y          float64
	wantX, wantY  float64
}{
	{"Marshal McBride (Northshire Abbey)", 0, 12, -8902.59, -162.606, 48.9, 41.6},
	{"Yarr Hammerstone (Kharanos)", 0, 1, -5528.87, -660.74, 50.0, 50.4},
	{"Krunn (Razor Hill)", 1, 14, 367.18, -4702.14, 52.0, 41.0},
	{"Antonas Riftgaze (Tower of Azora)", 0, 12, -9556.95, -717.488, 63.0, 70.0},
	{"Innkeeper Farley (Goldshire)", 0, 12, -9466.37, 21.4192, 42.0, 65.0},
}

const landmarkTolerance = 2.5

func newTestEngine(t *testing.T) *Engine {
	t.Helper()
	raw, err := web.FS.ReadFile("data/zones.json")
	if err != nil {
		t.Fatalf("read zones.json: %v", err)
	}
	engine, err := New(raw)
	if err != nil {
		t.Fatalf("New: %v", err)
	}
	return engine
}

func TestProjectLandmarks(t *testing.T) {
	engine := newTestEngine(t)
	for _, lm := range landmarks {
		t.Run(lm.name, func(t *testing.T) {
			gotX, gotY, ok := engine.Project(lm.mapID, lm.zoneID, lm.x, lm.y)
			if !ok {
				t.Fatalf("not projected (map %d zone %d)", lm.mapID, lm.zoneID)
			}
			if math.Abs(gotX-lm.wantX) > landmarkTolerance || math.Abs(gotY-lm.wantY) > landmarkTolerance {
				t.Errorf("got (%.1f, %.1f), want (%.1f, %.1f) +/- %.1f",
					gotX, gotY, lm.wantX, lm.wantY, landmarkTolerance)
			}
		})
	}
}

// Every WorldMapArea rect maps its own corners to the full 0..100 canvas.
func TestProjectRectCorners(t *testing.T) {
	engine := newTestEngine(t)
	for _, box := range engine.GetAllZones() {
		if box.LocLeft <= box.LocRight || box.LocTop <= box.LocBottom {
			t.Fatalf("%s: degenerate rect %+v", box.Name, box)
		}
		corners := []struct {
			name         string
			x, y         float64
			wantX, wantY float64
		}{
			{"north-west", box.LocTop, box.LocLeft, 0, 0},
			{"south-east", box.LocBottom, box.LocRight, 100, 100},
		}
		for _, tc := range corners {
			gotX, gotY, ok := engine.Project(box.MapID, box.AreaID, tc.x, tc.y)
			if !ok {
				t.Fatalf("%s/%s: not projected", box.Name, tc.name)
			}
			if math.Abs(gotX-tc.wantX) > 1e-6 || math.Abs(gotY-tc.wantY) > 1e-6 {
				t.Errorf("%s/%s: got (%.4f, %.4f), want (%.1f, %.1f)",
					box.Name, tc.name, gotX, gotY, tc.wantX, tc.wantY)
			}
		}
	}
}

func TestProjectClampsOutsideRect(t *testing.T) {
	engine := newTestEngine(t)
	box, ok := engine.GetZoneBox(0, 12) // Elwynn
	if !ok {
		t.Fatal("Elwynn box missing")
	}
	// Far north-west of the rect: both axes must clamp, never leave 0..100.
	gotX, gotY, ok := engine.Project(0, 12, box.LocTop+10000, box.LocLeft+10000)
	if !ok {
		t.Fatal("not projected")
	}
	if gotX != 0 || gotY != 0 {
		t.Errorf("north-west overflow = (%.1f, %.1f), want (0, 0)", gotX, gotY)
	}
}

// North/south and east/west must stay oriented: +X is north, +Y is west, so a
// point with a larger X projects higher (smaller pctY) and a larger Y projects
// further left (smaller pctX).
func TestProjectAxisOrientation(t *testing.T) {
	engine := newTestEngine(t)
	box, ok := engine.GetZoneBox(0, 12)
	if !ok {
		t.Fatal("Elwynn box missing")
	}
	midX := (box.LocTop + box.LocBottom) / 2
	midY := (box.LocLeft + box.LocRight) / 2

	northX, northY, _ := engine.Project(0, 12, midX+100, midY)
	southX, southY, _ := engine.Project(0, 12, midX-100, midY)
	if northY >= southY {
		t.Errorf("north y %.1f should be above south y %.1f", northY, southY)
	}
	if northX != southX {
		t.Errorf("north/south moved x: %.1f vs %.1f", northX, southX)
	}

	westX, westY, _ := engine.Project(0, 12, midX, midY+100)
	eastX, eastY, _ := engine.Project(0, 12, midX, midY-100)
	if westX >= eastX {
		t.Errorf("west x %.1f should be left of east x %.1f", westX, eastX)
	}
	if westY != eastY {
		t.Errorf("east/west moved y: %.1f vs %.1f", westY, eastY)
	}
}

func TestProjectUnknownZone(t *testing.T) {
	engine := newTestEngine(t)
	if _, _, ok := engine.Project(9999, 9999, 0, 0); ok {
		t.Error("unknown map/zone projected")
	}
}
