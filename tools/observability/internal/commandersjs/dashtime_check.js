"use strict";
// Run by commandersjs_test.go under node with TZ=Europe/Oslo: the dashboard's dates and times. Every clock time the page
// shows is the browser's local time: the feeds, the incident table, the "since HH:MM" label and the fight-history pack
// table format from the row's unix field on the client, and nights roll over at 06:00 local while the server keeps its
// UTC day files.
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");
const histPath = path.resolve(process.argv[2]), appPath = path.resolve(process.argv[3]);
const dir = path.dirname(appPath);
const src = fs.readFileSync(appPath, "utf8");
const histSrc = fs.readFileSync(histPath, "utf8");
function grab(name) {
  const re = new RegExp(`^  function ${name}\\(`, "m"), m = re.exec(src);
  assert.ok(m, `${name} not found`);
  const end = src.indexOf("\n  }\n", m.index);
  return src.slice(m.index, end + 4);
}
function grabOpt(name) {
  try { return grab(name); } catch (e) { return ""; }
}
// Oslo is UTC+1 in November, so this run must see Oslo's zone; otherwise every local-time reading below is meaningless.
assert.strictEqual(new Date(1793568913000).getHours(), 22, "run node with TZ=Europe/Oslo, got " + new Date(1793568913000).toString());

const ctx = { console };
vm.createContext(ctx);
vm.runInContext(histSrc, ctx, { filename: "fighthistory.js" });
assert.ok(ctx.FightHistory && typeof ctx.FightHistory.whenLabel === "function", "fighthistory.js loads as the global FightHistory");
vm.runInContext([grab("esc"), grab("numOr0"), grab("fmtClock"), grab("feedClock"), grab("counterWindow"), grabOpt("fmtHM"),
  grab("fdTodayDay"), grab("fdNightParams"), grab("fdNightFights"), grab("fdHistFetch"), "var FD_HIST_MS = 10000;",
  "var state = { activity: { summary: { since: 1793569896, since_str: '2026-11-01 21:51:36' } } };",
  "var FightHistory = this.FightHistory;",
].join("\n"), ctx, { filename: "app.js" });

const run = (expr, vars) => { if (vars) Object.assign(ctx, vars); return vm.runInContext(expr, ctx); };
const FH = ctx.FightHistory;
const plain = (v) => JSON.parse(JSON.stringify(v));

// ---- 1. the saved-fights "last <date>" is the row's own unix seconds ------------------------------------------------
// CommanderCount.last is unix seconds and whenLabel takes seconds; app.js must pass it straight through (a * 1000 lands
// ~5 weeks out: 1793568913 showed "8 Dec 10:56" instead of "1 Nov 22:35" in Oslo).
const LAST = 1793568913; // 2026-11-01 21:35:13 UTC = 22:35 Oslo
assert.strictEqual(FH.whenLabel(LAST), "1 Nov 22:35", "whenLabel takes seconds: " + FH.whenLabel(LAST));
assert.ok(/FightHistory\.whenLabel\(r\.last\)/.test(src), "the Saved fights list passes r.last, not r.last * 1000");
assert.ok(!/FightHistory\.whenLabel\(r\.last \* 1000\)/.test(src), "no double-scaled epoch left");

// ---- 2. every clock time the page shows is the browser's local time -------------------------------------------------
// feedClock formats the row's unix field like the clock; the daemon's UTC string cannot move the hour. The string shape
// is the browser's own locale, so compare the two formatters rather than pinning the text.
assert.strictEqual(run("feedClock({ at: LAST, time_str: '2026-11-01 21:35:13' })", { LAST }), run("fmtClock(LAST)", { LAST }),
  "the feed reads the row's unix field, ignoring the daemon's UTC string");
assert.strictEqual(run("feedClock({ at: LAST, time_str: '1999-01-01 00:00:00' })", { LAST }), run("fmtClock(LAST)", { LAST }),
  "a hostile daemon string cannot move the feed's hour");
assert.strictEqual(run("feedClock({})"), "–", "a row without a time shows a dash");
assert.strictEqual(run("counterWindow()"), "since 22:51", "the counter window reads local: " + run("counterWindow()"));
// The pack table's when is the fight's start in local time too.
const pack = plain(FH.packTable({ rows: [
  { key: "a", commander: "G", start: LAST, dur: 60000, outcome: "kill", deaths: 0 },
] }, null));
assert.strictEqual(pack.rows[0].when, "1 Nov 22:35", "the pack table reads local: " + pack.rows[0].when);
// The incident table formats the row's unix field, never the daemon's string.
assert.ok(/const timeStr = a\.ts \? new Date\(a\.ts \* 1000\)\.toLocaleTimeString\(\) : '-'/.test(src),
  "the incident table formats the row's unix field");
assert.ok(!/a\.time_str \|\|/.test(src), "the incident table never reads the daemon's string");
// The night labels come from fixed English tables, never a locale API.
assert.ok(!/toLocale|Intl\.|localeCompare/.test(histSrc), "no locale API in the night labels");

