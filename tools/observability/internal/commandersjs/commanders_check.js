"use strict";
// Run by commandersjs_test.go under node: the Commanders tab's wording, warnings and map math.
const assert = require("assert");
const C = require(require("path").resolve(process.argv[2]));

const bot = (o) => Object.assign({ name: "B", guid: 1, class: "warrior", role: "dps", level: 60, hp: 100, max_hp: 100, power: 50, max_power: 100, power_type: "mana", alive: true, lead: false, map: 70, zone: 1337, x: 0, y: 0, z: 0 }, o);
const party = () => ({
  commander: "Gbmage", online: true, view: true, group: 7, lead: "Tankie",
  bots: [
    bot({ name: "Tankie", guid: 1, role: "tank", class: "warrior", lead: true, x: 100, y: 100, power_type: "rage" }),
    bot({ name: "Heals", guid: 2, role: "healer", class: "priest", x: 105, y: 100 }),
    bot({ name: "Dmg1", guid: 3, role: "dps", class: "mage", x: 110, y: 105 }),
  ],
  orders: [],
});

// ---- roles, rows, distance
assert.strictEqual(C.roleKey(bot({ role: "tank" })), "tank");
assert.strictEqual(C.roleKey(bot({ role: "tank", alive: false })), "dead");
assert.strictEqual(C.roleKey(bot({ role: "" })), "other");
assert.strictEqual(C.roleLabel(bot({ role: "dps" })), "DPS");
assert.strictEqual(C.distance(bot({ x: 0, y: 0 }), bot({ x: 3, y: 4 })), 5);
assert.strictEqual(C.distance(bot({ map: 1 }), bot({ map: 70 })), null);
let rows = C.partyRows(party());
assert.deepStrictEqual(rows.map((r) => r.name), ["Tankie", "Heals", "Dmg1"]);
assert.strictEqual(rows[0].lead, true);
assert.strictEqual(rows[1].dist, 5);
assert.strictEqual(rows[1].far, false);
assert.strictEqual(rows[2].cls, "Mage");
let p = party(); p.bots.push(bot({ name: "Far", guid: 4, x: 200, y: 100, hp: 25, max_hp: 100 }), bot({ name: "Away", guid: 5, map: 1 }));
rows = C.partyRows(p);
assert.strictEqual(rows.find((r) => r.name === "Far").far, true);
assert.strictEqual(rows.find((r) => r.name === "Far").hp, 25);
assert.strictEqual(rows.find((r) => r.name === "Away").otherMap, true);
assert.strictEqual(rows.find((r) => r.name === "Away").dist, null);
rows = C.partyRows({ commander: "x", lead: "", bots: [bot({ max_hp: 0, hp: 0, max_power: 0 })] });
assert.strictEqual(rows[0].hp, 0); assert.strictEqual(rows[0].hasPower, false);

// ---- orders in plain words
const t = (o) => C.orderText(Object.assign({ at: 1, verb: "move", detail: "", ok: true, result: "party 5" }, o));
assert.strictEqual(t({ verb: "attack", detail: "0xF130001B5300AC91" }).text, "Attack a target → OK (5 bots)");
assert.strictEqual(t({ verb: "move", detail: "", ok: false, result: "no-path: No walkable path." }).text, "Move → refused: no walkable path");
assert.strictEqual(t({ verb: "move", detail: "-6.7 401.9 -52.1" }).text, "Move to -7, 402 → OK (5 bots)");
assert.strictEqual(t({ verb: "follow", result: "party 1" }).text, "Follow → OK (1 bot)");
assert.strictEqual(t({ verb: "loot", detail: "Heals 0x1", result: "bot:Heals 1" }).text, "Loot a target → OK (Heals)");
assert.strictEqual(t({ verb: "state", detail: "entry:7011@1,2,3", result: "guid:1,entry:7011" }).text, "Check creature 7011 → OK (guid:1,entry:7011)");
assert.strictEqual(t({ verb: "stay", ok: false, result: "denied" }).text, "Stay → refused: denied");
assert.strictEqual(t({ verb: "cc", detail: "a very long argument that goes on and on and on", ok: true, result: "" }).text.indexOf("…") > 0, true);
assert.strictEqual(t({ verb: "wibble" }).label, "Wibble");
assert.ok(/^ERR cmd move -> no-path/.test(t({ verb: "move", ok: false, result: "no-path: x" }).raw));

