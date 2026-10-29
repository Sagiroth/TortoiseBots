"use strict";
// Run by commandersjs_test.go under node: the ready check's pure logic (web/static/js/readycheck.js), fed with the
// attempt-2 bind (a 40-bot Onyxia raid at a stand of the plan golden) and with a party. A dead bot is dead and nothing
// else, a mana user under half is a failure, a stand 45 yd away is a failure, the raid expects flask or elixir and food,
// a party does not, and only the alive bound bots make a buff expected.
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");
const readyPath = path.resolve(process.argv[2]);
const cmdPath = path.resolve(process.argv[3]);

const ctx = { console };
vm.createContext(ctx);
vm.runInContext(fs.readFileSync(cmdPath, "utf8"), ctx, { filename: "commanders.js" });
assert.ok(ctx.CmdView && typeof ctx.CmdView.isRaid === "function", "commanders.js loads as CmdView");
vm.runInContext(fs.readFileSync(readyPath, "utf8"), ctx, { filename: "readycheck.js" });
const R = ctx.ReadyCheck;
assert.ok(R && typeof R.raid === "function" && typeof R.bot === "function" && typeof R.standOf === "function",
  "readycheck.js loads as ReadyCheck");

const fixture = JSON.parse(fs.readFileSync(path.join(__dirname, "testdata", "ready-bind.json"), "utf8"));
const plan = JSON.parse(fs.readFileSync(path.join(__dirname, "testdata", "onyxia-plan.json"), "utf8"));
const molten = JSON.parse(fs.readFileSync(path.join(__dirname, "testdata", "molten-core-plan.json"), "utf8"));
const stand = plan.steps.find((s) => s.i === 20);

// The bind at the plan's step-20 stand: every bot within 8 yd (the raid holds the stand), one 45 yd out, full health and
// mana so only the cases under test move a level. The rdy facts are the four cases the wire carries plus one bot without.
const bots = fixture.bots.map((b, i) => {
  const c = Object.assign({}, b);
  if (c.alive !== false) { c.hp = c.max_hp; c.power = c.max_power; }
  const a = (i * 137) * Math.PI / 180, r = 3 + (i % 5);
  c.x = stand.x + Math.cos(a) * r;
  c.y = stand.y + Math.sin(a) * r;
  c.z = stand.z;
  return c;
});
const byName = {};
bots.forEach((b) => { byName[b.name] = b; });
const far = byName.Brrtastic;
far.x = stand.x + 45; far.y = stand.y; far.z = stand.z;
byName.Basicie.power = Math.round(byName.Basicie.max_power * 0.4); // 40%
byName.Lorenthil.power = byName.Lorenthil.max_power / 2;            // exactly 50%
byName.Melis.power = Math.round(byName.Melis.max_power * 0.6);      // 60%
byName.Bonevale.rdy = { b: 63, c: 5, cr: 3, cn: 3 };
byName.Tinybot.rdy = { b: 62, c: 5, cr: 3, cn: 3 };                  // every buff but stamina
byName.Basicie.rdy = { b: 63, c: 5, cr: 3, cn: 3 };
byName.Brrtastic.rdy = { b: 63, c: 5, cr: 3, cn: 3 };
byName.Beardy.rdy = { b: 63, c: 1, cr: 3, cn: 3 };                   // a flask, no food

const bind = { commander: fixture.commander, bots, run: Object.assign({}, fixture.run, { step: 20, state: "running" }) };
const r = R.raid(bind, plan);
const row = (n) => r.bots.find((b) => b.name === n);
const plain = (v) => JSON.parse(JSON.stringify(v));

assert.deepStrictEqual(plain(row("Bonevale")), { name: "Bonevale", lvl: "ok", reasons: [] });
assert.deepStrictEqual(plain(row("Tinybot")), { name: "Tinybot", lvl: "warn", reasons: ["no stamina"] }, "a priest is bound");
assert.deepStrictEqual(plain(row("Basicie")), { name: "Basicie", lvl: "fail", reasons: ["mana 40%"] });
assert.deepStrictEqual(plain(row("Brrtastic")), { name: "Brrtastic", lvl: "fail", reasons: ["45 yd from the stand"] });
assert.deepStrictEqual(plain(row("Beardy")), { name: "Beardy", lvl: "warn", reasons: ["not well fed"] });
assert.deepStrictEqual(plain(row("Lorenthil")), { name: "Lorenthil", lvl: "warn", reasons: ["mana 50%"] }, "exactly half is a warn");
assert.deepStrictEqual(plain(row("Melis")), { name: "Melis", lvl: "warn", reasons: ["mana 60%"] }, "no rdy: the base rules only");
assert.deepStrictEqual(plain(row("Erommaron")), { name: "Erommaron", lvl: "fail", reasons: ["dead"] });
assert.deepStrictEqual(plain(r.counts), { ok: 20, warn: 4, fail: 15 }, JSON.stringify(r.counts));
assert.strictEqual(r.lvl, "fail");
assert.deepStrictEqual(plain(r.stand), { i: 20, x: -113.88, y: -127.47, z: -48.98 });
assert.strictEqual(r.known, true, "five bots carry rdy");
assert.strictEqual(r.gathered, true, "the run waits on the plan's stand step: the raid is gathered");

