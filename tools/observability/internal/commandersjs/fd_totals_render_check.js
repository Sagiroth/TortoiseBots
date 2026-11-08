"use strict";
// Run by commandersjs_test.go under node: renderFightDebug itself with a finished fight selected, once in "This fight"
// and once in "Total" mode (a fake fetch answering the run totals), plus no selection and a live selection.
// A finished fight must render in both modes (the header chips and the role tables follow the switch); the Total title
// names the server start the session id carries and the checks below keep the picked fight's name.
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");
const appPath = path.resolve(process.argv[2]), dir = path.dirname(appPath);
const src = fs.readFileSync(appPath, "utf8");

// A whole function or const: from its `  function name(` / `  const name =` line to its balanced end. The render is too
// long for the first-`  }` slice the markup checks use.
function take(name) {
  const re = new RegExp(`^  (?:function ${name}\\b|const ${name} =)`, "m");
  const m = re.exec(src);
  assert.ok(m, `${name} not found`);
  const eol = src.indexOf("\n", m.index);
  const open = src.indexOf("{", m.index);
  if (open < 0 || open > eol) return src.slice(m.index, eol + 1);
  let d = 0;
  for (let i = open; i < src.length; i++) {
    if (src[i] === "{") d++;
    else if (src[i] === "}") { if (--d === 0) return src.slice(m.index, i + 1) + "\n"; }
  }
  assert.fail(`${name} has no end`);
}

const FNS = ["esc", "numOr0", "setHtml", "jsonOrThrow", "cmdNowFor", "cmdClock", "cmdPlanName", "cmdPlanFor",
  "cmdBossTable", "cmdBossPanels", "cmdPaintBosses", "cmdLiveBossPanels", "cmdFightBossPanels", "cmdTrashBlockHtml",
  "fdState", "fdLiveState", "fdLiveTimelineHtml", "fdLiveFiledRow", "fdLiveClock", "fdLiveWinLabel", "fdEmptyNote", "fdSet", "fdSyncSel", "fdFightHash", "fdReplayStop",
  "fdReadyNotRun", "fdFrameStats", "fdReadyHtml", "fdFramesHtml",
  "fdShowAllGet", "fdTotalsGet", "fdTotalsSet", "fdTotalsSession", "fdTotalsLoad",
  "fdTotalChipsHtml", "fdTotalsSegHtml", "fdRunStartText", "fdTotalTitleHtml",
  "fdTonightRows", "fdMergeRows", "fdTodayDay", "fdOlderState",
  "fdRunModel", "fdRunPct", "fdRunClock", "fdRunT", "fdRunWindow", "fdRunCoverage", "fdRunHtml", "fdRunPlayHtml",
  "fdRunPlaySync", "fdRunSync", "fdRunLoad", "fdRunStop", "fdRunStopAll",
  "fdFightExtra", "fdPrevPackI", "fdFightHist",
  "fdRailFoldHtml", "fdRailCardHtml", "fdRailParts", "fdRailPatch",
  "fdRangeBandHtml", "fdOutCls", "fdViewSync", "fdRangeBarHtml", "fdWholeTags", "fdDragBandSync", "fdBossPhase", "fdUnderSync", "fdCheckNum", "fdCheckTitle", "fdChecksHtml", "fdTimelineHtml", "fdRecapHtml",
  "fdRolesHtml", "fdLiveTableHtml", "fdBotHtml",
  "fdPackLoad", "fdPackShow", "fdPackHtml", "fdDayOptionsHtml", "paintSeg",
  "fdPlayHtml", "fdPlayNote", "fdPlaySync", "fdLanesHtml", "fdCursorSync", "fdTrackLoad", "fdDayPaint",
  "FD_TOTALS_KEY", "FD_TOTALS_MS", "FD_TOTALS_CAP", "FD_SHOWALL_KEY", "FD_WINDOW_S", "FD_DETAIL_TRIES", "FD_EMPTY", "FD_TRACK_WINDOW_S",
  "renderFightDebug"];

const newStore = () => {
  const kept = {};
  return {
    getItem: (k) => (k in kept ? kept[k] : null),
    setItem: (k, v) => { kept[String(k)] = String(v); },
    kept,
  };
};

