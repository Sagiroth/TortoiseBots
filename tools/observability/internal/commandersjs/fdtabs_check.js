"use strict";
// Run by commandersjs_test.go under node: the Commanders page's raid tabs. web/static/js/fdtabs.js keeps the tab order, the
// state words, the fight verdict and the red-dot seen rule, the hash of an open tab and the per-commander fights cache;
// app.js builds the strip and the All raids grid. The checks here are the six the brief asks for: a strip of three
// commanders, only the open tab (and no others) fetching fight details, the red-dot rule, hash routing, twenty tabs and
// escaping of every name that comes from the game server.
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");

const appPath = path.resolve(process.argv[2]);
const tabsPath = path.resolve(process.argv[3]);
const cssPath = path.resolve(process.argv[4]);
const appSrc = fs.readFileSync(appPath, "utf8");
const tabsSrc = fs.readFileSync(tabsPath, "utf8");
const css = fs.readFileSync(cssPath, "utf8");

function grab(name) {
  const re = new RegExp(`^  function ${name}\\(`, "m"), m = re.exec(appSrc);
  assert.ok(m, `${name} not found`);
  const end = appSrc.indexOf("\n  }\n", m.index);
  return appSrc.slice(m.index, end + 4);
}

// The routing block: TABS/HASH_TO_TAB plus parseRoute and routeHash, run against a fake window.location.
const routeBlock = appSrc.slice(appSrc.indexOf("  const TABS = {"), appSrc.indexOf("  function openNavDrawer()"));

const ctx = { console, URLSearchParams, encodeURIComponent, decodeURIComponent };
vm.createContext(ctx);
vm.runInContext(tabsSrc, ctx, { filename: "fdtabs.js" });
assert.ok(ctx.FdTabs && typeof ctx.FdTabs.model === "function", "fdtabs.js loads as the global FdTabs");
vm.runInContext([
  grab("esc"), grab("fdTabsHtml"), grab("fdGridHtml"), grab("numOr0"),
  "var fdMiniMapSvg = () => '<svg class=\"cmd-mini-map\"></svg>';",
  "var fdTabsAll = (tabs) => tabs.filter(t => !t.gone).length;",
].join("\n"), ctx);
ctx.window = { location: { hash: "#/commanders" } };
vm.runInContext(routeBlock + "\n" + grab("parseRoute") + "\n" + grab("routeHash"), ctx);

const run = (expr, vars) => { if (vars) Object.assign(ctx, vars); return vm.runInContext(expr, ctx); };
const plain = (v) => JSON.parse(JSON.stringify(v));

