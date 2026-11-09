"use strict";
// Run by commandersjs_test.go under node: FdLayout (web/static/js/fdlayout.js), the pure side of the Fight debugger's panel layouts:
// the panel table, normalize, the link code (encode and decode), apply, and the viewer's named layouts in storage (junk, a storage that
// throws, the 20-layout cap, names). On a tree without the module this check fails at the first line: that is the named failing case.
const assert = require("assert");
const path = require("path");
const FdLayout = require(path.resolve(process.argv[2]));
const eq = (a, b, msg) => assert.deepStrictEqual(JSON.parse(JSON.stringify(a)), b, msg);

// ---- the panel table: fixed order, short codes, default spans; the allowed spans and their labels
eq(FdLayout.PANELS.map((p) => p.id), ["run", "frames", "controls", "bosses", "rail", "map", "meters", "checks", "timeline", "auras", "events", "live", "roles", "under", "mech"], "the default order");
eq(FdLayout.PANELS.map((p) => p.code), ["ru", "fr", "co", "bo", "ra", "ma", "me", "ch", "tl", "au", "ev", "li", "ro", "un", "mc"]);
eq(FdLayout.PANELS.map((p) => p.span), [12, 12, 12, 12, 3, 6, 6, 6, 6, 6, 6, 3, 3, 12, 12]);
assert.strictEqual(FdLayout.PANELS.find((p) => p.id === "mech").label, "Boss mechanics");
eq(FdLayout.SPANS, [3, 4, 6, 8, 12]);
eq(FdLayout.SPANS.map((s) => FdLayout.SPAN_LABELS[s]), ["Quarter", "Third", "Half", "Two thirds", "Full"]);
assert.strictEqual(FdLayout.STORAGE_KEY, "tbdash.fd.layouts.v1");

// ---- normalize: unknown ids dropped, repeats dropped, a bad span coerced, missing panels appended
const DEFAULT_CODE = "ru.12~fr.12~co.12~bo.12~ra.3~ma.6~me.6~ch.6~tl.6~au.6~ev.6~li.3~ro.3~un.12~mc.12";
const n1 = FdLayout.normalize({ v: 1, panels: [{ id: "map", span: 8 }, { id: "xx", span: 6 }, { id: "map", span: 3 }, { id: "timeline", span: 5, hide: true }] });
eq(n1.panels.slice(0, 2), [{ id: "map", span: 8, hide: false }, { id: "timeline", span: 6, hide: true }], "xx dropped, the second map dropped, span 5 -> the panel's default 6");
assert.strictEqual(n1.panels.length, 15, "every panel appears once");
eq(n1.panels.slice(2).map((e) => e.id), FdLayout.PANELS.map((p) => p.id).filter((id) => id !== "map" && id !== "timeline"), "the missing ones follow in default order");
assert.ok(n1.panels.slice(2).every((e) => !e.hide), "appended panels are shown");
eq(FdLayout.normalize(null), FdLayout.defaults(), "null is the default layout");
eq(FdLayout.normalize("nope"), FdLayout.defaults());
eq(FdLayout.normalize({ panels: "x" }), FdLayout.defaults());
assert.strictEqual(FdLayout.normalize({ panels: [{ id: "map", hide: "yes" }] }).panels[0].hide, false, "only true hides");
assert.strictEqual(FdLayout.normalize({ panels: [{ id: "map", span: "8" }] }).panels[0].span, 6, "a span that is text is not a span");
assert.strictEqual(FdLayout.normalize({ panels: [{ id: "__proto__", span: 3 }, { id: "constructor" }, { id: "toString" }, null, 7, "map"] }).panels.length, 15, "hostile ids and entries change nothing");
eq(FdLayout.normalize({ panels: [{ id: "__proto__" }] }), FdLayout.defaults());
const huge = { panels: new Array(5000).fill({ id: "map", span: 3 }) };
assert.strictEqual(FdLayout.normalize(huge).panels.length, 15, "a huge list is bounded and still one of each");
const keep = { panels: [{ id: "map", span: 8 }] };
FdLayout.normalize(keep);
eq(keep, { panels: [{ id: "map", span: 8 }] }, "the input is not changed");

