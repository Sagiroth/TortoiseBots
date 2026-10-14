// Package mapdir serves the real game-map pictures behind the Commanders tab maps from an optional local folder.
//
// The pictures are the owner's own extracted game assets (turtle-agent's `minimap-extract` writes them), so they are never part of
// this repository or the embedded web files: the operator points TBDASH_MAP_DIR at a folder and the daemon
// serves from that folder and nothing else. The folder holds, per map id, `<id>.png` + `<id>.json` (the main picture) and
// optionally `<id>.<source>[-<n>f].png` + `.json` (source: minimap, worldmap or navmesh; `-<n>f` a floor). The JSON is
// minimap-extract's: map, name, source, width, height (pixels), px_per_yard and the world coordinates of the picture's edges, north
// and south (world x of the top and bottom edge) and west and east (world y of the left and right edge).
package mapdir

import (
	"encoding/json"
	"fmt"
	"net/http"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strconv"
	"strings"
)

// maxJSON bounds a metadata file; real ones are a few hundred bytes.
const maxJSON = 1 << 16

// Meta is the JSON beside a picture, as minimap-extract writes it.
type Meta struct {
	Map       uint32  `json:"map"`
	Name      string  `json:"name,omitempty"`
	Source    string  `json:"source"`
	Width     int     `json:"width"`
	Height    int     `json:"height"`
	PxPerYard float64 `json:"px_per_yard"`
	North     float64 `json:"north"`
	South     float64 `json:"south"`
	West      float64 `json:"west"`
	East      float64 `json:"east"`
}

// Layer is one picture of a map, ready for the page: the rectangle it covers in the Commanders map's plane (x = -game y, y = -game x:
// north up, west left, one unit a yard) and the URL it is served at.
type Layer struct {
	Name   string  `json:"name"`
	Source string  `json:"source"`
	URL    string  `json:"url"`
	X      float64 `json:"x"`
	Y      float64 `json:"y"`
	W      float64 `json:"w"`
	H      float64 `json:"h"`
}

// Dir is a folder of map pictures.
type Dir struct{ root string }

// FilePrefix is where Handler serves the files of the folder from.
const FilePrefix = "/api/v1/mapfiles/"

// MapsPrefix is where Handler answers the layer list of a map id.
const MapsPrefix = "/api/v1/maps/"

var (
	// fileName is every name the daemon will serve: no separator and no dot sequence can match it.
	fileName = regexp.MustCompile(`^(\d{1,9})(?:\.(minimap|worldmap|navmesh)(-\d+f)?)?\.(json|png)$`)
	jsonName = regexp.MustCompile(`^\d{1,9}\.(minimap|worldmap|navmesh)(-\d+f)?\.json$`)
)

// New opens the folder. An empty path gives a nil Dir (no real maps); a path that is not a directory is an error.
func New(root string) (*Dir, error) {
	if strings.TrimSpace(root) == "" {
		return nil, nil
	}
	abs, err := filepath.Abs(root)
	if err != nil {
		return nil, err
	}
	st, err := os.Stat(abs)
	if err != nil {
		return nil, err
	}
	if !st.IsDir() {
		return nil, fmt.Errorf("%s is not a directory", abs)
	}
	return &Dir{root: abs}, nil
}

// Root is the folder.
func (d *Dir) Root() string { return d.root }

// Plane is where a picture with these world edges lies in the Commanders map's plane. Game x is north and game y is west; the map
// draws north up and west left, so the picture's top-left corner (north, west) is the plane point (-west, -north), and it spans
// west-east yards across and north-south yards down.
func Plane(m Meta) (x, y, w, h float64) {
	return -m.West, -m.North, m.West - m.East, m.North - m.South
}

// path is the file of a served name, or "" for a name that is not one: only files directly in the folder, only regular files (a
// symbolic link is refused, so nothing outside the folder can be reached through one).
func (d *Dir) path(name string) string {
	if d == nil || !fileName.MatchString(name) {
		return ""
	}
	full := filepath.Join(d.root, name)
	if filepath.Dir(full) != d.root {
		return ""
	}
	st, err := os.Lstat(full)
	if err != nil || !st.Mode().IsRegular() {
		return ""
	}
	return full
}

