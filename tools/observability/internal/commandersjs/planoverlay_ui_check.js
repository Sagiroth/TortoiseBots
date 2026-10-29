"use strict";
// Run by commandersjs_test.go under node: the plan layer as app.js draws it (cmdPlanSvg, fed with PA1's golden plan through
// PlanOverlay.view) and the wiring that feeds it (the plan fetch by name, the card's plan facts, the map's Layers button, the
// raid presets and the trail time cut). Every name that comes from the server leaves through esc; each of the three plan layers
// draws only its own part.
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");
const appPath = path.resolve(process.argv[2]), dir = path.dirname(appPath);
const src = fs.readFileSync(appPath, "utf8");
const indexHtml = fs.readFileSync(path.join(dir, "..", "..", "index.html"), "utf8");
const plan = JSON.parse(fs.readFileSync(path.join(__dirname, "..", "plans", "testdata", "onyxias_lair.plan.json"), "utf8"));

function grab(name) {
  const re = new RegExp(`^  function ${name}\\(`, "m"), m = re.exec(src);
  assert.ok(m, `${name} not found`);
  const end = src.indexOf("\n  }\n", m.index);
  return src.slice(m.index, end + 4);
}
function grabConst(name) {
  const re = new RegExp(`^  const ${name} = `, "m"), m = re.exec(src);
  assert.ok(m, `${name} not found`);
  const end = src.indexOf("\n  };\n", m.index), line = src.indexOf(";\n", m.index);
  return src.slice(m.index, (end > 0 && end < line + 400 && src.slice(m.index, line).includes("{\n") ? end + 5 : line + 1));
}

const ctx = { console };
vm.createContext(ctx);
["planoverlay.js", "paths.js", "commanders.js", "maplayers.js", "unitinfo.js"].forEach((f) => {
  vm.runInContext(fs.readFileSync(path.join(dir, f), "utf8"), ctx, { filename: f });
});
vm.runInContext([grab("esc"), grabConst("CMD_MAP_ICONS"), grab("cmdMapIcon"), grabConst("cmdPt"), grab("cmdPlanSvg")].join("\n"), ctx);

const f = (n) => n.toFixed(2);
const mk = () => ({ f, k: 0.5, units: {} });
ctx.mk = mk;

