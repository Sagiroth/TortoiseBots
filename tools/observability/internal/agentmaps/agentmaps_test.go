package agentmaps

import (
	"encoding/json"
	"fmt"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"sync/atomic"
	"testing"
	"time"
)

func write(t *testing.T, dir, name, body string) {
	t.Helper()
	if err := os.WriteFile(filepath.Join(dir, name), []byte(body), 0o644); err != nil {
		t.Fatal(err)
	}
}

func fixture(t *testing.T) (string, *Dir) {
	t.Helper()
	dir := t.TempDir()
	write(t, dir, "run.svg", "<svg xmlns=\"http://www.w3.org/2000/svg\"><rect width=\"1\" height=\"1\"/></svg>")
	write(t, dir, "run.html", "<!doctype html><html><body><script>1</script></body></html>")
	write(t, dir, "run-f2.svg", "<svg/>")
	write(t, dir, "notes.txt", "not a map")
	write(t, dir, "run.svg.bak", "<svg/>")
	write(t, dir, ".hidden.svg", "<svg>SECRET</svg>")
	if err := os.Mkdir(filepath.Join(dir, "sub"), 0o755); err != nil {
		t.Fatal(err)
	}
	write(t, filepath.Join(dir, "sub"), "nested.svg", "<svg/>")
	base := time.Unix(1_700_000_000, 0)
	for i, name := range []string{"run.svg", "run.html", "run-f2.svg"} {
		if err := os.Chtimes(filepath.Join(dir, name), base.Add(time.Duration(i)*time.Minute), base.Add(time.Duration(i)*time.Minute)); err != nil {
			t.Fatal(err)
		}
	}
	d, err := New(dir)
	if err != nil {
		t.Fatal(err)
	}
	return dir, d
}

func get(h http.HandlerFunc, path string) *httptest.ResponseRecorder {
	rec := httptest.NewRecorder()
	h(rec, httptest.NewRequest(http.MethodGet, path, nil))
	return rec
}

func getPath(h http.HandlerFunc, path string) *httptest.ResponseRecorder {
	rec := httptest.NewRecorder()
	req := httptest.NewRequest(http.MethodGet, "/", nil)
	req.URL.Path = path // as a mux hands over a request whose Path was decoded (or a caller that did not decode)
	h(rec, req)
	return rec
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
	if len(none.List()) != 0 || none.Root() != "" {
		t.Fatal("a nil Dir has no maps and no root")
	}
}

func TestListNewestFirstAndOnlyMaps(t *testing.T) {
	_, d := fixture(t)
	entries := d.List()
	var names []string
	for _, e := range entries {
		names = append(names, e.Name)
	}
	want := []string{"run-f2.svg", "run.html", "run.svg"} // mtimes 2m, 1m, 0m
	if strings.Join(names, ",") != strings.Join(want, ",") {
		t.Fatalf("list = %v, want %v", names, want)
	}
	if entries[0].Kind != "svg" || entries[1].Kind != "html" || entries[2].Kind != "svg" {
		t.Fatalf("kinds = %+v", entries)
	}
	if entries[2].Size != int64(len("<svg xmlns=\"http://www.w3.org/2000/svg\"><rect width=\"1\" height=\"1\"/></svg>")) || entries[2].MTime == 0 {
		t.Fatalf("size/mtime = %+v", entries[2])
	}
}

func TestListCap(t *testing.T) {
	dir := t.TempDir()
	base := time.Unix(1_700_000_000, 0)
	for i := range MaxFiles + 5 {
		name := filepath.Join(dir, fmt.Sprintf("map-%03d.svg", i))
		if err := os.WriteFile(name, []byte("<svg/>"), 0o644); err != nil {
			t.Fatal(err)
		}
		if err := os.Chtimes(name, base.Add(time.Duration(i)*time.Second), base.Add(time.Duration(i)*time.Second)); err != nil {
			t.Fatal(err)
		}
	}
	d, err := New(dir)
	if err != nil {
		t.Fatal(err)
	}
	entries := d.List()
	if len(entries) != MaxFiles {
		t.Fatalf("list length = %d, want %d", len(entries), MaxFiles)
	}
	if entries[0].Name != "map-204.svg" || entries[len(entries)-1].Name != "map-005.svg" {
		t.Fatalf("newest %q oldest %q", entries[0].Name, entries[len(entries)-1].Name)
	}
}