// ---- state of the party
const ord = (verb, at, ok) => ({ at, verb, detail: "", ok: ok !== false, result: "party 3" });
let b = party();
assert.strictEqual(C.stateWord(b, { nowSec: 1000 }), "idle");
assert.strictEqual(C.stateWord({ bots: [], orders: [] }, { nowSec: 1 }), "no bots reporting");
b.bots.forEach((x) => (x.alive = false));
assert.strictEqual(C.stateWord(b, { nowSec: 1000 }), "wiped");
b = party();
assert.strictEqual(C.stateWord(b, { nowSec: 1000, hpDropAt: 995 }), "fighting");
assert.strictEqual(C.stateWord(b, { nowSec: 1000, hpDropAt: 900 }), "idle");
// a module that reports the AI state (st: c = combat) says "fighting" itself, with no HP drop needed
assert.strictEqual(C.stateWord({ bots: [{ name: "A", alive: true, st: "c" }], orders: [] }, { nowSec: 1000 }), "fighting");
assert.strictEqual(C.stateWord({ bots: [{ name: "A", alive: false, st: "c" }, { name: "B", alive: true, st: "n" }], orders: [] }, { nowSec: 1000 }), "idle");
b.orders = [ord("attack", 990)];
assert.strictEqual(C.stateWord(b, { nowSec: 1000 }), "attacking");
assert.strictEqual(C.stateWord(b, { nowSec: 1100 }), "idle");
b.orders = [ord("pull", 990)];
assert.strictEqual(C.stateWord(b, { nowSec: 1000 }), "pulling");
b.orders = [ord("follow", 900)];
assert.strictEqual(C.stateWord(b, { nowSec: 1000 }), "following");
assert.strictEqual(C.stateWord(b, { nowSec: 1000, leadMovedAt: 997 }), "moving");
b.orders = [ord("rest", 900), ord("state", 950), ord("move", 960, false)];
assert.strictEqual(C.stateWord(b, { nowSec: 1000 }), "resting"); // a refused order and a question change nothing
assert.strictEqual(C.headerText(party(), "Uldaman", "fighting"), "Gbmage → 3 bots · Uldaman · fighting");
assert.strictEqual(C.headerText({ commander: "G", bots: [bot({})] }, "", ""), "G → 1 bot");

// ---- warnings
assert.deepStrictEqual(C.warnings(party(), { ageSec: 3 }), []);
p = party(); p.bots[1].alive = false; p.bots[1].hp = 0;
let w = C.warnings(p, {});
assert.strictEqual(w.length, 1); assert.strictEqual(w[0].kind, "dead"); assert.strictEqual(w[0].level, "bad"); assert.strictEqual(w[0].text, "1 down: Heals");
p = party(); p.bots[2].x = 160; // 60 yd from the lead
w = C.warnings(p, {});
assert.strictEqual(w[0].kind, "far"); assert.ok(/Dmg1 \d+ yd/.test(w[0].text));
p = party(); p.bots[2].map = 1;
assert.strictEqual(C.warnings(p, {})[0].kind, "othermap");
p = party(); p.bots[2].hp = 20;
assert.strictEqual(C.warnings(p, {})[0].kind, "lowhp");
p = party(); p.orders = [ord("move", 1, true), { at: 2, verb: "move", detail: "", ok: false, result: "no-path: No walkable path." }];
w = C.warnings(p, {});
assert.strictEqual(w[0].kind, "refused"); assert.strictEqual(w[0].text, "Last order refused: Move (no walkable path)");
p.orders.push(ord("follow", 3));
assert.deepStrictEqual(C.warnings(p, {}), []); // a newer accepted order clears it
p = party(); p.online = false;
assert.strictEqual(C.warnings(p, {})[0].kind, "offline");
p = party(); p.lead = "Gone"; p.bots.forEach((x) => (x.lead = false));
assert.strictEqual(C.warnings(p, {})[0].kind, "nolead");
assert.deepStrictEqual(C.warnings(party(), { ageSec: 30 }), []);
w = C.warnings(party(), { ageSec: 45 });
assert.strictEqual(w[0].kind, "stale"); assert.ok(/45 s/.test(w[0].text));
assert.strictEqual(C.ageText(5), "5 s"); assert.strictEqual(C.ageText(125), "2 min 05 s"); assert.strictEqual(C.ageText(3700), "1 h 01 min");
p = party(); for (let i = 0; i < 4; i++) p.bots.push(bot({ name: "F" + i, guid: 10 + i, x: 300 + i * 10 }));
assert.ok(/ and 2 more$/.test(C.warnings(p, {})[0].text));

