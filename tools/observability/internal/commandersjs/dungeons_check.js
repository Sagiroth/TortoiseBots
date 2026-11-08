"use strict";
// Run by commandersjs_test.go under node: the Dungeons tab's pure logic (run states, copy labels, party rows and raid subgroups).
const assert = require("assert");
const path = require("path");
const CmdView = require(path.resolve(process.argv[2]));
const D = require(path.resolve(process.argv[3]));
const M = require(path.resolve(process.argv[4]));

// ---- copy labels: two copies of one dungeon read apart, a lone group has no copy number
const runs = [
  { key: "409:14", map: 409, instance: 14, members: [] },
  { key: "409:13", map: 409, instance: 13, members: [] },
  { key: "70:5", map: 70, instance: 5, members: [] },
];
const labels = D.copyLabels(runs);
assert.deepStrictEqual(labels, { "409:13": 1, "409:14": 2 }); // ordered by instance id, whatever the report order
assert.deepStrictEqual(D.copyLabels([runs[2]]), {});         // a lone group: no copy number
assert.deepStrictEqual(D.copyLabels(null), {});

// ---- the short state of a run, worst first
const m = (over) => Object.assign({ name: "Bot", state: "idle", dead: false }, over);
assert.strictEqual(D.runState({ members: [m({ state: "moving" }), m({ state: "idle" })] }), "moving");
assert.strictEqual(D.runState({ members: [m({ state: "moving" }), m({ state: "combat" })] }), "fighting");
assert.strictEqual(D.runState({ members: [m({ state: "combat" }), m({ dead: true, state: "dead" })] }), "someone dead");
assert.strictEqual(D.runState({ members: [m({ state: "busy" }), m({ state: "resting" })] }), "busy");
assert.strictEqual(D.runState({ members: [m({ state: "resting" })] }), "resting");
assert.strictEqual(D.runState({ members: [m({ state: "idle" })] }), "idle");
assert.strictEqual(D.runState({ members: [] }), "");
assert.strictEqual(D.runState(null), "");

// ---- the party rows: the player first, then subgroup, then name; bars and words filled in
const run = { key: "409:13", map: 409, instance: 13, members: [
  { name: "Zug", guid: 3, player: false, class: "warrior", role: "tank", level: 60, hp: 2500, max_hp: 5000, power: 0, max_power: 0, subgroup: 2, state: "combat" },
  { name: "Thralia", guid: 1, player: true, class: "shaman", role: "", level: 60, hp: 3000, max_hp: 4000, power: 2000, max_power: 4000, power_type: "mana", subgroup: 1, state: "combat" },
  { name: "Aria", guid: 2, player: false, class: "mage", role: "dps", level: 60, hp: 0, max_hp: 3500, power: 1000, max_power: 5000, power_type: "mana", subgroup: 1, state: "dead" },
] };
const rows = D.memberRows(run);
assert.deepStrictEqual(rows.map((r) => r.name), ["Thralia", "Aria", "Zug"]); // player first, then subgroup, then name
const thralia = rows[0];
assert.strictEqual(thralia.player, true);
assert.strictEqual(thralia.roleLabel, "Player");
assert.strictEqual(thralia.role, "other");       // a player carries no AI role
assert.strictEqual(thralia.hp, 75);              // pct, not raw
assert.strictEqual(thralia.powerKind, "mana");
assert.strictEqual(thralia.power, 50);           // pct of the max, not raw
assert.strictEqual(thralia.powerText, "2000 / 4000");
const aria = rows[1];
assert.strictEqual(aria.role, "dead");           // a 0-HP corpse greys out
assert.strictEqual(aria.hpText, "0 / 3500");
assert.strictEqual(aria.hasPower, false);        // a dead member draws no power bar, even with power left
const zug = rows[2];
assert.strictEqual(zug.role, "tank");
assert.strictEqual(zug.hasPower, false);         // a warrior has no power bar (max_power 0)