func TestHandleList(t *testing.T) {
	_, d := fixture(t)
	rec := get(d.HandleList, ListPrefix)
	if rec.Code != 200 {
		t.Fatalf("list -> %d %s", rec.Code, rec.Body)
	}
	if ct := rec.Header().Get("Content-Type"); ct != "application/json; charset=utf-8" {
		t.Fatalf("content type %q", ct)
	}
	var payload struct {
		Files       []Entry `json:"files"`
		MaxFiles    int     `json:"max_files"`
		MaxFileSize int64   `json:"max_file_size"`
		Enabled     bool    `json:"enabled"`
	}
	if err := json.Unmarshal(rec.Body.Bytes(), &payload); err != nil {
		t.Fatal(err)
	}
	if len(payload.Files) != 3 || payload.MaxFiles != MaxFiles || payload.MaxFileSize != MaxFileSize || !payload.Enabled {
		t.Fatalf("payload = %+v", payload)
	}
	if rec := get(d.HandleList, ListPrefix+"/"); rec.Code != 404 {
		t.Errorf("trailing slash -> %d", rec.Code)
	}
	for _, method := range []string{http.MethodPost, http.MethodDelete, http.MethodPut} {
		rec := httptest.NewRecorder()
		d.HandleList(rec, httptest.NewRequest(method, ListPrefix, nil))
		if rec.Code != http.StatusMethodNotAllowed {
			t.Errorf("%s -> %d", method, rec.Code)
		}
	}
	// No folder: the probe answers 200 with enabled=false and no files, so a page load makes no failing request.
	var none *Dir
	off := get(none.HandleList, ListPrefix)
	if off.Code != 200 {
		t.Fatalf("no folder -> %d %s", off.Code, off.Body)
	}
	var disabled struct {
		Files   []Entry `json:"files"`
		Enabled bool    `json:"enabled"`
	}
	if err := json.Unmarshal(off.Body.Bytes(), &disabled); err != nil {
		t.Fatal(err)
	}
	if disabled.Enabled || len(disabled.Files) != 0 || !strings.Contains(off.Body.String(), `"enabled":false`) {
		t.Fatalf("no folder payload = %q", off.Body)
	}
	if rec := get(none.HandleFile, FilePrefix+"run.svg"); rec.Code != 404 {
		t.Errorf("no folder, a file -> %d", rec.Code)
	}
}

// A file named like markup is escaped by encoding/json (and rendered as text by the page): the list must not carry a raw tag.
func TestListEscapesMarkupNames(t *testing.T) {
	dir := t.TempDir()
	name := `<script>alert(1)<script>.svg`
	if err := os.WriteFile(filepath.Join(dir, name), []byte("<svg/>"), 0o644); err != nil {
		t.Fatal(err)
	}
	d, err := New(dir)
	if err != nil {
		t.Fatal(err)
	}
	rec := get(d.HandleList, ListPrefix)
	if rec.Code != 200 {
		t.Fatalf("list -> %d", rec.Code)
	}
	body := rec.Body.String()
	if strings.Contains(body, "<script>") {
		t.Fatalf("raw markup in the list: %s", body)
	}
	if !strings.Contains(body, `\u003cscript\u003e`) {
		t.Fatalf("name not HTML-escaped: %s", body)
	}
	var payload struct {
		Files []Entry `json:"files"`
	}
	if err := json.Unmarshal(rec.Body.Bytes(), &payload); err != nil || len(payload.Files) != 1 || payload.Files[0].Name != name {
		t.Fatalf("decoded list = %+v, %v", payload, err)
	}
}