// ---- 3. nights are grouped by the daemon from the page's own zone ---------------------------------------------------
// A fight at 02:17 local on 2 Nov belongs to the night of 1 Nov; at 07:00 it belongs to 2 Nov.
const NIGHT0217 = Math.floor(Date.UTC(2026, 10, 2, 1, 17, 0) / 1000); // 02:17 Oslo 2 Nov
const NIGHT0700 = Math.floor(Date.UTC(2026, 10, 2, 6, 0, 0) / 1000); // 07:00 Oslo 2 Nov
assert.strictEqual(FH.nightOf(NIGHT0217 * 1000), "2026-11-01", "02:17 belongs to the night before");
assert.strictEqual(FH.nightOf(NIGHT0700 * 1000), "2026-11-02", "07:00 belongs to its own night");
assert.strictEqual(FH.nightOf(NIGHT0700 * 1000 - 3601000), "2026-11-01", "05:59:59 still belongs to the night before");
assert.strictEqual(FH.nightOf(""), "", "no time is no night");
// Tonight is the current local night; a past night is labelled "(night)". The rows come from the daemon's night grouping.
const tonightOpts = plain(FH.dayOptions({ nights: [
  { night: "2026-11-01", fights: 9, first: NIGHT0217 - 3600, last: NIGHT0217 },
  { night: "2026-10-31", fights: 2, first: 1, last: 2 },
] }, NIGHT0217 * 1000));
assert.deepStrictEqual(tonightOpts, [
  { value: "", label: "Tonight · 9 fights" },
  { value: "2026-10-31", label: "Sat 31 Oct (night) · 2 fights" },
], "Tonight is the local night, labelled so: " + JSON.stringify(tonightOpts));
assert.deepStrictEqual(plain(FH.dayOptions({ nights: [{ night: "2026-11-05", fights: 2 }] }, NIGHT0217 * 1000)),
  [{ value: "", label: "Tonight" }], "a future night is not offered");
assert.strictEqual(FH.rowDay("2026-11-01", NIGHT0217 * 1000), "", "a fight of tonight opens the current list");
assert.strictEqual(FH.rowDay("2026-10-31", NIGHT0217 * 1000), "2026-10-31", "an older night keeps its night");
const goHistory = fs.readFileSync(path.join(__dirname, "../history/history.go"), "utf8");
assert.ok(/time\.Unix\(start, 0\)\.In\(loc\)\.Add\(-time\.Duration\(rollover\) \* time\.Hour\)/.test(goHistory), "the server groups the nights");
assert.ok(/time\.Unix\(start, 0\)\.UTC\(\)\.Format\(dayLayout\)/.test(goHistory), "the server still files nights as UTC days");
(async () => {
  // fdNightFights asks the daemon for the one night; the grouping already happened server-side.
  const rowsByNight = {
    "2026-11-01": [
      { key: "b", start: Math.floor(Date.UTC(2026, 10, 2, 3, 0, 0) / 1000), dur: 60000 }, // 04:00 local, still the night
      { key: "a", start: Math.floor(Date.UTC(2026, 10, 1, 22, 0, 0) / 1000), dur: 60000 }, // 23:00 local, the night's own
    ],
  };
  run("var fetchUrls = [];");
  run("function fetch(u) { fetchUrls.push(u); const night = /night=(\\d{4}-\\d{2}-\\d{2})/.exec(u)[1]; return Promise.resolve({ ok: true, json: () => Promise.resolve(byNight[night] || []) }); }", { byNight: rowsByNight });
  run("function jsonOrThrow(r) { if (!r || r.ok === false) throw new Error('http'); return r.json(); }");
  run("var FightHistory = this.FightHistory;", {});
  const fd = { hist: {}, histPending: {} };
  let got = "unset";
  run("fdNightFights(fd, { commander: 'G' }, '2026-11-01', (l) => { got = l; })", { fd, got: null });
  for (let i = 0; i < 8; i++) await new Promise((r) => setImmediate(r));
  const urls = plain(run("fetchUrls"));
  assert.strictEqual(urls.length, 1, "one request for the night: " + JSON.stringify(urls));
  assert.ok(/night=2026-11-01/.test(urls[0]) && /tz=/.test(urls[0]) && /rollover=6/.test(urls[0]),
    "the night fetch carries the zone and the rollover: " + urls[0]);
  assert.deepStrictEqual(plain(run("got")).map((r) => r.key), ["b", "a"], "the night's fights, newest first");
  assert.ok(/fdNightFights\(fd, bind, fd\.day, /.test(src), "a chosen night loads through fdNightFights");
  assert.ok(/fdNightFights\(fd, bind, tonight, /.test(src), "Tonight's whole night loads through fdNightFights");
  assert.ok(/fdNightParams\(\)/.test(src) && /resolvedOptions\(\)\.timeZone/.test(src), "the page sends its own zone");
  console.log("dashboard dates ok");
})().catch((e) => { console.error(e); process.exit(1); });
