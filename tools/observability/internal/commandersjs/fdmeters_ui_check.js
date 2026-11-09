"use strict";
// Run by commandersjs_test.go under node: the Meters panel in app.js (web/static/js/app.js): the panel's markup for a series at the
// replay's place (ten damage rows of twelve, escaped names, the ~ on a bot whose curve is a ramp, the deaths), the "No meter data"
// text, the range, the live panel (its note, no ~, no Play button), the replay that needs no track once the detail has meters,
// the play row's meters-only note, and the pins that the replay tick and the 1 s live tick repaint the panel's body.
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");
const appPath = path.resolve(process.argv[2]);
const src = fs.readFileSync(appPath, "utf8");
const jsDir = path.dirname(appPath);

function grab(name) {
  const re = new RegExp(`^  function ${name}\\(`, "m"), m = re.exec(src);
  assert.ok(m, `${name} not found`);
  const end = src.indexOf("\n  }\n", m.index);
  return src.slice(m.index, end + 4);
}

const FightMeters = require(path.join(jsDir, "fightmeters.js"));
const FightReplay = require(path.join(jsDir, "fightreplay.js"));
const FightDebug = require(path.join(jsDir, "fightdebug.js"));
const FdView = require(path.join(jsDir, "fdview.js"));
const FdLive = require(path.join(jsDir, "fdlive.js"));
const ctx = { console, FightMeters, FightReplay, FightDebug, FdView, FdLive, document: { activeElement: null } };
vm.createContext(ctx);
const names = ["esc", "setHtml", "fdSet", "fdLiveClock", "fdHasMeters", "fdReplayActive", "fdMetersInputs", "fdMtRoleCls", "fdMtColumn", "fdMetersParts",
  "fdMetersSync", "fdMtShape", "fdMetersPaintBody", "fdPlayHtml", "fdPlaySync"];
vm.runInContext(["var cmdFd = {};",
  "const FD_MT_NOTE = 'Shape from the 10 s window, totals exact';",
  "const FD_MT_LIVE_NOTE = 'Estimated live from the 10 s window; exact totals when the fight ends.';",
  "const FD_MT_NONE = 'No meter data for this fight (the dashboard keeps it only for fights it saw while running)';"].concat(names.map(grab)).join("\n"), ctx);
const run = (expr, vars) => { if (vars) Object.assign(ctx, vars); return vm.runInContext(expr, ctx); };
const count = (s, re) => (s.match(re) || []).length;
const EVIL = "<b>x</b>";

// ---- a filed fight with 12 bots: damage i * 1000 over 8 s in four buckets, bot 2 a healer, bot 3 a ramp
const bots = [];
const series = { v: 1, step_ms: 2000, n: 4, src: "window", bots: [] };
for (let i = 1; i <= 12; i++) {
  bots.push({ name: i === 5 ? EVIL : "Bot" + i, guid: i, role: i === 2 ? "healer" : "dps" });
  const d = [0.25, 0.5, 0.75, 1].map((f) => Math.round(f * i * 1000));
  const h = i === 2 ? [100, 200, 300, 400] : [0, 0, 0, 0];
  series.bots.push(Object.assign({ g: i, d, h }, i === 3 ? { flat: true } : {}));
}
const fight = { id: "1-1", dur_ms: 8000, start: 1000, bots, deaths: [{ name: "Bot7", t_ms: 3000, killer: EVIL }, { name: "Bot9", t_ms: 7000, killer: "Boss" }] };
const inputs = (over) => Object.assign({ live: false, unbound: false, fight, series, tMs: 4000, endMs: 8000, range: null }, over);
const parts = (m) => run("fdMetersParts(m)", { m });
const deathsOf = (p) => p.body.split("<h4>Deaths</h4>")[1];

