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
["commanders.js", "maplayers.js", "unitinfo.js", "paths.js", "demo.js"].forEach((f) => vm.runInContext(fs.readFileSync(path.join(dir, f), "utf8"), ctx, { filename: f }));
vm.runInContext(
  [grab("esc"), grabConst("CMD_MAP_ICONS"), grabConst("CMD_OBJECT_ICON"), grab("cmdMapIcon"), grab("cmdLineStyle"), grabConst("cmdPt"), grabConst("CMD_TRAIL_MARKS"), grab("cmdClock"), grab("cmdTrailSvg"),
    grab("cmdMemberTrailsSvg"), grab("cmdRunStepsSvg"), grab("cmdStallsSvg"), grab("cmdObjectsSvg"), grab("cmdLineSvg"), grab("cmdCombatLinesSvg"), grab("cmdMapFacts")].join("\n") +
  "\nvar cmdLayers = MapLayers.defaults(); var cmdTrails = CmdPaths.newTrails();" +
  "\nvar cmdTracker = CmdView.newTracker(); var cmdEvents = UnitInfo.newEvents(); var cmdStalls = CmdPaths.newStalls();" +
  "\nvar state = { dungeons: { runs: [] } }; var ZONE_BOUNDS = null;" +
  "\nfunction ensureCmdPics() { return undefined; } function cmdZoneRect() { return null; } function cmdNowFor() { return 1000; }", ctx);

const f = (n) => n.toFixed(2);
const mk = () => ({ f, k: 0.5, units: {} });
const EVIL = '<img src=x onerror=alert(1)>"&';

// ---- line style: thickness and opacity from the panel
vm.runInContext('cmdLayers = MapLayers.setStyle(MapLayers.setStyle(cmdLayers, "leadpath", "th", 200), "leadpath", "op", 50);', ctx);
assert.strictEqual(vm.runInContext('cmdLineStyle("leadpath", 3, 0.8)', ctx), "stroke-width:6.00px;opacity:0.40");
assert.strictEqual(vm.runInContext('cmdLineStyle("grid", 3)', ctx), "stroke-width:3.00px;opacity:1.00", "a layer without a style is drawn as normal");

// ---- trails
ctx.pts = []; for (let i = 0; i < 30; i++) ctx.pts.push([i * 5, 0, 100 + i]);
const lead = vm.runInContext('(function(){ var c = ' + JSON.stringify({}) + '; return cmdTrailSvg(' + 'ctx0' + ', "leadpath", "trail", pts, 1, 3); })', Object.assign(ctx, { ctx0: mk() }));
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

// ---- combat lines: the three action-line drawings share one style key, actionlines
ctx.dc = mk();
vm.runInContext('cmdLayers = MapLayers.setStyle(MapLayers.setStyle(MapLayers.defaults(), "actionlines", "th", 200), "actionlines", "op", 50);', ctx);
const ln = vm.runInContext('cmdLineSvg(dc, "actionlines", "healline", {x: 1, y: 2}, {x: 3, y: 4}, 0.9)', ctx);
assert.ok(/<line class="healline" style="stroke-width:3\.20px;opacity:0\.45" x1="-2\.00" y1="-1\.00" x2="-4\.00" y2="-3\.00"\/>/.test(ln), ln);
ctx.clBots = [
  { name: "T", id: "1", alive: true, role: "tank", x: 0, y: 0, target: "9" },
  { name: "H", id: "2", alive: true, role: "healer", x: 1, y: 1, cast: "Heal", ctgt: "1" },
  { name: "M", id: "3", alive: true, x: 2, y: 2, cast: "Polymorph", ctgt: "9" },
];
ctx.clMobs = [{ id: "9", name: "Mob", x: 3, y: 3, reaction: "h", cc: "c" }];
ctx.dc = mk();
const cl = vm.runInContext("cmdCombatLinesSvg(dc, { bottarget: true, healline: true, ccline: true }, clBots, clMobs)", ctx);
assert.ok(/class="bottarget"/.test(cl) && /class="healline"/.test(cl) && /class="ccline"/.test(cl), cl);
assert.strictEqual((cl.match(/stroke-width:3\.00px;opacity:0\.45/g) || []).length, 1, "the bot-target line uses the actionlines style");
assert.strictEqual((cl.match(/stroke-width:3\.20px;opacity:0\.45/g) || []).length, 2, "heal and crowd-control lines too");
assert.strictEqual(vm.runInContext("cmdCombatLinesSvg(dc, {}, clBots, clMobs)", ctx), "", "no layer on, no lines");

