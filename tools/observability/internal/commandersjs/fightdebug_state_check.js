"use strict";
// Run by commandersjs_test.go under node: the Fight debugger's state in app.js, not its markup. A live fight that ends hands the
// pick back, one fights-list fetch serves every caller of a commander, a failed fight detail waits and is then left alone so the
// rows after it still load, and a departed commander's card takes its cached state with it.
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");
const appPath = path.resolve(process.argv[2]);
const src = fs.readFileSync(appPath, "utf8");
const tabsSrc = fs.readFileSync(path.join(path.dirname(appPath), "fdtabs.js"), "utf8");
const fdSrc = fs.readFileSync(path.join(path.dirname(appPath), "fightdebug.js"), "utf8");

function grab(name) {
  const re = new RegExp(`^  function ${name}\\(`, "m"), m = re.exec(src);
  assert.ok(m, `${name} not found`);
  const end = src.indexOf("\n  }\n", m.index);
  assert.ok(end > 0, `${name} has no end`);
  return src.slice(m.index, end + 4);
}
function grabConst(name) {
  const re = new RegExp(`^  const ${name} = `, "m"), m = re.exec(src);
  assert.ok(m, `${name} not found`);
  const line = src.indexOf("\n", m.index);
  return src.slice(m.index, line + 1);
}

let clock = 0;
const ctx = { console, Date: { now: () => clock } };
vm.createContext(ctx);
vm.runInContext(tabsSrc, ctx, { filename: "fdtabs.js" });
vm.runInContext(fdSrc, ctx, { filename: "fightdebug.js" });
vm.runInContext([
  grab("cmdForgetCard"), grab("cmdRescueLayers"), grab("fdState"), grab("fdSyncSel"), grab("cmdRaidFights"), grab("fdFightsFeed"),
  grab("fdDetailFailed"), grab("fdDetailFail"), grab("fdDetailRow"), grab("fdLoad"), grab("fdDaysLoad"), grab("fdFightHist"),
  grab("fdMergeRows"), grab("numOr0"), grab("fdTodayDay"), grab("fdTonightRows"),
  grabConst("FD_DETAIL_MAX"), grabConst("FD_DETAIL_TRIES"), grabConst("FD_DAYS_MS"),
  "var document = { getElementById: () => null };",
  "var cmdPageMode = () => 'debug';",
  "var fdPage = { open: null };",
  "var fdFights = null, fdFightsCache = null, fdFightsPending = null;",
  "var cmdUnwatchStage = () => {};",
  "var fdTabListSeen = () => {};",
  "var fdFightHash = () => {};",
  "var renderFightDebug = () => {};",
  "var fdDayFights = (fd, bind, day, cb) => { if (cb) cb(null); };",
  "var FightHistory = { dayOf: () => \"2026-10-29\", rowDay: (day) => (day === \"2026-10-29\" ? \"\" : day) };",
].join("\n"), ctx, { filename: "app.js state" });

const run = (expr, vars) => { if (vars) Object.assign(ctx, vars); return vm.runInContext(expr, ctx); };
const settle = async () => { for (let i = 0; i < 8; i++) await new Promise((r) => setImmediate(r)); };

