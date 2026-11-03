"use strict";
// Run by commandersjs_test.go under node: the Fight debugger's "This fight | Total" switch. The switch above the role
// tables (the same .seg control as the Bots panel), the totals fetch, the title and chips, and the remembered choice;
// the live table and live chips keep the rolling 10 s window with no switch.
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");
const appPath = path.resolve(process.argv[2]), dir = path.dirname(appPath);
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
  return src.slice(m.index, src.indexOf("\n", m.index) + 1);
}

// Values built inside the vm carry its own Array prototype: compare them as plain JSON.
const plain = (v) => JSON.parse(JSON.stringify(v));
const deq = (a, b, m) => assert.deepStrictEqual(plain(a), plain(b), m);

const newStore = () => {
  const kept = {};
  return {
    getItem: (k) => (k in kept ? kept[k] : null),
    setItem: (k, v) => { kept[k] = String(v); },
    kept,
  };
};
const ctx = { console, window: { localStorage: newStore() }, Date, Math, JSON };
vm.createContext(ctx);
vm.runInContext(fs.readFileSync(path.join(dir, "fightdebug.js"), "utf8"), ctx, { filename: "fightdebug.js" });
vm.runInContext([
  grab("esc"), grab("numOr0"), grab("paintSeg"), grab("bindSeg"),
  grab("fdTotalsSegHtml"), grab("fdTotalsGet"), grab("fdTotalsSet"), grab("fdTotalsSession"), grab("fdTotalChipsHtml"),
  grab("fdRunStartText"), grab("fdTotalTitleHtml"),
  grab("fdLiveTableHtml"), grab("fdRolesHtml"),
  grabConst("FD_TOTALS_KEY"), grabConst("FD_TOTALS_MS"), grabConst("FD_TOTALS_CAP"),
  "var fdTotalsMem = null;",
].join("\n"), ctx, { filename: "app.js totals" });
const run = (expr, vars) => { if (vars) Object.assign(ctx, vars); return vm.runInContext(expr, ctx); };
const F = ctx.FightDebug;

// ---- the switch renders: the same .seg control, "This fight" on by default ---------------------------------------------
const segFight = run("fdTotalsSegHtml('fight')");
assert.ok(segFight.includes('class="seg"') && segFight.includes('role="group"'), "a .seg control: " + segFight);
assert.ok(segFight.includes('data-v="fight"') && segFight.includes('data-v="run"'), "paintSeg/bindSeg read data-v");
assert.ok(segFight.includes('data-fd-totals="fight"') && segFight.includes('data-fd-totals="run"'), "the focus restore finds it");
assert.ok(/data-v="fight"[^>]*class="seg-btn active"|class="seg-btn active"[^>]*data-v="fight"/.test(segFight)
  || (segFight.indexOf("This fight</button>") > 0 && segFight.match(/active/g).length === 1), "one active button: " + segFight);
assert.ok(segFight.indexOf("This fight") < segFight.indexOf(">Total<"), "this fight first, then the total");
const segRun = run("fdTotalsSegHtml('run')");
assert.strictEqual((segRun.match(/active/g) || []).length, 1, "one active button: " + segRun);
assert.ok(segRun.indexOf('data-v="run"') < segRun.indexOf("active") || segRun.indexOf("active") > segRun.indexOf('data-v="fight"'),
  "Total is the active one: " + segRun);
assert.ok(segRun.includes(">This fight<") && segRun.includes(">Total<"), "both labels stay");
deq(run("fdTotalsSegHtml('bogus')"), run("fdTotalsSegHtml('fight')"), "an unknown mode falls back to This fight");