// ---- the power bar helper (issue #1112): the Bots tab's types and colours, and when there is no bar
const pw = (over) => D.powerOf(Object.assign({ name: "B", guid: 1, hp: 1, max_hp: 1, power: 40, max_power: 100, power_type: "mana", state: "combat" }, over));
assert.deepStrictEqual(pw({}), { power: 40, kind: "mana", text: "40 / 100" });
assert.strictEqual(pw({ power_type: "rage", power: 0 }).kind, "rage");   // an empty rage bar still draws
assert.strictEqual(pw({ power_type: "rage", power: 0 }).power, 0);
assert.strictEqual(pw({ power_type: "energy" }).kind, "energy");
assert.strictEqual(pw({ power_type: "focus" }).kind, "mana");            // only rage and energy are coloured apart
assert.strictEqual(pw({ power_type: "happiness" }).kind, "mana");
assert.strictEqual(pw({ max_power: 0 }), null);                          // no resource: no bar
assert.strictEqual(pw({ power: 0, max_power: 0 }), null);                // 0 max, even at 0 current
assert.strictEqual(pw({ dead: true }), null);                            // dead: no bar
assert.strictEqual(pw({ state: "dead" }), null);                         // the daemon's dead state too
assert.strictEqual(pw({ max_power: undefined }), null);                  // a pre-power module sends no fields
assert.strictEqual(D.powerOf(undefined), null);

// ---- a party reads as one unlabelled group, a raid as compact subgroup blocks
assert.deepStrictEqual(D.memberGroups(D.memberRows(run)).length, 1);
assert.strictEqual(D.memberGroups(D.memberRows(run))[0].label, "");
const raid = { key: "409:1", map: 409, instance: 1, members: [] };
for (let s = 1; s <= 8; s++)
  for (let i = 1; i <= 5; i++)
    raid.members.push({ name: "Bot" + s + "-" + i, guid: s * 100 + i, class: "warrior", role: "dps", level: 60, hp: 1, max_hp: 1, power: 0, max_power: 0, subgroup: s, state: "moving" });
const groups = D.memberGroups(D.memberRows(raid));
assert.strictEqual(groups.length, 8); // one block per subgroup, not 40 rows
assert.deepStrictEqual(groups.map((g) => g.label), [1, 2, 3, 4, 5, 6, 7, 8].map((s) => "Group " + s));
assert.strictEqual(groups[0].rows.length, 5);
// Without subgroups in the snapshot (a pre-v12 module): chunks of 5 in the order reported.
const noSub = { key: "409:1", map: 409, instance: 1, members: raid.members.map((r) => Object.assign({}, r, { subgroup: 0 })) };
const chunks = D.memberGroups(D.memberRows(noSub));
assert.strictEqual(chunks.length, 8);
assert.strictEqual(chunks[0].label, "Group 1");
assert.deepStrictEqual(chunks[0].rows.map((r) => r.name), ["Bot1-1", "Bot1-2", "Bot1-3", "Bot1-4", "Bot1-5"]);

// ---- seen-for wording rides on the page's age text
assert.strictEqual(D.seenText(45), CmdView.ageText(45));

// ---- a run as a pseudo-binding for the card and map: ids, targets, mobs, and a living-tank lead
const liveRun = {
  key: "409:13", map: 409, instance: 13, mobs_map: 409, mobs_total: 30,
  mobs: [{ id: "900", entry: 1, name: "Rockchewer", hp: 85, x: 1230, y: 850, victim: "10844", reaction: "h" }],
  members: [
    { name: "Healer", guid: 2, player: false, role: "healer", class: "priest", level: 60, hp: 1, max_hp: 2, dead: false, state: "combat", map: 409, x: 1, y: 1, id: "200" },
    { name: "Tank", guid: 1, player: false, role: "tank", class: "warrior", level: 60, hp: 1, max_hp: 2, dead: false, state: "combat", map: 409, x: 2, y: 2, id: "10844", target: "900" },
    { name: "DeadTank", guid: 3, player: false, role: "tank", class: "warrior", level: 60, hp: 0, max_hp: 2, dead: true, state: "dead", map: 409, x: 3, y: 3, id: "300" },
  ],
};
const bind = D.bindOf(liveRun);
assert.strictEqual(bind.commander, "dgn:409:13");
assert.strictEqual(bind.dungeon, true);
assert.strictEqual(bind.lead, "Tank"); // the first living tank, not the dead one
// A dead tank listed first is skipped for the next living tank.
const deadFirstLead = D.bindOf({ key: "70:2", map: 70, instance: 2, members: [
  { name: "DeadTank", guid: 11, role: "tank", dead: true, state: "dead", map: 70 },
  { name: "OffTank", guid: 12, role: "tank", dead: false, state: "combat", map: 70 },
  { name: "Dps", guid: 13, role: "dps", dead: false, state: "combat", map: 70 },
]});
assert.strictEqual(deadFirstLead.lead, "OffTank");
const tank = bind.bots.find((b) => b.guid === 1);
assert.strictEqual(tank.id, "10844");
assert.strictEqual(tank.target, "900");
assert.strictEqual(tank.alive, true);
assert.strictEqual(bind.bots.find((b) => b.guid === 3).alive, false);
assert.deepStrictEqual(bind.mobs, liveRun.mobs); // the array the layers read, untouched
assert.strictEqual(bind.mobs_map, 409);
assert.strictEqual(bind.mobs_total, 30);

