"use strict";
// Run by commandersjs_test.go under node: the live fight's gate (web/static/js/fightdebug.js liveEngaged/liveSummary)
// and what it does to the page (app.js fdRailHtml's Live card and fdSyncSel's pick). A parked raid casting buffs and
// healing does not have a fight: no Live card, no live pick (so no LIVE checks panel); a hostile mob whose victim is a
// group member, or damage the group dealt in the module's 10 s window, does; and with no hostile engaged for 10 s (the
// mob dead or evaded and the window emptied) the fight closes again.
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");
const appPath = path.resolve(process.argv[2]);
const F = require(path.resolve(process.argv[3]));

function grab(name) {
  const src = fs.readFileSync(appPath, "utf8");
  const re = new RegExp(`^  function ${name}\\(`, "m"), m = re.exec(src);
  assert.ok(m, `${name} not found`);
  const end = src.indexOf("\n  }\n", m.index);
  return src.slice(m.index, end + 4);
}

const ctx = { console, FightDebug: F };
vm.createContext(ctx);
vm.runInContext([grab("esc"), grab("fdRailHtml"), grab("fdSyncSel"), "var fdFightHash = () => {};"].join("\n"), ctx);
const run = (expr, vars) => { if (vars) Object.assign(ctx, vars); return vm.runInContext(expr, ctx); };

const bots = [
  { name: "Tanky", guid: 813, id: "813", role: "tank", alive: true },
  { name: "Healy", guid: 814, id: "814", role: "healer", alive: true },
];
const window10 = [{ g: 814, dps: 0, hps: 250, c: 3, ls: "Power Word: Fortitude" }];
const warder = { id: "17379391165513648980", entry: 12129, name: "Onyxian Warder", reaction: "h" };

// ---- a parked raid: casts and heals, no damage, a hostile mob near it that fights nobody --------------------------
const parked = {
  commander: "Glacienne", bots: bots, mobs: [warder],
  combat: { win_s: 10, fight_ms: 173 * 60000 + 23000, bots: window10 },
};
assert.strictEqual(F.liveEngaged(parked), false, "buffs and heals on a parked raid are not engagement");
assert.strictEqual(F.liveSummary(parked), null, "no live summary for a parked raid");
assert.strictEqual(run("fdRailHtml(cards, live, null, null)", { cards: [], live: F.liveSummary(parked) }),
  "", "the rail holds no Live card for a parked raid");

// ---- an engaged mob: its victim is a group member ----------------------------------------------------------------
const engaged = JSON.parse(JSON.stringify(parked));
engaged.mobs[0].victim = "813";
assert.strictEqual(F.liveEngaged(engaged), true, "a hostile mob fighting a group member is engagement");
const live = F.liveSummary(engaged);
assert.ok(live && live.fightMs === 173 * 60000 + 23000 && live.winS === 10 && live.active === 1 && live.alive === 2,
  "the live summary carries the window, the bots acting and the living: " + JSON.stringify(live));
const html = run("fdRailHtml(cards, live, 'live', null)", { cards: [], live: live });
assert.ok(html.indexOf('class="fd-fight v-live') >= 0, "an engaged raid shows the Live card: " + html);
assert.ok(html.indexOf("Now · 173:23") >= 0, "with the fight's clock: " + html);
assert.ok(html.indexOf("2 alive · 1 acting") >= 0, "and the alive and acting counts: " + html);

// ---- only a hostile unit counts; a non-member victim is not the group's fight -----------------------------------
const neutral = JSON.parse(JSON.stringify(engaged));
neutral.mobs[0].reaction = "n";
assert.strictEqual(F.liveSummary(neutral), null, "a neutral mob's victim is not a hostile engagement");
const stranger = JSON.parse(JSON.stringify(engaged));
stranger.mobs[0].victim = "999";
assert.strictEqual(F.liveSummary(stranger), null, "a hostile mob targeting a non-member is not engagement");

// ---- damage dealt to a hostile in the 10 s window is engagement too (a mob the snapshot no longer lists) ---------
const dps = JSON.parse(JSON.stringify(parked));
dps.mobs = [];
assert.strictEqual(F.liveSummary(dps), null, "no mob and no damage: no fight");
dps.combat.bots[0].dps = 42;
assert.strictEqual(F.liveEngaged(dps), true, "damage in the module's 10 s window is engagement");

// ---- the fight closes: the mob died or evaded and the window emptied (no dps, no victim) ------------------------
const closed = JSON.parse(JSON.stringify(engaged));
closed.mobs = [];
assert.strictEqual(F.liveSummary(closed), null, "no hostile engaged for 10 s: the live fight closes");
assert.strictEqual(run("fdRailHtml(cards, live, null, null)", { cards: [], live: F.liveSummary(closed) }),
  "", "and its Live card goes");

// ---- the page's pick: no live selection without a fight, so the LIVE checks panel is never shown for a parked raid -
const pick = (fd, bind) => { run("fdSyncSel(fd, bind, [], [], null)", { fd: fd, bind: bind }); return fd; };
assert.strictEqual(pick({ sel: null, userSel: false, day: null }, parked).sel, null,
  "a parked raid is not picked live (its LIVE checks panel is not shown)");
assert.strictEqual(pick({ sel: null, userSel: false, day: null }, engaged).sel, "live",
  "an engaged raid opens on its live fight");
const chosen = pick({ sel: "live", userSel: true, day: null }, parked);
assert.strictEqual(chosen.sel, null, "a live pick is handed back when the fight is not engaged");
assert.strictEqual(chosen.userSel, false, "and stops standing as the user's own click");
assert.strictEqual(pick({ sel: "live", userSel: true, day: null }, engaged).sel, "live",
  "an engaged fight keeps the user's live pick");

// A report from an older module: bots carry only their low guid, a mob's victim is that decimal string.
const old = JSON.parse(JSON.stringify(engaged));
old.bots = bots.map((b) => ({ name: b.name, guid: b.guid, role: b.role, alive: true }));
old.mobs[0].victim = "814";
assert.strictEqual(F.liveEngaged(old), true, "the low guid matches a victim when the raw id is absent");

console.log("fd live engagement: ok");
