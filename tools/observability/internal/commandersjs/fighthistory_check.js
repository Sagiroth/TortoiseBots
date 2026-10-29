"use strict";
// Run by commandersjs_test.go under node: the fight history's pure logic (web/static/js/fighthistory.js: the night picker's
// options, the same-pack table, its best row and medians) and the two builders app.js feeds with them (the day select and
// the pack table). Every name from the game server is escaped, a past fight opens through data-fd-open, and the Live card
// belongs to Tonight only.
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");
const histPath = path.resolve(process.argv[2]), appPath = path.resolve(process.argv[3]);
const dir = path.dirname(appPath);
const src = fs.readFileSync(appPath, "utf8");

function grab(name) {
  const re = new RegExp(`^  function ${name}\\(`, "m"), m = re.exec(src);
  assert.ok(m, `${name} not found`);
  const end = src.indexOf("\n  }\n", m.index);
  return src.slice(m.index, end + 4);
}

const ctx = { console };
vm.createContext(ctx);
vm.runInContext(fs.readFileSync(histPath, "utf8"), ctx, { filename: "fighthistory.js" });
assert.ok(ctx.FightHistory && typeof ctx.FightHistory.dayOptions === "function", "fighthistory.js loads as the global FightHistory");
vm.runInContext(fs.readFileSync(path.join(dir, "fightdebug.js"), "utf8"), ctx, { filename: "fightdebug.js" });
assert.ok(ctx.FightDebug && typeof ctx.FightDebug.railCards === "function", "fightdebug.js loads as the global FightDebug");
vm.runInContext([grab("esc"), grab("fdRailHtml"), grab("fdDayOptionsHtml"), grab("fdPackHtml")].join("\n"), ctx,
  { filename: "app.js" });

const run = (expr, vars) => { if (vars) Object.assign(ctx, vars); return vm.runInContext(expr, ctx); };
const FH = ctx.FightHistory;
// Objects made inside the vm realm have another Object prototype: compare them as plain JSON.
const plain = (v) => JSON.parse(JSON.stringify(v));

// ---- the night picker -----------------------------------------------------------------------------------------------
// 2026-10-28 is a Tuesday (the real weekday of the date); today's own file is Tonight, not a second entry.
const TODAY = Date.UTC(2026, 9, 29, 12, 0, 0);
const DAYS = { days: [
  { day: "2026-10-29", fights: 9 },
  { day: "2026-10-28", fights: 12 },
  { day: "2026-10-27", fights: 1 },
] };
assert.deepStrictEqual(plain(FH.dayOptions(DAYS, TODAY)), [
  { value: "", label: "Tonight" },
  { value: "2026-10-28", label: "Wed 28 Oct · 12 fights" },
  { value: "2026-10-27", label: "Tue 27 Oct · 1 fight" },
], "the day labels, newest first, Today as Tonight");
assert.deepStrictEqual(plain(FH.dayOptions(null, TODAY)), [{ value: "", label: "Tonight" }], "a history that is off still offers Tonight");
assert.deepStrictEqual(plain(FH.dayOptions({ days: [{ day: "2026-13-01", fights: 2 }, { day: "bad", fights: 2 }] }, TODAY)),
  [{ value: "", label: "Tonight" }], "a malformed day is not offered");
assert.deepStrictEqual(plain(FH.dayOptions({ days: [{ day: "2026-10-28", fights: 1 }, { day: "2026-10-28", fights: 4 }] }, TODAY)),
  [{ value: "", label: "Tonight" }, { value: "2026-10-28", label: "Wed 28 Oct · 1 fight" }], "a day is listed once");

// ---- the same-pack table --------------------------------------------------------------------------------------------
const at = (y, m, d, hh, mm) => Math.floor(Date.UTC(y, m - 1, d, hh, mm, 0) / 1000);
const PACK = { pack_key: "onyxias_lair#12", rows: [
  { key: "wipe", commander: "Glacienne", start: at(2026, 10, 25, 21, 5), dur: 60000, outcome: "wipe", deaths: 0, nontank_ms: null },
  { key: "a", commander: "Glacienne", start: at(2026, 10, 26, 22, 0), dur: 400000, outcome: "kill", deaths: 1, nontank_ms: 23000 },
  { key: "b", commander: "Glacienne", start: at(2026, 10, 28, 22, 14), dur: 400000, outcome: "kill", deaths: 1, nontank_ms: 12000 },
  { key: "c", commander: "Glacienne", start: at(2026, 10, 28, 23, 40), dur: 450000, outcome: "end", deaths: 1, nontank_ms: null },
] };
const table = plain(FH.packTable(PACK, "c"));
assert.strictEqual(table.rows.length, 4);
const rowOf = (k) => table.rows.find((r) => r.key === k);
assert.deepStrictEqual(rowOf("b"), {
  key: "b", commander: "Glacienne", day: "2026-10-28", when: "28 Oct 22:14", start: at(2026, 10, 28, 22, 14),
  durS: 400, deaths: 1, ntS: 12, outcome: "kill", sel: false, best: true,
}, "the pack row's fields");
assert.strictEqual(rowOf("wipe").ntS, null, "no stored non-tank seconds stays null");
assert.strictEqual(rowOf("c").sel, true, "the selected fight's row is marked");
assert.strictEqual(table.best, "b", "fewest deaths, then shortest, then newest: not the 0-death wipe, not the older tie, not the longer end");
assert.deepStrictEqual(table.median, { durS: 400, deaths: 1 }, "the median over every row of the pack");
assert.deepStrictEqual(plain(FH.packTable({ rows: [] }, null)), { rows: [], best: null, median: { durS: 0, deaths: 0 } },
  "an empty pack has no best and zero medians");

