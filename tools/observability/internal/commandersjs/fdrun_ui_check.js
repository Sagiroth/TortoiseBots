"use strict";
// Run by commandersjs_test.go under node: the Fight debugger's run timeline (where the minutes went, the walking and fight
// blocks, the death marks and the run scrubber), the one layer control, the aggro lane caption the prototype uses, and the
// rail's total damage and the checks' emphasised key values. The builders live in app.js; this feeds them a made-up run.
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");
const appPath = path.resolve(process.argv[2]), dir = path.dirname(appPath);
const src = fs.readFileSync(appPath, "utf8");
const css = fs.readFileSync(path.join(dir, "..", "..", "..", "web", "static", "css", "app.css"), "utf8");

function grab(name) {
  const re = new RegExp(`^  function ${name}\\(`, "m"), m = re.exec(src);
  assert.ok(m, `${name} not found`);
  const end = src.indexOf("\n  }\n", m.index);
  return src.slice(m.index, end + 4);
}
function grabConst(name) {
  const re = new RegExp(`^  const ${name} = `, "m"), m = re.exec(src);
  assert.ok(m, `${name} not found`);
  const line = src.indexOf(";\n", m.index);
  return src.slice(m.index, line + 2);
}

const ctx = { console };
vm.createContext(ctx);
["fightdebug.js", "fightreplay.js", "aggrolanes.js"].forEach((f) => {
  vm.runInContext(fs.readFileSync(path.join(dir, f), "utf8"), ctx, { filename: f });
});
vm.runInContext([grab("esc"), grab("numOr0"), grab("fdRunModel"), grab("fdRunClock"), grab("fdRunPct"), grab("fdRunT"),
  grab("fdRunHtml"), grab("fdRunWindow"), grab("fdRunCoverage"), grabConst("FD_TRACK_WINDOW_S"), grab("fdRunPlayHtml"),
  grab("fdRailHtml"), grab("fdChecksHtml"), grab("fdCheckNum"), grab("fdCheckTitle"), grab("fdLanesHtml")].join("\n"), ctx);
const run = (expr, vars) => { if (vars) Object.assign(ctx, vars); return vm.runInContext(expr, ctx); };

// ---- the model: walking between the fights, P1..Pn blocks, a boss block from its name, deaths at their second ----------------
const ROWS = [
  { key: "k1", start: 1060, dur: 30000, step: 1, deaths: 0, boss_name: "" },
  { key: "k2", start: 1100, dur: 60000, step: 2, deaths: 1, boss_name: "Onyxia" },
];
const DETAILS = {
  k1: { fight: { start: 1060, dur_ms: 30000, bots: [{ dmg: 100 }, { dmg: 50 }], deaths: [] }, warnings: [] },
  k2: { fight: { start: 1100, dur_ms: 60000, bots: [{ dmg: 200 }], deaths: [{ name: "Heal", t_ms: 20000, killer: "Onyxia" }] }, warnings: [] },
};
const bind = { commander: "G", bound_at: 1000 };
const runModel = run("fdRunModel(bind, rows, details, 1180)", { bind, rows: ROWS, details: DETAILS });
assert.strictEqual(runModel.t0, 1000, "the run starts at the binding");
assert.strictEqual(runModel.t1, 1180, "and ends at the newest report");
assert.strictEqual(runModel.spanMs, 180000);
assert.strictEqual(runModel.segs.map((s) => s.kind).join(","), "walk,fight,walk,boss,walk");
assert.strictEqual(runModel.segs.filter((s) => s.kind !== "walk").map((s) => s.label).join(","), "P1,P2");
assert.strictEqual(runModel.segs.find((s) => s.kind === "boss").name, "Onyxia");
assert.strictEqual(runModel.deaths.length, 1, "the fight's death is on the strip");
assert.strictEqual(runModel.deaths[0].tS, 1120, "at the fight's start plus its t_ms");

