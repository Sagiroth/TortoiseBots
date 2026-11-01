"use strict";
// Run by commandersjs_test.go under node: the boss abilities panel's pure half (web/static/js/bosspanel.js), fed with the
// Venoxis/Jeklik rows the spec names. The next window comes from the table's timers, an ability in another phase is hidden,
// a cast event matches by spell id even when its name differs, an hp phase latches and an aura rule listed first wins.
const assert = require("assert");
const path = require("path");
const B = require(path.resolve(process.argv[2]));

// The converter's shape: one boss per entry, the CSV's a/b/c/d named.
const DATA = {
  bosses: [
    {
      entry: 14507, name: "Venoxis", map: 309, order: 0,
      abilities: [
        { spell: 23861, name: "Poison Cloud", first: 10000, repeatMin: 15000, repeatMax: 20000, flags: "p=snake;ground" },
        { spell: 8269, name: "Frenzy", first: 0, repeatMin: 0, repeatMax: 0, flags: "p=snake;hp20" },
        { spell: 23859, name: "Dispell", first: 0, repeatMin: 0, repeatMax: 0, flags: "" },
        { spell: 23966, name: "Bat Form", first: 0, repeatMin: 0, repeatMax: 0, flags: "p=bat;self" },
      ],
      phases: [
        { name: "snake", rule: "hp<50", order: 0 },
        { name: "p1", rule: "hp<100", order: 1 },
      ],
      adds: [{ entry: 14884, role: "summon", priority: 0 }],
      notes: ["Poison Cloud needs the snake phase."],
    },
    {
      entry: 14517, name: "Jeklik", map: 309, order: 1,
      abilities: [{ spell: 23966, name: "Bat Form", first: 0, repeatMin: 0, repeatMax: 0, flags: "self" }],
      phases: [{ name: "bat", rule: "aura=23966", order: 0 }, { name: "troll", rule: "hp<50", order: 1 }],
      adds: [],
      notes: [],
    },
  ],
};
const T = B.normalize(DATA);
const venoxis = B.bossFor(T, 14507);
const jeklik = B.bossFor(T, 14517);

// ---- the table loads from either the converter's object or raw CSV-shaped rows --------------------------------
assert.strictEqual(T.bosses.length, 2);
assert.strictEqual(B.normalize({ "14507": { name: "Venoxis", abilities: [] } }).bosses[0].entry, 14507, "a table keyed by entry loads too");
assert.strictEqual(B.normalize({ entry: 14507, name: "Venoxis", abilities: [] }).bosses[0].name, "Venoxis", "a single boss object loads too");
assert.strictEqual(venoxis.name, "Venoxis");
assert.deepStrictEqual(venoxis.abilities[0].flags, { phase: "snake", ground: true });
assert.strictEqual(B.bossFor(T, "14507").entry, 14507, "an entry from JSON is matched as a number");
const raw = B.normalize({ bosses: [{ entry: 1, abilities: [{ key: "24314", a: 33000, b: 0, c: 0, d: "aura;mark" }], phases: [{ key: "p1", a: "hp<100", b: 0 }], adds: [{ key: "14988", a: "linked", b: 0 }] }] });
assert.deepStrictEqual(raw.bosses[0].abilities[0], { sid: 24314, name: "spell 24314", first: 33000, repeatMin: 0, repeatMax: 0, rawFlags: "aura;mark", flags: { aura: true, mark: true }, source: "" });
assert.strictEqual(raw.bosses[0].phases[0].rule, "hp<100");
assert.strictEqual(raw.bosses[0].adds[0].entry, 14988);

// ---- next: seen -> last + b..c; never seen and a=0 -> null (not NaN) -----------------------------------------
const mob = { id: "5", entry: 14507, name: "Venoxis", hp: 60, x: 0, y: 0, rank: "b", au: [] };
const ev = [{ k: "cast", t: 30000, sid: 23861, n: "Venoxis", s: "Poison Cloud" }];
let p = B.model(T, mob, ev, 0, 50000);
const cloud = p.abilities.find((a) => a.sid === 23861);
assert.deepStrictEqual({ lo: cloud.next.lo, hi: cloud.next.hi }, { lo: 45000, hi: 50000 });
assert.strictEqual(cloud.lastMs, 20000, "30 s seen, 50 s now");
assert.strictEqual(cloud.seen, true);
const frenzy = p.abilities.find((a) => a.sid === 8269);
assert.strictEqual(frenzy.next, null, "a=0 and never seen: no next, not NaN");
const dispel = p.abilities.find((a) => a.sid === 23859);
assert.strictEqual(dispel.next, null);
const overdue = B.model(T, mob, [], 0, 100000);
assert.ok(B.render(T, overdue, {}).includes("≈ 10 s (late)"), "a first-cast window 90 s past is late, not silently current");

// ---- phase filter: p=bat is out of phase while the phase is troll -------------------------------------------
const troll = B.model(T, { entry: 14507, hp: 40, au: [] }, [], 0, 0);
assert.strictEqual(troll.phase, "snake");
assert.strictEqual(troll.abilities.find((a) => a.sid === 23966).inPhase, false, "Bat Form is a bat-phase ability");
assert.strictEqual(troll.abilities.find((a) => a.sid === 23861).inPhase, true, "Poison Cloud is a snake-phase ability");

// ---- a cast event matches by spell id even when the localized name differs -----------------------------------
const renamed = B.model(T, { entry: 14507, hp: 60, au: [] }, [{ k: "cast", t: 1000, sid: 23861, s: "Gift des Giftes" }], 0, 5000);
assert.strictEqual(renamed.abilities.find((a) => a.sid === 23861).seen, true, "the name did not decide the match");
assert.strictEqual(renamed.abilities.find((a) => a.name === "Frenzy").seen, false);

// ---- phase evaluation: hp 60 -> p1; 49 -> snake; back to 55 with prior snake stays snake; an aura rule first wins --
assert.strictEqual(B.phaseOf(venoxis, { hp: 60 }).phase, "p1");
assert.strictEqual(B.phaseOf(venoxis, { hp: 49 }).phase, "snake");
assert.strictEqual(B.phaseOf(venoxis, { hp: 55 }, "snake").phase, "snake", "the hp<50 rule latches");
assert.strictEqual(B.phaseOf(venoxis, { hp: 49 }, "p1").phase, "snake", "a more severe hp threshold still applies after p1");
assert.strictEqual(B.phaseOf(venoxis, { hp: 100 }, "snake").phase, "snake", "a heal back does not undo the phase");
assert.strictEqual(B.phaseOf(jeklik, { hp: 100, au: [{ id: 23966 }] }).phase, "bat");
assert.strictEqual(B.phaseOf(jeklik, { hp: 40, au: [{ id: 23966 }] }).phase, "bat", "the aura rule is listed before the hp rule");
const auraFirst = { phases: [{ name: "air", rule: "flag=h", order: 0 }, { name: "p1", rule: "hp<100", order: 1 }] };
assert.strictEqual(B.phaseOf(auraFirst, { hp: 60, uf: "h" }).phase, "air");
assert.strictEqual(B.phaseOf(auraFirst, { hp: 60, uf: "" }).phase, "p1");
const ladder = { phases: [
  { name: "rune4", rule: "hp<25", order: 0 },
  { name: "rune3", rule: "hp<50", order: 1 },
  { name: "rune2", rule: "hp<75", order: 2 },
  { name: "fight", rule: "start", order: 3 },
] };
assert.strictEqual(B.phaseOf(ladder, { hp: 80 }).phase, "fight", "above the top threshold is the start phase");
assert.strictEqual(B.phaseOf(ladder, { hp: 70 }).phase, "rune2", "the 75 percent rung");
assert.strictEqual(B.phaseOf(ladder, { hp: 40 }).phase, "rune3", "the 50 percent rung");
assert.strictEqual(B.phaseOf(ladder, { hp: 20 }).phase, "rune4", "the 25 percent rung listed first wins");
assert.strictEqual(B.phaseOf(ladder, { hp: 60 }, "rune3").phase, "rune3", "a rung latches after a heal");

// ---- auras, flags, adds and the current cast ----------------------------------------------------------------
p = B.model(T, Object.assign({}, mob, {
  hp: 48, uf: "nh", au: [{ id: 24169, n: "Tiger Form", st: 2, l: 12000 }, { id: 1234, n: "Passive", l: null }],
  cid: 23861, cleft: 1500, ctot: 3000, add: [{ entry: 14884, hp: 55 }],
}), ev, 0, 50000, { priorPhase: "snake" });
assert.deepStrictEqual(p.auras, [{ id: 24169, name: "Tiger Form", stacks: 2, leftMs: 12000 }, { id: 1234, name: "Passive", stacks: 1, leftMs: null }]);
assert.deepStrictEqual(p.flagWords, ["untargetable", "flying"]);
assert.strictEqual(p.adds[0].entry, 14884);
assert.strictEqual(p.adds[0].alive, true);
assert.strictEqual(p.abilities.find((a) => a.sid === 23861).seen, true);
assert.strictEqual(p.abilities.find((a) => a.sid === 23861).lastMs, 20000);
assert.strictEqual(p.abilities.find((a) => a.sid === 23861).casting.frac, 0.5);
const freshCast = B.model(T, Object.assign({}, mob, { cid: 23861, cleft: 1500, ctot: 3000 }), [], 0, 50000, { priorPhase: "snake" });
assert.strictEqual(freshCast.abilities.find((a) => a.sid === 23861).seen, true, "a cast the report carries is a sighting");
assert.strictEqual(freshCast.abilities.find((a) => a.sid === 23861).lastMs, 0);
assert.strictEqual(B.model(T, Object.assign({}, mob, { cid: 23861, cleft: 1500, ctot: 3000, ctargetName: "Healbot" }), [], 0, 50000, { priorPhase: "snake" }).abilities.find((a) => a.sid === 23861).casting.target, "Healbot", "the cast names its target");
assert.deepStrictEqual([freshCast.abilities.find((a) => a.sid === 23861).next.lo, freshCast.abilities.find((a) => a.sid === 23861).next.hi], [65000, 70000]);
assert.strictEqual(p.abilities.find((a) => a.sid === 23861).casting.leftMs, 1500);

