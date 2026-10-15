// Package commandersjs runs the Commanders tab's pure logic (web/static/js/commanders.js, maplayers.js) under node.
package commandersjs

import (
	"os/exec"
	"testing"
)

func runCheck(t *testing.T, check, script string) {
	t.Helper()
	node, err := exec.LookPath("node")
	if err != nil {
		t.Skip("node is not installed")
	}
	out, err := exec.Command(node, check, script).CombinedOutput()
	if err != nil {
		t.Fatalf("%s failed: %v\n%s", check, err, out)
	}
}

// TestCommandersJS runs commanders_check.js. It skips where node is not installed (the build images only carry Go).
func TestCommandersJS(t *testing.T) {
	runCheck(t, "commanders_check.js", "../../web/static/js/commanders.js")
}

// TestMapLayersJS runs maplayers_check.js: the layer defaults, the one global saved choice and which layers have data.
func TestMapLayersJS(t *testing.T) {
	runCheck(t, "maplayers_check.js", "../../web/static/js/maplayers.js")
}

// TestUnitInfoJS runs unitinfo_check.js: what a character does, the hover box, health bars, threat colours, status icons, events and
// the run line.
func TestUnitInfoJS(t *testing.T) {
	runCheck(t, "unitinfo_check.js", "../../web/static/js/unitinfo.js")
}

// TestPartyRowsJS runs partyrows_check.js: the party table header and every row have the same number of cells, and app.js declares
// no helper twice.
func TestPartyRowsJS(t *testing.T) {
	runCheck(t, "partyrows_check.js", "../../web/static/js/app.js")
}

// TestPathsJS runs paths_check.js: bot trails and their stored history, run steps and their route, stall spots, objects, aggro radius, heal
// and crowd-control lines.
func TestPathsJS(t *testing.T) {
	runCheck(t, "paths_check.js", "../../web/static/js/paths.js")
}

// TestDrawingJS runs drawing_check.js: the SVG the drawing layers build in app.js (escaping, line styles, one hover entry per mark).
func TestDrawingJS(t *testing.T) {
	runCheck(t, "drawing_check.js", "../../web/static/js/app.js")
}

// TestDemoJS runs demo_check.js: the Commanders tab demo (a simulated group) gives every drawing layer something to show, through the
// same helpers as a real report.
func TestDemoJS(t *testing.T) {
	runCheck(t, "demo_check.js", "../../web/static/js/demo.js")
}
