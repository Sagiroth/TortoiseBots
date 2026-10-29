"use strict";
// Run by commandersjs_test.go under node: the Commanders tab's live position frames (app.js's pos handler). A pos frame
// arrives at the live rate (500 ms in this check), so that is the gap the dots glide over; the 2 s report gap made them lag
// behind the game by about two seconds.
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
handlers.pos({ commander: "A", bots: [{ g: 1, x: 0, y: 0, m: 249, a: 1 }], mobs: [] });
nowMs = 500;
handlers.pos({ commander: "A", bots: [{ g: 1, x: 20, y: 0, m: 249, a: 1 }], mobs: [] });
const st = ctx.cmdGlide.A;
assert.ok(st, "the commander has a glide state");
assert.strictEqual(st.gap, 500, "the glide gap is the pos frame gap (500 ms), not the 2 s report gap");
const p = Glide.at(st, Glide.botKey(1), 1000);
assert.ok(p, "the dot is drawn");
assert.strictEqual(p.x, 20, "within one gap the dot reaches the point the frame reported");
assert.strictEqual(marks.length, 2, "each pos frame asks for the card's redraw");
console.log("live pos gap ok");