// The totals the fake fetch answers in "Total" mode: the same shape the daemon's route sends.
const TOTALS = {
  fights: 2, dur_ms: 90000,
  bots: [
    { name: "Tanky", guid: 1, class: "warrior", role: "tank", dmg: 2000, taken: 8000, casts: 10, deaths: 1,
      spells: { "Sunder Armor": 8, Taunt: 1, "Heroic Strike": 6 }, fight_ms: 90000, fights: 2 },
    { name: "Pew", guid: 3, class: "mage", role: "dps", dmg: 15000, taken: 400, casts: 30, deaths: 0,
      spells: { Fireball: 30 }, fight_ms: 30000, fights: 1 },
    { name: "Healz", guid: 2, class: "priest", role: "healer", heal: 12000, over: 4000, casts: 30, deaths: 0,
      spells: {}, fight_ms: 90000, fights: 2 },
  ],
};

let totalsAnswer = null, totalsCut = false;
const fetchLog = [];
const fetchStub = (url) => {
  fetchLog.push(String(url));
  if (String(url).indexOf("/run-totals") >= 0) {
    const body = JSON.parse(JSON.stringify(totalsAnswer));
    return Promise.resolve({
      status: 200, ok: true, json: () => Promise.resolve(body),
      headers: { get: (k) => (totalsCut && k === "X-More" ? "1" : null) },
    });
  }
  return new Promise(() => {}); // the days list, the tracks and the run track: never answer
};

const ctx = {
  console, setTimeout, clearTimeout, Date, Math, JSON,
  FightDebug: require(path.join(dir, "fightdebug.js")),
  FdLive: require(path.join(dir, "fdlive.js")),
  FdView: require(path.join(dir, "fdview.js")),
  FdTabs: require(path.join(dir, "fdtabs.js")),
  FightReplay: require(path.join(dir, "fightreplay.js")),
  AggroLanes: require(path.join(dir, "aggrolanes.js")),
  fetch: fetchStub,
  window: { localStorage: newStore() },
  document: { activeElement: null, createElement: () => ({}) },
};
vm.createContext(ctx);
vm.runInContext([
  "var cmdFd = {}, cmdFightsCache = {}, cmdPlans = {}, cmdBossSeen = {};",
  "var fdTotalsMem = null, fdShowAllMem = null;",
  "var cmdServerNow = () => 1793210300;",
  "var cmdPageMode = () => 'debug';",
  "var cmdPageHash = () => {};",
  "var fdPage = { open: null };",
  "var fdLive = {}, fdLivePaintBar = () => {}, fdLiveCatchUp = () => {}, fdLiveTickSync = () => {};",
  "var fdAurasSync = () => {}, fdAuraFight = (f) => f, fdAuLiveOpts = () => null, fdAuLiveKey = () => 'live:k';", // the Auras panel has its own check (fightauras_ui_check.js)
  "var cmdFindBind = (k) => null;",
].join("\n"), ctx);
vm.runInContext(FNS.map(take).join("\n"), ctx, { filename: "app.js totals render" });

const run = (expr, vars) => { if (vars) Object.assign(ctx, vars); return vm.runInContext(expr, ctx); };
const settle = async () => { for (let i = 0; i < 30; i++) await new Promise((r) => setImmediate(r)); };

// The game session id is the server start in unix seconds (internal/combat.Key puts it in the fight key).
const SESSION = 1793210300;
const startText = new Date(SESSION * 1000).toLocaleTimeString([], { hour: "2-digit", minute: "2-digit", hourCycle: "h23" });
const keyOf = (id) => "G\x00" + SESSION + "\x00" + id;

const fights = JSON.parse(fs.readFileSync(path.join(__dirname, "testdata", "walk2-fights.json"), "utf8"));
const F54 = fights.find((f) => f.id === "5-4");
const SEL = keyOf("5-4");
// Newest first, like GET /api/v1/combat/fights (the key is commander, session, id).
const ROWS = fights.map((f) => ({
  key: keyOf(f.id), session: SESSION, dur: f.dur_ms, step: f.step, deaths: (f.deaths || []).length,
  boss_name: f.boss_name, start: f.start,
}));
const DETAILS = {};
fights.forEach((f) => { DETAILS[keyOf(f.id)] = { fight: f, warnings: [] }; });

