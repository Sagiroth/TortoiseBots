"use strict";
// Run by commandersjs_test.go under node: what a character does, the hover box, bars, threat colours, status icons, events and the run line.
const assert = require("assert");
const U = require(require("path").resolve(process.argv[2]));

const bot = (o) => Object.assign({ name: "Forthur", guid: 7, id: "7", class: "warrior", role: "tank", level: 46, hp: 1000, max_hp: 2000, power: 40, max_power: 100, power_type: "rage", alive: true, map: 70, x: 10, y: 20 }, o);
const mob = (o) => Object.assign({ id: "500", entry: 6910, name: "Revelosh", hp: 80, x: 13, y: 24, reaction: "h" }, o);

// ---- what a character does
assert.strictEqual(U.activity(null), null);
assert.strictEqual(U.activity(bot({})), null, "an older module reports no state: nothing is said");
assert.deepStrictEqual(U.activity(bot({ alive: false })), { icon: "dead", word: "dead", text: "dead" }, "dead needs no module field");
assert.strictEqual(U.activity(bot({ st: "c" }), { targetName: "Revelosh" }).text, "fighting Revelosh");
assert.strictEqual(U.activity(bot({ st: "c", tname: "Rat" })).text, "fighting Rat", "the module's own target name");
assert.strictEqual(U.activity(bot({ st: "c" })).text, "fighting");
assert.strictEqual(U.activity(bot({ st: "c", cast: "Frostbolt", tname: "Rat" })).text, "casting Frostbolt on Rat");
assert.strictEqual(U.activity(bot({ st: "n", cast: "Frostbolt" })).icon, "cast");
assert.strictEqual(U.activity(bot({ st: "n", rest: "d" })).word, "drinking");
assert.strictEqual(U.activity(bot({ st: "n", rest: "e" })).word, "eating");
assert.strictEqual(U.activity(bot({ st: "n", mv: true })).word, "moving");
assert.strictEqual(U.activity(bot({ st: "c", mv: true })).word, "fighting", "chasing a target in combat is still fighting");
assert.strictEqual(U.activity(bot({ st: "c", cc: "s", cast: "x" })).word, "stunned", "crowd control beats everything");
assert.strictEqual(U.activity(bot({ st: "n", cc: "c" })).icon, "cc");
assert.strictEqual(U.activity(bot({ st: "n", act: "follow" })).word, "following");
assert.strictEqual(U.activity(bot({ st: "n", act: "some odd action" })).word, "idle", "an unknown action name is not guessed at");
assert.strictEqual(U.activity(bot({ st: "n" })).word, "idle");

