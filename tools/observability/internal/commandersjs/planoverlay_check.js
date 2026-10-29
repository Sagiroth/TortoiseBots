"use strict";
// Run by commandersjs_test.go under node: the Commanders map's plan overlay (web/static/js/planoverlay.js), fed with the
// daemon's plan (PA1's golden, GET /api/v1/plans/{plan}) and walk 2's fights. The second plan is Molten Core's real rows:
// several legs and several bosses, no spawns in the prep, so the case proves the overlay does not assume one leg or one boss.
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const P = require(path.resolve(process.argv[2]));

const plan = JSON.parse(fs.readFileSync(path.join(__dirname, "..", "plans", "testdata", "onyxias_lair.plan.json"), "utf8"));
const mc = JSON.parse(fs.readFileSync(path.join(__dirname, "..", "plans", "testdata", "molten_core.plan.json"), "utf8"));
const fights = JSON.parse(fs.readFileSync(path.join(__dirname, "testdata", "walk2-fights.json"), "utf8"));
const step = (i) => plan.steps.find((s) => s.i === i);
const copy = () => JSON.parse(JSON.stringify(plan));

// ---- no DOM, no fetch: the overlay is logic the page and the check both run
const src = fs.readFileSync(path.resolve(process.argv[2]), "utf8");
assert.ok(!/\bdocument\b/.test(src), "planoverlay.js never touches the document");
assert.ok(!/\bfetch\(/.test(src), "planoverlay.js fetches nothing");

// ---- aggro radius (decision 6 / 1.1): the core's GetAttackDistance, the level difference clamped at -25, floored at min(det, 5)
assert.strictEqual(P.AGGRO_MAX_LEVEL_DIFF, 25);
assert.strictEqual(P.AGGRO_MIN_REACH_YD, 5);
assert.strictEqual(P.PARTY_LEVEL_DEFAULT, 60);
assert.strictEqual(P.aggroYd({ det: 20, lvl: 63 }, 60), 23);
assert.strictEqual(P.aggroYd({ det: 20, lvl: 63 }, 35), 45);
assert.strictEqual(P.aggroYd({ det: 20, lvl: 40 }, 70), 5);
assert.strictEqual(P.aggroYd({ det: 0.5, lvl: 60 }, 60), 0);
assert.strictEqual(P.aggroYd({ det: 20, lvl: 60, noaggro: true }, 60), 0);
assert.strictEqual(P.aggroYd({ det: 20, lvl: 63 }, 0), 23);
assert.strictEqual(P.aggroYd({ det: 20, lvl: 63 }), 23, "no party level: the default");
assert.strictEqual(P.aggroYd({ det: 8, lvl: 63 }, 80), 5, "a low detection range floors at itself (5, its own det > 5)");

// ---- scripted wakes come from the plan (wake_yd), never from a boss id in this file
assert.strictEqual(P.WAKE, undefined, "no boss table in the overlay");
assert.strictEqual(P.ringYd({ e: 10184, wake_yd: 62, det: 20, lvl: 63 }, 60), 62);
assert.strictEqual(P.ringYd({ e: 12129, det: 20, lvl: 63 }, 60), 23);
assert.strictEqual(P.ringYd({ e: 10184, wake_yd: 0, det: 20, lvl: 63 }, 60), 23, "wake_yd 0 is no wake");
assert.strictEqual(P.ringYd({ det: 20, lvl: 60, noaggro: true }, 60), 0);
assert.strictEqual(P.wakeText(62), "wakes at ~62 yd (scripted)");

// ---- party level: the lowest level a binding reports, else 60
assert.strictEqual(P.partyLevel({ bots: [{ level: 60 }, { level: 58 }, { level: 0 }] }), 58);
assert.strictEqual(P.partyLevel({ bots: [] }), 60);
assert.strictEqual(P.partyLevel({}), 60);
assert.strictEqual(P.partyLevel(null), 60);

// ---- numbering: the plan the server loaded is the one the run numbers
assert.strictEqual(P.numberingOk(plan, null), true);
assert.strictEqual(P.numberingOk(plan, {}), true);
assert.strictEqual(P.numberingOk(plan, { steps: 29 }), true);
assert.strictEqual(P.numberingOk(plan, { steps: 30 }), false);

// ---- step states
const st = P.states(plan, 20);
assert.strictEqual(plan.steps.length, 29);
plan.steps.forEach((s) => assert.strictEqual(st[s.i], s.i < 20 ? "done" : s.i === 20 ? "cur" : "todo", "step " + s.i));
const none = P.states(plan, 0);
plan.steps.forEach((s) => assert.strictEqual(none[s.i], "todo", "cur 0: step " + s.i));

// ---- the fight's pack: the first pull/boss step at or after the fight's step that meets its engaged entries (or its boss)
assert.deepStrictEqual(fights.map((f) => P.fightPack(plan, f)), [25, 23, 21, 21, 2]);
assert.strictEqual(P.fightPack(plan, { step: 28, boss: 10184, engaged: [] }), 29);
assert.strictEqual(P.fightPack(plan, { step: 30, engaged: [{ e: 12129 }] }), 0);
assert.strictEqual(P.fightPack(plan, { step: 22 }), 23, "only the step is known: the next pack");
assert.strictEqual(P.fightPack(plan, {}), 2, "no step: the first pack");
assert.strictEqual(P.fightPack(null, { step: 1 }), 0);

// ---- stands: their next pack's nearest mob by 3D distance minus its ring
const margins = P.standMargins(plan, 60);
assert.deepStrictEqual(Object.keys(margins).map(Number).sort((a, b) => a - b), [1, 16, 20, 22, 24, 28]);
assert.strictEqual(margins[28].dist.toFixed(2), "62.72");
assert.strictEqual(margins[28].margin.toFixed(1), "0.7");
assert.strictEqual(margins[28].wake, true);
assert.strictEqual(margins[28].name, "Onyxia");
assert.strictEqual(margins[22].dist.toFixed(2), "20.22");
assert.strictEqual(margins[22].margin.toFixed(1), "-2.8");
assert.strictEqual(margins[22].wake, false);
assert.strictEqual(margins[1].name, "Onyxian Warder");
assert.strictEqual(P.standMargins(mc, 60) && Object.keys(P.standMargins(mc, 60)).length, 0, "no spawns: no margins");

// ---- strays (decision 7): a move step inside a later pack's ring, or inside any wake ring
assert.deepStrictEqual(P.strays(plan, 60), []);
let c = copy(); c.steps.find((s) => s.i === 28).x = -58; c.steps.find((s) => s.i === 28).y = -211.5; c.steps.find((s) => s.i === 28).z = -84.54;
let s = P.strays(c, 60);
assert.strictEqual(s.length, 1);
assert.deepStrictEqual([s[0].i, s[0].pack, s[0].guid, s[0].wake], [28, 29, 47572, true]);
assert.strictEqual(s[0].dist.toFixed(2), "58.72");
c = copy(); c.steps.find((x) => x.i === 3).x = -120; c.steps.find((x) => x.i === 3).y = -130; c.steps.find((x) => x.i === 3).z = -50;
s = P.strays(c, 60);
assert.strictEqual(s.length, 1);
assert.deepStrictEqual([s[0].i, s[0].pack, s[0].wake], [3, 21, false]);
assert.strictEqual(s[0].dist.toFixed(2), "13.70");
assert.strictEqual(s[0].name, "Onyxian Warder");
c = copy(); c.steps.find((x) => x.i === 19).x = -120; c.steps.find((x) => x.i === 19).y = -130; c.steps.find((x) => x.i === 19).z = -50;
assert.deepStrictEqual(P.strays(c, 60), [], "its own next pack's ring is the pull spot, not a stray");
// the same point on a pull step is never a stray
c = copy(); c.steps.find((x) => x.i === 21).x = -120; c.steps.find((x) => x.i === 21).y = -130; c.steps.find((x) => x.i === 21).z = -50;
assert.deepStrictEqual(P.strays(c, 60), []);

// ---- patrol paths (R7b): the ring stays at the spawn, the stray and stand distances measure the whole closed loop
// The four warders' recorded loops (the D1/trash-check data R7a attaches), rounded to 0.1 yd as the plan JSON carries them.
const warders = JSON.parse(fs.readFileSync(path.join(__dirname, "..", "plans", "testdata", "patrols", "onyxia_warders.json"), "utf8"));
const withWarders = () => {
  const p = copy();
  p.mobs.forEach((m) => {
    const row = warders[String(m.guid)];
    if (!row) return;
    m.path = row.path.map((q) => q.map((v) => Math.round(v * 10) / 10));
    m.period_s = row.period_s;
    m.path_est = !!row.estimate;
  });
  return p;
};
// the spawn distance alone (the no-path rule) for reference: 52050's spawn is 30.8 yd from stand 1
const wp = withWarders();
assert.strictEqual(wp.mobs.filter((m) => Array.isArray(m.path) && m.path.length >= 2).length, 4, "four warders patrol");
// Pinned from a separate Python reference of the rule over this plan + fixture (segment distance, closed loop, rings as above);
// with the loops attached the clean route still strays nowhere.
assert.deepStrictEqual(P.strays(wp, 60), []);
const wm = P.standMargins(wp, 60);
assert.strictEqual(wm[1].dist.toFixed(2), "7.19", "stand 1 measures to the warder's path, not its spawn");
assert.strictEqual(wm[1].margin.toFixed(1), "-15.8");
assert.strictEqual(wm[20].dist.toFixed(2), "3.41");
assert.strictEqual(wm[20].margin.toFixed(1), "-19.6");
assert.strictEqual(wm[24].dist.toFixed(2), "20.09", "52053 has no path: its spawn distance stands");
assert.strictEqual(wm[28].dist.toFixed(2), "62.72", "Onyxia has no path: unchanged");
const wv = P.view(wp, { mapId: 249, cur: 20, partyLevel: 60 });
assert.deepStrictEqual(wv.mobs.find((m) => m.guid === 52050).path[0], [52.3, -105.3, -24.8], "the view carries the path");
assert.strictEqual(wv.mobs.find((m) => m.guid === 52050).period_s, 36.21);
assert.strictEqual(wv.mobs.find((m) => m.guid === 52050).path_est, true);
assert.strictEqual(wv.mobs.find((m) => m.guid === 52053).path, undefined, "no path, nothing carried");
assert.strictEqual(wv.rings.find((r) => r.guid === 52050).r, 23, "the ring stays at the spawn");

// a move point 25 yd from the spawn but 5 yd from the patrol's path (ring 20)
function pathPlan(extra) {
  const p = copy();
  p.mobs.push(Object.assign({ step: 21, guid: 90001, e: 12129, n: "Path Warder", x: -70, y: -130, z: -50, lvl: 63, det: 20,
    wake_yd: 20, path: [[-100, -130, -50], [-90, -130, -50]], period_s: 30 }, extra || {}));
  const s3 = p.steps.find((s) => s.i === 3);
  s3.x = -95; s3.y = -125; s3.z = -50;
  return p;
}
let pc = pathPlan();
assert.strictEqual(P.distToMob({ x: -95, y: -125, z: -50 }, pc.mobs[pc.mobs.length - 1]).toFixed(2), "5.00");
let pst = P.strays(pc, 60).filter((x) => x.guid === 90001 && x.i === 3);
assert.strictEqual(pst.length, 1, "inside the ring of the path though 25 yd from the spawn");
assert.strictEqual(pst[0].dist.toFixed(2), "5.00");
assert.strictEqual(pst[0].pack, 21);
assert.deepStrictEqual(P.strays(pathPlan({ path: undefined }), 60).filter((x) => x.guid === 90001 && x.i === 3), [],
  "without a path the spawn distance is 25 yd, outside the ring");

// the closing segment (last point back to the first) counts: 10 yd from the two open segments, 0 from the closing one
const loop = copy();
loop.mobs.push({ step: 21, guid: 90002, e: 12129, n: "Loop", x: -60, y: -100, z: -50, lvl: 63, det: 20, wake_yd: 5,
  path: [[-100, -130, -50], [-80, -130, -50], [-80, -110, -50]], period_s: 30 });
const l3 = loop.steps.find((s) => s.i === 3);
l3.x = -90; l3.y = -120; l3.z = -50;
assert.deepStrictEqual(P.strays(loop, 60).filter((x) => x.guid === 90002).map((x) => [x.guid, x.dist.toFixed(2)]), [[90002, "0.00"]]);

// 3D: a point 20 yd above the path is 20 yd away, never 0
const above = pathPlan();
above.mobs[above.mobs.length - 1].path = [[-100, -130, -50], [-90, -130, -50]];
const a3 = above.steps.find((s) => s.i === 3);
a3.x = -95; a3.y = -130; a3.z = -30;
const am = above.mobs[above.mobs.length - 1];
assert.strictEqual(P.distToMob({ x: -95, y: -130, z: -30 }, am).toFixed(2), "20.00");
assert.deepStrictEqual(P.strays(above, 60).filter((x) => x.guid === 90001 && x.i === 3), [], "20 yd is not inside a 20 yd ring");
// a spawn-only mob still measures 3D to the spawn
assert.strictEqual(P.distToMob({ x: 0, y: 0, z: 0 }, { x: 3, y: 4, z: 12 }).toFixed(2), "13.00");
// another raid's plan is untouched: no patrols, no path keys in its view, no strays
assert.deepStrictEqual(P.strays(mc, 60), []);
const mcv = P.view(mc, { mapId: 409, cur: 40, partyLevel: 60 });
assert.deepStrictEqual(mcv.strays, []);
assert.strictEqual(mcv.mobs.length, 0);

// ---- view: the whole overlay for one map and one current step
let v = P.view(plan, { mapId: 249, cur: 20, fightStep: 22, partyLevel: 60 });
assert.ok(v);
assert.strictEqual(v.numbering, true);
assert.strictEqual(v.steps.length, 29);
assert.strictEqual(v.steps.filter((x) => x.stand).length, 6);
assert.strictEqual(v.steps.filter((x) => x.pack).length, 6);
assert.deepStrictEqual(v.steps.filter((x) => x.fight).map((x) => x.i), [23]);
assert.deepStrictEqual(v.steps.filter((x) => x.stray).map((x) => x.i), []);
assert.strictEqual(v.steps[19].state, "cur");
assert.strictEqual(v.steps[19].i, 20);
assert.strictEqual(v.steps[19].margin.dist.toFixed(2), "20.31");
assert.strictEqual(v.steps[27].margin.wake, true);
assert.strictEqual(v.steps[0].margin.dist.toFixed(2), "15.06");
assert.deepStrictEqual(Object.keys(v.steps[0]).sort(), ["c", "fight", "i", "k", "margin", "n", "pack", "r", "stand", "state", "stray", "x", "y", "z"].sort());
assert.strictEqual(v.rings.length, 6);
assert.strictEqual(v.rings.filter((x) => x.wake).length, 1);
const wakeRing = v.rings.find((x) => x.wake);
assert.strictEqual(wakeRing.r, 62);
assert.strictEqual(wakeRing.step, 29);
assert.strictEqual(wakeRing.name, "Onyxia");
assert.strictEqual(wakeRing.state, "todo");
v.rings.filter((x) => !x.wake).forEach((x) => assert.strictEqual(x.r, 23));
assert.strictEqual(v.rings.filter((x) => x.patrol).length, 4);
assert.strictEqual(v.mobs.length, 6);
assert.deepStrictEqual(v.mobs.filter((x) => x.boss).map((x) => x.guid), [47572]);
assert.strictEqual(v.mobs[0].state, "done");
// floors (A1): every drawn point keeps its z so the page can draw only the floor the map shows
v.steps.forEach((x) => assert.strictEqual(typeof x.z, "number", "step " + x.i + " has z"));
v.rings.forEach((x) => assert.strictEqual(typeof x.z, "number"));
v.mobs.forEach((x) => assert.strictEqual(typeof x.z, "number"));
// route: the done segment ends on the current step, the ahead segment starts on it
assert.strictEqual(v.route.length, 2);
assert.strictEqual(v.route[0].done, true);
assert.strictEqual(v.route[1].done, false);
const p20 = step(20);
assert.deepStrictEqual(v.route[0].points[v.route[0].points.length - 1], { x: p20.x, y: p20.y });
assert.deepStrictEqual(v.route[1].points[0], { x: p20.x, y: p20.y });
assert.strictEqual(v.route[0].points.length, 20);
assert.strictEqual(v.route[1].points.length, 10);

assert.strictEqual(P.view(plan, { mapId: 409 }), null, "another map: no plan");
assert.strictEqual(P.view(null, { mapId: 249 }), null);
assert.strictEqual(P.view(plan, { mapId: 249, cur: 0 }).route.length, 1, "cur 0: one ahead segment");
assert.strictEqual(P.view(plan, { mapId: 249, cur: 0 }).steps[0].state, "todo");
v = P.view(plan, { mapId: 249, cur: 20, partyLevel: 60, run: { steps: 30 } });
assert.strictEqual(v.numbering, false);
v.steps.forEach((x) => assert.strictEqual(x.state, "todo"));
assert.strictEqual(v.route.length, 1);
v = P.view(plan, { mapId: 249, cur: 20, partyLevel: 60, run: { steps: 29 } });
assert.strictEqual(v.numbering, true);
assert.strictEqual(v.steps[19].state, "cur");

// ---- a raid that is not Onyxia: several legs, several bosses, no spawns (Molten Core's real plan rows)
assert.strictEqual(mc.plan, "molten_core");
assert.strictEqual(mc.map, 409);
assert.strictEqual(mc.legs.length, 2);
assert.deepStrictEqual(mc.steps.filter((x) => x.k === "boss").map((x) => x.i), [38, 44]);
v = P.view(mc, { mapId: 409, cur: 40, partyLevel: 60 });
assert.ok(v);
assert.strictEqual(v.numbering, true);
assert.strictEqual(v.steps.length, 45);
assert.strictEqual(v.steps.filter((x) => x.stand).length, 20);
assert.strictEqual(v.steps.filter((x) => x.pack).length, 20);
assert.strictEqual(v.steps[39].state, "cur", "the second leg's own step 2");
assert.strictEqual(v.steps[38].state, "done");
assert.strictEqual(v.steps[40].state, "todo");
assert.strictEqual(v.rings.length, 0);
assert.strictEqual(v.mobs.length, 0);
assert.deepStrictEqual(v.strays, []);
assert.strictEqual(v.route.length, 2);
assert.strictEqual(v.route[0].points.length, 40);
assert.strictEqual(v.route[1].points.length, 6);
assert.strictEqual(P.view(mc, { mapId: 249 }), null);
assert.strictEqual(P.fightPack(mc, { step: 41, boss: 11982 }), 44);
assert.strictEqual(P.fightPack(mc, { step: 39, engaged: [{ e: 11671 }] }), 40);
assert.strictEqual(P.fightPack(mc, { step: 39 }), 40);
assert.strictEqual(P.fightPack(mc, { step: 45, engaged: [{ e: 11671 }] }), 0);

console.log("planoverlay.js ok");
