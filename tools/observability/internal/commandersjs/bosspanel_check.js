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
}), ev, 0, 50000, "snake");
assert.deepStrictEqual(p.auras, [{ id: 24169, name: "Tiger Form", stacks: 2, leftMs: 12000 }, { id: 1234, name: "Passive", stacks: 1, leftMs: null }]);
assert.deepStrictEqual(p.flagWords, ["untargetable", "flying"]);
assert.strictEqual(p.adds[0].entry, 14884);
assert.strictEqual(p.adds[0].alive, true);
assert.strictEqual(p.abilities.find((a) => a.sid === 23861).seen, true);
assert.strictEqual(p.abilities.find((a) => a.sid === 23861).lastMs, 20000);
assert.strictEqual(p.abilities.find((a) => a.sid === 23861).casting.frac, 0.5);
const freshCast = B.model(T, Object.assign({}, mob, { cid: 23861, cleft: 1500, ctot: 3000 }), [], 0, 50000, "snake");
assert.strictEqual(freshCast.abilities.find((a) => a.sid === 23861).seen, true, "a cast the report carries is a sighting");
assert.strictEqual(freshCast.abilities.find((a) => a.sid === 23861).lastMs, 0);
assert.strictEqual(B.model(T, Object.assign({}, mob, { cid: 23861, cleft: 1500, ctot: 3000, ctargetName: "Healbot" }), [], 0, 50000, "snake").abilities.find((a) => a.sid === 23861).casting.target, "Healbot", "the cast names its target");
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

// ---- marked: from a mark flag in the table, never a spell id in the page --------------------------------------
const markTable = B.normalize({ bosses: [{ entry: 1, abilities: [{ spell: 24314, name: "Threatening Gaze", flags: "aura;mark" }] }] });
assert.deepStrictEqual(B.markIds(markTable), { 24314: "Threatening Gaze" });
assert.strictEqual(B.markedBy(markTable, { db: [{ id: 24314, n: "Threatening Gaze" }] }), "Threatening Gaze");
assert.strictEqual(B.markedBy(markTable, { db: [{ id: 1, n: "Sunder Armor" }] }), "");

// ---- missing table / unknown boss: no panel rather than a guess ---------------------------------------------
assert.strictEqual(B.model(null, mob, ev, 0, 1), null);
assert.strictEqual(B.render(null, p), "");
assert.strictEqual(B.render(B.normalize({ bosses: [] }), p), "");

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
const moamStone = B.model(REAL, { entry: 15340, hp: 90, au: [{ id: 25685 }] }, [], 0, 0, "fight");
assert.strictEqual(moamStone.phase, "stone");
assert.strictEqual(moamStone.abilities.find((a) => a.sid === 15550).inPhase, false, "Trample stops in stone form");
assert.strictEqual(moamStone.abilities.find((a) => a.sid === 25676).inPhase, false, "Drain Mana stops in stone form");
assert.strictEqual(moamStone.abilities.find((a) => a.sid === 25685).inPhase, true, "Energize is the stone form");
const buru = aq20["15370"];
assert.deepStrictEqual(buru.phases.map((x) => x.rule), ["hp<20", "start"]);
assert.strictEqual(B.phaseOf(buru, { hp: 19, au: [] }).phase, "phase2");
assert.strictEqual(B.phaseOf(buru, { hp: 50, au: [] }).phase, "phase1");
const buruTwo = B.model(REAL, { entry: 15370, hp: 19, au: [] }, [], 0, 0, "phase1");
assert.strictEqual(buruTwo.abilities.find((a) => a.sid === 96).inPhase, false, "Dismember is phase 1");
assert.strictEqual(buruTwo.abilities.find((a) => a.sid === 25640).inPhase, false, "Thorns is phase 1");
assert.strictEqual(buruTwo.abilities.find((a) => a.sid === 20512).inPhase, true, "Creeping Plague is phase 2");
// Its first-cast timer starts at the phase change, not at the pull: 200 s in, an unseen plague is "—", never "(late)".
const buruLate = B.model(REAL, { entry: 15370, hp: 19, au: [] }, [], 0, 200000, "phase1");
const plague = buruLate.abilities.find((a) => a.sid === 20512);
assert.strictEqual(plague.inPhase, true);
assert.strictEqual(plague.next, null, "a phase-2 timer has no window counted from the pull");
const plagueRow = B.render(REAL, buruLate, {}).split("<li").find((row) => row.includes("Creeping Plague"));
assert.ok(plagueRow && !plagueRow.includes("(late)"), plagueRow);
const ayamiss = aq20["15369"];
assert.deepStrictEqual(ayamiss.phases.map((x) => x.rule), ["hp<70", "start"]);
assert.strictEqual(B.phaseOf(ayamiss, { hp: 60, au: [] }).phase, "p2");
assert.strictEqual(B.phaseOf(ayamiss, { hp: 80, au: [] }).phase, "p1");
const ayamissGround = B.model(REAL, { entry: 15369, hp: 60, au: [] }, [], 0, 0, "p1");
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
console.log("boss panel ok");
