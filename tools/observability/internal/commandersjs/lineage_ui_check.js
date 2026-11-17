"use strict";
// Run by commandersjs_test.go under node: the bot profile's Lineage section (app.js lineageHtml) and the Server panel's
// POOL · LINEAGES block (poolSection). An alt names its parent and lists the line founder first, a character without alts
// shows no section, and an older server without pool health adds no block.
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");
const appPath = path.resolve(process.argv[2]);
const src = fs.readFileSync(appPath, "utf8");

function grab(name) {
  const re = new RegExp(`^  function ${name}\\(`, "m"), m = re.exec(src);
  assert.ok(m, `${name} not found`);
  const end = src.indexOf("\n  }\n", m.index);
  return src.slice(m.index, end + 4);
}

const ctx = {
  console,
  DASH: "-",
  esc: (v) => String(v === undefined || v === null ? "" : v).replace(/&/g, "&amp;").replace(/</g, "&lt;"),
  classNameById: (id) => ({ 4: "Rogue", 11: "Druid" })[id] || "",
  classColor: () => "#fff",
  botLink: (guid, name) => `<a data-guid="${guid}">${name}</a>`,
  flagBadge: (v) => (v === "1" ? "ON" : "OFF"),
  fmtDuration: (sec) => `${Math.floor(sec / 3600)}h`,
  settingsSection: (title, rows) => `[${title}]` + rows.map(([label, value]) => `${label}=${value}`).join("|"),
};
vm.createContext(ctx);
vm.runInContext([grab("lineageHtml"), grab("fmtSpan"), grab("poolSection")].join("\n"), ctx);
const run = (expr, vars) => { if (vars) Object.assign(ctx, vars); return vm.runInContext(expr, ctx); };

// ---- an alt: "alt of" its parent, the founder first, links to the others ---------------------------------------------
const line = {
  parent_guid: 979119, parent_name: "Whiteward", founder_guid: 979119,
  members: [
    { guid: 979119, name: "Whiteward", race: 6, class: 11, level: 60, online: true, last_online: 1794863192 },
    { guid: 979868, parent_guid: 979119, name: "Pazzie", race: 9, class: 4, level: 1, created: 1794863090, online: false, last_online: 1794863090 },
  ],
};
const alt = run("lineageHtml(line, 979868)", { line });
assert.ok(alt.includes('alt of <a data-guid="979119">Whiteward</a>'), "the alt names its parent as a link");
assert.ok(alt.indexOf("Whiteward") < alt.indexOf("Pazzie"), "the founder comes first");
assert.ok(alt.includes("founder"), "the founder is marked");
assert.ok(alt.includes("<strong") && alt.includes("Pazzie</strong>"), "the open character is not a link to itself");
assert.ok(alt.includes("2026-11-16"), "creation day");
assert.ok(!alt.includes("undefined") && !alt.includes("NaN"), "no missing fields printed");

// ---- the founder's own page: no "alt of", the alt is a link -----------------------------------------------------------
// The API names the parent of the opened character only, so the founder's payload carries none.
const founder = run("lineageHtml(Object.assign({}, line, { parent_guid: 0, parent_name: '' }), 979119)");
assert.ok(!founder.includes("alt of"), "a founder is nobody's alt");
assert.ok(founder.includes('<a data-guid="979868">Pazzie</a>'));

// ---- a deleted member keeps its name, without a link -------------------------------------------------------------------
const gone = run("lineageHtml({ founder_guid: 1, members: [{ guid: 1, name: 'A', class: 11, level: 60 }, { guid: 2, name: 'B', class: 4, gone: true, created: 1794863090 }] }, 1)");
assert.ok(gone.includes("B (deleted)") && !gone.includes('data-guid="2"'));

// ---- no line: no section -----------------------------------------------------------------------------------------------
assert.strictEqual(run("lineageHtml(null, 5)"), "");
assert.strictEqual(run("lineageHtml({ founder_guid: 5, members: [{ guid: 5 }] }, 5)"), "");

// ---- Server panel: pool health, the ceiling badge, nothing from an older server ----------------------------------------
const pool = {
  lineage: "1", total: 1002, ceiling: 10000, ceiling_reached: "0", low_pool: 976, low_wanted: 1800, sixty: 26,
  alts: 2, alts_24h: 2, alts_alliance: 1, alts_horde: 1, per_hour: 10, oldest_sixty_unseen_sec: 0, sixty_cycle_sec: 5616,
  character_db_bytes: 305120277, bands: [{ name: "1-5", pool: 2, online: 1, target: 75 }, { name: "60", pool: 26, online: 26, target: 100 }],
};
const num = (v) => (v === undefined || v === null || v === "" ? "–" : String(v));
const cfg = (k) => `AiPlayerbot.${k}`;
const panel = run("poolSection(pool, num, cfg)", { pool, num, cfg });
assert.ok(panel.startsWith("[POOL · LINEAGES]"));
assert.ok(panel.includes("1002 / ceiling 10000") && !panel.includes("ceiling reached"));
assert.ok(panel.includes("976 / wanted 1800"));
assert.ok(panel.includes("291 MB"), "database size in MB");
assert.ok(panel.includes("1-5: <strong>2</strong>"), "every pool character per band, not only the online ones");
assert.ok(run("poolSection(Object.assign({}, pool, { ceiling_reached: '1' }), num, cfg)").includes("ceiling reached"));
assert.ok(run("poolSection(Object.assign({}, pool, { sixty_cycle_sec: 604800 }), num, cfg)").includes("7.0 d"), "long turns in days");
assert.strictEqual(run("poolSection(undefined, num, cfg)"), "", "an older server sends no pool health");

console.log("lineage_ui_check: ok");
