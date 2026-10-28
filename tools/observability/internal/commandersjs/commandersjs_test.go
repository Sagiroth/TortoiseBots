// Package commandersjs runs the Commanders tab's pure logic (web/static/js/commanders.js, maplayers.js) under node.
package commandersjs

import (
	"os/exec"
	"testing"
)

func runCheck(t *testing.T, check string, args ...string) {
	t.Helper()
	node, err := exec.LookPath("node")
	if err != nil {
		t.Skip("node is not installed")
	}
	out, err := exec.Command(node, append([]string{check}, args...)...).CombinedOutput()
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

// TestDungeonRunsJS runs dungeon_runs_check.js: the Dungeon runs tab and glance panel (formatting, empty states, boss table, wipe map), fed
// with the daemon's real answers for the Go fixtures.
func TestDungeonRunsJS(t *testing.T) {
	runCheck(t, "dungeon_runs_check.js", "../../web/static/js/dungeon-runs.js")
}

// TestFightsJS runs fights_check.js: the Fights panel's list rows, per-bot
// table, debuff bars, warning strip and pull post-mortem.
func TestFightsJS(t *testing.T) {
	runCheck(t, "fights_check.js", "../../web/static/js/fights.js")
}

// TestFightDebugJS runs fightdebug_check.js: the Fight debugger's checks, verdicts, death recaps, timeline, raid
// frames, role tables, fight rail and live window on walk 2.
func TestFightDebugJS(t *testing.T) {
	runCheck(t, "fightdebug_check.js", "../../web/static/js/fightdebug.js")
}

// TestFightDebugUIJS runs fightdebug_ui_check.js: the Fight debugger's markup in app.js (frames, rail, checks, timeline,
// recap, role tables, bot panel, map death marks) with the walk 2 fights and with names that would break the page.
func TestFightDebugUIJS(t *testing.T) {
	runCheck(t, "fightdebug_ui_check.js", "../../web/static/js/app.js")
}

// TestFightDebugState runs fightdebug_state_check.js: the debugger's state in app.js (the pick when a live fight ends, one
// fights-list fetch per commander, the failed-detail backoff and the state a departed commander's card releases).
func TestFightDebugState(t *testing.T) {
	runCheck(t, "fightdebug_state_check.js", "../../web/static/js/app.js")
}

// TestRefreshJS runs refresh_check.js: the refresh-rate picker's options, the choice saved per browser (also with a storage that throws)
// and the gate that holds live updates to the chosen rate or freezes them while paused.
func TestRefreshJS(t *testing.T) {
	runCheck(t, "refresh_check.js", "../../web/static/js/refresh.js")
}

// TestSpecLabelsJS runs speclabels_check.js: the Bots tab's spec labels (a shaman tank says "Enhancement (Tank)" while filters,
// chips and colours keep treating it as Enhancement) and the spec chips that never name a spec for a row without one.
func TestSpecLabelsJS(t *testing.T) {
	runCheck(t, "speclabels_check.js", "../../web/static/js/app.js")
}

// TestRefreshWiringJS runs refresh_wiring_check.js: app.js sends the roster and Commanders updates through the gate, no timer polls while
// paused, and the page loads refresh.js and offers the picker.
func TestRefreshWiringJS(t *testing.T) {
	runCheck(t, "refresh_wiring_check.js", "../../web/static/js/app.js")
}

// TestAgentMapsJS runs agentmaps_check.js: the Agent maps list (sizes, URL encoding, the viewer's sandbox and escaping a file name
// that looks like markup).
func TestAgentMapsJS(t *testing.T) {
	runCheck(t, "agentmaps_check.js", "../../web/static/js/agentmaps.js")
}

// TestFightDebugCSS runs fightdebug_css_check.js: the Fight debugger block in app.css (level tokens on the card, a rule for every
// class the card emits, the theme's colours only, the phone column, focus kept).
func TestFightDebugCSS(t *testing.T) {
	runCheck(t, "fightdebug_css_check.js", "../../web/static/css/app.css", "../../web/static/js/app.js")
}

// TestDungeonsJS runs dungeons_check.js: the Dungeons tab's run states, copy labels, party rows and raid subgroup blocks.
func TestDungeonsJS(t *testing.T) {
	node, err := exec.LookPath("node")
	if err != nil {
		t.Skip("node is not installed")
	}
	out, err := exec.Command(node, "dungeons_check.js",
		"../../web/static/js/commanders.js", "../../web/static/js/dungeons.js", "../../web/static/js/maplayers.js").CombinedOutput()
	if err != nil {
		t.Fatalf("dungeons_check.js failed: %v\n%s", err, out)
	}
}

// TestFightReplayJS runs fightreplay_check.js: the replay's interpolation between the kept positions, its fight window and the
// walk 2 track fixture.
func TestFightReplayJS(t *testing.T) {
	runCheck(t, "fightreplay_check.js", "../../web/static/js/fightreplay.js")
}

// TestAggroLanesJS runs aggrolanes_check.js: the aggro lanes of a fight (who each enemy targeted, sampled every 2 s).
func TestAggroLanesJS(t *testing.T) {
	runCheck(t, "aggrolanes_check.js", "../../web/static/js/aggrolanes.js")
}

// TestPlanOverlayJS runs planoverlay_check.js: the plan's route, stands, pack mobs and rings (scripted wakes from the
// plan's wake_yd), the step states and strays on Onyxia's plan and on Molten Core's multi-leg, multi-boss plan.
func TestPlanOverlayJS(t *testing.T) {
	runCheck(t, "planoverlay_check.js", "../../web/static/js/planoverlay.js")
}

// TestPlanOverlayUIJS runs planoverlay_ui_check.js: the plan layer as app.js draws it (each of its three layers, escaping,
// the hover entries) and the wiring that feeds it (the plan fetch, the card's plan facts, the raid presets, the Layers button).
func TestPlanOverlayUIJS(t *testing.T) {
	runCheck(t, "planoverlay_ui_check.js", "../../web/static/js/app.js")
}

// TestPhaseACSS runs phase_a_css_check.js: the Dashboard phase A block in app.css (the plan layer and the raid layers on the
// map, the replay row and the aggro lanes in the Fight debugger; theme tokens only apart from the eight group colours, a rule
// for every emitted class, the phone row, focus kept).
func TestPhaseACSS(t *testing.T) {
	runCheck(t, "phase_a_css_check.js", "../../web/static/css/app.css")
}
