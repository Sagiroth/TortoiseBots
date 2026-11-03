const assert = require("assert");
const path = require("path");
const CmdView = require(path.resolve(process.argv[2]));
require(path.resolve(process.argv[3])); // dungeons.js sets global DgnView
const RaidView = require(path.resolve(process.argv[4]));
assert.ok(RaidView && typeof RaidView.bindOf === "function", "raids.js must export RaidView");

const view = {
  key: "409:7", map: 409, instance: 7, first_seen: 100, seen_sec: 60, bots: 3, players: 0, dead: 1,
  mobs: [], mobs_map: 409, mobs_total: 0,
  groups: [{
    group: 7, key: "raid#7", rg: true, leader: "Tank", leader_guid: 1,
    bound: false, observed: true, run: null,
    members: [
      { name: "Tank", guid: 1, class: "warrior", role: "tank", level: 60, hp: 4000, max_hp: 5000, subgroup: 1, state: "combat", dead: false, map: 409, zone: 2437, x: 1, y: 2, z: 0, o: 0 },
      { name: "Heal", guid: 2, class: "priest", role: "healer", level: 60, hp: 0, max_hp: 3000, subgroup: 1, state: "dead", dead: true, map: 409, zone: 2437, x: 3, y: 4, z: 0, o: 0 },
      { name: "Off", guid: 3, class: "mage", role: "dps", level: 60, hp: 2500, max_hp: 2500, subgroup: 2, state: "idle", map: 409, zone: 2437, x: 5, y: 6, z: 0, o: 0, offline: true },
    ],
    combat: { win_s: 10, fight_ms: 20000, bots: [{ g: 1, dps: 500, hps: 10, c: 3, ls: "Heroic Strike" }] },
    trimmed: "",
  }],
  ungrouped: [],
};

// ---- bindOf: the shape the card and map draw ----
const bind = RaidView.bindOf(view, view.groups[0]);
assert.strictEqual(bind.commander, "raid#7", "commander = the RAIDS key: " + bind.commander);
assert.strictEqual(bind.cardKey, "raid:409:7:raid#7", "cardKey = the card the map draws: " + bind.cardKey);
assert.strictEqual(bind.observed, true);
assert.strictEqual(bind.raidKey, "409:7");
assert.strictEqual(bind.group, 7);
assert.strictEqual(bind.online, true);
assert.deepStrictEqual(bind.mobs, []);
assert.strictEqual(bind.mobs_map, 409);
assert.strictEqual(bind.run, null);
assert.strictEqual(bind.bots.length, 3);
assert.strictEqual(bind.bots[0].sub, 1, "sub rides along for raidBlocks");
assert.strictEqual(bind.bots[0].alive, true);
assert.strictEqual(bind.bots[1].alive, false, "dead member reads not alive");
assert.strictEqual(bind.bots[2].alive, false, "an OFFLINE member must render as not alive");
assert.strictEqual(bind.combat.win_s, 10, "combat rides raw");

// Distance is computed by partyRows from the lead, as on the Commanders card.
const rows = CmdView.partyRows(bind);
assert.strictEqual(rows.find((r) => r.name === "Tank").lead, true);

// ---- stateWord: fighting with combat or a combat state, wiped when all dead, else idle ----
assert.strictEqual(RaidView.stateWord({ combat: { win_s: 10 }, members: [] }), "fighting");
assert.strictEqual(RaidView.stateWord({ members: [{ state: "combat" }] }), "fighting");
assert.strictEqual(RaidView.stateWord({ members: [{ state: "dead", dead: true }] }), "wiped");
assert.strictEqual(RaidView.stateWord({ members: [{ state: "idle" }] }), "idle");

// ---- windowTotals: raid dps/hps summed over the window bots ----
assert.deepStrictEqual(RaidView.windowTotals({ win_s: 10, bots: [{ g: 1, dps: 500, hps: 10 }, { g: 2, dps: 100, hps: 40 }] }), { dps: 600, hps: 50 });
assert.deepStrictEqual(RaidView.windowTotals(null), { dps: 0, hps: 0 });

// ---- listRows order: fighting first, then by name ----
const fighting = Object.assign({}, view.groups[0], { key: "raid#7" });
const idle = Object.assign({}, view.groups[0], { key: "raid#9", group: 9, combat: null, members: [{ state: "idle" }] });
const ordered = RaidView.listRows({ groups: [idle, fighting] });
assert.deepStrictEqual(ordered.map((g) => g.key), ["raid#7", "raid#9"], "fighting first: " + JSON.stringify(ordered.map((g) => g.key)));
const mk = (key, combat) => ({ key, combat: combat ? { win_s: 1 } : null, members: [{ state: combat ? "combat" : "idle" }] });
const mixed = RaidView.listRows({ groups: [mk("raid#9", false), mk("raid#8", true), mk("raid#10", false), mk("raid#7", true)] });
assert.deepStrictEqual(mixed.map((g) => g.key), ["raid#7", "raid#8", "raid#10", "raid#9"],
  "fighting first, then key: " + JSON.stringify(mixed.map((g) => g.key)));

console.log("raids_check.js ok");
