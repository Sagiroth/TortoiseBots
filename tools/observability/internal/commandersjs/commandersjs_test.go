// Package commandersjs runs the Commanders tab's pure logic (web/static/js/commanders.js, maplayers.js) under node.
package commandersjs

import (
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
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

// TestReadyCheckJS runs readycheck_check.js: the ready check's pure rules on the attempt-2 bind of Onyxia's plan (the
// buff, consumable and key-cooldown facts, the dead, mana and stand rules) and on a party.
func TestReadyCheckJS(t *testing.T) {
	runCheck(t, "readycheck_check.js", "../../web/static/js/readycheck.js", "../../web/static/js/commanders.js")
}

// TestReadyCheckUIJS runs readycheck_ui_check.js: the ready check's markup in app.js (a dot per tile, the frames header
// and its toggle, the list of the not-ready bots, escaping).
func TestReadyCheckUIJS(t *testing.T) {
	runCheck(t, "readycheck_ui_check.js", "../../web/static/js/app.js")
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

// TestFDMatchUI runs fd_match_check.js: the debugger's header like the approved prototype (its own title and chips, the
// refresh picker, the short map legend, the raid frames' dead and mana counts) while Live mode keeps every dropped element.
func TestFDMatchUI(t *testing.T) {
	runCheck(t, "fd_match_check.js", "../../web/static/js/app.js")
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

// TestPlanOverlayMemoJS runs planoverlay_memo_check.js: the overlay kept per plan and step (the map draws it at about 30 fps)
// with the memoized strays still equal to the walk of every mob, on both plans and on a 1000-step, 1000-mob plan.
func TestPlanOverlayMemoJS(t *testing.T) {
	runCheck(t, "planoverlay_memo_check.js", "../../web/static/js/planoverlay.js")
}

// TestLivePosJS runs livepos_check.js: a pos frame's real gap is the gap the dots glide over (500 ms, not the 2 s report gap).
func TestLivePosJS(t *testing.T) {
	runCheck(t, "livepos_check.js", "../../web/static/js/app.js")
}

// TestLiveSyncJS runs live_sync_check.js: every tab change syncs the live stream, a departed commander's glide state and
// live events are pruned with its trails, and the focused map (the open debugger tab or the only card) glides at 60 fps
// while every other visible map glides at 15.
func TestLiveSyncJS(t *testing.T) {
	runCheck(t, "live_sync_check.js", "../../web/static/js/app.js")
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

// TestAggroLanesAttempts runs aggrolanes_attempts_check.js on attempts 1 and 3 of the recorded Onyxia attempts, regenerated
// into a temp dir by testdata/mk_lanes_fixture.py (attempt 2 is committed as lanes-a2.json). Skipped without the recordings.
func TestAggroLanesAttempts(t *testing.T) {
	home, err := os.UserHomeDir()
	if err != nil {
		t.Skip("no home directory: " + err.Error())
	}
	root := filepath.Join(home, "TortoiseBots-audit", "tests", "onyxia-attempts")
	if _, err := os.Stat(root); err != nil {
		t.Skip("the recorded attempts are not on this machine: " + root)
	}
	python, err := exec.LookPath("python3")
	if err != nil {
		t.Skip("python3 is not installed")
	}
	dir := t.TempDir()
	for _, c := range []struct {
		attempt         string
		ntMs, longestMs int64
	}{
		{"attempt-1", 52000, 8000},
		{"attempt-3", 20000, 14000},
	} {
		out := filepath.Join(dir, c.attempt+".json")
		if b, err := exec.Command(python, "-I", filepath.Join("testdata", "mk_lanes_fixture.py"),
			filepath.Join(root, c.attempt), out).CombinedOutput(); err != nil {
			t.Fatalf("mk_lanes_fixture.py %s: %v\n%s", c.attempt, err, b)
		}
		runCheck(t, "aggrolanes_attempts_check.js", "../../web/static/js/aggrolanes.js", out,
			strconv.FormatInt(c.ntMs, 10), strconv.FormatInt(c.longestMs, 10))
	}
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

// TestFightReplayUIJS runs fightreplay_ui_check.js: the fight replay's play row, the aggro lane bars, the replayed binding and
// the wiring that drives them, plus the floor rule of a multi-floor map picture.
func TestFightReplayUIJS(t *testing.T) {
	runCheck(t, "fightreplay_ui_check.js", "../../web/static/js/app.js")
}

// TestFightDebugRunUIJS runs fdrun_ui_check.js: the Fight debugger's run timeline (walking and fight blocks, deaths, the run
// scrubber and its clock), the one map layer control, the aggro lane caption, and the rail's total damage and bold key values.
func TestFightDebugRunUIJS(t *testing.T) {
	runCheck(t, "fdrun_ui_check.js", "../../web/static/js/app.js")
}

// TestFdTabsJS runs fdtabs_check.js: the Fight debugger's raid tabs (the strip of three commanders and twenty, only the open
// tab fetching details, the red-dot rule, the hash routing, escaping and the scrolling strip / one-column phone grid).
func TestFdTabsJS(t *testing.T) {
	runCheck(t, "fdtabs_check.js", "../../web/static/js/app.js", "../../web/static/js/fdtabs.js", "../../web/static/css/app.css")
}

// TestPhaseACSS runs phase_a_css_check.js: the Dashboard phase A block in app.css (the plan layer and the raid layers on the
// map, the replay row and the aggro lanes in the Fight debugger; theme tokens only apart from the eight group colours, a rule
// for every emitted class, the phone row, focus kept).
func TestPhaseACSS(t *testing.T) {
	runCheck(t, "phase_a_css_check.js", "../../web/static/css/app.css")
}

// TestGlideJS runs glide_check.js: the interpolation of the reported positions (halfway, the gap cap and floor, a teleport, a map
// change and a death snapping, a late sample starting from the shown position, the shallow copy the map draws, a backward
// clock jump or a new datagram session starting the card over, a reorder that never moves the clock back, position frames
// owning the delay while they run, and units no report carried for 15 s dropped).
func TestGlideJS(t *testing.T) {
	runCheck(t, "glide_check.js", "../../web/static/js/glide.js")
}

// TestBossOverlayJS runs bossoverlay_check.js: the boss overlay on the map and in the replay (the facing arrow, the spell's area,
// the cast ring and label, the escaping), and the CSS block that styles them.
func TestBossOverlayJS(t *testing.T) {
	runCheck(t, "bossoverlay_check.js", "../../web/static/js/app.js", "../../web/static/css/app.css")
}

// TestBossMapJS runs boss_check.js: a boss stands out on the Commanders map (its size and colour, the pulsing ring and its
// reduced-motion form, the crown above the body, the name with the health, the off-screen edge marker), it is drawn after
// the party, the replay keeps its rank, and the legend and the CSS agree.
func TestBossMapJS(t *testing.T) {
	runCheck(t, "boss_check.js", "../../web/static/js/app.js", "../../web/static/css/app.css")
}

// TestLiveViewJS runs live_check.js: the live stream's pure half (the commanders' URL, the copy-on-write pos merge, the refresh
// rates that may move the dots, the error fallback and the one-draw-per-frame scheduler: 60 fps for the focused map, 15 for
// the other visible maps, none for a hidden one, at most 8 glide-only draws per frame round robin).
func TestLiveViewJS(t *testing.T) {
	runCheck(t, "live_check.js", "../../web/static/js/live.js")
}

// TestFightPackParityJS runs fightpack_parity_check.js: web/static/js/planoverlay.js's fightPack and the Go port in
// internal/plans/fightpack.go answer the same on the shared fixture (internal/plans/testdata/fightpack-cases.json).
func TestFightPackParityJS(t *testing.T) {
	runCheck(t, "fightpack_parity_check.js", "../../web/static/js/planoverlay.js",
		filepath.Join("..", "plans", "testdata", "fightpack-cases.json"))
}

// TestFightHistoryJS runs fighthistory_check.js: the fight history's night picker and same-pack table
// (web/static/js/fighthistory.js) and the two builders app.js feeds from them (the day select, the pack rows that open a
// past fight, the Live card that belongs to Tonight, and every name escaped).
func TestFightHistoryJS(t *testing.T) {
	runCheck(t, "fighthistory_check.js", "../../web/static/js/fighthistory.js", "../../web/static/js/app.js")
}

// TestFdLiveEngagedJS runs fd_live_engaged_check.js: a live fight exists only while a hostile unit is engaged with the
// group (a hostile mob whose victim is a group member, or damage in the module's 10 s window). A parked raid casting
// buffs and healing shows no Live card and gets no live pick (so no LIVE checks panel); an engaged mob shows both; and
// with no hostile engaged for 10 s the fight closes.
func TestFdLiveEngagedJS(t *testing.T) {
	runCheck(t, "fd_live_engaged_check.js", "../../web/static/js/app.js", "../../web/static/js/fightdebug.js")
}

// TestCmdRouteJS runs cmd_route_check.js: the page's route in app.js. A profile opened from the Fight debugger keeps the raid
// tab and its hash, so the drawer closes onto the same view; switching tabs drops it, and the All raids deep link keeps its form.
func TestCmdRouteJS(t *testing.T) {
	runCheck(t, "cmd_route_check.js", "../../web/static/js/app.js")
}

// TestProfileXPJS runs profile_ui_check.js: the bot profile's XP bar (app.js xpPct/xpView). The level cap is named in words
// with no bar, below it the line is xp / next xp with the percent, and a missing field leaves the line out rather than
// printing "undefined".
func TestProfileXPJS(t *testing.T) {
	runCheck(t, "profile_ui_check.js", "../../web/static/js/app.js")
}

// TestPageAssetsJS runs page_assets_check.js: the page requests nothing the daemon does not serve. The map pictures start
// sourceless and load when the world view draws them, the agent-maps probe reads the daemon's enabled flag (an older daemon's
// 404 still hides the tab), and a demo group's made-up plan never reaches the plans API.
func TestPageAssetsJS(t *testing.T) {
	runCheck(t, "page_assets_check.js", "../../web/static/js/app.js")
}
