// Package agentmaps serves turtle-agent run maps behind the dashboard's Agent maps tab from an optional local folder.
//
// `turtle-agent map` writes one SVG per run (run.svg, or run-f1.svg/run-f2.svg when a run spans floors) and, with --html, one
// self-contained HTML replay next to it. Those files never belong in this repository or in the embedded web files: the operator
// points --agent-maps-dir (or TBDASH_AGENT_MAPS_DIR) at the output folder and the daemon serves read-only copies from it and
// nothing else. The folder is flat (turtle-agent writes its files next to --out), so only files directly inside it are served.
//
// Everything a request can influence is validated before it reaches the filesystem: a name is served only when it is one path
// component with no control character, no leading dot and a lowercase .svg or .html extension, the file it names is a regular file
// (a symbolic link only when it resolves to a regular file directly inside the folder), and the request is a GET or HEAD. The
// list is capped at MaxFiles entries and a served file at MaxFileSize bytes.
package agentmaps

import (
	"encoding/json"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"unicode/utf8"
)

const (
	// MaxFiles bounds the list: the newest MaxFiles maps are returned, oldest dropped.
	MaxFiles = 200
	// MaxFileSize bounds one served map. A real run SVG is a few hundred KiB and the HTML replay with its embedded script and
	// style a few MiB; 32 MiB leaves ample room and keeps a request from streaming an arbitrarily large file.
	MaxFileSize = 32 << 20
	// maxNameLen bounds a served file name; every common filesystem stops at 255 bytes.
	maxNameLen = 255
)

// Dir is a folder of turtle-agent run maps.
type Dir struct{ root string }

const (
	// ListPrefix answers the list (and the feature probe: a 200 with enabled=false when the tab is off).
	ListPrefix = "/api/v1/agent-maps"
	// FilePrefix serves the maps of the folder from.
	FilePrefix = "/api/v1/agent-maps/files/"
)

// Entry is one map in the list, as the page needs it.
type Entry struct {
	Name  string `json:"name"`
	Size  int64  `json:"size"`
	MTime int64  `json:"mtime"`
	Kind  string `json:"kind"`
}

// New opens the folder. An empty path gives a nil Dir (the tab is off: the list answers enabled=false and no file is served); a
// path that is not a directory is an error.
func New(root string) (*Dir, error) {
	if strings.TrimSpace(root) == "" {
		return nil, nil
	}
	abs, err := filepath.Abs(root)
	if err != nil {
		return nil, err
	}
	// Resolve the folder once so the prefix check later compares canonical paths (the folder itself may be a symlink).
	resolved, err := filepath.EvalSymlinks(abs)
	if err != nil {
		return nil, err
	}
	st, err := os.Stat(resolved)
	if err != nil {
		return nil, err
	}
	if !st.IsDir() {
		return nil, fmt.Errorf("%s is not a directory", abs)
	}
	return &Dir{root: resolved}, nil
}

// Root is the folder.
func (d *Dir) Root() string {
	if d == nil {
		return ""
	}
	return d.root
}

// validName reports whether name is a file this package may serve: one path component, printable UTF-8, no leading dot (which
// also rejects "." and ".." and every hidden file), and a lowercase .svg or .html extension. Encoded dot or separator sequences
// are rejected too, so a handler called with an undecoded path cannot be walked out of the folder.
func validName(name string) bool {
	if name == "" || len(name) > maxNameLen || !utf8.ValidString(name) {
		return false
	}
	if strings.HasPrefix(name, ".") || filepath.Base(name) != name || filepath.IsAbs(name) {
		return false
	}
	for _, r := range name {
		if r == '/' || r == '\\' || r == 0 || r < 0x20 || r == 0x7f {
			return false
		}
	}
	lower := strings.ToLower(name)
	for _, enc := range []string{"%2e", "%2f", "%5c", "%00"} {
		if strings.Contains(lower, enc) {
			return false
		}
	}
	switch filepath.Ext(name) {
	case ".svg", ".html":
		return true
	}
	return false
}

// inside reports whether path lies directly in root. Both are canonical (root resolved by New, path by EvalSymlinks).
func (d *Dir) inside(path string) bool {
	return filepath.Dir(path) == d.root
}

