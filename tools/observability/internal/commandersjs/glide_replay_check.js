"use strict";
// Run by commandersjs_test.go under node: a raid walk's live frames replayed, in their arrival order and at their page times,
// through the page's own code path, and the map drawn every 16 ms. The handlers are app.js's (cmdLiveHandlers,
// applyCommanders, cmdLiveMergeBinding: the pos frame, the WebSocket report through the 2 s refresh gate, the stream's
// commander frame while the WebSocket is down), the gate is refresh.js's, the glide is glide.js and live.js, and every frame
// is drawn as drawCmdMap draws it (Glide.apply on the card's state). Per unit it measures the backward steps (drawn movement
// against the unit's direction of travel over the last second) and names each one's cause: the render clock going back (the
// delay or the clock offset changed), a snap or reset, a sample from the 2 s report, or the server's own positions going back.
// It also flags every jump over 5 yd that comes back within 2 s and whether it landed on another unit (an identity mix-up).
//   node glide_replay_check.js <app.js> <trace.json> [--report]   (--report prints the per-unit table and does not assert)
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");

const appPath = path.resolve(process.argv[2]), dir = path.dirname(appPath);
const trace = JSON.parse(fs.readFileSync(path.resolve(process.argv[3]), "utf8"));
const reportOnly = process.argv.includes("--report");
const src = fs.readFileSync(appPath, "utf8");
function grab(name) {
  const re = new RegExp(`^  function ${name}\\(`, "m"), m = re.exec(src);
  assert.ok(m, `${name} not found`);
  const end = src.indexOf("\n  }\n", m.index);
  return src.slice(m.index, end + 4);
}

const G = require(path.join(dir, "glide.js"));
const LiveView = require(path.join(dir, "live.js"));
const RefreshRate = require(path.join(dir, "refresh.js"));

// ---- the page's clock and timers, simulated
let nowMs = trace.frames.length ? trace.frames[0][0] - 1 : 0;
const timers = [];
let timerSeq = 0;
const env = {
  now: () => nowMs,
  setTimer: (fn, ms) => { const h = ++timerSeq; timers.push({ h, at: nowMs + Math.max(0, ms), fn }); return h; },
  clearTimer: (h) => { const i = timers.findIndex((t) => t.h === h); if (i >= 0) timers.splice(i, 1); },
};

// ---- which feed brought each sample: the report (noteReport) or a pos frame (the pos handler)
let feed = null;
const note = G.note;
G.note = function (st, id) {
  const before = st && st.units && st.units[String(id)];
  const last = before && before.samples ? before.samples[before.samples.length - 1] : null;
  note.apply(this, arguments);
  const u = st && st.units && st.units[String(id)];
  const s = u && u.samples ? u.samples[u.samples.length - 1] : null;
  if (s && s !== last && !s.src) s.src = feed;
};
const noteReport = LiveView.noteReport;
LiveView.noteReport = function () { const was = feed; feed = "report"; try { return noteReport.apply(this, arguments); } finally { feed = was; } };

const key = trace.commander;
const ctx = {
  console, Array, Object, Math, JSON, String, Number, Date: { now: () => nowMs },
  Glide: G, LiveView,
  performance: { now: () => nowMs },
  refreshGate: RefreshRate.createGate(2, env),
  state: { activeTab: "commanders", wsConnected: true, commanders: { list: [] } },
  cmdGlide: {},
  cmdSched: { mark: () => {} },
  cmdLiveEvents: {}, CMD_LIVE_EVENT_MAX: 12,
  cmdLivePaintEvents: () => {}, cmdLiveFightChanged: () => {}, cmdLiveError: () => {},
  fetchCommanders: () => {}, cmdFightsCache: {},
  cmdNoteMobTimes: () => {}, CmdView: { note: () => {} }, UnitInfo: { noteEvents: () => {} }, cmdNotePaths: () => {},
  cmdTracker: {}, cmdEvents: {}, cmdPruneAll: () => {}, seedCmdTrails: () => {}, paintCommandersBadge: () => {},
  renderCommanders: () => {},
};
ctx.cmdFindBind = (k) => (ctx.state.commanders.list || []).find((b) => b && b.commander === k);
vm.createContext(ctx);
vm.runInContext([grab("cmdLiveHandlers"), grab("cmdLiveKeyCommander"), grab("applyCommanders"), grab("cmdLiveMergeBinding")].join("\n"), ctx);
const handlers = ctx.cmdLiveHandlers();
const onPos = handlers.pos;
handlers.pos = (d) => { const was = feed; feed = "pos"; try { onPos(d); } finally { feed = was; } };