(async () => {
  // ---- 1. a live fight that ends hands the pick back ---------------------------------------------------------------
  ctx.worstPick = 1;
  ctx.FightDebug.worstIndex = () => ctx.worstPick;
  const rows = [{ key: "5-5" }, { key: "5-4" }, { key: "5-3" }];
  const detail = [null, { fight: {} }, { fight: {} }];
  ctx.rows = rows; ctx.detail = detail;
  // A fight is running when a hostile unit is engaged with the group: damage dealt in the module's 10 s window.
  const running = { bots: [{ guid: 1, id: "1" }], combat: { fight_ms: 2000, win_s: 10, bots: [{ g: 1, dps: 5 }] } };
  const fd = { sel: "live", userSel: true, bot: null, death: null, details: {}, failed: {}, loading: false, list: rows };
  run("fdSyncSel(fd, bind, rows, detail, null)", { fd, bind: running });
  assert.strictEqual(fd.sel, "live", "a running fight keeps the live pick");

  run("fdSyncSel(fd, bind, rows, detail, null)", { fd, bind: {} });
  assert.strictEqual(fd.sel, "5-4", "when the live fight ends the worst finished fight is picked");
  assert.strictEqual(fd.userSel, false, "the pick is automatic again");

  const picked = { sel: "5-3", userSel: true, bot: null, death: null, details: {}, failed: {}, loading: false, list: rows };
  run("fdSyncSel(fd, bind, rows, detail, null)", { fd: picked, bind: {} });
  assert.strictEqual(picked.sel, "5-3", "a finished fight the user picked is kept when another fight ends");

  ctx.worstPick = -1;
  const unloaded = { sel: "live", userSel: true, bot: null, death: null, details: {}, failed: {}, loading: false, list: rows };
  run("fdSyncSel(fd, bind, rows, [null, null, null], null)", { fd: unloaded, bind: {} });
  assert.strictEqual(unloaded.sel, "5-5", "with no details loaded the newest finished fight is picked");

  // ---- 2a. one fights-list fetch per commander, every caller served -------------------------------------------------
  run("cmdFightsCache = {}; cmdFightsPending = {}");
  let fetches = 0;
  const pending = [];
  ctx.jsonOrThrow = (p) => p;
  ctx.fetch = () => { fetches += 1; return new Promise((res, rej) => pending.push({ res, rej })); };
  const got = [];
  run("cmdRaidFights(bind, l => got.push(l))", { bind: { commander: "A" }, got });
  run("cmdRaidFights(bind, l => got.push(l))", { bind: { commander: "A" }, got });
  assert.strictEqual(fetches, 1, "a second caller joins the one fetch in flight");
  pending[0].res([{ key: "k1" }]);
  await settle();
  assert.strictEqual(got.length, 2, "both callers hear the answer");
  assert.deepStrictEqual(got[0].map((r) => r.key), ["k1"]);

  run("cmdRaidFights(bind, l => got.push(l))", { bind: { commander: "A" }, got });
  assert.strictEqual(fetches, 1, "the 10 s cache answers without a fetch");
  assert.deepStrictEqual(got[2].map((r) => r.key), ["k1"]);

  clock = 31000;
  run("cmdRaidFights(bind, l => got.push(l))", { bind: { commander: "A" }, got });
  assert.strictEqual(fetches, 2, "the list is asked for again once the cache is old");
  pending[1].res([{ key: "k2" }]);
  await settle();
  assert.strictEqual(got[3][0].key, "k2");

  // A failed fetch with nothing cached answers null; two renders still share one fetch.
  ctx.failedAnswer = "unset";
  run("cmdRaidFights(bind, l => { failedAnswer = l; })", { bind: { commander: "B" } });
  run("cmdRaidFights(bind, l => { failedAnswer = l; })", { bind: { commander: "B" } });
  assert.strictEqual(fetches, 3, "two callers of a new commander share their fetch");
  pending[2].rej(new Error("500"));
  await settle();
  assert.strictEqual(run("failedAnswer"), null, "a failed first fetch answers null, not an empty list");

  // ---- 2b. a failed detail is asked once more at once, then says so; later rows still load ---------------------------
  run("cmdFd = {}; cmdFightsCache = {}; cmdFightsPending = {}");
  const list = [{ key: "bad" }, { key: "empty" }, { key: "good" }];
  ctx.cmdRaidFights = (bind, cb) => cb(list);
  const reqs = {};
  ctx.fetch = (url) => {
    const key = decodeURIComponent(url.slice(url.indexOf("/fight/") + 7));
    reqs[key] = (reqs[key] || 0) + 1;
    if (key === "bad") return Promise.reject(new Error("500"));
    if (key === "empty") return Promise.resolve({});
    return Promise.resolve({ fight: { id: key } });
  };
  ctx.renderFightDebug = () => {};
  ctx.drawCmdMap = () => {};
  const card = { classList: { contains: () => true } };
  clock = 0;
  run("fdPage.open = 'D'");
  run("fdLoad(card, bind)", { card, bind: { commander: "D" } });
  await settle();
  const fdD = run("fdState('D')");
  assert.strictEqual(reqs.bad, 2, "a failed key is asked once more at once, never left loading");
  assert.strictEqual(reqs.empty, 2, "an answer without a fight counts as a failure and gets the same retry");
  assert.strictEqual(reqs.good, 1, "the row after the failures loads");
  assert.deepStrictEqual(Object.keys(fdD.details), ["good"]);
  assert.strictEqual(fdD.failed.bad.n, 2);
  assert.strictEqual(fdD.failed.empty.n, 2);
  assert.strictEqual(run("fdDetailFailed(fdD, 'bad')", { fdD }), true, "two failures mark the key failed for good");
  assert.strictEqual(fdD.loading, false);
  assert.strictEqual(fdD.loadingKey, null);

  clock = 600000;
  run("fdLoad(card, bind)", { card, bind: { commander: "D" } });
  await settle();
  assert.strictEqual(reqs.bad, 2, "a failed key is never asked again by the loader");

  // A null list (a failed fetch) keeps the fights already known.
  run("cmdFd = {}");
  const fdK = run("fdState('K')");
  fdK.list = [{ key: "keep" }];
  fdK.details.keep = { fight: {} };
  ctx.cmdRaidFights = (bind, cb) => cb(null);
  run("fdPage.open = 'K'");
  run("fdLoad(card, bind)", { card, bind: { commander: "K" } });
  await settle();
  assert.deepStrictEqual(fdK.list.map((r) => r.key), ["keep"], "a failed list fetch does not wipe the known fights");

  // ---- 2d. a read-only history card opens on its newest stored night (today is Tonight) -------------------------------
  run("cmdFd = {}");
  const fdH = run("fdState('H')");
  fdH.days = [{ day: "2026-10-29", fights: 3 }, { day: "2026-10-27", fights: 1 }];
  fdH.daysAt = clock;
  ctx.cmdRaidFights = (b, cb) => cb([]);
  run("fdLoad(card, hbind)", { card, hbind: { commander: "H", history: true, bots: [] } });
  await settle();
  assert.strictEqual(fdH.day, "", "the newest night is today: the picker calls it Tonight");
  assert.deepStrictEqual(Object.keys(fdH.details), [], "no detail for an empty day list");
  assert.strictEqual(fdH.sel, null);

  // A past newest night keeps its own value.
  run("cmdFd = {}");
  const fdP = run("fdState('P')");
  fdP.days = [{ day: "2026-10-27", fights: 1 }];
  fdP.daysAt = clock;
  run("fdLoad(card, hbind)", { card, hbind: { commander: "P", history: true, bots: [] } });
  await settle();
  assert.strictEqual(fdP.day, "2026-10-27", "a past night is selected by its own day");

  // ---- 2c. the picked fight's detail is fetched even when it is older than the newest 50 --------------------------------
  const oldList = Array.from({ length: 60 }, (_, i) => ({ key: "k" + i }));
  const fdOld = { sel: "k59", details: {}, failed: {}, list: oldList };
  assert.strictEqual(run("(fdDetailRow(fd) || {}).key", { fd: fdOld }), "k59",
    "the picked fight is fetched by its key, beyond the newest-50 window");
  const fdLivePick = { sel: "live", details: {}, failed: {}, list: oldList };
  assert.strictEqual(run("(fdDetailRow(fd) || {}).key", { fd: fdLivePick }), "k0", "a live pick still walks the newest rows");
  const fdBeyond = { sel: "live", details: {}, failed: {}, list: oldList };
  for (let i = 0; i < 50; i++) fdBeyond.details["k" + i] = { fight: {} };
  assert.strictEqual(run("fdDetailRow(fd)", { fd: fdBeyond }), null,
    "a night longer than the window leaves the older rows unasked (their cards say so), not loading for ever");

  // A fight of another commander's binding has no kept positions on this card: it is read like a past night, never this
  // card's track.
  assert.strictEqual(run("fdFightHist({}, null, {commander:'B'}, bind)", { bind: { commander: "A" } }), true,
    "another commander's fight is history on this card");
  assert.strictEqual(run("fdFightHist({}, null, {commander:'A'}, bind)"), false, "the card's own fight keeps its positions");
  assert.strictEqual(run("fdFightHist({hist:true}, null, null, bind)"), true, "a past night stays history");
  assert.strictEqual(run("fdFightHist(null, {hist:true}, null, bind)"), true, "a history detail stays history");

  // ---- 3. a departed commander's card takes its cached state with it -------------------------------------------------
  run("cmdCards = new Map(); cmdMaps = {}; cmdAvail = {}; cmdFd = {}; cmdFightsCache = {}; cmdFightsPending = {}; removed = 0");
  const keys = [];
  for (let i = 0; i < 30; i++) {
    const key = "C" + i;
    keys.push(key);
    const card = { remove() { run("removed += 1"); }, querySelector() { return null; } };
    run("cmdCards.set(key, card); cmdMaps[key] = {}; cmdAvail[key] = {}; cmdFd[key] = { details: {} };"
      + " cmdFightsCache[key] = { list: [], at: 0 }; cmdFightsPending[key] = [() => {}]", { key, card });
  }
  keys.forEach((key) => run("cmdForgetCard(key)", { key }));
  assert.strictEqual(run("removed"), 30, "every card element is removed");
  assert.strictEqual(run("cmdCards.size"), 0);
  for (const name of ["cmdMaps", "cmdAvail", "cmdFd", "cmdFightsCache", "cmdFightsPending"]) {
    assert.deepStrictEqual([...run(`Object.keys(${name})`)], [], `${name} holds nothing for a departed commander`);
  }

  console.log("fight debugger state ok");
})().catch((e) => { console.error(e); process.exit(1); });