// ---- encode: the default order is the exact string, mech at its place; decode(encode(l)) is l
assert.strictEqual(FdLayout.encode(FdLayout.defaults()), DEFAULT_CODE, "the default code");
assert.ok(DEFAULT_CODE.includes("un.12~mc.12"), "mech sits after under");
assert.strictEqual(FdLayout.encode({ panels: [{ id: "map", span: 8 }, { id: "timeline", span: 12 }, { id: "auras", span: 6, hide: true }] }).split("~").slice(0, 3).join("~"), "ma.8~tl.12~au.6.h");
const L = FdLayout.normalize({ panels: [{ id: "mech", span: 4 }, { id: "map", span: 12, hide: true }, { id: "roles", span: 8 }] });
eq(FdLayout.decode(FdLayout.encode(L)), L, "decode(encode(l)) equals l");
eq(FdLayout.decode(DEFAULT_CODE), FdLayout.defaults());

// ---- decode: junk is null; unknown codes are dropped, the rest kept
const z = FdLayout.decode("ma.8~zz.3");
eq(z.panels[0], { id: "map", span: 8, hide: false }, "ma.8~zz.3 keeps only the map ...");
assert.strictEqual(z.panels.length, 15, "... plus the appended defaults");
eq(z.panels.slice(1).map((e) => e.id), FdLayout.PANELS.map((p) => p.id).filter((id) => id !== "map"));
const longCode = (n) => Array.from({ length: n }, () => "ma.12.h").join("~"); // well-formed items only: the length is the one thing wrong
assert.strictEqual(longCode(58).length, 463);
assert.strictEqual(FdLayout.decode(longCode(52)), null, "a 415-character code of well-formed items is refused for its length");
assert.strictEqual(longCode(57).length, 455);
assert.notStrictEqual(FdLayout.decode(longCode(50)), null, "399 characters are read (the cap is 400)");
assert.strictEqual(FdLayout.decode("ma.8~".repeat(80) + "ma.8"), null, "a 404-character code is refused");
assert.notStrictEqual(FdLayout.decode("ma.8~" + "tl.12~".repeat(5) + "au.6"), null, "a repeat inside the cap is read (and deduplicated)");
const mk = (sevens, sixes) => [].concat(Array(sevens).fill("ma.12.h"), Array(sixes).fill("ma.3.h")).join("~"); // 7- and 6-character items
assert.strictEqual(mk(44, 7).length, 400);
assert.notStrictEqual(FdLayout.decode(mk(44, 7)), null, "exactly 400 characters are still read");
assert.strictEqual(mk(45, 6).length, 401);
assert.strictEqual(FdLayout.decode(mk(45, 6)), null, "401 characters are not");
["", "ma", "ma.", "ma.x", "ma.8.", "ma.8.x", "MA.8", "ma.8~", "~ma.8", "ma.8~~tl.6", "ma.800", "ma.8.h.h", "ma.8 ", "../..", "__proto__.3", "ma.8\u0000"].forEach((s) => {
  assert.strictEqual(FdLayout.decode(s), null, "bad syntax -> null: " + JSON.stringify(s));
});
[null, undefined, 5, {}, [], ["ma.8"], true].forEach((v) => assert.strictEqual(FdLayout.decode(v), null, "not text -> null"));
assert.strictEqual(FdLayout.decode("ma.5").panels[0].span, 6, "a span that is not allowed is the default, not an error");
assert.strictEqual(FdLayout.decode("ma.8.h").panels[0].hide, true);
assert.strictEqual(FdLayout.decode(Array.from({ length: 61 }, () => "ma.8").join("~")), null, "more items than the cap");

// ---- the dialog's edits: move, resize, show; none changes its input, bad ids and ends change nothing
{
  const D = FdLayout.defaults();
  eq(FdLayout.move(D, "map", -1).panels.slice(4, 7).map((e) => e.id), ["map", "rail", "meters"], "map moves up past the rail");
  eq(FdLayout.move(D, "map", 1).panels.slice(4, 7).map((e) => e.id), ["rail", "meters", "map"], "and down");
  eq(FdLayout.move(D, "run", -1), D, "the first panel cannot go up");
  eq(FdLayout.move(D, "mech", 1), D, "the last cannot go down");
  eq(FdLayout.move(D, "zz", 1), D, "an unknown id changes nothing");
  assert.strictEqual(FdLayout.resize(D, "map", 12).panels[5].span, 12);
  assert.strictEqual(FdLayout.resize(D, "map", 5).panels[5].span, 6, "a span that is not allowed changes nothing");
  assert.strictEqual(FdLayout.resize(D, "map", "12").panels[5].span, 6, "text is not a span");
  assert.strictEqual(FdLayout.show(D, "auras", false).panels[9].hide, true);
  assert.strictEqual(FdLayout.show(FdLayout.show(D, "auras", false), "auras", true).panels[9].hide, false);
  eq(D, FdLayout.defaults(), "the input is untouched");
  eq(FdLayout.move(null, "map", -1).panels[4].id, "map", "a null layout is the default one");
}

