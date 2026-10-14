// Package commandersjs runs the Commanders tab's pure logic (web/static/js/commanders.js) under node.
package commandersjs

import (
	"os/exec"
	"testing"
)

// TestCommandersJS runs commanders_check.js. It skips where node is not installed (the build images only carry Go).
func TestCommandersJS(t *testing.T) {
	node, err := exec.LookPath("node")
	if err != nil {
		t.Skip("node is not installed")
	}
	out, err := exec.Command(node, "commanders_check.js", "../../web/static/js/commanders.js").CombinedOutput()
	if err != nil {
		t.Fatalf("commanders_check.js failed: %v\n%s", err, out)
	}
}
