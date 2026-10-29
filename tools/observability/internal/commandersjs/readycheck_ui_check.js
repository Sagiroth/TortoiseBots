"use strict";
// Run by commandersjs_test.go under node: the ready check's markup in app.js. fdFramesHtml puts one level dot on every
// tile, the frames header shows "Ready <ok>/<n>" with the check and failure counts, the list of the not-ready bots opens
// on the toggle only, and every name that comes from the game server is escaped.
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

const ctx = { console };
vm.createContext(ctx);
vm.runInContext(fs.readFileSync(path.join(dir, "commanders.js"), "utf8"), ctx, { filename: "commanders.js" });
vm.runInContext(fs.readFileSync(path.join(dir, "readycheck.js"), "utf8"), ctx, { filename: "readycheck.js" });
assert.ok(ctx.ReadyCheck, "readycheck.js loads as the global ReadyCheck");
vm.runInContext(fs.readFileSync(path.join(dir, "fightdebug.js"), "utf8"), ctx, { filename: "fightdebug.js" });
assert.ok(ctx.FightDebug && typeof ctx.FightDebug.raidFrames === "function", "fightdebug.js loads as the global FightDebug");
vm.runInContext([grab("esc"), grab("fdFramesHtml")].join("\n"), ctx, { filename: "app.js" });

const run = (expr, vars) => { if (vars) Object.assign(ctx, vars); return vm.runInContext(expr, ctx); };
const bot = (name, cls, role, rdy, extra) => Object.assign({
  name, guid: name.length, class: cls, role, hp: 10, max_hp: 10, power: 0, max_power: 100, power_type: "rage",
  alive: true, x: 0, y: 0, z: 0,
}, rdy ? { rdy } : {}, extra || {});
const bind = { bots: [
  bot("Bonevale", "warrior", "tank", { b: 63, c: 7, cr: 3, cn: 3 }),
  bot("Frostz", "mage", "dps", null, { power: 10, max_power: 100, power_type: "mana" }),
  bot("Venomwind", "rogue", "dps", null, { alive: false, hp: 0, power_type: "energy" }),
  bot("Quelin", "priest", "healer", null, { power: 80, max_power: 100, power_type: "mana" }),
  bot("Tinybot", "hunter", "dps", { b: 3, c: 5, cr: 2, cn: 3, cl: 142 }, { power: 100, max_power: 100, power_type: "mana" }),
] };
const frames = run("FightDebug.raidFrames(boards)", { boards: bind });
const ready = run("ReadyCheck.raid(boards, null)", { boards: bind });
assert.deepStrictEqual(JSON.parse(JSON.stringify(ready.counts)), { ok: 1, warn: 2, fail: 2 }, JSON.stringify(ready.counts));

// The frames as rendered: one dot per tile, the header with its chip and counts, no list while closed.
const closed = run("fdFramesHtml(frames, null, ready)", { frames, ready });
assert.strictEqual((closed.match(/class="fd-rdy l-/g) || []).length, 5, "one dot per tile");
assert.ok(closed.includes('class="fd-rdy l-ok" aria-label="Ready: READY"'), closed.slice(0, 300));
assert.ok(closed.includes('class="fd-rdy l-warn" aria-label="Ready: CHECK, mana 80%"'), "the tile names its reasons");
assert.ok(closed.includes('class="fd-rdy l-fail" aria-label="Ready: NOT READY, dead"'), "a dead bot is not ready");
assert.ok(closed.includes('<button type="button" class="fd-chip l-fail" data-fd-ready aria-expanded="false">Ready 1/5</button>'),
  "the chip counts the ready bots: " + closed.slice(0, 400));
assert.ok(closed.includes('class="fd-readysub">2 check · 2 not ready</span>'), closed.slice(0, 400));
assert.ok(closed.indexOf('class="fd-readyhead"') < closed.indexOf('class="fd-grp"'), "the header sits before the groups");
assert.ok(!closed.includes("fd-readylist"), "the list is closed by default");
assert.ok(closed.includes('class="fd-grp"'), "the groups still render");

// Open: one compact row per not-ready bot, the ready one left out, the cooldown reason in words.
ready.open = true;
const open = run("fdFramesHtml(frames, null, ready)", { frames, ready });
assert.ok(open.includes('data-fd-ready aria-expanded="true"'), "the toggle is expanded");
assert.ok(open.includes('<ol class="fd-readylist">'), "the list is open");
const listAt = open.indexOf('<ol class="fd-readylist">'), list = open.slice(listAt, open.indexOf("</ol>", listAt));
assert.strictEqual((list.match(/<li class="l-/g) || []).length, 4, "one row per non-ok bot: " + list.slice(0, 400));
assert.ok(!list.includes("Bonevale"), "the ready bot is not listed");
assert.ok(list.includes("2 of 3 key cooldowns ready (longest 142s)"), list.slice(0, 400));
assert.ok(list.includes('<li class="l-fail"><span class="fd-readynm">Venomwind</span> dead</li>'), list.slice(0, 400));

// An older module sends no rdy at all: the base rules still run and the header says the buffs are unknown.
const plain = { bots: bind.bots.map((b) => { const c = Object.assign({}, b); delete c.rdy; return c; }) };
const plainReady = run("ReadyCheck.raid(boards, null)", { boards: plain });
assert.strictEqual(plainReady.known, false);
const plainHtml = run("fdFramesHtml(frames, null, ready)", { frames: run("FightDebug.raidFrames(boards)", { boards: plain }), ready: plainReady });
assert.ok(plainHtml.includes("buffs unknown (older module)"), plainHtml.slice(0, 400));
assert.strictEqual((plainHtml.match(/class="fd-rdy l-/g) || []).length, 5, "the base rules still mark every tile");

// Nothing from the game server may become markup in the header or the list.
const EVIL = '<img src=x onerror=alert(1)>"&';
const evilBind = { bots: [bot(EVIL, EVIL, "mage", { b: 0, c: 0, cr: 0, cn: 1, cl: 9 }, { power: 0, max_power: 100, power_type: "mana" })] };
const evilReady = run("ReadyCheck.raid(boards, null)", { boards: evilBind });
evilReady.open = true;
const evil = run("fdFramesHtml(frames, null, ready)", { frames: run("FightDebug.raidFrames(boards)", { boards: evilBind }), ready: evilReady });
assert.ok(!/<img/i.test(evil), "raw markup survived: " + evil.slice(0, 300));
assert.ok(evil.includes("&lt;img"), "the name is escaped as text");
for (const m of evil.matchAll(/="([^"]*)"/g)) assert.ok(!/[<>]/.test(m[1]), "an attribute value broke out: " + m[1]);

console.log("ready check ui ok");
