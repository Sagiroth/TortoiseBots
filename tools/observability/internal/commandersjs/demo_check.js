"use strict";
// Run by commandersjs_test.go under node: the Commanders tab demo (demo.js) must give every drawing layer something to show. The
// simulated group is fed through the same helpers app.js uses for a real report (trails, events, stall spots, the SVG builders), and
// each layer of maplayers.js must be non-empty at some moment of the loop. A new layer fails here until the demo covers it.
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");
const demoPath = path.resolve(process.argv[2]), dir = path.dirname(demoPath);
const src = fs.readFileSync(path.join(dir, "app.js"), "utf8");

function grab(name) {
  const m = new RegExp(`^  function ${name}\\(`, "m").exec(src);
  assert.ok(m, `${name} not found in app.js`);
  return src.slice(m.index, src.indexOf("\n  }\n", m.index) + 4);
}
function grabConst(name) {
  const m = new RegExp(`^  const ${name} = `, "m").exec(src);
  assert.ok(m, `${name} not found in app.js`);
  const end = src.indexOf("\n  };\n", m.index), line = src.indexOf(";\n", m.index);
  return src.slice(m.index, end > 0 && end < line + 400 && src.slice(m.index, line).includes("{\n") ? end + 5 : line + 1);
}
const ctx = { console };
vm.createContext(ctx);
["commanders.js", "maplayers.js", "unitinfo.js", "paths.js", "demo.js"].forEach((f) => vm.runInContext(fs.readFileSync(path.join(dir, f), "utf8"), ctx, { filename: f }));
vm.runInContext(
  [grab("esc"), grabConst("CMD_MAP_ICONS"), grabConst("CMD_OBJECT_ICON"), grab("cmdMapIcon"), grab("cmdLineStyle"), grabConst("cmdPt"), grab("cmdTrailSvg"),
    grab("cmdMemberTrailsSvg"), grab("cmdRunStepsSvg"), grab("cmdStallsSvg"), grab("cmdObjectsSvg"), grab("cmdLineSvg"), grab("cmdNotePaths")].join("\n") +
  "\nvar cmdLayers = MapLayers.defaults(); var cmdTrails = CmdPaths.newTrails(); var cmdStalls = CmdPaths.newStalls(); var cmdTracker = CmdView.newTracker(); var cmdEvents = UnitInfo.newEvents();" +
  "\ncmdLayers.trailLen = 240;", ctx);
const same = (a, b, m) => assert.strictEqual(JSON.stringify(a), JSON.stringify(b), m);
const D = ctx.CmdDemo, V = ctx.CmdView, U = ctx.UnitInfo, P = ctx.CmdPaths, L = ctx.MapLayers;
const f = (n) => n.toFixed(2);
const run = (code) => vm.runInContext(code, ctx);

// the shape of the report: what the dashboard's own types promise
const b0 = D.frame(1000, 1000);
assert.strictEqual(b0.demo, true); assert.strictEqual(b0.bots.length, 5); assert.strictEqual(b0.mobs_map, 70);
same(b0.bots.map((b) => b.role), ["tank", "healer", "dps", "dps", "dps"]);
same(b0.bots.map((b) => b.class), ["warrior", "priest", "mage", "hunter", "warlock"]);
assert.strictEqual(b0.bots.filter((b) => b.lead).length, 1);
assert.ok(/simulated/i.test(D.COMMANDER), "labelled as simulated");
same(D.frame(1000 + D.LOOP, 1000).bots.map((b) => b.x), b0.bots.map((b) => b.x), "the loop repeats exactly");
D.frame(5, 1000); // a clock before the start is fine