// ---- apply: the renderer's map
const ap = FdLayout.apply({ panels: [{ id: "map", span: 12 }, { id: "timeline", span: 12 }] });
eq(ap.map, { order: 0, span: 12, hide: false });
eq(ap.timeline, { order: 1, span: 12, hide: false });
assert.strictEqual(Object.keys(ap).length, 15);
eq(ap.run, { order: 2, span: 12, hide: false }, "the rest follow in default order");
assert.strictEqual(FdLayout.apply(null).mech.order, 14);

// ---- storage: junk, a throwing storage, the cap, names
const mem = () => { const m = {}; return { m, getItem: (k) => (k in m ? m[k] : null), setItem: (k, v) => { m[k] = String(v); } }; };
const throwing = { getItem() { throw new Error("blocked"); }, setItem() { throw new Error("blocked"); } };
eq(FdLayout.load(throwing), { active: "", layouts: {} }, "a storage that throws on getItem: an empty state");
assert.strictEqual(FdLayout.save(throwing, FdLayout.emptyState()), false, "a storage that throws on setItem: save returns false");
eq(FdLayout.load(null), { active: "", layouts: {} });
assert.strictEqual(FdLayout.save(null, FdLayout.emptyState()), false);
["", "{", "null", "[]", "5", '"x"', '{"layouts":5}', '{"layouts":[]}', '{"active":5,"layouts":null}'].forEach((raw) => {
  const s = mem(); s.m[FdLayout.STORAGE_KEY] = raw;
  eq(FdLayout.load(s), { active: "", layouts: {} }, "junk JSON -> empty state: " + raw);
});
{
  const s = mem(); s.m[FdLayout.STORAGE_KEY] = "x".repeat(300000);
  eq(FdLayout.load(s), { active: "", layouts: {} }, "an oversized value is not read");
}
{
  const s = mem();
  const r = FdLayout.put(FdLayout.emptyState(), "  a  ", { panels: [{ id: "map", span: 12 }] });
  assert.ok(r.ok && r.name === "a" && r.state.active === "a", "a name is trimmed and becomes the active one");
  assert.strictEqual(FdLayout.save(s, r.state), true);
  const back = FdLayout.load(s);
  eq(back, r.state, "a saved state loads the same");
  eq(Object.keys(back.layouts), ["a"]);
  eq(FdLayout.activeOf(back).panels[0], { id: "map", span: 12, hide: false });
  assert.strictEqual(FdLayout.activeOf(FdLayout.emptyState()), null, "no active layout is the default grid");
}
{
  // hostile stored state: a bad name, a bad layout, an unknown active, a __proto__ name, more than 20
  const s = mem();
  const layouts = JSON.parse('{"__proto__": {"panels": []}, "": {}, "ok": {"panels": [{"id": "zz"}, {"id": "map", "span": 8}]}, "bad\\u0000": {}, " pad": {}}');
  for (let i = 0; i < 40; i++) layouts["n" + i] = { panels: [] };
  s.m[FdLayout.STORAGE_KEY] = JSON.stringify({ active: "ghost", layouts });
  const st = FdLayout.load(s);
  assert.strictEqual(st.active, "", "an active name that is not kept is dropped");
  assert.strictEqual(Object.keys(st.layouts).length, 20, "at most 20 layouts are read");
  assert.ok(!("__proto__" in {}) || Object.getPrototypeOf(st.layouts) === Object.prototype, "no prototype tampering");
  assert.ok(!Object.prototype.hasOwnProperty.call(st.layouts, "__proto__") && !("" in st.layouts) && !(" pad" in st.layouts), "bad names are dropped");
  eq(st.layouts.ok.panels[0], { id: "map", span: 8, hide: false }, "a stored layout is normalised");
  assert.strictEqual(st.layouts.ok.panels.length, 15);
}
{
  // the 21st layout is refused; replacing an existing name is not a new layout
  let st = FdLayout.emptyState();
  for (let i = 0; i < 20; i++) { const r = FdLayout.put(st, "L" + i, null); assert.ok(r.ok, "layout " + i); st = r.state; }
  const over = FdLayout.put(st, "L20", null);
  assert.strictEqual(over.ok, false, "the 21st layout is refused");
  assert.strictEqual(over.why, "At most 20 layouts");
  assert.strictEqual(Object.keys(st.layouts).length, 20);
  assert.ok(FdLayout.put(st, "L3", { panels: [{ id: "map", span: 4 }] }).ok, "an existing name is replaced");
  const again = FdLayout.remove(st, "L0");
  assert.strictEqual(Object.keys(again.layouts).length, 19);
  assert.ok(FdLayout.put(again, "L20", null).ok, "after a delete there is room");
}
{
  // names
  const ok = (s) => FdLayout.put(FdLayout.emptyState(), s, null);
  assert.strictEqual(ok("  a  ").name, "a", '"  a  " -> "a"');
  assert.strictEqual(ok("x".repeat(40)).ok, true, "40 characters fit");
  assert.strictEqual(ok("x".repeat(41)).ok, false, "41 characters are refused");
  assert.strictEqual(ok("   ").ok, false, "empty after trim");
  assert.strictEqual(ok("").ok, false);
  assert.strictEqual(ok("a\nb").ok, false, "a control character");
  assert.strictEqual(ok("a\u0000b").ok, false);
  assert.strictEqual(ok("a\u007fb").ok, false);
  assert.strictEqual(ok("__proto__").ok, false, "the one name the map cannot hold");
  assert.strictEqual(ok(null).ok, false);
  assert.strictEqual(ok(5).ok, false);
  assert.strictEqual(ok("<img src=x onerror=1>").name, "<img src=x onerror=1>", "markup is a name like any other (the page escapes it)");
  assert.strictEqual(ok("constructor").ok, true, "an ordinary word that is also a property name is fine");
  assert.strictEqual(FdLayout.setActive(ok("a").state, "a").active, "a");
  assert.strictEqual(FdLayout.setActive(ok("a").state, "nope").active, "", "an unknown name is the default");
  assert.strictEqual(FdLayout.remove(ok("a").state, "a").active, "", "deleting the active layout clears it");
}


