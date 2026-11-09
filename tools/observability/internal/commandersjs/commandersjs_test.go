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

// TestFDTotalsJS runs fd_totals_check.js: the Fight debugger's "This fight | Total" switch (the .seg above the role
// tables, the run's sums with per-bot DPS/HPS, the title and the remembered choice) while the live table keeps its 10 s window.
func TestFDTotalsJS(t *testing.T) {
	runCheck(t, "fd_totals_check.js", "../../web/static/js/app.js")
}

// TestFDTotalsRenderJS runs fd_totals_render_check.js: renderFightDebug itself with a finished fight selected, once in
// "This fight" and once in "Total" mode (a fake fetch answering the totals), plus no selection and a live one.
func TestFDTotalsRenderJS(t *testing.T) {
	runCheck(t, "fd_totals_render_check.js", "../../web/static/js/app.js")
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

// TestFDLeanJS runs fd_lean_check.js: the lean fight rail (details only for the opened fight, clean trash folded per leg and run,
// the per-viewer "Show all fights" box, Tonight's two-hour window with "Load older", the keyed rail repaint, the run strip's
// summary death marks).
func TestFDLeanJS(t *testing.T) {
	runCheck(t, "fd_lean_check.js", "../../web/static/js/app.js")
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

// TestRaidsJS runs raids_check.js: the Raids tab's pure logic (group binds, list order, state word, window totals,
// the offline member reading as not alive).
func TestRaidsJS(t *testing.T) {
	runCheck(t, "raids_check.js",
		"../../web/static/js/commanders.js", "../../web/static/js/dungeons.js", "../../web/static/js/raids.js")
}

// TestRaidsPollJS runs raids_poll_check.js: /api/v1/raids is fetched on snapshot only while the Raids tab is on
// screen and no `raids` event came in 5 s, off screen once per 30 s.
func TestRaidsPollJS(t *testing.T) {
	runCheck(t, "raids_poll_check.js", "../../web/static/js/app.js")
}

// TestRaidsRouteJS runs raids_route_check.js: #/raids?run=&group= opens the card, the Fights button builds
// #/commanders/<key>/fight, cmdFindBind returns the observed bind, the Commanders Live list drops it and the
// debugger tab strip keeps it.
func TestRaidsRouteJS(t *testing.T) {
	runCheck(t, "raids_route_check.js", "../../web/static/js/app.js")
}

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

// TestGlideReplayJS runs glide_replay_check.js: a raid walk's live frames (testdata/walk-live-trace.json, built by
// mk_walk_trace.py) replayed in arrival order through app.js's handlers, the refresh gate and the glide, drawn every 16 ms:
// no unit is drawn stepping back more than 0.3 yd by the page (a late report, the render clock, a snap) and no dot jumps
// away and back; the server's own turn back is still drawn.
func TestGlideReplayJS(t *testing.T) {
	runCheck(t, "glide_replay_check.js", "../../web/static/js/app.js", "testdata/walk-live-trace.json")
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

// TestBossPanelJS runs bosspanel_check.js: the boss ability panel's table, next window, phase latch and filter, cast match by
// spell id, auras/flags/adds, add links and escaping.
func TestBossPanelJS(t *testing.T) {
	runCheck(t, "bosspanel_check.js", "../../web/static/js/bosspanel.js")
}

// TestBossUIJS runs bossui_check.js: the page's boss pieces — raid-frame debuff chips and MC tag, the killing spell (and the
// labelled 3 s guess), the timeline's Boss lane, the map's hollow boss and phase word, and a bot's debuff/MC/mark icons.
func TestBossUIJS(t *testing.T) {
	runCheck(t, "bossui_check.js", "../../web/static/js/app.js", "../../web/static/css/app.css")
}

// TestTrashJS runs trash_check.js: the trash abilities module (web/static/js/trash.js) — the KICK/AoE cast word, the entry and
// spell lookups, the one-line hint and the "Dangerous here" block — and, with app.js, the page pieces it drives: the word
// before an elite's cast, the danger-3 map glyph and the Live card's block.
func TestTrashJS(t *testing.T) {
	runCheck(t, "trash_check.js", "../../web/static/js/trash.js", "../../web/static/js/app.js", "../../web/static/css/app.css")
}

// TestLiveViewJS runs live_check.js: the live stream's pure half (the commanders' URL, the copy-on-write pos merge, the refresh
// rates that may move the dots, the error fallback and the one-draw-per-frame scheduler: 60 fps for the focused map, 15 for
// the other visible maps, none for a hidden one, at most 8 glide-only draws per frame round robin).
func TestLiveViewJS(t *testing.T) {
	runCheck(t, "live_check.js", "../../web/static/js/live.js")
}

// TestFdLiveJS runs fdlive_check.js: the live fight view's pure state (the event cursor with its frames, duplicates, gaps, resets and
// catch-up, the bounded buffers, the sampled aura rows, the follow range, the fight clock, the synthetic fight object, the
// hand-over match and the catch-up rate limit).
func TestFdLiveJS(t *testing.T) {
	runCheck(t, "fdlive_check.js", "../../web/static/js/fdlive.js")
}

// TestFdLiveUIJS runs fdlive_ui_check.js: the live fight view's wiring in app.js (the tile's escaped markup and link, the window
// labels, the hand-over's filed-row match, the live hash segment, and the stream handlers feeding the live view before any gate).
func TestFdLiveUIJS(t *testing.T) {
	runCheck(t, "fdlive_ui_check.js", "../../web/static/js/app.js")
}

// TestFightAurasJS runs fightauras_check.js: the Auras panel's pure logic (intervals from aura rows, alive spans, raid and per-bot
// uptime, boss-cast and death markers, who had the aura at each cast, cycles, applications, the boss presets, aura names, and the
// rows FdLive rebuilds from the 2 s boss-debuff samples).
func TestFightAurasJS(t *testing.T) {
	runCheck(t, "fightauras_check.js", "../../web/static/js/fightauras.js", "../../web/static/js/fdlive.js")
}

// TestFightAurasUIJS runs fightauras_ui_check.js: the Auras panel's markup and wiring in app.js (a bar per interval, the cast markers,
// escaped names, the hints, the live and fallback sampled rows, the pick kept through the hand-over, x=1 on the replay's track request).
func TestFightAurasUIJS(t *testing.T) {
	runCheck(t, "fightauras_ui_check.js", "../../web/static/js/app.js")
}

// TestFdViewJS runs fdview_check.js: the Fight debugger's time range as pure arithmetic and state (the pixel-to-time grid, the
// clamp and its 1 s minimum, membership, overlap, the label, the band style, set / clear / follow, and the live view's hand-over
// with its re-clamp).
func TestFdViewJS(t *testing.T) {
	runCheck(t, "fdview_check.js", "../../web/static/js/fdview.js")
}

// TestFdRangeUIJS runs fdrange_ui_check.js: the range in app.js (the band and the dimmed marks on the timeline, lanes and aura strip,
// the death recap that hides what is outside and counts it, the cycle Select buttons, the range bar, the live follow range and a
// fixed live range, the hand-over, and the wiring pins of the drag, the click guard and Escape).
func TestFdRangeUIJS(t *testing.T) {
	runCheck(t, "fdrange_ui_check.js", "../../web/static/js/app.js")
}

// TestFightEventsJS runs fightevents_check.js: the Events panel's one normalised event list (the eight kinds from the log, the aura
// rows, the boss's victim rows and each death's last hits, sorted by time, roles from the fight's bots, malformed rows skipped, the
// 5000 cap, the value lists, and the running fight object of FdLive building without target or hit events).
func TestFightEventsJS(t *testing.T) {
	runCheck(t, "fightevents_check.js", "../../web/static/js/fightevents.js", "../../web/static/js/fdlive.js")
}

// TestFdFilterJS runs fdfilter_check.js: the event filter's AND / OR / NOT tree (matching, the depth, condition and value limits,
// the two fields the game server does not send, the ability id match, the URL-safe text form and its caps, the pure tree edits, the
// range, the presets and the header line).
func TestFdFilterJS(t *testing.T) {
	runCheck(t, "fdfilter_check.js", "../../web/static/js/fdfilter.js", "../../web/static/js/fightevents.js")
}

// TestFdEventsUIJS runs fdevents_ui_check.js: the Events panel in app.js (500 rows then Show 500 more, escaped names, the seek on
// every row, the filter builder with school and hit disabled, the edits and their refusals, the timeline marks the filter dims, a
// running fight's newest-first list and live note, the 1 s repaint that leaves the builder alone, and the focus an edit gets back).
func TestFdEventsUIJS(t *testing.T) {
	runCheck(t, "fdevents_ui_check.js", "../../web/static/js/app.js")
}

// TestFdLayoutJS runs fdlayout_check.js: FdLayout (web/static/js/fdlayout.js), the pure side of the Fight debugger's panel layouts: the panel
// table and its default code (mech included), normalize (unknown ids, repeats, a bad span, missing panels), encode/decode (400-character and
// 60-item bounds, bad item syntax), the dialog's move/resize/show, and the named layouts in storage (junk, a storage that throws, hostile names,
// the 20-layout cap, name rules). The failing case: on a tree without fdlayout.js (the dash/fight-map head) the check cannot load the module.
func TestFdLayoutJS(t *testing.T) {
	runCheck(t, "fdlayout_check.js", "../../web/static/js/fdlayout.js")
}

// TestFdShareUIJS runs fdshare_ui_check.js: the Fight debugger's share links in app.js (the route reads and writes the view after bot and
// p, a new fight starts clean, the card writes its view with replaceState only and never from a tick, a link is applied once when its
// fight is on screen, the live hand-over rewrites the hash to the filed fight, Copy link, the link kept through login, a hostile link
// that carries no markup and breaks nothing).
func TestFdShareUIJS(t *testing.T) {
	runCheck(t, "fdshare_ui_check.js", "../../web/static/js/app.js")
}

// TestFightMetersJS runs fightmeters_check.js: the Meters panel's pure logic (at() inside a bucket, before 0 and after the end, window()
// of a range, rank() top ten of twelve, deathsBy() and fromSamples(), the live series built with the daemon's rules on the same fixture
// as internal/meters: a steady 100 dps for 20 s).
func TestFightMetersJS(t *testing.T) {
	runCheck(t, "fightmeters_check.js", "../../web/static/js/fightmeters.js")
}

// TestFdMetersUIJS runs fdmeters_ui_check.js: the Meters panel in app.js (ten damage rows of twelve, escaped names, the ~ on a ramp,
// the deaths, a range, the "No meter data" text, the live note with no ~ and no Play button, a replay that needs no track once the
// detail has meters, the play row's meters-only note, and the pins that the replay and the 1 s live tick repaint the body only).
func TestFdMetersUIJS(t *testing.T) {
	runCheck(t, "fdmeters_ui_check.js", "../../web/static/js/app.js")
}

// TestWipeRecapJS runs wiperecap_check.js: the pure logic of the "first 3 deaths" box (isWipe by outcome or half the bots dead, firstDeaths
// in time order with ties by name, hpBefore from the replay's frames, framesFromSamples for a live fight, hostile input).
func TestWipeRecapJS(t *testing.T) {
	runCheck(t, "wiperecap_check.js", "../../web/static/js/wiperecap.js")
}

// TestWipeRecapUIJS runs wiperecap_ui_check.js: the box in app.js (a wipe draws three rows that select a death, a calm fight draws
// nothing, a live wipe is headed "Wiping" with dashes and its note, the live box repaints only on a new death, escaping, the wiring).
func TestWipeRecapUIJS(t *testing.T) {
	runCheck(t, "wiperecap_ui_check.js", "../../web/static/js/app.js")
}

// TestFightMapJS runs fightmap_check.js: the pure logic of the Fight layers on the card map (trails cut by the range and split at a 40 yd jump,
// 1000 frames thinned to 300 points with the first and last kept, the cloud a Son of Hakkar seen in frames 1-3 and gone in 4 leaves with its
// radius from the seek_aura row or the default 4, the puller's path, the anchor circle and stand from the two Hakkar rows and nothing for a
// row with spot 0, heat cells (a 40 % drop and a death, 500 cells kept at 400), the focus filter through FdFilter, the fit box, the track and
// live frames, the Dungeons pull pins, hostile input).
func TestFightMapJS(t *testing.T) {
	runCheck(t, "fightmap_check.js", "../../web/static/js/fightmap.js", "../../web/static/js/fdfilter.js")
}

// TestFightMapUIJS runs fightmap_ui_check.js: the Fight layers in app.js (the source of a finished fight or a live view and nothing outside the
// debugger, the model rebuilt only when something it reads changed, trails, anchor, clouds, puller and heat each switched off alone, every name
// escaped including the death mark's killer, the one fit, the unit focus through FdFilter, the Dungeons pull pins and their 30 s request rule,
// and the wiring pins).
func TestFightMapUIJS(t *testing.T) {
	runCheck(t, "fightmap_ui_check.js", "../../web/static/js/app.js")
}

// TestFdMechJS runs fdmech_check.js: the per-fight mechanic row (the dash-2 Hakkar fixture: dosed 0/2 and, with two more applies, 1/2; the
// 80 % boundary; Son pulled by the samples' windows, unknown without samples; Jin'do's Hex time on the layout's main tank; Shade deaths by
// killer; any other boss's deaths; a capped aura list as a minimum; hostile input).
func TestFdMechJS(t *testing.T) {
	runCheck(t, "fdmech_check.js", "../../web/static/js/fdmech.js", "../../web/static/js/fightauras.js", "../../web/static/js/fdlive.js",
		filepath.Join("..", "mechanics", "testdata", "mt-rule.json"))
}

// TestMechPanelUIJS runs mechpanel_ui_check.js: the Boss mechanics section in app.js (one row per boss with the worst fight's link, the
// "not deployed" text for a null Hex or dose, this fight's row above the table, escaping, one request per 30 s and per window and only while
// open), the live tile's mechanic slot, the live bar's row, and the page's script order and versions.
func TestMechPanelUIJS(t *testing.T) {
	runCheck(t, "mechpanel_ui_check.js", "../../web/static/js/app.js")
}

// TestLoginTargetJS runs login_target_check.js: login.html's own dashboardTarget() on plain, hostile and over-long fragments (#//evil.example,
// #javascript:alert(1), a route of 5000 characters): the target is always /dashboard on this origin with at most a #/ route.
func TestLoginTargetJS(t *testing.T) {
	runCheck(t, "login_target_check.js", "../../web/login.html")
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
	// Nights are local (06:00 rollover), so the picker's absolute nights need a pinned zone; UTC keeps noon inside the night.
	t.Setenv("TZ", "UTC")
	runCheck(t, "fighthistory_check.js", "../../web/static/js/fighthistory.js", "../../web/static/js/app.js")
}

// TestDashDatesJS runs dashtime_check.js under Europe/Oslo: every clock time the dashboard shows is the browser's local
// time (feeds, incident table, "since HH:MM", pack table), nights roll over at 06:00 local while the server keeps its UTC
// day files, and the Saved fights list passes the row's unix seconds straight to whenLabel.
func TestDashDatesJS(t *testing.T) {
	t.Setenv("TZ", "Europe/Oslo")
	runCheck(t, "dashtime_check.js", "../../web/static/js/fighthistory.js", "../../web/static/js/app.js")
}

// TestNightParitySantiagoJS runs night_parity_check.js under America/Santiago: FightHistory.nightOf answers the same
// night as the Go nightOf over the hours around the midnight-DST days (2024-09-09 00:15 is the night of 2024-09-08).
func TestNightParitySantiagoJS(t *testing.T) {
	t.Setenv("TZ", "America/Santiago")
	runCheck(t, "night_parity_check.js", "../../web/static/js/fighthistory.js", filepath.Join("testdata", "night-parity-santiago.json"))
}

// TestNightParityNewYorkJS runs night_parity_check.js under America/New_York: the same parity over the US spring-forward
// and fall-back weekends.
func TestNightParityNewYorkJS(t *testing.T) {
	t.Setenv("TZ", "America/New_York")
	runCheck(t, "night_parity_check.js", "../../web/static/js/fighthistory.js", filepath.Join("testdata", "night-parity-newyork.json"))
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

// TestStaticLayerJS runs static_layer_check.js: the map's static group (grid, zone marks, the whole plan) is rebuilt only when
// its key changes. Sixty glide frames rebuild it zero times, a zoom, a pan, a layer switch, a new plan step or floor rebuild it
// once, and the plan's hover entries are re-registered every frame without a rebuild.
func TestStaticLayerJS(t *testing.T) {
	runCheck(t, "static_layer_check.js", "../../web/static/js/app.js")
}

// TestCmdOverflowCSS runs cmd_overflow_css_check.js: the Commanders cards never widen the page on a phone. The Fights panel
// (six nowrap columns) scrolls inside the card, the panel and the card may shrink below it, and no card-scoped rule fixes a
// width above 390 px.
func TestCmdOverflowCSS(t *testing.T) {
	runCheck(t, "cmd_overflow_css_check.js", "../../web/static/css/app.css")
}

// TestFDModeRouteJS runs fd_mode_route_check.js: the Live / Fight debugger switch keeps the route sensible. With nothing bound
// the debugger button keeps the hash (no bare #/fight), Live keeps the open commander in the URL, and the demo flag is applied
// before the first render so a demo deep link never flashes "is gone".
func TestFDModeRouteJS(t *testing.T) {
	runCheck(t, "fd_mode_route_check.js", "../../web/static/js/app.js")
}

// TestDungeonsPollJS runs dungeons_poll_check.js: /api/v1/dungeons is asked with every snapshot only while the Dungeons tab or
// a dungeon card is on screen, and once every 30 s anywhere else (the audit saw 30 requests a minute on a Commanders tab).
func TestDungeonsPollJS(t *testing.T) {
	runCheck(t, "dungeons_poll_check.js", "../../web/static/js/app.js")
}

// TestFDHistoryJS runs fd_history_check.js: the Fight debugger with no raid bound. The commanders with saved fights come from
// the daemon (newest first, escaped), the one-line note stays above the list, and each opens a read-only history card.
func TestFDHistoryJS(t *testing.T) {
	runCheck(t, "fd_history_check.js", "../../web/static/js/app.js")
}

// TestCoverageJS runs coverage_check.js: web/static/js/coverage.js against the shared fixture
// data/boss/testdata/coverage_cases.json (the parity half with tools/boss_coverage.py), the coverage route round-trips and
// the guard that no boss entry or spell id is written in the code.
func TestCoverageJS(t *testing.T) {
	runCheck(t, "coverage_check.js", "../../web/static/js/coverage.js",
		filepath.Join("..", "..", "..", "..", "data", "boss", "testdata", "coverage_cases.json"))
}

// TestCoverageUIJS runs coverage_ui_check.js: the raid coverage view's markup ("missing only" hides answered and not-needed
// lines and keeps the counts, an empty boss card is hidden, an untrusted row comment is escaped, the rows=null banner, a
// chip's aria label and the boss= card).
func TestCoverageUIJS(t *testing.T) {
	runCheck(t, "coverage_ui_check.js", "../../web/static/js/coverage.js")
}

// TestCoverageTruncatedJS runs coverage_truncated_check.js: a snapshot the daemon cut at a query cap marks every
// database-answered need unknown, keeps the rows out of the proof and shows the incomplete-coverage banner.
func TestCoverageTruncatedJS(t *testing.T) {
	runCheck(t, "coverage_truncated_check.js", "../../web/static/js/coverage.js")
}

// TestCoverageCSS runs coverage_css_check.js: the Raid coverage block in app.css (a rule for every class it emits, theme
// colours only, the one-column phone line, no fixed width above 358 px) and the boss panel's chip colours.
func TestCoverageCSS(t *testing.T) {
	runCheck(t, "coverage_css_check.js", "../../web/static/css/app.css", "../../web/static/js/coverage.js")
}

// TestLiveLayoutScrollJS runs live_layout_scroll_check.js: the live events strip's grid area, rows, colours and kept scroll, and
// the repaint focus restores that must not scroll the page (the auto scroll-up while a fight is live).
func TestLiveLayoutScrollJS(t *testing.T) {
	runCheck(t, "live_layout_scroll_check.js", "../../web/static/css/app.css", "../../web/static/js/app.js")
}

// TestControlsDebugJS runs controls_debug_check.js: the run controls (pause, resume, skip, stop, status, start, revive, gear)
// also show in the Fight debugger, under the raid frames, with the same buttons, binding and answer note.
func TestControlsDebugJS(t *testing.T) {
	runCheck(t, "controls_debug_check.js", "../../web/static/css/app.css", "../../web/static/js/app.js")
}
