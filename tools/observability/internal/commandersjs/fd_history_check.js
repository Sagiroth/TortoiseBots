"use strict";
// Run by commandersjs_test.go under node: the Fight debugger with no raid bound. GET /api/v1/combat/history/commanders (the
// daemon's list of commanders with saved fights, newest first) feeds the All raids grid, "No raid bound right now." stays a
// one-line note above the list, and a commander the history knows opens a read-only card instead of "Waiting for this
// commander in the next report".
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
function grabLine(name, re) {
  const r = re || new RegExp(`^  const ${name} = `, "m");
  const m = r.exec(src);
  assert.ok(m, `${name} not found`);
  const end = src.indexOf(";\n", m.index);
  return src.slice(m.index, end + 2);
}

let clock = 1000000;
const ctx = { console, Date: { now: () => clock }, encodeURIComponent, decodeURIComponent };
vm.createContext(ctx);
vm.runInContext([
  grab("esc"), grab("numOr0"), grab("fdHistCmdLoad"), grab("cmdHistoryBind"), grab("fdHistoryListHtml"), grab("fdOverviewHtml"),
  grab("fdEmptyNote"),
  grabLine("fdHistCmd"), grabLine("fdHistRow"), grabLine("FD_HISTCMD_MS"), grabLine("FD_EMPTY"), grabLine("fdHistNamed"),
  "var state = { commanders: { list: [] } };",
  "var fdPage = { open: null };",
  "var FdTabs = { sortBinds: (l) => l.slice() };",
  "var fdGridHtml = (binds, tabs) => '<button class=\"cmd-mini\" data-fdtab=\"' + esc(encodeURIComponent(binds[0] && binds[0].commander)) + '\">card</button>';",
  "var FightHistory = { whenLabel: (msv) => '28 Oct ' + String(Math.floor(msv / 3600000) % 24) + ':14' };",
  // The daemon's answer, kept in the same realm as the code under test: a promise made by another realm confuses the vm.
  "var fetchCalls = 0, fetchPayload = null, fetchFail = false;",
  "function fetch() { fetchCalls += 1; return fetchFail ? Promise.reject(new Error('500')) : Promise.resolve({ ok: true, json: () => Promise.resolve(fetchPayload) }); }",
  "function jsonOrThrow(r) { if (!r || r.ok === false) throw new Error('http'); return r.json(); }",
].join("\n"), ctx);

const run = (expr, vars) => { if (vars) Object.assign(ctx, vars); return vm.runInContext(expr, ctx); };
const plain = (v) => JSON.parse(JSON.stringify(v));

const calls = () => run("fetchCalls");
function answer(payload) {
  run("fetchPayload = arguments0; fetchFail = false", { arguments0: payload });
}
const settle = async () => { for (let i = 0; i < 6; i++) await new Promise((r) => setImmediate(r)); };

(async () => {
  // ---- 1. the list: newest first, one row per commander, escaping and percent-encoded names ---------------------------
  const EVIL = '<img src=x onerror=alert(1)>"&';
  answer({ commanders: [
    { commander: "Glacienne", fights: 60, days: 2, last: 1761800000 },
    { commander: EVIL, fights: 1, days: 1, last: 1761700000 },
  ] });
  assert.strictEqual(run("fdHistNamed('Glacienne')"), false, "nothing is named before the list answers");
  run("fdHistCmdLoad()");
  await settle();
  assert.strictEqual(calls(), 1, "one request");
  assert.deepStrictEqual(plain(run("fdHistCmd.list.map(r => r.commander)")), ["Glacienne", EVIL], "the daemon's order is kept");

  const list = run("fdHistoryListHtml()");
  assert.ok(list.includes('<h3 class="cmd-h">Saved fights</h3>'), list.slice(0, 200));
  assert.ok(list.includes('data-fdtab="Glacienne"'), "a name opens its history");
  assert.ok(list.includes("60 fights · 2 nights · last "), "the fight and night counts and the newest fight: " + list);
  assert.ok(list.includes('data-fdtab="' + encodeURIComponent(EVIL) + '"') && !/<img/i.test(list), "a name is percent-encoded, never markup");
  assert.ok(list.indexOf("Glacienne") < list.indexOf("&lt;"), "newest first");

  // ---- 2. the overview: the note, then the list when nothing is bound; the bound raids keep the grid ----------------
  const none = run("fdOverviewHtml([], [])");
  assert.ok(none.startsWith('<p class="fd-nofight">No raid bound right now.</p>'), "the one-line note comes first: " + none.slice(0, 120));
  assert.ok(none.includes('class="fd-histlist"'), "the saved fights follow the note");
  const bound = run("fdOverviewHtml([{ commander: 'Alpha' }], [])");
  assert.ok(!bound.includes("fd-histlist") && bound.includes('class="cmd-mini"'), "a bound raid keeps the live grid");
  assert.ok(run("fdOverviewHtml([], []).includes('Loading the saved fights')") === false, "a loaded list needs no placeholder");

  // ---- 3. a history binding: no party, no run, the counts the header prints ------------------------------------------
  const bind = plain(run("cmdHistoryBind('Glacienne')"));
  assert.deepStrictEqual(bind.bots, []);
  assert.strictEqual(bind.history, true);
  assert.strictEqual(bind.run, null);
  assert.strictEqual(bind.histFights, 60);
  assert.strictEqual(bind.histDays, 2);
  assert.strictEqual(run("fdHistNamed('Nobody')"), false, "an unknown name gets no read-only card");
  assert.strictEqual(run("fdEmptyNote([], null, false, true)"), "No saved fights in this commander's history.");
  assert.strictEqual(run("fdEmptyNote([], null, false, false)"), "No fight yet: the first pull shows here.");

  // ---- 4. the cache: one request a minute, and a failed one keeps the list -------------------------------------------
  clock += 1000;
  run("fdHistCmdLoad()");
  await settle();
  assert.strictEqual(calls(), 1, "a second look inside the minute does not ask again");
  run("fetchFail = true");
  clock += 120000;
  run("fdHistCmdLoad()");
  await settle();
  assert.strictEqual(calls(), 2, "the list is asked again once the minute passed");
  assert.strictEqual(plain(run("fdHistCmd.list.length")), 2, "a failed refresh keeps the commanders already known");

  console.log("fd history ok");
})().catch((e) => { console.error(e); process.exit(1); });
