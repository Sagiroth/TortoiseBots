"use strict";
// Run by commandersjs_test.go under node: the Commanders tab's live position frames (app.js's pos handler). A pos frame
// carries the game server's own ts_ms: the samples and the card's clock are kept in server time, and the dot is drawn a delay
// behind the newest frame, interpolated between samples — it neither jumps on a frame nor stops between two.
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");

const appPath = path.resolve(process.argv[2]), dir = path.dirname(appPath);
const src = fs.readFileSync(appPath, "utf8");
function grab(name) {
  const re = new RegExp(`^  function ${name}\\(`, "m"), m = re.exec(src);
  assert.ok(m, `${name} not found`);
  const end = src.indexOf("\n  }\n", m.index);
  return src.slice(m.index, end + 4);
}

const Glide = require(path.join(dir, "glide.js"));
const LiveView = require(path.join(dir, "live.js"));

const ctx = { console, Array, Object, Math, JSON, String, Number };
vm.createContext(ctx);
vm.runInContext([grab("cmdLiveHandlers"), grab("cmdLiveKeyCommander")].join("\n"), ctx);

const bind = { commander: "A", mobs_map: 249, bots: [{ guid: 1, name: "Lead", x: 0, y: 0, map: 249, alive: true }], mobs: [] };
const marks = [];
let nowMs = 0;
Object.assign(ctx, {
  Glide, LiveView,
  performance: { now: () => nowMs },
  refreshGate: { interval: () => 1 },
  state: { commanders: { list: [bind] }, wsConnected: true },
  cmdGlide: {},
  cmdFindBind: () => bind,
  cmdSched: { mark: (k) => marks.push(k) },
  cmdLiveEvents: {}, CMD_LIVE_EVENT_MAX: 12,
  cmdLivePaintEvents: () => {}, cmdLiveFightChanged: () => {}, cmdLiveError: () => {},
  fetchCommanders: () => {}, cmdFightsCache: {}, cmdLiveMergeBinding: () => {}, applyCommanders: () => {},
});
const handlers = ctx.cmdLiveHandlers();
// the server's clock is unrelated to the page's: 500 ms of game time arrived 900 ms apart
handlers.pos({ commander: "A", ts_ms: 5000000, bots: [{ g: 1, x: 0, y: 0, m: 249, a: 1 }], mobs: [] });
nowMs = 900;
handlers.pos({ commander: "A", ts_ms: 5000500, bots: [{ g: 1, x: 20, y: 0, m: 249, a: 1 }], mobs: [] });
const st = ctx.cmdGlide.A;
assert.ok(st, "the commander has a glide state");
assert.strictEqual(st.offset, -5000000, "the clock is the frame's ts_ms, not its arrival time");
assert.strictEqual(st.delay, 625, "the render delay is 1.25 x the 500 ms sample gap: about 0.6 s behind in combat");
const p = Glide.at(st, Glide.botKey(1), 900);
assert.ok(p, "the dot is drawn");
assert.ok(Math.abs(p.x - 11) < 1e-6, "the render time is the server's 5000275, a third of the way to the newest sample: " + p.x);
assert.ok(p.x < 20, "the dot is behind the newest frame, not on it");
const later = Glide.at(st, Glide.botKey(1), 1900);
assert.ok(later.x > p.x, "and it keeps walking between the reports");
assert.strictEqual(marks.length, 2, "each pos frame asks for the card's redraw");
console.log("live pos gap ok");
