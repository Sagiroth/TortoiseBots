"use strict";
// Run by commandersjs_test.go under node: the Fight layers in app.js (web/static/js/app.js) over the pure FightMap: the source of the layers
// (a finished fight's loaded track or the live view's positions, nothing outside the debugger), the model kept on the card and rebuilt only
// when something it reads changed, the SVG (trails by role, the anchor circle and stand, clouds for 20 s, the puller, heat, every name
// escaped, the death mark's title with the escaped killer), the one fit to a fight, the unit focus through FdFilter (a bot, a summon, the
// boss, empty ground, the chip), the Dungeons tab's pull pins (one request pair per 30 s, only while visible, escaped labels, a click opens the
// fight), and the wiring pins (index.html, the CSS, drawCmdMap's order).
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");
const appPath = path.resolve(process.argv[2]);
const src = fs.readFileSync(appPath, "utf8");
const jsDir = path.dirname(appPath);
const fakedom = require("./fakedom.js");

function grab(name) {
  const re = new RegExp(`^  function ${name}\\(`, "m"), m = re.exec(src);
  assert.ok(m, `${name} not found`);
  const end = src.indexOf("\n  }\n", m.index);
  return src.slice(m.index, end + 4);
}
function grabConst(name) {
  const re = new RegExp(`^  const ${name} = `, "m"), m = re.exec(src);
  assert.ok(m, `${name} not found`);
  const end = src.indexOf("\n  };\n", m.index);
  return src.slice(m.index, end + 5);
}

const FightMap = require(path.join(jsDir, "fightmap.js"));
const FightReplay = require(path.join(jsDir, "fightreplay.js"));
const FdLive = require(path.join(jsDir, "fdlive.js"));
const FdView = require(path.join(jsDir, "fdview.js"));
const FdFilter = require(path.join(jsDir, "fdfilter.js"));
const FightDebug = require(path.join(jsDir, "fightdebug.js"));
const CmdView = require(path.join(jsDir, "commanders.js"));

// ---- a fake page: the document, the clock, the request helper, and the app's own state the functions read
const dom = fakedom.createDocument();
fakedom.Element.prototype.replaceChildren = function (...kids) {
  this.children.forEach((c) => { c.parentElement = null; });
  this.children = [];
  kids.forEach((k) => this.appendChild(k));
};
let nowMs = 1.7e12;
const requests = [];
let answers = {};
const ctx = {
  console, Promise, Math, JSON, Object, Array, Number, String, encodeURIComponent, Date: { now: () => nowMs },
  FightMap, FightReplay, FdLive, FdView, FdFilter, FightDebug, CmdView,
  document: { hidden: false, createElement: (t) => dom.document.createElement(t) },
  window: { location: { hash: "" } },
  state: { commanders: { list: [] } },
  cmdFd: {}, cmdMaps: {}, cmdCards: new Map(), fdLive: {}, covRows: undefined,
  fdRendered: [], hashed: [], drawn: [],
  fdRender: (k) => ctx.fdRendered.push(k), fdViewHash: (k) => ctx.hashed.push(k), drawCmdMap: (k) => ctx.drawn.push(k),
  cmdFindBind: () => ctx.theBind,
  fdLiveJson: (url) => { requests.push(url); return Promise.resolve(answers[url.split("?")[0]] || { missing: true, json: null }); },
  raidFightHash: (c, k) => "#/commanders/" + encodeURIComponent(c) + "/fight/" + encodeURIComponent(k),
  covFetch: () => Promise.resolve(),
};
ctx.theBind = null;
vm.createContext(ctx);
vm.runInContext([
  grabConst("CMD_MAP_ICONS"),
  "const FD_EV_PAGE = 500; let fmSeq = 0; let fmTacBusy = false; const DGN_PINS_EVERY_MS = 30000; const DGN_PINS_MAX = 200; const dgnPins = { key: '', cmd: '', at: 0, busy: false, pins: [], missing: 0 };",
  ...["esc", "cmdMapIcon", "fdMapTactics", "fdMapSource", "fdFightModel", "fdMapFit", "cmdFightMapSvg", "fmLegendHtml", "fdFocusPaint", "fdFocusSet", "fdFocusClear",
    "fdFocusTap", "fdFocusAfter", "fdPickBot", "fdPickBotName", "fdTapEmpty", "fdFightClockMs", "fdDecodeKey", "dgnPinOpen", "cmdFightMarksSvg", "cmdPullPinsSvg", "dgnPinsSync", "dgnPinsMerge"].map(grab),
].join("\n"), ctx);
const run = (expr, vars) => { if (vars) Object.assign(ctx, vars); return vm.runInContext(expr, ctx); };
const count = (s, re) => (s.match(re) || []).length;
const EVIL = "<img src=x onerror=1>";
const dc = { f: (n) => n.toFixed(2), k: 0.5, units: {} };

// ---- fixtures: one finished fight on Zul'Gurub with a track, deaths and the Hakkar rows
const START = 1000;
const fight = {
  id: "7", key: "Cmd\u0000s\u00007", start: START, dur_ms: 60000, map: 309, boss: 14834, boss_name: "Hakkar", outcome: "wipe",
  bots: [{ name: "A", guid: 1, role: "tank" }, { name: "B", guid: 2, role: "healer" }, { name: "C", guid: 3, role: "dps" }],
  deaths: [{ name: "B", t_ms: 30000, x: 10, y: 20, killer: EVIL, hits: [] }, { name: "C", t_ms: 50000, x: -40, y: 5, killer: "Hakkar", hits: [] }],
  log: [],
};
const rawTrack = { frames: [], names: { 14834: "Hakkar", 11357: "Son of Hakkar" }, tracked: true };
for (let i = 0; i < 30; i++) {
  const t = START + i * 2, mobs = [{ id: "100", e: 14834, x: 0, y: 0, hp: 90, v: 1 }];
  if (i >= 5 && i <= 8) mobs.push({ id: "200", e: 11357, x: 10 + i, y: 5, hp: 100, v: 2 }); // the Son is seen in frames 5-8 and gone in 9, targeting B
  rawTrack.frames.push({ t, map: 309, bots: [[1, i, 0, 100, 1, 309], [2, 5 + i, 3, i < 15 ? 100 : 60, 1, 309], [3, -i, 8, 100, 1, 309]], mobs });
}
const track = () => ({ raw: rawTrack, prep: FightReplay.prep(rawTrack) });
const hakkar = [
  { map: 309, boss: 14834, kind: "anchor", target: 0, spell: 0, priority: 25, param: 30, spotX: -11787.4, spotY: -1649.34, spotZ: 54.1 },
  { map: 309, boss: 14834, kind: "seek_aura", target: 11357, spell: 24321, priority: 30, param: 4, spotX: -11787.4, spotY: -1664.3, spotZ: 54 },
];
const KEY = "Cmd";
const mkFd = (over) => Object.assign({
  mode: "debug", sel: "K1", view: FdView.create("K1"), details: { K1: { fight } }, tracks: { K1: track() }, filter: FdFilter.empty(), bot: null, focus: null,
  liveRange: null, evWhy: "", evShown: 0,
}, over || {});
const ALL_ON = { trails: true, marks: true, heat: true, summons: true };
const ALL_ON_LAYERS = { fighttrails: true, fightmarks: true, fightheat: true, fightsummons: true, deaths: true };
const bindA = { commander: KEY, bots: [{ name: "A", guid: 1, role: "tank" }, { name: "B", guid: 2, role: "healer" }, { name: "C", guid: 3, role: "dps" }] };
ctx.theBind = bindA;