// ---- tracker: fights and the lead's trail from changes between reports
let tr = C.newTracker();
b = party();
C.note(tr, b, 100);
assert.strictEqual(C.context(tr, b, 100).hpDropAt, 0);
b = party(); b.bots[1].hp = 80;
C.note(tr, b, 102);
assert.strictEqual(C.context(tr, b, 102).hpDropAt, 102);
b = party(); b.bots[1].hp = 90; // healed: no new drop
C.note(tr, b, 104);
assert.strictEqual(C.context(tr, b, 104).hpDropAt, 102);
assert.strictEqual(C.context(tr, b, 104).leadMovedAt, 0);
b = party(); b.bots[0].x = 110; // the lead walked 10 yd
C.note(tr, b, 106);
assert.strictEqual(C.context(tr, b, 106).leadMovedAt, 106);
assert.strictEqual(tr.trail.Gbmage.pts.length, 2);
b = party(); b.bots[0].x = 110.5; C.note(tr, b, 108); // under 2 yd: no new point
assert.strictEqual(tr.trail.Gbmage.pts.length, 2);
b = party(); b.bots[0].map = 1; C.note(tr, b, 110); // a new map restarts the trail
assert.strictEqual(tr.trail.Gbmage.pts.length, 1); assert.strictEqual(tr.trail.Gbmage.map, 1);
for (let i = 0; i < 300; i++) { b = party(); b.bots[0].x = i * 5; C.note(tr, b, 200 + i); }
assert.strictEqual(tr.trail.Gbmage.pts.length, 240, "the lead's trail is a ring of 240 positions (the path-length slider's maximum)");
// the daemon's stored history goes in front: only older points, only on the trail's map
{
  const t2 = C.newTracker(); let q = party(); q.bots[0].x = 500; C.note(t2, q, 1000);
  assert.strictEqual(C.prependTrail(t2, "Gbmage", q.bots[0].map, [[1, 2, 10], [3, 4, 20], [500, 0, 1000], [9, 9, 1500], ["x", 1, 2], null]), 2);
  assert.deepStrictEqual(t2.trail.Gbmage.pts.map((p) => p[2]), [10, 20, 1000]);
  assert.strictEqual(C.prependTrail(t2, "Gbmage", q.bots[0].map, [[1, 2, 10]]), 0, "twice adds nothing");
  assert.strictEqual(C.prependTrail(t2, "Gbmage", 999, [[1, 2, 1]]), 0, "another map");
  assert.strictEqual(C.prependTrail(t2, "Nobody", 1, [[1, 2, 1]]), 0);
  assert.strictEqual(C.prependTrail(t2, "Gbmage", q.bots[0].map, "junk"), 0);
}
// a dead bot that is raised is not a "drop"
tr = C.newTracker(); b = party(); b.bots[1].alive = false; b.bots[1].hp = 0; C.note(tr, b, 1);
b = party(); b.bots[1].hp = 50; C.note(tr, b, 3);
assert.strictEqual(C.context(tr, b, 3).hpDropAt, 0);
C.prune(tr, []);
assert.deepStrictEqual(Object.keys(tr.trail), []); assert.deepStrictEqual(Object.keys(tr.hp), []);