// ---- the hover box
const lead = bot({ lead: true, x: 0, y: 0 });
const tank = bot({ name: "Tanky", guid: 8, id: "8", x: 3, y: 4, st: "c", target: "500", act: "melee", res: "O" });
const boss = mob({ lvl: 47, rank: "b", cast: "Chain Lightning", victim: "8", cc: "f" });
const ctx = { lead, bots: [lead, tank], mobs: [boss, mob({ id: "501", name: "", entry: 1234 })] };
let d = U.describeMob(boss, ctx);
assert.strictEqual(d.title, "Revelosh"); assert.strictEqual(d.sub, "level 47 · Boss · Hostile"); assert.strictEqual(d.tone, "boss");
const row = (box, label) => (box.rows.find((r) => r[0] === label) || [])[1];
assert.strictEqual(row(d, "Health"), "80 %"); assert.strictEqual(row(d, "Casting"), "Chain Lightning");
assert.strictEqual(row(d, "Attacking"), "Tanky"); assert.strictEqual(row(d, "Crowd control"), "feared or running away");
assert.strictEqual(row(d, "From the lead"), "27 yd"); assert.strictEqual(row(d, "Entry"), "6910");
d = U.describeMob(ctx.mobs[1], ctx);
assert.strictEqual(d.title, "Creature 1234", "no name: the entry"); assert.strictEqual(d.sub, "Hostile"); assert.strictEqual(row(d, "Attacking"), "nobody");
assert.strictEqual(row(U.describeMob(mob({ victim: "999" }), ctx), "Attacking"), "someone outside the party");
assert.strictEqual(U.describeMob(mob({ reaction: "f", kind: "g" }), ctx).sub, "Friendly · guard");
assert.strictEqual(U.describeMob(mob({ reaction: "n", kind: "k", lvl: 1 }), ctx).sub, "level 1 · Neutral · critter");
assert.strictEqual(U.describeMob(mob({ hp: undefined }), ctx).rows[0][1], "unknown", "no health reported");
d = U.describeBot(tank, ctx);
assert.strictEqual(d.title, "Tanky"); assert.strictEqual(d.sub, "Tank · warrior · level 46");
assert.strictEqual(row(d, "Health"), "50 % (1000 / 2000)"); assert.strictEqual(row(d, "Doing"), "fighting Revelosh");
assert.strictEqual(row(d, "Target"), "Revelosh"); assert.strictEqual(row(d, "From the lead"), "5 yd");
assert.strictEqual(row(d, "Last action"), "melee (done)"); assert.strictEqual(row(d, "Rage"), "40 %");
d = U.describeBot(lead, ctx);
assert.strictEqual(row(d, "Role"), "the lead of the party"); assert.strictEqual(row(d, "From the lead"), undefined);
d = U.describeBot(bot({ alive: false, name: "Gone" }), ctx);
assert.strictEqual(d.tone, "dead"); assert.strictEqual(row(d, "Health"), "dead"); assert.strictEqual(row(d, "Doing"), "dead");
assert.strictEqual(row(U.describeBot(bot({ target: "777", st: "c" }), ctx), "Target"), "a unit that is not listed");
assert.strictEqual(U.describeBot(bot({ name: null, role: "" }), ctx).title, "Bot");
d = U.describeCommander("Zfmagea", { x: 30, y: 40 }, ctx);
assert.strictEqual(d.title, "Zfmagea"); assert.strictEqual(row(d, "From the lead"), "50 yd");
// the box stays inside the panel
assert.deepStrictEqual(U.boxPosition(100, 100, 150, 80, 600, 320), { left: 114, top: 114 });
assert.deepStrictEqual(U.boxPosition(560, 100, 150, 80, 600, 320), { left: 396, top: 114 }, "flips left near the right edge");
assert.deepStrictEqual(U.boxPosition(100, 300, 150, 80, 600, 320), { left: 114, top: 206 }, "flips up near the bottom");
assert.deepStrictEqual(U.boxPosition(5, 5, 900, 900, 300, 200), { left: 2, top: 2 }, "never leaves the panel, even when larger");

// ---- bars, threat colours, icons
assert.deepStrictEqual(U.hpBar(bot({}), "bot"), { pct: 50, cls: "mid" });
assert.strictEqual(U.hpBar(bot({ hp: 1900 }), "bot").cls, "good"); assert.strictEqual(U.hpBar(bot({ hp: 100 }), "bot").cls, "low");
assert.strictEqual(U.hpBar(bot({ alive: false }), "bot"), null); assert.strictEqual(U.hpBar(bot({ max_hp: 0 }), "bot"), null);
assert.deepStrictEqual(U.hpBar(mob({ hp: 30 }), "mob"), { pct: 30, cls: "mob" }); assert.strictEqual(U.hpBar(mob({ hp: 0 }), "mob"), null);
assert.strictEqual(U.hpBar(mob({ hp: undefined }), "mob"), null);
const dps = bot({ name: "Dee", guid: 9, id: "9", role: "dps" });
assert.strictEqual(U.threatClass(mob({ victim: "8" }), [tank, dps]), "tank");
assert.strictEqual(U.threatClass(mob({ victim: "9" }), [tank, dps]), "other");
assert.strictEqual(U.threatClass(mob({}), [tank, dps]), "none"); assert.strictEqual(U.threatClass(mob({ victim: "123" }), [tank, dps]), "none");
const marks = U.targetMarks([mob({ id: "500", victim: "8" }), mob({ id: "501" }), mob({ id: "502" })], [tank, bot({ name: "Pat", guid: 9, id: "9", role: "dps", target: "501" }), bot({ guid: 10, id: "10", role: "tank", alive: false, target: "502" })]);
assert.deepStrictEqual(marks, { tank: { 500: true }, pull: { 501: true } }, "the living tank's target, and the unengaged mob a bot picked");
assert.deepStrictEqual(U.statusIcons(bot({ alive: false, cast: "x" }), "bot"), [{ icon: "skull", title: "dead" }]);
assert.deepStrictEqual(U.statusIcons(bot({ cc: "c", cast: "Frostbolt", rest: "d" }), "bot").map((i) => i.icon), ["sheep", "spark", "cup"]);
assert.deepStrictEqual(U.statusIcons(mob({ cc: "f" }), "mob").map((i) => i.icon), ["arrow"]);
assert.deepStrictEqual(U.statusIcons(mob({ rest: "d" }), "mob"), [], "a mob has no cup");
assert.deepStrictEqual(U.statusIcons(mob({ cc: "s" }), "mob").map((i) => i.icon), ["stun"]);
assert.deepStrictEqual(U.statusIcons(null, "bot"), []);

