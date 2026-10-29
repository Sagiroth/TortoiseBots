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
assert.deepStrictEqual(rp.mobs[0].shape, { k: "cone", a: 120, l: 20 });
// between two frames the facing takes the shorter arc
rp = ctx.fdReplayBind(live, fight, track, 1001);
assert.ok(rp.mobs[0].o > 0.4 && rp.mobs[0].o < 0.6, "halfway: " + rp.mobs[0].o);
assert.strictEqual(rp.mobs[0].cleft, 500, "the frame after the cursor is not used for the cast");
// a replay row with no overlay fields stays plain
const plain = { raw: null, prep: ctx.FightReplay.prep({ commander: "c", frames: [{ t: 1000, map: 249, bots: [[7, 0, 0, 100, 1, 249]], mobs: [{ id: "6", e: 1, x: 1, y: 1, hp: 50 }] }] }) };
rp = ctx.fdReplayBind(live, { start: 1000, dur_ms: 1000, bots: [] }, plain, 1000.5);
assert.strictEqual(rp.mobs[0].o, undefined); assert.strictEqual(rp.mobs[0].cid, undefined); assert.strictEqual(rp.mobs[0].cleft, undefined);

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
