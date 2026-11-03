"use strict";
// Run by commandersjs_test.go under node: the Commanders tab's live stream and the browser state it keeps, from app.js.
// showTab syncs the stream (it serves the Commanders tab only, so leaving it closes the stream), cmdPruneAll forgets a
// departed commander's glide state and live events along with its trails, and cmdGlideFocus names the one map that glides at
// the screen's rate (the open debugger tab or the only card).
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
// ---- cmdGlideFocus: the one map that glides at the screen's rate -----------------------------------------------------
{
  const ctx = {
    console, Array, Map,
    cmdPageMode: () => "live", fdPage: { open: null }, cmdCards: new Map(), state: { activeTab: "commanders" },
  };
  vm.createContext(ctx);
  vm.runInContext(grab("cmdGlideFocus"), ctx);
  assert.strictEqual(ctx.cmdGlideFocus(), null, "the Live page with several cards has no focused map");
  ctx.cmdCards.set("A", {});
  ctx.cmdCards.set("dgn:1", {});
  assert.strictEqual(ctx.cmdGlideFocus(), "A", "the only commander card is the one the viewer looks at (a dungeon card is not)");
  ctx.cmdCards.set("B", {});
  assert.strictEqual(ctx.cmdGlideFocus(), null, "two cards: every visible map glides at the grid rate");
  ctx.cmdPageMode = () => "debug";
  ctx.fdPage.open = "B";
  assert.strictEqual(ctx.cmdGlideFocus(), "B", "the open Fight debugger tab is focused");
  ctx.fdPage.open = null;
  assert.strictEqual(ctx.cmdGlideFocus(), null, "the All raids grid has no focused map");
  ctx.state.activeTab = "overview";
  assert.strictEqual(ctx.cmdGlideFocus(), null, "off the Commanders tab nothing glides");
}
console.log("live sync ok");
