"use strict";
// Run by commandersjs_test.go under node: the page's boss pieces (web/static/js/app.js) fed with the fields a newer module
// sends — raid-frame debuff chips and the MC tag, the killing spell in a death recap (and the labelled 3 s heuristic when the
// hit carries none), the Boss lane of the timeline, the phase word and hollow body on the map, and a bot's debuff dot, MC
// ring and mark eye. The boss table has one row per spell; no spell id lives in the page.
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");

const appPath = path.resolve(process.argv[2]), dir = path.dirname(appPath);
const css = fs.readFileSync(path.resolve(process.argv[3]), "utf8");
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
["commanders.js", "unitinfo.js", "fightreplay.js", "fightdebug.js", "bosspanel.js"].forEach((f) => {
  vm.runInContext(fs.readFileSync(path.join(dir, f), "utf8"), ctx, { filename: f });
});
ctx.BP = ctx.BossPanel;
ctx.BOSS_TABLE = ctx.BossPanel.normalize({ bosses: [
  { entry: 14834, name: "Hakkar", map: 309, abilities: [
    { spell: 24315, name: "Blood Siphon", first: 90000, repeatMin: 90000, repeatMax: 90000, flags: "t=victim" },
    { spell: 24328, name: "Corrupted Blood", first: 15000, repeatMin: 14000, repeatMax: 16000, flags: "aura" },
  ], phases: [{ name: "p1", rule: "hp<100", order: 0 }], adds: [], notes: [] },
  { entry: 14507, name: "Venoxis", map: 309, abilities: [{ spell: 23861, name: "Poison Cloud", first: 0, repeatMin: 0, repeatMax: 0, flags: "ground" }],
    phases: [{ name: "snake", rule: "hp<50", order: 0 }, { name: "p1", rule: "hp<100", order: 1 }], adds: [], notes: [] },
  { entry: 11382, name: "Mandokir", map: 309, abilities: [{ spell: 24314, name: "Threatening Gaze", first: 33000, repeatMin: 0, repeatMax: 0, flags: "aura;mark" }],
    phases: [], adds: [], notes: [] },
] });
vm.runInContext([grab("esc"), grabConst("CMD_MAP_ICONS"), grab("cmdMapIcon"), grab("cmdMobOverlaySvg"), grab("cmdMobLook"),
  grab("cmdMobSvg"), grab("fdFramesHtml"), grab("fdTimelineHtml"), grab("fdRecapHtml"), grab("bossBotMarks")].join("\n")
  + "\nvar cmdBossTable = () => ctxBossTable(); var BossPanel = BP;", ctx);
ctx.ctxBossTable = () => ctx.BOSS_TABLE;

const run = (expr, vars) => { if (vars) Object.assign(ctx, vars); return vm.runInContext(expr, ctx); };

