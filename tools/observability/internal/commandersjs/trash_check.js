"use strict";
// Run by commandersjs_test.go under node: the trash abilities module (web/static/js/trash.js) — the data/boss/trash_abilities.csv
// page half — fed with a fixture table in the converter's shape. Then, when app.js is passed too, the pieces the page draws from
// it: the KICK/AoE word before an elite's cast, the danger-3 map glyph and the Live card's "Dangerous here" block. Behaviour
// only, never a source-text pin.
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");

const moduleSrc = fs.readFileSync(path.resolve(process.argv[2]), "utf8");
const appPath = process.argv[3] ? path.resolve(process.argv[3]) : "";

// Everything the module hands back was built in another vm realm, so a structural compare goes through JSON.
const j = (x) => JSON.parse(JSON.stringify(x));

// A fresh module instance with its own fetch: load() caches, so each fixture needs a clean copy.
function fresh(doc, opts) {
  const ctx = { console };
  vm.createContext(ctx);
  ctx.fetch = (opts && opts.reject)
    ? () => Promise.reject(new Error("no file"))
    : () => Promise.resolve({ ok: !(opts && opts.ok === false), json: () => Promise.resolve(doc) });
  vm.runInContext(moduleSrc, ctx, { filename: "trash.js" });
  return ctx.TrashInfo;
}

const FIXTURE = {
  version: 1,
  note: "test",
  maps: [{ key: "zulgurub", mapId: 309 }],
  mobs: {
    "15111": { entry: 15111, name: "Mad Servant", map: "zulgurub", danger: 3, roles: ["caster", "summoner"], kill: 3,
      abilities: [
        { sid: 24611, name: "Fireball", int: true, t: "victim", aoe: false, ground: false, cone: false, r: 0, ok: "" },
        { sid: 24612, name: "Flamestrike", int: false, t: "aoe", aoe: true, ground: true, cone: false, r: 8, ok: "" },
        { sid: 24613, name: "Shadow Bolt", int: false, t: "victim", aoe: false, ground: false, cone: false, r: 0, ok: "" },
      ], notes: ["brain food"] },
    "15112": { entry: 15112, name: "Grim Caster", danger: 2, roles: ["caster"], kill: 0, abilities: [], notes: [] },
    "15113": { entry: 15113, name: "Docile Rat", danger: 1, roles: [], kill: 0, abilities: [], notes: [] },
    "15114": { entry: 15114, name: "Wanderer", danger: 2, roles: [], kill: 0, abilities: [], notes: [] },
    "15115": { entry: 15115, name: "Sneaky <script>", danger: 3, roles: ["cc"], kill: 0, abilities: [], notes: [] },
  },
};