// ---- render: escaped, phase and ticks, unseen grey, other phases folded -------------------------------------
const html = B.render(T, p, { open: true });
assert.ok(/<details class="fd-boss-panel" data-entry="14507" open>/.test(html), html.slice(0, 200));
assert.ok(html.includes("Venoxis") && html.includes('class="fd-boss-phase">snake<'), "the phase label");
assert.ok(html.includes('class="fd-boss-tick" style="left:50%"'), "an hp<50 tick");
assert.ok(/class="fd-boss-ability unseen/.test(html), "a never-seen ability is grey");
assert.ok(html.includes("Other phases"), "an ability of another phase is folded");
assert.ok(html.includes("untargetable") && html.includes("flying"), "the flag words");
assert.ok(B.render(T, Object.assign({}, p, { flags: "v", flagWords: B.flagsText("v") }), {}).includes("not visible"), "a vanished boss says so");
assert.ok(html.includes("Poison Cloud needs the snake phase."), "the note");
const evil = B.render(T, Object.assign({}, p, { boss: { entry: 14507 }, notes: ["<img src=x>"] }), {});
assert.ok(!/<img/.test(evil) && evil.includes("&lt;img"), evil);

// ---- add links: sum wins over the data's entry link ----------------------------------------------------------
const bossRow = { id: "5", entry: 14507, rank: "b", hp: 40, x: 10, y: 10 };
const dataAdd = { id: "7", entry: 14884, hp: 80, x: 20, y: 20 };
const summoned = { id: "8", entry: 99999, hp: 80, x: 30, y: 30, sum: "5" };
const links = B.addLinks(T, [bossRow, dataAdd, summoned]);
assert.strictEqual(links.length, 2);
assert.strictEqual(links.find((l) => l.from.id === "7").to.id, "5", "the data's add entry links to the boss");
assert.strictEqual(links.find((l) => l.from.id === "8").to.id, "5", "the summoner guid links an unknown add by itself");
assert.strictEqual(links.find((l) => l.from.id === "7").priority, 0);
// An add links to its boss in whatever form the boss is reported: Thekal is alive as his second entry (14509).
const ZG = B.normalize({ bosses: [
  { entry: 14599, name: "Thekal", map: 309, aliases: [14509], abilities: [], phases: [], notes: [],
    adds: [{ entry: 15068, role: "summon", priority: 2 }] },
] });
const thekal = { id: "5", entry: 14509, name: "Thekal", rank: "b", hp: 60 };
const tiger = { id: "9", entry: 15068, name: "Zulian Tiger", hp: 80 };
const aliasLinks = B.addLinks(ZG, [thekal, tiger]);
assert.strictEqual(aliasLinks.length, 1, "an add links to a boss reported under an alias entry");
assert.strictEqual(aliasLinks[0].to.id, "5");
assert.strictEqual(aliasLinks[0].priority, 2, "the data's kill priority is the boss's own row");
assert.strictEqual(aliasLinks[0].role, "summon");

// ---- rank: a boss-rank unit the table lists only as an add of a boss on the same map draws as an elite ------------
const RDOC = {
  maps: [{ key: "mc", mapId: 409, bosses: [57642] }, { key: "tmh", mapId: 819, bosses: [62936, 62937] }],
  bosses: [
    { entry: 57642, name: "Sorcerer-Thane Thaurissan", map: "mc", adds: [{ entry: 57643, name: "Image of Sorcerer-Thane Thaurissan", role: "summon" }] },
    { entry: 62936, name: "Rotgrowl", map: "tmh", adds: [{ entry: 62937, name: "Kodiak", role: "summon" }] },
    { entry: 62937, name: "Kodiak", map: "tmh", abilities: [{ spell: 1, name: "Swipe" }] },
  ],
};
const R = B.normalize(RDOC);
assert.strictEqual(B.rankFor(R, 409, { entry: 57643, rank: "b" }), "e", "an add of a boss on this map is an elite");
assert.strictEqual(B.rankFor(R, 819, { entry: 62937, rank: "b" }), "b", "a table boss stays a boss even as another's add");
assert.strictEqual(B.rankFor(R, 819, { entry: 57643, rank: "b" }), "b", "an add listed on another map keeps its rank");
assert.strictEqual(B.rankFor(R, 409, { entry: 99999, rank: "b" }), "b", "an entry the table does not know keeps the boss rank");
assert.strictEqual(B.rankFor(R, 409, { entry: 57643, rank: "e" }), "e");
assert.strictEqual(B.rankFor(null, 409, { entry: 57643, rank: "b" }), "b");
assert.strictEqual(B.rankFor(R, null, { entry: 57643, rank: "b" }), "b");
assert.strictEqual(B.rankFor(R, 409, null), "");
const mc = R.bosses.find((b) => b.entry === 57642);
assert.strictEqual(mc.mapId, 409, "the document's maps resolve a boss's map slug to its id");
assert.strictEqual(mc.map, null, "the slug stays the map field the sort reads");

// ---- marked: from a mark flag in the table, never a spell id in the page --------------------------------------
const markTable = B.normalize({ bosses: [{ entry: 1, abilities: [{ spell: 24314, name: "Threatening Gaze", flags: "aura;mark" }] }] });
assert.deepStrictEqual(B.markIds(markTable), { 24314: "Threatening Gaze" });
assert.strictEqual(B.markedBy(markTable, { db: [{ id: 24314, n: "Threatening Gaze" }] }), "Threatening Gaze");
assert.strictEqual(B.markedBy(markTable, { db: [{ id: 1, n: "Sunder Armor" }] }), "");

// ---- missing table / unknown boss: no panel rather than a guess ---------------------------------------------
assert.strictEqual(B.model(null, mob, ev, 0, 1), null);
assert.strictEqual(B.render(null, p), "");
assert.strictEqual(B.render(B.normalize({ bosses: [] }), p), "");

// ---- one panel per boss: an alias entry is the boss in another form, same-entry copies join the lowest guid --------
const ALIAS = B.normalize({ bosses: [
  { entry: 14599, name: "Thekal", map: 309, aliases: [14509], abilities: [], phases: [], adds: [], notes: [] },
  { entry: 15263, name: "Skeram", map: 531, aliases: [14507], abilities: [], phases: [], adds: [], notes: [] },
  { entry: 14507, name: "Venoxis", map: 309, abilities: [], phases: [], adds: [], notes: [] },
] });
assert.strictEqual(B.bossFor(ALIAS, 14509).entry, 14599, "an alias entry resolves to its boss");
assert.strictEqual(B.bossFor(ALIAS, 14507).entry, 14507, "a real boss entry wins over another boss's alias");
assert.strictEqual(B.bossFor(ALIAS, "14509").entry, 14599, "an alias from JSON is matched as a number");
const copies = B.bossRows(ALIAS, [{ id: "900", entry: 15263 }, { id: "12", entry: 15263 }, { id: "1000", entry: 15263 }]);
assert.strictEqual(copies.length, 1, "three copies of one entry are one panel");
assert.strictEqual(copies[0].row.id, "12", "the lowest guid is kept (length first, then the digits)");
assert.strictEqual(copies[0].copies, 3);
const aliased = B.bossRows(ALIAS, [{ id: "5", entry: 14509 }, { id: "4", entry: 14599 }]);
assert.strictEqual(aliased.length, 1, "an alias and its boss are one panel");
assert.strictEqual(aliased[0].boss.entry, 14599);
assert.strictEqual(aliased[0].row.id, "4", "the original spawn is the row the panel follows");
assert.strictEqual(aliased[0].copies, 1, "the alias form of the same unit adds no copy");
// A finished fight's engaged entries carry no guid: their c is the count, and an alias entry is still the same unit.
const thekalFight = B.bossRows(ALIAS, [{ entry: 14599, name: "Thekal", c: 1 }, { entry: 14509, name: "Thekal", c: 1 }]);
assert.strictEqual(thekalFight.length, 1, "Thekal's two forms are one panel");
assert.strictEqual(thekalFight[0].copies, 1, "two forms of one Thekal are not two copies");
const images3 = B.bossRows(ALIAS, [{ entry: 15263, name: "Skeram", c: 3 }]);
assert.strictEqual(images3.length, 1);
assert.strictEqual(images3[0].copies, 3, "an id-less row carries the report's count");
assert.strictEqual(B.bossRows(ALIAS, [{ entry: 15263, name: "Skeram" }])[0].copies, 1, "no count is one unit");
assert.strictEqual(B.bossRows(ALIAS, [{ entry: 15263 }, { id: "7", entry: 15263 }])[0].row.id, "7",
  "a row without a guid sorts last and never hides the original");
assert.strictEqual(B.bossRows(ALIAS, [{ id: "1", entry: 1 }]).length, 0, "a row the table does not know is dropped");
const copy3 = B.model(ALIAS, { entry: 15263, hp: 100, au: [], copies: 3 }, [], 0, 0);
assert.strictEqual(copy3.copies, 3);
assert.ok(B.render(ALIAS, copy3, {}).includes("×3"), "three in view are marked");
assert.ok(!B.render(ALIAS, B.model(ALIAS, { entry: 15263, hp: 100, au: [] }, [], 0, 0), {}).includes("×"),
  "one copy is not marked");

// The committed table (tools/boss_data.py writes rules as objects): its phases must read as rules, not "[object Object]".
const REAL = B.normalize(require(path.resolve(path.dirname(process.argv[2]), "../../data/boss_abilities.json")));
const ven = (REAL.byEntry || REAL.bosses || REAL)["14507"];
assert.deepStrictEqual(ven.phases.map((x) => x.rule), ["aura=23849", "hp<50", "start"]);
assert.strictEqual(B.phaseOf(ven, { hp: 60, au: [] }).phase, "p1");
assert.strictEqual(B.phaseOf(ven, { hp: 40, au: [] }).phase, "snake");
assert.strictEqual(B.phaseOf(ven, { hp: 80, au: [{ id: 23849 }] }).phase, "snake");

// AQ40: the Eye of C'Thun is in Dark Glare while it carries the freeze aura, and Sartura's berserk outranks her enrage.
const eye = (REAL.byEntry || REAL.bosses || REAL)["15589"];
assert.deepStrictEqual(eye.phases.map((x) => x.rule), ["aura=16245", "start"]);
assert.strictEqual(B.phaseOf(eye, { hp: 90, au: [] }).phase, "beam");
assert.strictEqual(B.phaseOf(eye, { hp: 90, au: [{ id: 16245 }] }).phase, "glare");
const sartura = (REAL.byEntry || REAL.bosses || REAL)["15516"];
assert.strictEqual(B.phaseOf(sartura, { hp: 15, au: [{ id: 8269 }] }).phase, "enraged");
assert.strictEqual(B.phaseOf(sartura, { hp: 15, au: [{ id: 8269 }, { id: 27680 }] }).phase, "berserk");
const chromaggus = (REAL.byEntry || REAL.bosses || REAL)["14020"];
assert.strictEqual(B.phaseOf(chromaggus, { hp: 15, au: [] }).phase, "fight");
assert.strictEqual(B.phaseOf(chromaggus, { hp: 15, au: [{ id: 28747 }] }).phase, "enraged");
const nefarian = (REAL.byEntry || REAL.bosses || REAL)["11583"];
assert.strictEqual(B.phaseOf(nefarian, { hp: 90, au: [{ id: 17131 }] }).phase, "air");
assert.strictEqual(B.phaseOf(nefarian, { hp: 90, au: [] }).phase, "grounded");
// The Bone Construct spawn below 20 percent is a note, not a phase: an hp<20 phase row would win the exclusive phase
// and hide every p=grounded ability, though the core keeps casting them all.
const nefLow = B.model(REAL, { entry: 11583, hp: 15, au: [] }, [], 0, 0);
assert.strictEqual(nefLow.phase, "grounded");
assert.strictEqual(nefLow.abilities.filter((a) => a.flags.phase === "grounded" && !a.inPhase).length, 0,
  "no ground ability is hidden at 15 percent");

// AQ20: every phase rule at a value inside it, and which abilities it folds.
const aq20 = REAL.byEntry || REAL.bosses || REAL;
const moam = aq20["15340"];
assert.deepStrictEqual(moam.phases.map((x) => x.rule), ["aura=25685", "start"]);
assert.strictEqual(B.phaseOf(moam, { hp: 90, au: [{ id: 25685 }] }).phase, "stone");
assert.strictEqual(B.phaseOf(moam, { hp: 90, au: [] }).phase, "fight");
const moamStone = B.model(REAL, { entry: 15340, hp: 90, au: [{ id: 25685 }] }, [], 0, 0, { priorPhase: "fight" });
assert.strictEqual(moamStone.phase, "stone");
assert.strictEqual(moamStone.abilities.find((a) => a.sid === 15550).inPhase, false, "Trample stops in stone form");
assert.strictEqual(moamStone.abilities.find((a) => a.sid === 25676).inPhase, false, "Drain Mana stops in stone form");
assert.strictEqual(moamStone.abilities.find((a) => a.sid === 25685).inPhase, true, "Energize is the stone form");
const buru = aq20["15370"];
assert.deepStrictEqual(buru.phases.map((x) => x.rule), ["hp<20", "start"]);
assert.strictEqual(B.phaseOf(buru, { hp: 19, au: [] }).phase, "phase2");
assert.strictEqual(B.phaseOf(buru, { hp: 50, au: [] }).phase, "phase1");
const buruTwo = B.model(REAL, { entry: 15370, hp: 19, au: [] }, [], 0, 0, { priorPhase: "phase1" });
assert.strictEqual(buruTwo.abilities.find((a) => a.sid === 96).inPhase, false, "Dismember is phase 1");
assert.strictEqual(buruTwo.abilities.find((a) => a.sid === 25640).inPhase, false, "Thorns is phase 1");
assert.strictEqual(buruTwo.abilities.find((a) => a.sid === 20512).inPhase, true, "Creeping Plague is phase 2");
// Its first-cast timer starts at the phase change, not at the pull: 200 s in, an unseen plague is "—", never "(late)".
const buruLate = B.model(REAL, { entry: 15370, hp: 19, au: [] }, [], 0, 200000, { priorPhase: "phase1" });
const plague = buruLate.abilities.find((a) => a.sid === 20512);
assert.strictEqual(plague.inPhase, true);
assert.strictEqual(plague.next, null, "a phase-2 timer has no window counted from the pull");
const plagueRow = B.render(REAL, buruLate, {}).split("<li").find((row) => row.includes("Creeping Plague"));
assert.ok(plagueRow && !plagueRow.includes("(late)"), plagueRow);
const ayamiss = aq20["15369"];
assert.deepStrictEqual(ayamiss.phases.map((x) => x.rule), ["hp<70", "start"]);
assert.strictEqual(B.phaseOf(ayamiss, { hp: 60, au: [] }).phase, "p2");
assert.strictEqual(B.phaseOf(ayamiss, { hp: 80, au: [] }).phase, "p1");
const ayamissGround = B.model(REAL, { entry: 15369, hp: 60, au: [] }, [], 0, 0, { priorPhase: "p1" });
assert.strictEqual(ayamissGround.phase, "p2");
assert.strictEqual(ayamissGround.abilities.find((a) => a.sid === 25748).inPhase, false, "Poison Stinger is the flying phase");
assert.strictEqual(ayamissGround.abilities.find((a) => a.sid === 25852).inPhase, true, "Lash is the ground phase");
assert.strictEqual(ayamissGround.abilities.find((a) => a.sid === 25749).inPhase, true, "Stinger Spray runs in both");
const ossirian = aq20["15339"];
assert.deepStrictEqual(ossirian.phases.map((x) => x.rule), ["aura=25176", "start"]);
assert.strictEqual(B.phaseOf(ossirian, { hp: 90, au: [{ id: 25176 }] }).phase, "strength");
assert.strictEqual(B.phaseOf(ossirian, { hp: 90, au: [] }).phase, "vulnerable");
for (const phase of ["strength", "vulnerable"]) {
  const o = B.model(REAL, { entry: 15339, hp: 90, au: phase === "strength" ? [{ id: 25176 }] : [] }, [], 0, 0);
  assert.strictEqual(o.phase, phase);
  assert.ok(o.abilities.every((a) => a.inPhase), "every Ossirian ability runs in both states");
}

// AQ40's last three: Viscidus freezes on aura 25937 (the normal spells stop, the trigger's cloud does not) and
// splits into globs when he turns invisible; Ouro is enraged on aura 26615 (Boulder is enraged-only, the ground
// spells keep going) and vanishes for his submerge (his visual aura 26063 is not a phase rule). Both invisible
// states are `flag=v` rows: the reports leave the boss out. The Bug Trio has no phase rows at all.
const viscidus = (REAL.byEntry || REAL.bosses || REAL)["15299"];
assert.deepStrictEqual(viscidus.phases.map((x) => x.rule), ["aura=25937", "flag=v", "start"]);
assert.strictEqual(B.phaseOf(viscidus, { hp: 90, au: [] }).phase, "fight");
assert.strictEqual(B.phaseOf(viscidus, { hp: 90, au: [{ id: 25937 }] }).phase, "frozen");
assert.strictEqual(B.phaseOf(viscidus, { hp: 90, uf: "v" }).phase, "split");
const frozenV = B.model(REAL, { entry: 15299, hp: 90, au: [{ id: 25937 }] }, [], 0, 0);
assert.strictEqual(frozenV.abilities.find((a) => a.sid === 25993).inPhase, false, "Poison Shock is normal-phase");
assert.strictEqual(frozenV.abilities.find((a) => a.sid === 25989).inPhase, true, "the trigger's Toxin Cloud is not phase-bound");
const normalV = B.model(REAL, { entry: 15299, hp: 90, au: [] }, [], 0, 0);
assert.strictEqual(normalV.abilities.find((a) => a.sid === 25993).inPhase, true);
assert.strictEqual(normalV.abilities.find((a) => a.sid === 25937).inPhase, true);

const ouro = (REAL.byEntry || REAL.bosses || REAL)["15517"];
assert.deepStrictEqual(ouro.phases.map((x) => x.rule), ["aura=26615", "flag=v", "start"]);
assert.strictEqual(B.phaseOf(ouro, { hp: 15, au: [{ id: 26615 }] }).phase, "enraged");
assert.strictEqual(B.phaseOf(ouro, { hp: 90, au: [{ id: 26063 }] }).phase, "fight", "the submerge visual is not a phase rule");
assert.strictEqual(B.phaseOf(ouro, { hp: 90, au: [] }).phase, "fight");
assert.strictEqual(B.phaseOf(ouro, { hp: 90, uf: "v" }).phase, "submerged", "an absent boss is the submerge phase");
assert.strictEqual(B.phaseOf(ouro, { hp: 15, au: [{ id: 26615 }, { id: 26063 }] }).phase, "enraged", "the first rule wins");
const enragedO = B.model(REAL, { entry: 15517, hp: 15, au: [{ id: 26615 }] }, [], 0, 0);
assert.strictEqual(enragedO.abilities.find((a) => a.sid === 26616).inPhase, true, "Boulder is enraged-only");
assert.strictEqual(enragedO.abilities.find((a) => a.sid === 26617).inPhase, true, "the enrage mound is enraged-only");
assert.strictEqual(enragedO.abilities.find((a) => a.sid === 26102).inPhase, true, "ground spells keep running through the enrage");
const visualO = B.model(REAL, { entry: 15517, hp: 90, au: [{ id: 26063 }] }, [], 0, 0);
assert.strictEqual(visualO.abilities.find((a) => a.sid === 26616).inPhase, false, "Boulder stays hidden outside the enrage");
assert.strictEqual(visualO.abilities.find((a) => a.sid === 26058).inPhase, true, "the mound summon is not phase-bound");
const fightO = B.model(REAL, { entry: 15517, hp: 90, au: [] }, [], 0, 0);
assert.strictEqual(fightO.abilities.find((a) => a.sid === 26616).inPhase, false);

const kri = (REAL.byEntry || REAL.bosses || REAL)["15511"];
assert.deepStrictEqual(kri.phases, []);
assert.strictEqual(B.phaseOf(kri, { hp: 90, au: [] }).phase, null);
const trio = B.model(REAL, { entry: 15511, hp: 90, au: [] }, [], 0, 0);
assert.ok(trio.abilities.length > 0 && trio.abilities.every((a) => a.inPhase), "no phase row means every row stays visible");

// Lower Karazhan: Howlfang's frenzy is the Bloodfrenzy aura, Moroes' below-50 abilities appear only under 50 percent.
const howlfang = (REAL.byEntry || REAL.bosses || REAL)["61223"];
assert.deepStrictEqual(howlfang.phases.map((x) => x.rule), ["aura=57082", "start"]);
assert.strictEqual(B.phaseOf(howlfang, { hp: 31, au: [] }).phase, "fight");
assert.strictEqual(B.phaseOf(howlfang, { hp: 31, au: [{ id: 57082 }] }).phase, "frenzy");
const howlfangFrenzy = B.model(REAL, { entry: 61223, hp: 20, au: [{ id: 57082 }] }, [], 0, 0);
assert.strictEqual(howlfangFrenzy.phase, "frenzy");
assert.strictEqual(howlfangFrenzy.abilities.find((a) => a.sid === 57076).inPhase, true,
  "an untagged ability still shows in the frenzy phase");
const moroes = (REAL.byEntry || REAL.bosses || REAL)["61225"];
assert.deepStrictEqual(moroes.phases.map((x) => x.rule), ["hp<50", "start"]);
assert.strictEqual(B.phaseOf(moroes, { hp: 60, au: [] }).phase, "fight");
assert.strictEqual(B.phaseOf(moroes, { hp: 49, au: [] }).phase, "below");
assert.strictEqual(B.phaseOf(moroes, { hp: 40 }, "below").phase, "below", "the hp<50 rule latches");
const moroesAbove = B.model(REAL, { entry: 61225, hp: 60, au: [] }, [], 0, 0);
assert.strictEqual(moroesAbove.abilities.find((a) => a.sid === 57095).inPhase, true,
  "an untagged ability is in every phase");
assert.strictEqual(moroesAbove.abilities.find((a) => a.sid === 57099).inPhase, false,
  "Shadow Blast is below-50 only");
const moroesBelow = B.model(REAL, { entry: 61225, hp: 49, au: [] }, [], 0, 0);
assert.strictEqual(moroesBelow.phase, "below");
assert.strictEqual(moroesBelow.abilities.find((a) => a.sid === 57099).inPhase, true);
assert.strictEqual(moroesBelow.abilities.find((a) => a.sid === 57100).inPhase, true);
assert.strictEqual(moroesBelow.abilities.find((a) => a.sid === 27564).inPhase, true);
assert.strictEqual(moroesBelow.abilities.find((a) => a.sid === 57097).inPhase, true,
  "Shuffle Kick runs in both phases");

// Upper Karazhan Halls: an aura phase outranks the health rule, Sanv's four health phases step down, and no Kara
// ability is p-tagged (its phases change timers, not the ability set), so every row stays listed.
const KARA = REAL.byEntry || REAL.bosses || REAL;
const gnarl = KARA["61939"];
assert.deepStrictEqual(gnarl.phases.map((x) => x.rule), ["aura=51096", "start"]);
assert.strictEqual(B.phaseOf(gnarl, { hp: 50, au: [] }).phase, "fight");
assert.strictEqual(B.phaseOf(gnarl, { hp: 50, au: [{ id: 51096 }] }).phase, "intermission");
const incant = KARA["61946"];
assert.deepStrictEqual(incant.phases.map((x) => x.rule), ["aura=51176", "hp<40", "start"]);
assert.strictEqual(B.phaseOf(incant, { hp: 90, au: [{ id: 51176 }] }).phase, "charged");
assert.strictEqual(B.phaseOf(incant, { hp: 90, au: [] }).phase, "exposed");
assert.strictEqual(B.phaseOf(incant, { hp: 35, au: [] }).phase, "devouring");
assert.strictEqual(B.model(REAL, { entry: 61946, hp: 90, au: [] }, [], 0, 0).abilities.find((a) => a.sid === 51180).inPhase,
  true, "Blizzard is listed once the seekers are gone");
const echo = KARA["61958"];
assert.deepStrictEqual(echo.phases.map((x) => x.rule), ["aura=8269", "aura=51125", "start"]);
assert.strictEqual(B.phaseOf(echo, { hp: 60, au: [{ id: 51125 }] }).phase, "shadow");
assert.strictEqual(B.phaseOf(echo, { hp: 15, au: [{ id: 51125 }, { id: 8269 }] }).phase, "enraged");
const chess = KARA["59967"];
assert.deepStrictEqual(chess.phases.map((x) => x.rule), ["aura=51228", "start"]);
assert.strictEqual(B.phaseOf(chess, { hp: 100, au: [{ id: 51228 }] }).phase, "shielded");
assert.strictEqual(B.phaseOf(chess, { hp: 100, au: [] }).phase, "fight");
// The chess pieces are their own boss entries: the panel looks a boss up by the exact engaged entry, so a King-only
// block left Bishop/Rook/Knight with no abilities. Each piece must render its own list, with names.
for (const [entry, name, spells] of [
  ["59971", "Bishop", [51212, 51213, 51214, 51215, 51216, 51217, 51218]],
  ["59970", "Rook", [51218, 51219, 51220, 51221, 51223]],
  ["59968", "Knight", [51218, 51224, 51225, 51226, 51227]],
]) {
  const block = KARA[entry];
  assert.strictEqual(block.name, name, entry + " block name");
  assert.deepStrictEqual(block.abilities.map((a) => a.sid).sort((a, b) => a - b), spells, entry + " abilities");
  const piece = B.model(REAL, { entry: Number(entry), hp: 100, au: [] }, [], 0, 0);
  assert.strictEqual(piece.boss.name, name);
  assert.strictEqual(piece.abilities.length, spells.length, entry + " model abilities");
  assert.ok(piece.abilities.every((a) => a.inPhase), entry + " lists every ability");
  assert.ok(piece.abilities.every((a) => a.name && !/^spell /.test(a.name)), entry + " names every ability");
}
const kingModel = B.model(REAL, { entry: 59967, hp: 100, au: [] }, [], 0, 0);
assert.ok(kingModel.abilities.some((a) => a.sid === 51229), "the King keeps Kings Fury");
assert.ok(!kingModel.abilities.some((a) => a.sid === 51214), "the King does not carry the Bishop's spells");
const sanv = KARA["59981"];
assert.deepStrictEqual(sanv.phases.map((x) => x.rule), ["hp<20", "hp<50", "hp<80", "start"]);
assert.strictEqual(B.phaseOf(sanv, { hp: 90 }).phase, "fight");
assert.strictEqual(B.phaseOf(sanv, { hp: 60 }).phase, "portals");
assert.strictEqual(B.phaseOf(sanv, { hp: 40 }).phase, "rift");
assert.strictEqual(B.phaseOf(sanv, { hp: 15 }).phase, "enraged");
assert.strictEqual(B.phaseOf(sanv, { hp: 40 }, "enraged").phase, "enraged", "the 20% rule latches");
const kruul = KARA["59991"];
assert.deepStrictEqual(kruul.phases.map((x) => x.rule), ["aura=51173", "start"]);
assert.strictEqual(B.phaseOf(kruul, { hp: 20, au: [{ id: 51173 }] }).phase, "enraged");
for (const entry of ["61939", "61946", "61958", "59967", "59981", "59991"]) {
  const m = B.model(REAL, { entry: Number(entry), hp: 40, au: [] }, [], 0, 0);
  assert.ok(m.abilities.length > 0 && m.abilities.every((a) => a.inPhase), entry + " keeps every ability listed");
}

// Timbermaw: the core has no script, so every boss carries only a start label; that label must hide no ability,
// and a boss whose only rows are a phase and a note still opens a panel.
const loktanag = (REAL.byEntry || REAL.bosses || REAL)["2139"];
assert.deepStrictEqual(loktanag.phases.map((x) => x.rule), ["start"]);
const lokModel = B.model(REAL, { entry: 2139, hp: 50, au: [] }, [], 0, 0);
assert.strictEqual(lokModel.phase, "fight");
assert.ok(lokModel.abilities.length === 4 && lokModel.abilities.every((a) => a.inPhase), "a start-only boss hides nothing");
const karrsh = (REAL.byEntry || REAL.bosses || REAL)["62934"];
assert.strictEqual(karrsh.abilities.length, 0);
assert.strictEqual(B.model(REAL, { entry: 62934, hp: 50, au: [] }, [], 0, 0).phase, "fight");
assert.ok(B.render(REAL, B.model(REAL, { entry: 62934, hp: 50, au: [] }, [], 0, 0), {}).includes("Karrsh the Sentinel"));
// Kodiak is Rotgrowl's add with its own spawn and the Dreamform is a separate entry: both have a start label and a
// note but no ability, and each opens a panel under its own entry.
const kodiak = (REAL.byEntry || REAL.bosses || REAL)["62937"];
assert.deepStrictEqual(kodiak.phases.map((x) => x.rule), ["start"]);
assert.strictEqual(kodiak.abilities.length, 0);
assert.strictEqual(B.model(REAL, { entry: 62937, hp: 50, au: [] }, [], 0, 0).phase, "fight");
assert.ok(B.render(REAL, B.model(REAL, { entry: 62937, hp: 50, au: [] }, [], 0, 0), {}).includes("Kodiak"));
const dreamform = (REAL.byEntry || REAL.bosses || REAL)["60699"];
assert.deepStrictEqual(dreamform.phases.map((x) => x.rule), ["start"]);
assert.strictEqual(dreamform.abilities.length, 0);
assert.strictEqual(B.model(REAL, { entry: 60699, hp: 50, au: [] }, [], 0, 0).phase, "fight");
assert.ok(B.render(REAL, B.model(REAL, { entry: 60699, hp: 50, au: [] }, [], 0, 0), {}).includes("Dreamform of Kronn"));

// Emerald Sanctum: Solnius has no sleep phase. Aura 25148 is sleep inside phase 2, the transform latches p2, and
// the phase-1 casts hide in p2 while Acid Breath and the gimmicks stay in it through the sleep.
const solnius = (REAL.byEntry || REAL.bosses || REAL)["60748"];
assert.deepStrictEqual(solnius.phases.map((x) => x.rule), ["hp<90", "start"]);
assert.strictEqual(B.phaseOf(solnius, { hp: 100, au: [] }).phase, "p1");
assert.strictEqual(B.phaseOf(solnius, { hp: 85, au: [] }).phase, "p2");
assert.strictEqual(B.phaseOf(solnius, { hp: 50, au: [{ id: 25148 }] }).phase, "p2", "sleep stays in p2");
const solP2 = B.model(REAL, { entry: 60748, hp: 85, au: [] }, [], 0, 0);
assert.strictEqual(solP2.abilities.find((a) => a.sid === 56507).inPhase, false, "Corrosive Bolt is a p1 cast");
assert.strictEqual(solP2.abilities.find((a) => a.sid === 24839).inPhase, true, "Acid Breath is a p2 cast");
const solSleep = B.model(REAL, { entry: 60748, hp: 50, au: [{ id: 25148 }] }, [], 0, 0);
assert.strictEqual(solSleep.phase, "p2", "the sleep aura does not make a phase");
assert.strictEqual(solSleep.abilities.find((a) => a.sid === 24839).inPhase, true, "Acid Breath still runs asleep");
assert.strictEqual(solSleep.abilities.find((a) => a.sid === 56510).inPhase, true, "the gimmicks still run asleep");

// Emerald Sanctum portal adds: each has its own boss block (the panel looks a boss up by exact entry), with the
// timers, targets and flags from its AI; the Sanctum Wyrm stays ordinary trash.
const addBlock = (entry) => (REAL.byEntry || REAL.bosses || REAL)[entry];
const dreamer = addBlock("60742");
assert.deepStrictEqual(dreamer.abilities.map((a) => [a.sid, a.first, a.repeatMin, a.repeatMax, a.flags.target]),
  [[56500, 10000, 10000, 10000, "random"]], "Dreamer casts Dreamstate every 10 s at a random player");
assert.strictEqual(dreamer.abilities[0].flags.aura, true);
assert.strictEqual(B.model(REAL, { entry: 60742, hp: 100, au: [] }, [], 0, 0).abilities[0].inPhase, true);
const dragonkin = addBlock("60743");
assert.deepStrictEqual(dragonkin.abilities.map((a) => [a.sid, a.first, a.repeatMin, a.repeatMax, a.flags.target, a.flags.self]),
  [[27564, 10000, 10000, 10000, "self", true]], "Dragonkin reflects on itself every 10 s");
const wyrmkin = addBlock("60745");
assert.deepStrictEqual(wyrmkin.abilities.map((a) => [a.sid, a.first, a.repeatMin, a.repeatMax, a.flags.target]),
  [[26050, 7000, 7000, 7000, "victim"], [56503, 25000, 21000, 21000, "aoe"]], "Wyrmkin: Acid Spit 7 s, Venom 25 s then 21 s");
assert.strictEqual(wyrmkin.abilities.find((a) => a.sid === 56503).flags.aura, true);
const scalebane = addBlock("60746");
assert.deepStrictEqual(scalebane.abilities.map((a) => [a.sid, a.first, a.repeatMin, a.repeatMax, a.flags.target]),
  [[19983, 6000, 6000, 6000, "victim"], [56504, 25000, 25000, 25000, "aoe"]], "Scalebane: Cleave 6 s, Intimidation 25 s");
assert.strictEqual(scalebane.abilities.find((a) => a.sid === 56504).flags.aura, true);
const suppressor = addBlock("61212");
assert.deepStrictEqual(suppressor.abilities.map((a) => [a.sid, a.first, a.repeatMin, a.repeatMax, a.flags.target]),
  [[56501, 15000, 14000, 14000, "random"]], "Suppressor: Emerald Supression 15 s then every 14 s");
assert.strictEqual(suppressor.abilities[0].flags.aura, true);
assert.strictEqual(addBlock("60744"), undefined, "Sanctum Wyrm has no boss block");

// BRD: Magmus gains War Stomp only in the hp<51 stomp phase; Fiery Burst stays in every phase.
const magmus = (REAL.byEntry || REAL.bosses || REAL)["9938"];
assert.deepStrictEqual(magmus.phases.map((x) => x.rule), ["hp<51", "start"]);
assert.strictEqual(B.phaseOf(magmus, { hp: 60, au: [] }).phase, "fight");
assert.strictEqual(B.phaseOf(magmus, { hp: 40, au: [] }).phase, "stomp");
const at60 = B.model(REAL, { entry: 9938, hp: 60, au: [] }, [], 0, 1000);
assert.strictEqual(at60.abilities.find((a) => a.sid === 24375).inPhase, false);
assert.strictEqual(at60.abilities.find((a) => a.sid === 13900).inPhase, true);
const at40 = B.model(REAL, { entry: 9938, hp: 40, au: [] }, [], 0, 1000);
assert.strictEqual(at40.abilities.find((a) => a.sid === 24375).inPhase, true);

// UBRS: Valthalak's three EventAI phases are HP thresholds; phase 3 outranks phase 2, summoning and
// Shadow Wrath stop in phase 2, and Shadow Bolt Volley only exists in phase 3.
const valthalak = (REAL.byEntry || REAL.bosses || REAL)["16042"];
assert.deepStrictEqual(valthalak.phases.map((x) => x.rule), ["hp<15", "hp<40", "start"]);
assert.strictEqual(B.phaseOf(valthalak, { hp: 10, au: [] }).phase, "phase3");
assert.strictEqual(B.phaseOf(valthalak, { hp: 30, au: [] }).phase, "phase2");
assert.strictEqual(B.phaseOf(valthalak, { hp: 90, au: [] }).phase, "phase1");
const p3 = B.model(REAL, { entry: 16042, hp: 10, au: [] }, [], 0, 1000);
assert.strictEqual(p3.phase, "phase3");
assert.strictEqual(p3.abilities.find((a) => a.sid === 27382).inPhase, true, "volley is a phase-3 ability");
assert.strictEqual(p3.abilities.find((a) => a.sid === 27249).inPhase, false, "summoning stopped in phase 2");
assert.strictEqual(p3.abilities.find((a) => a.sid === 27286).inPhase, false, "Shadow Wrath stopped in phase 2");
const p2 = B.model(REAL, { entry: 16042, hp: 30, au: [] }, [], 0, 1000);
assert.strictEqual(p2.abilities.find((a) => a.sid === 27249).inPhase, false);
assert.strictEqual(p2.abilities.find((a) => a.sid === 27286).inPhase, false, "Shadow Wrath is phase 1 only");
assert.strictEqual(p2.abilities.find((a) => a.sid === 27382).inPhase, false);
const p1 = B.model(REAL, { entry: 16042, hp: 90, au: [] }, [], 0, 1000);
assert.strictEqual(p1.abilities.find((a) => a.sid === 27249).inPhase, true);
assert.strictEqual(p1.abilities.find((a) => a.sid === 27286).inPhase, true, "Shadow Wrath is phase 1");
assert.strictEqual(p1.abilities.find((a) => a.sid === 27382).inPhase, false);

// Dungeon batch 1: Krastinov's enrage aura outranks the fight phase; Dathrohan's transform changes the entry to 10813,
// whose own block is demon-only and starts at full health.
const krastinov = (REAL.byEntry || REAL.bosses || REAL)["11261"];
assert.deepStrictEqual(krastinov.phases.map((x) => x.rule), ["aura=8269", "start"]);
assert.strictEqual(B.phaseOf(krastinov, { hp: 70, au: [] }).phase, "fight");
assert.strictEqual(B.phaseOf(krastinov, { hp: 20, au: [{ id: 8269 }] }).phase, "enraged");
const krasFight = B.model(REAL, { entry: 11261, hp: 70, au: [] }, [], 0, 0);
assert.strictEqual(krasFight.abilities.find((a) => a.sid === 8269).inPhase, false, "Enrage is hidden outside its aura phase");
const krasEnraged = B.model(REAL, { entry: 11261, hp: 20, au: [{ id: 8269 }] }, [], 0, 0);
assert.strictEqual(krasEnraged.abilities.find((a) => a.sid === 8269).inPhase, true);
assert.strictEqual(krasEnraged.abilities.find((a) => a.sid === 16509).inPhase, true, "Rend runs in every phase");

const dathrohan = (REAL.byEntry || REAL.bosses || REAL)["10812"];
assert.deepStrictEqual(dathrohan.phases.map((x) => x.rule), ["start"]);
assert.strictEqual(B.phaseOf(dathrohan, { hp: 30, au: [] }).phase, "human", "10812 is the human form for its whole life");
const dathHuman = B.model(REAL, { entry: 10812, hp: 90, au: [] }, [], 0, 0);
assert.strictEqual(dathHuman.abilities.find((a) => a.sid === 17286).inPhase, true);
assert.strictEqual(dathHuman.abilities.some((a) => a.sid === 17399), false, "Shadow Shock lives in the 10813 block");
assert.strictEqual(dathHuman.abilities.find((a) => a.sid === 17288).inPhase, true, "the hp40 transform marker stays on 10812");

const balnazzar = (REAL.byEntry || REAL.bosses || REAL)["10813"];
assert.deepStrictEqual(balnazzar.phases.map((x) => x.rule), ["start"], "the transform heals him to full, so no hp rule");
const bal = B.model(REAL, { entry: 10813, hp: 100, au: [] }, [], 0, 0);
assert.strictEqual(bal.phase, "demon");
assert.strictEqual(bal.abilities.find((a) => a.sid === 17399).inPhase, true);
assert.strictEqual(bal.abilities.find((a) => a.sid === 17287).inPhase, true, "Mind Blast runs in both forms");

// The committed table names one field per flag instead of a flags string: the panel must read those too.
const jeklikReal = (REAL.byEntry || REAL.bosses || REAL)["14517"];
assert.strictEqual(jeklikReal.abilities.find((a) => a.sid === 23919).flags.phase, "bat");
assert.strictEqual(jeklikReal.abilities.find((a) => a.sid === 23953).flags.interruptible, true);
const arlokkMark = (REAL.byEntry || REAL.bosses || REAL)["14515"].abilities.find((a) => a.sid === 24210);
assert.strictEqual(arlokkMark.flags.mark, true);
assert.strictEqual(B.markIds(REAL)[24210], "Mark of Arlokk");
assert.ok(Object.keys(B.markIds(REAL)).length > 0);
assert.ok(REAL.bosses.some((b) => b.abilities.some((a) => a.flags.summon)), "some ability summons");
const batPhase = B.model(REAL, { entry: 14517, hp: 100, au: [] }, [], 0, 0);
assert.strictEqual(batPhase.phase, "bat");
assert.strictEqual(batPhase.abilities.find((a) => a.sid === 23919).inPhase, true);
assert.strictEqual(batPhase.abilities.find((a) => a.sid === 23953).inPhase, false, "Mind Flay is a troll-phase ability");

// ---- coverage chip: only when opts.coverage names the spell, one glyph per status, and never without it --------------
const cov = { 23861: { status: "missing", title: "avoid: no row" }, 23966: { status: "notneeded", title: "self: ok: nopurge" } };
const chipHtml = B.render(T, p, { open: true, coverage: cov });
assert.ok(chipHtml.includes('<span class="fd-cov fd-cov-missing" title="avoid: no row" aria-label="coverage: missing">✗</span>'),
  "a missing chip with its need in the title");
assert.ok(chipHtml.includes('<span class="fd-cov fd-cov-notneeded"'), "a not-needed chip");
assert.strictEqual((chipHtml.match(/class="fd-cov /g) || []).length, 2, "only the answered spells get a chip");
assert.ok(!B.render(T, p, { open: true }).includes("fd-cov"), "no opts.coverage: the row is unchanged");
assert.ok(!B.render(T, p, { open: true, coverage: cov }).includes("fd-boss-cov"), "a numeric map keeps the head without a coverage link");
const venReal = B.model(REAL, { entry: 14507, hp: 60, au: [] }, [], 0, 0);
assert.ok(B.render(REAL, venReal, { open: true, coverage: cov }).includes('href="#/coverage/zulgurub?boss=14507"'),
  "the head links to the raid's coverage card");

// ---- Naxxramas phases: Noth and Gothik have no aura phase (29230 is passive and never sent), so every
// ---- ability shows; Thaddius, Sapphiron and Kel'Thuzad still hide the other side's abilities.
const noth = B.model(REAL, { entry: 15954, hp: 80, au: [] }, [], 0, 0);
assert.strictEqual(noth.phase, "ground");
assert.ok(noth.abilities.every((a) => a.inPhase), "no Noth aura phase: every ability shows");
assert.strictEqual(noth.abilities.find((a) => a.sid === 29213).inPhase, true, "Curse shows");
assert.strictEqual(noth.abilities.find((a) => a.sid === 29217).inPhase, true, "a champion summon shows");

const gothik = B.model(REAL, { entry: 16060, hp: 80, au: [] }, [], 0, 0);
assert.strictEqual(gothik.phase, "ground");
assert.ok(gothik.abilities.every((a) => a.inPhase), "no Gothik aura phase: every ability shows");
assert.strictEqual(gothik.abilities.find((a) => a.sid === 29317).inPhase, true, "Shadow Bolt shows");
assert.strictEqual(gothik.abilities.find((a) => a.sid === 28679).inPhase, true, "Harvest Soul shows");

const thaddAdds = B.model(REAL, { entry: 15928, hp: 100, au: [{ id: 28160 }] }, [], 0, 0);
assert.strictEqual(thaddAdds.phase, "adds");
assert.strictEqual(thaddAdds.abilities.find((a) => a.sid === 28125).inPhase, true, "War Stomp is an add-phase ability");
assert.strictEqual(thaddAdds.abilities.find((a) => a.sid === 28089).inPhase, false, "Polarity Shift waits for Thaddius");
const thaddFight = B.model(REAL, { entry: 15928, hp: 100, au: [] }, [], 0, 0);
assert.strictEqual(thaddFight.phase, "fight");
assert.strictEqual(thaddFight.abilities.find((a) => a.sid === 28089).inPhase, true);
assert.strictEqual(thaddFight.abilities.find((a) => a.sid === 28125).inPhase, false);

const sappAir = B.model(REAL, { entry: 15989, hp: 80, au: [], uf: "h" }, [], 0, 0);
assert.strictEqual(sappAir.phase, "air");
assert.strictEqual(sappAir.abilities.find((a) => a.sid === 28522).inPhase, true, "Icebolt is an air-phase ability");
assert.strictEqual(sappAir.abilities.find((a) => a.sid === 19983).inPhase, false, "Cleave is ground-only");
const sappGround = B.model(REAL, { entry: 15989, hp: 80, au: [], uf: "" }, [], 0, 0);
assert.strictEqual(sappGround.phase, "ground");
assert.strictEqual(sappGround.abilities.find((a) => a.sid === 19983).inPhase, true);
assert.strictEqual(sappGround.abilities.find((a) => a.sid === 28522).inPhase, false);

const ktAdds = B.model(REAL, { entry: 15990, hp: 100, au: [], uf: "i" }, [], 0, 0);
assert.strictEqual(ktAdds.phase, "adds");
assert.strictEqual(ktAdds.abilities.find((a) => a.sid === 29423).inPhase, true, "the channel is a phase-one ability");
assert.strictEqual(ktAdds.abilities.find((a) => a.sid === 28478).inPhase, false, "Frostbolt waits for phase two");
const ktFight = B.model(REAL, { entry: 15990, hp: 80, au: [], uf: "" }, [], 0, 0);
assert.strictEqual(ktFight.phase, "fight");
assert.strictEqual(ktFight.abilities.find((a) => a.sid === 28478).inPhase, true);
assert.strictEqual(ktFight.abilities.find((a) => a.sid === 29423).inPhase, false);
// ---- a spell two bosses list counts for the boss that cast it (Four Horsemen marks, Twin Emperors berserk) -----------
const SHARED = B.normalize({ bosses: [
  { entry: 1, name: "Erennius", map: 309, order: 0,
    abilities: [{ spell: 46079, name: "Call of Nightmare", first: 0, repeatMin: 7000, repeatMax: 7000, flags: "" }],
    phases: [], adds: [], notes: [] },
  { entry: 2, name: "Solnius", map: 309, order: 1,
    abilities: [
      { spell: 46079, name: "Call of Nightmare", first: 0, repeatMin: 7000, repeatMax: 7000, flags: "" },
      { spell: 99, name: "Solitary", first: 0, repeatMin: 7000, repeatMax: 7000, flags: "" },
    ],
    phases: [], adds: [], notes: [] },
] });
assert.strictEqual(SHARED.bySpellAll[46079].length, 2, "both slots list the shared spell");
const castByA = [{ k: "cast", sid: 46079, t: 5000, e: 1, n: "Erennius" }];
const rowB = B.model(SHARED, { entry: 2, hp: 100, au: [] }, castByA, 0, 10000).abilities.find((a) => a.sid === 46079);
assert.strictEqual(rowB.seen, false, "A's cast is not B's sighting");
assert.strictEqual(rowB.next, null, "unseen with no first window: no next");
const rowA = B.model(SHARED, { entry: 1, hp: 100, au: [] }, castByA, 0, 10000).abilities.find((a) => a.sid === 46079);
assert.strictEqual(rowA.seen, true);
assert.deepStrictEqual({ lo: rowA.next.lo, hi: rowA.next.hi }, { lo: 12000, hi: 12000 });
const noEntryB = B.model(SHARED, { entry: 2, hp: 100, au: [] }, [{ k: "cast", sid: 46079, t: 5000 }], 0, 10000).abilities;
assert.strictEqual(noEntryB.find((a) => a.sid === 46079).seen, true, "an old event without an entry counts everywhere");
assert.strictEqual(B.model(SHARED, { entry: 1, hp: 100, au: [] }, [{ k: "cast", sid: 46079, t: 5000 }], 0, 10000).abilities.find((a) => a.sid === 46079).seen, true);
const foreignCast = [{ k: "cast", sid: 46079, t: 5000, e: 777 }];
assert.strictEqual(B.model(SHARED, { entry: 1, hp: 100, au: [] }, foreignCast, 0, 10000).abilities.find((a) => a.sid === 46079).seen, true, "a cast by a non-boss counts on A");
assert.strictEqual(B.model(SHARED, { entry: 2, hp: 100, au: [] }, foreignCast, 0, 10000).abilities.find((a) => a.sid === 46079).seen, true, "a cast by a non-boss counts on B");
const addCast = [{ k: "cast", sid: 46079, t: 5000, e: 16428 }];
assert.strictEqual(B.model(SHARED, { entry: 1, hp: 100, au: [] }, addCast, 0, 10000).abilities.find((a) => a.sid === 46079).seen, true, "an add's cast of a shared spell counts on A (Kel'Thuzad Mortal Wound)");
assert.strictEqual(B.model(SHARED, { entry: 2, hp: 100, au: [] }, addCast, 0, 10000).abilities.find((a) => a.sid === 46079).seen, true, "an add's cast of a shared spell counts on B");
const lone = B.model(SHARED, { entry: 2, hp: 100, au: [] }, [{ k: "cast", sid: 99, t: 1000, e: 777 }], 0, 5000).abilities.find((a) => a.sid === 99);
assert.strictEqual(lone.seen, true, "an unshared spell is never filtered");
const duel = [{ k: "cast", sid: 46079, t: 5000, e: 1, tn: "Tank" }, { k: "cast", sid: 46079, t: 6000, e: 2, tn: "Healer" }];
const castMob = (entry) => ({ entry: entry, hp: 100, au: [], cid: 46079, cleft: 500, ctot: 1000 });
assert.strictEqual(B.model(SHARED, castMob(2), duel, 0, 6500).abilities.find((a) => a.sid === 46079).casting.target, "Healer");
assert.strictEqual(B.model(SHARED, castMob(1), duel, 0, 6500).abilities.find((a) => a.sid === 46079).casting.target, "Tank");
assert.strictEqual(B.abilityBySpell(SHARED, 46079, 2).boss.entry, 2);
assert.strictEqual(B.abilityBySpell(SHARED, 46079).boss.entry, 1);
assert.strictEqual(B.abilityBySpell(SHARED, 46079, 999).boss.entry, 1);

// Scarlet Monastery: the revive restores Whitemane's spell list and no hp or aura the panel can read
// separates the stages, so she has no phase row and all six spells stay visible at any health.
const white = (REAL.byEntry || REAL.bosses || REAL)["3977"];
assert.deepStrictEqual(white.phases.map((x) => x.rule), []);
const whiteBelow = B.model(REAL, { entry: 3977, hp: 40, au: [] }, [], 0, 0);
for (const sid of [14515, 12039, 22187, 9481, 9256, 9232]) {
  assert.strictEqual(whiteBelow.abilities.find((a) => a.sid === sid).inPhase, true,
    `spell ${sid} is visible at 40%`);
}


// ---- the dungeon batch's phase rules: Bladestorm latches at 5 percent, Ice Block and Banish hide the fight,
// Chronar's enrage is only a label -----------------------------------------------------------------------------
const volkan = B.bossFor(REAL, 80851);
const volkanFight = B.model(REAL, { entry: 80851, hp: 40, au: [] }, [], 0, 0);
assert.strictEqual(volkanFight.phase, "fight");
assert.strictEqual(volkanFight.abilities.find((a) => a.sid === 23931).inPhase, true, "Thunderclap in the fight phase");
assert.strictEqual(volkanFight.abilities.find((a) => a.sid === 9632).inPhase, false, "Bladestorm waits for 5 percent");
const volkanStorm = B.model(REAL, { entry: 80851, hp: 4, au: [] }, [], 0, 0);
assert.strictEqual(B.phaseOf(volkan, { hp: 4 }).phase, "bladestorm");
assert.strictEqual(volkanStorm.abilities.find((a) => a.sid === 23931).inPhase, false, "Thunderclap stops in Bladestorm");
assert.strictEqual(volkanStorm.abilities.find((a) => a.sid === 9632).inPhase, true, "Bladestorm is the last phase");
assert.strictEqual(volkanStorm.abilities.find((a) => a.sid === 7141).inPhase, false, "Blink stops in Bladestorm");
assert.strictEqual(volkanStorm.abilities.find((a) => a.sid === 23951).inPhase, false, "Bloodlust stops in Bladestorm");
assert.strictEqual(volkanFight.abilities.find((a) => a.sid === 7141).inPhase, true, "Blink is a fight-phase ability");
assert.strictEqual(volkanFight.abilities.find((a) => a.sid === 23951).inPhase, true, "Bloodlust is a fight-phase ability");

const tham = B.bossFor(REAL, 80852);
assert.strictEqual(B.phaseOf(tham, { hp: 10, au: [{ id: 11958 }] }).phase, "iceblock");
assert.strictEqual(B.phaseOf(tham, { hp: 10, au: [] }).phase, "fight");
const thamBlock = B.model(REAL, { entry: 80852, hp: 10, au: [{ id: 11958 }] }, [], 0, 0);
assert.strictEqual(thamBlock.abilities.find((a) => a.sid === 19717).inPhase, false, "Rain of Fire stops in Ice Block");
assert.strictEqual(thamBlock.abilities.find((a) => a.sid === 11958).inPhase, true, "the Ice Block aura itself stays");

const epo = B.bossFor(REAL, 65116);
assert.strictEqual(B.phaseOf(epo, { hp: 40, au: [{ id: 18647 }] }).phase, "banished");
assert.strictEqual(B.phaseOf(epo, { hp: 40, au: [] }).phase, "fight");
const epoBanished = B.model(REAL, { entry: 65116, hp: 40, au: [{ id: 18647 }] }, [], 0, 0);
assert.strictEqual(epoBanished.abilities.find((a) => a.sid === 20717).inPhase, false, "Sand Breath stops in Banish");
assert.strictEqual(epoBanished.abilities.find((a) => a.sid === 18647).inPhase, true, "the Banish aura stays");

const chronar = B.bossFor(REAL, 65113);
const chronarRage = B.model(REAL, { entry: 65113, hp: 30, au: [{ id: 28131 }] }, [], 0, 0);
assert.strictEqual(chronarRage.phase, "enraged");
assert.strictEqual(chronarRage.abilities.find((a) => a.sid === 18328).inPhase, true, "Chronar keeps casting while enraged");

// Dire Maul: Alzzin's normal/wolf/tree forms are exclusive. Each form's abilities are p= tagged, so a wolf
// hides the normal and tree rows; the aura rules are listed before the start rule.
const alzzin = REAL.byEntry || REAL.bosses || REAL;
const alzzinBoss = alzzin["11492"];
assert.deepStrictEqual(alzzinBoss.phases.map((x) => x.rule), ["aura=22688", "aura=22660", "start"]);
assert.strictEqual(B.phaseOf(alzzinBoss, { hp: 90, au: [] }).phase, "normal");
assert.strictEqual(B.phaseOf(alzzinBoss, { hp: 90, au: [{ id: 22660 }] }).phase, "wolf");
assert.strictEqual(B.phaseOf(alzzinBoss, { hp: 90, au: [{ id: 22688 }] }).phase, "tree");
const alzzinWolf = B.model(REAL, { entry: 11492, hp: 90, au: [{ id: 22660 }] }, [], 0, 0);
assert.strictEqual(alzzinWolf.phase, "wolf");
assert.strictEqual(alzzinWolf.abilities.find((a) => a.sid === 22689).inPhase, true, "Mangle is a wolf ability");
assert.strictEqual(alzzinWolf.abilities.find((a) => a.sid === 22662).inPhase, false, "Wither is normal-form");
assert.strictEqual(alzzinWolf.abilities.find((a) => a.sid === 7948).inPhase, false, "Wild Regeneration is tree-form");
const alzzinTree = B.model(REAL, { entry: 11492, hp: 90, au: [{ id: 22688 }] }, [], 0, 0);
assert.strictEqual(alzzinTree.phase, "tree");
assert.strictEqual(alzzinTree.abilities.find((a) => a.sid === 7948).inPhase, true, "Wild Regeneration is a tree ability");
assert.strictEqual(alzzinTree.abilities.find((a) => a.sid === 19319).inPhase, false, "Vicious Bite is wolf-form");
assert.strictEqual(alzzinTree.abilities.find((a) => a.sid === 10101).inPhase, true, "Knock Away is a tree ability");
// Wild Regeneration only fires below 50 % in tree form: the row carries that gate, the panel shows no
// window for it, and it is active only while the boss is under the gate and the tree aura is up.
const regen = (m) => {
  const a = m.abilities.find((x) => x.sid === 7948);
  return { hpBelow: a.hpBelow, active: a.inPhase && (a.hpBelow == null || m.hp < a.hpBelow), next: a.next };
};
const regenHigh = regen(B.model(REAL, { entry: 11492, hp: 90, au: [{ id: 22688 }] }, [], 0, 0));
assert.strictEqual(regenHigh.hpBelow, 50, "Wild Regeneration carries the below-50 % gate");
assert.strictEqual(regenHigh.next, null, "an hp-gated row has no next window");
assert.strictEqual(regenHigh.active, false, "Wild Regeneration is inactive at 90 %");
const regenLow = regen(B.model(REAL, { entry: 11492, hp: 40, au: [{ id: 22688 }] }, [], 0, 0));
assert.strictEqual(regenLow.active, true, "Wild Regeneration is active below 50 % in tree form");

// ---- anchored timers: dur= shifts the repeat window, from= counts the first window from a phase start --------------
const ANCHOR_TABLE = B.normalize({ bosses: [
  { entry: 9001, name: "Phase Boss", map: 309, order: 0,
    abilities: [{ key: "9", a: 10000, b: 30000, c: 30000, d: "p=fight;from=fight" }],
    phases: [{ key: "adds", a: "aura=1", b: 1 }, { key: "fight", a: "start", b: 2 }], adds: [], notes: [] },
  { entry: 9002, name: "Dur Boss", map: 309, order: 1,
    abilities: [{ key: "7", a: 8000, b: 5000, c: 10000, d: "dur=15000" }],
    phases: [], adds: [], notes: [] },
  { entry: 9003, name: "Plain Boss", map: 309, order: 2,
    abilities: [{ key: "8", a: 12000, b: 6000, c: 9000, d: "" }],
    phases: [], adds: [], notes: [] },
]});
const phaseRow = (m) => m.abilities.find((a) => a.sid === 9);
const seenPhase = B.model(ANCHOR_TABLE, { entry: 9001, au: [] }, [], 0, 60000, { phaseAt: { fight: 50000 } });
assert.deepStrictEqual([phaseRow(seenPhase).next.lo, phaseRow(seenPhase).next.hi], [60000, 60000],
  "from= counts the first window from the phase start");

const anchored = {};
const firstSight = B.model(ANCHOR_TABLE, { entry: 9001, au: [] }, [], 0, 60000, { phaseAt: anchored });
assert.strictEqual(firstSight.phase, "fight");
assert.strictEqual(firstSight.phaseAt.fight, 60000, "the phase is first seen now");
assert.strictEqual(firstSight.phaseAt !== anchored, true, "the caller's map is not written");
assert.strictEqual(phaseRow(firstSight).next.lo, 70000, "the first window counts from that moment");

const noPhase = B.model(ANCHOR_TABLE, { entry: 9001, au: [] }, [], 0, 60000, { phaseAt: null });
assert.strictEqual(phaseRow(noPhase).next, null, "an unseen from= phase has no first window");
assert.strictEqual(noPhase.phaseAt, null);

// A vanished boss row is a ghost: it still draws (as not visible) but never records a phase start of its own.
const ghostSeen = B.model(ANCHOR_TABLE, { entry: 9001, au: [], uf: "v", ghost: true }, [], 0, 60000, { phaseAt: {} });
assert.deepStrictEqual(ghostSeen.phaseAt, {}, "a ghost row adds no phase start");

const durCast = [{ k: "cast", t: 8000, sid: 7, n: "Dur Boss" }];
const durSeen = B.model(ANCHOR_TABLE, { entry: 9002, au: [] }, durCast, 0, 20000).abilities.find((a) => a.sid === 7);
assert.deepStrictEqual([durSeen.next.lo, durSeen.next.hi], [28000, 33000], "dur= shifts the repeat window");
const durUnseen = B.model(ANCHOR_TABLE, { entry: 9002, au: [] }, [], 0, 20000).abilities.find((a) => a.sid === 7);
assert.deepStrictEqual([durUnseen.next.lo, durUnseen.next.hi], [8000, 8000], "dur= never moves the first window");
const durFaded = B.model(ANCHOR_TABLE, { entry: 9002, au: [] }, [
  { k: "cast", t: 8000, sid: 7 }, { k: "ab", t: 12000, sid: 7 }, { k: "da", t: 15000, sid: 7 }, { k: "dr", t: 18000, sid: 7 },
], 0, 24000).abilities.find((a) => a.sid === 7);
assert.deepStrictEqual([durFaded.next.lo, durFaded.next.hi], [28000, 33000],
  "a later fade or refresh does not move a dur= window");
const durFadeOnly = B.model(ANCHOR_TABLE, { entry: 9002, au: [] }, [{ k: "da", t: 20000, sid: 7 }], 0, 24000)
  .abilities.find((a) => a.sid === 7);
assert.deepStrictEqual([durFadeOnly.next.lo, durFadeOnly.next.hi], [40000, 45000],
  "with no cast event the fade is the only sighting left");

const plainUnseen = B.model(ANCHOR_TABLE, { entry: 9003, au: [] }, [], 0, 0).abilities.find((a) => a.sid === 8);
assert.deepStrictEqual([plainUnseen.next.lo, plainUnseen.next.hi], [12000, 12000], "no flag: the pull-relative first window");
const plainSeen = B.model(ANCHOR_TABLE, { entry: 9003, au: [] }, [{ k: "cast", t: 20000, sid: 8 }], 0, 40000)
  .abilities.find((a) => a.sid === 8);
assert.deepStrictEqual([plainSeen.next.lo, plainSeen.next.hi], [26000, 29000], "no flag: the repeat window is unchanged");

const jsonFlags = B.normalize({ bosses: [{ entry: 9004, abilities: [
  { key: "7", a: 0, b: 0, c: 0, d: "", dur: 15000, from: "fight" }], phases: [] }] });
assert.deepStrictEqual(B.bossFor(jsonFlags, 9004).abilities[0].flags, { dur: 15000, from: "fight" },
  "the converter's dur/from fields reach the flags");

// The committed table's alias: Thekal keeps one panel after he rises as his second form.
assert.strictEqual(B.bossFor(REAL, 14509).entry, 14599, "the committed alias 14509 resolves to Thekal");
assert.strictEqual(B.model(REAL, { entry: 14509, hp: 40, au: [] }, [], 0, 0).boss.entry, 14599);
console.log("boss panel ok");
