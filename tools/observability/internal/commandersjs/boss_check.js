"use strict";
// Run by commandersjs_test.go under node: a boss on the Commanders map as app.js builds it — the big purple body with the gold
// outline and the pulsing ring (a static double ring with reduced motion), the crown above the body, the name with its health,
// the edge marker of a boss off the view, the draw order (bosses last), the replay keeping the rank, the legend and the CSS.
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");

const appPath = path.resolve(process.argv[2]), dir = path.dirname(appPath);
const css = fs.readFileSync(path.resolve(process.argv[3]), "utf8");
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
["commanders.js", "unitinfo.js", "fightreplay.js", "bosspanel.js"].forEach((f) => {
  vm.runInContext(fs.readFileSync(path.join(dir, f), "utf8"), ctx, { filename: f });
});
vm.runInContext([grab("esc"), grabConst("CMD_MAP_ICONS"), grab("cmdMapIcon"), grab("cmdMobOverlaySvg"), grab("cmdMobLook"),
  grab("cmdMobSvg"), grab("cmdBossPoint"), grab("cmdLegendHtml"), grab("cmdBossTable"), grab("cmdApplyBossRanks"),
  grab("cmdBossTableLoaded"), grab("fdReplayBind")].join("\n"), ctx);

// ---- the look: a boss is 14 screen px across at any zoom, an elite a step up, a normal unit unchanged
{
  const boss = ctx.cmdMobLook({ rank: "b" }, true), elite = ctx.cmdMobLook({ rank: "e" }, true), plain = ctx.cmdMobLook({ reaction: "h" }, true);
  assert.deepStrictEqual([boss.r, boss.boss, boss.elite], [14, true, false]);
  assert.deepStrictEqual([elite.r, elite.boss, elite.elite], [7, false, true]);
  assert.strictEqual(plain.r, 5, "a normal mob keeps its size");
  assert.strictEqual(ctx.cmdMobLook({ rank: "b" }, false).r, 5, "the layer off: a boss is drawn like any unit");
}

// ---- the group: the purple body with the gold ring and the crown above it, the name with the health
const c = () => ({ f: (n) => n.toFixed(2), k: 0.5, units: {} });
const on = { bossmark: true, hostilenames: false, hpbars: false, statusicons: false, mobcast: false, facing: false, threatcolors: false };
const draw = (m, look) => ctx.cmdMobSvg(c(), m, { on: look || on, marks: { tank: {}, pull: {} }, onMap: [], botsById: {}, overlay: () => "", leftMs: 0 });
{
  const mx = c();
  const boss = { id: "5", entry: 10184, name: "Onyxia", hp: 63, x: 0, y: 0, reaction: "h", rank: "b" };
  const svg = ctx.cmdMobSvg(mx, boss, { on, marks: { tank: {}, pull: {} }, onMap: [], botsById: {}, overlay: () => "", leftMs: 0 });
  assert.ok(/<g class="mob hostile boss"/.test(svg), "the boss class: " + svg.slice(0, 120));
  assert.ok(/<circle class="body" cx="0\.00" cy="0\.00" r="7\.00"\/>/.test(svg), "body radius 14*k (k = 0.5): " + svg);
  assert.ok(/<circle class="bossring" cx="0\.00" cy="0\.00" r="10\.00"\/>/.test(svg), "the gold ring 6*k outside the body");
  assert.ok(/<circle class="bossring2" cx="0\.00" cy="0\.00" r="12\.00"\/>/.test(svg), "the static second ring");
  const crown = /<g class="ic ic-crown" transform="translate\((-?[\d.]+) (-?[\d.]+)\) scale\(([\d.]+)\)"/.exec(svg);
  assert.ok(crown, "the crown is drawn: " + svg);
  assert.ok(Number(crown[2]) < -7, "the crown is above the body, not on it: y " + crown[2]);
  assert.ok(Math.abs(Number(crown[3]) - (16 * 0.5) / 11) < 1e-3, "the crown is 16*k");
  assert.ok(/<text class="mname boss"[^>]*>Onyxia 63%<\/text>/.test(svg), "the name and the health: " + svg);
  assert.ok(mx.units["m:5"] && mx.units["m:5"].kind === "m", "the boss can still be hovered");
  // the health is left out when the report does not know it, and clipped a float
  assert.ok(/>Onyxia<\/text>/.test(draw({ id: "6", name: "Onyxia", x: 0, y: 0, reaction: "h", rank: "b" })));
  assert.ok(/>Onyxia 62%<\/text>/.test(draw({ id: "7", name: "Onyxia", hp: 62.4, x: 0, y: 0, reaction: "h", rank: "b" })));
  // an elite takes the smaller step, keeps the hostile colour and gets no crown
  const elite = draw({ id: "8", entry: 6910, name: "Stonevault Warden", hp: 80, x: 0, y: 0, reaction: "h", rank: "e" });
  assert.ok(/<g class="mob hostile elite"/.test(elite), elite.slice(0, 120));
  assert.ok(/<circle class="body" cx="0\.00" cy="0\.00" r="3\.50"\/>/.test(elite), "elite radius 7*k");
  assert.ok(!elite.includes("ic-crown") && !elite.includes("bossring"), "no crown or boss ring on an elite");
  // a normal unit is untouched: radius 5*k, no boss classes
  const plain = draw({ id: "9", entry: 1, name: "Rat", hp: 100, x: 0, y: 0, reaction: "h" });
  assert.ok(/<g class="mob hostile"/.test(plain) && !/boss|elite/.test(plain), plain.slice(0, 120));
  assert.ok(/<circle class="body" cx="0\.00" cy="0\.00" r="2\.50"\/>/.test(plain), "an ordinary unit keeps 5*k");
  // every server string is escaped
  const evil = draw({ id: "10", name: '<img src=x>', hp: 50, x: 0, y: 0, reaction: "h", rank: "b" });
  assert.ok(!/<img/.test(evil) && evil.includes("&lt;img"), evil);
}

