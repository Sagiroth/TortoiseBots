"use strict";
// Run by commandersjs_test.go under node: the bot profile's XP bar (app.js xpPct/xpView). A level-capped bot says so and draws
// no bar; below the cap the line is xp / next xp with the percent; a missing field leaves the line out instead of printing
// "undefined".
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");
const appPath = path.resolve(process.argv[2]);
const src = fs.readFileSync(appPath, "utf8");

function grab(name) {
  const re = new RegExp(`^  function ${name}\\(`, "m"), m = re.exec(src);
  assert.ok(m, `${name} not found`);
  const end = src.indexOf("\n  }\n", m.index);
  return src.slice(m.index, end + 4);
}

const ctx = { console };
vm.createContext(ctx);
vm.runInContext([grab("xpPct"), grab("xpView"), grab("fmtXpRate")].join("\n"), ctx);
const run = (expr, vars) => { if (vars) Object.assign(ctx, vars); return vm.runInContext(expr, ctx); };
const plain = (v) => JSON.parse(JSON.stringify(v));

// ---- a level-60 bot: words, no bar, whatever the wire carries --------------------------------------------------------
assert.deepStrictEqual(plain(run("xpView({ xp: undefined, next_xp: 217400 }, 60, 60)")), { text: "Level 60 (max)", pct: null },
  "the cap is named, not divided by a next-level number");
assert.deepStrictEqual(plain(run("xpView({ xp: 0, next_xp: 0 }, 60, 60)")), { text: "Level 60 (max)", pct: null });
assert.deepStrictEqual(plain(run("xpView({ xp: 123, next_xp: 217400 }, 60, 60)")), { text: "Level 60 (max)", pct: null },
  "no XP bar at the cap, even with numbers");
assert.deepStrictEqual(plain(run("xpView(null, 60, 60)")), { text: "Level 60 (max)", pct: null },
  "a capped offline bot still says it");

// ---- below the cap: the numbers in one line --------------------------------------------------------------------------
const below = run("xpView({ xp: 100, next_xp: 200, xp_per_hour: 0 }, 59, 60)");
assert.deepStrictEqual(plain(below), { text: "100 / 200 XP (50%)", pct: 50 });
assert.strictEqual(run("xpView({ xp: 100, next_xp: 200, xp_per_hour: 3600 }, 59, 60)").text, "100 / 200 XP (50%) · 3.6k/h");
assert.strictEqual(run("xpView({ xp: 300, next_xp: 200 }, 59, 60)").pct, 100, "a wire value over the bar is clamped");

// ---- no data: no line, no "undefined" --------------------------------------------------------------------------------
assert.strictEqual(run("xpView({ xp: undefined, next_xp: 217400 }, 59, 60)"), null, "a missing xp leaves the line out");
assert.strictEqual(run("xpView({ xp: 100 }, 59, 60)"), null, "a missing next xp leaves the line out");
assert.strictEqual(run("xpView({ xp: 100, next_xp: 0 }, 59, 60)"), null, "next xp 0 is no data");
assert.strictEqual(run("xpView(null, 59, 60)"), null, "an offline bot below the cap has no XP line");
assert.strictEqual(run("xpView({ xp: 1, next_xp: 2 }, null, 60)").text, "1 / 2 XP (50%)", "numbers without a level still draw the line");

assert.strictEqual(run("xpPct({ xp: undefined, next_xp: 217400 })"), null, "xpPct never invents 0% from a missing xp");
assert.strictEqual(run("xpPct({ xp: 50, next_xp: 100 })"), 50);

// ---- the page draws through it ---------------------------------------------------------------------------------------
assert.ok(/const v = xpView\(live, level, cap\)/.test(src), "profileBars renders the XP line through xpView");
assert.ok(/armoryXpBar\.style\.display = v && v\.pct !== null/.test(src), "the bar is hidden when there is no fraction to draw");

console.log("profile ui ok");

// ---- the profile header draws through it: a level-60 bot gets words and no bar, a missing field hides the line -------------
const barCtx = { console };
vm.createContext(barCtx);
vm.runInContext([
  grab("xpPct"), grab("xpView"), grab("fmtXpRate"), grab("fmtNum"), grab("profileBars"),
  "var el = { armoryHpBar: { style: {} }, armoryHpText: { textContent: '' }, armoryPowBar: { style: {} }, armoryPowText: { textContent: '' },",
  "  armoryXpWrap: { style: {} }, armoryXpBar: { style: {}, className: '' }, armoryXpText: { textContent: '' } };",
  "var state = { serverInfo: { max_level: 60 } };",
  "var powerLabel = () => 'Mana';",
].join("\n"), barCtx);
const draw = (live, profile) => vm.runInContext("profileBars(p, live)", Object.assign(barCtx, { p: profile || {}, live }));
const read = () => JSON.parse(JSON.stringify({
  wrap: barCtx.el.armoryXpWrap.style.display,
  bar: barCtx.el.armoryXpBar.style.display,
  width: barCtx.el.armoryXpBar.style.width,
  text: barCtx.el.armoryXpText.textContent,
}));

draw({ level: 60, hp: 10, max_hp: 10, xp: undefined, next_xp: 217400 });
assert.deepStrictEqual(read(), { wrap: "", bar: "none", text: "Level 60 (max)" },
  "a capped bot: the words, no bar, no undefined");
draw({ level: 59, hp: 10, max_hp: 10, xp: 100, next_xp: 200, xp_per_hour: 0 });
assert.deepStrictEqual(read(), { wrap: "", bar: "", width: "50%", text: "100 / 200 XP (50%)" }, "below the cap: the numbers and the bar");
draw({ level: 59, hp: 10, max_hp: 10, next_xp: 217400 });
assert.deepStrictEqual(read(), { wrap: "none", bar: "none", width: "50%", text: "100 / 200 XP (50%)" },
  "a missing xp hides the line and its bar, keeping the last text (never redrawn as undefined)");
draw(null, { summary: { level: 60 } });
assert.deepStrictEqual(read(), { wrap: "", bar: "none", width: "50%", text: "Level 60 (max)" }, "an offline capped bot says it too");
