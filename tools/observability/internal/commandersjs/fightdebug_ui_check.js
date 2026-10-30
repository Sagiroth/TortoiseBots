"use strict";
// Run by commandersjs_test.go under node: the Fight debugger's markup builders in app.js (web/static/js/app.js), fed with the
// walk 2 fights and with names that would break the page. Every field of a builder is escaped, the frames, checks, timeline,
// rail, roles and bot panel carry the states the card promises, and the page loads the debugger's logic and its own script.
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
  const end = src.indexOf("\n  };\n", m.index), line = src.indexOf(";\n", m.index);
  return src.slice(m.index, (end > 0 && end < line + 400 && src.slice(m.index, line).includes("{\n") ? end + 5 : line + 1));
}

const ctx = { console };
vm.createContext(ctx);
vm.runInContext(fs.readFileSync(path.join(dir, "fightdebug.js"), "utf8"), ctx, { filename: "fightdebug.js" });
assert.ok(ctx.FightDebug && typeof ctx.FightDebug.checks === "function", "fightdebug.js loads as the global FightDebug");
vm.runInContext(
  [grab("esc"), grabConst("CMD_MAP_ICONS"), grab("cmdMapIcon"), grab("cmdFightMarksSvg"), grab("fdFramesHtml"),
    grab("fdRailHtml"), grab("fdChecksHtml"), grab("fdCheckNum"), grab("fdCheckTitle"), grab("fdTimelineHtml"), grab("fdRecapHtml"), grab("fdRolesHtml"),
    grab("fdLiveTableHtml"), grab("fdBotHtml")].join("\n") +
  "\nvar CmdView = { project: (x, y) => ({ x: -y, y: -x }) };" +
  "\nvar cmdBotLink = (guid, name) => '<a class=\"cmd-botlink\" href=\"#bot/' + guid + '\">' + esc(name) + '</a>';", ctx);

const run = (expr, vars) => { if (vars) Object.assign(ctx, vars); return vm.runInContext(expr, ctx); };
const fights = JSON.parse(fs.readFileSync(path.join(__dirname, "testdata", "walk2-fights.json"), "utf8"));
const byId = (id) => fights.find((f) => f.id === id);
const F54 = byId("5-4");
const LAYOUT = { mt: "Ragemeister", ot: ["Brun", "Irelanthe"] };
const ROWS = fights.map((f) => ({ key: f.id, dur: f.dur_ms, step: f.step, deaths: (f.deaths || []).length, boss_name: f.boss_name }));
const DETAILS = {};
fights.forEach((f) => { DETAILS[f.id] = { fight: f, warnings: [] }; });
const RECAPS = ctx.FightDebug.deathRecaps(F54);