// ---- the off-screen marker: nothing while the boss is in view; at the edge, pointing its way, with a crown
{
  const box = [0, 0, 100, 100]; // plane units on screen
  // project(x, y) = {x: -y, y: -x}: this boss stands at plane (200, 50), beyond the right edge
  const off = { x: -50, y: -200, name: "Onyxia" };
  const svg = ctx.cmdBossPoint(c(), off, box);
  assert.ok(/<g class="bosspoint" data-bx="-50" data-by="-200">/.test(svg), svg);
  assert.ok(/<title>Onyxia is off this part of the map: click to centre on it<\/title>/.test(svg), "the marker says why");
  const arrow = /<polygon class="bosspoint-arrow" points="([^"]+)"\/>/.exec(svg);
  assert.ok(arrow, "the gold arrow: " + svg);
  const tip = arrow[1].split(" ")[0].split(",").map(Number);
  assert.ok(tip[0] > 92, "the arrow points to the right edge: " + tip.join(","));
  assert.ok(/class="ic ic-crown"/.test(svg), "the crown rides the marker");
  // a boss in view draws no marker
  assert.strictEqual(ctx.cmdBossPoint(c(), { x: -50, y: -50 }, box), "", "inside the view: no marker");
  // off the top, the arrow points up
  const top = /<polygon class="bosspoint-arrow" points="([^"]+)"\/>/.exec(ctx.cmdBossPoint(c(), { x: 200, y: -50 }, box));
  const ttip = top[1].split(" ")[0].split(",").map(Number);
  assert.ok(ttip[1] < 20 && Math.abs(ttip[0] - 50) < 1, "the arrow points up: " + ttip.join(","));
}

// ---- draw order: a boss is kept out of the ordinary mob pass and drawn after the party
{
  const at = src.indexOf("  function drawCmdMap(");
  assert.ok(at > 0);
  const body = src.slice(at, src.indexOf("\n  }\n", at));
  const plain = body.indexOf("shown.filter(m => bosses.indexOf(m) < 0)");
  const bots = body.indexOf("onMap.slice().sort(");
  const bossPass = body.indexOf("bosses.forEach(m => { g += drawMob(m); })");
  const markers = body.indexOf("cmdBossPoint(dc, m, box)");
  assert.ok(plain > 0 && bots > 0 && bossPass > bots, "bosses are drawn after the bots");
  assert.ok(markers > bossPass, "the edge markers come after the bosses");
}

// ---- the controls: the tool button, its hidden rule and the click that centres on a marker
assert.ok(src.includes('data-act="boss" class="cmd-bossbtn"'), "the Boss button sits with Follow and Whole map");
assert.ok(src.includes("bossBtn.hidden = !(ms.bosses && ms.bosses.length)"), "hidden when no living boss is on the map");
assert.ok(/btn\.dataset\.act === 'boss'/.test(src) && /cmdBossPoint\(dc, m, box\)/.test(src), "the button centres on a boss");
assert.ok(/closest\('g\.bosspoint'\)/.test(src) && /const bx = Number\(n\.dataset\.bx\)/.test(src), "a click on the marker picks the boss");
assert.ok(/m\.view = m\.view \? \{ cx: p\.x, cy: p\.y, scale: m\.view\.scale \}/.test(src), "and centres the view on it");
assert.ok(/const bosses = on\.bossmark \? shown\.filter\(m => m\.rank === 'b'\) : \[\]/.test(src), "only the boss layer draws them apart");