async function main() {
  // ---- castWord: KICK for the interruptible ability, AoE for the area one, "" otherwise ---------------------
  const T = fresh(FIXTURE);
  const loaded = await T.load();
  assert.ok(loaded && loaded.byEntry[15111], "load resolves the normalized table");
  assert.strictEqual(T.castWord("15111", "24611"), "KICK");
  assert.strictEqual(T.castWord(15111, 24612), "AoE", "not interruptible but a persistent ground area");
  assert.strictEqual(T.castWord("15111", "24613"), "", "neither kickable nor an area");
  assert.strictEqual(T.castWord("15111", "99999"), "", "a spell the mob does not have");
  assert.strictEqual(T.castWord("15199", "24611"), "", "an unknown entry");

  // ---- the lookups match string entries and spell ids from JSON as numbers ---------------------------------
  assert.strictEqual(T.for("15111").name, "Mad Servant");
  assert.strictEqual(T.for(15111).entry, 15111);
  assert.strictEqual(T.for("nope"), null);
  assert.strictEqual(T.ability("15111", "24611").name, "Fireball");
  assert.strictEqual(T.ability(15111, 24611).sid, 24611);
  assert.strictEqual(T.ability("15111", "1"), null);
  assert.strictEqual(T.danger("15111"), 3);
  assert.strictEqual(T.danger(15112), 2);
  assert.strictEqual(T.danger("nope"), 0);
  assert.deepStrictEqual(j(T.data().maps), [{ key: "zulgurub", mapId: 309 }]);

  // ---- line: the exact hint, and no stray separator when there is no hint ----------------------------------
  assert.strictEqual(T.line(T.for(15111), 4), "4× Mad Servant: kill first · kick Fireball · caster, summoner");
  assert.strictEqual(T.line(T.for("15114"), 2), "2× Wanderer", "no kill, no kick, no roles: just the head");
  assert.strictEqual(T.line(T.for("15112"), 1), "1× Grim Caster: caster", "roles alone still take the separator");

  // ---- block: danger-1 and unknown rows dropped, order danger/count/entry, at most five --------------------
  const rows = j(T.block([
    { entry: "15111" }, { entry: "15111" }, { entry: "15111" }, { entry: "15111" },
    { entry: "15112" }, { entry: "15112" },
    { entry: "15114" }, { entry: "15113" }, { entry: "123456" }, { entry: null },
  ]));
  assert.deepStrictEqual(rows.map((r) => r.entry), [15111, 15112, 15114], "danger 1 and unknown rows are dropped");
  assert.deepStrictEqual(rows.map((r) => r.count), [4, 2, 1]);
  assert.deepStrictEqual(rows.map((r) => r.danger), [3, 2, 2]);
  assert.strictEqual(rows[0].text, "4× Mad Servant: kill first · kick Fireball · caster, summoner");
  assert.strictEqual(rows[2].text, "1× Wanderer");

  const many = fresh({ mobs: {
    "1": { entry: 1, name: "A", danger: 3 }, "2": { entry: 2, name: "B", danger: 2 }, "3": { entry: 3, name: "C", danger: 2 },
    "4": { entry: 4, name: "D", danger: 2 }, "5": { entry: 5, name: "E", danger: 2 }, "6": { entry: 6, name: "F", danger: 2 },
  } });
  await many.load();
  const capped = j(many.block([{ entry: 1 }, { entry: 2 }, { entry: 3 }, { entry: 4 }, { entry: 5 }, { entry: 6 }]));
  assert.strictEqual(capped.length, 5, "at most five lines");
  assert.deepStrictEqual(capped.map((r) => r.entry), [1, 2, 3, 4, 5], "danger first, then the count and the entry ascending");

  // ---- a missing, rejected or empty table leaves every lookup null/empty ----------------------------------
  const gone = fresh(FIXTURE, { reject: true });
  assert.strictEqual(await gone.load(), null, "a rejected fetch resolves null");
  assert.strictEqual(gone.for(15111), null);
  assert.strictEqual(gone.ability(15111, 24611), null);
  assert.strictEqual(gone.castWord(15111, 24611), "");
  assert.strictEqual(gone.danger(15111), 0);
  assert.deepStrictEqual(j(gone.block([{ entry: 15111 }])), []);

  const notFound = fresh(FIXTURE, { ok: false });
  assert.strictEqual(await notFound.load(), null, "a 404 resolves null");
  assert.strictEqual(await fresh({ version: 1, maps: [], mobs: {} }).load(), null, "a table with no mob resolves null");

  const never = fresh(FIXTURE);
  assert.strictEqual(never.for(15111), null, "before load the module knows nothing");
  assert.strictEqual(never.castWord(15111, 24611), "");
  assert.deepStrictEqual(j(never.block([{ entry: 15111 }])), []);

  let calls = 0;
  const once = { console };
  vm.createContext(once);
  once.fetch = () => { calls += 1; return Promise.resolve({ ok: true, json: () => Promise.resolve(FIXTURE) }); };
  vm.runInContext(moduleSrc, once, { filename: "trash.js" });
  await once.TrashInfo.load(); await once.TrashInfo.load();
  assert.strictEqual(calls, 1, "the table is fetched once");

  // ---- the page pieces, when app.js is passed --------------------------------------------------------------
  if (appPath) {
    const appSrc = fs.readFileSync(appPath, "utf8");
    const appCtx = { console };
    vm.createContext(appCtx);
    vm.runInContext(moduleSrc, appCtx, { filename: "trash.js" });
    appCtx.fetch = () => Promise.resolve({ ok: true, json: () => Promise.resolve(FIXTURE) });
    await appCtx.TrashInfo.load();
    appCtx.CmdView = { mobClass: () => "hostile" };
    const grab = (name) => {
      const m = new RegExp(`^  function ${name}\\(`, "m").exec(appSrc);
      assert.ok(m, `${name} not found`);
      const end = appSrc.indexOf("\n  }\n", m.index);
      return appSrc.slice(m.index, end + 4);
    };
    vm.runInContext([grab("esc"), grab("cmdTrashWord"), grab("cmdMobLook"), grab("cmdTrashBlockHtml"), grab("cmdPaintBosses")].join("\n"), appCtx);
    const run = (expr) => vm.runInContext(expr, appCtx);

    // the cast word: an elite with a known spell says KICK or AoE, a boss never does
    assert.deepStrictEqual(j(run("cmdTrashWord({ entry: 15111, cid: 24611, rank: 'e' })")), { word: "KICK", cls: "kick" });
    assert.deepStrictEqual(j(run("cmdTrashWord({ entry: 15111, cid: 24612, rank: 'e' })")), { word: "AoE", cls: "aoe" });
    assert.strictEqual(run("cmdTrashWord({ entry: 15111, cid: 24611, rank: 'b' })"), "", "a boss cast is the boss table's business");
    assert.strictEqual(run("cmdTrashWord({ entry: 99999, cid: 24611, rank: 'e' })"), "");
    assert.strictEqual(run("cmdTrashWord({ entry: 15111, cid: 24613, rank: 'e' })"), "");

    // the danger glyph: only a boss-marked danger-3 elite
    assert.strictEqual(run("cmdMobLook({ entry: 15111, rank: 'e' }, true).danger"), 3);
    assert.strictEqual(run("cmdMobLook({ entry: 15112, rank: 'e' }, true).danger"), 0, "danger 2 shows no glyph");
    assert.strictEqual(run("cmdMobLook({ entry: 15111, rank: 'e' }, false).danger"), 0, "the layer is off");
    assert.strictEqual(run("cmdMobLook({ entry: 15111, rank: 'b' }, true).danger"), 0, "a boss is not trash");

    // the Live block: text escaped, only rows the table knows and only danger >= 2 survive
    const html = run("cmdTrashBlockHtml({ mobs: [{ entry: 15111 }, { entry: 15113 }, { entry: 123456 }] })");
    assert.ok(html.includes('<h3 class="cmd-h">Dangerous here</h3>'), html);
    assert.ok(html.includes('<ol class="fd-trashlist"><li>1× Mad Servant: kill first · kick Fireball · caster, summoner</li></ol>'), html);
    assert.strictEqual(run("cmdTrashBlockHtml({ mobs: [{ entry: 15113 }] })"), "", "no dangerous row: no block");
    const evil = run("cmdTrashBlockHtml({ mobs: [{ entry: 15115 }] })");
    assert.ok(evil.includes("&lt;script&gt;") && !/<script/.test(evil), evil);
    assert.ok(evil.includes("1× Sneaky &lt;script&gt;: cc"), evil);

    // ---- the paint: one box, one write per report --------------------------------------------------------
    // cmdPaintBosses decides between the boss panels and the trash fallback once, so an unchanged report writes
    // nothing (the list keeps its hover) and a boss panel never shows the trash under it.
    const { createDocument } = require("./fakedom.js");
    const dom = createDocument();
    const card = dom.element("section", "", "cmd-card");
    const section = dom.element("section", "", "fd-bosses");
    const list = dom.element("div", "", "fd-bosslist");
    dom.attach(card, section);
    dom.attach(section, list);
    let writes = 0;
    appCtx.setHtml = (node, html) => { writes += 1; node._h = html; node.innerHTML = html; };
    appCtx.cmdBossPanelsHtml = () => "<p>boss panel</p>";
    const paint = (panels, fallback) => {
      writes = 0;
      appCtx.cmdPaintBosses(card, panels, fallback);
      return { writes: writes, html: list.innerHTML, hidden: section.hidden };
    };
    const trashHtml = run("cmdTrashBlockHtml({ mobs: [{ entry: 15111 }] })");
    let painted = paint([], trashHtml);
    assert.strictEqual(painted.writes, 1, "the trash list is written once");
    assert.strictEqual(painted.html, trashHtml);
    assert.strictEqual(painted.hidden, false, "the boss section shows the trash");
    painted = paint([], trashHtml);
    assert.strictEqual(painted.writes, 0, "an identical report writes nothing, so the hover survives");
    painted = paint([{ boss: {} }], trashHtml);
    assert.strictEqual(painted.writes, 1);
    assert.strictEqual(painted.html, "<p>boss panel</p>", "a boss panel takes the box");
    assert.ok(!painted.html.includes("Dangerous here"), "no trash under a boss panel");
    painted = paint([{ boss: {} }], trashHtml);
    assert.strictEqual(painted.writes, 0, "an identical panel report writes nothing either");
    painted = paint([], "");
    assert.strictEqual(painted.hidden, true, "no panel and no trash: the section hides");
    appCtx.cmdPaintBosses(null, [], trashHtml); // no card: no throw
  }
}

main().then(() => {}, (e) => { console.error((e && e.stack) || e); process.exit(1); });
