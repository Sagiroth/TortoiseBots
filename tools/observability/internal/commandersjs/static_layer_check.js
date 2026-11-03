"use strict";
// Run by commandersjs_test.go under node: the Commander map's static layer. The grid, the zone marks and the whole plan (route,
// stands, rings, patrols) live in one <g class="static"> rebuilt only when its inputs change, so a glide frame redraws the units
// and nothing else: sixty frames at 60 fps rebuild it zero times, a zoom (or a pane, a layer switch, a new plan step, the floor)
// rebuilds it once. The plan's hover entries survive between builds.
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

const ctx = { console, WeakMap, Object, Array, Infinity };
vm.createContext(ctx);
vm.runInContext([grab("cmdStaticKey"), grab("cmdStaticLayer"), "var cmdViewIds = new WeakMap(); var cmdViewSeq = 0;", grab("cmdViewId")].join("\n"), ctx);
const run = (expr, vars) => { if (vars) Object.assign(ctx, vars); return vm.runInContext(expr, ctx); };

// The card's map group: the same two fields drawCmdMap touches.
const group = () => ({ innerHTML: undefined, _k: undefined, _units: undefined });

const VIEW_A = { steps: 1 }, VIEW_B = { steps: 2 };
const base = {
  box: [0, 0, 100, 80], step: 50, viewId: run("cmdViewId(v)", { v: VIEW_A }),
  first: { map: 209, zone: 1519 }, zones: true, flKey: "", zr: null,
  on: { grid: true, zonelabels: false, zone: true, planroute: true, planstands: true, planrings: true, patrols: false },
};
const key = (over) => run("cmdStaticKey(o)", { o: Object.assign({}, base, over || {}) });

// ---- sixty glide frames of the same view: one build, then nothing ------------------------------------------------
const g = group();
let builds = 0;
const build = (units) => { builds += 1; units["p:1"] = { kind: "s", obj: { i: 1 } }; return '<g class="plan">x</g>'; };
const units = {};
for (let i = 0; i < 60; i++) {
  run("cmdStaticLayer(node, k, build, u)", { node: g, k: key(), build, u: units });
  assert.strictEqual(Object.keys(units).length, 1, "frame " + i + ": the plan's hover entry is re-registered on every frame");
}
assert.strictEqual(builds, 1, "60 glide frames rebuild the static layer once, not 60 times");
assert.strictEqual(g.innerHTML, '<g class="plan">x</g>');
assert.strictEqual(run("cmdStaticLayer(node, k, build, {})", { node: g, k: key(), build }), false, "an unchanged key reports no rebuild");

// ---- a zoom (a new box), a pan, a layer switch, a new plan step, a new floor: one rebuild each --------------------
const zoomed = key({ box: [0, 0, 50, 40] });
assert.notStrictEqual(zoomed, key(), "a zoom changes the key");
assert.strictEqual(run("cmdStaticLayer(node, k, build, {})", { node: g, k: zoomed, build }), true, "a zoom rebuilds it once");
assert.strictEqual(builds, 2);
assert.strictEqual(run("cmdStaticLayer(node, k, build, {})", { node: g, k: zoomed, build }), false, "the next frame of the zoomed view does not");
assert.strictEqual(builds, 2);
assert.notStrictEqual(key({ box: [5, 0, 50, 40] }), zoomed, "a pan changes the key");
assert.notStrictEqual(key({ on: Object.assign({}, base.on, { grid: false }) }), key(), "a layer toggle changes the key");
assert.notStrictEqual(key({ viewId: run("cmdViewId(v)", { v: VIEW_B }) }), key(), "a new plan step (a new view) changes the key");
assert.notStrictEqual(key({ flKey: "2" }), key(), "the floor changes the key");
assert.notStrictEqual(key({ first: { map: 209, zone: 999 } }), key(), "the zone under the party changes the key");
assert.notStrictEqual(key({ zr: { x: 1, y: 2, w: 3, h: 4, name: "Thousand Needles" } }), key(), "the zone rectangle changes the key");
assert.notStrictEqual(key({ zones: false }), key(), "the zones data arriving changes the key");
assert.strictEqual(key({ box: [0, 0, 100, 80.001] }), key(), "sub-pixel jitter of the box does not rebuild a 1900-node plan");

// ---- the same view object keeps its id (PlanOverlay.view memoizes it) ---------------------------------------------
assert.strictEqual(run("cmdViewId(v)", { v: VIEW_A }), run("cmdViewId(v)", { v: VIEW_A }), "one memoized view, one id");
assert.strictEqual(run("cmdViewId(null)"), 0);

// ---- drawCmdMap wires it: the group exists in the card's SVG and the draw uses the key ------------------------------
assert.ok(src.includes('<g class="pic"></g><g class="static"></g><g class="dyn"></g>'), "the card's map has a static group between the picture and the units");
assert.ok(/cmdStaticLayer\(svg\.querySelector\('g\.static'\), staticKey, su =>/.test(src), "drawCmdMap builds that group through cmdStaticLayer");
assert.ok(/const staticKey = cmdStaticKey\(\{ box, step, on, flKey, viewId: cmdViewId\(pf\.view\), first, zones: !!ZONE_BOUNDS, zr \}\);/.test(src),
  "the key comes from the box, the layer switches, the plan view and the zones");
assert.ok(!/if \(pf\.view && \(on\.planroute \|\| on\.planstands \|\| on\.planrings \|\| on\.patrols\)\) g \+= cmdPlanSvg/.test(src),
  "the plan is no longer drawn into the per-frame g.dyn");
assert.ok(!/g \+= `<line class="grid"/.test(src), "the grid is no longer drawn into g.dyn");

console.log("static layer ok");
