"use strict";
// Run by commandersjs_test.go under node: #bot-search comma-separated OR filter and AND semantics across words.
const assert = require("assert");
const fs = require("fs");
const vm = require("vm");
const src = fs.readFileSync(require("path").resolve(process.argv[2]), "utf8");

const re = /^  function matchBotSearchWord\(/m;
const m = re.exec(src);
assert.ok(m, "matchBotSearchWord not found");
const end = src.indexOf("\n  }\n", m.index);
assert.ok(end !== -1, "matchBotSearchWord end not found");
const fnSrc = src.slice(m.index, end + 4);

const ctx = {};
vm.createContext(ctx);
vm.runInContext(fnSrc + "\nthis.matchBotSearchWord = matchBotSearchWord;", ctx);

const match = (row, query) => {
  const words = query.toLowerCase().trim().split(/\s+/).filter(Boolean);
  return words.every((w) => ctx.matchBotSearchWord(row, w));
};

const alice = { name: "Alice", cls: "warrior", spec: "Prot", kind: "pool", guild: "Knights", race: "Human" };
const bob = { name: "Bobby", cls: "priest", spec: "Holy", kind: "claimed", guild: "Knights", race: "Dwarf" };
const cyd = { name: "Cydonia", cls: "mage", spec: "Frost", kind: "pool", guild: "", race: "Gnome" };
const dave = { name: "Dave", cls: "rogue", spec: "Combat", kind: "pool", guild: "AliceFanClub", race: "Night Elf" };

// Comma-separated names use OR semantics (case-insensitive substring on name)
assert.strictEqual(match(alice, "alice,bob,cyd"), true);
assert.strictEqual(match(bob, "alice,bob,cyd"), true);
assert.strictEqual(match(cyd, "alice,bob,cyd"), true);
assert.strictEqual(match(dave, "alice,bob,cyd"), false, "comma-OR matches name, not guild");

// Space-separated words keep AND semantics
assert.strictEqual(match(alice, "alice,bob warrior"), true);
assert.strictEqual(match(bob, "alice,bob warrior"), false);
assert.strictEqual(match(alice, "knights"), true);
assert.strictEqual(match(dave, "dave rogue"), true);

// Degenerate comma tokens do not break matching
assert.strictEqual(match(alice, "alice,,"), true);
assert.strictEqual(match(dave, ","), true);
