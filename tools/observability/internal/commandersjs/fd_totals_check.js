"use strict";
// Run by commandersjs_test.go under node: the Fight debugger's "This fight | Run total" switch (owner decision 2026-11-02).
// The switch above the role tables (the same .seg control as the Bots tab), the run-totals fetch window, the run chips and the
// remembered choice; the live table and live chips keep the rolling 10 s window with no switch.
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
  grab("fdTotalsSegHtml"), grab("fdTotalsGet"), grab("fdTotalsSet"), grab("fdTotalsSinceMs"), grab("fdTotalChipsHtml"),
  grab("fdWindowStart"), grab("fdLiveTableHtml"), grab("fdRolesHtml"),
  grabConst("FD_TOTALS_KEY"), grabConst("FD_TOTALS_MS"), grabConst("FD_WINDOW_S"),
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
assert.ok(segFight.indexOf("This fight") < segFight.indexOf("Run total"), "this fight first");
const segRun = run("fdTotalsSegHtml('run')");
assert.strictEqual((segRun.match(/active/g) || []).length, 1, "one active button: " + segRun);
assert.ok(segRun.indexOf('data-v="run"') < segRun.indexOf("active") || segRun.indexOf("active") > segRun.indexOf('data-v="fight"'),
  "Run total is the active one: " + segRun);
assert.ok(segRun.includes(">This fight<") && segRun.includes(">Run total<"), "both labels stay");
deq(run("fdTotalsSegHtml('bogus')"), run("fdTotalsSegHtml('fight')"), "an unknown mode falls back to This fight");

// paintSeg and bindSeg drive it: the same helpers as the Bots tab segs.
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
assert.deepStrictEqual([seg.buttons[0].on, seg.buttons[1].on], [false, true], "paintSeg activates Run total");
run("var picked = null;", {});
run("bindSeg(seg, (v) => { picked = v; })", { seg });
assert.strictEqual(typeof seg.handler, "function", "bindSeg listens on the seg");
seg.handler({ target: { closest: () => seg.buttons[0] } });
assert.strictEqual(vm.runInContext("picked", ctx), "fight", "a click reports This fight");
assert.deepStrictEqual([seg.buttons[0].on, seg.buttons[1].on], [true, false], "a click paints This fight active");
seg.handler({ target: { closest: () => null } });
assert.deepStrictEqual([seg.buttons[0].on, seg.buttons[1].on], [true, false], "a click outside a button changes nothing");

// ---- "Run total" renders the same role tables from a totals payload -----------------------------------------------------
const TOTALS = {
  since: 1000, fights: 2, dur_ms: 90000,
  bots: [
    { name: "Tanky", guid: 1, class: "warrior", role: "tank", dmg: 2000, taken: 8000, casts: 10, deaths: 1,
      spells: { "Sunder Armor": 8, Taunt: 1, "Heroic Strike": 6 }, fight_ms: 90000, fights: 2 },
    { name: "Pew", guid: 3, class: "mage", role: "dps", dmg: 15000, taken: 400, casts: 30, deaths: 0,
      spells: { Fireball: 30 }, fight_ms: 30000, fights: 1 },
    { name: "Healz", guid: 2, class: "priest", role: "healer", heal: 12000, over: 4000, casts: 30, deaths: 0,
      spells: {}, fight_ms: 90000, fights: 2 },
  ],
};
const tables = F.runRoleTables(TOTALS, null);
const three = run("fdRolesHtml(tables, null)", { tables: plain(tables) });
assert.strictEqual(three.length, 3, "tanks, healers, damage");
assert.ok(three[1].includes("133") && three[1].includes("25%"), "HPS = 12000 / 90 s, Over 25%: " + three[1].slice(0, 200));
assert.ok(three[2].includes("500"), "DPS = 15000 / Pew's own 30 s, not the run's 90 s: " + three[2].slice(0, 200));
assert.ok(three[0].includes(">8<") && three[0].includes(">1<") && three[0].includes("8,000"),
  "Sunder, taunts and taken from the totals: " + three[0].slice(0, 300));
assert.ok(three[0].includes("✕"), "the summed death shows");
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

// ---- the run the totals sum -----------------------------------------------------------------------------------------------
const NOW = 2000000 * 1000;
assert.strictEqual(run("fdTotalsSinceMs(bind, null, now)", { bind: { bound_at: 1000000 }, now: NOW }), 999960 * 1000,
  "the bound run's start (fdWindowStart rounds down to the minute)");
const win = run("fdTotalsSinceMs(bind, null, now)", { bind: { bound_at: 1990000 }, now: NOW });
assert.ok(win > 1990000 * 1000 - 7200 * 1000 - 60000 && win <= 1990000 * 1000, "a fresh bind: the two-hour window, got " + win);
assert.strictEqual(run("fdTotalsSinceMs(bind, fd, now)", {
  bind: { bound_at: 1000000 }, fd: { day: "2026-10-26", list: [{ start: 5000 }, { start: 3000 }, { start: 4000 }] }, now: NOW,
}), 3000 * 1000, "on a past night the night's first fight start");
assert.strictEqual(run("fdTotalsSinceMs(bind, fd, now)", {
  bind: { bound_at: 0, history: true }, fd: { day: "", list: [{ start: 7000 }, { start: 9000 }] }, now: NOW,
}), 7000 * 1000, "a history card's first fight start");
assert.ok(run("fdTotalsSinceMs(bind, fd, now)", { bind: { bound_at: 0 }, fd: { day: "2026-10-26", list: [] }, now: NOW }) > 0,
  "an empty past night falls back to the window");

// ---- the live table and live chips keep the 10 s window: no switch, no totals ----------------------------------------------
const liveHtml = run("fdLiveTableHtml(rows, null, 10)", { rows: [{ name: "Pew", dps: 10, hps: 5, casts: 1, last: "Fireball" }] });
assert.ok(liveHtml.includes("Live, last 10 s"), "the caption is unchanged: " + liveHtml.slice(0, 120));
assert.ok(!liveHtml.includes("data-fd-totals") && !liveHtml.includes("Run total"), "no switch on the live table");
assert.ok(!/totals/i.test(grab("fdLiveTableHtml")), "the live table builder knows no totals");
assert.ok(/\/api\/v1\/combat\/run-totals\?commander=/.test(src), "Run total fetches the run-totals route");
assert.ok(/fdTotalsSegHtml\(totalsMode\)/.test(src), "the seg paints every render");
assert.ok(!/fdTotalsSegHtml|fdTotalChipsHtml|runRoleTables/.test(grab("fdLiveTableHtml")), "live rendering stays out of it");

console.log("fd_totals_check: ok");