// The card: one remembered node per section the render paints (the rail stays null, so fdRailPatch keeps its early
// return); anything else the render touches is absent, as on a card missing that box.
function nodeStub() {
  return {
    innerHTML: "", textContent: "", hidden: false, value: "", checked: false,
    dataset: {}, style: {}, attributes: {},
    classList: { contains: () => false, add() {}, remove() {}, toggle() {} },
    contains: () => false, hasAttribute: () => false, getAttribute: () => null,
    setAttribute() {}, querySelector: () => null, querySelectorAll: () => [],
    focus() {}, remove() {}, appendChild() {}, replaceChild() {}, insertBefore() {},
    children: [], _h: undefined,
  };
}
function makeCard() {
  const nodes = {};
  [".fd-nofight", ".fd-title", ".fd-chips", ".fd-checklist", ".fd-checkfor", ".fd-tanks", ".fd-heals", ".fd-dps",
    ".fd-totalsseg", ".fd-bot", ".fd-frames", ".fd-dead", ".fd-ready", "[data-fd-showall]",
    "[data-fd-rt]", ".fd-runtl", ".fd-tl", ".fd-recap", ".fd-play", ".fd-lanes",
  ].forEach((s) => { nodes[s] = nodeStub(); });
  return {
    nodes,
    classList: { contains: (c) => c === "fd-on", add() {}, remove() {}, toggle() {} },
    dataset: {},
    querySelector(sel) { return nodes[sel] || null; },
    querySelectorAll() { return []; },
  };
}

