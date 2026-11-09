"use strict";
// Run by commandersjs_test.go under node: FdFilter (web/static/js/fdfilter.js), the Fight debugger's event filter: AND / OR / NOT
// groups of conditions matched against FightEvents' events, the limits (depth 3, 20 conditions, 80 characters), the two fields the
// game server does not send (rejected), the id match for an ability, the URL-safe text form and its caps, the pure tree edits, the
// range, the presets and the header line.
// args: fdfilter.js, then fightevents.js.
const assert = require("assert");
const path = require("path");
const F = require(path.resolve(process.argv[2]));
const E = require(path.resolve(process.argv[3]));
const J = (v) => JSON.parse(JSON.stringify(v));
const C = (f, op, v, not) => ({ f, op, v, not: !!not });
const G = (op, items, not) => ({ op, not: !!not, items });

const siphon = { t: 5000, k: "cast", src: "Hakkar", srcRole: "enemy", ab: "Blood Siphon", sid: 24324, tgt: "Healy", tgtRole: "healer", amt: null };
const death = { t: 40000, k: "death", src: "Hakkar", srcRole: "", ab: "", sid: null, tgt: "Dpsy", tgtRole: "dps", amt: null };
const hit = { t: 39000, k: "hit", src: "Hakkar", srcRole: "", ab: "", sid: null, tgt: "Dpsy", tgtRole: "dps", amt: 1500 };
const aura = { t: 10000, k: "aura_on", src: "", srcRole: "", ab: "Hex", sid: 17172, tgt: "Tanky", tgtRole: "tank", amt: null };