// ---- the strip: the fight blocks, the boss colour, the death mark, the cursor and the run clock -----------------------------------
const strip = run("fdRunHtml(model, 'k1', { run: { t: null } })", { model: runModel });
assert.strictEqual((strip.match(/class="fd-rtfight/g) || []).length, 2, "one block per fight");
assert.ok(/class="fd-rtfight boss/.test(strip), "the boss block is red");
assert.ok(/class="fd-rtfight on"/.test(strip), "the selected fight is marked");
assert.strictEqual((strip.match(/class="fd-rtwalk"/g) || []).length, 3, "the walking gaps are drawn");
assert.ok(strip.includes("✕"), "the death has its mark");
assert.ok(strip.includes('data-fd-rtfight="k1"'), "a block selects its fight");
assert.ok(/class="fd-rtcur" style="left:100\.00%"/.test(strip), "with no replay place the cursor sits at the end");
const half = run("fdRunHtml(model, 'k1', { run: { t: 90000 } })", { model: runModel });
assert.ok(/class="fd-rtcur" style="left:50\.00%"/.test(half), "the scrubber's place moves the cursor");

// ---- the control row: Replay, the run-length scrubber and "m:ss of m:ss", enabled only with kept positions ---------------------
const track = run("FightReplay.prep(json)", { json: { frames: [
  { t: 1000, map: 249, bots: [[1, 0, 0, 100, 1, 249]], mobs: [] },
  { t: 1100, map: 249, bots: [[1, 1, 0, 100, 1, 249]], mobs: [] },
  { t: 1180, map: 249, bots: [[1, 2, 0, 100, 1, 249]], mobs: [] },
], names: {} } });
const play = run("fdRunPlayHtml(model, { t: null, playing: false, track })", { model: runModel, track: { prep: track } });
assert.ok(play.includes('data-fd-rtplay'), "the Replay button");
assert.ok(play.includes('max="180000"'), "the scrub runs the whole run");
assert.ok(play.includes("3:00 of 3:00"), "the clock reads m:ss of m:ss");
assert.ok(!/disabled/.test(play), "kept positions make the run playable");
const blind = run("fdRunPlayHtml(model, { t: 0, playing: false, track: null })", { model: runModel, track: null });
assert.strictEqual((blind.match(/disabled/g) || []).length, 2, "no positions: the button and the scrubber are disabled");
assert.ok(blind.includes("No kept positions for this run"), "and the row says why");
const evil = run("fdRunHtml({ t0: 0, t1: 10, spanMs: 10000, segs: [{ kind: 'boss', key: 'x', label: 'P1', name: evilName, fromS: 0, toS: 10 }], deaths: [] }, 'x', { run: { t: 0 } })", { evilName: '<img src=x onerror=alert(1)>"&' });
assert.ok(!/<img/i.test(evil), "a boss name is escaped");

// ---- one layer control: the map's Layers panel, the toolbar button gone in the debugger -----------------------------------------
assert.ok(/\.cmd-card\.fd-on \.cmd-maptools \[data-act="layers"\]\s*\{\s*display:\s*none;/.test(css),
  "the debugger hides the toolbar's duplicate Layers button");
assert.ok(/\.cmd-layers\.cmd-layers-map\s*\{[^}]*position:\s*absolute/.test(css), "the Layers panel sits on the map");
assert.ok(src.includes("stage.appendChild(box)") && src.includes("sum.textContent = 'Layers ▾'"), "the one panel moves into the map");
assert.ok((src.match(/data-act="layers"/g) || []).length === 1, "only the Live map toolbar carries the button in the markup");

// ---- the lanes: the prototype's caption, one lane per enemy, the healer's time in red ------------------------------------------
const laneFight = { start: 1000, dur_ms: 10000, bots: [{ name: "Tank", guid: 1, role: "tank" }, { name: "Heal", guid: 2, role: "healer" }] };
const frame = (t, mobs) => ({ t, map: 1, step: 0, fight: true, bots: [], mobs });
const mob = (id, v, hp) => ({ id, e: 7, x: 0, y: 0, hp: hp == null ? 98 : hp, v });
const lanesTrack = { commander: "G", frames: [
  frame(1000, [mob("m1", 1)]), frame(1002, [mob("m1", 2)]), frame(1004, [mob("m1", 2)]), frame(1006, [mob("m1", 1)]),
  frame(1008, [mob("m1", 1), mob("m2", 1)]),
], names: { "7": "Onyxian Warder" } };
const lanesRes = run("AggroLanes.lanes(json, fight, [])", { json: lanesTrack, fight: laneFight });
const lanesHtml = run("fdLanesHtml(res, 10000)", { res: lanesRes });
assert.ok(lanesHtml, "the lane builder exists");
assert.ok(lanesHtml.includes("Aggro per mob: blue = on a tank, red = on anyone else."), "the prototype's caption");
assert.strictEqual((lanesHtml.match(/class="fd-lane"/g) || []).length, 2, "one lane per reported enemy");
assert.ok(lanesHtml.includes("fd-seg k-healer bad"), "the healer's stretch is the red one");
assert.ok(lanesHtml.includes("Onyxian Warder"), "the lane is labelled");

// ---- the rail's total damage and the checks' bold key values ------------------------------------------------------------------
const cards = run("FightDebug.railCards(rows, details, null)", { rows: ROWS, details: DETAILS });
const rail = run("fdRailHtml(cards, null, 'k2')", { cards });
assert.ok(rail.includes("200 dmg"), "the fight rail shows the fight's total damage");
const check = { id: "mt_sunder", lvl: "warn", title: "Main tank Sunder rate", num: "2.0/10 s", ev: "x" };
const checkHtml = run("fdChecksHtml([check], [], {}, null)", { check });
assert.ok(checkHtml.includes("Main tank Sunder rate:"), "the Sunder rate title ends with a colon");
assert.ok(checkHtml.includes('<span class="fd-num">2.0 per 10 s</span>'), "the rate is the bold key value");
const outside = JSON.parse(run("JSON.stringify(fdCheckTitle({ id: 'taken_outside', title: 'Most damage taken outside the tanks: Kitetastic (damage)' }))"));
assert.deepStrictEqual(outside, { text: "Most damage taken outside the tanks:", hi: "Kitetastic (damage)" });
assert.strictEqual(run("fdCheckNum({ id: 'debuff:coe', num: '57%' })"), "57% of the fight");
assert.strictEqual(run("fdCheckNum({ id: 'taken_outside', num: '6%' })"), "6% of all damage taken");

// ---- the wiring: the strip sits above the raid frames, hidden in Live mode, and the map honours the run replay ----------------
const runSec = src.indexOf('class="fd-runtl"'), framesSec = src.indexOf('aria-label="Raid frames"');
assert.ok(runSec > 0 && runSec < framesSec, "the run strip is above the raid frames");
assert.ok(src.includes("card.querySelectorAll('.fd-runtl, .fd-raid,"), "the strip follows the debugger mode");
assert.ok(src.includes("fdReplayActive(key) || fdRunActive(key)"), "the map draws the run replay when it is on");
assert.ok(src.includes("'&from=' + from + '&to=' + to"), "the run's kept positions are fetched once per run");
assert.ok(src.includes("FD_TRACK_WINDOW_S"), "a run longer than the kept window is clamped");
assert.ok(src.includes("setTimeout(() => fdRunTick(key)"), "the run plays as a setTimeout chain");
assert.ok(grab("fdRunStop").includes("clearTimeout("), "stopping the run clears the pending step");
assert.strictEqual((src.match(/setInterval\(/g) || []).length, 9, "no new setInterval but the live stream's fallback poll");

console.log("fight debugger run timeline ok");