(async () => {
  // ---- no fight selected: one line, no empty tables -----------------------------------------------------------------
  fetchLog.length = 0;
  run("cmdFd = {}; cmdFightsCache = { G: { list: [] } };");
  const cardA = makeCard();
  run("renderFightDebug(card, bind)", { card: cardA, bind: { commander: "G", bots: F54.bots } });
  assert.strictEqual(cardA.nodes[".fd-nofight"].textContent, "No fight yet: the first pull shows here.");
  assert.strictEqual(cardA.nodes[".fd-tanks"].innerHTML, "", "no role table without a fight");
  assert.ok(!fetchLog.some((u) => u.indexOf("/run-totals") >= 0), "no totals fetch with no fight");

  // ---- a live fight selected: the rolling window, no switch ----------------------------------------------------------
  run("cmdFd = {}; cmdFightsCache = { G: { list: rows } };", { rows: ROWS });
  const fdB = run("fdState('G')");
  run("fd.sel = 'live'; fd.userSel = true;", { fd: fdB });
  const liveBind = {
    commander: "G", bots: F54.bots,
    combat: { fight_ms: 65000, win_s: 10, bots: [{ g: 813, dps: 120, hps: 8, c: 4, ls: "Fireball" }] },
  };
  const cardB = makeCard();
  run("renderFightDebug(card, bind)", { card: cardB, bind: liveBind });
  assert.ok(cardB.nodes[".fd-title"].innerHTML.indexOf("Live ·") === 0, "the live title: " + cardB.nodes[".fd-title"].innerHTML);
  assert.ok(cardB.nodes[".fd-tanks"].innerHTML.includes("Live, last 10 s"), "the live table keeps its window");
  assert.strictEqual(cardB.nodes[".fd-totalsseg"].innerHTML, "", "no switch on the live table");
  assert.strictEqual(cardB.nodes[".fd-checkfor"].innerHTML, "", "no checks caption on the live table");

  // ---- a finished fight in "This fight" mode: its own numbers ----------------------------------------------------------
  fetchLog.length = 0;
  run("cmdFd = {}; cmdFightsCache = { G: { list: rows } };", { rows: ROWS });
  run("var fdTotalsMem = null;");
  const fdC = run("fdState('G')");
  run("fd.sel = sel; fd.userSel = true; fd.details = details;", { fd: fdC, sel: SEL, details: DETAILS });
  const fightBind = { commander: "G", bots: F54.bots };
  const cardC = makeCard();
  run("renderFightDebug(card, bind)", { card: cardC, bind: fightBind });
  assert.ok(cardC.nodes[".fd-title"].innerHTML.includes("step 22"), "the picked fight's title: " + cardC.nodes[".fd-title"].innerHTML);
  assert.ok(cardC.nodes[".fd-chips"].innerHTML.includes("raid dps"), "the fight's own chips: " + cardC.nodes[".fd-chips"].innerHTML.slice(0, 160));
  const tanksC = cardC.nodes[".fd-tanks"].innerHTML;
  assert.ok(tanksC.includes("Sunder") && tanksC.includes(">8<"), "the fight's own tanks table: " + tanksC.slice(0, 200));
  assert.ok(tanksC.includes("✕"), "This fight's Died cell is the single cross: " + tanksC.slice(0, 200));
  const segC = cardC.nodes[".fd-totalsseg"].innerHTML;
  assert.ok(segC.includes(">This fight<") && segC.includes(">Total<"), "the switch paints: " + segC.slice(0, 160));
  assert.ok(/data-v="fight"[^>]*class="seg-btn active"|class="seg-btn active"[^>]*data-v="fight"/.test(segC),
    "This fight is the active one: " + segC);
  assert.strictEqual(cardC.nodes[".fd-checkfor"].innerHTML, "", "no checks caption in This fight");
  assert.ok(!fetchLog.some((u) => u.indexOf("/run-totals") >= 0), "This fight fetches no totals");

  // ---- the same fight in "Total" mode: the run's sums ----------------------------------------------------------------
  run("fdTotalsSet('run')");
  totalsAnswer = TOTALS;
  totalsCut = false;
  fetchLog.length = 0;
  const cardD = makeCard();
  run("renderFightDebug(card, bind)", { card: cardD, bind: fightBind });
  await settle(); // the totals fetch answers, then the render runs again with the sums
  assert.strictEqual(cardD.nodes[".fd-title"].innerHTML, "Total · 2 fights since " + startText);
  const chipsD = cardD.nodes[".fd-chips"].innerHTML;
  assert.ok(chipsD.includes("189 raid dps") && chipsD.includes("133 hps") && chipsD.includes("2 fights"),
    "the header chips follow the switch (17000 / 90 s, 12000 / 90 s): " + chipsD);
  const healsD = cardD.nodes[".fd-heals"].innerHTML;
  assert.ok(healsD.includes("133") && healsD.includes("25%"), "HPS = 12000 / 90 s, Over 25%: " + healsD.slice(0, 200));
  const dpsD = cardD.nodes[".fd-dps"].innerHTML;
  assert.ok(dpsD.includes("500"), "DPS = 15000 / Pew's own 30 s, not the run's 90 s: " + dpsD.slice(0, 300));
  const tanksD = cardD.nodes[".fd-tanks"].innerHTML;
  assert.ok(tanksD.includes(">1<") && !tanksD.includes("✕"), "Total shows the summed death count: " + tanksD.slice(0, 300));
  assert.ok(cardD.nodes[".fd-checkfor"].innerHTML.indexOf("Checks for ") === 0 && cardD.nodes[".fd-checkfor"].innerHTML.length > 11,
    "the checks below keep the picked fight's name: " + cardD.nodes[".fd-checkfor"].innerHTML);
  const segD = cardD.nodes[".fd-totalsseg"].innerHTML;
  assert.ok(/data-v="run"[^>]*class="seg-btn active"|class="seg-btn active"[^>]*data-v="run"/.test(segD),
    "Total is the active one: " + segD);
  assert.strictEqual(fetchLog.filter((u) => u.indexOf("/run-totals") >= 0).length, 1, "one totals fetch per session");
  assert.ok(fetchLog.some((u) => u.indexOf("session=" + SESSION) >= 0), "the session is parsed from the selected key: " + fetchLog);

  // ---- a capped answer says so in the title ---------------------------------------------------------------------------
  totalsCut = true;
  run("fdTotalsSet('run'); cmdFd = {};");
  const fdE = run("fdState('G')");
  run("fd.sel = sel; fd.userSel = true; fd.details = details;", { fd: fdE, sel: SEL, details: DETAILS });
  const cardE = makeCard();
  run("renderFightDebug(card, bind)", { card: cardE, bind: fightBind });
  await settle();
  assert.strictEqual(cardE.nodes[".fd-title"].innerHTML, "Total · 2 fights since " + startText + " (newest 500 fights)");

  console.log("fd_totals_render_check: ok");
})().catch((e) => { console.error(e); process.exit(1); });