// ---- the replay
const tracks = {};     // unit key -> [{t, x, y, cause}]
const inputs = {};     // bot key -> [{t: server ms, x, y}] from the pos frames (the server's own positions)
let lastRt = null;
const clockOf = (st) => (typeof G.clock === "function" ? st.rt : G.renderTime(st, nowMs));
function frame() {
  const bind = ctx.cmdFindBind(key);
  if (!bind) return;
  const st = ctx.cmdGlide[key] || (ctx.cmdGlide[key] = G.create());
  const drawn = G.apply(bind, st, nowMs); // drawCmdMap's own line
  const rt = clockOf(st);
  const clockBack = lastRt != null && rt < lastRt - 1e-6;
  lastRt = rt;
  const put = (k, x, y) => {
    const u = st.units[k], s = u && u.samples;
    let seg = null;
    if (s && s.length > 1) {
      let i = 0;
      while (i < s.length - 2 && s[i + 1].t <= rt) i += 1;
      seg = [s[i], s[i + 1]];
    }
    (tracks[k] || (tracks[k] = [])).push({ t: nowMs, x, y, clockBack, n: s ? s.length : 0, seg, raw: !u });
  };
  (drawn.bots || []).forEach((b) => put(G.botKey(b.guid), b.x, b.y));
  (drawn.mobs || []).forEach((m) => put(G.mobKey(m.id), m.x, m.y));
}

const events = trace.frames.map((f, i) => ({ at: f[0], i, kind: f[1], data: f[2] }));
let ei = 0;
const end = trace.end || (events.length ? events[events.length - 1].at + 1000 : 0);
let nextDraw = events.length ? events[0].at : 0;
while (nowMs <= end) {
  const nextEv = ei < events.length ? events[ei].at : Infinity;
  timers.sort((a, b) => a.at - b.at || a.h - b.h);
  const nextTimer = timers.length ? timers[0].at : Infinity;
  const t = Math.min(nextEv, nextTimer, nextDraw);
  if (t === Infinity) break;
  nowMs = t;
  if (t === nextTimer) { timers.shift().fn(); continue; }
  if (t === nextEv) {
    const e = events[ei++];
    if (trace.ws_down_at != null && e.at >= trace.ws_down_at) ctx.state.wsConnected = false;
    if (e.kind === "ws") ctx.applyCommanders(e.data);
    else if (e.kind === "commander") handlers.commander(e.data);
    else if (e.kind === "pos") {
      handlers.pos(e.data);
      (e.data.bots || []).forEach((b) => (inputs[G.botKey(b.g)] || (inputs[G.botKey(b.g)] = [])).push({ t: e.data.ts_ms, x: b.x, y: b.y }));
    }
    continue;
  }
  // a blocked page (trace.freezes) draws nothing; the frames that arrived meanwhile were moved to its end by the fixture
  if (!(trace.freezes || []).some((f) => t >= f[0] && t < f[1])) frame();
  nextDraw = t + G.FRAME_MS;
}