// An older module reports no ids, targets or mobs: no keys invented, and no lead.
const oldBind = D.bindOf({ key: "70:1", map: 70, instance: 1, members: [{ name: "B", guid: 5, role: "dps", map: 70 }] });
assert.ok(!("id" in oldBind.bots[0]) && !("target" in oldBind.bots[0]));
assert.strictEqual(oldBind.lead, "");
assert.strictEqual(oldBind.mobs, undefined);

// The layer availability app.js computes (cmdMapFacts) from the same fields: the mob and combat layers are
// available on a run that reported a list, greyed on one that did not.
function facts(b) {
  const first = b.bots[0];
  const mapId = first ? first.map : null;
  const reported = !!first && Array.isArray(b.mobs) && b.mobs_map === mapId;
  return { mobs: reported, mobTargets: reported, botTargets: reported && b.bots.some((x) => !!x.id) };
}
const avail = M.availability(facts(bind));
assert.ok(avail.enemies.ok && avail.critters.ok && avail.enemynames.ok && avail.threat.ok && avail.actionlines.ok);
const missing = M.availability(facts(oldBind));
assert.ok(!missing.enemies.ok && !missing.enemynames.ok && !missing.threat.ok && !missing.actionlines.ok);
assert.match(missing.enemies.why, /newer module/);

// A reported-empty list keeps the layers available (old mobs vanish, the choice stands).
const emptyBind = D.bindOf({ key: "409:14", map: 409, instance: 14, mobs_map: 409, mobs_total: 0, mobs: [], members: [{ name: "B", guid: 6, role: "dps", map: 409, id: "6" }] });
assert.ok(M.availability(facts(emptyBind)).enemies.ok);

// The drawing helpers accept bindOf's output directly: a mob's victim names a bot, a bot's target names a mob.
const threats = CmdView.threatLines(bind.mobs, bind.bots);
assert.strictEqual(threats.length, 1);
assert.strictEqual(threats[0].bot.name, "Tank");
const targets = CmdView.botTargetLines(bind.bots, bind.mobs);
assert.strictEqual(targets.length, 1);
assert.strictEqual(targets[0].mob.id, "900");

// ---- the commander whose fights belong to a run: found by a member guid, by name; demo, pseudo and stale bindings are not asked
{
  const run = { key: "409:14", members: [{ name: "Tank", guid: 10 }, { name: "Healy", guid: 11 }] };
  const bd = (commander, guids, over) => Object.assign({ commander, online: true, bots: guids.map((g) => ({ guid: g, name: "b" + g })) }, over || {});
  assert.strictEqual(D.fightCommander(run, [bd("Zed", [10, 12]), bd("Alpha", [11])]), "Alpha", "the first by name among the bindings that hold a member");
  assert.strictEqual(D.fightCommander(run, [bd("Zed", [99]), bd("Alpha", [98])]), null, "no member guid, no commander");
  assert.strictEqual(D.fightCommander(run, [bd("Alpha", [10], { demo: true })]), null, "a demo binding is ignored");
  assert.strictEqual(D.fightCommander(run, [bd("Alpha", [10], { online: false }), bd("Beta", [10])]), "Beta", "a stale binding (its commander offline) is ignored");
  assert.strictEqual(D.fightCommander(run, [bd("Alpha", [10], { dungeon: true }), bd("Raid#4", [10], { observed: true })]), null, "pseudo-bindings are ignored");
  assert.strictEqual(D.fightCommander(null, [bd("Alpha", [10])]), null);
  assert.strictEqual(D.fightCommander(run, null), null);
}