// paintSeg and bindSeg drive it: the same helpers as the Bots panel segs.
const btn = (v) => ({ dataset: { v }, cls: new Set(), classList: { toggle: function (c, on) { on ? this.cls.add(c) : this.cls.delete(c); }.bind({ cls: new Set() }) } });
const mkSeg = () => {
  const fight = { dataset: { v: "fight" }, on: false, classList: { toggle(c, on) { fight.on = on; } } };
  const runB = { dataset: { v: "run" }, on: false, classList: { toggle(c, on) { runB.on = on; } } };
  void btn;
  const seg = { buttons: [fight, runB], handler: null,
    querySelectorAll() { return seg.buttons; },
    addEventListener(ev, fn) { seg.handler = fn; } };
  return seg;
};
run("paintSeg(seg, 'run')", { seg: mkSeg() });
const seg = mkSeg();
run("paintSeg(seg, 'run')", { seg });
assert.deepStrictEqual([seg.buttons[0].on, seg.buttons[1].on], [false, true], "paintSeg activates Total");
run("var picked = null;", {});
run("bindSeg(seg, (v) => { picked = v; })", { seg });
assert.strictEqual(typeof seg.handler, "function", "bindSeg listens on the seg");
seg.handler({ target: { closest: () => seg.buttons[0] } });
assert.strictEqual(vm.runInContext("picked", ctx), "fight", "a click reports This fight");
assert.deepStrictEqual([seg.buttons[0].on, seg.buttons[1].on], [true, false], "a click paints This fight active");
seg.handler({ target: { closest: () => null } });
assert.deepStrictEqual([seg.buttons[0].on, seg.buttons[1].on], [true, false], "a click outside a button changes nothing");

// ---- Total renders the same role tables from a totals payload ----------------------------------------------------------
const TOTALS = {
  fights: 2, dur_ms: 90000,
  bots: [
    { name: "Tanky", guid: 1, class: "warrior", role: "tank", dmg: 2000, taken: 8000, casts: 10, deaths: 2,
      spells: { "Sunder Armor": 8, Taunt: 1, "Heroic Strike": 6 }, fight_ms: 90000, fights: 2 },
    { name: "Pew", guid: 3, class: "mage", role: "dps", dmg: 15000, taken: 400, casts: 30, deaths: 0,
      spells: { Fireball: 30 }, fight_ms: 30000, fights: 1 },
    { name: "Healz", guid: 2, class: "priest", role: "healer", heal: 12000, over: 4000, casts: 30, deaths: 0,
      spells: {}, fight_ms: 90000, fights: 2 },
  ],
};
const tables = F.runRoleTables(TOTALS, null);
const three = run("fdRolesHtml(tables, null, 'run')", { tables: plain(tables) });
assert.strictEqual(three.length, 3, "tanks, healers, damage");
assert.ok(three[1].includes("133") && three[1].includes("25%"), "HPS = 12000 / 90 s, Over 25%: " + three[1].slice(0, 200));
assert.ok(three[2].includes("500"), "DPS = 15000 / Pew's own 30 s, not the run's 90 s: " + three[2].slice(0, 200));
assert.ok(three[0].includes(">8<") && three[0].includes(">1<") && three[0].includes("8,000"),
  "Sunder, taunts and taken from the totals: " + three[0].slice(0, 300));
assert.ok(three[0].includes(">2<") && !three[0].includes("✕"),
  "Total shows the summed death count, not one cross: " + three[0].slice(0, 300));
// "This fight" keeps the cross: one fight's Died cell says died or not, never how many.
const fightTables = F.roleTables({ dur_ms: 1000, bots: [{ name: "Tanky", guid: 1, class: "warrior", role: "tank", taken: 10 }],
  deaths: [{ name: "Tanky" }] }, null);
const fightThree = run("fdRolesHtml(tables, null, 'fight')", { tables: plain(fightTables) });
assert.ok(fightThree[0].includes("✕") && !fightThree[0].includes(">1<"),
  "This fight keeps the single cross: " + fightThree[0].slice(0, 300));
const chips = run("fdTotalChipsHtml(totals)", { totals: plain(TOTALS) });
assert.ok(chips.includes("189 raid dps") && chips.includes("133 hps") && chips.includes("2 fights"),
  "the header chips follow the switch (17000 / 90 s, 12000 / 90 s): " + chips);

// ---- the choice is remembered, per viewer, with the memory fallback ------------------------------------------------------
assert.strictEqual(run("fdTotalsGet()"), "fight", "This fight is the default");
run("fdTotalsSet('run')");
assert.strictEqual(run("fdTotalsGet()"), "run");
assert.strictEqual(ctx.window.localStorage.kept["tbdash.fdTotals"], "run", "kept in localStorage under tbdash.fdTotals");
run("var fdTotalsMem = null;", {});
assert.strictEqual(run("fdTotalsGet()"), "run", "a new page reads the stored choice");
run("fdTotalsSet('bogus')");
assert.strictEqual(run("fdTotalsGet()"), "fight", "an unknown value falls back to This fight");
assert.strictEqual(ctx.window.localStorage.kept["tbdash.fdTotals"], "fight");
const throwing = { getItem() { throw new Error("blocked"); }, setItem() { throw new Error("blocked"); } };
run("fdTotalsSet('run')", {});
ctx.window.localStorage = throwing;
run("var fdTotalsMem = null;", {});
assert.strictEqual(run("fdTotalsGet()"), "fight", "a blocked storage still defaults to This fight");
run("fdTotalsSet('run')");
assert.strictEqual(run("fdTotalsGet()"), "run", "and the choice is kept in memory for the page");
ctx.window.localStorage = newStore();