// ---- events
const bind = (o) => Object.assign({ commander: "Z", bound_at: 1000, bots: [bot({ lead: true }), tank], mobs_map: 70, mobs: [], orders: [] }, o);
const ev = U.newEvents();
assert.deepStrictEqual(U.noteEvents(ev, bind({}), 1010), [], "the first report only sets the baseline");
assert.deepStrictEqual(U.eventsOf(ev, "Z"), []);
// a death
let made = U.noteEvents(ev, bind({ bots: [bot({ lead: true }), Object.assign({}, tank, { alive: false })] }), 1020);
assert.strictEqual(made.length, 1); assert.strictEqual(made[0].kind, "death"); assert.strictEqual(made[0].name, "Tanky"); assert.strictEqual(made[0].map, 70); assert.strictEqual(made[0].x, 3);
assert.deepStrictEqual(U.noteEvents(ev, bind({ bots: [bot({ lead: true }), Object.assign({}, tank, { alive: false })] }), 1022), [], "a death is told once");
// a wipe
made = U.noteEvents(ev, bind({ bots: [bot({ lead: true, alive: false }), Object.assign({}, tank, { alive: false })] }), 1030);
assert.deepStrictEqual(made.map((e) => e.kind), ["death", "wipe"]);
assert.deepStrictEqual(U.noteEvents(ev, bind({ bots: [bot({ lead: true, alive: false }), Object.assign({}, tank, { alive: false })] }), 1032), [], "a wipe is told once");
U.noteEvents(ev, bind({}), 1040); // everyone alive again
// a boss that goes down: seen, then gone while nearly dead
const B = mob({ id: "900", name: "Revelosh", rank: "b", hp: 60 });
U.noteEvents(ev, bind({ mobs: [B] }), 1050);
U.noteEvents(ev, bind({ mobs: [Object.assign({}, B, { hp: 12 })] }), 1055);
made = U.noteEvents(ev, bind({ mobs: [] }), 1060);
assert.strictEqual(made.length, 1); assert.strictEqual(made[0].kind, "boss"); assert.strictEqual(made[0].name, "Revelosh"); assert.strictEqual(made[0].map, 70);
// a boss that merely leaves sight at high health is not a kill
const C = mob({ id: "901", name: "Other", rank: "b", hp: 90 });
U.noteEvents(ev, bind({ mobs: [C] }), 1070);
assert.deepStrictEqual(U.noteEvents(ev, bind({ mobs: [] }), 1075), []);
// a boss at zero health in the list is a kill
U.noteEvents(ev, bind({ mobs: [Object.assign({}, B, { id: "902", hp: 50 })] }), 1080);
assert.strictEqual(U.noteEvents(ev, bind({ mobs: [Object.assign({}, B, { id: "902", hp: 0 })] }), 1082)[0].kind, "boss");
// a boss that vanished during a wipe is not counted
const w = U.newEvents();
const dead2 = [bot({ lead: true }), tank];
U.noteEvents(w, bind({ commander: "W", mobs: [Object.assign({}, B, { hp: 20 })] }), 2000);
const none = [bot({ lead: true, alive: false }), Object.assign({}, tank, { alive: false })];
made = U.noteEvents(w, bind({ commander: "W", bots: none, mobs: [] }), 2010);
assert.deepStrictEqual(made.map((e) => e.kind), ["death", "death", "wipe"]);
// objects opened / looted: only orders that arrive after the first report, and only accepted ones
const o = U.newEvents();
const old = { at: 900, verb: "open", detail: "entry:5", ok: true, result: "party 1" };
U.noteEvents(o, bind({ commander: "O", orders: [old] }), 3000);
assert.deepStrictEqual(U.eventsOf(o, "O"), [], "an order from before this page started watching is not an event");
const fresh = [old, { at: 3005, verb: "open", detail: "entry:1234", ok: true, result: "party 1" }, { at: 3006, verb: "open", detail: "entry:9", ok: false, result: "x" },
  { at: 3007, verb: "loot", detail: "entry:77", ok: true, result: "party 1" }, { at: 3008, verb: "move", detail: "1 2", ok: true, result: "party 5" }];