// ---- the legend names the new look
const legend = ctx.cmdLegendHtml({ bossmark: true });
assert.ok(legend.includes("Boss: purple, gold ring, crowned, health shown"), legend);
assert.ok(legend.includes('class="lelite"') && legend.includes("Elite"), "the elite has its own entry");
assert.ok(!ctx.cmdLegendHtml({}).includes("Boss: purple"), "no boss layer, no entry");

// ---- the rank the map draws: a rank-3 add the table lists on this map is an elite, its boss keeps the crown ----
{
  const RDOC = {
    maps: [{ key: "mc", mapId: 409, bosses: [57642] }, { key: "tmh", mapId: 819, bosses: [62936, 62937] }],
    bosses: [
      { entry: 57642, name: "Sorcerer-Thane Thaurissan", map: "mc", adds: [{ entry: 57643, name: "Image of Sorcerer-Thane Thaurissan", role: "summon" }] },
      { entry: 62936, name: "Rotgrowl", map: "tmh", adds: [{ entry: 62937, name: "Kodiak", role: "summon" }] },
      { entry: 62937, name: "Kodiak", map: "tmh", abilities: [{ spell: 1, name: "Swipe" }] },
    ],
  };
  const noTable = { mobs_map: 819, mobs: [{ entry: 62937, rank: "b" }] };
  ctx.cmdApplyBossRanks(noTable);
  assert.deepStrictEqual(noTable.mobs.map((m) => m.rank), ["b"], "no table loaded: nothing is demoted");
  const data = ctx.BossPanel.data;
  ctx.BossPanel.data = () => ctx.BossPanel.normalize(RDOC);
  const bind = { mobs_map: 409, mobs: [{ entry: 57643, rank: "b" }, { entry: 57642, rank: "b" }] };
  ctx.cmdApplyBossRanks(bind);
  assert.deepStrictEqual(bind.mobs.map((m) => m.rank), ["e", "b"], "the Image is drawn as an elite, its summoner as a boss");
  ctx.BossPanel.data = data;
}

// ---- the table arrives after a report: the report already on the page is ranked when it loads, before the rerender --------
{
  const DOC = { maps: [{ key: "mc", mapId: 409 }], bosses: [
    { entry: 57642, name: "Sorcerer-Thane Thaurissan", map: "mc",
      adds: [{ entry: 57643, name: "Image of Sorcerer-Thane Thaurissan", role: "summon" }] },
  ] };
  const stored = { commander: "c", mobs_map: 409, mobs: [{ entry: 57643, rank: "b" }, { entry: 57642, rank: "b" }] };
  ctx.state = { activeTab: "commanders", commanders: { list: [stored] } };
  let painted = null;
  ctx.covCompute = () => {};
  ctx.renderCoverage = () => {};
  ctx.renderCommanders = () => { painted = stored.mobs.map((m) => m.rank); };
  const data = ctx.BossPanel.data;
  ctx.BossPanel.data = () => null; // the first report lands while the static table is still loading
  ctx.cmdApplyBossRanks(stored);
  assert.deepStrictEqual(stored.mobs.map((m) => m.rank), ["b", "b"], "no table yet: the report's own rank stands");
  ctx.BossPanel.data = () => ctx.BossPanel.normalize(DOC);
  ctx.cmdBossTableLoaded();
  ctx.BossPanel.data = data;
  assert.deepStrictEqual(stored.mobs.map((m) => m.rank), ["e", "b"], "the load ranks the stored report: the Image is an elite");
  assert.deepStrictEqual(painted, ["e", "b"], "and the rerender already sees it");
}

// ---- the ranks are set at both ingestion points: every fresh report and every replayed frame ------------------
{
  const at = src.indexOf("  function applyCommanders(");
  assert.ok(at > 0);
  const body = src.slice(at, src.indexOf("\n  }\n", at));
  const apply = body.indexOf("cmdApplyBossRanks(bind)"), noteTimes = body.indexOf("cmdNoteMobTimes(bind");
  assert.ok(apply > 0 && noteTimes > apply, "a fresh report is ranked before its mob times are noted");
  const rp = src.indexOf("  function fdReplayBind(");
  assert.ok(rp > 0);
  const rpBody = src.slice(rp, src.indexOf("\n  }\n", rp));
  assert.ok(rpBody.includes("BossPanel.rankFor("), "the replay ranks its mobs with the same table");
}