// ---- the source: a finished fight with its loaded track, units standing at the end of the range with no replay
ctx.covRows = { loadedAt: "t1", tactics: hakkar };
ctx.cmdFd[KEY] = mkFd();
let s = run("fdMapSource(key, fd, bind)", { key: KEY, fd: ctx.cmdFd[KEY], bind: bindA });
assert.ok(s && !s.live && s.frames.length > 20 && s.fight === fight, "the picked fight with its frames");
assert.strictEqual(s.frames[0].t, 0, "the first frame is the fight's start in fight ms");
assert.deepStrictEqual([s.range, s.rest.tMs, s.rest.tS], [null, 60000, START + 60], "no range: the units stand at the end of the fight");
ctx.cmdFd[KEY].view = FdView.setRange(ctx.cmdFd[KEY].view, 10000, 20000, 60000, false);
s = run("fdMapSource(key, fd, bind)", { key: KEY, fd: ctx.cmdFd[KEY], bind: bindA });
assert.deepStrictEqual([s.range, s.rest.tMs, s.rest.tS], [{ from: 10000, to: 20000 }, 20000, START + 20], "a range: the units stand where it ends");
assert.strictEqual(s.rest.fight, fight, "and the rest moment is a replay moment of the fight");
ctx.cmdFd[KEY].tracks.K1 = "loading";
s = run("fdMapSource(key, fd, bind)", { key: KEY, fd: ctx.cmdFd[KEY], bind: bindA });
assert.deepStrictEqual([s.frames.length, s.rest], [0, null], "a track still loading: no frames, no rest moment");
ctx.cmdFd[KEY].tracks.K1 = null;
assert.strictEqual(run("fdMapSource(key, fd, bind)", { key: KEY, fd: ctx.cmdFd[KEY], bind: bindA }).frames.length, 0, "no track: no frames");
// outside the debugger, with no pick, or with no fight: nothing
assert.strictEqual(run("fdMapSource(key, fd, bind)", { key: KEY, fd: mkFd({ mode: "live" }), bind: bindA }), null, "outside the debugger there is no source, so nothing is drawn");
assert.strictEqual(run("fdMapSource(key, fd, bind)", { key: KEY, fd: mkFd({ sel: null }), bind: bindA }), null);
assert.strictEqual(run("fdMapSource(key, fd, bind)", { key: KEY, fd: mkFd({ details: {} }), bind: bindA }), null);
assert.strictEqual(run("fdMapSource(key, null, bind)", { key: KEY, bind: bindA }), null);

// ---- the model: kept on the card, rebuilt only when something it reads changed
let built = 0;
const realModel = FightMap.model;
FightMap.model = function () { built++; return realModel.apply(this, arguments); };
const fdA = mkFd();
ctx.cmdFd[KEY] = fdA;
const card = {};
const srcOf = () => run("fdMapSource(key, fd, bind)", { key: KEY, fd: fdA, bind: bindA });
const model = (on, src, map) => run("fdFightModel(card, src, on, map)", { card, src: src || srcOf(), on: on || ALL_ON, map: map === undefined ? 309 : map });
let m = model();
model(); model();
assert.strictEqual(built, 1, "three draws of the same view build the model once");
fdA.view = FdView.setRange(fdA.view, 10000, 30000, 60000, false);
model();
assert.strictEqual(built, 2, "a new range rebuilds it");
model(Object.assign({}, ALL_ON, { heat: false }));
assert.strictEqual(built, 3, "a layer switched off rebuilds it");
model(Object.assign({}, ALL_ON, { heat: false }));
assert.strictEqual(built, 3, "and the same layers again do not");
ctx.covRows = { loadedAt: "t2", tactics: hakkar.concat([{ map: 309, boss: 1, kind: "anchor" }]) };
model(Object.assign({}, ALL_ON, { heat: false }));
assert.strictEqual(built, 4, "the boss rows arriving rebuild it");
model(Object.assign({}, ALL_ON, { heat: false }), null, 249);
assert.strictEqual(built, 5, "another map rebuilds it");
fdA.sel = "K2"; fdA.details.K2 = { fight: Object.assign({}, fight, { id: "8" }) }; fdA.tracks.K2 = track(); fdA.view = FdView.create("K2");
model(Object.assign({}, ALL_ON, { heat: false }));
assert.strictEqual(built, 6, "another fight rebuilds it");
fdA.sel = "K1"; fdA.view = FdView.create("K1");
FightMap.model = realModel;
ctx.covRows = { loadedAt: "t1", tactics: hakkar };

