"use strict";
// Run by commandersjs_test.go under node: the draw-time memo of web/static/js/planoverlay.js. The card's map draws at about
// 30 fps, so the stands, the strays and the view are built once per key and handed back; the memoized strays must still be the
// answer walking every mob of every later pack gave (the same walk this check keeps beside it).
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const P = require(path.resolve(process.argv[2]));

const plan = JSON.parse(fs.readFileSync(path.join(__dirname, "..", "plans", "testdata", "onyxias_lair.plan.json"), "utf8"));
const mc = JSON.parse(fs.readFileSync(path.join(__dirname, "..", "plans", "testdata", "molten_core.plan.json"), "utf8"));
const fights = JSON.parse(fs.readFileSync(path.join(__dirname, "testdata", "walk2-fights.json"), "utf8"));

// ---- the walk the memo replaced: every move step against every later pack, every mob of it ---------------------------
const list = (v) => (Array.isArray(v) ? v : []);
const num = (v) => (typeof v === "number" && isFinite(v) ? v : null);
const PACK = { pull: 1, boss: 1 };
const dist3 = (a, b) => {
  const dx = a.x - b.x, dy = a.y - b.y, dz = (a.z || 0) - (b.z || 0);
  return Math.sqrt(dx * dx + dy * dy + dz * dz);
};
function nextPack(steps, fromI) {
  for (let i = 0; i < steps.length; i++) {
    if (steps[i].i > fromI && PACK[steps[i].k]) return steps[i];
  }
  return null;
}
function walkStrays(plan, partyLevel) {
  const out = [];
  const steps = list(plan && plan.steps);
  const mobs = list(plan && plan.mobs);
  steps.forEach((s) => {
    if (!P.MOVE_KINDS[s.k]) return;
    const own = nextPack(steps, s.i);
    steps.forEach((p) => {
      if (!PACK[p.k] || p.i <= s.i) return;
      mobs.forEach((m) => {
        if (m.step !== p.i) return;
        const ring = P.ringYd(m, partyLevel);
        if (!(ring > 0)) return;
        const dist = dist3(s, m);
        if (dist >= ring) return;
        const wake = m.wake_yd > 0;
        if (own && p.i === own.i && !wake) return;
        out.push({ i: s.i, pack: p.i, guid: m.guid, name: m.n || "", dist, ring, wake });
      });
    });
  });
  out.sort((a, b) => a.i - b.i || a.pack - b.pack || a.guid - b.guid);
  return out;
}
function walkMargins(plan, partyLevel) {
  const out = {};
  const steps = list(plan && plan.steps);
  const mobs = list(plan && plan.mobs);
  steps.forEach((s) => {
    if (!s.stand) return;
    const pack = nextPack(steps, s.i);
    if (!pack) return;
    let best = null;
    mobs.forEach((m) => {
      if (m.step !== pack.i) return;
      const ring = P.ringYd(m, partyLevel);
      if (!(ring > 0)) return;
      const dist = dist3(s, m), margin = dist - ring;
      if (!best || margin < best.margin || (margin === best.margin && m.guid < best.guid)) {
        best = { dist, ring, margin, name: m.n || "", wake: m.wake_yd > 0 };
      }
    });
    if (best) out[s.i] = best;
  });
  return out;
}

// ---- the same answer on both real plans, at three party levels ------------------------------------------------------
[["onyxias_lair", plan], ["molten_core", mc]].forEach(([name, p]) => {
  [60, 45, 70].forEach((level) => {
    assert.deepStrictEqual(P.strays(p, level), walkStrays(p, level), `${name}: the strays match the walk (level ${level})`);
    assert.deepStrictEqual(P.standMargins(p, level), walkMargins(p, level), `${name}: the stand margins match the walk (level ${level})`);
  });
  assert.notStrictEqual(P.strays(p, 60), P.strays(p, 45), `${name}: another party level is another answer`);
  assert.strictEqual(P.strays(p, 60), P.strays(p, 60), `${name}: the same party level is kept`);
});

// ---- the view is kept per (plan, step, fight step, party level, numbering) -----------------------------------------
const viewOpts = { mapId: plan.map, cur: 20, fightStep: fights[0].step, partyLevel: 60, run: { steps: plan.steps_total } };
const v1 = P.view(plan, viewOpts);
assert.ok(v1 && v1.steps.length, "the view has steps");
assert.strictEqual(P.view(plan, viewOpts), v1, "the same key hands back the same view");
assert.deepStrictEqual(P.view(JSON.parse(JSON.stringify(plan)), viewOpts), v1, "another plan object with the same rows gives the same view");
assert.notStrictEqual(P.view(plan, Object.assign({}, viewOpts, { cur: 21 })), v1, "the next step is another view");
assert.notStrictEqual(P.view(plan, Object.assign({}, viewOpts, { partyLevel: 45 })), v1, "another party level is another view");
assert.strictEqual(P.view(plan, Object.assign({}, viewOpts, { cur: 21 })), P.view(plan, Object.assign({}, viewOpts, { cur: 21 })),
  "and that key is kept too");

// ---- 1000 steps, 1000 mobs: one build per key, then O(1) ------------------------------------------------------------
const STEP_N = 1000, MOB_N = 1000;
const bigSteps = [], bigMobs = [];
for (let i = 1; i <= STEP_N; i++) {
  bigSteps.push({ i, k: i % 10 === 0 ? "pull" : "move", x: i, y: i % 3, z: 0, r: i % 10 === 0 ? 9 : 0, n: "step " + i, stand: i % 10 === 1 });
}
for (let i = 0; i < MOB_N; i++) bigMobs.push({ step: (i % STEP_N) + 1, guid: i + 1, n: "mob " + i, x: i % 40, y: i % 9, z: 0, det: 20, lvl: 60 });
const big = { plan: "big", map: 249, steps: bigSteps, mobs: bigMobs, steps_total: STEP_N };
let reads = 0;
Object.defineProperty(big, "mobs", { get() { reads += 1; return bigMobs; } });
Object.defineProperty(big, "steps", { get() { reads += 1; return bigSteps; } });
const bigOpts = { mapId: 249, cur: 500, fightStep: 400, partyLevel: 60, run: { steps: STEP_N } };
const built = P.view(big, bigOpts);
assert.ok(built && built.steps.length === STEP_N && built.mobs.length === MOB_N, "the big plan's view is complete");
const afterBuild = reads;
assert.ok(afterBuild >= 1, "the first view reads the plan");
for (let i = 0; i < 5; i++) assert.strictEqual(P.view(big, bigOpts), built, "the repeated key hands back the same view");
assert.strictEqual(reads, afterBuild, "and does not read the plan again");
const t0 = process.hrtime.bigint();
for (let i = 0; i < 200; i++) P.view(big, bigOpts);
const ms = Number(process.hrtime.bigint() - t0) / 1e6;
assert.ok(ms < 50, "200 repeats of one key are O(1) after the first (" + ms.toFixed(1) + " ms)");
assert.strictEqual(reads, afterBuild, "the repeats read nothing");
P.view(big, Object.assign({}, bigOpts, { cur: 501 }));
assert.ok(reads > afterBuild, "a new key builds once");

console.log("plan overlay memo ok");
