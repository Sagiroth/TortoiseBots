"use strict";
// Run by commandersjs_test.go under node: the SVG builders of the Commanders map drawing layers (in app.js) with the real helpers.
// Every string that comes from the game server (names, reasons) must leave them escaped; a missing field must not throw.
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
function grabConst(name) {
  const re = new RegExp(`^  const ${name} = `, "m"), m = re.exec(src);
  assert.ok(m, `${name} not found`);
  const end = src.indexOf("\n  };\n", m.index), line = src.indexOf(";\n", m.index);
  return src.slice(m.index, (end > 0 && end < line + 400 && src.slice(m.index, line).includes("{\n") ? end + 5 : line + 1));
}
const ctx = { console };
vm.createContext(ctx);
["commanders.js", "maplayers.js", "unitinfo.js", "paths.js"].forEach((f) => vm.runInContext(fs.readFileSync(path.join(dir, f), "utf8"), ctx, { filename: f }));
vm.runInContext(
  [grab("esc"), grabConst("CMD_MAP_ICONS"), grabConst("CMD_OBJECT_ICON"), grab("cmdMapIcon"), grab("cmdLineStyle"), grabConst("cmdPt"), grab("cmdTrailSvg"),
    grab("cmdMemberTrailsSvg"), grab("cmdRunStepsSvg"), grab("cmdStallsSvg"), grab("cmdObjectsSvg"), grab("cmdLineSvg")].join("\n") +
  "\nvar cmdLayers = MapLayers.defaults(); var cmdTrails = CmdPaths.newTrails();", ctx);

const f = (n) => n.toFixed(2);
const mk = () => ({ f, k: 0.5, units: {} });
const EVIL = '<img src=x onerror=alert(1)>"&';

// ---- line style: thickness and opacity from the panel
vm.runInContext('cmdLayers = MapLayers.setStyle(MapLayers.setStyle(cmdLayers, "trail", "th", 200), "trail", "op", 50);', ctx);
assert.strictEqual(vm.runInContext('cmdLineStyle("trail", 3, 0.8)', ctx), "stroke-width:6.00px;opacity:0.40");
assert.strictEqual(vm.runInContext('cmdLineStyle("grid", 3)', ctx), "stroke-width:3.00px;opacity:1.00", "a layer without a style is drawn as normal");

// ---- trails
ctx.pts = []; for (let i = 0; i < 30; i++) ctx.pts.push([i * 5, 0, 100 + i]);
const lead = vm.runInContext('(function(){ var c = ' + JSON.stringify({}) + '; return cmdTrailSvg(' + 'ctx0' + ', "trail", "trail", pts, 1, 3); })', Object.assign(ctx, { ctx0: mk() }));
const svg1 = lead();
assert.ok(/<polyline class="trail" style="stroke-width:6\.00px;opacity:[0-9.]+"/.test(svg1), svg1.slice(0, 200));
assert.ok((svg1.match(/<polyline/g) || []).length >= 2, "fading chunks");
// member trails, cut by the length setting
const bind = { commander: "C", bots: [{ guid: 1, lead: true, map: 1, x: 0, y: 0 }, { guid: 2, role: "healer", name: "H", map: 1, x: 0, y: 0 }] };
ctx.bind = bind;
vm.runInContext('for (var i = 0; i < 50; i++) { bind.bots[1].x = i * 4; CmdPaths.noteTrails(cmdTrails, bind, 100 + i); }', ctx);
vm.runInContext('cmdLayers = MapLayers.setTrailLen(cmdLayers, 10);', ctx);
ctx.dc = mk();
const mtShort = vm.runInContext('cmdMemberTrailsSvg(dc, bind, 1)', ctx);
assert.ok(/class="mtrail healer"/.test(mtShort) && !/class="mtrail lead"/.test(mtShort));
const pointsOf = (s) => (s.match(/points="([^"]+)"/g) || []).join(" ").split(/[ ,]+/).filter((x) => /\d/.test(x)).length / 2;
vm.runInContext('cmdLayers = MapLayers.setTrailLen(cmdLayers, 100);', ctx);
const mtLong = vm.runInContext('cmdMemberTrailsSvg(dc, bind, 1)', ctx);
assert.ok(pointsOf(mtShort) < pointsOf(mtLong), "the length slider shortens the drawn path");

