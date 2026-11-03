"use strict";
// Run by commandersjs_test.go: the non-tank lane rule on one regenerated Onyxia attempt fixture. Arguments: the aggrolanes
// script, the fixture (testdata/mk_lanes_fixture.py output) and the expected ntMs / ntLongestMs of the boss lane. The numbers
// are the lead's independent reference on this data; the test pins them, it never tunes them.
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const A = require(path.resolve(process.argv[2]));
const d = JSON.parse(fs.readFileSync(process.argv[3], "utf8"));
const res = A.lanes(d.track, d.fight, d.fight.bots);
const lane = res.lanes.find((l) => l.label === "Onyxia");
assert.ok(lane, "the Onyxia lane is in the strip");
assert.strictEqual(lane.ntMs, Number(process.argv[4]), "ntMs of " + process.argv[3]);
assert.strictEqual(lane.ntLongestMs, Number(process.argv[5]), "ntLongestMs of " + process.argv[3]);
assert.strictEqual(res.ntWorst, lane, "the boss lane is the worst of " + process.argv[3]);
console.log("aggrolanes attempts: ok");