// A raid walking the plan (its step is not a stand) is not gathered: the stand distance of every bot is no readiness verdict,
// and the header says "Ready check: not run yet" instead of a red 0/N (the site audit's mid-run chip).
const walking = R.raid(Object.assign({}, bind, { run: Object.assign({}, bind.run, { step: 21 }) }), plan);
assert.strictEqual(walking.gathered, false);
assert.strictEqual(walking.stand, null, "no stand rule while the raid is on the move");
assert.strictEqual(walking.bots.find((b) => b.name === "Brrtastic").reasons.indexOf("45 yd from the stand"), -1);
const parked = R.raid(Object.assign({}, bind, { run: Object.assign({}, bind.run, { step: 21, state: "paused" }) }), plan);
assert.strictEqual(parked.gathered, true, "a parked run may check at the next stand");
assert.strictEqual(R.gatheredAt({ bots: [] }, plan), true, "a party without a run has nothing to gather at");
assert.strictEqual(R.gatheredAt(bind, plan), true);
assert.strictEqual(R.gatheredAt({ run: { step: 21, state: "running" } }, plan), false);
assert.strictEqual(R.gatheredAt({ run: { step: 21, state: "running" } }, null), true,
  "without the plan the gather cannot be known: the bot rules speak");
assert.strictEqual(R.gatheredAt({ run: { step: 20, state: "running" } }, plan), true);

// A bot at exactly half mana is a warn, not a failure (the boundary is pinned here for the mutation).
assert.deepStrictEqual(plain(R.bot({ name: "Half", alive: true, power_type: "mana", power: 50, max_power: 100 }, {})),
  { name: "Half", lvl: "warn", reasons: ["mana 50%"] });
assert.deepStrictEqual(plain(R.bot({ name: "Dead", alive: false, power_type: "mana", power: 0, max_power: 100 }, {})),
  { name: "Dead", lvl: "fail", reasons: ["dead"] });

// The stand: the first stand at or after the run's step, on the plan golden and on a multi-leg raid plan.
assert.deepStrictEqual(plain(R.standOf(plan, { step: 20 })), { i: 20, x: -113.88, y: -127.47, z: -48.98 });
assert.strictEqual(R.standOf(plan, { step: 21 }).i, 22);
assert.strictEqual(R.standOf(plan, { step: 29 }), null);
assert.strictEqual(R.standOf(plan, null), null);
assert.strictEqual(R.standOf(null, { step: 1 }), null);
assert.strictEqual(R.standOf(plan, {}), null);
assert.strictEqual(R.standOf(molten, { step: 2 }).i, 3);
assert.strictEqual(R.standOf(molten, { step: 12 }).i, 13);

// A party: five bots, no raid, and its only priest is dead, so no flask, food or stamina reason may appear.
const partyBots = ["Bonevale", "Quelin", "Basicie", "Beardy", "Tinybot"].map((n) => {
  const c = Object.assign({}, byName[n]);
  delete c.sub;
  delete c.rdy;
  if (n === "Quelin") c.alive = false;
  if (c.alive !== false) { c.hp = c.max_hp; c.power = c.max_power; }
  return c;
});
partyBots.forEach((b) => { if (b.alive !== false) b.rdy = { b: 2, c: 0, cr: 3, cn: 3 }; });
const pr = R.raid({ bots: partyBots }, null);
assert.strictEqual(pr.stand, null);
const prow = (n) => pr.bots.find((b) => b.name === n);
assert.deepStrictEqual(plain(prow("Bonevale")), { name: "Bonevale", lvl: "ok", reasons: [] });
assert.deepStrictEqual(plain(prow("Basicie")), { name: "Basicie", lvl: "ok", reasons: [] });
assert.deepStrictEqual(plain(prow("Quelin")), { name: "Quelin", lvl: "fail", reasons: ["dead"] });
assert.ok(!pr.bots.some((b) => b.reasons.some((x) => /flask|food|stamina/.test(x))),
  "a party expects no raid consumables, and a dead priest expects no stamina: " + JSON.stringify(pr.bots));
assert.strictEqual(pr.known, true);
assert.deepStrictEqual(plain(pr.counts), { ok: 4, warn: 0, fail: 1 });

console.log("ready check ok");