// ---- cmdMapFacts: a Commanders card unions its run's DUNGEON_MOBS list (map + shared bot) with the feed
ctx.umRuns = [{ key: "70:1", map: 70, instance: 1, members: [{ name: "A", guid: 1 }], mobs_map: 70, mobs_total: 30,
  mobs: [{ id: "1", name: "Run Name", reaction: "h", x: 5, y: 5 }, { id: "2", name: "Extra", reaction: "h", x: 6, y: 6 },
    { id: "3", name: "Neutral", reaction: "n", x: 7, y: 7 }] }];
ctx.umBind = { commander: "C", lead: "A", bots: [{ name: "A", guid: 1, map: 70, x: 0, y: 0, role: "tank", id: "1", alive: true }],
  mobs_map: 70, mobs: [{ id: "1", name: "Feed Name", reaction: "h", x: 1, y: 1, victim: "1" }] };
vm.runInContext("state.dungeons.runs = umRuns;", ctx);
const um = vm.runInContext("cmdMapFacts(umBind)", ctx);
assert.strictEqual(um.mobs.map((m) => m.id).join(","), "1,2,3", "the union by guid");
assert.strictEqual(um.mobs[0].name, "Feed Name", "the commander feed wins the conflict");
assert.strictEqual(um.mobsTotal, 30);
assert.strictEqual(vm.runInContext("MapLayers.enemiesText(cmdMapFacts(umBind).mobs.length, cmdMapFacts(umBind).mobsTotal)", ctx), "3 of 30 enemies shown");
// no matching run: the feed only, no cap note
ctx.umOther = { commander: "C2", lead: "Z", bots: [{ name: "Z", guid: 50, map: 70, x: 0, y: 0, role: "tank", id: "50", alive: true }],
  mobs_map: 70, mobs: [{ id: "9", name: "Feed only", reaction: "h", x: 1, y: 1 }] };
const noRun = vm.runInContext("cmdMapFacts(umOther)", ctx);
assert.strictEqual(noRun.mobs.map((m) => m.id).join(","), "9", "no shared bot, no union");
assert.strictEqual(noRun.mobsTotal, 0);
assert.strictEqual(vm.runInContext("MapLayers.enemiesText(cmdMapFacts(umOther).mobs.length, cmdMapFacts(umOther).mobsTotal)", ctx), "",
  "without a run there is no total to compare: no N of M note");
// ---- the picture slots: the Map picture layer is about a PICTURE, not a list (an all-missing answer must grey it)
ctx.noPic = [{ kind: "worldmap", floor: "", label: "World map", available: false, reason: "no world map" },
  { kind: "minimap", floor: "", label: "Minimap", available: false, reason: "no minimap" },
  { kind: "navmesh", floor: "", label: "Navmesh", available: false, reason: "no navmesh" }];
ctx.onePic = [Object.assign({}, ctx.noPic[0], { available: true, url: "/api/v1/mapfiles/70.png", x: 0, y: 0, w: 10, h: 10 }),
  ctx.noPic[1], ctx.noPic[2]];
