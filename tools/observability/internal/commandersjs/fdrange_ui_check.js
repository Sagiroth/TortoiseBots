"use strict";
// Run by commandersjs_test.go under node: the Fight debugger's time range in app.js (web/static/js/app.js): the band and the dimmed marks
// on the timeline, the lanes and the aura strip, the death recap that hides what is outside the range (and says how many), the
// cycle Select buttons, the range bar, the live view's follow range and fixed range, the hand-over of a live view to its filed fight,
// and the wiring pins (the drag listeners, the click seek that skips the click ending a drag, Escape, the template slots).
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

const FightAuras = require(path.join(jsDir, "fightauras.js"));
const FdLive = require(path.join(jsDir, "fdlive.js"));
const FdView = require(path.join(jsDir, "fdview.js"));
const FightDebug = require(path.join(jsDir, "fightdebug.js"));
const Fights = require(path.join(jsDir, "fights.js"));
const ctx = { console, FightAuras, FdLive, FdView, FightDebug, Fights, document: { activeElement: null }, fdLive: {}, cmdFindBind: () => null };
vm.createContext(ctx);
const names = ["esc", "setHtml", "fdSet", "fdLiveClock", "fdLiveWinLabel", "fdAuIv", "fdAuState", "fdAuMarkVal", "fdAuPct", "fdAurasControlsHtml", "fdAurasRestHtml",
  "fdAurasParts", "fdAurasHtml", "fdAuLiveOpts", "fdRangeBandHtml", "fdOutCls", "fdTimelineHtml", "fdRecapHtml", "fdLanesHtml", "fdRangeBarHtml", "fdWholeTags",
  "fdViewFight", "fdRangeAction", "fdLiveRangeOf", "fdViewSync", "fdViewHandover", "fdLiveTimelineHtml"];
vm.runInContext(["var FD_AU_NAMES_MAX = 8; const fdAuMemo = new WeakMap();"].concat(names.map(grab)).join("\n"), ctx);
const run = (expr, vars) => { if (vars) Object.assign(ctx, vars); return vm.runInContext(expr, ctx); };
const J = (v) => JSON.parse(JSON.stringify(v));
const count = (s, re) => (s.match(re) || []).length;

const POISON = 24321, SIPHON = 24324;
const fight = () => ({
  boss: 14834, dur_ms: 120000,
  bots: [{ name: "A", guid: 1, role: "tank" }, { name: "B", guid: 2, role: "healer" }, { name: "C", guid: 3, role: "dps" }, { name: "D", guid: 4, role: "dps" }],
  auras: [[10000, 1, POISON, 1], [40000, 1, POISON, -1], [30000, 2, POISON, 1], [5000, 3, POISON, -1], [20000, 4, POISON, 1]],
  log: [{ k: "cast", t: 35000, n: "Hakkar", s: "Blood Siphon", sid: SIPHON }, { k: "cast", t: 95000, n: "Hakkar", s: "Blood Siphon", sid: SIPHON }],
  deaths: [{ name: "D", t_ms: 60000, killer: "k" }, { name: "C", t_ms: 90000, killer: "k" }],
});
const tlOf = (f) => ({ durMs: f.dur_ms, ticks: [], hits: [], bossCasts: [{ t_ms: 35000, pct: 29.17, n: "Hakkar", s: "Blood Siphon", sid: SIPHON, e: null, target: "" }],
  deaths: f.deaths.map((d, i) => ({ i, pct: d.t_ms / f.dur_ms * 100, name: d.name, t_ms: d.t_ms })) });