// ---- map math
assert.deepStrictEqual(C.project(10, 20), { x: -20, y: -10 }); // north (x) is up, west (y) is left
let v = C.fitView([{ x: 0, y: 0 }, { x: 100, y: 50 }], 400, 300, 20, 60);
assert.strictEqual(v.cx, 50); assert.strictEqual(v.cy, 25);
assert.ok(Math.abs(v.scale - 360 / 100) < 1e-9);
v = C.fitView([{ x: 5, y: 5 }], 400, 300, 20, 60); // one point: at least 60 yd across
assert.ok(Math.abs(v.scale - Math.min(360 / 60, 260 / 60)) < 1e-9);
const bx = C.viewBox({ cx: 10, cy: 20, scale: 2 }, 400, 200);
assert.deepStrictEqual(bx, [-90, -30, 200, 100]);
// zoom keeps the point under the cursor
v = { cx: 0, cy: 0, scale: 2 };
const z = C.zoomAt(v, 2, 400, 300, 300, 100);
const at = (vv) => ({ x: vv.cx + (300 - 200) / vv.scale, y: vv.cy + (100 - 150) / vv.scale });
assert.ok(Math.abs(at(v).x - at(z).x) < 1e-9 && Math.abs(at(v).y - at(z).y) < 1e-9);
assert.strictEqual(C.zoomAt({ cx: 0, cy: 0, scale: 13 }, 4, 400, 300, 200, 150).scale, 14);
assert.deepStrictEqual(C.panBy({ cx: 0, cy: 0, scale: 2 }, 20, -10), { cx: -10, cy: 5, scale: 2 });
const pz = C.pinch({ cx: 0, cy: 0, scale: 1 }, { x: 150, y: 150 }, { x: 250, y: 150 }, { x: 100, y: 150 }, { x: 300, y: 150 }, 400, 300);
assert.ok(Math.abs(pz.scale - 2) < 1e-9);
assert.strictEqual(C.gridStep(1), 100); assert.strictEqual(C.gridStep(4), 20); assert.strictEqual(C.gridStep(0.04), 2000);
const gl = C.gridLines([-95, -30, 200, 100], 50);
assert.deepStrictEqual(gl.xs, [-50, 0, 50, 100]); assert.deepStrictEqual(gl.ys.map((n) => n + 0), [0, 50]); // -30..70 -> 0, 50
const segs = C.trailSegments(Array.from({ length: 50 }, (_, i) => ({ x: i, y: 0 })), 10, 0.2);
assert.ok(segs.length <= 10 && segs.length >= 2);
assert.ok(segs[0].alpha < segs[segs.length - 1].alpha && segs[segs.length - 1].alpha === 1);
assert.deepStrictEqual(C.trailSegments([{ x: 0, y: 0 }], 5), []);

// ---- links to the detail view of a character (the route app.js parses)
assert.strictEqual(C.botHref(1234), "#/commanders?bot=1234");
assert.strictEqual(C.botHref("77"), "#/commanders?bot=77");
["", 0, -5, 1.5, "abc", null, undefined, NaN, 4294967296, "1;x", "0x10"].forEach((g) => assert.strictEqual(C.botHref(g), "", String(g)));
assert.strictEqual(C.partyRows(party())[1].guid, 2);
assert.deepStrictEqual(C.headerParts(party(), "Uldaman", "fighting"), ["Gbmage", " → 3 bots · Uldaman · fighting"]);
assert.deepStrictEqual(C.headerParts({ commander: "", bots: [] }, "", ""), ["", " → 0 bots"]);