func (d *Dir) readMeta(name string) (Meta, bool) {
	full := d.path(name)
	if full == "" {
		return Meta{}, false
	}
	if st, err := os.Stat(full); err != nil || st.Size() > maxJSON {
		return Meta{}, false
	}
	b, err := os.ReadFile(full)
	if err != nil {
		return Meta{}, false
	}
	var m Meta
	if json.Unmarshal(b, &m) != nil || m.Width <= 0 || m.Height <= 0 || !(m.West > m.East) || !(m.North > m.South) {
		return Meta{}, false
	}
	return m, true
}

// Layers lists the pictures the folder has for a map: the main one (<id>.png) first, then the others by name. A picture whose
// metadata is unreadable, or whose image file is missing, is left out.
func (d *Dir) Layers(id uint32) []Layer {
	if d == nil {
		return nil
	}
	base := strconv.FormatUint(uint64(id), 10)
	layer := func(file, name string, m Meta) (Layer, bool) {
		png := strings.TrimSuffix(file, ".json") + ".png"
		if d.path(png) == "" {
			return Layer{}, false
		}
		x, y, w, h := Plane(m)
		return Layer{Name: name, Source: m.Source, URL: FilePrefix + png, X: x, Y: y, W: w, H: h}, true
	}
	var out []Layer
	mainSrc := ""
	if m, ok := d.readMeta(base + ".json"); ok {
		if l, ok := layer(base+".json", m.Source, m); ok {
			out = append(out, l)
			mainSrc = m.Source
		}
	}
	ents, _ := os.ReadDir(d.root)
	var names []string
	for _, e := range ents {
		n := e.Name()
		if jsonName.MatchString(n) && strings.HasPrefix(n, base+".") && n != base+"."+mainSrc+".json" {
			names = append(names, n)
		}
	}
	sort.Strings(names)
	for _, n := range names {
		if m, ok := d.readMeta(n); ok {
			// "<id>.worldmap-2f.json" -> "worldmap 2f"
			name := strings.ReplaceAll(strings.TrimSuffix(strings.TrimPrefix(n, base+"."), ".json"), "-", " ")
			if l, ok := layer(n, name, m); ok {
				out = append(out, l)
			}
		}
	}
	return out
}

// HandleLayers answers GET /api/v1/maps/<map id> with the layer list, 404 for a map without pictures (or no folder).
func (d *Dir) HandleLayers(w http.ResponseWriter, r *http.Request) {
	id, err := strconv.ParseUint(strings.TrimPrefix(r.URL.Path, MapsPrefix), 10, 32)
	if err != nil || d == nil {
		http.NotFound(w, r)
		return
	}
	ls := d.Layers(uint32(id))
	if len(ls) == 0 {
		http.NotFound(w, r)
		return
	}
	b, _ := json.Marshal(ls)
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Cache-Control", "no-cache")
	_, _ = w.Write(b)
}

// HandleFile serves GET /api/v1/mapfiles/<name>: a .png or .json of the folder with a name minimap-extract writes, and nothing else.
func (d *Dir) HandleFile(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet && r.Method != http.MethodHead {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	name := strings.TrimPrefix(r.URL.Path, FilePrefix)
	full := d.path(name)
	if full == "" {
		http.NotFound(w, r)
		return
	}
	f, err := os.Open(full)
	if err != nil {
		http.NotFound(w, r)
		return
	}
	defer f.Close()
	st, err := f.Stat()
	if err != nil || !st.Mode().IsRegular() {
		http.NotFound(w, r)
		return
	}
	if strings.HasSuffix(name, ".png") {
		w.Header().Set("Content-Type", "image/png")
	} else {
		w.Header().Set("Content-Type", "application/json")
	}
	w.Header().Set("Cache-Control", "no-cache")
	w.Header().Set("X-Content-Type-Options", "nosniff")
	http.ServeContent(w, r, name, st.ModTime(), f)
}