made = U.noteEvents(o, bind({ commander: "O", orders: fresh }), 3010);
assert.deepStrictEqual(made.map((e) => [e.kind, e.name]), [["open", "object 1234"], ["loot", "object 77"]]);
assert.strictEqual(made[0].t, 3005);
assert.deepStrictEqual(U.noteEvents(o, bind({ commander: "O", orders: fresh }), 3012), [], "told once");
// bounds, pins, pruning
const big = U.newEvents();
U.noteEvents(big, bind({ commander: "B" }), 1);
for (let i = 0; i < 200; i++) { U.noteEvents(big, bind({ commander: "B", bots: [bot({ lead: true, alive: i % 2 === 1 })] }), 2 + i); }
assert.ok(U.eventsOf(big, "B").length <= 80, "the list is capped");
assert.strictEqual(U.pinsOn(U.eventsOf(big, "B"), 70, 5).length, 5); assert.strictEqual(U.pinsOn(U.eventsOf(big, "B"), 71, 5).length, 0, "other map");
assert.strictEqual(U.pinsOn([{ map: 70, x: null, y: 1 }], 70).length, 0, "an event without a position is not pinned");
U.pruneEvents(big, ["Z"]);
assert.deepStrictEqual(U.eventsOf(big, "B"), []);
const ids = U.eventsOf(ev, "Z").map((e) => e.id);
assert.strictEqual(new Set(ids).size, ids.length, "event ids are unique");

// ---- the feed and the run line
assert.strictEqual(U.clockText(65), "01:05"); assert.strictEqual(U.clockText(3725), "1:02:05"); assert.strictEqual(U.clockText(-4), "00:00");
assert.deepStrictEqual(U.feedLine({ t: 1065, kind: "boss", name: "Revelosh" }, 1000), { time: "01:05", text: "Boss down: Revelosh", kind: "boss" });
assert.strictEqual(U.feedLine({ t: 1, kind: "death", name: "A" }, 1000).time, "", "an event before the start has no run time");
assert.strictEqual(U.feedLine({ t: 5, kind: "wipe" }, 0).text, "Wipe: the whole party is down");
assert.strictEqual(U.feedLine({ t: 5, kind: "open", name: "object 4" }, 0).text, "Opened object 4");
assert.strictEqual(U.feedLine({ t: 5, kind: "loot", name: "" }, 0).text, "Looted a corpse or chest");
assert.strictEqual(U.describeEvent({ t: 1065, kind: "death", name: "Tanky" }, 1000).sub, "at 01:05 into the run");

const r = U.newEvents();
U.noteEvents(r, bind({ commander: "R", bound_at: 1000, mobs: [mob({ id: "1", name: "Baelog", rank: "b", hp: 100 }), mob({ id: "2", name: "Olaf", rank: "b", hp: 100 })] }), 1100);
let info = U.runInfo(bind({ commander: "R", bound_at: 1000 }), r, 1100);
assert.strictEqual(info.elapsed, 100); assert.strictEqual(info.text, "Run 01:40 · bosses 0 / 2 seen · next: Baelog"); assert.strictEqual(info.next, "Baelog");
U.noteEvents(r, bind({ commander: "R", mobs: [mob({ id: "1", name: "Baelog", rank: "b", hp: 10 }), mob({ id: "2", name: "Olaf", rank: "b", hp: 100 })] }), 1110);
U.noteEvents(r, bind({ commander: "R", mobs: [mob({ id: "2", name: "Olaf", rank: "b", hp: 100 })] }), 1120);
info = U.runInfo(bind({ commander: "R", bound_at: 1000 }), r, 1130);
assert.deepStrictEqual(info.down, ["Baelog"]); assert.strictEqual(info.text, "Run 02:10 · bosses 1 / 2 seen · next: Olaf");
// no boss seen: just the timer; no module start time: from the first report here
assert.strictEqual(U.runInfo(bind({ commander: "Q", bound_at: 0 }), U.newEvents(), 50).text, "Run 00:00");
const f = U.newEvents(); U.noteEvents(f, bind({ commander: "F", bound_at: 0 }), 500);
assert.strictEqual(U.runInfo(bind({ commander: "F", bound_at: 0 }), f, 560).elapsed, 60);
assert.strictEqual(U.firstSeen(f, "F"), 500);
// every boss killed: the last one is named
const g = U.newEvents();
U.noteEvents(g, bind({ commander: "G", mobs: [mob({ id: "1", name: "Last", rank: "b", hp: 20 })] }), 10);
U.noteEvents(g, bind({ commander: "G", mobs: [] }), 20);
assert.strictEqual(U.runInfo(bind({ commander: "G", bound_at: 0 }), g, 30).text, "Run 00:20 · bosses 1 / 1 seen · last: Last");
console.log("unitinfo.js ok");