func TestHandleFileContentTypesAndHeaders(t *testing.T) {
	_, d := fixture(t)
	rec := get(d.HandleFile, FilePrefix+"run.svg")
	if rec.Code != 200 || rec.Header().Get("Content-Type") != "image/svg+xml" {
		t.Fatalf("svg: %d %q %q", rec.Code, rec.Header().Get("Content-Type"), rec.Body)
	}
	csp := rec.Header().Get("Content-Security-Policy")
	if !strings.Contains(csp, "default-src 'none'") || !strings.Contains(csp, "sandbox") {
		t.Fatalf("svg CSP = %q", csp)
	}
	if strings.Contains(csp, "allow-scripts") {
		t.Fatalf("SVG must not allow scripts: %q", csp)
	}
	if rec.Header().Get("X-Content-Type-Options") != "nosniff" || rec.Header().Get("Cache-Control") != "no-cache" {
		t.Fatalf("headers = %v", rec.Header())
	}
	rec = get(d.HandleFile, FilePrefix+"run.html")
	if rec.Code != 200 || rec.Header().Get("Content-Type") != "text/html; charset=utf-8" {
		t.Fatalf("html: %d %q", rec.Code, rec.Header().Get("Content-Type"))
	}
	csp = rec.Header().Get("Content-Security-Policy")
	for _, want := range []string{"default-src 'none'", "connect-src 'none'", "script-src 'unsafe-inline'", "sandbox allow-scripts"} {
		if !strings.Contains(csp, want) {
			t.Fatalf("html CSP %q lacks %q", csp, want)
		}
	}
	if strings.Contains(csp, "allow-same-origin") {
		t.Fatalf("the replay must not share the dashboard origin: %q", csp)
	}
	// HEAD carries the headers and no body.
	rec = httptest.NewRecorder()
	d.HandleFile(rec, httptest.NewRequest(http.MethodHead, FilePrefix+"run.html", nil))
	if rec.Code != 200 || rec.Body.Len() != 0 || rec.Header().Get("Content-Type") != "text/html; charset=utf-8" {
		t.Fatalf("HEAD: %d %q body=%d", rec.Code, rec.Header().Get("Content-Type"), rec.Body.Len())
	}
}

// Every name that is not one .svg/.html directly in the folder must 404, whether the path arrived decoded or encoded.
func TestHandleFileRejectsTraversalAndNonMaps(t *testing.T) {
	dir, d := fixture(t)
	// A secret on each side of the folder, reachable only through traversal or the symlink below.
	outsideDir := filepath.Join(filepath.Dir(dir), "outside-"+filepath.Base(dir))
	if err := os.MkdirAll(outsideDir, 0o755); err != nil {
		t.Fatal(err)
	}
	write(t, outsideDir, "secret.svg", "<svg>SECRET</svg>")
	write(t, outsideDir, "secret.html", "<script>SECRET</script>")
	defer os.RemoveAll(outsideDir)
	// An in-folder name that would leak if the extension check or the hidden-file check were dropped remains on disk.
	write(t, dir, ".secret.svg", "<svg>SECRET</svg>")
	write(t, dir, "secret.txt", "SECRET")

	bad := []string{
		"", "run.svg/", "/etc/passwd", "/etc/passwd.svg", "C:\\secret.svg", `sub\secret.svg`, "sub/secret.svg",
		"../secret.svg", "..%2fsecret.svg", "%2e%2e/secret.svg", "%2e%2e%2fsecret.svg", "..\\secret.svg",
		"x%2f..%2fsecret.svg", "x%5c..%5csecret.svg", "../outside-" + filepath.Base(dir) + "/secret.svg",
		".secret.svg", ".hidden.svg", "secret.txt", "secret.html.bak", "run.svg.bak", "run.SVG", "run.HTML", "run",
		"run.svg\x00", "%00.svg", "run.svg%00.txt", "..", ".", "%2e", "run%2esvg",
	}
	for _, name := range bad {
		rec := getPath(d.HandleFile, FilePrefix+name)
		if rec.Code != 404 {
			t.Errorf("%q -> %d %q", name, rec.Code, rec.Body)
		}
		if strings.Contains(rec.Body.String(), "SECRET") {
			t.Errorf("%q leaked a file: %q", name, rec.Body)
		}
	}
	// encoded traversal as a real URL (net/http decodes it into Path)
	for _, raw := range []string{"..%2f..%2fetc%2fpasswd", "..%2f..%2fetc%2fpasswd.svg", "%2e%2e%2fsecret.svg"} {
		rec := httptest.NewRecorder()
		d.HandleFile(rec, httptest.NewRequest(http.MethodGet, FilePrefix+raw, nil))
		if rec.Code != 404 {
			t.Errorf("url %q -> %d", raw, rec.Code)
		}
	}
	for _, method := range []string{http.MethodPost, http.MethodPut, http.MethodDelete} {
		rec := httptest.NewRecorder()
		d.HandleFile(rec, httptest.NewRequest(method, FilePrefix+"run.svg", nil))
		if rec.Code != http.StatusMethodNotAllowed {
			t.Errorf("%s -> %d", method, rec.Code)
		}
	}
	rec := getPath(d.HandleFile, "/api/v1/other/run.svg")
	if rec.Code != 404 {
		t.Errorf("wrong prefix -> %d", rec.Code)
	}
	var none *Dir
	if rec := get(none.HandleFile, FilePrefix+"run.svg"); rec.Code != 404 {
		t.Errorf("no folder -> %d", rec.Code)
	}
}

