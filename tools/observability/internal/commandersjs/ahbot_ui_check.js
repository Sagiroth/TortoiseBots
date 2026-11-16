"use strict";
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");

const appPath = path.resolve(process.argv[2]);
const src = fs.readFileSync(appPath, "utf8");
const indexHtml = fs.readFileSync(path.join(path.dirname(appPath), "..", "..", "index.html"), "utf8");

function grab(name) {
  const re = new RegExp(`^  function ${name}\\(`, "m");
  const m = re.exec(src);
  assert.ok(m, `${name} not found in app.js`);
  const end = src.indexOf("\n  }\n", m.index);
  return src.slice(m.index, end + 4);
}

// 1. Verify index.html wiring inside tab-auctions.
assert.ok(indexHtml.includes('id="tab-auctions"'), "tab-auctions present");
assert.ok(indexHtml.includes('id="ahbot-panel"'), "ahbot-panel present");
assert.ok(indexHtml.includes('id="ahbot-house"'), "ahbot-house filter present");
assert.ok(indexHtml.includes('data-v="ahbot"'), "ah-kind includes ahbot option");

// 2. Run renderAhBot in a minimal VM context.
const elems = {};
const mkEl = (id) => (elems[id] = { id, innerHTML: "", textContent: "", style: {} });
[
  "ahbot-body", "ahbot-status-badge", "ahbot-status-note", "ahbot-house",
  "ah-body", "ah-head", "ah-summary", "ah-house", "ah-count", "ah-page", "ah-prev", "ah-next"
].forEach(mkEl);

const ctx = {
  console,
  QUALITY_NAMES: ["Poor", "Common", "Uncommon", "Rare", "Epic", "Legendary", "Artifact"],
  AH_COLS: [
    { key: "name", label: "Item" },
    { key: "level", label: "Req. lvl" },
    { key: "seller", label: "Seller" },
    { key: "bid", label: "Bid" },
    { key: "buyout", label: "Buyout" },
    { key: "expire", label: "Time left" }
  ],
  ah: { filter: { q: "", quality: "", min_level: "", max_level: "", seller: "", kind: "", house: "" }, sort: "expire", desc: true, page: 0, ahbotHouse: "", data: null, error: null, seq: 0 },
  document: { getElementById: (id) => elems[id] || null },
  esc: (v) => String(v == null ? "" : v).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;"),
  qualityName: (q) => ["Poor", "Common", "Uncommon", "Rare", "Epic", "Legendary", "Artifact"][q] || String(q),
  formatMoney: (c) => `${c}c`,
  getItemIconUrl: () => "",
  botLink: (g, n) => String(n),
  classNameById: () => "Warrior",
};
vm.createContext(ctx);
vm.runInContext([
  grab("ahTimeLeft"),
  grab("ahBotHouseLabel"),
  grab("ahBotSellerStateBadge"),
  grab("renderAhBot"),
  grab("renderAuctions"),
].join("\n"), ctx);

// Off state (table absent or empty).
ctx.renderAhBot({ enabled: false, table_present: false, health: { status: "off", note: "AH bot off (tortoise_bots_ahbot_seller table not present)" } });
assert.ok(elems["ahbot-status-badge"].innerHTML.includes("AH bot off"), "off badge shows AH bot off");
assert.ok(elems["ahbot-body"].innerHTML.includes("AH bot off"), "off body shows AH bot off");
assert.strictEqual(elems["ahbot-house"].style.display, "none", "house filter hidden when off");

// Healthy active state with house x category x quality and escaping check.
ctx.ah.data = {
  matched: 1,
  page: 0,
  page_size: 100,
  rows: [{ id: 1, name: "Copper Ore", quality: 1, item_level: 5, item_template: 2770, required_level: 0, seller_guid: 101, seller_name: "Aldric", seller_kind: "ahbot", start_bid: 10, bid: 0, buyout: 50, expire_at: Math.floor(Date.now() / 1000) + 3600 }],
  summary: { total: 1, with_bid: 0, buyout_value: 50, by_quality: { 1: 1 }, by_house: { alliance: 1 } },
  ahbot: {
    enabled: true,
    table_present: true,
    total: 5,
    with_bid: 1,
    buyout_value: 35000,
    by_house: [
      { house: "alliance", total: 3, by_quality: { 1: 1, 2: 2 } },
      { house: "horde", total: 2, by_quality: { 3: 2 } },
      { house: "neutral", total: 0, by_quality: {} }
    ],
    breakdown: [
      { house: "alliance", category: "TradeGood", class_id: 7, total: 3, by_quality: { 1: 1, 2: 2 } },
      { house: "horde", category: "Armor", class_id: 4, total: 2, by_quality: { 3: 2 } }
    ],
    health: {
      status: "healthy",
      account_id: 55,
      account_name: "TBAHBOT",
      account_checked: true,
      account_exists: true,
      sellers_total: 2,
      healthy_total: 2,
      alliance_total: 1,
      alliance_ok: 1,
      horde_total: 1,
      horde_ok: 1,
      note: "Account TBAHBOT (#55) · 1/1 Alliance · 1/1 Horde"
    },
    sellers: [
      { guid: 101, name: "<script>bad</script>", faction: "alliance", account_id: 55, healthy: true, state: "ok", listings: 3 },
      { guid: 201, name: "Gorak", faction: "horde", account_id: 55, healthy: true, state: "ok", listings: 2 }
    ],
    rates: { derivable: true, sold_per_hour: 12, bought_per_hour: 4, active_bids: 1, note: "From unpurged seller mail in the last 1 h" }
  }
};
ctx.renderAuctions();
assert.ok(elems["ahbot-status-badge"].innerHTML.includes("Healthy"), "healthy badge rendered");
assert.ok(elems["ahbot-body"].innerHTML.includes("TradeGood") && elems["ahbot-body"].innerHTML.includes("Armor"), "breakdown categories rendered");
assert.ok(elems["ahbot-body"].innerHTML.includes("12/h") && elems["ahbot-body"].innerHTML.includes("4/h"), "rates rendered");
assert.ok(!elems["ahbot-body"].innerHTML.includes("<script>bad</script>"), "seller name HTML-escaped");
assert.ok(elems["ah-body"].innerHTML.includes("AH bot"), "ahbot badge rendered in auction table");

// House filter narrows breakdown rows.
ctx.ah.ahbotHouse = "horde";
ctx.renderAhBot(ctx.ah.data.ahbot);
assert.ok(!elems["ahbot-body"].innerHTML.includes("TradeGood") && elems["ahbot-body"].innerHTML.includes("Armor"), "house filter narrows matrix");

console.log("ahbot ui check ok");