// ---- play two loops, 2 s apart, through the page's own bookkeeping; collect what each layer would draw
const seen = {}, mark = (k, n) => { if (n > 0) seen[k] = (seen[k] || 0) + 1; };
const events = new Set();
for (let sec = 1000; sec <= 1000 + 2 * D.LOOP; sec += 2) {
  const bind = D.frame(sec, 1000);
  ctx.B = bind; ctx.S = sec;
  run("CmdView.note(cmdTracker, B, S); UnitInfo.noteEvents(cmdEvents, B, S); cmdNotePaths(B, S);");
  const lead = V.leadBot(bind), first = lead, onMap = bind.bots.filter((b) => b.map === first.map), mapId = first.map;
  const mobs = V.mobsOf(bind, mapId), trail = ctx.cmdTracker.trail[bind.commander].pts;
  ctx.mobs = mobs; ctx.onMap = onMap; ctx.bind = bind; ctx.trail = trail; ctx.lead = lead;
  const dc = () => ({ f, k: 0.5, units: {} });
  const cls = (c) => mobs.filter((m) => V.mobClass(m) === c).length;
  const marks = U.targetMarks(mobs, onMap);
  const steps = P.runSteps(bind.run), stalls = P.stallsOf(ctx.cmdStalls, bind.commander, mapId);
  mark("bg", 1); mark("grid", 1); mark("scale", 1);
  mark("bots", onMap.length); mark("botnames", onMap.length); mark("lead", lead ? 1 : 0);
  mark("trail", trail.length >= 2 ? run("cmdTrailSvg({f: " + f.toString() + ", k: 0.5, units: {}}, 'trail', 'trail', trail, 1, 3).length") : 0);
  ctx.dcx = dc();
  mark("membertrails", run("cmdMemberTrailsSvg(dcx, bind, 70)").length);
  mark("trailtime", V.trailStamps(trail, 1, 70, 8).length);
  mark("heading", V.heading(trail, sec) ? 1 : 0);
  mark("healring", onMap.filter((b) => b.role === "healer" && b.alive !== false).length);
  mark("commander", V.commanderPlace(bind, mapId) ? 1 : 0);
  mark("mobhostile", cls("hostile")); mark("mobneutral", cls("neutral")); mark("mobcritter", cls("critter"));
  mark("aggro", V.aggroMobs(mobs.filter((m) => V.mobClass(m) === "hostile")).length);
  mark("hostilenames", mobs.filter((m) => V.mobClass(m) === "hostile" && m.name).length);
  mark("bossmark", mobs.filter((m) => m.rank === "b").length);
  mark("threatcolors", Object.keys(marks.tank).length > 0 && Object.keys(marks.pull).length > 0 ? 1 : 0);
  mark("threat", V.threatLines(mobs, onMap).length); mark("bottarget", V.botTargetLines(onMap, mobs).length);
  mark("healline", P.healLines(onMap).length); mark("ccline", P.ccLines(onMap, mobs).length);
  mark("hpbars", onMap.filter((b) => U.hpBar(b, "bot")).length + mobs.filter((m) => U.hpBar(m, "mob")).length);
  const icons = {};
  onMap.forEach((b) => U.statusIcons(b, "bot").forEach((i) => { icons[i.icon] = 1; }));
  mobs.forEach((m) => U.statusIcons(m, "mob").forEach((i) => { icons[i.icon] = 1; }));
  Object.keys(icons).forEach((i) => { seen["icon:" + i] = (seen["icon:" + i] || 0) + 1; });
  mark("statusicons", Object.keys(icons).length);
  ctx.steps = steps; ctx.dcx = dc();
  mark("runsteps", run("cmdRunStepsSvg(dcx, steps, lead)").length);
  ctx.stalls = stalls; ctx.dcx = dc();
  mark("stalls", stalls.length ? run("cmdStallsSvg(dcx, stalls)").length : 0);
  const evs = U.eventsOf(ctx.cmdEvents, bind.commander);
  evs.forEach((e) => events.add(e.kind));
  mark("events", U.pinsOn(evs, mapId, 30).length); mark("feed", evs.length);
  const ri = U.runInfo(bind, ctx.cmdEvents, sec); mark("runinfo", ri && ri.text ? 1 : 0);
  ctx.objs = P.objectsOf(bind, mapId); ctx.dcx = dc();
  mark("objects", ctx.objs.length ? run("cmdObjectsSvg(dcx, objs)").length : 0);
  ctx.objStates = (ctx.objStates || new Set()); ctx.objs.forEach((o) => ctx.objStates.add(o.type + o.state));
  steps.forEach((s) => { seen["step:" + s.state] = 1; seen["stepk:" + s.k] = 1; });
  stalls.forEach((s) => { seen["stall:" + s.kind] = 1; });
  mobs.forEach((m) => { if (m.cc) seen["cc:" + m.cc] = 1; if (m.cast) seen["mobcast"] = 1; if (m.rank) seen["rank:" + m.rank] = 1; });
  bind.bots.forEach((b) => { if (b.rest) seen["rest:" + b.rest] = 1; if (b.cc) seen["botcc:" + b.cc] = 1; if (!b.alive) seen["botdead"] = 1; });
  mobs.forEach((m) => { if (m.victim === "9001") seen["victim:tank"] = 1; if (m.victim === "9002") seen["victim:healer"] = 1; });
}

// the layers that need the daemon (the real picture, the zone outlines) are the only ones the demo cannot feed itself
const EXTERNAL = new Set(["zone", "zonelabels"]);
L.DEFS.forEach((d) => { if (!EXTERNAL.has(d.key)) assert.ok(seen[d.key] > 0, `layer "${d.key}" has nothing to show in the demo`); });
assert.ok(seen.bg, "the map picture comes from the daemon's map folder for map 70");

// and the pieces the story promises
["mobcast", "victim:tank", "victim:healer", "botdead", "rest:d", "rest:e", "cc:c", "cc:f", "botcc:s", "botcc:r", "rank:b", "rank:e",
  "step:cleared", "step:next", "step:skipped", "step:todo", "stall:pause", "stall:refused", "stepk:boss", "stepk:pull", "stepk:move", "stepk:open", "stepk:rest",
  "icon:skull", "icon:spark", "icon:sheep", "icon:arrow", "icon:cup", "icon:stun", "icon:root"].forEach((k) => assert.ok(seen[k], `the demo never shows ${k}`));
["death", "boss", "wipe", "open", "loot"].forEach((k) => assert.ok(events.has(k), `no ${k} event in the demo`));
["dr", "da", "br", "ba", "cr", "cu", "gr", "ga"].forEach((k) => assert.ok(ctx.objStates.has(k), `no object ${k}`));
assert.ok(new Set(D.frame(1040, 1000).mobs.map((m) => m.reaction)).size === 3, "hostile, neutral and friendly units");
console.log("demo ok", Object.keys(seen).length, "things seen");