// A symlink inside the folder that resolves outside it is refused; one that resolves to a file directly inside is served.
func TestHandleFileSymlinks(t *testing.T) {
	dir, d := fixture(t)
	outsideDir := filepath.Join(filepath.Dir(dir), "outside-"+filepath.Base(dir))
	if err := os.MkdirAll(outsideDir, 0o755); err != nil {
		t.Fatal(err)
	}
	write(t, outsideDir, "secret.svg", "<svg>SECRET</svg>")
	defer os.RemoveAll(outsideDir)
	if err := os.Symlink(filepath.Join(outsideDir, "secret.svg"), filepath.Join(dir, "escape.svg")); err != nil {
		t.Skipf("no symlinks here: %v", err)
	}
	if rec := get(d.HandleFile, FilePrefix+"escape.svg"); rec.Code != 404 || strings.Contains(rec.Body.String(), "SECRET") {
		t.Fatalf("escaping symlink -> %d %q", rec.Code, rec.Body)
	}
	// A symlink to a file in the same folder is a regular file as far as the viewer is concerned.
	if err := os.Symlink(filepath.Join(dir, "run.svg"), filepath.Join(dir, "alias.svg")); err != nil {
		t.Skipf("no symlinks here: %v", err)
	}
	if rec := get(d.HandleFile, FilePrefix+"alias.svg"); rec.Code != 200 {
		t.Fatalf("in-folder symlink -> %d", rec.Code)
	}
	// A directory symlink named like a map must not be served either.
	if err := os.Symlink(outsideDir, filepath.Join(dir, "dir.svg")); err != nil {
		t.Skipf("no symlinks here: %v", err)
	}
	if rec := get(d.HandleFile, FilePrefix+"dir.svg"); rec.Code != 404 {
		t.Fatalf("directory symlink -> %d", rec.Code)
	}
}

func TestHandleFileSizeCap(t *testing.T) {
	dir := t.TempDir()
	big := filepath.Join(dir, "big.svg")
	f, err := os.Create(big)
	if err != nil {
		t.Fatal(err)
	}
	if err := f.Truncate(MaxFileSize + 1); err != nil {
		f.Close()
		t.Fatal(err)
	}
	if err := f.Close(); err != nil {
		t.Fatal(err)
	}
	d, err := New(dir)
	if err != nil {
		t.Fatal(err)
	}
	if rec := get(d.HandleFile, FilePrefix+"big.svg"); rec.Code != http.StatusRequestEntityTooLarge {
		t.Fatalf("oversized map -> %d", rec.Code)
	}
	// A map exactly at the cap is still served.
	if err := os.Truncate(big, MaxFileSize); err != nil {
		t.Fatal(err)
	}
	if rec := get(d.HandleFile, FilePrefix+"big.svg"); rec.Code != 200 {
		t.Fatalf("map at the cap -> %d", rec.Code)
	}
}

// The pre-open Lstat is authoritative: a symbolic link swapped in after it must not be followed, even though the link resolves
// to a file outside the folder.
func TestHandleFileRefusesSymlinkSwappedAfterCheck(t *testing.T) {
	dir, d := fixture(t)
	outside := t.TempDir()
	secret := filepath.Join(outside, "secret.svg")
	write(t, outside, "secret.svg", "<svg>SECRET</svg>")
	target := filepath.Join(dir, "run-f2.svg")
	var once sync.Once
	testHookBeforeOpen = func(path string) {
		if path != target {
			return
		}
		once.Do(func() {
			if err := os.Remove(target); err != nil {
				t.Errorf("remove: %v", err)
				return
			}
			if err := os.Symlink(secret, target); err != nil {
				t.Errorf("symlink: %v", err)
			}
		})
	}
	t.Cleanup(func() { testHookBeforeOpen = nil })
	rec := get(d.HandleFile, FilePrefix+"run-f2.svg")
	if rec.Code != 404 {
		t.Fatalf("swapped symlink -> %d %q", rec.Code, rec.Body)
	}
	if strings.Contains(rec.Body.String(), "SECRET") {
		t.Fatalf("swapped symlink leaked a file: %q", rec.Body)
	}
}