// ---- map layers helpers
// heading: positions [x, y, t] in game coordinates; the unit vector comes back in the plane (x -> -y, y -> -x)
let hd = C.heading([[0, 0, 1], [10, 0, 2]], 3, 20, 4); // walking north (game x grows): up the screen, plane y shrinks
assert.ok(Math.abs(hd.x) < 1e-9 && Math.abs(hd.y + 1) < 1e-9);
hd = C.heading([[0, 0, 1], [0, 10, 2]], 3, 20, 4); // walking west (game y grows): plane x shrinks, to the left of the screen
assert.ok(Math.abs(hd.x + 1) < 1e-9 && Math.abs(hd.y) < 1e-9);
assert.strictEqual(C.heading([[0, 0, 1], [1, 0, 2]], 3, 20, 4), null);   // moved less than 4 yd
assert.strictEqual(C.heading([[0, 0, 1], [10, 0, 2]], 100, 20, 4), null); // newest point is stale
assert.strictEqual(C.heading([[0, 0, 1]], 3), null);
assert.strictEqual(C.heading(null, 3), null);
// a step of 1 yd at a time still finds the point far enough back
hd = C.heading(Array.from({ length: 12 }, (_, i) => [i, 0, i + 1]), 12, 20, 4);
assert.ok(hd && Math.abs(hd.y + 1) < 1e-9);
// time labels keep their distance and the newest one
const pts = Array.from({ length: 101 }, (_, i) => [i, 0, 1000 + i]);
let st = C.trailStamps(pts, 2, 70, 8); // 70 px at 2 px/yd is 35 yd
assert.strictEqual(st[st.length - 1].t, 1100);
for (let i = 1; i < st.length; i++) assert.ok(Math.hypot(st[i].x - st[i - 1].x, st[i].y - st[i - 1].y) * 2 >= 70 - 1e-9);
assert.ok(st.length <= 8 && st.length >= 3);
assert.strictEqual(C.trailStamps(pts, 2, 70, 2).length, 2);
assert.deepStrictEqual(C.trailStamps([[0, 0, 0], [5, 5, 0]], 2, 70, 8), []); // no times, no labels
assert.deepStrictEqual(C.trailStamps(null, 1), []);
// scale bar: the longest round number of yards within maxPx
assert.deepStrictEqual(C.scaleBar(1, 110), { yd: 100, px: 100 });
assert.deepStrictEqual(C.scaleBar(4, 110), { yd: 20, px: 80 });
assert.deepStrictEqual(C.scaleBar(0.04, 110), { yd: 2000, px: 80 });
assert.strictEqual(C.scaleBar(500, 110).yd, 1); // even a yard is wide: still a yard
// zones around: those overlapping the box, not the skipped one
const zb = {
  "1_14": { map_id: 1, area_id: 14, name: "Durotar", loc_left: 100, loc_right: -100, loc_top: 100, loc_bottom: -100 },
  "1_17": { map_id: 1, area_id: 17, name: "Barrens", loc_left: 1000, loc_right: 500, loc_top: 1000, loc_bottom: 500 },
  "0_9": { map_id: 0, area_id: 9, name: "Elsewhere", loc_left: 100, loc_right: -100, loc_top: 100, loc_bottom: -100 },
};
let zs = C.zonesIn(zb, 1, [-150, -150, 300, 300], 0);
assert.deepStrictEqual(zs.map((z) => z.name), ["Durotar"]);
assert.deepStrictEqual([zs[0].x, zs[0].y, zs[0].w, zs[0].h], [-100, -100, 200, 200]);
assert.deepStrictEqual(C.zonesIn(zb, 1, [-150, -150, 300, 300], 14), []);
assert.deepStrictEqual(C.zonesIn(null, 1, [0, 0, 1, 1], 0), []);
// a picture lies where the game puts it: layer rect from the daemon (Zul'Farrak 209), a game point inside it
const L = { x: -1625, y: -2052.080078125, w: 1625 - 241.6699981689453, h: 2052.080078125 - 1129.1700439453125 };
const gp = C.project(2052.080078125 - 100, 1625 - 200); // 100 yd south and 200 yd east of the north-west corner
assert.ok(Math.abs(gp.x - (L.x + 200)) < 1e-9 && Math.abs(gp.y - (L.y + 100)) < 1e-9);
assert.ok(gp.x > L.x && gp.x < L.x + L.w && gp.y > L.y && gp.y < L.y + L.h);
// picture layers from the daemon: only its own file urls and finite rectangles
const okLayer = { name: "worldmap", source: "worldmap", url: "/api/v1/mapfiles/209.png", x: -1625, y: -2052, w: 1383, h: 923 };
assert.deepStrictEqual(C.pictureLayers([okLayer, Object.assign({}, okLayer, { name: "navmesh", url: "/api/v1/mapfiles/209.navmesh.png" }),
  Object.assign({}, okLayer, { name: "2f", url: "/api/v1/mapfiles/189.worldmap-2f.png" })]).map((l) => l.name), ["worldmap", "navmesh", "2f"]);
