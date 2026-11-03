"use strict";
// Run by commandersjs_test.go under node: the page asks for nothing the daemon does not have. A map picture is requested only
// when the world view draws it, the agent-maps probe reads the daemon's enabled flag instead of a 404, and a demo group's
// made-up plan is never asked of the plans API.
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");
const appPath = path.resolve(process.argv[2]), dir = path.dirname(appPath);
const src = fs.readFileSync(appPath, "utf8");
const indexHtml = fs.readFileSync(path.join(dir, "..", "..", "index.html"), "utf8");

function grab(name) {
  const re = new RegExp(`^  function ${name}\\(`, "m"), m = re.exec(src);
  assert.ok(m, `${name} not found`);
  const end = src.indexOf("\n  }\n", m.index);
  return src.slice(m.index, end + 4);
}
function grabConst(name) {
  const re = new RegExp(`^  const ${name} = `, "m"), m = re.exec(src);
  assert.ok(m, `${name} not found`);
  const end = src.indexOf(";\n", m.index);
  return src.slice(m.index, end + 2);
}

// ---- no map picture is requested at load -------------------------------------------------------------------------------
const world = /<img[^>]*id="world-img"[^>]*>/.exec(indexHtml);
const zone = /<img[^>]*id="map-img"[^>]*>/.exec(indexHtml);
assert.ok(world && !/\bsrc=/.test(world[0]), "the continent image starts with no source: " + (world || []).toString());
assert.ok(zone && !/\bsrc=/.test(zone[0]), "the zone image starts with no source: " + (zone || []).toString());

const ctx = { console };
vm.createContext(ctx);
vm.runInContext(grab("worldArtFile"), ctx);
assert.strictEqual(vm.runInContext("worldArtFile(0)", ctx), "/maps/azeroth.webp");
assert.strictEqual(vm.runInContext("worldArtFile(1)", ctx), "/maps/kalimdor.webp");
assert.strictEqual(vm.runInContext("worldArtFile(999)", ctx), "/maps/azeroth.webp", "an unknown continent falls back to the main picture");
const renderWorld = src.slice(src.indexOf("  function renderWorld()"), src.indexOf("\n  }\n", src.indexOf("  function renderWorld()")));
assert.ok(/worldImg\.src = file/.test(renderWorld), "the world view sets the picture when it draws");
assert.ok(/worldImg\.src = file/.test(src.slice(src.indexOf("  function setWorldTab("), src.indexOf("\n  }\n", src.indexOf("  function setWorldTab(")))),
  "switching continent sets the picture");

// ---- no zone art on another tab: it is asked for only while the Map tab shows that zone -------------------------------
const paintZone = src.slice(src.indexOf("  function paintZoneArt()"), src.indexOf("\n  }\n", src.indexOf("  function paintZoneArt()")));
assert.ok(/state\.activeTab === 'map' && state\.mapView === 'zone'/.test(paintZone), "the zone picture is asked for only on the Map tab's zone view");
assert.ok(/mapImg\.src = `\/maps\/\$\{zone\.file\}`/.test(paintZone), "and that is where it is set");
const loadZone = src.slice(src.indexOf("  function loadZoneMap("), src.indexOf("\n  }\n", src.indexOf("  function loadZoneMap(")));
assert.ok(!/mapImg\.(setAttribute\('src'|src =)/.test(loadZone), "loadZoneMap (called on every page load) asks for no picture");
const renderMapSrc = src.slice(src.indexOf("  function renderMap()"), src.indexOf("\n  }\n", src.indexOf("  function renderMap()")));
assert.ok(/paintZoneArt\(\);/.test(renderMapSrc), "renderMap paints the zone art");
assert.ok(/if \(state\.mapView === 'world' && state\.activeTab === 'map'\) renderMap\(\);/.test(src),
  "the zones data redraws the continents only on the Map tab (no /maps/ request on a Commanders load)");

// ---- the agent-maps probe reads the daemon's flag, not a 404 ----------------------------------------------------------
assert.ok(src.includes("data.enabled === false"), "the probe hides the tab on the daemon's enabled=false");
assert.ok(src.includes("r.status === 404"), "an older daemon's 404 still hides it");

// ---- a demo group has no plan in the world database --------------------------------------------------------------------
const planCtx = { console };
vm.createContext(planCtx);
vm.runInContext([
  grabConst("CMD_PLAN_RETRY_MS"), grab("cmdPlanGet"),
  "var cmdPlans = {}; var calls = [];",
  "var fetch = (u) => { calls.push(String(u)); return new Promise(() => {}); };",
  "var cmdCards = new Map(); var drawCmdMap = () => {};",
].join("\n"), planCtx);
assert.strictEqual(vm.runInContext("cmdPlanGet('uldaman-demo', true)", planCtx), null);
assert.deepStrictEqual(JSON.parse(JSON.stringify(vm.runInContext("calls", planCtx))), [], "a demo plan is not asked for");
assert.ok(src.includes("cmdPlanGet(name, !!bind.demo)"), "both plan readers pass the demo flag");
vm.runInContext("cmdPlanGet('onyxias_lair', false)", planCtx);
assert.deepStrictEqual(JSON.parse(JSON.stringify(vm.runInContext("calls", planCtx))), ["/api/v1/plans/onyxias_lair"],
  "a real plan is asked for once");

// ---- the coverage view's script is loaded before app.js ---------------------------------------------------------------
const covTag = /<script src="\/static\/js\/coverage\.js\?v=1"><\/script>/.exec(indexHtml);
const appTag = /<script src="\/static\/js\/app\.js\?v=[\d]+"><\/script>/.exec(indexHtml);
assert.ok(covTag, "index.html loads coverage.js versioned");
assert.ok(appTag, "index.html loads app.js versioned");
assert.ok(indexHtml.indexOf(covTag[0]) < indexHtml.indexOf(appTag[0]), "coverage.js comes before app.js");
assert.ok(indexHtml.includes('id="tab-coverage"') && indexHtml.includes('id="cov-root"'), "the coverage tab has its root");

console.log("page assets ok");