// ---- 1. a strip of three commanders -----------------------------------------------------------------------------------
const THREE = [
  { key: "Alpha", name: "Alpha", plan: "onyxias_lair", place: "Onyxia's Lair", state: "fighting", dead: 0, gone: false, dot: true, last: { key: "a2" } },
  { key: "Bravo", name: "Bravo", plan: "", place: "Molten Core", state: "walking", dead: 2, gone: false, dot: false, last: null },
  { key: "Charlie", name: "Charlie", plan: "zulfarrak", place: "Zul'Farrak", state: "idle", dead: 0, gone: false, dot: false, last: null },
];
const strip = run("fdTabsHtml(tabs, 'Bravo')", { tabs: THREE });
assert.strictEqual((strip.match(/role="tab"/g) || []).length, 4, "All raids and one tab per commander: " + strip);
assert.ok(strip.includes(">All raids (3)<"), strip.slice(0, 300));
assert.deepStrictEqual([...strip.matchAll(/data-fdtab="([^"]*)"/g)].map((m) => decodeURIComponent(m[1])), ["Alpha", "Bravo", "Charlie"], "one tab per commander, in order");
assert.strictEqual((strip.match(/aria-selected="true"/g) || []).length, 1, "only the open tab is selected");
assert.ok(/data-fdtab="Bravo"[^>]*aria-selected="true"/.test(strip), "Bravo is the open tab");
assert.strictEqual((strip.match(/tabindex="0"/g) || []).length, 1, "roving tabindex: one tab is reachable with Tab");
assert.ok(strip.includes('class="cmd-tab-dot"'), "a new problem fight puts the dot on Alpha");
assert.ok(strip.includes("2 dead"), "the dead count is on Bravo");
assert.ok(/class="cmd-chip s-walking"[^>]*>walking</.test(strip), "the state chip is on the tab");
assert.ok(strip.includes("onyxias_lair") && strip.includes("Molten Core"), "the plan or the instance names the tab");

// in combat first, then by name
const sorted = run("FdTabs.sortBinds(binds).map(b => b.commander)", {
  binds: [{ commander: "Alpha", combat: null }, { commander: "Zulu", combat: { fight_ms: 1 } }, { commander: "Bravo", combat: null }],
});
assert.deepStrictEqual(plain(sorted), ["Zulu", "Alpha", "Bravo"], "a fighting commander comes first, the rest by name");

// ---- 2. only the open tab fetches fight details ------------------------------------------------------------------------
// The cache in fdtabs.js is the one the page uses: rows are fetched for the commanders whose card is drawn, details only for
// the open tab, and a summary is refetched at most every 10 s.
(async () => {
  let clock = 100000;
  const calls = [], detailCalls = [];
  const cache = {}, pending = {};
  const feed = ctx.FdTabs.newFights({
    cache, pending, now: () => clock, ms: 10000,
    fetchRows: (k) => { calls.push(k); return Promise.resolve([{ key: k + "-newest" }]); },
  });
  const list = (key) => new Promise((ok) => feed.list(key, ok));
  const a1 = list("Alpha");
  const a2 = list("Alpha"); // in flight: joins the one request
  assert.strictEqual(calls.length, 1, "one request per commander while one is in flight");
  await Promise.all([a1, a2]);
  assert.deepStrictEqual(plain(await list("Alpha")), [{ key: "Alpha-newest" }], "the cached answer is used");
  assert.strictEqual(calls.length, 1, "no second summary inside 10 s");
  await list("Bravo"); await list("Charlie");
  assert.deepStrictEqual(calls, ["Alpha", "Bravo", "Charlie"], "All raids asks once per commander");
  clock += 10001;
  await list("Alpha");
  assert.strictEqual(calls.length, 4, "after 10 s the summary is asked again");

  // A failed fetch is kept for the window too: a failing daemon is not asked again on every draw, and what was shown stays.
  let failClock = 500000, failCalls = 0;
  const failCache = {}, failPending = {};
  const failing = ctx.FdTabs.newFights({
    cache: failCache, pending: failPending, now: () => failClock, ms: 10000,
    fetchRows: () => { failCalls += 1; return Promise.reject(new Error("down")); },
  });
  const failList = (key) => new Promise((ok) => failing.list(key, ok));
  assert.strictEqual(await failList("Alpha"), null, "a failed fetch hands the callers null");
  assert.strictEqual(failCalls, 1, "one request");
  assert.strictEqual(await failList("Alpha"), null, "the failure is served from the cache");
  assert.strictEqual(failCalls, 1, "a second draw inside 10 s sends no request");
  assert.ok(failCache.Alpha && Math.abs(failCache.Alpha.at - failClock) < 1, "the failure carries the fetch time");
  failClock += 10001;
  await failList("Alpha");
  assert.strictEqual(failCalls, 2, "after the window the summary is asked for again");
  let keptClock = 600000, keptCalls = 0;
  const keptCache = {}, keptPending = {};
  const flaky = ctx.FdTabs.newFights({
    cache: keptCache, pending: keptPending, now: () => keptClock, ms: 10000,
    fetchRows: () => { keptCalls += 1; return keptCalls === 1 ? Promise.resolve([{ key: "k1" }]) : Promise.reject(new Error("down")); },
  });
  const keptList = (key) => new Promise((ok) => flaky.list(key, ok));
  assert.deepStrictEqual(plain(await keptList("Alpha")), [{ key: "k1" }]);
  keptClock += 10001;
  assert.deepStrictEqual(plain(await keptList("Alpha")), [{ key: "k1" }], "a failed refetch keeps the rows already shown");
  assert.strictEqual(keptCalls, 2);

  // The kept positions of a fight: a failure is retried after 30 s, never sooner, and an answer is never asked for again.
  assert.strictEqual(ctx.FdTabs.TRACK_RETRY_MS, 30000);
  assert.strictEqual(ctx.FdTabs.trackDue(undefined, undefined, 0), true, "never asked: ask");
  assert.strictEqual(ctx.FdTabs.trackDue("loading", undefined, 0), false, "an ask in flight: wait");
  assert.strictEqual(ctx.FdTabs.trackDue({ raw: {}, prep: {} }, undefined, 0), false, "an answer is kept for the page's life");
  assert.strictEqual(ctx.FdTabs.trackDue(null, 1000, 30999), false, "a failure is not retried inside 30 s");
  assert.strictEqual(ctx.FdTabs.trackDue(null, 1000, 31000), true, "a failure is retried after 30 s");
  assert.strictEqual(ctx.FdTabs.trackDue(null, undefined, 9e9), false, "a failure with no clock is not retried blindly");

  const detail = (k) => { detailCalls.push(k); return Promise.resolve({ fight: { id: k } }); };
  await feed.detail("Alpha", "Alpha-k1", ctx.FdTabs.detailAllowed("debug", "Alpha", "Alpha"), detail);
  assert.strictEqual(feed.detail("Bravo", "Bravo-k1", ctx.FdTabs.detailAllowed("debug", "Alpha", "Bravo"), detail), null,
    "a tab that is not open never fetches a fight detail");
  assert.strictEqual(feed.detail("Alpha", "Alpha-k2", ctx.FdTabs.detailAllowed("debug", null, "Alpha"), detail), null,
    "the All raids tab fetches no fight details");
  assert.deepStrictEqual(detailCalls, ["Alpha-k1"], "exactly the open tab's detail was fetched");
  assert.ok(appSrc.includes("FdTabs.detailAllowed(cmdPageMode(), fdPage.open"), "the page gates the detail, track and run fetches with the open tab");

  // ---- 3. the red-dot rule ---------------------------------------------------------------------------------------------
  const newestProblem = { key: "f2", start: 100, dur: 10000, deaths: 0, warnings: 1, outcome: "end" };
  const clean = { key: "f1", start: 80, dur: 10000, deaths: 0, warnings: 0, outcome: "end" };
  const wiped = { key: "f3", start: 120, dur: 10000, deaths: 3, warnings: 0, outcome: "wipe" };
  assert.strictEqual(ctx.FdTabs.problem(newestProblem), true, "a warning makes a fight a problem");
  assert.strictEqual(ctx.FdTabs.problem(clean), false);
  assert.strictEqual(ctx.FdTabs.problem(wiped), true, "a wipe is a problem");
  // model reads the list through the key of the row
  const tabDot = (list, seen) => plain(ctx.FdTabs.model([{ key: "Alpha" }], { fights: { Alpha: list }, seen: seen || {} }))[0];
  assert.strictEqual(tabDot(null).dot, false, "no fights, no dot");
  assert.strictEqual(tabDot([newestProblem, clean]).dot, true, "the newest fight's problem verdict shows the dot");
  assert.strictEqual(tabDot([newestProblem, clean], { f2: 1 }).dot, false, "a seen fight keeps no dot");
  assert.strictEqual(tabDot([clean, newestProblem]).dot, false, "only the newest fight counts");
  assert.strictEqual(tabDot([clean]).dot, false, "a clean newest fight has no dot");
  assert.strictEqual(tabDot([wiped]).dot, true);
  assert.strictEqual(plain(ctx.FdTabs.model([{ key: "Alpha" }], { fights: { Alpha: { list: [newestProblem], at: 1 } }, seen: {} }))[0].dot, true,
    "the page's cached {list, at} entry is read too");

  // ---- 4. hash routing -------------------------------------------------------------------------------------------------
  ctx.window.location.hash = "#/commanders";
  assert.strictEqual(run("parseRoute().tab"), "commanders");
  assert.strictEqual(run("parseRoute().cmd"), null, "#/commanders is All raids");
  ctx.window.location.hash = "#/commanders/Alpha%20Beta";
  assert.strictEqual(run("parseRoute().cmd"), "Alpha Beta", "the open tab is the URL");
  assert.strictEqual(run("parseRoute().tab"), "commanders");
  ctx.window.location.hash = "#/commanders/A%2FB?demo=1";
  assert.strictEqual(run("parseRoute().cmd"), "A/B", "a slash in a name survives");
  assert.strictEqual(run("parseRoute().demo"), true, "the demo choice survives");
  assert.strictEqual(run("routeHash({ tab: 'commanders', cmd: 'A B/C' })"), "#/commanders/A%20B%2FC");
  assert.strictEqual(run("routeHash({ tab: 'commanders', cmd: null })"), "#/commanders");
  ctx.window.location.hash = "#/commanders/";
  assert.strictEqual(run("parseRoute().cmd"), null, "a trailing slash is All raids");

  // ---- 5. twenty tabs, and the strip scrolls while the phone grid stacks ------------------------------------------------
  const many = Array.from({ length: 20 }, (_, i) => ({ key: "Raid " + i, name: "Raid " + i, plan: "", place: "Map " + i, state: "idle", dead: 0, gone: false, dot: false, last: null }));
  const wide = run("fdTabsHtml(tabs, null)", { tabs: many });
  assert.strictEqual((wide.match(/role="tab"/g) || []).length, 21, "All raids plus twenty commander tabs");
  assert.ok(wide.includes(">All raids (20)<"), "the All raids count");
  assert.ok(/\.cmd-tabs\s*\{[^}]*overflow-x:\s*auto/.test(css), "the strip scrolls sideways");
  assert.ok(/\.cmd-tabs\s*\{[^}]*flex-wrap:\s*nowrap/.test(css), "the tabs stay on one row");
  assert.ok(/\.cmd-tab\s*\{[^}]*flex:\s*0 0 auto/.test(css), "a tab keeps its width");
  assert.ok(/\.cmd-overview\s*\{[^}]*display:\s*grid/.test(css), "All raids is a grid");
  const phones = [...css.matchAll(/@media\s*\(max-width:\s*\d+px\)\s*\{([\s\S]*?)\n\}/g)].map((m) => m[1]);
  assert.ok(phones.length, "the stylesheet has a phone breakpoint");
  assert.ok(phones.some((b) => /\.cmd-overview\s*\{[^}]*grid-template-columns:\s*1fr/.test(b)), "the phone stacks the grid in one column");
  // The Fight debugger's rule holds on every tab: All raids drops the refresh explanation row and the demo box as well.
  const gridHide = /#tab-commanders\.cmd-debug-grid\s*>\s*\.kpi-row[\s\S]*?\{[^}]*display:\s*none/.exec(css);
  assert.ok(gridHide, "the All raids tab hides the page tiles");
  for (const sel of [".cmd-refresh-bar", ".cmd-demo-bar"]) {
    assert.ok(gridHide[0].includes(sel), "the All raids tab hides " + sel + " too: " + gridHide[0]);
  }

  // ---- 6. every name is escaped ----------------------------------------------------------------------------------------
  const EVIL = '<img src=x onerror=alert(1)>"&';
  const evilTabs = [{ key: EVIL, name: EVIL, plan: EVIL, place: EVIL, state: EVIL, dead: 1, gone: false, dot: true, last: null }];
  for (const html of [run("fdTabsHtml(tabs, null)", { tabs: evilTabs }), run("fdTabsHtml(tabs, tabs[0].key)", { tabs: evilTabs })]) {
    assert.ok(!/<img/i.test(html), "raw markup survived in the tab strip: " + html.slice(0, 300));
    assert.ok(html.includes("&lt;img"), "the name is escaped as text: " + html.slice(0, 300));
    for (const m of html.matchAll(/="([^"]*)"/g)) assert.ok(!/[<>]/.test(m[1]), "an attribute value broke out: " + m[1]);
    assert.strictEqual((html.match(/"/g) || []).length % 2, 0, "unbalanced quotes: " + html.slice(0, 300));
  }
  const evilGrid = run("fdGridHtml(binds, tabs)", {
    binds: [{ commander: EVIL, bots: [], run: { plan: EVIL, step: 1, steps: 2 } }],
    tabs: [{ key: EVIL, name: EVIL, plan: EVIL, place: EVIL, state: EVIL, dead: 1, gone: false, dot: false, last: { key: "k", warnings: 1 } }],
  });
  assert.ok(!/<img/i.test(evilGrid), "raw markup survived in the All raids grid: " + evilGrid.slice(0, 300));
  assert.ok(evilGrid.includes("&lt;img"), "the grid escapes the name");
  for (const m of evilGrid.matchAll(/="([^"]*)"/g)) assert.ok(!/[<>]/.test(m[1]), "an attribute value broke out: " + m[1]);

  console.log("raid tabs ok");
})().catch((e) => { console.error(e && e.stack || e); process.exit(1); });