// ---- the replay keeps the rank (a boss in the fight debugger is the same boss on the map)
{
  const track = { raw: null, prep: ctx.FightReplay.prep({ commander: "c", frames: [
    { t: 1000, map: 249, bots: [[7, 0, 0, 100, 1, 249]], mobs: [{ id: "5", e: 10184, x: 0, y: 0, hp: 63, rk: "b" }] },
  ] }) };
  const live = { commander: "c", bots: [{ name: "Tankie", guid: 7, max_hp: 100, map: 249 }], mobs: [] };
  const rp = ctx.fdReplayBind(live, { start: 1000, dur_ms: 2000, bots: [{ name: "Tankie", guid: 7 }] }, track, 1000);
  assert.strictEqual(rp.mobs[0].rank, "b", "the replay row keeps the boss rank");
  assert.ok(/<g class="mob hostile boss"/.test(draw(rp.mobs[0])), "and draws as a boss");
}

// ---- the replay ranks with the frame's map: a demoted add on its own map, the same report a boss on another
{
  const DOC = { maps: [{ key: "mc", mapId: 409 }, { key: "tmh", mapId: 819 }], bosses: [
    { entry: 57642, name: "Sorcerer-Thane Thaurissan", map: "mc",
      adds: [{ entry: 57643, name: "Image of Sorcerer-Thane Thaurissan", role: "summon" }] },
  ] };
  const track = (mapId) => ({ raw: null, prep: ctx.FightReplay.prep({ commander: "c", frames: [
    { t: 1000, map: mapId, bots: [[7, 0, 0, 100, 1, mapId]], mobs: [{ id: "9", e: 57643, x: 0, y: 0, hp: 63, rk: "b" }] },
  ] }) });
  const live = { commander: "c", bots: [{ name: "Tankie", guid: 7, max_hp: 100, map: 409 }], mobs: [] };
  const fight = { start: 1000, dur_ms: 2000, bots: [{ name: "Tankie", guid: 7 }] };
  const data = ctx.BossPanel.data;
  ctx.BossPanel.data = () => ctx.BossPanel.normalize(DOC);
  const onMc = ctx.fdReplayBind(live, fight, track(409), 1000);
  const onTmh = ctx.fdReplayBind(live, fight, track(819), 1000);
  ctx.BossPanel.data = data;
  assert.strictEqual(onMc.mobs[0].rank, "e", "the replayed Image on its map is an elite");
  assert.strictEqual(onTmh.mobs[0].rank, "b", "the same report on another map keeps its boss rank");
}

// ---- the CSS: the colours, the pulse and its reduced-motion form, the marker and the legend swatches
{
  assert.ok(/\.cmd-map \.mob\.boss circle\.body \{[^}]*fill: #7c3aed/.test(css), "the deep purple body");
  assert.ok(/\.cmd-map \.mob\.boss circle\.body \{[^}]*stroke: #facc15; stroke-width: 3/.test(css), "a 3 px gold outline");
  assert.ok(/\.cmd-map \.mob\.elite circle\.body \{[^}]*stroke: #c0c0c0/.test(css), "the silver elite outline");
  assert.ok(/\.cmd-map \.mob circle\.bossring \{[^}]*animation: bossring 1\.6s/.test(css), "the 1.6 s pulse");
  assert.ok(/@keyframes bossring \{ 0% \{[^}]*\} 100% \{[^}]*scale\(1\.5\)[^}]*opacity: 0/.test(css), "the pulse grows and fades");
  assert.ok(/@media \(prefers-reduced-motion: reduce\) \{[^}]*\.cmd-map \.mob circle\.bossring \{ animation: none/.test(css), "no pulse with reduced motion");
  assert.ok(/\.cmd-map \.mob circle\.bossring2 \{ display: inline; \}/.test(css), "a static double ring instead");
  assert.ok(/\.cmd-map \.bosspoint-arrow \{[^}]*fill: #facc15/.test(css), "the edge arrow is gold");
  assert.ok(/\.cmd-legend i\.lboss \{[^}]*background: #7c3aed; border: 3px solid #facc15/.test(css), "the legend swatch is the boss");
  assert.ok(/\.cmd-legend i\.lelite \{[^}]*border: 2px solid #c0c0c0/.test(css), "and the elite one is silver");
  assert.ok(/\.cmd-map text\.mname\.boss \{[^}]*font-weight: 700/.test(css), "the boss name is bold");
  assert.ok(/\.cmd-map text\.mname \{[^}]*paint-order: stroke/.test(css), "with a halo so it reads on any picture");
}
console.log("boss map ok");