// ---- run steps
const steps = ctx.CmdPaths.runSteps({
  step: 3, plan_steps: [
    { i: 1, k: "move", x: 0, y: 0 }, { i: 2, k: "pull", x: 5, y: 5, r: 15, sk: true }, { i: 3, k: "pull", x: 9, y: 9, r: 15 },
    { i: 4, k: "boss", x: 12, y: 12, n: EVIL, r: 30 }, { i: 5, k: "rest", x: 15, y: 15 }, { i: 6, k: "open", x: 18, y: 18 }, { i: 120, k: "pull", x: 20, y: 20 },
  ],
});
ctx.steps = steps; ctx.dc = mk(); ctx.leadBot = { x: 1, y: 1 };
const rs = vm.runInContext("cmdRunStepsSvg(dc, steps, leadBot)", ctx);
assert.ok(!rs.includes(EVIL) && !/<img/.test(rs), "a boss name from the server is escaped");
assert.ok(rs.includes("&lt;img"), "...and still shown");
assert.ok(/class="route done"/.test(rs) && /class="route ahead"/.test(rs) && /class="route toward"/.test(rs), "route lines");
assert.ok(/class="rstep next k-pull"/.test(rs) && /class="rstep skipped k-pull"/.test(rs) && /class="rstep todo k-boss"/.test(rs), "state classes");
assert.ok(/class="steparea"/.test(rs), "the next pull shows its radius");
assert.strictEqual((rs.match(/class="stepskip"/g) || []).length, 1, "a skipped pull is struck out");
assert.ok(/class="ic ic-crown crownmark"/.test(rs) && /ic-stop/.test(rs) && /ic-door/.test(rs), "boss crown, stop mark, door");
assert.ok(/class="stepmove"/.test(rs));
assert.deepStrictEqual(Object.keys(ctx.dc.units).sort(), ["s:1", "s:120", "s:2", "s:3", "s:4", "s:5", "s:6"], "every step can be hovered");
assert.strictEqual(vm.runInContext("cmdRunStepsSvg(dc, [], null)", ctx), "", "no steps, nothing drawn");
ctx.steps = steps.slice(2, 3); ctx.dc = mk();
assert.doesNotThrow(() => vm.runInContext("cmdRunStepsSvg(dc, steps, null)", ctx), "no lead is fine");

// ---- stalls
ctx.dc = mk();
ctx.stalls = [
  { id: "p:1:2", kind: "pause", map: 1, x: 3, y: 4, reason: EVIL, at: 1, live: true },
  { id: "o:2:move", kind: "refused", map: 1, x: 5, y: 6, verb: "move", reason: "R".repeat(100), at: 2, live: false },
];
const ss = vm.runInContext("cmdStallsSvg(dc, stalls)", ctx);
assert.ok(!/<img/.test(ss) && ss.includes("&lt;img"), "a reason from the server is escaped");
assert.ok(/class="stall live"/.test(ss) && /class="stall"/.test(ss));
assert.ok(!ss.includes("R".repeat(50)) && ss.includes("R".repeat(43) + "…"), "long reasons are clipped on the map");
assert.deepStrictEqual(Object.keys(ctx.dc.units).sort(), ["x:o:2:move", "x:p:1:2"]);
assert.strictEqual((ss.match(/class="stalltext"/g) || []).length, 2);

// ---- objects
ctx.dc = mk();
ctx.objs = [
  { id: "1", name: EVIL, type: "c", state: "u", x: 1, y: 2 }, { id: "2", name: "Door", type: "d", state: "a", x: 3, y: 4 },
  { id: "3", name: "Gong", type: "g", state: "r", x: 5, y: 6 }, { id: "4", name: "Lever", type: "b", state: "r", x: 7, y: 8 }, { id: "5", name: "?", type: "r", state: "r", x: 9, y: 9 },
];
const os = vm.runInContext("cmdObjectsSvg(dc, objs)", ctx);
assert.ok(!/<img/.test(os), "object names never reach the markup");
assert.ok(/ic-chest/.test(os) && /ic-door/.test(os) && /ic-gong/.test(os) && /ic-lever/.test(os) && /ic-spark/.test(os), "an icon per type");
assert.ok(/class="obj st-u"/.test(os) && /class="obj st-a"/.test(os) && /class="obj st-r"/.test(os), "state classes");
assert.strictEqual(Object.keys(ctx.dc.units).length, 5);

// ---- combat lines
ctx.dc = mk();
vm.runInContext('cmdLayers = MapLayers.setStyle(MapLayers.defaults(), "healline", "th", 300);', ctx);
const ln = vm.runInContext('cmdLineSvg(dc, "healline", "healline", {x: 1, y: 2}, {x: 3, y: 4}, 0.9)', ctx);
assert.ok(/<line class="healline" style="stroke-width:4\.80px;opacity:0\.90" x1="-2\.00" y1="-1\.00" x2="-4\.00" y2="-3\.00"\/>/.test(ln), ln);
console.log("drawing ok");
