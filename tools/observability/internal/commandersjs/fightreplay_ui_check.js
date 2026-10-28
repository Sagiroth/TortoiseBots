"use strict";
// Run by commandersjs_test.go under node: the fight replay and the aggro lanes as app.js builds them (the play row, the lane bars,
// the replayed binding) and the wiring that drives them (one track fetch per fight, the setTimeout chain, the live-only layers
// switched off). Also the floor rule: a multi-floor map picture (AQ40, Naxxramas) draws only the plan points of the floor it
// shows; one floor, or points without a z, draw as before.
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");
const appPath = path.resolve(process.argv[2]), dir = path.dirname(appPath);
const src = fs.readFileSync(appPath, "utf8");
const indexHtml = fs.readFileSync(path.join(dir, "..", "..", "index.html"), "utf8");
const trackJson = JSON.parse(fs.readFileSync(path.join(__dirname, "testdata", "walk2-track.json"), "utf8"));
const walking = JSON.parse(fs.readFileSync(path.join(__dirname, "..", "state", "testdata", "walk2-commanders-walking.json"), "utf8"));

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
["fightdebug.js", "fightreplay.js", "aggrolanes.js"].forEach((f) => {
  vm.runInContext(fs.readFileSync(path.join(dir, f), "utf8"), ctx, { filename: f });
});
assert.ok(ctx.FightReplay && typeof ctx.FightReplay.botsAt === "function", "fightreplay.js loads as FightReplay");
assert.ok(ctx.AggroLanes && typeof ctx.AggroLanes.lanes === "function", "aggrolanes.js loads as AggroLanes");
vm.runInContext([grab("esc"), grabConst("CMD_MAP_ICONS"), grab("cmdMapIcon"), grabConst("cmdPt"), grab("cmdPlanSvg"),
  grab("cmdFloors"), grab("cmdFloorZs"), grabConst("CMD_FLOOR_GAP_YD"), grabConst("FD_REPLAY_OFF"), grab("fdPlayHtml"),
  grab("fdLanesHtml"), grab("fdReplayBind")].join("\n")
  + "\nvar CmdView = { project: (x, y) => ({ x: -y, y: -x }) };", ctx);

const run = (expr, vars) => { if (vars) Object.assign(ctx, vars); return vm.runInContext(expr, ctx); };
const f = (n) => n.toFixed(2);
const mk = () => ({ f, k: 0.5, units: {} });
ctx.mk = mk;

// ---- the play row: a fight with positions gets the controls, one without them says so and cannot be played ------------------
const okWin = { startS: 100, endS: 140, frames: 20, gapMaxS: 2, ok: true };
const rp = { on: false, t: 0, playing: false, speed: 1 };
const play = ctx.fdPlayHtml(okWin, 39111, rp);
assert.ok(play.includes('max="39111"'), "the scrub runs the fight's length");
assert.ok(play.includes('step="100"'));
assert.ok(play.includes('aria-label="Replay time"'));
assert.ok(play.includes('aria-pressed="false"'));
assert.ok(play.includes("1×"), "the speed button shows the speed");
assert.ok(!/disabled/.test(play), "a fight with positions is playable");
const blind = ctx.fdPlayHtml({ ok: false, frames: 0, gapMaxS: 0 }, 39111, rp);
assert.strictEqual((blind.match(/disabled/g) || []).length, 3, "no positions: play, speed and scrub are disabled");
assert.ok(blind.includes("No positions kept for this fight"), "and the row says why");