{
  const p = parts(inputs());
  assert.strictEqual(count(p.body, /class="fd-mtrow"/g), 10 + 1, "ten damage rows of twelve, and the one healer: " + p.body);
  assert.strictEqual(count(p.body.split("<h4>Healing</h4>")[0], /class="fd-mtrow"/g), 10, "ten damage rows");
  assert.ok(p.body.indexOf("Bot12") < p.body.indexOf("Bot11") && p.body.indexOf("Bot11") < p.body.indexOf("Bot3"), "biggest first");
  assert.ok(!/>Bot1</.test(p.body.split("<h4>Healing</h4>")[0]) && !/>Bot2</.test(p.body.split("<h4>Healing</h4>")[0]), "the two smallest are cut from the damage list");
  assert.ok(!p.body.includes(EVIL) && p.body.includes("&lt;b&gt;x&lt;/b&gt;"), "a bot named with markup is shown as text, in the name and its title");
  // t = 4000 is the end of bucket 1: half of each total; Bot12 12000 -> 6000, over 4 s = 1500 dps
  assert.ok(p.body.includes("6,000 · 1,500 dps") || /6.000 · 1.500 dps/.test(p.body), "value and dps (value / elapsed): " + p.body.slice(0, 600));
  assert.strictEqual(count(p.body, /~/g), 1, "one ~: the ramp bot's damage (its healing is 0, so it is not listed)");
  assert.ok(/>Bot3<\/span>.*?3?,?\d+~/.test(p.body.replace(/\n/g, "")) && /Bot3[^]*?~/.test(p.body), "and it is on Bot3's row");
  assert.ok(p.body.includes("Bot2") && p.body.includes("hps"), "the healer in the healing column");
  assert.ok(deathsOf(p).includes("0:03 Bot7 · &lt;b&gt;x&lt;/b&gt;") && !deathsOf(p).includes("Bot9"), "deaths up to 4 s only (Bot9 died at 7 s), the killer escaped");
  assert.strictEqual(p.note, "Shape from the 10 s window, totals exact");
  assert.strictEqual((p.body.match(/"/g) || []).length % 2, 0, "balanced quotes");
  assert.ok(/class="fd-mtfill" style="width:100%"/.test(p.body), "the top bar is full width");
}
// the end of the fight: the exact totals and both deaths; the replay's place beyond the end is the end
{
  const p = parts(inputs({ tMs: 8000 }));
  assert.ok(p.body.includes("12,000"), "the exact total at the end");
  assert.ok(deathsOf(p).includes("0:07 Bot9") && deathsOf(p).includes("0:03 Bot7") && deathsOf(p).indexOf("Bot9") < deathsOf(p).indexOf("Bot7"), "both deaths, newest first");
}
// a range: only what happened inside (4 s to 8 s is the second half of each total), the header says so, the deaths inside it only
{
  const p = parts(inputs({ tMs: 8000, range: { from: 4000, to: 8000 } }));
  assert.ok(p.note.startsWith("in the range 0:04 to 0:08 · "), "the note names the range: " + p.note);
  assert.ok(/6,000 · 1,500 dps/.test(p.body), "Bot12 did 6000 in the 4 s of the range");
  assert.ok(deathsOf(p).includes("Bot9") && !deathsOf(p).includes("Bot7"), "the death at 3 s is outside the range");
  // the replay place inside the range cuts the window at the place
  const q = parts(inputs({ tMs: 6000, range: { from: 4000, to: 8000 } }));
  assert.ok(/3,000 · 1,500 dps/.test(q.body), "at 6 s the window is 4 to 6 s: 3000 over 2 s");
  // a place before the range: nothing yet
  const z = parts(inputs({ tMs: 2000, range: { from: 4000, to: 8000 } }));
  assert.ok(!/fd-mtrow/.test(z.body) && z.body.includes("Nothing yet"), "before the range nothing has happened in it");
}
// no meter data: the text, nothing else
{
  const p = parts(inputs({ series: null }));
  assert.ok(p.body.includes("No meter data for this fight (the dashboard keeps it only for fights it saw while running)"), "the no-data text");
  assert.ok(!/fd-mtrow/.test(p.body), "no bars");
}
// live: the estimate's note, no ~ (nothing is scaled), values at now, the deaths from the live fight
{
  const lf = { dur_ms: 8000, bots, deaths: [{ name: "Bot7", t_ms: 3000, killer: "Boss" }] };
  const live = FightMeters.fromSamples([{ t: 1000 * 1000 + 6000, bots: { 3: { dps: 100, hps: 0 } }, w: 10 }, { t: 1000 * 1000 + 8000, bots: { 3: { dps: 100, hps: 0 } }, w: 10 }], 1000000, 8000);
  live.bots.forEach((b) => { b.flat = true; }); // even a series that said so is not marked: nothing is scaled live
  const p = parts({ live: true, unbound: false, fight: lf, series: live, tMs: 8000, endMs: 8000, range: null });
  assert.strictEqual(p.note, "Estimated live from the 10 s window; exact totals when the fight ends.");
  assert.ok(!p.body.includes("~") && p.body.includes("Bot3"), "no ~ in live");
  assert.ok(deathsOf(p).includes("0:03 Bot7 · Boss"), "the live deaths");
  assert.ok(parts({ live: true, unbound: true, fight: lf, series: null, tMs: 0, endMs: 0, range: null }).body.includes("Live meters need a bound commander"), "an unbound raid says so");
  assert.ok(parts({ live: true, unbound: false, fight: lf, series: null, tMs: 0, endMs: 0, range: null }).body.includes("Waiting for the first report"), "a live fight with no series yet");
}

// ---- fdMetersInputs: what the card's state turns into
{
  const KEY = "F1";
  const fd = { mode: "debug", sel: KEY, details: { [KEY]: { fight, meters: series } }, rp: { on: false, t: 0, playing: false, speed: 1, key: KEY }, view: FdView.create(KEY), liveRange: null };
  let m = run("fdMetersInputs(fd, null, null)", { fd });
  assert.deepStrictEqual([m.live, m.tMs, m.endMs, m.range, !!m.series], [false, 8000, 8000, null, true], "at rest the panel shows the whole fight");
  fd.rp = { on: true, t: 2500.4, playing: true, speed: 1, key: KEY };
  assert.strictEqual(run("fdMetersInputs(fd, null, null)").tMs, 2500, "replaying: the play head");
  fd.rp.t = 99999;
  assert.strictEqual(run("fdMetersInputs(fd, null, null)").tMs, 8000, "past the end is the end");
  fd.rp = { on: true, t: 3000, playing: false, speed: 1, key: "other" };
  assert.strictEqual(run("fdMetersInputs(fd, null, null)").tMs, 8000, "a replay of another fight does not move this one");
  fd.view = FdView.setRange(FdView.create(KEY), 2000, 6000, 8000, false);
  assert.deepStrictEqual(JSON.parse(JSON.stringify(run("fdMetersInputs(fd, null, null)").range)), { from: 2000, to: 6000 }, "the dash-1 range");
  delete fd.details[KEY].meters;
  assert.strictEqual(run("fdMetersInputs(fd, null, null)").series, null, "no meters in the detail");
  fd.details[KEY].meters = { v: 1, n: 0, bots: [] };
  assert.strictEqual(run("fdMetersInputs(fd, null, null)").series, null, "an empty series is no series");
  fd.sel = null;
  assert.strictEqual(run("fdMetersInputs(fd, null, null)"), null, "no fight picked: hidden");
  assert.strictEqual(run("fdMetersInputs({ mode: 'live', sel: 'x', details: {} }, null, null)"), null, "outside the debug mode: hidden");
  // live: from the state's samples and the synthetic fight
  const st = { cmd: "G", startMs: 1000000, samples: [{ t: 1000000 + 6000, bots: { 3: { dps: 100, hps: 0 } }, w: 10 }] };
  const lf = { dur_ms: 8000, bots, deaths: [] };
  const lv = { mode: "debug", sel: "live", details: {}, liveRange: { from: 1000, to: 8000 } };
  const lm = run("fdMetersInputs(lv, st, lf)", { lv, st, lf });
  assert.deepStrictEqual([lm.live, lm.unbound, lm.tMs, lm.endMs, lm.series.src], [true, false, 8000, 8000, "live-window"], "live: now, and the live series");
  assert.deepStrictEqual(JSON.parse(JSON.stringify(lm.range)), { from: 1000, to: 8000 }, "the follow range");
  assert.strictEqual(run("fdMetersInputs(lv, null, null)", { lv }), null, "a live view with no fight yet is hidden");
  assert.strictEqual(run("fdMetersInputs(lv, st, lf)", { lv, st: Object.assign({}, st, { cmd: null }) }).unbound, true, "no bound commander");
}

// ---- fdMetersSync draws the note and the body, hides the section without inputs
{
  const mk = () => ({ hidden: true, innerHTML: "" });
  const sec = mk(), note = mk(), body = mk();
  const card = { querySelector: (s) => (s === ".fd-meters" ? Object.assign(sec, { querySelector: (q) => (q === ".fd-mtnote" ? note : body) }) : null) };
  run("fdMetersSync(card, m)", { card, m: inputs() });
  assert.strictEqual(sec.hidden, false);
  assert.ok(body.innerHTML.includes("fd-mtrow") && note.innerHTML.includes("totals exact"), "painted");
  run("fdMetersSync(card, null)", { card });
  assert.strictEqual(sec.hidden, true, "no inputs: hidden");
  run("fdMetersSync(null, m)", { m: inputs() }); // a missing card is a no-op
}

// ---- a tick under one second of elapsed time reads as one second (no huge rate from a few hits)
{
  const p = parts(inputs({ tMs: 500 }));
  assert.ok(/750 · 750 dps/.test(p.body), "Bot12 has 750 at 0.5 s, shown over 1 s, not over 0.5 s: " + p.body.slice(0, 400));
  assert.ok(!/1,500 dps/.test(p.body), "the rate is not doubled by the short span");
}

// ---- the body is patched in place when the rows are the same, so the nodes stay and the width transition runs
{
  const ab = { bots: [{ name: "A", guid: 1, role: "dps" }, { name: "B", guid: 2, role: "dps" }], deaths: [] };
  const sr = { v: 1, step_ms: 2000, n: 4, src: "window", bots: [{ g: 1, d: [10, 20, 30, 40], h: [0, 0, 0, 0] }, { g: 2, d: [50, 50, 50, 50], h: [0, 0, 0, 0] }] };
  const body = { sets: 0, _i: "", fills: [], vals: [], _h: undefined };
  Object.defineProperty(body, "innerHTML", { get() { return this._i; }, set(v) {
    this._i = v; this.sets++;
    this.fills = (v.match(/class="fd-mtfill/g) || []).map(() => ({ style: { width: "" } }));
    this.vals = (v.match(/class="fd-mtval"/g) || []).map(() => ({ textContent: "" }));
  } });
  body.querySelectorAll = (sel) => (sel === ".fd-mtfill" ? body.fills : body.vals);
  const mk = (t, f, series) => ({ live: false, unbound: false, fight: f, series, tMs: t, endMs: 8000, range: null });
  const paint = (m) => run("fdMetersPaintBody(body, fdMetersParts(m).body)", { body, m });
  paint(mk(2000, ab, sr));
  assert.strictEqual(body.sets, 1, "the first paint writes the markup");
  const nodes = body.fills.slice(), vals = body.vals.slice();
  assert.strictEqual(nodes.length, 2, "two bars in the damage column");
  paint(mk(8000, ab, sr));
  assert.strictEqual(body.sets, 1, "the same rows at another time do not rewrite the markup");
  assert.ok(body.fills.every((n, i) => n === nodes[i]) && body.vals.every((n, i) => n === vals[i]), "the bar and value nodes are the same objects");
  assert.deepStrictEqual(body.fills.map((n) => n.style.width), ["100%", "80%"], "B stays the top, A grew from 20% to 80%: the width was patched");
  assert.ok(/^50/.test(body.vals[0].textContent) && /^40/.test(body.vals[1].textContent) && /dps/.test(body.vals[1].textContent), "and the value text: " + body.vals[1].textContent);
  paint(mk(2000, ab, sr));
  assert.deepStrictEqual(body.fills.map((n) => n.style.width), ["100%", "20%"], "and back");
  assert.strictEqual(body.sets, 1);
  // other rows (another fight's bots, a death) rebuild the markup
  paint(mk(2000, { bots: [{ name: "C", guid: 1, role: "dps" }, { name: "B", guid: 2, role: "dps" }], deaths: [] }, sr));
  assert.strictEqual(body.sets, 2, "another name is a new markup");
  paint(mk(2000, { bots: ab.bots, deaths: [{ name: "A", t_ms: 1000, killer: "x" }] }, sr));
  assert.strictEqual(body.sets, 3, "a new death is a new markup");
  paint(mk(2000, ab, null));
  assert.strictEqual(body.sets, 4, "no data is a new markup");
}

// ---- the live series is rebuilt when a sample arrived or the second moved, not on every repaint
{
  let calls = 0;
  const orig = FightMeters.fromSamples;
  FightMeters.fromSamples = (...a) => { calls++; return orig(...a); };
  try {
    const st = { cmd: "G", startMs: 1000000, version: 5, samples: [{ t: 1000000 + 6000, bots: { 3: { dps: 100, hps: 0 } }, w: 10 }] };
    const lv = { mode: "debug", sel: "live", details: {}, liveRange: null };
    const lf = (ms) => ({ dur_ms: ms, bots, deaths: [] });
    const at = (f) => run("fdMetersInputs(lv, st, f)", { lv, st, f });
    at(lf(8000)); at(lf(8400)); at(lf(8900));
    assert.strictEqual(calls, 1, "three repaints inside one second build the series once");
    at(lf(9000));
    assert.strictEqual(calls, 2, "the next second rebuilds it");
    st.version = 6; at(lf(9100));
    assert.strictEqual(calls, 3, "a new sample rebuilds it");
    assert.strictEqual(at(lf(9100)).series, st.mtMemo.series, "and the memo is the series handed out");
  } finally { FightMeters.fromSamples = orig; }
}

// ---- the replay needs no track once the detail has meters
{
  const KEY = "F1";
  const det = { fight, meters: series };
  const mkfd = (over) => Object.assign({ mode: "debug", sel: KEY, details: { [KEY]: det }, tracks: {}, rp: { on: true, t: 3000, playing: false, speed: 1, key: KEY } }, over);
  const act = (fd) => { ctx.cmdFd = { G: fd }; return run("fdReplayActive('G')"); };
  let a = act(mkfd());
  assert.ok(a && a.track === null && a.tMs === 3000 && a.tS === 1003 && a.fight === fight, "meters only: a replay with no track: " + JSON.stringify(a && a.tMs));
  a = act(mkfd({ tracks: { [KEY]: "loading" } }));
  assert.ok(a && a.track === null, "a track still loading does not stop the meters");
  a = act(mkfd({ tracks: { [KEY]: null } }));
  assert.ok(a && a.track === null, "no track kept");
  const goodTrack = { raw: {}, prep: { frames: [{ t: 1000, bots: {}, mobs: {} }, { t: 1002, bots: {}, mobs: {} }, { t: 1004, bots: {}, mobs: {} }], names: {}, t0: 1000, t1: 1004 } };
  a = act(mkfd({ tracks: { [KEY]: goodTrack } }));
  assert.ok(a && a.track === goodTrack, "with kept positions the track is used as before");
  assert.strictEqual(act(mkfd({ details: { [KEY]: { fight } } })), null, "no track and no meters: no replay (as before)");
  assert.strictEqual(act(mkfd({ rp: { on: false, t: 0, playing: false, speed: 1, key: KEY } })), null, "replay off");
  assert.strictEqual(act(mkfd({ mode: "live" })), null, "not the debugger");
  assert.strictEqual(act(mkfd({ sel: "live" })), null, "never in live");
}

// ---- the play row: enabled with meters, the note says positions are not kept; disabled and silent as before without
{
  const win = FightReplay.window(null, fight);
  const off = (h) => count(h, / disabled/g);
  const withM = run("fdPlayHtml(win, 8000, { speed: 1, on: false }, true)", { win });
  const without = run("fdPlayHtml(win, 8000, { speed: 1, on: false }, false)", { win });
  assert.strictEqual(off(withM), 0, "Play, speed and the scrub are enabled with meters: " + withM);
  assert.ok(withM.includes("Meters only, no positions kept"), "the note");
  assert.strictEqual(off(without), 3, "no positions and no meters: the three controls stay disabled");
  assert.ok(without.includes("No positions kept for this fight") && !without.includes("Meters only"));
  assert.ok(run("fdPlayHtml(win, 8000, { speed: 16, on: false }, true)", { win }).includes("16×"), "16x on the button");
  assert.strictEqual(FightReplay.coverageText({ ok: true, frames: 5, gapMaxS: 0 }, true).includes("Meters only"), false, "kept positions win over the meters-only note");
  // the row is rebuilt when the meters arrive (its key changes)
  const node = { dataset: {}, innerHTML: "", querySelector: () => null };
  const card = { querySelector: (s) => (s === ".fd-play" ? node : null) };
  const fd = { sel: "F1", rp: { on: false, t: 0, playing: false, speed: 1 } };
  run("fdPlaySync(card, fd, fight, null, false)", { card, fd, fight });
  const first = node.innerHTML;
  run("fdPlaySync(card, fd, fight, null, true)", { card, fd, fight });
  assert.notStrictEqual(node.innerHTML, first, "the row is rebuilt when the meters arrive");
  assert.strictEqual(node.dataset.fdKey, "F1|m");
}

// ---- wiring pins
{
  const tick = grab("fdReplaySync");
  assert.ok(tick.includes("fdMetersSync(card, fdMetersInputs(fd, null, null))") && tick.includes("fdHasMeters(det)"), "every replay step repaints the meters and tells the play row about them");
  assert.ok(!tick.includes("renderFightDebug"), "and never renders the card");
  const live = grab("fdLivePaintDebug");
  assert.ok(grab("fdMetersSync").includes("fdMetersPaintBody(sec.querySelector('.fd-mtbody'), p.body)"), "the panel's body goes through the in-place patch");
  assert.ok(live.includes("fdMetersSync(card, fdMetersInputs(fd, st, lf))"), "the 1 s live tick repaints the meters' body");
  const render = src.slice(src.indexOf("  function renderFightDebug("), src.indexOf("  // The window an underperf answer covers"));
  assert.ok(render.includes("fdMetersSync(card, noWin ? null : fdMetersInputs(fd, liveSt, liveFight));"), "the card render draws the panel (live: from the live fight)");
  assert.ok(render.includes(".fd-checks, .fd-meters, .fd-timeline, .fd-auras, .fd-events, .fd-roles').forEach(el => { el.hidden = noWin; });"), "the panel hides with the other fight panels");
  assert.ok(src.includes(".fd-checks, .fd-meters, .fd-timeline, .fd-auras, .fd-events, .fd-roles').forEach(el => { el.hidden = !debug; });"), "and outside the debug mode");
  assert.ok(src.includes('<section class="fd-meters" aria-label="Meters" hidden><h3 class="cmd-h">Meters <span class="fd-mtnote"></span></h3><div class="fd-mtbody"></div></section>'), "the card template");
  assert.ok(src.indexOf('class="fd-meters"') > src.indexOf('class="cmd-legend"') && src.indexOf('class="fd-meters"') < src.indexOf('<section class="fd-checks"'), "the panel sits right after the map box, inside the centre column");
  assert.ok(render.includes("fdSet(card.querySelector('.fd-play'), '');"), "a running fight (no filed fight) draws no replay row: no Play button in live");
  assert.ok(render.includes("if (fdHasMeters(det)) fdPlaySync(card, fd, fight, null, true);") && render.includes("No replay for past nights (positions are kept 30 min)"), "a past night's fight with meters plays; one without says so as before");
  const draw = grab("drawCmdMap");
  assert.ok(draw.includes("act && act.track ? act : fdRunActive(key)"), "a meters-only replay leaves the map on its own picture");
  assert.ok(!/fdMetersSync|FightMeters/.test(grab("fdTrackLoad")), "the track request is the one it was (no second path for meters)");
  const css = fs.readFileSync(path.join(jsDir, "..", "css", "app.css"), "utf8");
  assert.ok(/\.fd-mtfill \{[^}]*transition: width 200ms linear/.test(css) && /prefers-reduced-motion: reduce\) \{ \.fd-mtfill \{ transition: none; \}/.test(css), "the bars animate and reduced motion turns it off");
  assert.ok(css.includes(".fd-meters[hidden]"), "hidden means hidden");
  const html = fs.readFileSync(path.join(jsDir, "..", "..", "index.html"), "utf8");
  const at = (n) => html.indexOf("/static/js/" + n);
  assert.ok(at("fightmeters.js") > 0 && at("fightmeters.js") < at("app.js"), "fightmeters.js loads before app.js");
  assert.ok(/fightmeters\.js\?v=1/.test(html) && /fightreplay\.js\?v=3/.test(html) && /static\/js\/app\.js\?v=56/.test(html) && /app\.css\?v=31/.test(html), "the changed assets carry new versions");
}
console.log("fdmeters_ui_check.js ok");