[ "http://evil.example/x.png", "/api/v1/mapfiles/../../etc/passwd.png", "/api/v1/mapfiles/209.json", "javascript:alert(1)", "/api/v1/mapfiles/209.png\"onload=\"x", "", 5 ]
  .forEach((u) => assert.deepStrictEqual(C.pictureLayers([Object.assign({}, okLayer, { url: u })]), [], String(u)));
assert.deepStrictEqual(C.pictureLayers([Object.assign({}, okLayer, { w: NaN })]), []);
assert.deepStrictEqual(C.pictureLayers([Object.assign({}, okLayer, { w: 0 })]), []);
assert.deepStrictEqual(C.pictureLayers([Object.assign({}, okLayer, { x: "1" })]), []);
assert.deepStrictEqual(C.pictureLayers(null), []); assert.deepStrictEqual(C.pictureLayers([null, 3]), []);
assert.strictEqual(C.pictureLayers([Object.assign({}, okLayer, { name: "x".repeat(100) })])[0].name.length, 40);

// ---- zooming while following: the party stays in the middle, only the scale changes
{
  const pts = [{ x: 0, y: 0 }, { x: 100, y: 60 }];
  const fit = C.followView(pts, 400, 300, 20, 60, null);
  assert.deepStrictEqual(fit, C.fitView(pts, 400, 300, 20, 60), "no zoom of the viewer's own: the fitting view");
  const in1 = C.zoomFollow(fit, 1.5);
  assert.strictEqual(in1.cx, fit.cx); assert.strictEqual(in1.cy, fit.cy); // the party's centre did not move
  assert.ok(Math.abs(in1.scale - fit.scale * 1.5) < 1e-9);
  // the next refresh (the party walked): centred on the new middle, at the viewer's zoom
  const walked = [{ x: 500, y: 500 }, { x: 600, y: 560 }];
  const kept = C.followView(walked, 400, 300, 20, 60, in1.scale);
  assert.strictEqual(kept.scale, in1.scale);
  assert.strictEqual(kept.cx, 550); assert.strictEqual(kept.cy, 530);
  // zoom out and in, clamped
  assert.strictEqual(C.zoomFollow({ cx: 1, cy: 2, scale: 13 }, 4).scale, 14);
  assert.strictEqual(C.zoomFollow({ cx: 1, cy: 2, scale: 0.05 }, 0.1).scale, 0.04);
  assert.strictEqual(C.followView(pts, 400, 300, 20, 60, 99).scale, 14, "a stored zoom is clamped too");
  assert.deepStrictEqual(C.followView([], 400, 300, 20, 60, 2), { cx: 0, cy: 0, scale: 1 }, "no points: nothing to follow");
  assert.deepStrictEqual(C.followView(pts, 400, 300, 20, 60, 0), fit, "0 means not chosen");
  // pinch while following: only the distance counts, the middle stays
  const a0 = { x: 100, y: 100 }, b0 = { x: 200, y: 100 };
  const pz = C.pinchFollow(fit, a0, b0, { x: 50, y: 300 }, { x: 250, y: 300 }); // spread 100 -> 200, and dragged down
  assert.strictEqual(pz.cx, fit.cx); assert.strictEqual(pz.cy, fit.cy);
  assert.ok(Math.abs(pz.scale - fit.scale * 2) < 1e-9);
  assert.strictEqual(C.pinchFollow(fit, a0, a0, a0, b0), fit, "fingers on one point: no change");
  // while not following, zoom around the cursor still keeps the point under it (unchanged)
  const free = C.zoomAt(fit, 2, 400, 300, 300, 100);
  assert.notStrictEqual(free.cx, fit.cx);
}