// ServeContent measures a file by seeking, so a file that grows after the check would stream past MaxFileSize. The response must
// stay at the checked size.
func TestHandleFileServesAtMostTheCheckedSize(t *testing.T) {
	dir := t.TempDir()
	write(t, dir, "grow.svg", "ORIGINAL")
	d, err := New(dir)
	if err != nil {
		t.Fatal(err)
	}
	testHookAfterStat = func(path string) {
		if filepath.Base(path) != "grow.svg" {
			return
		}
		f, err := os.OpenFile(path, os.O_APPEND|os.O_WRONLY, 0)
		if err != nil {
			t.Errorf("open for append: %v", err)
			return
		}
		if _, err := f.WriteString("LIAR-LIAR"); err != nil {
			t.Errorf("append: %v", err)
		}
		_ = f.Close()
	}
	t.Cleanup(func() { testHookAfterStat = nil })
	rec := get(d.HandleFile, FilePrefix+"grow.svg")
	if rec.Code != 200 {
		t.Fatalf("grow -> %d", rec.Code)
	}
	if rec.Body.String() != "ORIGINAL" || rec.Header().Get("Content-Length") != "8" {
		t.Fatalf("served %q with length %q, want the checked bytes", rec.Body, rec.Header().Get("Content-Length"))
	}
}

// The stored root path is not proof: a folder renamed away and replaced by a symlink to another directory must stop both
// endpoints, instead of serving the replacement's files.
func TestRefusesWhenTheFolderPathIsReplaced(t *testing.T) {
	parent := t.TempDir()
	dir := filepath.Join(parent, "maps")
	if err := os.Mkdir(dir, 0o755); err != nil {
		t.Fatal(err)
	}
	write(t, dir, "run.svg", "<svg>inside</svg>")
	d, err := New(dir)
	if err != nil {
		t.Fatal(err)
	}
	outside := t.TempDir()
	write(t, outside, "run.svg", "<svg>SECRET</svg>")
	moved := dir + "-moved"
	if err := os.Rename(dir, moved); err != nil {
		t.Fatal(err)
	}
	if err := os.Symlink(outside, dir); err != nil {
		t.Skipf("no symlinks here: %v", err)
	}
	if rec := get(d.HandleList, ListPrefix); rec.Code != 404 {
		t.Fatalf("list after root replacement -> %d %q", rec.Code, rec.Body)
	}
	rec := get(d.HandleFile, FilePrefix+"run.svg")
	if rec.Code != 404 || strings.Contains(rec.Body.String(), "SECRET") {
		t.Fatalf("file after root replacement -> %d %q", rec.Code, rec.Body)
	}
	if entries := d.List(); len(entries) != 0 {
		t.Fatalf("List after root replacement = %+v", entries)
	}
}

// The probe's race loop: alternate the served name between a regular file and a symlink to an outside secret and require that no
// request ever returns the secret.
func TestHandleFileNeverServesAnOutsideSymlinkUnderRaces(t *testing.T) {
	dir := t.TempDir()
	outside := t.TempDir()
	secret := filepath.Join(outside, "secret.svg")
	write(t, outside, "secret.svg", "SECRET-OUTSIDE")
	write(t, dir, "reg", "<svg>inside</svg>")
	target := filepath.Join(dir, "a.svg")
	reg := filepath.Join(dir, "reg")
	if err := os.Link(reg, target); err != nil {
		t.Skipf("no hard links here: %v", err)
	}
	d, err := New(dir)
	if err != nil {
		t.Fatal(err)
	}
	var stop atomic.Bool
	done := make(chan struct{})
	go func() {
		defer close(done)
		for !stop.Load() {
			_ = os.Link(reg, filepath.Join(dir, "tmp1"))
			_ = os.Rename(filepath.Join(dir, "tmp1"), target)
			_ = os.Symlink(secret, filepath.Join(dir, "tmp2"))
			_ = os.Rename(filepath.Join(dir, "tmp2"), target)
		}
	}()
	deadline := time.Now().Add(time.Second)
	leaks, n := 0, 0
	for time.Now().Before(deadline) {
		n++
		rec := get(d.HandleFile, FilePrefix+"a.svg")
		if strings.Contains(rec.Body.String(), "SECRET-OUTSIDE") {
			leaks++
		}
	}
	stop.Store(true)
	<-done
	if leaks != 0 {
		t.Fatalf("%d of %d requests served the outside file", leaks, n)
	}
}
