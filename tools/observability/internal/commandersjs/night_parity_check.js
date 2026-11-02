"use strict";
// Run by commandersjs_test.go under node: FightHistory.nightOf (web/static/js/fighthistory.js) and the Go nightOf in
// internal/history/history.go must answer the same night around the midnight-DST days in Santiago and New York.
// process.argv[2] is fighthistory.js; process.argv[3] is the unix-epoch list; arg TZ picks the zone under test.
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");

const histPath = path.resolve(process.argv[2]);
const epochsPath = path.resolve(process.argv[3]);
const zone = process.env.TZ || "";
assert.ok(zone, "run node with TZ set (America/Santiago or America/New_York)");

const ctx = { console };
vm.createContext(ctx);
vm.runInContext(fs.readFileSync(histPath, "utf8"), ctx, { filename: "fighthistory.js" });
assert.ok(ctx.FightHistory && typeof ctx.FightHistory.nightOf === "function", "fighthistory.js loads");

const FH = ctx.FightHistory;
const cases = JSON.parse(fs.readFileSync(epochsPath, "utf8")).cases;
assert.ok(cases.length >= 10, "at least ten parity epochs");
for (const c of cases) {
  const got = FH.nightOf(c.epoch * 1000);
  assert.strictEqual(got, c.want, zone + " epoch " + c.epoch + " (" + c.name + "): nightOf = " + got + ", want " + c.want);
}

// The brief's own cases in this zone: 2024-09-09 00:15 is the night of 2024-09-08 in Santiago.
if (zone === "America/Santiago") {
  assert.strictEqual(FH.nightOf(1725851700 * 1000), "2024-09-08", "Santiago 2024-09-09 00:15");
}
if (zone === "America/Havana") {
  assert.strictEqual(FH.nightOf(1710130500 * 1000), "2024-03-10", "Havana 2024-03-11 00:15");
}

console.log("night parity ok (" + zone + ", " + cases.length + " epochs)");
