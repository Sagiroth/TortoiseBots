"use strict";
// Run by commandersjs_test.go under node: /api/v1/raids is asked with every snapshot only while the Raids tab is on screen
// and no `raids` event came in 5 s (flag-off servers never send one); off screen it is asked once every 30 s. While `raids`
// events flow, no snapshot may trigger a fetch.
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
vm.runInContext([grab("raidsOnScreen"), grab("raidsPollDue"), grabLine("RAIDS_POLL_MS"), grabLine("RAIDS_EVENT_MS")].join("\n"), ctx);
const run = (expr, vars) => { if (vars) Object.assign(ctx, vars); return vm.runInContext(expr, ctx); };

const T0 = 1000000000;
assert.strictEqual(run("RAIDS_POLL_MS"), 30000, "the off-screen window is 30 s");
assert.strictEqual(run("RAIDS_EVENT_MS"), 5000, "a recent raids event suppresses the snapshot fetch");
assert.strictEqual(run("raidsPollDue(true, 0, 0, now)", { now: T0 }), true, "on screen with no event: every snapshot");
assert.strictEqual(run("raidsPollDue(true, now - 1000, 0, now)", { now: T0 }), true, "on screen, stale event: every snapshot");
assert.strictEqual(run("raidsPollDue(true, now - 4000, 0, now)", { now: T0 }), false, "an event 4 s ago suppresses the fetch: no fetch per snapshot while events flow");
assert.strictEqual(run("raidsPollDue(true, now - 5000, 0, now)", { now: T0 }), true, "after 5 s the snapshot fetches again");
assert.strictEqual(run("raidsPollDue(false, 0, 0, now)", { now: T0 }), true, "a page that never asked asks once");
assert.strictEqual(run("raidsPollDue(false, now - 29000, 0, now)", { now: T0 }), false, "off screen: not before the 30 s are up");
assert.strictEqual(run("raidsPollDue(false, now - 30000, 0, now)", { now: T0 }), true, "off screen: again after 30 s");

// On screen: the Raids tab. The Commanders tab does NOT count (its cards read state.dungeons only).
ctx.state = { activeTab: "overview", raids: { at: 0, eventAt: 0 } };
assert.strictEqual(run("raidsOnScreen()"), false, "the overview shows no raid");
ctx.state.activeTab = "raids";
assert.strictEqual(run("raidsOnScreen()"), true, "the Raids tab is on screen");
ctx.state.activeTab = "commanders";
assert.strictEqual(run("raidsOnScreen()"), false, "the Commanders tab does not poll raids");

// The snapshot handler asks through the rule, not on every message.
const hs = src.slice(src.indexOf("  function handleStreamMessage("), src.indexOf("\n  }\n", src.indexOf("  function handleStreamMessage(")));
assert.ok(/if \(raidsPollDue\(raidsOnScreen\(\), state\.raids\.at, state\.raids\.eventAt, Date\.now\(\)\)\) fetchRaids\(\);/.test(hs),
  "the roster snapshot polls the rollup through raidsPollDue");
assert.ok(/state\.raids\.at = Date\.now\(\);/.test(src), "the last answer's time is kept for the window");

console.log("raids_poll_check.js ok");