// ---- mobs, lines, rings, the commander
{
  const mobs = [
    { id: "11", entry: 1, name: "Wolf", hp: 100, x: 10, y: 20, reaction: "h", victim: "700" },
    { id: "12", entry: 2, name: "Rat", hp: 100, x: 11, y: 21, reaction: "n", kind: "k" },
    { id: "13", entry: 3, name: "Boar", hp: 50, x: 12, y: 22, reaction: "n" },
    { id: "14", entry: 4, name: "Guard", hp: 100, x: 13, y: 23, reaction: "f", kind: "g" },
    { id: "15", entry: 5, name: "Spider", hp: 90, x: 14, y: 24, reaction: "h" },
    { id: "16", entry: 6, name: "Cultist", hp: 0, x: 15, y: 25, reaction: "h" },
    { id: "17", entry: 7, name: "Civilian", hp: 100, x: 1, y: 1, reaction: "h", kind: "v" },
  ];
  const bind = { mobs_map: 209, mobs: mobs.concat([null, { id: 5, x: 1, y: 1, reaction: "h" }, { id: "18", x: NaN, y: 1, reaction: "h" }, { id: "19", x: 1, y: 1, reaction: "z" }]) };
  assert.strictEqual(C.mobsOf(bind, 209).length, 7, "malformed mobs are dropped");
  assert.deepStrictEqual(C.mobsOf(bind, 1), [], "another map: none");
  assert.deepStrictEqual(C.mobsOf({}, 209), []); assert.deepStrictEqual(C.mobsOf(null, 209), []);
  assert.deepStrictEqual(C.mobsOf({ mobs_map: 209, mobs: "x" }, 209), []);
  assert.deepStrictEqual(mobs.map(C.mobClass), ["hostile", "critter", "neutral", "", "hostile", "hostile", ""]);
  const bots = [{ id: "700", name: "Tank", x: 0, y: 0, alive: true, target: "11" }, { id: "701", name: "Heal", x: 1, y: 1, alive: true },
    { id: "702", name: "Dead", x: 2, y: 2, alive: false, target: "15" }];
  const tl = C.threatLines(mobs, bots);
  assert.strictEqual(tl.length, 1); assert.strictEqual(tl[0].mob.id, "11"); assert.strictEqual(tl[0].bot.name, "Tank");
  assert.deepStrictEqual(C.threatLines(mobs, []), []);
  assert.deepStrictEqual(C.threatLines([{ id: "1", x: 0, y: 0, reaction: "h", victim: "999" }], bots), [], "a victim that is not a listed bot");
  const bl = C.botTargetLines(bots, mobs);
  assert.strictEqual(bl.length, 1, "a dead bot draws no line"); assert.strictEqual(bl[0].mob.name, "Wolf");
  assert.deepStrictEqual(C.botTargetLines([{ id: "1", target: "nope", alive: true }], mobs), []);
  // aggro rings: hostile mobs nobody fights yet (the wolf has a victim, the dead cultist has no health)
  assert.deepStrictEqual(C.aggroMobs(mobs).map((m) => m.name), ["Spider"]);
  assert.strictEqual(C.AGGRO_RANGE_YD, 20);
  assert.deepStrictEqual(C.commanderPlace({ commander_pos: { map: 209, x: 5, y: 6 } }, 209), { x: 5, y: 6 });
  assert.strictEqual(C.commanderPlace({ commander_pos: { map: 1, x: 5, y: 6 } }, 209), null);
  assert.strictEqual(C.commanderPlace({ commander_pos: { map: 209, x: "5", y: 6 } }, 209), null);
  assert.strictEqual(C.commanderPlace({}, 209), null);
}
console.log("commanders.js ok");