// Test hooks, set only by tests: called after the pre-open Lstat (so a test can replace the validated path before it is opened)
// and after the opened file is measured (so a test can grow it before it is served).
var (
	testHookBeforeOpen func(string)
	testHookAfterStat  func(string)
)

// rootOK re-resolves the configured folder and requires it to still be the canonical directory New found at startup. The stored
// path alone is not proof: if the folder is renamed away and its path replaced by a symlink to another directory, every child
// path would follow that link. Refusing here keeps listing and serving to the folder the operator started the daemon with.
func (d *Dir) rootOK() bool {
	if d == nil {
		return false
	}
	resolved, err := filepath.EvalSymlinks(d.root)
	if err != nil {
		log.Printf("[AgentMaps] refusing %s: cannot re-resolve the folder: %v", d.root, err)
		return false
	}
	if resolved != d.root {
		log.Printf("[AgentMaps] refusing %s: the folder now resolves to %s, not the %s it was at startup", d.root, resolved, d.root)
		return false
	}
	st, err := os.Stat(resolved)
	if err != nil || !st.IsDir() {
		log.Printf("[AgentMaps] refusing %s: it is no longer a directory", d.root)
		return false
	}
	return true
}

// path is the regular file of a served name, or "" for a name that is not one. A symbolic link is followed only when it resolves to
// a regular file directly inside the folder, so nothing outside it can be reached through one; the target (never the link) is
// what the caller opens, and it starts life under the same checks.
func (d *Dir) path(name string) string {
	if d == nil || !validName(name) {
		return ""
	}
	full := filepath.Join(d.root, name)
	if filepath.Dir(full) != d.root {
		return ""
	}
	st, err := os.Lstat(full)
	if err != nil {
		return ""
	}
	if st.Mode()&os.ModeSymlink != 0 {
		resolved, err := filepath.EvalSymlinks(full)
		if err != nil || !d.inside(resolved) {
			return ""
		}
		st, err = os.Lstat(resolved)
		if err != nil || !st.Mode().IsRegular() {
			return ""
		}
		return resolved
	}
	if !st.Mode().IsRegular() {
		return ""
	}
	return full
}

// List returns the servable maps, newest first (ties by name), capped at MaxFiles. A file that is not servable (wrong extension,
// hidden, a link out of the folder) is left out, never an error.
func (d *Dir) List() []Entry {
	files := make([]Entry, 0)
	if d == nil || !d.rootOK() {
		return files
	}
	ents, err := os.ReadDir(d.root)
	if err != nil {
		return files
	}
	for _, e := range ents {
		name := e.Name()
		if !validName(name) {
			continue
		}
		full := d.path(name)
		if full == "" {
			continue
		}
		st, err := os.Lstat(full)
		if err != nil || !st.Mode().IsRegular() {
			continue
		}
		kind := "svg"
		if strings.HasSuffix(name, ".html") {
			kind = "html"
		}
		files = append(files, Entry{Name: name, Size: st.Size(), MTime: st.ModTime().Unix(), Kind: kind})
	}
	sort.Slice(files, func(i, j int) bool {
		if files[i].MTime != files[j].MTime {
			return files[i].MTime > files[j].MTime
		}
		return files[i].Name < files[j].Name
	})
	if len(files) > MaxFiles {
		files = files[:MaxFiles]
	}
	return files
}

// The list's answer. enabled is how the page learns the tab is off: no folder answers 200 with an empty list and enabled=false,
// so a dashboard load makes no failing request; a folder that stopped being usable still 404s, like its files.
type listPayload struct {
	Files       []Entry `json:"files"`
	MaxFiles    int     `json:"max_files"`
	MaxFileSize int64   `json:"max_file_size"`
	Enabled     bool    `json:"enabled"`
}