// ---- the timeline: the band under the marks, the marks outside the range dimmed (not removed)
{
  const f = fight(), range = { from: 30000, to: 60000 };
  const html = run("fdTimelineHtml(tl, recaps, null, r)", { tl: tlOf(f), recaps: FightDebug.deathRecaps(f), r: range });
  assert.ok(html.includes('<i class="fd-range" style="left:25.00%;width:25.00%"></i>'), "the band over 30-60 s of 120 s: " + html);
  assert.ok(html.indexOf("fd-range") < html.indexOf("fd-skull"), "the band is drawn first, so the marks sit over it");
  assert.ok(/class="fd-skull fd-death fd-out" data-fd-death="1"/.test(html), "the death at 90 s is outside and carries fd-out: " + html);
  assert.ok(/class="fd-skull fd-death" data-fd-death="0"/.test(html), "the death at 60 s (the range's end) is inside");
  assert.ok(/class="fd-bosscast bc-\d fd-out"/.test(html) === false, "the cast at 35 s is inside");
  const plain = run("fdTimelineHtml(tl, recaps, null)", { tl: tlOf(f), recaps: FightDebug.deathRecaps(f) });
  assert.ok(!plain.includes('class="fd-range"') && !plain.includes("fd-out"), "no range, no band and nothing dimmed");
  const none = run("fdTimelineHtml(tl, [], null, r)", { tl: { durMs: 120000, ticks: [], hits: [], bossCasts: [], deaths: [] }, r: range });
  assert.ok(none.includes("fd-range") && none.includes("No deaths this fight."), "the band is drawn on a timeline with no deaths too");
  const hit = run("fdTimelineHtml(tl, [], null, r)", { tl: { durMs: 120000, ticks: [], hits: [{ i: 0, pct: 10, d: 5, a: "x", h: 6, s: "" }], bossCasts: [], deaths: [] }, r: range });
  assert.ok(/class="fd-hit fd-out"/.test(hit), "a hit at 12 s is outside 30-60 s");
}
// ---- the recap: only deaths inside the range, and a line that says how many are left out
{
  const f = fight(), recaps = FightDebug.deathRecaps(f), range = { from: 30000, to: 60000 };
  recaps.forEach((r) => { r.hits = r.hits || []; });
  const html = run("fdRecapHtml(recaps, 1, r)", { recaps, r: range });
  assert.ok(html.includes("1 death outside the range"), "exactly one death is outside: " + html);
  assert.ok(!html.includes("1 deaths"), "singular for one");
  assert.ok(html.includes("Died") && html.includes("at 60.0 s") && !html.includes("at 90.0 s"), "the picked death (90 s) is outside, so the first inside shows: " + html);
  assert.strictEqual(run("fdRecapHtml(recaps, 0, r)", { r: { from: 0, to: 1000 } }).replace(/<[^>]+>/g, ""), "2 deaths outside the range", "none inside: only the line, plural");
  assert.ok(!run("fdRecapHtml(recaps, 1)").includes("outside the range"), "no range, no line, nothing hidden");
  assert.ok(run("fdRecapHtml(recaps, 1)").includes("at 90.0 s"), "no range: the picked one as before");
  assert.strictEqual(run("fdRecapHtml([], 0, r)", { r: range }), "", "no deaths at all: nothing");
}
// ---- the lanes: the band in each lane's bar
{
  const res = { lanes: [{ label: "Hakkar", segs: [{ fromMs: 0, toMs: 60000, kind: "tank", victim: "A" }], onTankPct: 100, badMs: 0, badNames: [] }], more: 0 };
  const html = run("fdLanesHtml(res, 120000, r)", { res, r: { from: 30000, to: 60000 } });
  assert.ok(html.includes('<i class="fd-range" style="left:25.00%;width:25.00%"></i></span>'), "the band closes the lane's bar: " + html);
  assert.ok(!run("fdLanesHtml(res, 120000)").includes('class="fd-range"'), "no range, no band");
}
// ---- the Auras panel: the band in every bar, the marker rows outside the range dimmed, the cycle Select buttons, the selects untouched
{
  const f = fight(), au = { key: "k", sid: POISON, marker: { sid: SIPHON, name: "Blood Siphon" } };
  const draw = (o) => run("fdAurasParts(f, FightAuras.intervals(f), au, o)", { f, au, o });
  const html = draw({ range: { from: 30000, to: 60000 } }).rest;
  assert.strictEqual(count(html, /<i class="fd-range" style="left:25.00%;width:25.00%"><\/i>/g), 5, "the raid bar and the four bots' bars");
  assert.ok(html.includes('data-fd-range-set="35000-95000"'), "the cycle from the cast at 35 s to the one at 95 s: " + html);
  assert.ok(/<button type="button" class="fd-link" data-fd-range-set="35000-95000">Select<\/button>/.test(html), "the Select button");
  assert.ok(/<i class="fd-au-mark" data-fd-bosscast="35000"/.test(html) && /<i class="fd-au-mark fd-out" data-fd-bosscast="95000"/.test(html), "the cast at 95 s is dimmed on the strip");
  assert.ok(/<tr class="fd-out"><td>2<\/td><td class="mono">1:35<\/td>/.test(html) && /<tr><td>1<\/td><td class="mono">0:35<\/td>/.test(html), "and in the table of casts: " + html);
  const bare = draw({}).rest;
  assert.ok(!bare.includes('class="fd-range"') && !bare.includes("fd-out"), "no range: no band, nothing dimmed");
  assert.ok(bare.includes('data-fd-range-set="35000-95000"'), "the Select button is there without a range too (it sets one)");
  assert.strictEqual(draw({ range: { from: 30000, to: 60000 } }).ctl, draw({ range: { from: 0, to: 5000 } }).ctl, "the selects' slot does not depend on the range, so an open list survives each repaint");
  assert.ok(html.includes("<th></th></tr></thead>"), "the cycle table's extra column");
  // the uptime follows the range (A is up 10-40 s: all of 30-40 s is 10 of 30 s)
  assert.ok(/fd-au-name r-tank">A<\/span><span class="fd-au-pct">33 %/.test(html), "bot A's uptime is measured inside the range: " + html);
}

// ---- the range bar
{
  const bar = (v, live, win) => run("fdRangeBarHtml(v, live, win)", { v, live, win });
  assert.ok(bar(FdView.create("k"), false, 0).includes("Drag across the timeline to pick a range."), "the post-fight hint");
  const set = FdView.setRange(FdView.create("k"), 72000, 125000, 200000, false);
  const html = bar(set, false, 0);
  assert.ok(html.includes("Range 1:12–2:05 (53 s)") && html.includes("data-fd-range-clear") && html.includes("data-fd-range-zoom") && html.includes("Zoom replay here"), html);
  assert.ok(!bar(FdView.create("k"), true, 60).includes("data-fd-"), "a following live view has no buttons");
  assert.ok(bar(FdView.create("k"), true, 60).includes("Following the last 60 s. Drag to fix a range."), "the live hint: " + bar(FdView.create("k"), true, 60));
  assert.ok(bar(FdView.create("k"), true, 120).includes("Following the last 2 min. Drag to fix a range."));
  assert.ok(bar(FdView.create("k"), true, 0).includes("Following the whole fight. Drag to fix a range."));
  const lfix = FdView.setRange(FdView.create("k"), 30000, 60000, 90000, true);
  const lh = bar(lfix, true, 60);
  assert.ok(lh.includes("Range 0:30–1:00 (30 s)") && lh.includes("data-fd-range-clear") && lh.includes("data-fd-follow") && lh.includes("Follow live"), lh);
  assert.ok(!lh.includes("Zoom replay here") && !lh.includes("data-fd-range-zoom"), "a running fight has no replay to zoom");
}
// ---- the whole-fight tags: shown while a range is set (live: only for a range the reader fixed)
{
  const tags = [{ hidden: true }, { hidden: true }];
  const card = { querySelectorAll: (q) => (q === ".fd-whole" ? tags : []) };
  const hid = () => tags.map((t) => t.hidden);
  run("fdWholeTags(card, v, live)", { card, v: FdView.create("k"), live: false });
  assert.deepStrictEqual(hid(), [true, true], "no range: hidden");
  run("fdWholeTags(card, v, live)", { v: FdView.setRange(FdView.create("k"), 0, 5000, 9000, false) });
  assert.deepStrictEqual(hid(), [false, false], "a range: shown");
  run("fdWholeTags(card, v, live)", { v: FdView.create("k") });
  assert.deepStrictEqual(hid(), [true, true], "cleared: removed");
  run("fdWholeTags(card, v, live)", { v: FdView.setRange(FdView.create("k"), 0, 5000, 9000, true), live: true });
  assert.deepStrictEqual(hid(), [false, false], "a fixed live range: shown");
  run("fdWholeTags(card, v, live)", { v: FdView.create("k"), live: true });
  assert.deepStrictEqual(hid(), [true, true], "the live follow window is the view's default, not a filter: hidden");
}

// ---- LIVE: the follow window, a fixed range that outlives the fight's growth, the hand-over
{
  const NOW = 5e12, key = "Cmd\u0000sess\u000012";
  const st = FdLive.create("Cmd");
  const hdr = (ms) => FdLive.applyEventsFrame(st, { key, from: 0, cur: 0, fight: { start_ms: NOW - ms, boss: 14834, boss_name: "Hakkar", fight_ms: ms } }, NOW);
  hdr(90000);
  const bind = { bots: [] };
  const fd = { liveWin: 60, view: FdView.create(null), liveRange: null };
  const lf = () => FdLive.fight(st, { bots: [] }, NOW);
  assert.strictEqual(lf().dur_ms, 90000);
  // follow, 60 s window, 90 s fight: 30-90 s, a band of two thirds at the right
  assert.deepStrictEqual(J(run("fdLiveRangeOf(fd, st, now)", { fd, st, now: NOW })), { from: 30000, to: 90000 }, "the follow window");
  assert.strictEqual(fd.view.key, "live:" + key, "the view belongs to this live fight");
  let html = run("fdLiveTimelineHtml(st, lf, fd.liveRange)", { lf: lf(), fd });
  assert.ok(html.includes("left:33.33%;width:66.67%"), "the follow band of a 90 s fight, window 60 s: " + html);
  // the window picker: Whole fight is no range
  fd.liveWin = 0;
  assert.strictEqual(run("fdLiveRangeOf(fd, st, now)", { fd, st, now: NOW }), null, "Whole fight follows with no range");
  fd.liveWin = 60;
  // a fixed range (a drag): the window no longer matters, and the fight growing to 120 s leaves it where it was, drawn against the new length
  fd.view = FdView.setRange(fd.view, 30000, 60000, 90000, true);
  assert.strictEqual(fd.view.follow, false);
  assert.deepStrictEqual(J(run("fdLiveRangeOf(fd, st, now)", { fd, st, now: NOW })), { from: 30000, to: 60000 });
  html = run("fdLiveTimelineHtml(st, lf, fd.liveRange)", { lf: lf(), fd });
  assert.ok(html.includes("left:33.33%;width:33.33%"), "the fixed band at 90 s: " + html);
  hdr(120000);
  assert.strictEqual(lf().dur_ms, 120000);
  assert.deepStrictEqual(J(run("fdLiveRangeOf(fd, st, now)", { fd, st, now: NOW })), { from: 30000, to: 60000 }, "the fixed range keeps its place as the fight grows");
  html = run("fdLiveTimelineHtml(st, lf, fd.liveRange)", { lf: lf(), fd });
  assert.ok(html.includes("left:25.00%;width:25.00%"), "its band follows the new length: " + html);
  assert.strictEqual(fd.view.follow, false, "still fixed");
  // another live fight starts with no range and follows
  const st2 = FdLive.create("Cmd");
  FdLive.applyEventsFrame(st2, { key: "Cmd\u0000sess\u000013", from: 0, cur: 0, fight: { start_ms: NOW - 1000, fight_ms: 1000 } }, NOW);
  run("fdLiveRangeOf(fd, st2, now)", { st2 });
  assert.deepStrictEqual(J(fd.view), { key: "live:Cmd\u0000sess\u000013", range: null, follow: true, reclamp: false }, "the next fight resets the view");
  // Follow live / Clear / the window picker: follow again
  fd.view = FdView.setRange(FdView.create("live:" + key), 30000, 60000, 90000, true);
  assert.ok(run("fdRangeAction('k', fd, node)", { fd, node: { dataset: { fdFollow: "" } } }));
  assert.deepStrictEqual(J(fd.view), { key: "live:" + key, range: null, follow: true, reclamp: false }, "Follow live");
  fd.view = FdView.setRange(FdView.create("live:" + key), 30000, 60000, 90000, true);
  assert.ok(run("fdRangeAction('k', fd, node)", { node: { dataset: { fdRangeClear: "" } } }));
  assert.strictEqual(fd.view.follow, true, "Clear in a live view is Follow live");
  // a cycle's Select button in a live view sets a fixed range and stops following
  fd.sel = "live";
  run("fdLive = { k: st }; cmdFindBind = () => bind;", { st, bind });
  assert.ok(run("fdRangeAction('k', fd, node)", { node: { dataset: { fdRangeSet: "35000-95000" } } }));
  assert.deepStrictEqual(J(fd.view.range), { from: 35000, to: 95000 });
  assert.strictEqual(fd.view.follow, false, "Select in a live view fixes the range");
  // ---- the hand-over
  const fixedView = FdView.setRange(FdView.create("live:" + key), 30000, 60000, 90000, true);
  const f1 = { view: fixedView, sel: "live" };
  run("fdViewHandover(f1, 'filed')", { f1 });
  assert.deepStrictEqual(J(f1.view), { key: "filed", range: { from: 30000, to: 60000 }, follow: false, reclamp: true }, "a fixed range is kept, under the filed key (so the reset rule does not fire)");
  run("fdViewSync(f1, 'filed')");
  assert.strictEqual(f1.view.range.from, 30000, "the sync at the first render of the filed fight does not reset it");
  f1.view = FdView.reclamp(f1.view, 45000);
  assert.deepStrictEqual(J(f1.view.range), { from: 30000, to: 45000 }, "and it is re-fitted to the filed fight's length on that render");
  const f2 = { view: { key: "live:" + key, range: { from: 0, to: 5000 }, follow: true, reclamp: false }, sel: "live" };
  run("fdViewHandover(f2, 'filed')", { f2 });
  assert.deepStrictEqual(J(f2.view), { key: "filed", range: null, follow: true, reclamp: false }, "a following view becomes the whole fight");
  run("fdViewSync(f2, 'filed')");
  assert.strictEqual(f2.view.key, "filed");
  run("fdViewSync(f2, 'another')");
  assert.deepStrictEqual(J(f2.view), { key: "another", range: null, follow: true, reclamp: false }, "another fight clears the range");
}
// ---- post-fight actions
{
  const fd = { sel: "k", view: FdView.create("k"), details: { k: { fight: { dur_ms: 120000 } } } };
  assert.ok(run("fdRangeAction('c', fd, node)", { fd, node: { dataset: { fdRangeSet: "35000-95000" } } }));
  assert.deepStrictEqual(J(fd.view.range), { from: 35000, to: 95000 });
  assert.strictEqual(fd.view.follow, true, "post-fight: follow is not touched");
  assert.strictEqual(run("fdRangeAction('c', fd, node)", { node: { dataset: { fdRangeSet: "bad" } } }), false, "a malformed value changes nothing");
  assert.strictEqual(run("fdRangeAction('c', fd, node)", { node: { dataset: { fdRangeSet: "1000-1500" } } }) && J(fd.view.range).from, 35000, "a click-sized range keeps the old one");
  assert.ok(run("fdRangeAction('c', fd, node)", { node: { dataset: { fdRangeClear: "" } } }));
  assert.strictEqual(fd.view.range, null, "Clear");
}

// ---- wiring pins on the text of app.js
assert.ok(/card\.addEventListener\('pointerdown'/.test(src) && /card\.addEventListener\('pointermove'/.test(src) && /card\.addEventListener\('pointerup'/.test(src) && /card\.addEventListener\('pointercancel'/.test(src), "the card listens to the pointer for the drag");
assert.ok(src.includes("FD_DRAG_BARS = ['.fd-tl', '.fd-lanebar', '.fd-au-bar']"), "a drag starts on the timeline, a lane's bar or an aura bar");
assert.ok(src.includes("FD_DRAG_PX = 6"), "a drag is 6 px");
assert.ok(src.includes("if (Date.now() - fd.dragEnd < 300) return;"), "the click seek skips the click that ends a drag");
assert.ok(src.indexOf("fd.dragEnd < 300") < src.indexOf("const bar = e.target.closest && e.target.closest('[data-fd-seekbar]');"), "and does so before it seeks");
assert.ok(src.includes("fd.dragEnd = Date.now();"), "a drag records when it ended");
assert.ok(/pointermove[\s\S]{0,700}setPointerCapture/.test(src), "the pointer is captured once the drag starts (never on a press, which would break the click)");
assert.ok(!/pointerdown[\s\S]{0,900}setPointerCapture[\s\S]{0,40}\n    \}\);\n    card\.addEventListener\('pointermove'/.test(src), "not at the press");
assert.ok(src.includes("FdView.pxToMs(dr.x0 - dr.left, dr.width, dr.dur)") && src.includes("FdView.setRange(fd.view, dr.a, dr.b, dr.dur, fd.sel === 'live')"), "the drag converts pixels with the fight's own length and sets the range through FdView");
assert.ok(/e\.key === 'Escape'[\s\S]{0,700}FdView\.clear\(fd\.view\)/.test(src), "Escape clears the range (a live view follows again)");
assert.ok(src.includes("[data-fd-range-clear], [data-fd-range-zoom], [data-fd-follow], [data-fd-range-set]"), "the range buttons share one click path");
assert.ok(/fd\.sel !== 'live' && fd\.view && fd\.view\.range[\s\S]{0,300}fd\.rp\.t = zr\.from;[\s\S]{0,120}fd\.rp\.on = true;[\s\S]{0,120}fdReplaySync\(key\)/.test(src), "Zoom replay here sets the replay to the range's start, post-fight only");
assert.ok(src.includes("fd.view = FdView.clear(fd.view); // picking a window follows the fight again"), "the window picker follows again");
assert.ok(grab("fdLivePaintBar").includes("fdLiveRangeOf(fd, st, now);") && !grab("fdLivePaintBar").includes("FdLive.range("), "the bar works the range out once through the view (fd.liveRange), not around it");
assert.ok(grab("fdLivePaintBar").includes("fdRangeBarHtml(fd.view, true, fd.liveWin)"), "the live bar draws the range bar");
const paint = grab("fdLivePaintDebug");
assert.ok(paint.includes("fdLiveTimelineHtml(st, lf, fd.liveRange)") && paint.includes("fdAuLiveOpts(st, fd.liveRange)") && paint.includes("fdDragBandSync(card, fd)"), "the 1 s repaint hands fd.liveRange to the timeline and the aura panel, and redraws a drag band");
assert.ok(grab("fdLiveHandoff").includes("fdViewHandover(fd, summary.key);") && grab("fdLiveHandoff").indexOf("fdViewHandover(") < grab("fdLiveHandoff").indexOf("fd.sel = summary.key;"), "the hand-over moves the view's key before the selection and the render");
assert.ok(grab("fdSyncSel").includes("fdViewHandover(fd, filed.key);"), "so does the list's own hand-over");
const render = grab("renderFightDebug");
assert.ok(render.includes("fdViewSync(fd, fd.sel);") && render.includes("FdView.reclamp(fd.view, fight.dur_ms)"), "a new fight clears the range, a handed-over one is fitted on its first render");
assert.ok(render.includes("fdTimelineHtml(FightDebug.timeline(fight), recaps, fd.death, range)") && render.includes("fdRecapHtml(recaps, fd.death, range)") && render.includes("fdLanesHtml(lanesRes, fight.dur_ms, range)"), "the timeline, recap and lanes get the range");
assert.ok(render.includes("{ pending, untracked, range }") && render.includes("fdAuLiveOpts(liveSt, range)"), "and so does the Auras panel, live and filed");
assert.ok(render.includes("fdWholeTags(card, fd.view, selLive)"), "the whole-fight tags follow the range");
assert.ok(render.trimEnd().endsWith("fdDragBandSync(card, fd);\n  }"), "a full render ends by redrawing a drag in progress");
assert.ok((src.match(/class="fd-whole" hidden/g) || []).length >= 6, "the panels without time data carry a whole-fight tag");
assert.ok(src.includes('<div class="fd-rangebar"></div>'), "the range bar's slot is in the timeline section, outside the live bar and its select");
assert.ok(!/<div class="fd-livebar"[^>]*>[^]*?fd-rangebar[^]*?<\/div><div class="fd-play"/.test(src.replace('</select></label></div><div class="fd-rangebar">', "")), "the range bar is not inside the live bar");
assert.ok(src.includes("view: FdView.create(null), dragEnd: 0, drag: null"), "the view is part of the card's debugger state");
const html = fs.readFileSync(path.join(jsDir, "..", "..", "index.html"), "utf8");
assert.ok(html.indexOf("/static/js/fdview.js") > 0 && html.indexOf("/static/js/fdview.js") < html.indexOf("/static/js/fightauras.js"), "FdView loads before fightauras.js (and app.js)");
const css = fs.readFileSync(path.join(jsDir, "..", "css", "app.css"), "utf8");
assert.ok(css.includes(".fd-range {") && css.includes(".fd-out { opacity: .3; }") && css.includes(".fd-whole {"), "the css classes");
assert.ok(/\[data-fd-seekbar\] \{[^}]*touch-action: none; user-select: none;/.test(css), "a seek bar takes no touch scrolling and no text selection");
assert.ok(/\.fd-range \{[^}]*pointer-events: none;/.test(css), "the band takes no pointer");
console.log("fdrange_ui_check.js ok");