// ---- escaping: nothing that comes from the game server may close an attribute or become markup -----------------------
const EVIL = '<img src=x onerror=alert(1)>"&';
function assertEscaped(html, what) {
  assert.ok(!/<img/i.test(html), what + ": raw markup survived: " + html.slice(0, 200));
  for (const m of html.matchAll(/="([^"]*)"/g)) assert.ok(!/[<>]/.test(m[1]), what + ": an attribute value broke out: " + m[1]);
  assert.strictEqual((html.match(/"/g) || []).length % 2, 0, what + ": unbalanced quotes: " + html.slice(0, 200));
  assert.ok(html.includes("&lt;img"), what + ": the name is escaped as text");
}
const EVIL_FIGHT = {
  dur_ms: 20000,
  bots: [
    { name: EVIL, guid: 1, class: EVIL, role: "tank", sub: 0, dmg: 0, heal: 0, over: 0, taken: 10, casts: 1, spells: [{ n: EVIL, c: 2 }] },
    { name: "H", guid: 2, class: "priest", role: "healer", sub: 0, dmg: 0, heal: 300, over: 0, taken: 5, casts: 4, spells: [] },
  ],
  deaths: [{ name: EVIL, t_ms: 9000, x: 1, y: 2, killer: EVIL, hits: [{ a: EVIL, d: 700, t_ms: 8000 }] }],
  debuffs: { sampled_s: 10, up_s: {} },
  engaged: [{ e: 1, n: EVIL, c: 1 }],
};
assertEscaped(run("fdFramesHtml(FightDebug.raidFrames(evBind), null)", {
  evBind: { bots: [{ name: EVIL, guid: 9, role: "tank", class: EVIL, sub: 0, hp: 5, max_hp: 10, power: 1, max_power: 2, power_type: "mana", alive: true }], layout: { mt: EVIL, ot: [] } },
}), "raid frames");
assertEscaped(run("fdRailHtml(cards, null, 'k')", {
  cards: run("FightDebug.railCards(rows, details, null)", { rows: [{ key: "k", dur: 20000, step: 1, deaths: 1, boss_name: EVIL }], details: { k: { fight: EVIL_FIGHT, warnings: [{ rule: "r", level: "red", bot: EVIL, text: EVIL }] } } }),
}), "fight rail");
const evilRecaps = ctx.FightDebug.deathRecaps(EVIL_FIGHT);
assertEscaped(run("fdChecksHtml(FightDebug.checks(f, null, [{ rule: 'r', level: 'red', bot: 'x', text: 'rule text' }]), recaps)", { f: EVIL_FIGHT, recaps: evilRecaps }), "checks");
assertEscaped(run("fdTimelineHtml(FightDebug.timeline(f), recaps, 0)", { f: EVIL_FIGHT, recaps: evilRecaps }), "timeline");
assertEscaped(run("fdRecapHtml(recaps, 0)", { recaps: evilRecaps }), "death recap");
assertEscaped(run("fdRolesHtml(FightDebug.roleTables(f, { mt: 'x', ot: [] }), null).join('')", { f: EVIL_FIGHT }), "role tables");
assertEscaped(run("fdLiveTableHtml(FightDebug.liveTables(bind), null, 10)", {
  bind: { bots: [{ name: EVIL, guid: 1, role: "dps" }], combat: { win_s: 10, fight_ms: 9000, bots: [{ g: 1, dps: 10, hps: 0, c: 1, ls: EVIL }] } },
}), "live table");
assertEscaped(run("fdBotHtml(panel)", { panel: { name: EVIL, cls: EVIL, sub: 0, dmg: 1, heal: 0, taken: 0, casts: 2, guid: 1, spells: [{ n: EVIL, c: 2 }] } }), "bot panel");
assertEscaped(run("cmdFightMarksSvg(dc, recaps, 0)", { dc: { f: (n) => n.toFixed(2), k: 0.5, units: {} }, recaps: evilRecaps }), "map death marks");

// ---- raid frames: every bot a button, the layout tags, one selected ------------------------------------------------
const frames54 = run("FightDebug.raidFrames(bind54)", { bind54: { bots: F54.bots, layout: LAYOUT } });
const framesHtml = run("fdFramesHtml(frames, 'Brun')", { frames: frames54 });
assert.deepStrictEqual([...framesHtml.matchAll(/<span class="fd-tag">(\w+)<\/span>/g)].map((m) => m[1]), ["MT", "OT", "OT"]);
assert.strictEqual((framesHtml.match(/<button type="button"/g) || []).length, 39, "one button per bot");
assert.strictEqual((framesHtml.match(/<button type="button" class="fd-tile[^"]*" data-fd-bot="[^"]*" aria-pressed="[^"]*" aria-label="[^"]*">/g) || []).length, 39,
  "every tile is a button with the bot, its pressed state and a label");
assert.strictEqual((framesHtml.match(/aria-pressed="true"/g) || []).length, 1);
assert.ok(framesHtml.includes('class="fd-tile r-tank sel" data-fd-bot="Brun" aria-pressed="true"'), "the selected bot is marked");
assert.ok(framesHtml.indexOf("Group 1") < framesHtml.indexOf("Group 8"), "groups ascending");
assert.ok(framesHtml.includes("Group 1 · 5/5"), "the group header counts the living: " + framesHtml.slice(0, 120));

// ---- checks: the failing tank death first, ten rows, the recap and show buttons ------------------------------------
const ch54 = run("FightDebug.checks(f, layout, [])", { f: F54, layout: LAYOUT });
const checksHtml = run("fdChecksHtml(checks, recaps)", { checks: ch54, recaps: RECAPS });
assert.strictEqual((checksHtml.match(/<li /g) || []).length, 10);
const firstLi = checksHtml.slice(0, checksHtml.indexOf("</li>") + 5);
assert.ok(firstLi.includes('class="fd-check l-fail"'), firstLi);
assert.ok(firstLi.includes('class="fd-lvl">PROBLEM<'), firstLi);
assert.ok(firstLi.includes('class="fd-ttl">Tank died: Irelanthe (2×)<'), firstLi);
assert.ok(firstLi.includes('class="fd-num">2<'), firstLi);
assert.ok(firstLi.includes('data-fd-bot="Irelanthe"'), firstLi);
assert.ok(firstLi.includes('data-fd-death="0"') && firstLi.includes(">recap</button>"), firstLi);
// With the fight's guids the check's show button opens the bot's profile and says whether that bot is selected.
const guidChecks = run("fdChecksHtml(checks, recaps, guids, 'Irelanthe')", { checks: ch54, recaps: RECAPS, guids: { Irelanthe: 814, Brun: 815 } });
assert.ok(guidChecks.includes('class="fd-link" data-fd-profile="814" aria-pressed="true">show</button>'), guidChecks.slice(0, 400));
assert.ok(guidChecks.includes('data-fd-profile="815" aria-pressed="false">show</button>'), guidChecks.slice(0, 400));
assert.strictEqual((guidChecks.match(/aria-pressed="true"/g) || []).length, 2, "only the selected bot's show buttons are pressed (the tank death and her taunt row)");

// ---- timeline: the deaths on their marks, the recap of the selected one ---------------------------------------------
const tl54 = run("FightDebug.timeline(f)", { f: F54 });
const tlHtml = run("fdTimelineHtml(tl, recaps, null)", { tl: tl54, recaps: RECAPS });
const skulls = [...tlHtml.matchAll(/<button type="button" class="fd-skull fd-death" data-fd-death="(\d+)" data-x="[^"]*" data-y="[^"]*" style="left:([\d.]+)%" aria-label="[^"]*">/g)];
assert.strictEqual(skulls.length, 2, tlHtml.slice(0, 200));
assert.deepStrictEqual(skulls.map((m) => m[2]), ["30.78", "64.16"]);
assert.strictEqual((tlHtml.match(/class="fd-tick"/g) || []).length, 8);
assert.ok(tlHtml.includes('class="fd-hit"'), "the hits before a death");
assert.ok(run("fdTimelineHtml(FightDebug.timeline(f), [], null)", { f: { dur_ms: 5000, bots: [], deaths: [] } }).includes("No deaths this fight."));
const recapHtml = run("fdRecapHtml(recaps, null)", { recaps: RECAPS });
assert.ok(recapHtml.includes('Died <button type="button" class="fd-botbtn" data-fd-profile="814">Irelanthe</button> at 12.0 s, killed by Onyxian Warder'), recapHtml);
assert.ok(recapHtml.includes('6,422</span> damage in <span class="mono">5.0</span> s'), recapHtml);
assert.strictEqual((recapHtml.match(/<tr><td class="mono">/g) || []).length, 5, "the five hits of the first death");
const recapOne = run("fdRecapHtml(recaps, 1)", { recaps: RECAPS });
assert.strictEqual((recapOne.match(/<tr><td class="mono">/g) || []).length, 1, "the second death keeps its own hit only");

// ---- rail: five fights, the chosen one pressed, the live card first ------------------------------------------------
const cards = run("FightDebug.railCards(rows, details, layout)", { rows: ROWS, details: DETAILS, layout: LAYOUT });
const railHtml = run("fdRailHtml(cards, null, '5-4')", { cards });
assert.strictEqual((railHtml.match(/<button type="button" class="fd-fight/g) || []).length, 5);
assert.strictEqual((railHtml.match(/aria-pressed="true"/g) || []).length, 1);
assert.deepStrictEqual([...railHtml.matchAll(/data-fd-fight="([^"]*)" aria-pressed="true"/g)].map((m) => m[1]), ["5-4"]);
assert.ok(railHtml.includes(">problem<") && railHtml.includes(">check<") && railHtml.includes(">clean<"), "the verdict words");
// Pack N counts the fights oldest first, the rail reads Pack 1 at the top, the mob is the subtitle.
assert.deepStrictEqual([...railHtml.matchAll(/<span class="fd-fl">([^<]*)<\/span>/g)].map((m) => m[1]),
  ["Pack 1", "Pack 2", "Pack 3", "Pack 4", "Pack 5"]);
assert.strictEqual(railHtml.indexOf("Pack 1") < railHtml.indexOf("Pack 5"), true, "Pack 1 comes first");
assert.ok(railHtml.includes('<span class="fd-fs">Onyxian Warder</span>'), railHtml.slice(0, 260));
assert.ok(railHtml.includes('<span class="fd-fd"><span>step 22 · 39 s · 2 deaths</span>'), railHtml.slice(0, 400));
assert.ok(/<span class="fd-fdmg">[\d,]+ dmg<\/span>/.test(railHtml), "the fight's total damage sits at the bottom right: " + railHtml.slice(0, 500));
const railLive = run("fdRailHtml(cards, live, 'live')", { cards, live: { fightMs: 65000, winS: 10, dps: 4321, hps: 900, active: 31, alive: 39 } });
assert.ok(railLive.startsWith('<button type="button" class="fd-fight v-live'), railLive.slice(0, 100));
assert.ok(railLive.indexOf('data-fd-fight="live"') < railLive.indexOf('data-fd-fight="5-1"'), "the live fight comes first");
assert.ok(railLive.includes("Now · 1:05 · 4,321 dps"), railLive.slice(0, 220));
assert.strictEqual((railLive.match(/aria-pressed="true"/g) || []).length, 1);
// A real fight key is commander, session and id joined by NUL: only a percent-encoded key survives an HTML attribute.
const NUL_KEY = "Glacienne\u000017\u00005-5";
const nulHtml = run("fdRailHtml(cards, null, null)", { cards: run("FightDebug.railCards(rows, null, null)", { rows: [{ key: NUL_KEY, dur: 9000, step: 1, deaths: 0, boss_name: "Naxx" }] }) });
assert.ok(nulHtml.includes('data-fd-fight="Glacienne%0017%005-5"'), "the key is percent-encoded: " + nulHtml.slice(0, 140));
assert.ok(!nulHtml.includes("\u0000"), "no raw NUL in the markup");
// A card whose detail has not arrived must say why, never "loading" for ever: the second failure, an unasked older row and
// the one detail still on its way each read differently.
const stateRows = [{ key: "fail", dur: 1000 }, { key: "asked", dur: 1000 }, { key: "sel", dur: 1000 }, { key: "old", dur: 1000 }];
const stateCards = run("FightDebug.railCards(rows, {}, null, st)", {
  rows: stateRows,
  st: { failed: { fail: { n: 2 } }, loadingKey: "asked", asked: 2, retry: 2, sel: null },
});
assert.deepStrictEqual(stateCards.map((c) => c.detail), ["failed", "loading", "unasked", "unasked"], "the detail states: " + JSON.stringify(stateCards.map((c) => c.detail)));
const stateHtml = run("fdRailHtml(cards, null, null)", { cards: stateCards });
assert.ok(stateHtml.includes(">details not available<"), "a failed detail says so: " + stateHtml.slice(0, 400));
assert.ok(stateHtml.includes(">loading<"), "the card on its way still says loading");
assert.ok(stateHtml.includes(">details not loaded<"), "an older row the page never asked says so");
assert.ok(!stateHtml.includes(">loading<") || stateHtml.indexOf(">loading<") < stateHtml.indexOf(">details not available<"), "no card loads for ever");

// ---- map marks: one skull per death, the selected one bigger and named ---------------------------------------------
const marks = run("cmdFightMarksSvg(dc, recaps, 1)", { dc: { f: (n) => n.toFixed(2), k: 0.5, units: {} }, recaps: RECAPS });
assert.strictEqual((marks.match(/ic-skull/g) || []).length, 2);
assert.strictEqual((marks.match(/class="fdmark on"/g) || []).length, 1);
assert.strictEqual((marks.match(/class="fdmark"/g) || []).length, 1);
assert.strictEqual((marks.match(/class="fdlabel"/g) || []).length, 2);
assert.ok(marks.includes("killed by Onyxian Warder"), marks.slice(0, 200));

// ---- role tables: three tables, the main tank first, the selected row marked ---------------------------------------
const tables = run("FightDebug.roleTables(f, layout)", { f: F54, layout: LAYOUT });
const roles = run("fdRolesHtml(tables, 'Dawnfist')", { tables });
assert.ok(Array.isArray(roles) && roles.length === 3, "three tables: tanks, healers, damage");
const rolesHtml = roles.join("");
assert.strictEqual((rolesHtml.match(/class="fd-botbtn"/g) || []).length, 39);
assert.strictEqual((rolesHtml.match(/<tr class="sel">/g) || []).length, 1);
// A name is a profile button carrying its guid and whether that bot is the selected one.
assert.strictEqual((rolesHtml.match(/class="fd-botbtn" data-fd-profile="\d+" aria-pressed="(true|false)">/g) || []).length, 39);
assert.ok(roles[0].includes('data-fd-profile="813" aria-pressed="false">★ Ragemeister</button>'), roles[0].slice(0, 300));
assert.ok(roles[2].includes('data-fd-profile="828" aria-pressed="true">Dawnfist</button>'), roles[2].slice(0, 300));
assert.strictEqual((rolesHtml.match(/aria-pressed="true"/g) || []).length, 1);
assert.ok(roles[0].includes("<caption>Tanks</caption>") && roles[1].includes("<caption>Healers</caption>") && roles[2].includes("<caption>Damage</caption>"));
assert.ok(roles[0].indexOf("Ragemeister") < roles[0].indexOf("Brun"), "the main tank comes first");
assert.ok(roles[1].indexOf("Magnarr") > 0, "the top healer is in the healers table");
assert.ok(roles[2].indexOf("Dawnfist") > 0, "the top damage is in the damage table");
assert.strictEqual((roles[0].match(/class="fd-botbtn"/g) || []).length, 3, "three tank rows");
assert.strictEqual((roles[1].match(/class="fd-botbtn"/g) || []).length, 10, "ten healer rows");
assert.strictEqual((roles[2].match(/class="fd-botbtn"/g) || []).length, 26, "26 damage rows");

// ---- the bot panel and the live table ------------------------------------------------------------------------------
const panel = run("FightDebug.botPanel(f, 'Ragemeister')", { f: F54 });
const panelHtml = run("fdBotHtml(panel)", { panel: Object.assign({}, panel, { guid: 813 }) });
assert.ok(panelHtml.includes("Ragemeister · warrior · group 1"), panelHtml);
assert.ok(panelHtml.includes('<button type="button" class="fd-link" data-fd-profile="813">Gear &amp; profile →</button>'), "the panel opens the character's profile: " + panelHtml.slice(0, 240));
assert.ok(panelHtml.includes("Sunder Armor"), "the spell table");
const liveRows = run("FightDebug.liveTables(bind)", {
  bind: {
    bots: [{ name: "A", guid: 1, role: "tank" }, { name: "B", guid: 2, role: "dps" }],
    combat: { win_s: 10, fight_ms: 20000, bots: [{ g: 2, dps: 500, hps: 0, c: 4, ls: "Frostbolt" }, { g: 1, dps: 50, hps: 0, c: 2, ls: "Maul" }] },
  },
});
const liveHtml = run("fdLiveTableHtml(rows, 'B', 10)", { rows: liveRows });
assert.ok(liveHtml.includes("<caption>Live, last 10 s</caption>"));
assert.ok(liveHtml.indexOf(">B</button>") < liveHtml.indexOf(">A</button>"), "by damage in the window");
assert.ok(liveHtml.includes("Frostbolt") && liveHtml.includes('class="sel"'), liveHtml);

// ---- the page wires it up ------------------------------------------------------------------------------------------
assert.ok(src.includes("'/api/v1/combat/fight/' + encodeURIComponent("), "the fight detail is fetched by its encoded key");
assert.ok(src.includes("FightDebug.worstIndex("), "the debugger opens on the worst fight");
const html = fs.readFileSync(path.join(dir, "..", "..", "index.html"), "utf8");
assert.ok(src.includes("cmdPageMode("), "the page decides the view for every commander card");
assert.ok(html.includes('data-page-mode="debug"'), "the page offers the debugger mode");
assert.ok(src.includes('data-act="whole"'), "the map offers the whole picture");
assert.ok(html.indexOf("/static/js/fightdebug.js") > 0 && html.indexOf("/static/js/fightdebug.js") < html.indexOf("/static/js/app.js"),
  "index.html loads fightdebug.js before app.js");

// ---- the plan facts a finished fight's checks get (the lanes result, the ring strays, the pack pair) ------------------
vm.runInContext([grab("fdFightExtra"), grab("fdPrevPackI")].join("\n"), ctx);
vm.runInContext(fs.readFileSync(path.join(dir, "planoverlay.js"), "utf8"), ctx);
assert.ok(ctx.PlanOverlay && typeof ctx.PlanOverlay.strays === "function", "planoverlay.js loaded for the real plan view");
const planJson = JSON.parse(fs.readFileSync(path.join(__dirname, "..", "plans", "testdata", "onyxias_lair.plan.json"), "utf8"));
ctx.cmdPlanName = (bind) => bind.planName || "";
ctx.cmdPlanGet = (name) => (name === "onyxias_lair" ? planJson : null);
const bind18 = { commander: "C", bots: [{ level: 60 }], run: { step: 18 }, planName: "onyxias_lair" };
const extra18 = run("fdFightExtra(bind, fight, null)", { bind: bind18, fight: { map: 249, plan: "onyxias_lair", step: 18, dur_ms: 20000 } });
assert.strictEqual(extra18.packI, 21, "the fight's own pack");
assert.strictEqual(extra18.prevPackI, 17, "the pack before it");
assert.strictEqual(extra18.strays.length, 0, "the plan's own strays (this plan is clean)");
assert.strictEqual(extra18.lanes, null, "no positions loaded yet");
const noPlan = run("fdFightExtra(bind, fight, null)", { bind: { commander: "C", bots: [] }, fight: { map: 249, plan: "", step: 5 } });
assert.strictEqual(noPlan.packI, 0);
assert.strictEqual(noPlan.strays, null, "a fight without a plan has no ring row");
assert.strictEqual(run("FightDebug.checks(fight, null, [], extra).some(c => c.id === 'plan_strays')",
  { fight: { map: 249, plan: "", step: 5, dur_ms: 20000, bots: [], deaths: [], debuffs: {} }, extra: noPlan }), false);
assert.strictEqual(run("fdFightExtra(bind, fight, null)", { bind: bind18, fight: { map: 409, plan: "onyxias_lair", step: 5 } }).strays,
  null, "a plan on another map is no plan facts");
assert.strictEqual((src.match(/AggroLanes\.lanes\(/g) || []).length, 1, "one lanes result feeds the strip and the checks");
assert.ok(src.includes("det.extra = extra"), "the fight's extra reaches the rail's verdict too");
assert.ok(src.includes("FightDebug.checks(fight, layout, det.warnings || [], extra)"), "one checks call with the extra");

// The lane row on the card: attempt 2's checks render as a PROBLEM with the bot's name and the seconds.
const Fd = require(path.join(dir, "fightdebug.js"));
const AggroHost = require(path.join(dir, "aggrolanes.js"));
const a2Fixture = JSON.parse(fs.readFileSync(path.join(__dirname, "testdata", "lanes-a2.json"), "utf8"));
const a2Checks = Fd.checks(a2Fixture.fight, { mt: "Bonevale", ot: [] }, [], {
  lanes: AggroHost.lanes(a2Fixture.track, a2Fixture.fight, a2Fixture.fight.bots), strays: null, packI: 0, prevPackI: 0,
});
const a2Html = run("fdChecksHtml(checks, [], {}, '')", { checks: a2Checks });
assert.ok(a2Html.includes("Enemy on a non-tank: Onyxia"), "the lane row renders: " + a2Html.slice(0, 200));
assert.ok(a2Html.includes('<span class="fd-num">12 s</span>'), "the longest hold in seconds");
assert.ok(a2Html.includes("PROBLEM"), "a 12 s hold is a problem");
console.log("fight debugger ui ok");