// ---- the last shown panel (L1): show() refuses to hide it, normalize keeps one shown, lastVisible names it
{
  const allHidden = { panels: FdLayout.PANELS.map((p) => ({ id: p.id, hide: true })) };
  const n = FdLayout.normalize(allHidden);
  assert.deepStrictEqual(n.panels.map((e) => e.hide), [false].concat(Array(14).fill(true)), "a layout that hides everything keeps its first panel shown");
  const dec = FdLayout.decode(FdLayout.PANELS.map((p) => p.code + "." + p.span + ".h").join("~"));
  assert.strictEqual(dec.panels.filter((e) => !e.hide).length, 1, "so does a link's");
  let l = FdLayout.defaults();
  FdLayout.PANELS.slice(1).forEach((p) => { l = FdLayout.show(l, p.id, false); });
  assert.strictEqual(FdLayout.lastVisible(l), "run");
  eq(FdLayout.show(l, "run", false), l, "hiding the last shown panel changes nothing");
  assert.strictEqual(FdLayout.show(FdLayout.show(l, "map", true), "run", false).panels[0].hide, true, "with another shown, the first can be hidden");
  assert.strictEqual(FdLayout.lastVisible(FdLayout.defaults()), null);
}

// ---- names (L5): bidi overrides and isolates, zero-width characters and separators are refused like control characters
["\u200b", "\u200c", "\u200d", "\u200e", "\u200f", "\u202a", "\u202b", "\u202c", "\u202d", "\u202e", "\u2066", "\u2067", "\u2068", "\u2069", "\u2028", "\u2029", "\u2060", "\ufeff"].forEach((c) => {
  assert.strictEqual(FdLayout.cleanName("ab" + c + "cd"), null, "refused: U+" + c.charCodeAt(0).toString(16));
  if (c.trim() !== "") assert.strictEqual(FdLayout.cleanName(c + "x"), null, "also at the start (whitespace ones are trimmed there, like any space)");
});
assert.strictEqual(FdLayout.cleanName("Raid \u00e5\u00f8 night 2"), "Raid \u00e5\u00f8 night 2", "ordinary text with letters outside ASCII is fine");
{
  const s = { m: {}, getItem(k) { return k in this.m ? this.m[k] : null; }, setItem(k, v) { this.m[k] = v; } };
  s.m[FdLayout.STORAGE_KEY] = JSON.stringify({ active: "", layouts: { ["ab\u202ecd"]: {}, ok: {} } });
  assert.deepStrictEqual(Object.keys(FdLayout.load(s).layouts), ["ok"], "a stored name with an override is dropped when read");
}

console.log("fdlayout_check.js ok");