// ---- the run the totals sum: a run is one commander in one game session, named by the selected fight's key ---------------
assert.strictEqual(run("fdTotalsSession(key)", { key: "G\x007\x00f9" }), 7,
  "the selected fight's session comes from its key");
assert.strictEqual(run("fdTotalsSession(key)", { key: "G\x008\x00f9" }), 8,
  "a selected fight the list does not hold still uses its own session");
assert.strictEqual(run("fdTotalsSession(key)", { key: "live" }), "", "no fight key: no session to ask for");
assert.strictEqual(run("fdTotalsSession(key)", { key: null }), "", "no selection: no session");
assert.strictEqual(run("fdTotalsSession(key)", { key: "G\x00nope\x00f9" }), "", "a bad session is no session");

// ---- the title: the count, the server start the session names, and a capped answer -------------------------------------
const session = 1793210300; // the module's session id is the server start in unix seconds
const startText = new Date(session * 1000).toLocaleTimeString([], { hour: "2-digit", minute: "2-digit", hourCycle: "h23" });
assert.strictEqual(run("fdTotalTitleHtml(totals, session, more)", { totals: plain(TOTALS), session, more: false }),
  "Total · 2 fights since " + startText);
assert.strictEqual(run("fdTotalTitleHtml(totals, session, more)", { totals: plain(TOTALS), session, more: true }),
  "Total · 2 fights since " + startText + " (newest 500 fights)", "a cut says the newest 500");
assert.strictEqual(run("fdTotalTitleHtml(totals, session, more)", { totals: { fights: 1 }, session, more: false }),
  "Total · 1 fight since " + startText, "one fight is singular");
assert.strictEqual(run("fdTotalTitleHtml(totals, session, more)", { totals: plain(TOTALS), session: "", more: false }),
  "Total · 2 fights", "an unknown start is left out");
assert.strictEqual(run("fdRunStartText(session)", { session }), startText, "the session epoch renders as the reader's local time");

// ---- the live table and live chips keep the 10 s window: no switch, no totals ----------------------------------------------
const liveHtml = run("fdLiveTableHtml(rows, null, 10)", { rows: [{ name: "Pew", dps: 10, hps: 5, casts: 1, last: "Fireball" }] });
assert.ok(liveHtml.includes("Live, last 10 s"), "the caption is unchanged: " + liveHtml.slice(0, 120));
assert.ok(!liveHtml.includes("data-fd-totals") && !liveHtml.includes(">Total<"), "no switch on the live table");
assert.ok(!/totals/i.test(grab("fdLiveTableHtml")), "the live table builder knows no totals");
assert.ok(/\/api\/v1\/combat\/run-totals\?commander=/.test(src), "Total fetches the run-totals route");
assert.ok(/run-totals\?commander=.*&session=/.test(src), "Total sends the run's session");
assert.ok(/headers\.get\('X-More'\)/.test(src), "the client reads the X-More cut");
assert.ok(/totalsCache = \{ at: Date\.now\(\), session: String\(session\)/.test(src), "the client cache keys one run: commander plus session");
assert.ok(/fdTotalsSegHtml\(totalsMode\)/.test(src), "the seg paints every render");
assert.ok(/fdTotalsSession\(fd\.sel\)/.test(src), "the session comes from the selected fight's key");
assert.ok(!/fdTotalsSession\(bind, fd, list, selRow\)/.test(src), "no list fallback for the session");
assert.ok(/fdRolesHtml\(tables, fd\.bot, totalsMode\)/.test(src), "the role tables know the mode");
assert.ok(/Checks for /.test(src), "Total keeps the picked fight's name for the checks below");
assert.ok(!/fdTotalsSegHtml|fdTotalChipsHtml|runRoleTables/.test(grab("fdLiveTableHtml")), "live rendering stays out of it");

console.log("fd_totals_check: ok");
