"use strict";
// Run by commandersjs_test.go under node: the boss overlay on the Commanders map as app.js builds it — the facing arrow,
// the spell's area under the dot, the cast's progress ring and its label — plus the fields the fight replay copies for it
// and the CSS block that styles them. A boss or elite only, every server string escaped, both layers honoured.
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

const ctx = { console };
vm.createContext(ctx);
["commanders.js", "fightreplay.js"].forEach((f) => {
  vm.runInContext(fs.readFileSync(path.join(dir, f), "utf8"), ctx, { filename: f });
});
assert.ok(ctx.CmdView && typeof ctx.CmdView.shapePath === "function", "commanders.js loads as CmdView");
assert.ok(ctx.FightReplay && typeof ctx.FightReplay.mobsAt === "function", "fightreplay.js loads as FightReplay");
vm.runInContext([grab("esc"), grab("cmdMobOverlaySvg"), grab("fdReplayBind")].join("\n"), ctx);
const V = ctx.CmdView;

// ---- the live overlay of one row
const c = { f: (n) => n.toFixed(2), k: 1, units: {} };
const bots = { 7: { id: "7", name: "Tankie" } };
const on = { facing: true, mobcast: true };
const boss = {
  id: "5", entry: 10184, name: "Onyxia", hp: 80, x: 0, y: 0, reaction: "h", rank: "b",
  o: 0, cid: 18435, cast: "Wing Buffet", cleft: 2000, ctot: 3000, ctg: "7",
  shape: { k: "cone", a: 120, l: 20 },
};
const svg = ctx.cmdMobOverlaySvg(c, boss, on, 1500, bots);
assert.deepStrictEqual(Object.keys(svg).sort(), ["over", "under"], "the caller draws the area below the dot, the rest above it");
assert.strictEqual((svg.under.match(/class="mobshape s-cone"/g) || []).length, 1, "one spell area");
assert.ok(!svg.under.includes("facing") && !svg.under.includes("castring"), "only the area sits under the dot: " + svg.under);
assert.strictEqual((svg.over.match(/class="facing"/g) || []).length, 1, "one facing arrow");
assert.strictEqual((svg.over.match(/class="castring"/g) || []).length, 1, "one cast ring");
assert.ok(/Wing Buffet 1\.5 s → Tankie/.test(svg.over), svg.over);
assert.ok(/d="M/.test(svg.under) && /<path /.test(svg.under), svg.under);

// an ordinary unit and a mob with no overlay data carry nothing
const parts = (r) => ({ under: r.under, over: r.over });
assert.deepStrictEqual(parts(ctx.cmdMobOverlaySvg(c, { id: "9", name: "Rat", hp: 100, x: 0, y: 0, reaction: "h" }, on, 1000, bots)), { under: "", over: "" });
assert.deepStrictEqual(parts(ctx.cmdMobOverlaySvg(c, Object.assign({}, boss, { rank: "r" }), on, 1000, bots)), { under: "", over: "" });

// each layer on its own
const facingOnly = ctx.cmdMobOverlaySvg(c, boss, { facing: true, mobcast: false }, 1500, bots);
assert.ok(facingOnly.over.includes("facing") && !facingOnly.over.includes("castring") && facingOnly.under === "");
const castOnly = ctx.cmdMobOverlaySvg(c, boss, { facing: false, mobcast: true }, 1500, bots);
assert.ok(!castOnly.over.includes("facing") && castOnly.over.includes("castring") && castOnly.under.includes("mobshape"));
assert.deepStrictEqual(parts(ctx.cmdMobOverlaySvg(c, boss, { facing: false, mobcast: false }, 1500, bots)), { under: "", over: "" });

// a cast whose time left has run out hides its area and ring (the report is stale, the mob may have moved on)
const ended = ctx.cmdMobOverlaySvg(c, Object.assign({}, boss, { cast: "" }), on, 0, bots);
assert.strictEqual(ended.under, ""); assert.ok(!ended.over.includes("castring") && !ended.over.includes("mobcast"));
// the facing stays: it is still where the unit looks
assert.ok(ended.over.includes("facing"));

// a target that is not one of the bound bots: no arrow part
const stranger = ctx.cmdMobOverlaySvg(c, Object.assign({}, boss, { ctg: "999" }), on, 1500, bots);
assert.ok(/Wing Buffet 1\.5 s<\/text>/.test(stranger.over), stranger.over);
const noTarget = ctx.cmdMobOverlaySvg(c, Object.assign({}, boss, { ctg: "" }), on, 1500, bots);
assert.ok(/Wing Buffet 1\.5 s<\/text>/.test(noTarget.over), noTarget.over);

// a channel and a line spell draw too
const channel = ctx.cmdMobOverlaySvg(c, Object.assign({}, boss, { ch: 1, shape: { k: "back-cone", a: 90, l: 12 } }), on, 1500, bots);
assert.ok(channel.under.includes("mobshape s-back-cone") && channel.over.includes("castring"), channel.under);
const line = ctx.cmdMobOverlaySvg(c, Object.assign({}, boss, { shape: { k: "line", l: 60, w: 6 } }), on, 1500, bots);
assert.ok(line.under.includes("mobshape s-line"), line.under);

// every server string is escaped: a name and a spell name that would otherwise start markup or break the attribute
const evil = Object.assign({}, boss, { name: '<img src=x onerror=alert(1)>', cast: 'Fire "in the hole" & more' });
const e = ctx.cmdMobOverlaySvg(c, evil, on, 1500, bots).over;
assert.ok(!/<img/.test(e) && !/Fire "in/.test(e), e);
assert.ok(/&quot;in the hole&quot; &amp; more/.test(e), e);
// an unknown spell area draws no path, the rest stays
const noShape = ctx.cmdMobOverlaySvg(c, Object.assign({}, boss, { shape: undefined }), on, 1500, bots);
assert.strictEqual(noShape.under, ""); assert.ok(noShape.over.includes("castring"));

// ---- the replay copies facing (angle-lerped), the cast's time left at the cursor and its name
const track = { raw: null, prep: ctx.FightReplay.prep({
  commander: "c", tracked: true, names: { 10184: "Onyxia" },
  frames: [
    { t: 1000, map: 249, step: 0, fight: true, bots: [[7, 0, 0, 100, 1, 249]], mobs: [{ id: "5", e: 10184, x: 0, y: 0, hp: 80, rk: "b", o: 0, cid: 18435, ce: 1001500, ctot: 3000, ct: 3000, ctg: "7", sh: { k: "cone", a: 120, l: 20 } }] },
    { t: 1002, map: 249, step: 0, fight: true, bots: [[7, 0, 0, 100, 1, 249]], mobs: [{ id: "5", e: 10184, x: 0, y: 0, hp: 80, rk: "b", o: 1, cid: 18435, ce: 1004000, ctot: 3000, ct: 3000, ctg: "7", cn: "Wing Buffet", sh: { k: "cone", a: 120, l: 20 } }] },
  ],
}) };
const live = { commander: "c", bots: [{ name: "Tankie", guid: 7, max_hp: 100, map: 249 }], mobs: [] };
const fight = { start: 1000, dur_ms: 5000, bots: [{ name: "Tankie", guid: 7 }] };
let rp = ctx.fdReplayBind(live, fight, track, 1002.5);
assert.strictEqual(rp.mobs.length, 1);
assert.strictEqual(rp.mobs[0].o, 1, "held at the last frame's facing");
assert.strictEqual(rp.mobs[0].cleft, 1500, "2000 ms at the last frame, 500 ms since");
assert.strictEqual(rp.mobs[0].cast, "Wing Buffet", "the cast's name from the frame");
assert.strictEqual(rp.mobs[0].cid, 18435);
assert.deepStrictEqual(JSON.parse(JSON.stringify(rp.mobs[0].shape)), { k: "cone", a: 120, l: 20 });
// between two frames the facing takes the shorter arc
rp = ctx.fdReplayBind(live, fight, track, 1001);
assert.ok(rp.mobs[0].o > 0.4 && rp.mobs[0].o < 0.6, "halfway: " + rp.mobs[0].o);
assert.strictEqual(rp.mobs[0].cleft, 500, "the frame after the cursor is not used for the cast");
// a replay row with no overlay fields stays plain
const plain = { raw: null, prep: ctx.FightReplay.prep({ commander: "c", frames: [{ t: 1000, map: 249, bots: [[7, 0, 0, 100, 1, 249]], mobs: [{ id: "6", e: 1, x: 1, y: 1, hp: 50 }] }] }) };
rp = ctx.fdReplayBind(live, { start: 1000, dur_ms: 1000, bots: [] }, plain, 1000.5);
assert.strictEqual(rp.mobs[0].o, undefined); assert.strictEqual(rp.mobs[0].cid, undefined); assert.strictEqual(rp.mobs[0].cleft, undefined);

// ---- the breath lane: a line whose ends are pinned in map coordinates, kept through the replay and drawn between them
const SEG = [20.730539, -215.23761, -130.7883, -213.424026];
const breathShape = { k: "line", l: 151.53, w: 60, seg: SEG.slice() };
const breathRow = (t, sh) => ({ t: t, map: 249, step: 0, fight: true, bots: [[7, 0, 0, 100, 1, 249]],
  mobs: [{ id: "5", e: 10184, x: 20.730539, y: -215.23761, hp: 80, rk: "b", o: 0.5, cid: 17086, ce: 1001500, ctot: 5000,
    ct: 5000, ctg: "7", ch: 1, cn: "Deep Breath", sh: sh }] });
const breathTrack = { raw: null, prep: ctx.FightReplay.prep({ commander: "c", tracked: true, names: { 10184: "Onyxia" },
  frames: [breathRow(1000, breathShape), breathRow(1002, breathShape)] }) };
const held = ctx.FightReplay.mobsAt(breathTrack.prep, 1000.5)["5"];
const plainSh = (sh) => (sh === undefined ? undefined : JSON.parse(JSON.stringify(sh)));
assert.deepStrictEqual(plainSh(held.sh), breathShape, "the held row keeps the lane's ends");
const between = ctx.FightReplay.mobsAt(breathTrack.prep, 1001)["5"];
assert.deepStrictEqual(plainSh(between.sh), breathShape, "the interpolated row keeps the lane's ends");
assert.strictEqual(between.ctg, "7", "the interpolated row keeps the cast's target");
assert.strictEqual(between.cn, "Deep Breath", "the interpolated row keeps the cast's name");
assert.strictEqual(between.ce, 1001500, "the interpolated row keeps the cast's absolute end");
const replayBand = ctx.fdReplayBind({ commander: "c", bots: [{ name: "Tankie", guid: 7, max_hp: 100, map: 249 }], mobs: [] },
  { start: 1000, dur_ms: 5000, bots: [{ name: "Tankie", guid: 7 }] }, breathTrack, 1001.5);
const rm = replayBand.mobs[0];
assert.deepStrictEqual(plainSh(rm.shape.seg), SEG, "the replay row reaching the map keeps the lane's ends");

// The band is drawn between the two pinned ends, wherever the unit stands, w yards across (Projected: north up, -y right).
const project = (x, y) => ({ x: -y, y: -x });
const bandPoints = (d) => (d.match(/-?\d+\.\d+/g) || []).map(Number);
function assertBand(d, seg, w, why) {
  assert.ok(/^M/.test(d) && /Z$/.test(d), why + ": one closed path: " + d);
  const n = bandPoints(d);
  assert.strictEqual(n.length, 8, why + ": four corners: " + d);
  const pts = [];
  for (let i = 0; i < 8; i += 2) pts.push({ x: n[i], y: n[i + 1] });
  const a = project(seg[0], seg[1]), b = project(seg[2], seg[3]);
  const mid = (p, q) => ({ x: (p.x + q.x) / 2, y: (p.y + q.y) / 2 });
  const near = (p, q) => Math.hypot(p.x - q.x, p.y - q.y) < 0.01;
  assert.ok(near(mid(pts[0], pts[1]), a), why + ": the near edge's middle is the segment's first end, got " + JSON.stringify(mid(pts[0], pts[1])));
  assert.ok(near(mid(pts[2], pts[3]), b), why + ": the far edge's middle is the segment's second end, got " + JSON.stringify(mid(pts[2], pts[3])));
  const span = Math.hypot(b.x - a.x, b.y - a.y), nx = -(b.y - a.y) / span, ny = (b.x - a.x) / span;
  const half = { x: nx * w / 2, y: ny * w / 2 };
  assert.ok(near(pts[0], { x: a.x + half.x, y: a.y + half.y }), why + ": the band is " + w + " yd across");
  assert.ok(near(pts[1], { x: a.x - half.x, y: a.y - half.y }), why + ": the band is " + w + " yd across");
}
const replaySvg = ctx.cmdMobOverlaySvg(c, rm, on, 1500, bots);
assert.strictEqual((replaySvg.under.match(/class="mobshape s-line"/g) || []).length, 1, replaySvg.under);
assertBand((/d="([^"]+)"/.exec(replaySvg.under) || [])[1] || "", SEG, 60, "the replay's breath band");
assert.ok(replaySvg.over.includes("castring") && /Deep Breath 1\.5 s → Tankie/.test(replaySvg.over), "the countdown ring and label as R2 draws them");

// the live map draws the same band from the server's own row: the unit's place and facing do not move a pinned lane
const liveBreath = Object.assign({}, boss, { x: 99, y: -88, o: 2.7, shape: breathShape });
const liveSvg = ctx.cmdMobOverlaySvg(c, liveBreath, on, 1500, bots);
assertBand((/d="([^"]+)"/.exec(liveSvg.under) || [])[1] || "", SEG, 60, "the live breath band");
// a pinned lane needs no facing: its ends say everything
const noFacing = ctx.cmdMobOverlaySvg(c, Object.assign({}, liveBreath, { o: undefined }), on, 1500, bots);
assertBand((/d="([^"]+)"/.exec(noFacing.under) || [])[1] || "", SEG, 60, "the live breath band without a facing");
assert.ok(!noFacing.over.includes("facing"), "no facing arrow without a facing");

// a seg that is not four finite numbers is ignored: the row falls back to a line along the facing, the old way
const noSeg = ctx.cmdMobOverlaySvg(c, Object.assign({}, boss, { shape: { k: "line", l: 60, w: 6 } }), on, 1500, bots).under;
for (const bad of [[20, -215, -130], [20, -215, -130, "x"], [20, null, -130, -213], "20,-215,-130,-213"]) {
  const got = ctx.cmdMobOverlaySvg(c, Object.assign({}, boss, { shape: { k: "line", l: 60, w: 6, seg: bad } }), on, 1500, bots).under;
  assert.strictEqual(got, noSeg, "a broken seg (" + JSON.stringify(bad) + ") draws the facing line, not a band");
}
const badTrack = { raw: null, prep: ctx.FightReplay.prep({ commander: "c", frames: [breathRow(1000, { k: "line", l: 151.53, w: 60, seg: [1, 2, 3] })] }) };
assert.strictEqual(ctx.FightReplay.mobsAt(badTrack.prep, 1000)["5"].sh.seg, undefined, "a broken seg is dropped by the replay parser");

// ---- the CSS block: its own header, once, theme tokens only, a rule per class the overlay emits
const HEAD = "/* ---------- Boss overlay (facing, casts, spell areas) ---------- */";
const at = css.indexOf(HEAD);
assert.ok(at >= 0, "the boss overlay block starts with its header comment");
const next = css.indexOf("/* ---------- ", at + HEAD.length);
const block = css.slice(at, next < 0 ? css.length : next);
assert.ok(!css.slice(at + HEAD.length).includes(HEAD), "the block is appended once");
assert.strictEqual(block.match(/#[0-9a-fA-F]{3,8}\b/g), null, "no raw colour: theme tokens only");
for (const sel of ["\\.facing", "\\.mobshape", "\\.mobshape\\.s-line", "\\.castring", "\\.mobcast", "\\.cmd-legend i\\.facing", "\\.cmd-legend i\\.larea"]) {
  assert.ok(new RegExp(sel + "[\\s,{.:]").test(block), sel + " has a rule");
}
assert.ok(/\.facing\s*\{[^}]*fill:\s*var\(--accent-red\)/.test(block), "the facing arrow is the theme red");
assert.ok(/\.mobshape\s*\{[^}]*color-mix\(in srgb, var\(--accent-red\) 22%, transparent\)/.test(block), "the spell area is a faint red");
assert.ok(/\.mobshape\s*\{[^}]*stroke:\s*var\(--accent-red\)/.test(block));
assert.ok(/\.mobshape\.s-line\s*\{[^}]*stroke-dasharray:\s*4 3/.test(block), "a line spell is dashed");
assert.ok(/\.castring\s*\{[^}]*stroke:\s*var\(--accent-yellow\)/.test(block), "the cast ring is the theme yellow");
console.log("boss overlay ok");
