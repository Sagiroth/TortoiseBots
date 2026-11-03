"use strict";
// Run by commandersjs_test.go under node: the Live / Fight debugger switch and the route it writes. Clicking Fight debugger
// with nothing bound keeps the hash (the saved-fights list is this page's view, not a bare #/fight route), and Live keeps the
// open commander in the route (#/commanders/<name>, not All raids). The demo flag is applied before the first render, so a
// #/commanders?demo=1 load never flashes "DEMO (simulated) is gone".
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");

const appPath = path.resolve(process.argv[2]);
const src = fs.readFileSync(appPath, "utf8");
function grab(name) {
  const re = new RegExp(`^  function ${name}\\(`, "m"), m = re.exec(src);
  assert.ok(m, `${name} not found`);
  return src.slice(m.index, src.indexOf("\n  }\n", m.index) + 4);
}

const ctx = { console, URLSearchParams, encodeURIComponent, decodeURIComponent, Object };
vm.createContext(ctx);
const routeSrc = src.slice(src.indexOf("  const TABS = {"), src.indexOf("  function openNavDrawer()"));
vm.runInContext([routeSrc, grab("parseRoute"), grab("routeHash"), grab("cmdPageHash"), grab("cmdPageSetMode")].join("\n"), ctx);

const location = { _hash: "#/commanders", get hash() { return this._hash; }, set hash(v) { this._hash = v; } };
Object.assign(ctx, {
  window: { location },
  history: { replaceState: (a, b, h) => { location._hash = h; }, pushState: (a, b, h) => { location._hash = h; } },
  fdPage: { mode: null, explicit: false, loaded: true, open: null, fight: null },
  fdPageStore: { getItem: () => null, setItem: () => {} },
  cmdPageMode: () => ctx.fdPage.mode || "live",
  fdStopCard: () => {},
  cmdPaintPageMode: () => {},
  renderCommanders: () => {},
});
const run = (expr, vars) => { if (vars) Object.assign(ctx, vars); return vm.runInContext(expr, ctx); };

// ---- nothing bound: the debugger keeps the hash ---------------------------------------------------------------------
location._hash = "#/commanders";
Object.assign(ctx.fdPage, { open: null, fight: null, explicit: true, mode: "live" });
run("cmdPageSetMode('debug')");
assert.strictEqual(location._hash, "#/commanders", "no raid bound: the button keeps the hash, no bare #/fight: " + location._hash);
assert.strictEqual(ctx.fdPage.mode, "debug", "and the view is the debugger");

// A hash that already names the debugger goes back to All raids on Live.
location._hash = "#/fight";
Object.assign(ctx.fdPage, { open: null, fight: null, mode: "debug" });
run("cmdPageSetMode('live')");
assert.strictEqual(location._hash, "#/commanders", "Live clears the debugger route");
assert.strictEqual(ctx.fdPage.mode, "live");

// The demo flag and a query are kept.
location._hash = "#/commanders?demo=1";
Object.assign(ctx.fdPage, { open: null, fight: null, mode: "live" });
run("cmdPageSetMode('debug')");
assert.strictEqual(location._hash, "#/commanders?demo=1", "the query survives the button: " + location._hash);

// ---- Live keeps the commander ----------------------------------------------------------------------------------------
location._hash = "#/commanders/Glacienne/fight";
Object.assign(ctx.fdPage, { open: "Glacienne", fight: "K", mode: "debug" });
run("cmdPageSetMode('live')");
assert.strictEqual(location._hash, "#/commanders/Glacienne", "Live keeps the commander in the URL: " + location._hash);
assert.strictEqual(ctx.fdPage.open, "Glacienne", "the commander's Live card stays open");
assert.strictEqual(ctx.fdPage.fight, null, "the fight segment goes");

// ---- the demo comes on before the first render -----------------------------------------------------------------------
const apply = grab("applyRoute");
assert.ok(apply.indexOf("setCmdDemo(true, true)") > 0, "applyRoute turns the demo on");
assert.ok(apply.indexOf("setCmdDemo(true, true)") < apply.indexOf("cmdRouteTab(r)"),
  "the demo is on before the route picks the tab, so the first render shows it (no 'is gone' flash)");
assert.ok(apply.indexOf("setCmdDemo(true, true)") < apply.indexOf("showTab(tab)"), "and before the first paint");
assert.strictEqual((src.match(/setCmdDemo\(true, true\)/g) || []).length, 1, "the demo flag is applied once per route");

console.log("fd mode route ok");
