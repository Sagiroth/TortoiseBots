package mapdir

import (
	"encoding/json"
	"math"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// zf is turtle-agent's minimap-extract JSON for Zul'Farrak (map 209), as written.
const zf = `{"map":209,"name":"Zul'Farrak","source":"worldmap","width":1002,"height":668,"px_per_yard":0.7243390938342229,
"north":2052.080078125,"south":1129.1700439453125,"west":1625,"east":241.6699981689453,"tiles":12}`

const sm2f = `{"map":189,"name":"Scarlet Monastery","source":"worldmap","width":1002,"height":668,"px_per_yard":3.12,
"north":307.37,"south":93.91,"west":-162.27,"east":-482.46}`

func write(t *testing.T, dir, name, body string) {
	t.Helper()
	if err := os.WriteFile(filepath.Join(dir, name), []byte(body), 0o644); err != nil {
		t.Fatal(err)
	}
}

func fixture(t *testing.T) (string, *Dir) {
	t.Helper()
	dir := t.TempDir()
	write(t, dir, "209.json", zf)
	write(t, dir, "209.png", "\x89PNG-zf")
	write(t, dir, "209.worldmap.json", zf)
	write(t, dir, "209.worldmap.png", "\x89PNG-zf")
	write(t, dir, "209.navmesh.json", strings.Replace(zf, `"worldmap"`, `"navmesh"`, 1))
	write(t, dir, "209.navmesh.png", "\x89PNG-nav")
	write(t, dir, "189.worldmap-2f.json", sm2f)
	write(t, dir, "189.worldmap-2f.png", "\x89PNG-2f")
	write(t, dir, "secret.txt", "not for the web")
	write(t, dir, "209.exe", "x")
	d, err := New(dir)
	if err != nil {
		t.Fatal(err)
	}
	return dir, d
}

func TestNewEmptyAndMissing(t *testing.T) {
	if d, err := New(""); d != nil || err != nil {
		t.Fatalf("empty path: %v %v", d, err)
	}
	if d, err := New("   "); d != nil || err != nil {
		t.Fatalf("blank path: %v %v", d, err)
	}
	if _, err := New(filepath.Join(t.TempDir(), "nope")); err == nil {
		t.Fatal("a missing folder must be an error")
	}
	f := filepath.Join(t.TempDir(), "file")
	write(t, filepath.Dir(f), "file", "x")
	if _, err := New(f); err == nil {
		t.Fatal("a file is not a folder")
	}
	var none *Dir
	if none.Layers(209) != nil {
		t.Fatal("a nil Dir has no layers")
	}
}

// The picture lies where the game puts it: the pixel of a world point under the plane transform (x -> -y, y -> -x).
func TestPlaneMatchesPixelTransform(t *testing.T) {
	var m Meta
	if err := json.Unmarshal([]byte(zf), &m); err != nil {
		t.Fatal(err)
	}
	x, y, w, h := Plane(m)
	if math.Abs(w*m.PxPerYard-float64(m.Width)) > 1 || math.Abs(h*m.PxPerYard-float64(m.Height)) > 1 {
		t.Fatalf("plane size %.1f x %.1f yd is not %d x %d px at %.3f px/yd", w, h, m.Width, m.Height, m.PxPerYard)
	}
	// a point 100 yd south and 200 yd east of the north-west corner, in game coordinates (x north, y west)
	gx, gy := m.North-100, m.West-200
	px, py := (-gy-x)*m.PxPerYard, (-gx-y)*m.PxPerYard // plane point -> pixel inside the picture
	if math.Abs(px-200*m.PxPerYard) > 1e-6 || math.Abs(py-100*m.PxPerYard) > 1e-6 {
		t.Fatalf("pixel %.2f,%.2f, want %.2f,%.2f", px, py, 200*m.PxPerYard, 100*m.PxPerYard)
	}
	// the corners: north-west is the picture's top-left, south-east its bottom-right
	if x != -m.West || y != -m.North || x+w != -m.East || y+h != -m.South {
		t.Fatalf("corners wrong: %v %v %v %v", x, y, w, h)
	}
}

func TestLayers(t *testing.T) {
	_, d := fixture(t)
	ls := d.Layers(209)
	if len(ls) != 2 {
		t.Fatalf("map 209 layers = %+v", ls)
	}
	if ls[0].Source != "worldmap" || ls[0].URL != FilePrefix+"209.png" || ls[1].Name != "navmesh" || ls[1].URL != FilePrefix+"209.navmesh.png" {
		t.Fatalf("order or urls: %+v", ls)
	}
	if ls[0].X != -1625 || math.Abs(ls[0].W-(1625-241.6699981689453)) > 1e-6 {
		t.Fatalf("rect: %+v", ls[0])
	}
	ls = d.Layers(189)
	if len(ls) != 1 || ls[0].Name != "worldmap 2f" {
		t.Fatalf("map 189 layers = %+v", ls)
	}
	if d.Layers(70) != nil {
		t.Fatal("a map without pictures has no layers")
	}
}

func TestLayersSkipBrokenMeta(t *testing.T) {
	dir, d := fixture(t)
	write(t, dir, "33.json", `{"map":33,"source":"worldmap","width":10,"height":10,"north":1,"south":5,"west":5,"east":1}`) // north below south
	write(t, dir, "33.png", "x")
	write(t, dir, "34.json", "not json")
	write(t, dir, "34.png", "x")
	write(t, dir, "35.json", zf) // metadata without its picture
	if d.Layers(33) != nil || d.Layers(34) != nil || d.Layers(35) != nil {
		t.Fatal("broken metadata or a missing picture must not become a layer")
	}
}

func get(h http.HandlerFunc, path string) *httptest.ResponseRecorder {
	rec := httptest.NewRecorder()
	h(rec, httptest.NewRequest(http.MethodGet, path, nil))
	return rec
}

func TestHandleLayers(t *testing.T) {
	_, d := fixture(t)
	rec := get(d.HandleLayers, MapsPrefix+"209")
	if rec.Code != 200 || !strings.Contains(rec.Body.String(), `"url":"/api/v1/mapfiles/209.png"`) {
		t.Fatalf("%d %s", rec.Code, rec.Body)
	}
	for _, p := range []string{"70", "abc", "-1", "209/../189", "209.png", "99999999999999999999", ""} {
		if rec := get(d.HandleLayers, MapsPrefix+p); rec.Code != 404 {
			t.Errorf("%q -> %d", p, rec.Code)
		}
	}
	var none *Dir
	if rec := get(none.HandleLayers, MapsPrefix+"209"); rec.Code != 404 {
		t.Errorf("no folder -> %d", rec.Code)
	}
}

func TestHandleFileServesOnlyPngAndJsonOfTheFolder(t *testing.T) {
	dir, d := fixture(t)
	if rec := get(d.HandleFile, FilePrefix+"209.png"); rec.Code != 200 || rec.Header().Get("Content-Type") != "image/png" || rec.Body.String() != "\x89PNG-zf" {
		t.Fatalf("png: %d %q %q", rec.Code, rec.Header().Get("Content-Type"), rec.Body)
	}
	if rec := get(d.HandleFile, FilePrefix+"209.worldmap.json"); rec.Code != 200 || rec.Header().Get("Content-Type") != "application/json" {
		t.Fatalf("json: %d", rec.Code)
	}
	if rec := get(d.HandleFile, FilePrefix+"189.worldmap-2f.png"); rec.Code != 200 {
		t.Fatalf("floor: %d", rec.Code)
	}
	// a secret one level above the folder, and a symlink to it inside the folder
	outside := filepath.Join(filepath.Dir(dir), "outside-"+filepath.Base(dir))
	if err := os.WriteFile(outside+".png", []byte("secret"), 0o644); err != nil {
		t.Fatal(err)
	}
	defer os.Remove(outside + ".png")
	if err := os.Symlink(outside+".png", filepath.Join(dir, "300.png")); err != nil {
		t.Skipf("no symlinks here: %v", err)
	}
	bad := []string{
		"secret.txt", "209.exe", "209.PNG", "209.png.bak", "../209.png", "..%2f209.png", "%2e%2e/209.png", "..\\209.png", "/etc/passwd",
		"sub/209.png", "209.png/", "209.worldmap.txt", "209.hacked.png", "0209x.png", ".png", "209", "", "300.png", "209.png\x00.txt",
		"12345678901.png", "209.minimap-2.png", "209.worldmap-f.png", "70.png",
	}
	for _, name := range bad {
		rec := httptest.NewRecorder()
		req := httptest.NewRequest(http.MethodGet, "/", nil)
		req.URL.Path = FilePrefix + name // as the mux hands it over (already decoded)
		d.HandleFile(rec, req)
		if rec.Code != 404 {
			t.Errorf("%q -> %d %q", name, rec.Code, rec.Body)
		}
		if strings.Contains(rec.Body.String(), "secret") || strings.Contains(rec.Body.String(), "not for the web") {
			t.Errorf("%q leaked a file", name)
		}
	}
	// a name that is only encoded traversal in the URL form
	rec := httptest.NewRecorder()
	d.HandleFile(rec, httptest.NewRequest(http.MethodGet, FilePrefix+"..%2f..%2fetc%2fpasswd", nil))
	if rec.Code != 404 {
		t.Errorf("encoded traversal -> %d", rec.Code)
	}
	// only reads
	rec = httptest.NewRecorder()
	d.HandleFile(rec, httptest.NewRequest(http.MethodPost, FilePrefix+"209.png", nil))
	if rec.Code != http.StatusMethodNotAllowed {
		t.Errorf("POST -> %d", rec.Code)
	}
	var none *Dir
	if rec := get(none.HandleFile, FilePrefix+"209.png"); rec.Code != 404 {
		t.Errorf("no folder -> %d", rec.Code)
	}
}
