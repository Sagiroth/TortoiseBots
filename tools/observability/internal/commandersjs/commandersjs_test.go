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