// ---- raid frames: the boss's debuffs as dispel-coloured chips, the dispel border and the red MC tag ------------------
const frames = run(`FightDebug.raidFrames({ bots: [
  { name: "Healbot", guid: 7, role: "healer", class: "priest", hp: 80, max_hp: 100, power: 50, max_power: 100, power_type: "mana", alive: true,
    db: [{ id: 24328, n: "Corrupted Blood", st: 2, l: 8000, dt: "d" }, { id: 24314, n: "Threatening Gaze", l: 3000, dt: "" }], mc: 1 },
  { name: "Tankie", guid: 8, role: "tank", class: "warrior", hp: 100, max_hp: 100, power: 100, max_power: 100, power_type: "rage", alive: true },
] })`);
assert.strictEqual(frames[0].tiles[0].debuffs.length, 2);
assert.strictEqual(frames[0].tiles[0].debuffs[0].dt, "d");
assert.strictEqual(frames[0].tiles[0].mc, true);
const framesHtml = run("fdFramesHtml(frames, null)", { frames: frames });
assert.ok(framesHtml.includes('class="fd-db dt-disease"'), framesHtml);
assert.ok(framesHtml.includes("Corrupted Blood ×2 8s"), "the chip carries the stacks and the seconds left: " + framesHtml);
assert.ok(framesHtml.includes('class="fd-mc">MC<'), "the MC tag");
assert.ok(/class="fd-tile r-healer[^"]*hasdispel/.test(framesHtml) && /class="fd-tile r-healer[^"]*mc/.test(framesHtml), "the dispel border and the MC class: " + framesHtml);
assert.ok(/class="fd-tile r-tank"/.test(framesHtml) && !/r-tank[^"]*hasdispel/.test(framesHtml), "a clean tile keeps no border");

// ---- death recap: a hit's spell is named; the heuristic is only used when no hit names one ---------------------------
const SPELL_FIGHT = {
  dur_ms: 20000, bots: [{ name: "Healbot", guid: 7, role: "healer" }], log: [],
  deaths: [{ name: "Healbot", t_ms: 9000, x: 1, y: 2, killer: "Hakkar", hits: [{ a: "Hakkar", d: 1240, t_ms: 8500, s: "Corrupted Blood", sid: 24328 }] }],
};
const spellRecaps = run("FightDebug.deathRecaps(f)", { f: SPELL_FIGHT });
assert.strictEqual(spellRecaps[0].spell.name, "Corrupted Blood");
assert.strictEqual(spellRecaps[0].likely, false);
assert.strictEqual(spellRecaps[0].hits[0].s, "Corrupted Blood", "the hit keeps its spell");
const spellHtml = run("fdRecapHtml(recaps, 0)", { recaps: spellRecaps });
assert.ok(spellHtml.includes("killed by Hakkar's <b>Corrupted Blood</b>"), spellHtml);
assert.ok(!spellHtml.includes("likely"), "a named spell needs no guess");
assert.ok(spellHtml.includes("<th>Spell</th>"), "the recap table names the spell column");
assert.ok(spellHtml.includes("<td>Corrupted Blood</td>"), "the hit's spell is in its own column: " + spellHtml);

const HEURISTIC_FIGHT = {
  dur_ms: 20000, bots: [{ name: "Healbot", guid: 7, role: "healer" }],
  log: [
    { k: "cast", t: 5000, n: "Hakkar", s: "Blood Siphon", sid: 24315, tg: "7", tn: "Healbot" },
    { k: "cast", t: 8000, n: "Hakkar", s: "Blood Siphon", sid: 24315, tg: "7", tn: "Healbot" },
    { k: "cast", t: 8900, n: "Hakkar", s: "Cause Insanity", sid: 24327, tg: "8" },
  ],
  deaths: [{ name: "Healbot", t_ms: 9000, x: 1, y: 2, killer: "Hakkar", hits: [{ a: "Hakkar", d: 900, t_ms: 8800 }] }],
};
const guessed = run("FightDebug.deathRecaps(f)", { f: HEURISTIC_FIGHT });
assert.strictEqual(guessed[0].spell.name, "Blood Siphon", "the newest cast in 3 s aimed at the bot: " + JSON.stringify(guessed[0].spell));
assert.strictEqual(guessed[0].likely, true);
const guessedHtml = run("fdRecapHtml(recaps, 0)", { recaps: guessed });
assert.ok(guessedHtml.includes("killed by Hakkar's <b>Blood Siphon</b> <span class=\"fd-likely\""), guessedHtml);
assert.ok(guessedHtml.includes("<td>Blood Siphon"), "the matched spell is the hit's SPELL cell too: " + guessedHtml);
const noGuess = run("FightDebug.deathRecaps(f)", { f: Object.assign({}, HEURISTIC_FIGHT, { log: [] }) });
assert.strictEqual(noGuess[0].spell, null);
assert.strictEqual(noGuess[0].likely, false);
assert.strictEqual(noGuess[0].hits[0].sp, "", "with no cast log the hit's spell is unknown (blank)");

// ---- the SPELL column's three states: the hit's spell, melee for a white hit, blank when unknown ----------------------
const MELEE_FIGHT = {
  dur_ms: 20000, bots: [{ name: "Healbot", guid: 7, role: "healer" }],
  log: [{ k: "cast", t: 1000, n: "Hakkar", s: "Blood Siphon", sid: 24315, tg: "7", tn: "Healbot" }],
  deaths: [{ name: "Healbot", t_ms: 9000, x: 1, y: 2, killer: "Hakkar", hits: [{ a: "Hakkar", d: 900, t_ms: 8800 }] }],
};
const meleeRecaps = run("FightDebug.deathRecaps(f)", { f: MELEE_FIGHT });
assert.strictEqual(meleeRecaps[0].hits[0].sp, "melee", "a logged fight with no matching cast is a white hit");
assert.strictEqual(meleeRecaps[0].spell, null, "melee is not a killing spell for the summary");
const meleeHtml = run("fdRecapHtml(recaps, 0)", { recaps: meleeRecaps });
assert.ok(meleeHtml.includes("<td>melee</td>"), meleeHtml);
assert.ok(meleeHtml.includes("killed by Hakkar ·"), "no spell, no guess: " + meleeHtml);
assert.deepStrictEqual(run("FightDebug.spellForHit(h, bot, names, log)", {
  h: { a: "Hakkar", d: 1, t_ms: 8800 }, bot: { name: "Healbot", guid: 7 }, names: { Healbot: true }, log: null }), null,
  "no log: unknown, never melee");
assert.strictEqual(run("FightDebug.spellForHit(h, bot, names, log)", {
  h: { a: "Healbot", d: 1, t_ms: 8800 }, bot: { name: "Healbot", guid: 7 }, names: { Healbot: true }, log: MELEE_FIGHT.log }), null,
  "a fellow bot's hit is never melee");

// ---- the timeline's Boss lane: one marker per logged cast, coloured by the table and scrubbing to its time -------------
const tl = run("FightDebug.timeline(f)", { f: HEURISTIC_FIGHT });
assert.strictEqual(tl.bossCasts.length, 3);
assert.deepStrictEqual(JSON.parse(JSON.stringify(tl.bossCasts.map((c) => [c.t_ms, c.s, c.target]))), [[5000, "Blood Siphon", "Healbot"], [8000, "Blood Siphon", "Healbot"], [8900, "Cause Insanity", ""]]);
const tlHtml = run("fdTimelineHtml(tl, recaps, null)", { tl: tl, recaps: guessed });
assert.strictEqual((tlHtml.match(/class="fd-bosscast bc-\d"/g) || []).length, 3, tlHtml);
assert.ok(tlHtml.includes('data-fd-bosscast="8000"'), "a marker scrubs to its own time");
assert.ok(tlHtml.includes("Hakkar: Blood Siphon → Healbot at 8.0 s"), "the marker says who cast what at whom");
assert.ok(/class="fd-bosscast bc-0"/.test(tlHtml), "Blood Siphon takes the table's first slot");
assert.ok(css.includes(".fd-bosscast"), "the marker is styled");

// ---- map: a hollow body and the phase word for an untouchable boss ----------------------------------------------------
const c = { f: (n) => n.toFixed(2), k: 0.5, units: {} };
const on = { bossmark: true, hostilenames: false, hpbars: false, statusicons: false, mobcast: false, facing: false, threatcolors: false };
const svg = run("cmdMobSvg(c, m, d)", { c: c, m: { id: "5", entry: 14507, name: "Venoxis", hp: 48, x: 0, y: 0, reaction: "h", rank: "b", uf: "n", phase: "snake" },
  d: { on: on, marks: { tank: {}, pull: {} }, onMap: [], botsById: {}, overlay: () => "", leftMs: 0 } });
assert.ok(/<g class="mob hostile boss ghost"/.test(svg), "an untargetable boss is hollow: " + svg.slice(0, 120));
assert.ok(/>Venoxis 48% snake<\/text>/.test(svg), "the phase word after the health: " + svg);
const flying = run("cmdMobSvg(c, m, d)", { c: c, m: { id: "5", name: "Onyxia", hp: 60, x: 0, y: 0, reaction: "h", rank: "b", uf: "h" },
  d: { on: on, marks: { tank: {}, pull: {} }, onMap: [], botsById: {}, overlay: () => "", leftMs: 0 } });
assert.ok(/boss ghost/.test(flying), "a flying boss is hollow too");
const solnius = run("cmdMobSvg(c, m, d)", { c: c, m: { id: "6", name: "Solnius", hp: 80, x: 0, y: 0, reaction: "h", rank: "b", uf: "i" },
  d: { on: on, marks: { tank: {}, pull: {} }, onMap: [], botsById: {}, overlay: () => "", leftMs: 0 } });
assert.ok(/boss ghost/.test(solnius), "a boss that cannot be attacked is hollow: " + solnius.slice(0, 120));
const evading = run("cmdMobSvg(c, m, d)", { c: c, m: { id: "7", name: "Broodlord", hp: 90, x: 0, y: 0, reaction: "h", rank: "b", uf: "e" },
  d: { on: on, marks: { tank: {}, pull: {} }, onMap: [], botsById: {}, overlay: () => "", leftMs: 0 } });
assert.ok(!/ghost/.test(evading), "an evading boss is not hollow");
const elite = run("cmdMobSvg(c, m, d)", { c: c, m: { id: "8", name: "Acolyte", hp: 100, x: 0, y: 0, reaction: "h", rank: "e", uf: "i" },
  d: { on: on, marks: { tank: {}, pull: {} }, onMap: [], botsById: {}, overlay: () => "", leftMs: 0 } });
assert.ok(!/ghost/.test(elite), "the hollow body stays boss-only");

// ---- a bot on the map: its first debuff as one coloured dot, the MC ring, the mark eye -------------------------------
const p = { x: 10, y: 20 };
const marks = run("bossBotMarks(b, p, 1)", { b: { name: "Healbot", db: [{ id: 24328, n: "Corrupted Blood", dt: "d" }, { id: 24314, n: "Threatening Gaze", dt: "" }], mc: 1 }, p: p });
assert.ok(/class="bdbuff dt-disease"/.test(marks), marks);
assert.ok(/class="mcring"/.test(marks), marks);
const marked = run("bossBotMarks(b, p, 1)", { b: { name: "Tankie", db: [{ id: 24314, n: "Threatening Gaze" }] }, p: p });
assert.ok(/ic-eye/.test(marked), "an aura whose table row is flagged mark draws the eye: " + marked);
assert.ok(!/bdbuff/.test(run("bossBotMarks(b, p, 1)", { b: { name: "Tankie", db: [] }, p: p })), "a clean bot has no marks");
assert.ok(css.includes(".cmd-map .mob.ghost") && css.includes(".cmd-map .mcring") && css.includes(".cmd-map .bdbuff"), "the map marks are styled");

// ---- the card carries the panel in both views and loads the table script ---------------------------------------------
assert.ok(src.includes('<section class="fd-bosses"'), "the card has the boss panel slot");
assert.ok(src.includes("cmdPaintBosses(card, cmdLiveBossPanels(bind))"), "Live paints the engaged bosses");
assert.ok(src.includes("cmdPaintBosses(card, selLive ? cmdLiveBossPanels(bind) : fight ? cmdFightBossPanels(bind, fight) : [])"), "the debugger paints the live or finished fight's bosses");
assert.ok(src.includes("BossPanel.load()"), "the page loads the committed table once");
assert.strictEqual(run("BP.model(null, { entry: 1 }, [], 0, 0)"), null, "no table: no panel and no throw");

// ---- the hover box lists the same chips -------------------------------------------------------------------------------
const botBox = ctx.UnitInfo.describeBot({ name: "Healbot", role: "healer", hp: 80, max_hp: 100, db: [{ id: 24328, n: "Corrupted Blood", st: 2, l: 8000, dt: "d" }], mc: 1, marked: "Threatening Gaze" }, {});
const botRows = JSON.stringify(botBox.rows);
assert.ok(botRows.includes("Boss debuffs") && botRows.includes("Corrupted Blood (disease) ×2 8 s"), botRows);
assert.ok(botRows.includes("Mind controlled") && botRows.includes("Marked") && botRows.includes("Threatening Gaze"), botRows);
const mobBox = ctx.UnitInfo.describeMob({ name: "Venoxis", hp: 48, rank: "b", reaction: "h", au: [{ id: 24169, n: "Snake Form", l: 12000 }], uf: "nh", phase: "snake" }, {});
const mobRows = JSON.stringify(mobBox.rows);
assert.ok(mobRows.includes("Auras") && mobRows.includes("Snake Form 12 s"), mobRows);
assert.ok(mobRows.includes("untargetable") && mobRows.includes("flying") && mobRows.includes("snake"), mobRows);

console.log("boss ui ok");

// Codex round 2: the likely-spell fallback reads only the killing attacker's casts; another mob casting at the bot in the
// last 3 s is not the killing spell.
{
  const TWO = JSON.parse(JSON.stringify(MELEE_FIGHT));
  const d0 = TWO.deaths && TWO.deaths[0];
  if (d0 && Array.isArray(TWO.log)) {
    TWO.log.push({ k: "cast", t: d0.t_ms - 500, n: "Some Other Mob", s: "Shadow Bolt", sid: 9613, tn: d0.name });
    const r = run("FightDebug.deathRecaps(f)", { f: TWO });
    assert.notStrictEqual(r[0].spell && r[0].spell.name, "Shadow Bolt", "another mob's cast is not the killing spell");
  }
}