// HandleList answers GET /api/v1/agent-maps with {"files":[...],"max_files":N,"max_file_size":N,"enabled":true}, newest first.
// No folder: 200 with enabled=false and no files.
func (d *Dir) HandleList(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet && r.Method != http.MethodHead {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	if r.URL.Path != ListPrefix {
		http.NotFound(w, r)
		return
	}
	if d == nil {
		writeList(w, listPayload{Files: []Entry{}})
		return
	}
	if !d.rootOK() {
		http.NotFound(w, r)
		return
	}
	writeList(w, listPayload{Files: d.List(), MaxFiles: MaxFiles, MaxFileSize: MaxFileSize, Enabled: true})
}

func writeList(w http.ResponseWriter, payload listPayload) {
	// json.Marshal escapes <, > and & in strings, so a file named "<script>.svg" cannot break out when a client inserts the
	// decoded name into markup; the page additionally renders names as text, never as markup.
	b, err := json.Marshal(payload)
	if err != nil {
		http.Error(w, "cannot list the folder", http.StatusInternalServerError)
		return
	}
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	w.Header().Set("Cache-Control", "no-cache")
	w.Header().Set("X-Content-Type-Options", "nosniff")
	w.Header().Set("Content-Length", strconv.Itoa(len(b)))
	_, _ = w.Write(b)
}

// cspHTML: a replay is one file with its own inline script and style, and nothing else. No external fetch, no frame, no form, and
// sandbox without allow-same-origin: scripts run in an opaque origin that cannot read the dashboard's cookies, DOM or storage,
// even if the viewer opens the file directly in a tab.
const cspHTML = "default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; img-src data:; connect-src 'none'; " +
	"font-src 'none'; media-src 'none'; object-src 'none'; frame-src 'none'; child-src 'none'; worker-src 'none'; " +
	"base-uri 'none'; form-action 'none'; frame-ancestors 'self'; sandbox allow-scripts"

// cspSVG: an SVG needs no script at all. sandbox (no allow-scripts) disables script and external references; inline styles are
// what a generated map uses.
const cspSVG = "default-src 'none'; style-src 'unsafe-inline'; img-src data:; object-src 'none'; base-uri 'none'; " +
	"form-action 'none'; frame-ancestors 'self'; sandbox"

// HandleFile serves GET /api/v1/agent-maps/files/<name>: one .svg or .html directly in the folder. Everything else is 404; a map
// larger than MaxFileSize is 413. Files are served inline with a restrictive CSP so a replay can reach neither the dashboard
// origin nor the network.
func (d *Dir) HandleFile(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet && r.Method != http.MethodHead {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	if d == nil || !strings.HasPrefix(r.URL.Path, FilePrefix) || !d.rootOK() {
		http.NotFound(w, r)
		return
	}
	name := strings.TrimPrefix(r.URL.Path, FilePrefix)
	full := d.path(name)
	if full == "" {
		http.NotFound(w, r)
		return
	}
	// The authoritative check is the Lstat immediately before the open: the path must be a regular file, not a symbolic link.
	// The open itself then refuses a link swapped in after this check (O_NOFOLLOW; the descriptor is compared with this Lstat
	// as well), and it is non-blocking so a FIFO swapped in cannot hang the request. The opened descriptor must be that same
	// regular file.
	checked, err := os.Lstat(full)
	if err != nil || !checked.Mode().IsRegular() {
		http.NotFound(w, r)
		return
	}
	if testHookBeforeOpen != nil {
		testHookBeforeOpen(full)
	}
	f, err := openNoFollow(full)
	if err != nil {
		http.NotFound(w, r)
		return
	}
	defer f.Close()
	st, err := f.Stat()
	if err != nil || !st.Mode().IsRegular() || !os.SameFile(checked, st) {
		http.NotFound(w, r)
		return
	}
	if st.Size() > MaxFileSize {
		http.Error(w, "map too large", http.StatusRequestEntityTooLarge)
		return
	}
	if testHookAfterStat != nil {
		testHookAfterStat(full)
	}
	w.Header().Set("Cache-Control", "no-cache")
	w.Header().Set("X-Content-Type-Options", "nosniff")
	w.Header().Set("Referrer-Policy", "no-referrer")
	if strings.HasSuffix(name, ".html") {
		w.Header().Set("Content-Type", "text/html; charset=utf-8")
		w.Header().Set("Content-Security-Policy", cspHTML)
	} else {
		w.Header().Set("Content-Type", "image/svg+xml")
		w.Header().Set("Content-Security-Policy", cspSVG)
	}
	// Serve exactly the bytes measured above. ServeContent finds the size by seeking, so a file that grows after the check
	// would stream past MaxFileSize; a section reader keeps the response at the checked length.
	http.ServeContent(w, r, name, st.ModTime().UTC(), io.NewSectionReader(f, 0, st.Size()))
}