vm.runInContext("state.commanders = { realMaps: true }; ensureCmdPics = () => CmdView.pictureSlots(noPic);", ctx);
const noPic = vm.runInContext("cmdMapFacts(umOther)", ctx);
assert.strictEqual(noPic.ctx.picture, false, "three missing slots are no picture (a slots.length check would be wrong)");
assert.strictEqual(noPic.ctx.pictureWhy, "No picture of this map in the map folder");
vm.runInContext("ensureCmdPics = () => CmdView.pictureSlots(onePic);", ctx);
assert.strictEqual(vm.runInContext("cmdMapFacts(umOther).ctx.picture", ctx), true, "one available slot is a picture");
assert.strictEqual(vm.runInContext("cmdMapFacts(umOther).ctx.pictureWhy", ctx), "", "and needs no reason");
// the demo card gets its made-up slots only while the daemon has a map folder (its URLs are the daemon's files)
ctx.demoBind = { commander: "DEMO", demo: true, lead: "Simtank", bots: [{ name: "Simtank", guid: 9001, map: 70, x: 0, y: 0, role: "tank", alive: true }], orders: [] };
vm.runInContext("state.commanders = { realMaps: false };", ctx);
assert.strictEqual(vm.runInContext("cmdMapFacts(demoBind).ctx.picture", ctx), false, "no map folder, no demo picture");
vm.runInContext("state.commanders = { realMaps: true };", ctx);
assert.strictEqual(vm.runInContext("cmdMapFacts(demoBind).ctx.picture", ctx), true, "with a map folder the demo draws its own slots");
// ---- asking again: a stale list is refreshed behind what is on screen (no loading state over an existing list) and only an empty
// answer replaces it; a failed refresh never throws away a list
const picsFn = (() => { const i = src.indexOf("  function ensureCmdPics("); assert.ok(i > 0, "ensureCmdPics"); return src.slice(i, src.indexOf("\n  }\n", i)); })();
assert.ok(/CmdView\.pictureRefreshAfter\(have\)/.test(picsFn), "the retry interval comes from the slots (30 s none, 5 min partial, never complete)");
assert.ok(/if \(!cmdPics\.has\(mapId\)\) cmdPics\.set\(mapId, 'loading'\);/.test(picsFn), "only the first ask sets the loading state");
assert.ok(/if \(have === 'loading'\) return have;/.test(picsFn), "an answer in flight is not asked for again");
assert.ok(/if \(!Array\.isArray\(cmdPics\.get\(mapId\)\)\) cmdPics\.set\(mapId, null\);/.test(picsFn), "a failed refresh keeps a list on screen");

// ---- a path with a teleport is drawn in pieces, with a swirl where it broke and a skull where the bot died
{
  vm.runInContext('cmdLayers = MapLayers.defaults();', ctx);
  ctx.tpts = [[0, 0, 100], [10, 0, 102], [20, 0, 104], [800, 0, 106, "t"], [810, 0, 108], [820, 0, 110, "d"], [830, 0, 112]];
  ctx.dc = mk();
  const svg = vm.runInContext('cmdTrailSvg(dc, "leadpath", "trail", tpts, 1, 3)', ctx);
  const lines = (svg.match(/points="[^"]+"/g) || []).map((m) => m.slice(8, -1).split(" ").map((p) => Number(p.split(",")[1]) * -1)); // game x of each vertex
  assert.ok(lines.length >= 2, "pieces");
  lines.forEach((xs) => assert.ok(!(Math.min(...xs) <= 20 && Math.max(...xs) >= 800), "one polyline runs from before the teleport to after it: " + xs));
  assert.ok(lines.some((xs) => Math.max(...xs) >= 830) && lines.some((xs) => Math.min(...xs) === 0), "both sides are drawn");
  assert.strictEqual((svg.match(/ic-swirl/g) || []).length, 1, "one swirl where it broke");
  assert.strictEqual((svg.match(/ic-skull/g) || []).length, 1, "one skull where it died");
  assert.ok(/Teleported or summoned here/.test(svg) && /Died here/.test(svg), "with a hover text");
  ctx.tpts = [[0, 0, 1], [10, 0, 2], [20, 0, 3]];
  const plain = vm.runInContext('cmdTrailSvg(dc, "leadpath", "trail", tpts, 1, 3)', ctx);
  assert.ok(!/ic-swirl|ic-skull/.test(plain), "no marks on a plain walk");
}
// ---- the bot names a tight group can show: drawn through CmdView.labelFit, the hover box keeps every name --------------
const draw = src.slice(src.indexOf("  function drawCmdMap("), src.indexOf("\n  }\n", src.indexOf("  function drawCmdMap(")));
assert.ok(/CmdView\.labelFit\(labelCand\.map\(c => c\.item\), k\)/.test(draw), "drawCmdMap asks labelFit which names fit");
assert.ok(/const named = !!labelIds\[uid\];/.test(draw), "a bot's label is drawn only when its box was kept");
assert.ok(/units\[uid\] = \{ kind: 'b', obj: b \};/.test(draw), "every bot keeps its hover entry, named or not");
assert.ok(/const rank = isLead \|\| isFd \? 0 : CmdView\.roleKey\(b\) === 'tank' \? 1 : 2;/.test(draw),
  "the lead and the debugger's bot first, then the tanks, then the rest: a tight group keeps their names");

console.log("drawing ok");