// ---- the three layers of the one panel: each part draws only with its own switch
ctx.plan = plan;
vm.runInContext("view = PlanOverlay.view(plan, { mapId: 249, cur: 20, fightStep: 22 });", ctx);
const both = vm.runInContext("cmdPlanSvg(mk(), view, { planroute: true, planstands: true })", ctx);
assert.ok(!both.includes("plan-ring"), "no rings without the rings layer");
const standsOnly = vm.runInContext("cmdPlanSvg(mk(), view, { planstands: true })", ctx);
assert.ok(!standsOnly.includes("plan-route"), "no route without the route layer");
assert.strictEqual((standsOnly.match(/class="plan-stand/g) || []).length, 6, "the six stands");
const ringsOnly = vm.runInContext("cmdPlanSvg(mk(), view, { planrings: true })", ctx);
assert.strictEqual((ringsOnly.match(/class="plan-ring/g) || []).length, 6, "six rings");
assert.ok(!ringsOnly.includes("plan-step"), "rings alone draw no step");

// ---- the whole overlay at step 20: rings, route, stands, the current step and the fight's pack
const full = vm.runInContext("cmdPlanSvg(mk(), view, { planroute: true, planstands: true, planrings: true })", ctx);
assert.strictEqual((full.match(/class="plan-ring/g) || []).length, 6);
assert.strictEqual((full.match(/class="plan-ring wake/g) || []).length, 1, "Onyxia's scripted wake ring");
assert.strictEqual((full.match(/class="plan-stand/g) || []).length, 6);
assert.ok(/<polyline class="plan-route done"/.test(full) && /<polyline class="plan-route ahead"/.test(full), "both route parts");
const curG = full.match(/<g class="plan-step[^"]*s-cur[^"]*" data-u="([^"]+)"/g) || [];
assert.strictEqual(curG.length, 1, "exactly one current step");
assert.ok(/data-u="p:20"/.test(curG[0]), curG[0]);
assert.ok(/ stand/.test(curG[0]), "step 20 is a stand");
const fightG = full.match(/<g class="plan-step[^"]*plan-fight[^"]*" data-u="([^"]+)"/g) || [];
assert.strictEqual(fightG.length, 1);
assert.ok(/data-u="p:23"/.test(fightG[0]), fightG[0]);
assert.strictEqual((full.match(/<g class="plan-step[^"]*s-done/g) || []).length, 19, "steps 1-19 are done");
assert.ok(!full.includes("plan-stray"), "the clean plan has no stray");
assert.strictEqual((full.match(/plan-num/g) || []).length, 6, "a number in each pack");
assert.ok(/plan-stand/.test(full) && /class="plan-move"/.test(full), "stands and plain moves");
assert.ok(full.includes("wakes at ~62 yd (scripted)"), "the wake ring says it is a scripted wake");

// ---- a name from the server is escaped (the ring's and the stand's title, the mob's hover entry)
ctx.evil = JSON.parse(JSON.stringify(plan));
ctx.evil.mobs.forEach((m) => { m.n = '<img src=x onerror=alert(1)>"&'; });
vm.runInContext("evilView = PlanOverlay.view(evil, { mapId: 249, cur: 20, partyLevel: 60 });", ctx);
const evilOut = vm.runInContext("cmdPlanSvg(mk(), evilView, { planroute: true, planstands: true, planrings: true })", ctx);
assert.ok(!/<img/.test(evilOut), "no markup from a mob name");
assert.ok(evilOut.includes("&lt;img"), "the name is still shown");

// ---- a stray move point is marked on its own step
ctx.strayPlan = JSON.parse(JSON.stringify(plan));
const s3 = ctx.strayPlan.steps.find((s) => s.i === 3);
s3.x = -120; s3.y = -130; s3.z = -50;
vm.runInContext("strayView = PlanOverlay.view(strayPlan, { mapId: 249, cur: 20, partyLevel: 60 });", ctx);
const strayOut = vm.runInContext("cmdPlanSvg(mk(), strayView, { planroute: true, planstands: true, planrings: true })", ctx);
const strayG = strayOut.match(/<g class="plan-step[^"]*plan-stray[^"]*" data-u="([^"]+)"/g) || [];
assert.strictEqual(strayG.length, 1, "one stray mark");
assert.ok(/data-u="p:3"/.test(strayG[0]), strayG[0]);
assert.ok(/inside the ring of/.test(strayOut), "the mark says whose ring it is");

// ---- patrol paths (R7b): only under their layer, closed, floor-filtered, every name escaped
const warders = JSON.parse(fs.readFileSync(path.join(__dirname, "..", "plans", "testdata", "patrols", "onyxia_warders.json"), "utf8"));
ctx.patrolPlan = JSON.parse(JSON.stringify(plan));
ctx.patrolPlan.mobs.forEach((m) => {
  const row = warders[String(m.guid)];
  if (!row) return;
  m.path = row.path.map((q) => q.map((v) => Math.round(v * 10) / 10));
  m.period_s = row.period_s;
  m.path_est = !!row.estimate;
});
vm.runInContext("patrolView = PlanOverlay.view(patrolPlan, { mapId: 249, cur: 20, partyLevel: 60 });", ctx);
const noPatrols = vm.runInContext("cmdPlanSvg(mk(), patrolView, { planroute: true, planstands: true, planrings: true })", ctx);
assert.ok(!noPatrols.includes("patrol-path"), "no path without the patrols layer");
const patrolsOut = vm.runInContext("cmdPlanSvg(mk(), patrolView, { patrols: true })", ctx);
const paths = patrolsOut.match(/<path class="patrol-path[^"]*" d="([^"]*)"/g) || [];
assert.strictEqual(paths.length, 4, "one loop per patrolling warder");
assert.strictEqual((patrolsOut.match(/class="patrol-path est"/g) || []).length, 4, "the recorded loops are estimates");
paths.forEach((el) => assert.ok(/ Z"$/.test(el), "a closed loop: " + el));
assert.ok(/Onyxian Warder: patrol, about 36.21 s per round, estimated/.test(patrolsOut), "the title gives the round");
assert.ok((patrolsOut.match(/patrol-tick/g) || []).length >= 4, "direction ticks along the loops");
// an ordinary mob draws nothing
const plain = patrolsOut.match(/<path class="patrol-path[^"]*" d="[^"]*"><title>([^<]*)<\/title>/g) || [];
assert.strictEqual(plain.length, 4);
// a mob name from the server is escaped in the title too
ctx.evilPatrol = JSON.parse(JSON.stringify(ctx.patrolPlan));
ctx.evilPatrol.mobs.forEach((m) => { if (Array.isArray(m.path)) m.n = '<img src=x onerror=alert(1)>"&'; });
vm.runInContext("evilPatrolView = PlanOverlay.view(evilPatrol, { mapId: 249, cur: 20, partyLevel: 60 });", ctx);
const evilPatrolOut = vm.runInContext("cmdPlanSvg(mk(), evilPatrolView, { patrols: true })", ctx);
assert.ok(!/<img/.test(evilPatrolOut), "no markup from a patrol's name");
assert.ok(evilPatrolOut.includes("&lt;img"), "the name is still shown");
// a multi-floor map: only a segment whose both ends are on the shown floor
ctx.floorPlan = JSON.parse(JSON.stringify(plan));
ctx.floorPlan.mobs.forEach((m) => {
  if (m.guid !== 52050) return;
  m.path = [[0, 0, -50], [10, 0, -50], [10, 10, -10], [0, 10, -10]];
  m.period_s = 30;
});
vm.runInContext("floorView = PlanOverlay.view(floorPlan, { mapId: 249, cur: 20, partyLevel: 60 });", ctx);
vm.runInContext("floorView.floors = { has: (z) => z === -50 };", ctx);
const floorOut = vm.runInContext("cmdPlanSvg(mk(), floorView, { patrols: true })", ctx);
const floorPaths = floorOut.match(/<path class="patrol-path[^"]*" d="([^"]*)"/g) || [];
assert.strictEqual(floorPaths.length, 1, "one segment of the loop is on the floor");
assert.ok(floorPaths[0].includes('d="M 0.00,0.00 L 0.00,-10.00"'), "only the two floor points are drawn: " + floorPaths[0]);
assert.ok(!floorPaths[0].includes(" Z"), "a run that a floor change cuts is not closed");

// ---- the hover entries: the existing step text, the plan's states mapped to the run's words
ctx.u = mk();
vm.runInContext("cmdPlanSvg(u, view, { planroute: true, planstands: true, planrings: true });", ctx);
assert.strictEqual(ctx.u.units["p:29"].kind, "s");
assert.strictEqual(ctx.u.units["p:29"].obj.state, "todo");
assert.strictEqual(ctx.u.units["p:20"].obj.state, "next");
assert.strictEqual(ctx.u.units["p:2"].obj.state, "cleared");
assert.ok(/^Step 20: /.test(ctx.CmdPaths.stepText(ctx.u.units["p:20"].obj)));

// ---- the wiring: the plan is fetched once per name, the card's facts feed availability, the plan is drawn under the bots
assert.ok(src.includes("'/api/v1/plans/' + encodeURIComponent("), "the plan URL is encoded");
assert.ok(src.includes("g += cmdPlanSvg(dc, pf.view, on)"), "drawCmdMap draws the plan");
assert.ok(src.includes("facts.ctx.planWhy = pf.why"), "the card's plan reason reaches the panel");
assert.ok(src.includes("facts.ctx.patrols = "), "the card says whether the plan's mobs have patrol paths");
const wanted = src.slice(src.indexOf("  function cmdPlanWanted()"), src.indexOf("\n  }\n", src.indexOf("  function cmdPlanWanted()")));
assert.ok(/cmdLayers\.patrols/.test(wanted), "only patrol paths on still build the plan overlay");
assert.ok(src.includes("pf.view && (on.planroute || on.planstands || on.planrings || on.patrols)"), "and it is drawn for them");
assert.ok(src.includes("(Array.isArray(p) ? p[2] : p && p.z)"), "the floor split sees the path points");
const overlaySrc = fs.readFileSync(path.join(dir, "planoverlay.js"), "utf8");
assert.ok(overlaySrc.includes("path: m.path") && overlaySrc.includes("period_s: num(m.period_s)"), "the view passes the path through");
assert.ok(src.includes("const pf = cmdPlanFacts(bind, facts.first)"));
assert.ok(src.includes("fetch('/api/v1/plans/' + encodeURIComponent(name), { credentials: 'same-origin' })"));
// the plan sits under the bots: inside drawCmdMap its call comes before the party's dots are built
const draw = src.slice(src.indexOf("  function drawCmdMap("), src.indexOf("\n  }\n", src.indexOf("  function drawCmdMap(")));
assert.ok(draw.indexOf("cmdPlanSvg(") > 0 && draw.indexOf("cmdPlanSvg(") < draw.indexOf("onMap.slice().sort("), "the plan is drawn under the bots");

// ---- 4b in app.js: the presets, the trail cut, the layout's lines and the Layers button
["MapLayers.applyPreset(", "MapLayers.presetOf(", 'data-act="layers"', "CmdPaths.cutByAge(", "CmdView.threatLines(shown, onMap, bind.layout)",
  "CmdPaths.layoutHealLines(", "MapLayers.setTrailSec(", "data-trailsec"].forEach((s) => assert.ok(src.includes(s), s));
assert.strictEqual((src.match(/setInterval\(/g) || []).length, 9, "no new setInterval but the live stream's fallback poll");
assert.ok(src.includes("getElementById('cmd-layers')"), "the Layers button opens the one panel");

// ---- index.html loads the overlay before app.js
const pi = indexHtml.indexOf('<script src="/static/js/planoverlay.js');
const ai = indexHtml.indexOf('<script src="/static/js/app.js');
assert.ok(pi > 0, "index.html loads planoverlay.js");
assert.ok(ai > 0 && pi < ai, "planoverlay.js loads before app.js");
assert.ok(indexHtml.includes('<script src="/static/js/fightdebug.js'));
assert.ok(indexHtml.indexOf('<script src="/static/js/fightdebug.js') < pi, "planoverlay.js comes after fightdebug.js");

console.log("planoverlay ui ok");
