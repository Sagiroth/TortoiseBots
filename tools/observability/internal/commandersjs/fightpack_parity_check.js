"use strict";
// Run by commandersjs_test.go under node: PlanOverlay.fightPack (the plan's pack a fight was about) and the Go port in
// internal/plans/fightpack.go must answer the same on the same cases. The fixture lives with the Go test's testdata so both
// sides read one file; the plans are the plan goldens.
const assert = require("assert");
const fs = require("fs");
const path = require("path");

const P = require(path.resolve(process.argv[2]));
const casesPath = path.resolve(process.argv[3]);
const cases = JSON.parse(fs.readFileSync(casesPath, "utf8")).cases;
assert.ok(cases.length >= 10, "at least ten parity cases");

const plansDir = path.join(__dirname, "..", "plans", "testdata");
const cache = new Map();
const planOf = (name) => {
  if (!cache.has(name)) cache.set(name, JSON.parse(fs.readFileSync(path.join(plansDir, name + ".plan.json"), "utf8")));
  return cache.get(name);
};

for (const c of cases) {
  const plan = planOf(c.plan);
  const fight = { step: c.step, boss: c.boss, engaged: (c.engaged || []).map((e) => ({ e })) };
  const got = P.fightPack(plan, fight);
  assert.strictEqual(got, c.want, c.name + ": fightPack(step " + c.step + ", boss " + c.boss + ", engaged " + JSON.stringify(c.engaged) + ")");
}

// No plan, no fight: 0, the answer the history writer stores.
assert.strictEqual(P.fightPack(null, { step: 3, boss: 10184 }), 0);
assert.strictEqual(P.fightPack(planOf("onyxias_lair"), null), 0);
