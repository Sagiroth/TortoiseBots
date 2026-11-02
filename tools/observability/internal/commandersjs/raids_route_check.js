"use strict";
// Run by commandersjs_test.go under node: the Raids route and debugger cross-links.
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");

const appPath = path.resolve(process.argv[2]);
const src = fs.readFileSync(appPath, "utf8");

function grab(name) {
  let re = new RegExp(`^  function ${name}\\(`, "m"), m = re.exec(src);
  if (m) {
    const end = src.indexOf("\n  }\n", m.index);
    return src.slice(m.index, end + 4);
  }
  // ...or two-space indented const arrows (`const cmdFindBind = key => ...;`).
  re = new RegExp(`^  const ${name} = `, "m");
  m = re.exec(src);
  assert.ok(m, `${name} not found`);
  return src.slice(m.index, src.indexOf(";\n", m.index) + 2);
}
// ---- route: #/raids?run=<key>&group=<group> round-trips through parseRoute/routeHash/go ----
const ctx = { console, URLSearchParams, encodeURIComponent, decodeURIComponent, Object };
vm.createContext(ctx);
vm.runInContext(src.slice(src.indexOf("  const TABS = {"), src.indexOf("  function openNavDrawer()")), ctx);
vm.runInContext([grab("parseRoute"), grab("routeHash"), grab("go")].join("\n"), ctx);
const location = { _hash: "#/raids", get hash() { return this._hash; }, set hash(v) { this._hash = v; } };
Object.assign(ctx, {
  window: { location },
  history: { replaceState: (a, b, h) => { location._hash = h; } },
  state: { activeTab: "raids", profileGuid: null, profileTab: "overview", dungeons: { openKey: null }, raids: { openKey: null, openGroup: null } },
  applyRoute: () => {},
});
const run = (expr, vars) => { if (vars) Object.assign(ctx, vars); return vm.runInContext(expr, ctx); };

location._hash = "#/raids?run=409%3A7&group=raid%237";
let r = run("parseRoute()");
assert.strictEqual(r.tab, "raids", "tab: " + JSON.stringify(r));
assert.strictEqual(r.run, "409:7");
assert.strictEqual(r.group, "raid#7");
run("go({ tab: 'raids', run: '409:7', group: 'raid#7' })", {});
assert.strictEqual(location._hash, "#/raids?run=409%3A7&group=raid%237", "deep link round-trips: " + location._hash);

// ---- cmdFindBind returns the observed bind; Live drops it; the debugger keeps it ----
const ctx2 = { console };
vm.createContext(ctx2);
vm.runInContext([
  grab("cmdLiveBinds"),
  "var cmdAllBinds = () => commanders.concat(state.dungeons.binds, state.raids.binds);",
  grab("cmdFindBind"),
  grab("cmdBindForCard"),
  grab("cmdRaidShownTwice"),
  grab("cmdDebugBinds"),
  grab("fdTabsModel"),
  "var FdTabs = { sortBinds: (l) => l.slice().sort((a, b) => String(a.commander) < String(b.commander) ? -1 : 1), stateOf: (b, w) => w || 'idle', deadCount: () => 0, model: (rows) => rows };",
  "var CmdView = { stateWord: () => 'idle', context: () => ({}) };",
  "var cmdNowFor = () => 100; var cmdPlace = () => ''; var cmdFightsCache = {}; var fdPageSeen = {}; var cmdTracker = {}; var fdPage = { gone: {} };",
].join("\n"), ctx2);
Object.assign(ctx2, {
  state: { commanders: { list: [] }, dungeons: { binds: [] }, raids: { binds: [] } },
  commanders: [],
});
const run2 = (expr) => vm.runInContext(expr, ctx2);
run2("commanders = [{ commander: 'Hypeer' }]");
run2("state.commanders.list = commanders");
run2("state.raids.binds = [{ commander: 'raid#7', cardKey: 'raid:409:7:raid#7', observed: true, bots: [] }]");
assert.strictEqual(run2("cmdFindBind('raid#7').commander"), "raid#7", "the open card resolves through cmdFindBind");
assert.strictEqual(run2("cmdBindForCard('raid:409:7:raid#7').commander"), "raid#7", "the Raids card resolves through cardKey");
assert.deepStrictEqual(JSON.parse(JSON.stringify(run2("cmdLiveBinds().map(b => b.commander)"))), ["Hypeer"],
  "the Commanders Live list does not contain the observed raid");
assert.deepStrictEqual(JSON.parse(JSON.stringify(run2("cmdDebugBinds().map(b => b.commander)"))),
  ["Hypeer", "raid#7"], "the debugger list does contain it");
assert.deepStrictEqual(JSON.parse(JSON.stringify(run2("fdTabsModel(cmdDebugBinds()).map(r => r.key)"))),
  ["Hypeer", "raid#7"], "the debugger tab strip does contain it");
run2("state.raids.binds = [{ commander: 'Hypeer', cardKey: 'raid:409:7:Hypeer', observed: true, bound: true, bots: [] }]");
assert.deepStrictEqual(JSON.parse(JSON.stringify(run2("cmdDebugBinds().map(b => b.commander)"))),
  ["Hypeer"], "a bound raid shows once, under its commander binding");

// ---- the Fights button builds #/commanders/<key>/fight ----
assert.ok(/function raidFightHash\(groupKey, fightKey\)/.test(src), "raidFightHash builds the debugger link");
const ctx3 = { console, encodeURIComponent };
vm.createContext(ctx3);
vm.runInContext(grab("raidFightHash"), ctx3);
assert.strictEqual(vm.runInContext("raidFightHash('raid#7', null)", ctx3), "#/commanders/raid%237/fight");

console.log("raids_route_check.js ok");