// ---- the measures
const MOVING_YD = 0.5, BACK_YD = 0.3;
function direction(tr, i, win) {
  let j = i - 1;
  while (j > 0 && tr[i - 1].t - tr[j - 1].t <= win) j -= 1;
  const dx = tr[i - 1].x - tr[j].x, dy = tr[i - 1].y - tr[j].y, l = Math.hypot(dx, dy);
  return l < MOVING_YD ? null : { dx: dx / l, dy: dy / l };
}
function causeOf(p, prev) {
  if (p.clockBack) return "render clock back";
  if (p.raw || prev.raw) return "drawn raw (no glide state)";
  if (p.n < prev.n || p.n <= 1) return "snap/reset";
  // a segment between the two feeds: the report's position against a pos frame's (the report is older, and stamped late)
  if (p.seg && p.seg[0].src !== p.seg[1].src) return "report sample";
  return "server back"; // between two samples of one feed: the unit itself went back (a real turn, a strafe)
}
const PAGE_CAUSES = ["render clock back", "drawn raw (no glide state)", "snap/reset", "report sample"];
function pageCaused(e) {
  return PAGE_CAUSES.some((c) => (e.causes[c] || 0) > BACK_YD / 2);
}
const result = { units: {}, causes: {}, spikes: [], serverBack: 0, serverBackMax: 0 };
Object.keys(tracks).forEach((k) => {
  const tr = tracks[k], episodes = [];
  let ep = null;
  for (let i = 2; i < tr.length; i++) {
    const d = direction(tr, i, 1000);
    const step = d ? (tr[i].x - tr[i - 1].x) * d.dx + (tr[i].y - tr[i - 1].y) * d.dy : 0;
    if (step < -1e-6) {
      if (!ep) { ep = { t: tr[i].t, size: 0, causes: {} }; episodes.push(ep); }
      ep.size += -step;
      const c = causeOf(tr[i], tr[i - 1]);
      ep.causes[c] = (ep.causes[c] || 0) + -step;
    } else ep = null;
    // a jump over 5 yd that comes back within 2 s: where did it land?
    const jump = Math.hypot(tr[i].x - tr[i - 1].x, tr[i].y - tr[i - 1].y);
    if (jump > 5) {
      for (let j = i + 1; j < tr.length && tr[j].t - tr[i].t <= 2000; j++) {
        if (Math.hypot(tr[j].x - tr[i - 1].x, tr[j].y - tr[i - 1].y) < jump / 2) {
          const other = Object.keys(tracks).filter((o) => o !== k && tracks[o].some((q) => q.t === tr[i].t && Math.hypot(q.x - tr[i].x, q.y - tr[i].y) < 1));
          result.spikes.push({ unit: k, t: Math.round(tr[i].t), yd: +jump.toFixed(1), onOther: other });
          break;
        }
      }
    }
  }
  const big = episodes.filter((e) => e.size > BACK_YD);
  const page = big.filter(pageCaused);
  big.forEach((e) => {
    const c = Object.keys(e.causes).sort((a, b) => e.causes[b] - e.causes[a])[0];
    if (reportOnly && process.argv.includes("--episodes")) console.log("episode", k, Math.round(e.t - trace.frames[0][0]), e.size.toFixed(2), c);
    const r = result.causes[c] || (result.causes[c] = { n: 0, max: 0, sum: 0 });
    r.n += 1; r.sum += e.size; r.max = Math.max(r.max, e.size);
  });
  result.units[k] = { back: big.length, max: big.reduce((m, e) => Math.max(m, e.size), 0), page: page.length };
});
// the server's own positions: does a bot's pos frame step back against its last second of pos frames?
Object.keys(inputs).forEach((k) => {
  const tr = inputs[k];
  for (let i = 2; i < tr.length; i++) {
    const d = direction(tr, i, 1000);
    if (!d) continue;
    const step = (tr[i].x - tr[i - 1].x) * d.dx + (tr[i].y - tr[i - 1].y) * d.dy;
    if (step < -BACK_YD) { result.serverBack += 1; result.serverBackMax = Math.max(result.serverBackMax, -step); }
  }
});

const bots = Object.keys(result.units).filter((k) => k.startsWith("b:"));
const botBack = bots.reduce((n, k) => n + result.units[k].back, 0);
const pageBack = Object.keys(result.units).reduce((n, k) => n + result.units[k].page, 0);
if (reportOnly) {
  Object.keys(result.units).sort().forEach((k) => console.log(k.padEnd(24), "back>0.3yd", String(result.units[k].back).padStart(3), "max", result.units[k].max.toFixed(2)));
  Object.keys(result.causes).forEach((c) => console.log("cause", c.padEnd(28), "n", result.causes[c].n, "max", result.causes[c].max.toFixed(2), "mean", (result.causes[c].sum / result.causes[c].n).toFixed(2)));
  console.log("jumps >5 yd that came back within 2 s:", JSON.stringify(result.spikes));
  console.log("server pos frames stepping back >0.3 yd:", result.serverBack, "max", result.serverBackMax.toFixed(2));
  console.log("bots: backward steps >0.3 yd:", botBack, "| all units, page-caused:", pageBack);
  process.exit(0);
}
assert.ok(bots.length >= 3 && tracks[bots[0]].length > 1000, "the replay drew the walking bots");
// Every backward step left is the server's own (the turn back at 32 s, a strafe): the page adds none, to a bot or a mob.
assert.strictEqual(pageBack, 0, "no unit is drawn stepping back more than 0.3 yd by the page: " + JSON.stringify(result.causes));
assert.ok(!result.causes["report sample"] && !result.causes["render clock back"], "no report or clock step at all: " + JSON.stringify(result.causes));
assert.deepStrictEqual(result.spikes, [], "no dot jumps away and back");
// the real turn back at 32 s is still drawn: the lead ends where the server says it stands
const lead = tracks[G.botKey(808)], tail = lead[lead.length - 1];
const lastIn = inputs[G.botKey(808)][inputs[G.botKey(808)].length - 1];
assert.ok(Math.hypot(tail.x - lastIn.x, tail.y - lastIn.y) < 0.5, "the lead is drawn where it stands after the turn back: " + JSON.stringify([tail, lastIn]));
console.log("glide replay ok: " + bots.length + " bots, " + Object.keys(result.units).length + " units, 0 backward steps the page made");