// (kind is cast) AND (ability has "siphon"): a Blood Siphon cast matches, a death does not; case-insensitive
{
  const g = G("and", [C("kind", "is", "cast"), C("ability", "has", "siphon")]);
  assert.ok(F.validate(g).ok);
  assert.strictEqual(F.match(g, siphon), true, "a Blood Siphon cast");
  assert.strictEqual(F.match(g, death), false, "a death");
  assert.strictEqual(F.match(G("and", [C("kind", "is", "CAST")]), siphon), true, "is is case-insensitive");
  assert.strictEqual(F.match(G("and", [C("kind", "is", "cas")]), siphon), false, "is is equality, has is a substring");
}
// an empty group matches everything; an incomplete condition (no value yet) constrains nothing, in AND and in OR
{
  assert.strictEqual(F.match(F.empty(), death), true);
  assert.ok(F.isEmpty(F.empty()));
  const half = G("and", [C("kind", "is", "")]);
  assert.strictEqual(F.match(half, death), true, "a new row does not blank the list");
  assert.ok(F.isEmpty(half));
  assert.strictEqual(F.match(G("or", [C("kind", "is", ""), C("kind", "is", "cast")]), death), false, "an OR with one real condition: the empty row is left out, not a match-all");
  assert.strictEqual(F.match(G("and", [C("amount", "ge", "abc")]), hit), true, "a non-number amount constrains nothing");
}
// OR group, NOT on a group, NOT on a condition
{
  const or = G("or", [C("kind", "is", "death"), C("kind", "is", "hit")]);
  assert.deepStrictEqual([death, hit, siphon].map((e) => F.match(or, e)), [true, true, false], "OR");
  assert.deepStrictEqual([death, hit, siphon].map((e) => F.match(G("or", or.items, true), e)), [false, false, true], "NOT on the group inverts it");
  const notHex = G("and", [C("kind", "is", "aura_on"), C("ability", "has", "Hex", true)]);
  assert.strictEqual(F.match(notHex, aura), false, "NOT on the condition: it is a Hex");
  assert.strictEqual(F.match(notHex, { t: 1, k: "aura_on", ab: "Banish", sid: 24466 }), true);
  const nested = G("and", [C("kind", "is", "aura_on"), G("or", [C("target", "is", "Tanky"), C("target", "is", "Healy")])]);
  assert.strictEqual(F.match(nested, aura), true, "a group inside a group");
}
// depth: 3 levels are fine, a fourth is rejected; 20 conditions are fine, 21 rejected
{
  const d3 = G("and", [G("and", [G("and", [C("kind", "is", "cast")])])]);
  assert.ok(F.validate(d3).ok, "three levels of group");
  const d4 = G("and", [G("and", [G("and", [G("and", [C("kind", "is", "cast")])])])]);
  const v4 = F.validate(d4);
  assert.ok(!v4.ok && /nest/.test(v4.why), "four levels: " + v4.why);
  const many = (n) => G("and", Array.from({ length: n }, () => C("kind", "is", "cast")));
  assert.ok(F.validate(many(20)).ok, "20 conditions");
  const v21 = F.validate(many(21));
  assert.ok(!v21.ok && /20/.test(v21.why), "21 conditions: " + v21.why);
  assert.ok(!F.validate(G("and", [G("and", Array.from({ length: 11 }, () => C("kind", "is", "cast"))), G("and", Array.from({ length: 10 }, () => C("kind", "is", "cast")))])).ok, "the count is over the whole tree");
}
// fields, operators and values: school and hit are rejected (the server does not send them), as is anything unknown
{
  const vs = F.validate(G("and", [C("school", "is", "fire")]));
  assert.ok(!vs.ok && /does not send/.test(vs.why), "school: " + vs.why);
  assert.ok(!F.validate(G("and", [C("hit", "is", "crit")])).ok, "hit");
  assert.ok(!F.validate(G("and", [C("colour", "is", "red")])).ok, "unknown field");
  assert.ok(!F.validate(G("and", [C("kind", "ge", 3)])).ok, "ge on a text field");
  assert.ok(!F.validate(G("and", [C("amount", "has", "3")])).ok, "has on the amount");
  assert.ok(!F.validate(G("and", [C("kind", "is", "x".repeat(81))])).ok, "a value over 80 characters");
  assert.ok(F.validate(G("and", [C("kind", "is", "x".repeat(80))])).ok);
  assert.ok(!F.validate(G("and", [C("amount", "ge", NaN)])).ok, "a non-finite number");
  assert.ok(!F.validate(G("and", [C("kind", "is", {})])).ok, "an object value");
  assert.ok(!F.validate(null).ok && !F.validate("x").ok && !F.validate({ op: "and", not: false }).ok, "not a group");
  assert.ok(!F.validate(G("xor", [])).ok, "unknown group op");
  assert.deepStrictEqual(F.UNAVAILABLE.map((u) => u.f), ["school", "hit"]);
  assert.strictEqual(F.UNAVAILABLE_TITLE, "The game server does not send this");
}
// amount: ge / le / is on a hit, never on an event with no amount
{
  assert.strictEqual(F.match(G("and", [C("amount", "ge", 1000)]), hit), true, "amount ge 1000 on a 1500 hit");
  assert.strictEqual(F.match(G("and", [C("amount", "ge", "1500")]), hit), true, "typed text is read as a number");
  assert.strictEqual(F.match(G("and", [C("amount", "ge", 2000)]), hit), false);
  assert.strictEqual(F.match(G("and", [C("amount", "le", 2000)]), hit), true);
  assert.strictEqual(F.match(G("and", [C("amount", "is", 1500)]), hit), true);
  assert.strictEqual(F.match(G("and", [C("amount", "ge", 0)]), death), false, "a death has no amount");
}
// ability is 24324 matches by spell id; has does not
{
  assert.strictEqual(F.match(G("and", [C("ability", "is", 24324)]), siphon), true, "by id");
  assert.strictEqual(F.match(G("and", [C("ability", "is", "24324")]), siphon), true, "by id typed as text");
  assert.strictEqual(F.match(G("and", [C("ability", "is", 24325)]), siphon), false);
  assert.strictEqual(F.match(G("and", [C("ability", "is", "blood siphon")]), siphon), true, "by name");
  assert.strictEqual(F.match(G("and", [C("source", "is", 24324)]), siphon), false, "the id match is for the ability field only");
}
// encode -> decode round trip; junk and caps
{
  const g = G("or", [C("kind", "is", "cast"), G("and", [C("ability", "has", "Síphoñ ☃", true), C("amount", "ge", 12.5)], true)]);
  const s = F.encode(g);
  assert.ok(/^1\.[A-Za-z0-9_-]+$/.test(s), "prefixed base64url, no padding: " + s);
  assert.deepStrictEqual(F.decode(s), g, "round trip equal (including non-ASCII text)");
  assert.deepStrictEqual(F.decode(F.encode(F.empty())), F.empty());
  assert.strictEqual(F.decode("1.junk!!"), null, "bad base64");
  assert.strictEqual(F.decode("1.junk"), null, "base64 of no JSON");
  assert.strictEqual(F.decode("2." + s.slice(2)), null, "bad prefix");
  assert.strictEqual(F.decode(""), null);
  assert.strictEqual(F.decode(null), null);
  const bad = "1." + Buffer.from(JSON.stringify({ o: "a", i: [{ f: "school", p: "is", v: "x" }] })).toString("base64url");
  assert.strictEqual(F.decode(bad), null, "a tree that does not validate");
  const nul = "1." + Buffer.from("null").toString("base64url");
  assert.strictEqual(F.decode(nul), null);
  // a filter of 20 values of 80 characters is over the 2000 character cap: null, not a long URL
  const big = G("and", Array.from({ length: 20 }, (_, i) => C("ability", "has", String(i % 10).repeat(80))));
  assert.ok(F.validate(big).ok);
  assert.strictEqual(F.encode(big), null, "a ~3 kB tree is left out of URLs");
  assert.strictEqual(F.encode(G("and", [C("school", "is", "x")])), null, "an invalid tree has no text form");
  assert.strictEqual(F.decode("1." + "A".repeat(2100)), null, "an over-long text is refused");
  // every preset encodes and decodes
  F.PRESETS.forEach((p) => assert.deepStrictEqual(F.decode(F.encode(p.group)), p.group, p.id));
}
// setAt / addAt / removeAt return new trees and never change their input
{
  const g = G("and", [C("kind", "is", "cast"), G("or", [C("source", "has", "x")])]);
  const before = J(g);
  const a = F.setAt(g, "1.0", { v: "y", not: true });
  assert.deepStrictEqual(a.items[1].items[0], C("source", "has", "y", true));
  const b = F.addAt(g, "1", F.newCond());
  assert.strictEqual(b.items[1].items.length, 2);
  const c = F.removeAt(g, "0");
  assert.strictEqual(c.items.length, 1);
  assert.ok(isGroup(c.items[0]));
  const d = F.setAt(g, "", { op: "or" });
  assert.strictEqual(d.op, "or", "the root has the empty path");
  assert.deepStrictEqual(g, before, "the input is not mutated");
  const n = F.newCond();
  const e = F.addAt(g, "", n);
  n.v = "changed";
  assert.strictEqual(e.items[2].v, "", "the added node is a copy");
  assert.deepStrictEqual(F.removeAt(g, ""), g, "the root cannot be removed");
  assert.deepStrictEqual(F.setAt(g, "9.9", { v: 1 }), g, "a path that is not there changes nothing");
  assert.deepStrictEqual(F.removeAt(g, "5"), g);
  assert.deepStrictEqual(F.addAt(g, "0", F.newCond()), g, "a condition has no items to add to");
  assert.deepStrictEqual(F.parsePath("0.2.1"), [0, 2, 1]);
  assert.deepStrictEqual(F.parsePath(""), []);
  const frozen = Object.freeze(J(g));
  assert.strictEqual(F.setAt(frozen, "0", { v: "z" }).items[0].v, "z", "works on a frozen tree");
}
function isGroup(n) { return Array.isArray(n.items); }
// apply: the range drops events outside it, the filter drops the rest; the order is kept
{
  const evs = [siphon, aura, hit, death];
  const g = G("or", [C("kind", "is", "cast"), C("kind", "is", "death"), C("kind", "is", "aura_on")]);
  assert.deepStrictEqual(F.apply(g, evs, null).map((e) => e.k), ["cast", "aura_on", "death"]);
  assert.deepStrictEqual(F.apply(g, evs, { from: 6000, to: 39500 }).map((e) => e.k), ["aura_on"], "range 6-39.5 s: the cast (5 s) and the death (40 s) are outside it");
  assert.deepStrictEqual(F.apply(F.empty(), evs, { from: 0, to: 9999 }).map((e) => e.k), ["cast"], "an empty filter still honours the range");
  assert.deepStrictEqual(F.apply(F.empty(), evs, { from: 5000, to: 5000 }).map((e) => e.k), ["cast"], "the range is closed at both ends");
  assert.strictEqual(F.apply(F.empty(), null, null).length, 0);
}
// presets: ids and labels, each a valid group; "Auras on tanks" and "Without target changes" do what they say
{
  assert.deepStrictEqual(F.PRESETS.map((p) => [p.id, p.label]), [["casts", "Boss casts"], ["deaths", "Deaths and last hits"], ["tankauras", "Auras on tanks"], ["notrash", "Without target changes"]]);
  F.PRESETS.forEach((p) => assert.ok(F.validate(p.group).ok, p.id));
  const fx = E.build({ bots: [{ name: "Tanky", guid: 11, role: "tank" }, { name: "Dpsy", guid: 13, role: "dps" }], auras: [[1, 11, 24321, 1], [2, 13, 24321, 1], [3, 11, 24321, -1]], victims: [[1, 11, "Tanky", "tank"]], log: [{ k: "cast", t: 5, n: "B", s: "x" }] }).events;
  const by = (id) => F.apply(F.preset(id), fx, null).map((e) => e.k + ":" + e.tgt);
  assert.deepStrictEqual(by("tankauras"), ["aura_on:Tanky"]);
  assert.ok(!by("notrash").some((x) => x.startsWith("target")) && by("notrash").length === fx.length - 1);
  assert.deepStrictEqual(by("casts"), ["cast:"]);
  assert.notStrictEqual(F.preset("casts"), F.PRESETS[0].group, "a preset is handed out as a copy");
  assert.strictEqual(F.preset("nope"), null);
}
// describe: the short English line
{
  assert.strictEqual(F.describe(G("and", [C("kind", "is", "cast"), C("target", "has", "Hex", true)])), "kind is Cast and not target has Hex", "a kind is described by the label the table shows");
  assert.strictEqual(F.describe(F.empty()), "every event");
  assert.strictEqual(F.describe(G("and", [C("kind", "is", "")])), "every event", "an incomplete row is not described");
  assert.strictEqual(F.describe(G("or", [C("kind", "is", "death"), G("and", [C("amount", "ge", 100), C("srcRole", "is", "enemy")])])), "kind is Death or (amount is at least 100 and source role is enemy)");
  assert.strictEqual(F.describe(G("or", [C("kind", "is", "death"), C("kind", "is", "hit")], true)), "not (kind is Death or kind is Hit before death)");
  assert.strictEqual(F.describe(null), "");
}
// kind: the visible label works as well as the id ("Aura applied" is aura_on), in is and has, case-insensitively
{
  const ev = (k) => ({ t: 1, k, ab: "", src: "", tgt: "" });
  const is = (v, not) => G("and", [C("kind", "is", v, not)]);
  assert.strictEqual(F.match(is("Aura applied"), ev("aura_on")), true, "the label");
  assert.strictEqual(F.match(is("aura_on"), ev("aura_on")), true, "the id");
  assert.strictEqual(F.match(is("aura APPLIED"), ev("aura_on")), true, "case-insensitive");
  assert.strictEqual(F.match(is("Aura applied"), ev("aura_off")), false, "another kind");
  assert.strictEqual(F.match(is("Aura applied", true), ev("aura_on")), false, "NOT");
  assert.strictEqual(F.match(is("Hit before death"), ev("hit")), true);
  assert.strictEqual(F.match(G("and", [C("kind", "has", "applied")]), ev("aura_on")), true, "has reads the label");
  assert.strictEqual(F.match(G("and", [C("kind", "has", "aura_o")]), ev("aura_on")), true, "and the id");
  assert.strictEqual(F.match(G("and", [C("kind", "has", "aura")]), ev("cast")), false);
  assert.strictEqual(F.describe(is("aura_on")), "kind is Aura applied");
  assert.strictEqual(F.describe(is("Aura applied")), "kind is Aura applied", "a label typed by the reader is printed as it is");
  assert.strictEqual(F.describe(G("and", [C("target", "is", "aura_on")])), "target is aura_on", "only a kind is mapped");
}
// the field and operator tables the builder reads
{
  assert.deepStrictEqual(F.opsFor("amount"), ["is", "ge", "le"]);
  assert.deepStrictEqual(F.opsFor("kind"), ["is", "has"]);
  assert.deepStrictEqual(F.FIELDS.map((x) => x.f), ["kind", "ability", "source", "target", "srcRole", "tgtRole", "amount"]);
  assert.ok(F.FIELDS.every((x) => E.FIELD_PROP[x.f]), "every filter field is an event property FightEvents knows");
}
console.log("fdfilter_check.js ok");