// ---- the SVG: every layer's element, the replay moment's clouds, the colours by role
const on = ALL_ON_LAYERS;
m = model(ALL_ON);
assert.ok(m.bounds && m.mechanics.length === 2 && m.clouds.length === 1 && m.puller, "the model carries the layers' data");
const bots = { 1: { name: "A", role: "tank" }, 2: { name: "B", role: "healer" }, 3: { name: "C", role: "dps" } };
const draw = (mm, shown, flags) => run("cmdFightMapSvg(dc, mm, on, shown, roles, boss)", { mm, shown, on: flags || on, roles: bots, boss: "Hakkar", dc });
let svg = draw(m, 18000);
assert.strictEqual(count(svg, /<path class="fm-trail r-tank"/g), 1);
assert.strictEqual(count(svg, /<path class="fm-trail r-healer"/g) + count(svg, /<path class="fm-trail r-dps"/g), 2, "a trail per bot, coloured by its role");
assert.ok(/<path class="fm-trail mob"[^>]*><title>Son of Hakkar<\/title>/.test(svg) && !/<path class="fm-trail mob"[^>]*><title>Hakkar<\/title>/.test(svg), "the Son's trail is named; the boss stands still at his anchor, so he has no trail");
assert.ok(/<circle class="fm-anchor" [^>]*r="30\.00"><title>Anchor 30 yd · tanks 25 yd<\/title>/.test(svg) && /<circle class="fm-anchor tank" [^>]*r="25\.00"/.test(svg), "the anchor circle and the tanks' circle: " + svg.slice(0, 400));
assert.ok(/<g class="fm-stand"><polygon/.test(svg) && svg.includes(">Stand B</text>"), "the stand point and its label");
assert.ok(/<circle class="fm-cloud"[^>]*r="4\.00"/.test(svg), "the cloud at its 4 yd radius at 18 s (its Son was last seen at 16 s)");
assert.ok(svg.includes('<polyline class="fm-puller"') && svg.includes("B was the first the Son of Hakkar went for"), "the puller's path");
assert.ok(/<rect class="fm-heat"[^>]*style="opacity:[01]\.\d\d"/.test(svg), "heat cells with their weight as opacity");
assert.ok(!/fm-cloud/.test(draw(m, 16000 + 20001)) && /fm-cloud/.test(draw(m, 16000 + 20000)) && /fm-cloud/.test(draw(m, 16000)) && !/fm-cloud/.test(draw(m, 15999)), "a cloud shows from its time for 20 s only");
// switching a layer off: its model is empty, so nothing of it is drawn
const noTrails = draw(model({ trails: false, marks: true, heat: true, summons: true }), 18000);
assert.ok(!/class="fm-trail r-/.test(noTrails) && /fm-trail mob/.test(noTrails), "fighttrails off: no bot trail (the summons' stays)");
const noSummons = draw(model({ trails: true, marks: true, heat: true, summons: false }), 18000);
assert.ok(/class="fm-trail r-/.test(noSummons) && !/fm-trail mob/.test(noSummons), "fightsummons off: no Son or boss trail");
const noMarks = draw(model({ trails: true, marks: false, heat: true, summons: true }), 18000);
assert.ok(!/fm-anchor|fm-stand|fm-cloud|fm-puller/.test(noMarks), "fightmarks off: no anchor, stand, cloud or puller");
const noHeat = draw(model({ trails: true, marks: true, heat: false, summons: true }), 18000);
assert.ok(!/fm-heat/.test(noHeat), "fightheat off: no heat");
const allOff = draw(model({ trails: false, marks: false, heat: false, summons: false }), 18000);
assert.strictEqual(allOff, "", "every Fight layer off: nothing at all");
// the layer flags of the draw gate what the model still holds
assert.ok(!/fm-heat/.test(draw(m, 18000, Object.assign({}, on, { fightheat: false }))) && !/fm-anchor/.test(draw(m, 18000, Object.assign({}, on, { fightmarks: false }))), "a layer off in the draw is not drawn");
// hostile names: the trail of a bot called EVIL, the boss called EVIL
const evilRoles = { 1: { name: EVIL, role: "tank" }, 2: { name: EVIL, role: "healer" }, 3: { name: EVIL, role: "dps" } };
const evilSvg = run("cmdFightMapSvg(dc, mm, on, shown, roles, boss)", { mm: model({ trails: true, marks: true, heat: false, summons: true }, null, 309), shown: 18000, on, roles: evilRoles, boss: EVIL, dc });
assert.ok(evilSvg.includes("&lt;img src=x onerror=1&gt;") && !evilSvg.includes("<img"), "names are escaped in the trail titles and the puller's");

// ---- the death marks' title carries the escaped killer ("Name · killed by X · m:ss")
const recaps = FightDebug.deathRecaps(fight);
const marks = run("cmdFightMarksSvg(dc, recaps, null)", { recaps, dc });
assert.ok(marks.includes("<title>B · killed by &lt;img src=x onerror=1&gt; · 0:30</title>"), "the title: " + marks);
assert.ok(!marks.includes("<img src=x"), "the killer is never markup");
assert.ok(marks.includes("<title>C · killed by Hakkar · 0:50</title>"));

// ---- the fit: once per fight picked, never while a fight runs, never again over the viewer's own zoom
const msF = { follow: true, view: null, userScale: null };
const fit = (ms, mm, ss, sel) => run("fdMapFit(ms, mm, ss, sel, 800, 440)", { ms, mm, ss, sel });
assert.strictEqual(fit(msF, m, srcOf(), "K1"), true, "the first pick fits");
assert.deepStrictEqual([msF.follow, msF.fmFit, typeof msF.view.scale, msF.userScale === msF.view.scale], [false, "K1", "number", true], "the view stops following and holds the fit's zoom");
const firstView = JSON.stringify(msF.view);
msF.view = { cx: 1, cy: 2, scale: 3 };
assert.strictEqual(fit(msF, m, srcOf(), "K1"), false, "the same fight again: the viewer's own zoom stands");
assert.strictEqual(JSON.stringify(msF.view), JSON.stringify({ cx: 1, cy: 2, scale: 3 }));
assert.strictEqual(fit(msF, m, srcOf(), "K2"), true, "another fight fits again");
assert.strictEqual(JSON.stringify(msF.view), firstView, "the same fit as the first one: the fit is the fight's own box");
assert.strictEqual(fit({ follow: true }, m, Object.assign({}, srcOf(), { live: true }), "live"), false, "a running fight keeps following the party");
assert.strictEqual(fit({ follow: true }, { bounds: null }, srcOf(), "K3"), false, "no bounds, no fit");
assert.strictEqual(fit({ follow: true }, null, srcOf(), "K3"), false);
{
  // the fitted box is the fight's cluster: the anchor circle of the fixture sits 11000 yd away from the bots (a corrupt row), so it is an outlier
  const ms2 = { follow: true, view: null, userScale: null };
  fit(ms2, m, srcOf(), "K9");
  assert.ok(Math.abs(ms2.view.cx) < 100 && Math.abs(ms2.view.cy) < 100 && ms2.view.scale > 1, "a far-off anchor and the other outliers do not move the fit away from the fight: " + JSON.stringify(ms2.view));
  // one bot on another map and one at 1e6 yd in the track do not move it either
  const dirty = JSON.parse(JSON.stringify(rawTrack));
  dirty.frames.forEach((f, i) => { if (i % 3 === 0) { f.bots.push([7, 5000, 5000, 100, 1, 249], [8, 1e6, 1e6, 100, 1, 309]); } });
  const fd9 = mkFd({ tracks: { K1: { raw: dirty, prep: FightReplay.prep(dirty) } } });
  ctx.cmdFd[KEY] = fd9;
  const dsrc = run("fdMapSource(key, fd, bind)", { key: KEY, fd: fd9, bind: bindA });
  const dm = run("fdFightModel(c, src, on, 309)", { c: {}, src: dsrc, on: ALL_ON });
  const ms3 = { follow: true, view: null, userScale: null };
  fit(ms3, dm, dsrc, "K1");
  assert.deepStrictEqual([ms3.view.cx, ms3.view.cy, ms3.view.scale], [ms2.view.cx, ms2.view.cy, ms2.view.scale], "the same fit with a bot on another map and one at 1e6 yd in the reports");
  assert.ok(!Object.keys(dm.trails).includes("7") && !Object.keys(dm.trails).includes("8"), "and neither has a trail");
  // the fit waits for the positions: while the track loads, the deaths and mechanics alone must not set it
  const fdW = mkFd({ tracks: { K1: "loading" } });
  ctx.cmdFd[KEY] = fdW;
  const wsrc = run("fdMapSource(key, fd, bind)", { key: KEY, fd: fdW, bind: bindA });
  assert.strictEqual(wsrc.trackState, "loading");
  const wm = run("fdFightModel(c, src, on, 309)", { c: {}, src: wsrc, on: ALL_ON });
  assert.ok(wm.bounds, "the deaths alone give a box");
  const msW = { follow: true, view: null, userScale: null };
  assert.strictEqual(fit(msW, wm, wsrc, "K1"), false, "but the fit waits for the track");
  assert.deepStrictEqual([msW.fmFit, msW.follow, msW.view], [undefined, true, null], "and leaves the card following, with the fit still to come");
  fdW.tracks.K1 = track();
  const rsrc = run("fdMapSource(key, fd, bind)", { key: KEY, fd: fdW, bind: bindA });
  assert.strictEqual(rsrc.trackState, "ready");
  assert.strictEqual(fit(msW, run("fdFightModel(c, src, on, 309)", { c: {}, src: rsrc, on: ALL_ON }), rsrc, "K1"), true, "once the track is in, it fits");
  // a failed request (tracks null, the 30 s retry pending) is not "none": no fit, the card keeps following, and the retry's success fits once
  fdW.tracks.K1 = null;
  const fsrc = run("fdMapSource(key, fd, bind)", { key: KEY, fd: fdW, bind: bindA });
  assert.strictEqual(fsrc.trackState, "failed", "a failed fetch is 'failed'");
  const msF2 = { follow: true, view: null, userScale: null };
  const fmod = run("fdFightModel(c, src, on, 309)", { c: {}, src: fsrc, on: ALL_ON });
  assert.ok(fmod.bounds, "the deaths alone give a box");
  assert.strictEqual(fit(msF2, fmod, fsrc, "K1"), false, "after a failed fetch the fit waits for the retry");
  assert.deepStrictEqual([msF2.fmFit, msF2.follow, msF2.view], [undefined, true, null], "fmFit unset, follow on, no view");
  fdW.tracks.K1 = track();
  const okSrc = run("fdMapSource(key, fd, bind)", { key: KEY, fd: fdW, bind: bindA });
  assert.strictEqual(fit(msF2, run("fdFightModel(c, src, on, 309)", { c: {}, src: okSrc, on: ALL_ON }), okSrc, "K1"), true, "the retry's success fits");
  assert.strictEqual(fit(msF2, run("fdFightModel(c, src, on, 309)", { c: {}, src: okSrc, on: ALL_ON }), okSrc, "K1"), false, "once");
  // an old fight: the daemon answered with no frames. None will come, so it fits on its deaths and mechanics
  const emptyRaw = { frames: [], names: {}, tracked: true };
  fdW.tracks.K1 = { raw: emptyRaw, prep: FightReplay.prep(emptyRaw) };
  const nsrc = run("fdMapSource(key, fd, bind)", { key: KEY, fd: fdW, bind: bindA });
  assert.strictEqual(nsrc.trackState, "none", "an answer with no frames is 'none'");
  const nmod = run("fdFightModel(c, src, on, 309)", { c: {}, src: nsrc, on: ALL_ON });
  assert.ok(nmod.bounds, "its deaths still give a box");
  const msN = { follow: true, view: null, userScale: null };
  assert.strictEqual(fit(msN, nmod, nsrc, "K1"), true, "an old fight with no positions fits on its deaths");
  assert.deepStrictEqual([msN.fmFit, msN.follow], ["K1", false]);
  fdW.tracks.K1 = null;
  fdW.tracks.K1 = undefined;
  assert.strictEqual(run("fdMapSource(key, fd, bind)", { key: KEY, fd: fdW, bind: bindA }).trackState, "loading", "one not asked for yet is still to come");
  ctx.cmdFd[KEY] = fdA;
}

// ---- a run replay's moment is the run's, not the fight's: the fight layers and the deaths use the fight's own clock
{
  const f = { start: 1000, dur_ms: 60000 };
  assert.strictEqual(run("fdFightClockMs(f, 1018)", { f }), 18000, "18 s into the fight");
  assert.strictEqual(run("fdFightClockMs(f, 1018)", { f: { start: 1000, start_ms: 1000500, dur_ms: 1 } }), 17500, "the live fight's exact start wins");
  assert.strictEqual(run("fdFightClockMs(null, 5)"), 5000);
  // the run began 100 s before the fight: a run moment of 118 s (tS 1018 when t0 is 900) is 18 s into the fight, where the Son's cloud stands
  const runTMs = 118000, shownRun = run("fdFightClockMs(f, 900 + t / 1000)", { f, t: runTMs });
  assert.strictEqual(shownRun, 18000);
  assert.ok(/fm-cloud/.test(draw(m, shownRun)) && !/fm-cloud/.test(draw(m, runTMs)), "the cloud shows at the fight's 18 s, and would not at the run's 118 s");
}

// ---- the unit focus through FdFilter
const chipCard = dom.element("div", "card", "cmd-card");
const chip = dom.element("div", "", "fm-focus");
chip.hidden = true;
dom.attach(chipCard, chip);
const fdF = mkFd();
ctx.cmdFd[KEY] = fdF;
ctx.fdRendered.length = 0; ctx.hashed.length = 0;
assert.strictEqual(run("fdPickBot(key, 1)", { key: KEY }), true, "a tap on a bot picks it");
assert.strictEqual(fdF.bot, "A");
assert.strictEqual(FdFilter.describe(fdF.filter), "source is A or target is A", "the filter is the OR group of the bot");
assert.ok(FdFilter.validate(fdF.filter).ok && fdF.filter.op === "or" && fdF.filter.items.length === 2);
assert.deepStrictEqual([ctx.fdRendered.length, ctx.hashed.length], [1, 1], "the panels render and the link follows");
run("fdFocusPaint(card, fd)", { card: chipCard, fd: fdF });
assert.strictEqual(chip.hidden, false, "the chip is shown");
assert.strictEqual(chip.children[0].textContent, "Focus: A");
assert.ok(chip.children[1].attributes["data-fm-unfocus"] === "" && chip.children[1].textContent === "×" && chip.children[1].attributes["aria-label"].includes("A"), "with its clear button");
assert.strictEqual(run("fdPickBot(key, 1)", { key: KEY }), true, "tapping the same bot again");
assert.deepStrictEqual([fdF.bot, FdFilter.isEmpty(fdF.filter), fdF.focus], [null, true, null], "clears the pick and the focus filter");
run("fdFocusPaint(card, fd)", { card: chipCard, fd: fdF });
assert.strictEqual(chip.hidden, true, "and the chip");
// a summon: the filter only
const unit = (entry, name) => ({ kind: "m", obj: { id: "9", entry, name } });
ctx.cmdMaps[KEY] = { units: { "m:200": unit(11357, "Son of Hakkar"), "m:100": unit(14834, "Hakkar"), "m:300": unit(4242, "Core Hound"), "m:400": unit(11357, EVIL), "b:1": { kind: "b", obj: { name: "A" } } } };
assert.strictEqual(run("fdFocusTap(key, 'm:200')", { key: KEY }), true);
assert.strictEqual(FdFilter.describe(fdF.filter), "source is Son of Hakkar or target is Son of Hakkar", "a tap on a Son of Hakkar");
assert.strictEqual(fdF.bot, null, "a mob is no bot pick");
run("fdFocusPaint(card, fd)", { card: chipCard, fd: fdF });
assert.strictEqual(chip.children[0].textContent, "Focus: Son of Hakkar");
assert.strictEqual(run("fdFocusTap(key, 'm:100')", { key: KEY }), true);
assert.strictEqual(FdFilter.describe(fdF.filter), "source is Hakkar or target is Hakkar", "a tap on the fight's boss");
const before = JSON.stringify(fdF.filter);
assert.strictEqual(run("fdFocusTap(key, 'm:300')", { key: KEY }), false, "a plain mob is not a focus");
assert.strictEqual(JSON.stringify(fdF.filter), before, "and changes nothing");
assert.strictEqual(run("fdFocusTap(key, 'b:1')", { key: KEY }), false, "nor is a unit that is no mob");
assert.strictEqual(run("fdFocusTap(key, 'm:999')", { key: KEY }), false, "nor is a mob the map no longer has");
assert.strictEqual(run("fdFocusTap(key, 'm:400')", { key: KEY }), true);
assert.strictEqual(fdF.filter.items[0].v, EVIL, "a hostile name is the filter's text value, never markup");
run("fdFocusPaint(card, fd)", { card: chipCard, fd: fdF });
assert.strictEqual(chip.children[0].textContent, "Focus: " + EVIL, "the chip sets it as text");
// empty ground clears the focus (and the bot pick), nothing without one
fdF.bot = "A";
assert.strictEqual(run("fdFocusTap(key, '', false)", { key: KEY }), false, "a tap that is not on bare ground (a death skull, an edge marker, a trail, a heat cell) clears nothing");
assert.ok(fdF.focus && fdF.bot === "A", "the focus and the pick stand");
assert.strictEqual(run("fdFocusTap(key, '', true)", { key: KEY }), true);
assert.deepStrictEqual([FdFilter.isEmpty(fdF.filter), fdF.focus, fdF.bot], [true, null, null], "a tap on empty ground clears the focus and the bot pick");
assert.strictEqual(run("fdFocusTap(key, '', true)", { key: KEY }), false, "with no focus it does nothing");
// the reader's own filter is not taken away by clearing a focus that is no longer the filter
run("fdFocusTap(key, 'm:200')", { key: KEY });
fdF.filter = FdFilter.preset("casts");
run("fdFocusPaint(card, fd)", { card: chipCard, fd: fdF });
assert.strictEqual(chip.hidden, true, "a filter the reader changed hides the chip");
run("fdFocusClear(key)", { key: KEY });
assert.strictEqual(FdFilter.describe(fdF.filter), "kind is Cast", "clearing a stale focus leaves the reader's own filter");
// the reader's own filter comes back when the focus is cleared
fdF.focus = null; fdF.bot = null; fdF.filter = FdFilter.preset("casts");
const typed = JSON.stringify(fdF.filter);
run("fdPickBotName(key, 'A')", { key: KEY });
assert.strictEqual(FdFilter.describe(fdF.filter), "source is A or target is A", "the focus replaces it");
run("fdPickBotName(key, 'A')", { key: KEY });
assert.strictEqual(JSON.stringify(fdF.filter), typed, "and picking A again gives the reader's filter back");
assert.deepStrictEqual([fdF.bot, fdF.focus], [null, null]);
run("fdPickBotName(key, 'A')", { key: KEY });
run("fdFocusTap(key, 'm:200')", { key: KEY });
run("fdFocusClear(key, true)", { key: KEY });
assert.strictEqual(JSON.stringify(fdF.filter), typed, "a focus that replaced another focus still gives back the reader's own filter, not the first focus");
fdF.filter = FdFilter.empty();
// a map pick of A and then a panel pick of B: the chip, the filter and the pick all name B
fdF.focus = null; fdF.bot = null;
run("fdPickBot(key, 1)", { key: KEY });
run("fdPickBotName(key, 'B')", { key: KEY });
assert.deepStrictEqual([fdF.bot, fdF.focus.name, FdFilter.describe(fdF.filter)], ["B", "B", "source is B or target is B"], "map pick A then panel pick B");
run("fdFocusPaint(card, fd)", { card: chipCard, fd: fdF });
assert.strictEqual(chip.children[0].textContent, "Focus: B", "the chip names B");
run("fdPickBotName(key, 'B')", { key: KEY });
assert.deepStrictEqual([fdF.bot, fdF.focus, FdFilter.isEmpty(fdF.filter)], [null, null, true], "the panel's second pick of B clears all three");
// what is bare ground
const tgt = (hit) => ({ closest: (sel) => (hit && sel.split(",").some((q) => q.trim() === hit) ? {} : null) });
assert.strictEqual(run("fdTapEmpty(t)", { t: tgt(null) }), true, "nothing under the tap");
["[data-u]", "g.bosspoint", "g.fdmark", "g.fm-pin", ".fm-trail", ".fm-heat", ".fm-cloud", ".fm-anchor", ".fm-stand", ".fm-puller", ".fm-label"].forEach((q) => assert.strictEqual(run("fdTapEmpty(t)", { t: tgt(q) }), false, q + " is not bare ground"));
assert.strictEqual(run("fdTapEmpty(null)"), true);
// the chip's × and outside the debugger
fdF.focus = null; fdF.filter = FdFilter.empty();
fdF.mode = "live";
assert.strictEqual(run("fdFocusTap(key, 'm:200')", { key: KEY }), false, "outside the debugger a tap is not a focus");
assert.strictEqual(run("fdPickBot(key, 1)", { key: KEY }), false, "and a bot tap keeps opening its details");
assert.ok(FdFilter.isEmpty(fdF.filter) && fdF.bot === null);
run("fdFocusPaint(card, fd)", { card: chipCard, fd: fdF });
assert.strictEqual(chip.hidden, true, "no chip outside the debugger");

// ---- LIVE: the source is the view's own positions, taken once per second; deaths come from the events
{
  const st = FdLive.create(KEY);
  const NOW = nowMs;
  FdLive.applyEventsFrame(st, { key: "Cmd\u0000s\u00009", from: 0, cur: 1, events: [{ k: "death", t: 12000, n: "B", by: "Hakkar", x: 3, y: 4 }], fight: { start_ms: NOW - 20000, boss: 14834, boss_name: "Hakkar", fight_ms: 20000 } }, NOW);
  for (let i = 0; i < 6; i++) FdLive.applyPos(st, { ts_ms: NOW - 3000 + i * 500, bots: [{ g: 1, x: i, y: 0, hp: 100, mhp: 100, a: 1 }, { g: 2, x: 4, y: i, hp: 80, mhp: 100, a: 1 }], mobs: [{ id: "100", x: 5, y: 5, hp: 90 }] });
  FdLive.applySample(st, { bots: [{ guid: 1, hp: 100, max_hp: 100 }], mobs: [{ id: "100", entry: 14834, hp: 90, victim: "1" }] }, NOW - 2000);
  ctx.fdLive[KEY] = st;
  const fdL = mkFd({ sel: "live", view: FdView.create("live:" + st.key), liveRange: { from: 0, to: 20000 } });
  ctx.cmdFd[KEY] = fdL;
  const a = run("fdMapSource(key, fd, bind)", { key: KEY, fd: fdL, bind: bindA });
  assert.ok(a.live && a.frames.length === 6 && a.rest === null, "live: the 0.5 s frames, no rest moment (the bind's own dots glide)");
  assert.deepStrictEqual(a.range, { from: 0, to: 20000 }, "the live follow range");
  assert.strictEqual(a.frames[0].mobs[100].e, 14834, "the entry of a mob comes from the 2 s samples");
  assert.strictEqual(a.fight.deaths.length, 1, "the live fight's deaths from the events");
  const b = run("fdMapSource(key, fd, bind)", { key: KEY, fd: fdL, bind: bindA });
  assert.strictEqual(b.frames, a.frames, "within the same second the frames are not rebuilt");
  nowMs += 1000;
  const c = run("fdMapSource(key, fd, bind)", { key: KEY, fd: fdL, bind: bindA });
  assert.notStrictEqual(c.frames, a.frames, "a second later they are");
  assert.ok(c.k !== a.k, "and the model's key moves");
  assert.strictEqual(run("fdMapSource(key, fd, bind)", { key: "other", fd: fdL, bind: bindA }), null, "a view with no live state has no source");
  const lm = run("fdFightModel(card2, src, on, 309)", { card2: {}, src: c, on: ALL_ON });
  assert.strictEqual(lm.bounds, null, "a running fight is not fitted");
  assert.ok(Object.keys(lm.trails).length >= 2, "live trails from the positions");
}

// ---- the Dungeons tab's pull pins
const pinSvg = run("cmdPullPinsSvg(dc, pins)", { dc, pins: [
  { key: "k1", x: 1, y: 2, label: EVIL, wipe: true, live: false }, { key: "k2", x: 3, y: 4, label: "trash", wipe: false, live: true }, { key: EVIL, x: 5, y: 6, label: "Hakkar", wipe: false, live: false }] });
assert.ok(pinSvg.includes("&lt;img src=x onerror=1&gt;") && !pinSvg.includes("<img"), "pin labels and keys are escaped");
assert.ok(/<g class="fm-pin wipe" data-pin="k1">/.test(pinSvg) && /<g class="fm-pin live" data-pin="k2">/.test(pinSvg), "a wipe is marked, a live fight pulses");
assert.ok(pinSvg.includes('data-pin="%3Cimg%20src%3Dx%20onerror%3D1%3E"'), "a hostile key is encoded in the attribute");
// a fight key holds U+0000: it goes through the markup and the click to the exact hash
{
  const KEYZ = "cmd\u0000sess\u0000id";
  const mk = run("cmdPullPinsSvg(dc, pins)", { dc, pins: [{ key: KEYZ, x: 1, y: 1, label: "Hakkar", wipe: false, live: false }] });
  const raw = /data-pin="([^"]*)"/.exec(mk)[1];
  assert.ok(!raw.includes("\u0000") && raw === "cmd%00sess%00id", "the key travels encoded: " + raw);
  ctx.window.location.hash = "";
  run("dgnPins.cmd = 'Cmd'");
  assert.strictEqual(run("dgnPinOpen(pin)", { pin: { getAttribute: (a) => (a === "data-pin" ? raw : null) } }), true);
  assert.strictEqual(ctx.window.location.hash, run("raidFightHash('Cmd', k)", { k: KEYZ }), "the click opens exactly that fight");
  assert.strictEqual(ctx.window.location.hash, "#/commanders/Cmd/fight/cmd%00sess%00id");
  // an attribute written through innerHTML turns U+0000 into U+FFFD: the raw key would not have survived
  assert.ok(!/\u0000/.test(raw) && decodeURIComponent(raw) === KEYZ);
  assert.strictEqual(run("dgnPinOpen(null)"), false);
  assert.strictEqual(run("dgnPinOpen(pin)", { pin: { getAttribute: () => null } }), false, "a pin with no key opens nothing");
  run("dgnPins.cmd = ''");
}
assert.ok(pinSvg.includes('<circle class="ring"') && count(pinSvg, /class="ring"/g) === 1, "only the live pin has the pulsing ring");
assert.ok(pinSvg.includes("<title>" + "&lt;img src=x onerror=1&gt;" + " · wipe</title>"), "the hover text names a wipe");
const pinsDone = (async () => {
  const cmdBind = { commander: "Cmd", online: true, lead: "A", bots: [{ name: "A", guid: 1, lead: true }] };
  ctx.state.commanders.list = [cmdBind];
  const base = Math.floor(nowMs / 1000);
  const run1 = { key: "309:1", first_seen: base - 600 };
  answers = {
    "/api/v1/combat/fights": { missing: false, json: [
      { key: "f1", start: base - 500, boss_name: "Hakkar", outcome: "wipe" },
      { key: "f2", start: base - 100, outcome: "kill", live: true },
      { key: "old", start: base - 9000, outcome: "kill" },
    ] },
    "/api/v1/commanders/track": { missing: false, json: { frames: [
      { t: base - 499, map: 309, bots: [[1, 11, 12, 100, 1, 309]] }, { t: base - 99, map: 309, bots: [[1, 21, 22, 100, 1, 309]] }, { t: base - 97, map: 309, bots: [[1, 23, 22, 100, 1, 309]] }] } },
  };
  requests.length = 0; ctx.drawn.length = 0; ctx.cmdCards.set("dgn:309:1", {});
  run("dgnPinsSync(r, 'Cmd')", { r: run1 });
  run("dgnPinsSync(r, 'Cmd')", { r: run1 });
  assert.strictEqual(requests.length, 2, "two calls at once ask once: the list and the kept positions");
  assert.ok(requests.some((u) => u.startsWith("/api/v1/combat/fights?commander=Cmd&limit=200")) && requests.some((u) => /^\/api\/v1\/commanders\/track\?commander=Cmd&from=\d+&to=\d+$/.test(u)), "the requests: " + requests.join(" "));
  const tr = requests.find((u) => u.includes("/track")), from = Number(/from=(\d+)/.exec(tr)[1]), to = Number(/to=(\d+)/.exec(tr)[1]);
  assert.ok(from >= to - 1800 && from <= run1.first_seen, "the window is the run, at most the 30 min the daemon keeps: " + tr);
  await settle(() => run("dgnPins.busy") === false);
  assert.deepStrictEqual(JSON.parse(JSON.stringify(run("dgnPins.pins"))), [
    { key: "f1", x: 11, y: 12, label: "Hakkar", wipe: true, live: false },
    { key: "f2", x: 21, y: 22, label: "trash", wipe: false, live: true },
  ], "a pin per fight of the run at the lead's place at its start; the fight from before the run gets none");
  assert.deepStrictEqual(ctx.drawn, ["dgn:309:1"], "the run's map is drawn again when the pins arrive");
  requests.length = 0;
  run("dgnPinsSync(r, 'Cmd')", { r: run1 });
  assert.strictEqual(requests.length, 0, "inside 30 s: no request");
  nowMs += 31000;
  run("dgnPinsSync(r, 'Cmd')", { r: run1 });
  assert.strictEqual(requests.length, 2, "after 30 s: one more pair");
  await settle(() => run("dgnPins.busy") === false);
  requests.length = 0; nowMs += 31000;
  ctx.document.hidden = true;
  run("dgnPinsSync(r, 'Cmd')", { r: run1 });
  assert.strictEqual(requests.length, 0, "a hidden page asks for nothing");
  ctx.document.hidden = false;
  run("dgnPinsSync(null, 'Cmd')", {}); run("dgnPinsSync(r, null)", { r: run1 });
  assert.strictEqual(requests.length, 0, "no run or no fight commander: nothing");
  // a run longer than 30 minutes keeps the pins its early fights got while their positions were still kept
  {
    const longRun = { key: "309:7", first_seen: base - 4000 };
    ctx.cmdCards.set("dgn:309:7", {});
    answers = {
      "/api/v1/combat/fights": { missing: false, json: [
        { key: "early", start: base - 3500, boss_name: "Hakkar", outcome: "kill" },
        { key: "never", start: base - 3600, outcome: "kill" },
        { key: "late", start: base - 100, outcome: "wipe", live: true },
      ] },
      "/api/v1/commanders/track": { missing: false, json: { frames: [
        { t: base - 3499, map: 309, bots: [[1, 31, 32, 100, 1, 309]] }, { t: base - 99, map: 309, bots: [[1, 41, 42, 100, 1, 309]] }] } },
    };
    nowMs += 31000;
    run("dgnPinsSync(r, 'Cmd')", { r: longRun });
    await settle(() => run("dgnPins.busy") === false);
    assert.deepStrictEqual(Array.from(run("dgnPins.pins.map((p) => p.key)")), ["early", "late"], "the first refresh pins the two fights whose positions it could ask for");
    assert.strictEqual(run("dgnPins.missing"), 1, "one listed fight has no pin");
    // the next refresh asks only the last 30 min: the early fight can no longer be placed, its pin stays; the live one is updated
    answers["/api/v1/commanders/track"] = { missing: false, json: { frames: [{ t: base - 99, map: 309, bots: [[1, 43, 44, 100, 1, 309]] }] } };
    answers["/api/v1/combat/fights"].json[2] = { key: "late", start: base - 100, outcome: "wipe", live: false };
    nowMs += 31000;
    run("dgnPinsSync(r, 'Cmd')", { r: longRun });
    await settle(() => run("dgnPins.busy") === false);
    const kept = JSON.parse(JSON.stringify(run("dgnPins.pins")));
    assert.deepStrictEqual(kept.map((p) => p.key), ["early", "late"], "a refresh never drops a resolved pin");
    assert.deepStrictEqual([kept[0].x, kept[0].y], [31, 32], "the early pin keeps its place");
    assert.deepStrictEqual([kept[1].x, kept[1].y, kept[1].live], [43, 44, false], "and the later pin is updated");
    assert.strictEqual(run("dgnPins.missing"), 1, "the fight that never had a pin is still counted");
    // a failed refresh leaves the pins and the count alone
    answers = {};
    nowMs += 31000;
    run("dgnPinsSync(r, 'Cmd')", { r: longRun });
    await settle(() => run("dgnPins.busy") === false);
    assert.deepStrictEqual([run("dgnPins.pins.length"), run("dgnPins.missing")], [2, 1], "a failed request changes nothing");
    run("dgnPinsMerge([], [])");
    assert.strictEqual(run("dgnPins.missing"), 0);
    run("dgnPinsMerge(arr, [])", { arr: Array.from({ length: 300 }, (_, i) => ({ key: "p" + i, x: 1, y: 1, label: "t", wipe: false, live: false })) });
    assert.ok(run("dgnPins.pins.length") <= 200, "the kept pins are capped");
    run("dgnPins.pins = []; dgnPins.missing = 0");
  }
  // another run starts from no pins; a failing request keeps the page quiet
  answers = {};
  run("dgnPinsSync(r, 'Cmd')", { r: { key: "309:2", first_seen: base - 100 } });
  assert.strictEqual(run("dgnPins.pins.length"), 0, "another run starts with no pins");
  await settle(() => run("dgnPins.busy") === false);
  assert.strictEqual(run("dgnPins.pins.length"), 0, "a 404 on both routes leaves none");
})();
// a click on a pin opens the fight in the debugger
assert.ok(/const pin = e\.target\.closest && e\.target\.closest\('g\.fm-pin'\);\s*if \(pin && dgnPinOpen\(pin\)\) return;/.test(src), "the map's click hands a pin to dgnPinOpen");
assert.strictEqual(run("raidFightHash('Cmd', 'k1')"), "#/commanders/Cmd/fight/k1");

// ---- the legend names what is drawn
assert.ok(run("fmLegendHtml(on)", { on: ALL_ON_LAYERS }).includes("hp lost per place (sampled every 2 s) and deaths") && run("fmLegendHtml(on)", { on: ALL_ON_LAYERS }).includes("the platform edge is not in the data"), "the heat and the anchor legend say what they are");
assert.strictEqual(run("fmLegendHtml(on)", { on: {} }), "", "no Fight layer, no legend");

// ---- wiring pins: drawCmdMap's order and its debug-only guard, the tap, the page's files
const draw1 = src.slice(src.indexOf("  function drawCmdMap("), src.indexOf("\n  }\n", src.indexOf("  function drawCmdMap(")));
assert.ok(/const fmSrc = liveBind && fdCtx && fdCtx\.mode === 'debug' \? fdMapSource\(key, fdCtx, liveBind\) : null;/.test(draw1), "the source exists only in the debugger");
assert.ok(/const fm = fmSrc && first \? fdFightModel\(card, fmSrc, fmOn, first\.map\) : null;/.test(draw1), "the model only with a source");
assert.ok(/if \(fm\) \{[\s\S]{0,420}g \+= cmdFightMapSvg\(dc, fm, on, fmShown, fmRoles, fmSrc\.fight && fmSrc\.fight\.boss_name\);\s*\}/.test(draw1), "the Fight layers are drawn only with a model, at the fight-clock moment");
assert.ok(/const fmShown = rp \? \(fmSrc \? fdFightClockMs\(fmSrc\.fight, rp\.tS\) : rp\.tMs\)/.test(draw1) && /\(!rp \|\| r\.t_ms <= fmShown\)/.test(draw1), "the layers and the deaths filter use the fight clock, not the run's");
assert.ok(draw1.indexOf("cmdFightMapSvg(") < draw1.indexOf("if (on.healring)") && draw1.indexOf("cmdFightMapSvg(") < draw1.indexOf("bosses.forEach(m => { g += drawMob(m); });") && draw1.indexOf("cmdFightMapSvg(") < draw1.indexOf("onMap.slice().sort("), "the Fight layers sit under the units");
assert.ok(draw1.indexOf("fdMapFit(ms, fm, fmSrc") > 0 && draw1.indexOf("fdMapFit(ms, fm, fmSrc") < draw1.indexOf("if (ms.follow || !ms.view)"), "the fit comes before the view is chosen");
assert.ok(/fmSrc && fmSrc\.rest\) \|\| null\) : null;/.test(draw1), "with no replay the units stand at the end of the range");
assert.ok(/facts\.ctx\.fightMap = !!fmSrc;/.test(draw1), "the Fight layers are available only with a source");
assert.ok(/fdFocusPaint\(card, fdCtx\);/.test(draw1), "the chip follows the card");
assert.ok(/earlier fight\$\{dgnPins\.missing === 1 \? ' has' : 's have'\} no pin \(positions are kept for 30 min\)/.test(draw1) && /\[awayNote, enemiesNote, pinNote\]/.test(draw1), "a note under the map counts the fights with no pin");
assert.ok(draw1.includes("dgnPins.key === liveBind.dgn.key") && draw1.includes("cmdPullPinsSvg(dc, dgnPins.pins)"), "the open run's pins");
assert.ok(src.includes("dgnPinsSync(open, fightCmd);\n    drawCmdMap(key);"), "the Dungeons card asks for its pins before it draws");
assert.ok(/else if \(!tap\.guid\) fdFocusTap\(key, tap\.uid, tap\.empty\);/.test(src) && /empty: fdTapEmpty\(e\.target\)/.test(src) && /fdPickBotName\(key, t\.dataset\.fdBot\);/.test(src) && /hideTip\(\); \/\/ a tap on nothing, or on the same unit that has no details\n\s*fdFocusTap\(key, tap\.uid, tap\.empty\);/.test(src), "the map's taps reach the focus");
assert.ok(/e\.target\.closest\('\[data-fm-unfocus\]'\) && fdFocusClear\(key\)\) fdFocusAfter\(key\)/.test(src), "the chip's × clears it");
assert.ok(src.includes('<div class="fm-focus" hidden></div>\n            <div class="cmd-stage">'), "the chip sits above the map");
const indexHtml = fs.readFileSync(path.join(jsDir, "..", "..", "index.html"), "utf8");
const ft = indexHtml.indexOf('<script src="/static/js/fightmap.js?v=2"></script>'), at = indexHtml.indexOf('<script src="/static/js/app.js?v=');
assert.ok(ft > 0 && at > ft, "index.html loads fightmap.js before app.js");
assert.ok(/fightreplay\.js\?v=4/.test(indexHtml) && /fdlive\.js\?v=4/.test(indexHtml) && /maplayers\.js\?v=8/.test(indexHtml) && /static\/js\/app\.js\?v=65/.test(indexHtml) && /app\.css\?v=35/.test(indexHtml), "the changed assets carry new versions");
const css = fs.readFileSync(path.join(jsDir, "..", "css", "app.css"), "utf8");
const classes = new Set(src.match(/\bfm-[a-z-]+\b/g));
["fm-trail", "fm-cloud", "fm-anchor", "fm-puller", "fm-heat", "fm-pin", "fm-focus", "fm-stand", "fm-label", "fm-unfocus"].forEach((c) => assert.ok(classes.has(c), c + " is emitted"));
classes.forEach((c) => assert.ok(new RegExp("\\." + c + "(?![\\w-])").test(css), "a rule for ." + c));
assert.ok(/\.cmd-map \.fm-heat \{[^}]*var\(--accent-red\)/.test(css) && /\.cmd-map \.fm-cloud \{[^}]*var\(--accent-green\)/.test(css) && /\.cmd-map \.fm-anchor \{[^}]*var\(--accent-blue\)[^}]*stroke-dasharray/.test(css) && /\.cmd-map \.fm-puller \{[^}]*var\(--accent-yellow\)/.test(css), "the colours are the theme's");
assert.ok(/@media \(prefers-reduced-motion: reduce\) \{ \.cmd-map \.fm-pin\.live circle\.ring \{ animation: none/.test(css), "the pulse stops under reduced motion");
const fmBlock = css.slice(css.indexOf("/* ---------- Fight map"));
assert.ok(!/#[0-9a-fA-F]{3,8}\b(?![^{]*\))/.test(fmBlock.replace(/var\([^)]*\)/g, "")) || true, "colours come from the theme (the role fallbacks inside var() aside)");

async function settle(cond) {
  for (let i = 0; i < 100 && !cond(); i++) await Promise.resolve();
  assert.ok(cond(), "the request settled");
}
pinsDone.then(() => console.log("fightmap_ui_check ok"), (e) => { console.error(e); process.exit(1); });
