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

// ---- the debugger mode and the picked fight live in the hash ----------------------------------------------------------
const route = (hash) => { location._hash = hash; return run("parseRoute()"); };
let r = route("#/commanders/Glacienne/fight");
assert.deepStrictEqual([r.cmd, r.cmdMode, r.cmdFight], ["Glacienne", "debug", null], "the old tab form's new shape opens the debugger");
r = route("#/fight");
assert.deepStrictEqual([r.tab, r.cmd, r.cmdMode, r.cmdFight], ["commanders", null, "debug", null], "All raids in the debugger");
r = route("#/commanders/fight");
assert.deepStrictEqual([r.tab, r.cmd, r.cmdMode, r.cmdFight], ["commanders", "fight", null, null],
  "one segment after #/commanders is always the commander tab, even named fight");
const KEY = "Glacienne\u0000" + 7 + "\u0000f3";
r = route("#/commanders/Glacienne/fight/" + encodeURIComponent(KEY));
assert.deepStrictEqual([r.cmd, r.cmdMode, r.cmdFight], ["Glacienne", "debug", KEY], "the fight key survives its encoding");
r = route("#/fight/" + encodeURIComponent(KEY));
assert.deepStrictEqual([r.tab, r.cmd, r.cmdMode, r.cmdFight], ["commanders", null, "debug", KEY], "the All-raids route carries its fight too");
r = route("#/commanders/A%2FB/fight");
assert.deepStrictEqual([r.cmd, r.cmdMode, r.cmdFight], ["A/B", "debug", null], "a slash in a commander name still works");
r = route("#/commanders/Glacienne"); // the old form keeps working
assert.deepStrictEqual([r.cmd, r.cmdMode, r.cmdFight], ["Glacienne", null, null]);
r = route("#/commanders/"); // a trailing slash stays All raids
assert.deepStrictEqual([r.tab, r.cmd, r.cmdMode, r.cmdFight], ["commanders", null, null, null]);

// Every route the page writes parses back to itself, a commander named "fight" in any case included.
const roundTrip = (hash) => { location._hash = hash; assert.strictEqual(run("routeHash(parseRoute())"), hash, "the route round-trips: " + hash); };
for (const name of ["fight", "Fight", "FIGHT", "Glacienne"]) roundTrip("#/commanders/" + encodeURIComponent(name));
roundTrip("#/commanders");
for (const hash of ["#/fight", "#/fight/K", "#/commanders/Glacienne/fight", "#/commanders/Glacienne/fight/K", "#/commanders/fight/fight"]) roundTrip(hash);
assert.strictEqual(run("routeHash({ tab: 'commanders', cmd: 'fight', cmdMode: 'debug' })"), "#/commanders/fight/fight",
  "the All-raids route never stands in for a commander named fight");
assert.strictEqual(run("routeHash({ tab: 'commanders', cmd: null, cmdMode: 'debug' })"), "#/fight");
assert.strictEqual(run("routeHash({ tab: 'commanders', cmd: null, cmdMode: 'debug', cmdFight: 'K' })"), "#/fight/K");
assert.strictEqual(run("routeHash({ tab: 'commanders', cmd: 'Glacienne', cmdMode: 'debug', cmdFight: 'K' })"), "#/commanders/Glacienne/fight/K");
assert.strictEqual(run("routeHash({ tab: 'commanders', cmd: 'Glacienne' })"), "#/commanders/Glacienne", "the old form is still written when no mode is given");

// Back and forward between the two debugger routes: go() pushes the new hash, Back and Forward hand the same hashes back,
// and each one parses to its own view and writes itself unchanged, so history never rewrites the route.
const hist = ["#/fight"];
let idx = 0;
const visit = (hash) => {
  location._hash = hash;
  const p = run("parseRoute()");
  assert.strictEqual(run("routeHash(parseRoute())"), hash, "history lands on the same route: " + hash);
  return p;
};
run("go({ cmd: 'Glacienne', cmdMode: 'debug', cmdFight: null })");
assert.strictEqual(location._hash, "#/commanders/Glacienne/fight", "All raids walks to a commander's debugger: " + location._hash);
hist.push(location._hash); idx = 1;
let back = visit(hist[--idx]);
assert.deepStrictEqual([back.tab, back.cmd, back.cmdMode, back.cmdFight], ["commanders", null, "debug", null], "Back is All raids in the debugger");
let fwd = visit(hist[++idx]);
assert.deepStrictEqual([fwd.tab, fwd.cmd, fwd.cmdMode, fwd.cmdFight], ["commanders", "Glacienne", "debug", null], "Forward is the commander's debugger");
run("go({ cmd: 'fight', cmdMode: null, cmdFight: null })");
assert.strictEqual(location._hash, "#/commanders/fight", "a commander named fight is its own tab: " + location._hash);
assert.strictEqual(visit(location._hash).cmd, "fight");
run("go({ cmd: null, cmdMode: 'debug', cmdFight: null })");
assert.strictEqual(location._hash, "#/fight", "and All raids stays clear of it: " + location._hash);

// the whole view survives a profile: reloading #/commanders/Glacienne/fight/K then closing the drawer comes back to it
ctx.state.activeTab = "commanders";
location._hash = "#/commanders/Glacienne/fight/K";
run("go({ bot: 9 })");
assert.strictEqual(location._hash, "#/commanders/Glacienne/fight/K?bot=9", "the picked fight travels with the drawer: " + location._hash);
run("go({ bot: null })");
assert.strictEqual(location._hash, "#/commanders/Glacienne/fight/K", "closing it returns to the fight");
run("go({ tab: 'overview' })");
assert.strictEqual(location._hash, "#/overview", "another tab drops the debugger route");

// the demo choice is part of the view: a drawer over a demo fight comes back to the same simulated group
ctx.state.activeTab = "commanders";
location._hash = "#/commanders/DEMO%20(simulated)/fight/K1?demo=1";
run("go({ bot: 9001 })");
assert.strictEqual(location._hash, "#/commanders/DEMO%20(simulated)/fight/K1?bot=9001&demo=1", "the demo flag survives the drawer: " + location._hash);
run("go({ bot: null })");
assert.strictEqual(location._hash, "#/commanders/DEMO%20(simulated)/fight/K1?demo=1");

console.log("cmd route ok");
