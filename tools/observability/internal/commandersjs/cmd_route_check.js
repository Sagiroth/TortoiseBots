"use strict";
// Run by commandersjs_test.go under node: the page's route (parseRoute, routeHash, go) read out of app.js and run against a fake
// window. A profile opened over the Commanders tab keeps that tab and its hash, switching tabs drops it, and the profile links
// that point at a raid tab still land on it.
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");
const appPath = path.resolve(process.argv[2]);
const src = fs.readFileSync(appPath, "utf8");

function grab(name) {
  const re = new RegExp(`^  function ${name}\\(`, "m"), m = re.exec(src);
  assert.ok(m, `${name} not found`);
  const end = src.indexOf("\n  }\n", m.index);
  return src.slice(m.index, end + 4);
}

const ctx = { console, URLSearchParams, encodeURIComponent, decodeURIComponent, Object };
vm.createContext(ctx);
const routeSrc = src.slice(src.indexOf("  const TABS = {"), src.indexOf("  function openNavDrawer()"));
vm.runInContext(routeSrc + "\n" + grab("parseRoute") + "\n" + grab("routeHash") + "\n" + grab("go"), ctx);

// The page's bits go() touches: the current hash, history's replace, the tab state it keeps and the route painter it calls.
const location = { _hash: "#/commanders", get hash() { return this._hash; }, set hash(v) { this._hash = v; } };
Object.assign(ctx, {
  window: { location },
  history: { replaceState: (a, b, h) => { location._hash = h; } },
  state: { activeTab: "commanders", profileGuid: null, profileTab: "overview", dungeons: { openKey: null } },
  applyRoute: () => {},
});
const run = (expr, vars) => { if (vars) Object.assign(ctx, vars); return vm.runInContext(expr, ctx); };

// ---- a profile over an open raid tab keeps that tab ---------------------------------------------------------------
location._hash = "#/commanders/Glacienne";
assert.deepStrictEqual(run("parseRoute()", {}).cmd, "Glacienne");
run("go({ bot: 7 })");
assert.strictEqual(location._hash, "#/commanders/Glacienne?bot=7", "the show link keeps the raid tab: " + location._hash);
run("go({ bot: null })");
assert.strictEqual(location._hash, "#/commanders/Glacienne", "closing the drawer returns to the same tab: " + location._hash);
assert.strictEqual(run("parseRoute()").cmd, "Glacienne", "and the route still names it");

// the All raids tab keeps its own form with the drawer open
location._hash = "#/commanders";
run("go({ bot: 3 })");
assert.strictEqual(location._hash, "#/commanders?bot=3", "the All raids deep link keeps its shape");
run("go({ bot: null, ptab: 'gear' })");
assert.strictEqual(location._hash, "#/commanders", "closing it leaves All raids");

// switching tabs drops the raid tab; a profile on another tab carries only that tab
location._hash = "#/commanders/Glacienne?bot=7";
run("go({ tab: 'overview' })");
assert.strictEqual(location._hash, "#/overview", "a tab switch does not carry the raid tab: " + location._hash);
ctx.state.activeTab = "roster"; // what applyRoute does after that switch
location._hash = "#/bots";
run("go({ bot: 5 })");
assert.strictEqual(location._hash, "#/bots?bot=5", "a profile on another tab keeps that tab");

// a profile drawn over All raids in the debugger keeps the debugger's own route
ctx.state.activeTab = "commanders";
location._hash = "#/commanders/Glacienne";
run("go({ bot: 7, ptab: 'gear' })");
assert.strictEqual(location._hash, "#/commanders/Glacienne?bot=7&p=gear", "the profile tab travels with the raid tab: " + location._hash);

console.log("cmd route ok");