// A wipe can never be best, however clean: only kill/end count.
const onlyWipes = plain(FH.packTable({ rows: [
  { key: "w1", start: at(2026, 10, 27, 1, 0), dur: 30000, outcome: "wipe", deaths: 0 },
  { key: "w2", start: at(2026, 10, 28, 1, 0), dur: 20000, outcome: "wipe", deaths: 1 },
] }, null));
assert.strictEqual(onlyWipes.best, null, "a pack of wipes has no best row");
assert.strictEqual(onlyWipes.rows.every((r) => !r.best), true, "no wipe row is marked best");

// ---- the builders ---------------------------------------------------------------------------------------------------
const DAY_OPTIONS = FH.dayOptions(DAYS, TODAY);
const selectHtml = run("fdDayOptionsHtml(opts)", { opts: DAY_OPTIONS });
assert.ok(selectHtml.includes('<option value="">Tonight</option>'), "Tonight is the first option");
assert.ok(selectHtml.includes('<option value="2026-10-28">Wed 28 Oct · 12 fights</option>'), "a past night keeps its label");

const EVIL = '<img src=x onerror=alert(1)>"&';
const evilSelect = run("fdDayOptionsHtml(opts)", { opts: [{ value: '"' + EVIL + '"', label: EVIL }] });
assert.ok(!/<img/i.test(evilSelect), "a day label cannot become markup: " + evilSelect);
assert.ok(evilSelect.includes("&lt;img"), "a day label is escaped as text");
for (const m of evilSelect.matchAll(/="([^"]*)"/g)) assert.ok(!/[<>]/.test(m[1]), "an option attribute holds no raw tag byte");

const EVIL_PACK = { pack_key: EVIL, rows: [
  { key: 'k"><img src=x>', commander: EVIL, start: at(2026, 10, 28, 22, 14), dur: 120000, outcome: "kill", deaths: 0, nontank_ms: null },
] };
const packHtml = run("fdPackHtml(FightHistory.packTable(json, 'b'), false, 'onyxias_lair#12')", { json: PACK });
assert.ok(packHtml.includes('<tr class="sel" aria-current="true">') || /class="sel"/.test(packHtml), "the selected fight's row is marked");
assert.ok(packHtml.includes('data-fd-open="b"') && packHtml.includes('data-fd-day="2026-10-28"'),
  "each row's time opens its fight and its night");
assert.ok(/<button type="button" class="fd-packopen" data-fd-open="b"[^>]*aria-label="[^"]+"/.test(packHtml),
  "the open button carries an aria-label");
assert.ok(packHtml.includes(">—<") || packHtml.includes(">—"), "a null non-tank time shows a dash");
assert.ok(/<span class="fd-best">best<\/span>/.test(packHtml), "the best row says best in words");
assert.ok(packHtml.includes('<input type="checkbox" data-fd-packall>'), "the all-commanders box is off by default");
assert.ok(run("fdPackHtml(FightHistory.packTable(json, null), true, 'p')", { json: PACK })
  .includes('<input type="checkbox" data-fd-packall checked>'), "the all-commanders box shows its state");

const evilPack = run("fdPackHtml(FightHistory.packTable(json, 'k'), false, packKey)", { json: EVIL_PACK, packKey: EVIL });
assert.ok(!/<img/i.test(evilPack), "a commander name and a fight key cannot become markup: " + evilPack.slice(0, 300));
assert.ok(evilPack.includes("&lt;img"), "the commander name is escaped as text");
for (const m of evilPack.matchAll(/="([^"]*)"/g)) assert.ok(!/[<>]/.test(m[1]), "a pack attribute cannot break out: " + m[1]);

// The Live card belongs to Tonight: a chosen past day shows no live row, even while the commander fights.
const LIVE = { fightMs: 63000, alive: 5, active: 3, dps: 900 };
const tonight = run("fdRailHtml([], live, null, '')", { live: LIVE });
assert.ok(tonight.includes('data-fd-fight="live"'), "Tonight shows the live fight");
assert.ok(!run("fdRailHtml([], live, null, '2026-10-28')", { live: LIVE }).includes('data-fd-fight="live"'),
  "a past night shows no live fight");

// ---- the source stays free of the browser's locale tables --------------------------------------------------------
const histSrc = fs.readFileSync(histPath, "utf8");
assert.ok(!/toLocale|Intl\.|localeCompare/.test(histSrc), "the labels come from fixed English tables, never a locale API");

console.log("fight history ok");
