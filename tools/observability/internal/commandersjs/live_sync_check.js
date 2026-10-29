"use strict";
// Run by commandersjs_test.go under node: the Commanders tab's live stream and the browser state it keeps, from app.js.
// showTab syncs the stream (it serves the Commanders tab only, so leaving it closes the stream), and cmdPruneAll forgets a
// departed commander's glide state and live events along with its trails.
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

// ---- showTab: every tab change syncs the live stream ----------------------------------------------------------------
{
  const calls = [];
  const ctx = {
    console, Date,
    TABS: { overview: { title: "Overview" }, commanders: { title: "Commanders" }, map: { title: "Map" } },
    state: { activeTab: "overview", rosterAt: 0 },
    closeNavDrawer: () => {}, paintNavActive: () => {},
    el: { tabViews: [] }, renderTab: () => {},
    fetchRoster: () => {}, fetchFeed: () => {}, fetchAuctions: () => {}, fetchAnomalyTotals: () => {},
    fetchCommanders: () => {}, fetchDungeons: () => {}, fetchDgRuns: () => {}, fetchAgentMaps: () => {},
    cmdLiveSync: () => calls.push("sync"),
  };
  vm.createContext(ctx);
  vm.runInContext(grab("showTab"), ctx);
  ctx.showTab("commanders");
  assert.strictEqual(ctx.state.activeTab, "commanders");
  assert.deepStrictEqual(calls, ["sync"], "showing the Commanders tab syncs the stream (it opens)");
  ctx.showTab("overview");
  assert.strictEqual(ctx.state.activeTab, "overview");
  assert.deepStrictEqual(calls, ["sync", "sync"], "leaving the Commanders tab syncs it again (it closes)");
  ctx.showTab("not-a-tab");
  assert.strictEqual(ctx.state.activeTab, "overview", "an unknown tab falls back to the overview");
  assert.strictEqual(calls.length, 3, "and syncs");
}

// ---- cmdPruneAll: the glide state and live events of a commander that is gone ---------------------------------------
{
  const ctx = {
    console, Array, Object,
    cmdAllBinds: () => [{ commander: "A", bots: [] }],
    CmdView: { prune: () => {} }, UnitInfo: { pruneEvents: () => {} },
    CmdPaths: { pruneTrails: () => {}, pruneStalls: () => {} },
    cmdTracker: { trail: {} }, cmdEvents: {}, cmdTrails: {}, cmdStalls: {},
    cmdGlide: { A: { units: {} }, B: { units: {} } },
    cmdLiveEvents: { A: [{ ev: {} }], B: [{ ev: {} }] },
    cmdMobSeenAt: { A: { 5: 1 }, B: { 6: 1 } },
  };
  vm.createContext(ctx);
  vm.runInContext(grab("cmdPruneAll"), ctx);
  ctx.cmdPruneAll();
  assert.deepStrictEqual(Object.keys(ctx.cmdGlide), ["A"], "a gone commander's glide state is dropped");
  assert.deepStrictEqual(Object.keys(ctx.cmdLiveEvents), ["A"], "a gone commander's live events are dropped");
  assert.deepStrictEqual(Object.keys(ctx.cmdMobSeenAt), ["A"], "a gone commander's cast clocks are dropped");
  assert.ok(ctx.cmdGlide.A && ctx.cmdLiveEvents.A, "the commanders still bound keep theirs");
}
console.log("live sync ok");
