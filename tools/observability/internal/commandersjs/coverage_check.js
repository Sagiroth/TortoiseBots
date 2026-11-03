"use strict";
// Run by commandersjs_test.go under node: web/static/js/coverage.js against the shared fixture
// (data/boss/testdata/coverage_cases.json). Every case is the contract with tools/boss_coverage.py: a rule change on one
// side fails this test. Also pins the coverage route round-trips and that no boss fact is written in the code.
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const C = require(path.resolve(process.argv[2]));
const fixturePath = path.resolve(process.argv[3]);
const fixture = JSON.parse(fs.readFileSync(fixturePath, "utf8"));

// ---- every shared case: compute + project equal the expected projection ------------------------------
assert.ok(Array.isArray(fixture.cases) && fixture.cases.length > 0, "the fixture has cases");
fixture.cases.forEach((one) => {
  const got = C.project(C.compute(one.table, one.rows));
  assert.deepStrictEqual(got, one.expect, one.name);
});

// ---- routes: parseHash / hash round-trip -------------------------------------------------------------
assert.deepStrictEqual(C.parseHash("#/coverage/aq40?missing=1&boss=15263"), { raid: "aq40", missing: true, boss: 15263 });
assert.strictEqual(C.hash({ raid: "aq40", missing: true, boss: 15263 }), "#/coverage/aq40?missing=1&boss=15263");
assert.deepStrictEqual(C.parseHash("#/coverage"), { raid: null, missing: false, boss: null });
assert.strictEqual(C.hash(C.parseHash("#/coverage")), "#/coverage");
assert.deepStrictEqual(C.parseHash("#/coverage/unknown-raid"), { raid: "unknown-raid", missing: false, boss: null });
assert.strictEqual(C.hash(C.parseHash("#/coverage/unknown-raid")), "#/coverage/unknown-raid", "an unknown key survives the round trip");
assert.deepStrictEqual(C.parseHash("#/coverage?boss=x"), { raid: null, missing: false, boss: null }, "a bad boss is dropped");
assert.deepStrictEqual(C.parseHash("#/overview"), null, "another tab is no coverage route");

// ---- no boss fact in the code (the same guard as the Python side) -------------------------------------
const src = fs.readFileSync(path.resolve(process.argv[2]), "utf8");
const allowed = new Set(["1146", "1000", "5000", "358", "390", "500"]);
const found = (src.match(/\b\d{4,5}\b/g) || []).filter((id) => !allowed.has(id));
assert.deepStrictEqual(found, [], "boss entry or spell id written in coverage.js");

console.log("coverage ok");
