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
assert.ok(tr.trail.Gbmage.pts.length <= 120);
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
console.log("commanders.js ok");
