"use strict";
// Run by commandersjs_test.go under node: /api/v1/dungeons is not polled twice a second off screen. The rollup is asked with
// every snapshot only while the Dungeons tab or a dungeon card is on screen; anywhere else it is asked once every 30 s (the
// profile's run link and the world map's link still read it). The site audit measured 30 requests a minute on a Commanders
// tab that never showed a dungeon.
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
const grabLine = (name) => {
  const m = new RegExp(`^  const ${name} = `, "m").exec(src);
  assert.ok(m, `${name} not found`);
  return src.slice(m.index, src.indexOf(";\n", m.index) + 2);
};

const ctx = { console, Map };
vm.createContext(ctx);
vm.runInContext([grab("dgnOnScreen"), grab("dgnPollDue"), grabLine("DGN_POLL_MS")].join("\n"), ctx);
const run = (expr, vars) => { if (vars) Object.assign(ctx, vars); return vm.runInContext(expr, ctx); };

const T0 = 1000000000;
assert.strictEqual(run("DGN_POLL_MS"), 30000, "the off-screen window is 30 s");
assert.strictEqual(run("dgnPollDue(true, 0, now)", { now: T0 }), true, "on screen: every snapshot");
assert.strictEqual(run("dgnPollDue(true, now - 1000, now)", { now: T0 }), true);
assert.strictEqual(run("dgnPollDue(false, 0, now)", { now: T0 }), true, "a page that never asked asks once");
assert.strictEqual(run("dgnPollDue(false, now - 29000, now)", { now: T0 }), false, "off screen: not before the 30 s are up");
assert.strictEqual(run("dgnPollDue(false, now - 30000, now)", { now: T0 }), true, "off screen: again after 30 s");

// On screen: the Dungeons tab, a dungeon card in the Commanders page, or the live runs drawn there.
ctx.state = { activeTab: "overview", dungeons: { binds: [] } };
ctx.cmdCards = new Map();
assert.strictEqual(run("dgnOnScreen()"), false, "the overview shows no dungeon");
ctx.state.activeTab = "dungeons";
assert.strictEqual(run("dgnOnScreen()"), true, "the Dungeons tab shows the rollup");
ctx.state.activeTab = "commanders";
assert.strictEqual(run("dgnOnScreen()"), false, "a Commanders tab with no dungeon card");
ctx.cmdCards.set("dgn:1:2", {});
assert.strictEqual(run("dgnOnScreen()"), true, "a dungeon card is on the Commanders page");
ctx.cmdCards.delete("dgn:1:2");
ctx.state.dungeons.binds = [{}];
assert.strictEqual(run("dgnOnScreen()"), true, "a live dungeon run is drawn there");
ctx.state.dungeons.binds = [];
ctx.state.activeTab = "map";
assert.strictEqual(run("dgnOnScreen()"), false, "the map tab asks every 30 s");

// The snapshot handler asks through the rule, not on every message.
const hs = src.slice(src.indexOf("  function handleStreamMessage("), src.indexOf("\n  }\n", src.indexOf("  function handleStreamMessage(")));
assert.ok(/if \(dgnPollDue\(dgnOnScreen\(\), state\.dungeons\.at, Date\.now\(\)\)\) fetchDungeons\(\);/.test(hs),
  "the roster snapshot polls the rollup through dgnPollDue");
assert.ok(!/^\s+fetchDungeons\(\);$/m.test(hs), "the snapshot handler no longer fetches the rollup unconditionally");
assert.ok(/state\.dungeons\.at = Date\.now\(\);/.test(src), "the last answer's time is kept for the window");

console.log("dungeons poll ok");