// ---- the lanes: who each enemy targeted, the healer's time called out in red ------------------------------------------------
const fight = { start: 1000, dur_ms: 10000, bots: [
  { name: "Tank", guid: 1, role: "tank" }, { name: "Heal", guid: 2, role: "healer" }, { name: "Dps", guid: 3, role: "dps" },
] };
const frame = (t, mobs) => ({ t, map: 1, step: 0, fight: true, bots: [], mobs });
const m1 = (v, hp) => ({ id: "m1", e: 7, x: 0, y: 0, hp: hp == null ? 98 : hp, v });
const laneJson = { commander: "G", frames: [
  frame(998, [m1(1)]), frame(1000, [m1(1)]), frame(1002, [m1(2)]), frame(1004, [m1(2)]), frame(1006, [m1(1)]),
  frame(1008, [m1(1), { id: "m2", e: 7, x: 1, y: 1, hp: 100 }]), frame(1010, [m1(0, 0)]),
], names: { "7": "Warder" } };
ctx.laneJson = laneJson; ctx.fight = fight;
const lanesRes = run("AggroLanes.lanes(laneJson, fight, [])");
const lanes = ctx.fdLanesHtml(lanesRes, 10000);
assert.strictEqual((lanes.match(/class="fd-seg/g) || []).length, 3, "three segments: tank, healer, tank");
assert.strictEqual((lanes.match(/fd-seg k-healer bad/g) || []).length, 1, "the healer's time is the bad one");
assert.ok(lanes.includes("60% on tanks"));
assert.ok(lanes.includes("4.0 s on Heal"));
assert.ok(lanes.includes("Aggro per mob: blue = on a tank, red = on anyone else."), "the prototype caption");
assert.ok(lanes.includes("Warder"), "the lane is labelled with the entry's name");
const evilLane = JSON.parse(JSON.stringify(lanesRes));
evilLane.lanes[0].label = '<img src=x onerror=alert(1)>"&';
const evilOut = ctx.fdLanesHtml(evilLane, 10000);
assert.ok(!/<img/.test(evilOut), "a lane label is escaped");

// ---- the replayed binding: the walk 2 report replaced by the kept positions at one moment -----------------------------------
const bind = walking.commanders[0];
ctx.bind = bind; ctx.track = { raw: trackJson, prep: ctx.FightReplay.prep(trackJson) }; ctx.walkFight = { start: 1793210079, dur_ms: 4000 };
const replayed = run("fdReplayBind(bind, walkFight, track, 1793210079)");
assert.strictEqual(replayed.bots.length, 39, "every bound bot is drawn at that moment");
replayed.bots.forEach((b) => {
  assert.strictEqual(b.id, String(b.guid), "a replay bot keeps its guid as its id");
  assert.strictEqual(typeof b.hp, "number");
  assert.strictEqual(b.target, "");
});
assert.strictEqual(replayed.mobs.length, 1);
assert.strictEqual(replayed.mobs[0].victim, "814", "the warder's target is the bot it faced in that report");
assert.strictEqual(replayed.mobs[0].name, "Onyxian Warder");
assert.strictEqual(replayed.mobs_map, 249);
assert.strictEqual(replayed.objects.length, 0, "the replay draws no objects");
assert.strictEqual(replayed.orders.length, 0, "and no orders");
assert.strictEqual(replayed.commander, "Glacienne", "the live binding's own fields survive");

// ---- floors: two floor pictures, points on two z levels; the shown floor's points only --------------------------------------
const slots = [
  { kind: "worldmap", floor: "", key: "worldmap", label: "World map", available: true, url: "/api/v1/mapfiles/531.png", x: 0, y: 0, w: 1, h: 1 },
  { kind: "worldmap", floor: "2f", key: "worldmap 2f", label: "World map 2f", available: true, url: "/api/v1/mapfiles/531.worldmap-2f.png", x: 0, y: 0, w: 1, h: 1 },
];
const stepAt = (i, kind, x, z, state) => ({ i, k: kind, x, y: 0, z, r: 0, n: "", c: "", state,
  stand: false, pack: kind === "pull", fight: false, stray: false, margin: null });
const floorView = {
  numbering: true, route: [], strays: [],
  steps: [stepAt(1, "move", 0, -80, "done"), stepAt(2, "pull", 10, -80, "cur"), stepAt(3, "move", 20, -40, "todo"), stepAt(4, "pull", 30, -40, "todo")],
  rings: [
    { x: 10, y: 0, z: -80, r: 23, wake: false, step: 2, guid: 11, name: "Warder", state: "cur", patrol: false },
    { x: 30, y: 0, z: -40, r: 23, wake: false, step: 4, guid: 12, name: "Warder", state: "todo", patrol: false },
  ],
  mobs: [
    { x: 10, y: 0, z: -80, step: 2, guid: 11, name: "Warder", state: "cur", patrol: false, boss: false },
    { x: 30, y: 0, z: -40, step: 4, guid: 12, name: "Warder", state: "todo", patrol: false, boss: false },
  ],
};
ctx.slots = slots; ctx.floorView = floorView;
run("upper = cmdFloors(slots, slots[1], cmdFloorZs(floorView)); floorView.floors = upper;", { upper: null });
const upperSvg = run("cmdPlanSvg(mk(), floorView, { planroute: true, planstands: true, planrings: true })");
assert.ok(upperSvg.includes('data-u="p:4"'), "the second floor's pack is drawn");
assert.ok(!upperSvg.includes('data-u="p:2"'), "the first floor's pack is hidden");
assert.strictEqual((upperSvg.match(/class="plan-ring/g) || []).length, 1, "one ring: the shown floor's");
assert.strictEqual((upperSvg.match(/class="plan-mob/g) || []).length, 1, "one mob dot: the shown floor's");
run("floorView.floors = cmdFloors(slots, slots[0], cmdFloorZs(floorView));");
const lowerSvg = run("cmdPlanSvg(mk(), floorView, { planroute: true, planstands: true, planrings: true })");
assert.ok(lowerSvg.includes('data-u="p:2"'), "the main picture shows the first floor");
assert.ok(!lowerSvg.includes('data-u="p:4"'), "and not the second");
assert.strictEqual(run("cmdFloors([slots[0]], slots[0], [-80, -40])"), null, "one floor picture hides nothing");
assert.strictEqual(run("cmdFloors(slots, { kind: 'minimap', floor: '' }, [-80, -40])"), null, "a minimap says no floor");

// ---- the wiring: one track fetch per fight, a timeout chain (never an interval), the live-only layers off in replay ---------
assert.ok(src.includes("'/api/v1/commanders/track?commander=' + encodeURIComponent("), "the commander is encoded in the track URL");
assert.ok(src.includes("FightReplay.botsAt("), "the replay reads the kept positions");
assert.ok(src.includes("AggroLanes.lanes("), "the lanes read the same track answer");
assert.ok(src.includes("setTimeout(() => fdReplayTick(key)"), "playback is a setTimeout chain");
assert.ok(src.includes("clearTimeout(fd.rp.timer)"), "stopping clears the pending step");
assert.ok(grab("fdReplayStop").includes("clearTimeout("), "fdReplayStop clears the pending step");
assert.ok(src.includes("const liveBind = cmdFindBind(key)"), "the map keeps the live binding beside the replayed one");
assert.ok(src.includes("fd.tracks["), "the per-commander state keeps the tracks");
assert.strictEqual((src.match(/setInterval\(/g) || []).length, 8, "no new setInterval");
const off = run("FD_REPLAY_OFF");
assert.ok(off.includes("trail") && off.includes("membertrails") && off.includes("events"));
assert.ok(!off.includes("threat") && !off.includes("bots") && !off.includes("plan"));
assert.ok(src.includes("fdReplayStop("), "the selection change, the mode change and the card's removal stop the replay");

// ---- index.html loads the two pure modules before the page script ----------------------------------------------------------
const fi = indexHtml.indexOf('<script src="/static/js/fightreplay.js');
const ai2 = indexHtml.indexOf('<script src="/static/js/aggrolanes.js');
const pi = indexHtml.indexOf('<script src="/static/js/planoverlay.js');
const app = indexHtml.indexOf('<script src="/static/js/app.js');
assert.ok(fi > 0 && ai2 > 0, "index.html loads fightreplay.js and aggrolanes.js");
assert.ok(pi > 0 && fi > pi && ai2 > pi, "both come after planoverlay.js");
assert.ok(app > fi && app > ai2, "and before app.js");

console.log("fight replay ui ok");
