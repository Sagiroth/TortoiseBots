"use strict";
// FdFilter: the Fight debugger's event filter. A filter is a tree of groups (all of / any of, each optionally negated) and
// conditions (field, operator, value, each optionally negated); it is matched against the events FightEvents.build lists and
// composes with the time range. The tree has a compact, URL-safe text form. No DOM, no fetch, no timers: unit-tested by
// internal/commandersjs/fdfilter_check.js under node.
//
//   Group = { op: "and" | "or", not: bool, items: [Cond | Group] }     Cond = { f, op, v, not: bool }
//
// A condition with no value yet (a freshly added row) is "incomplete": it is left out of the match, so adding a row does not blank
// the list; a group whose items are all incomplete matches everything, like an empty group.
(function (root) {
  const MAX_DEPTH = 3;       // the root group is depth 1
  const MAX_CONDS = 20;
  const MAX_VALUE = 80;      // characters of a text value
  const MAX_ENCODED = 2000;  // characters of the text form; a longer filter is left out of URLs
  const PREFIX = "1.";

  // The fields the data carries, and two it does not: the game server sends neither a spell school nor a hit type, so they are
  // listed (disabled in the builder) and never evaluated.
  const FIELDS = [
    { f: "kind", label: "Kind" },
    { f: "ability", label: "Ability" },
    { f: "source", label: "Source" },
    { f: "target", label: "Target" },
    { f: "srcRole", label: "Source role" },
    { f: "tgtRole", label: "Target role" },
    { f: "amount", label: "Amount" },
  ];
  const UNAVAILABLE = [
    { f: "school", label: "Spell school" },
    { f: "hit", label: "Hit type (crit, miss, resist)" },
  ];
  const UNAVAILABLE_TITLE = "The game server does not send this";
  const OPS = { is: "is", has: "has", ge: "at least", le: "at most" };
  const PROP = { kind: "k", ability: "ab", source: "src", target: "tgt", srcRole: "srcRole", tgtRole: "tgtRole", amount: "amt" };

  const isNum = (v) => typeof v === "number" && isFinite(v);
  const isGroup = (n) => !!n && typeof n === "object" && Array.isArray(n.items);

  function view() {
    return root.FdView || (typeof require === "function" ? require("./fdview.js") : null);
  }

  function opsFor(f) {
    return f === "amount" ? ["is", "ge", "le"] : ["is", "has"];
  }

  function empty() {
    return { op: "and", not: false, items: [] };
  }

  function clone(n) {
    return JSON.parse(JSON.stringify(n));
  }

  function countConds(g) {
    return g.items.reduce((n, it) => n + (isGroup(it) ? countConds(it) : 1), 0);
  }

  function depthOf(g) {
    return 1 + g.items.reduce((d, it) => Math.max(d, isGroup(it) ? depthOf(it) : 0), 0);
  }

  // {ok, why}: the tree's shape and limits. Never throws.
  function validate(g) {
    if (!isGroup(g)) return { ok: false, why: "The filter is not a group." };
    const bad = (why) => ({ ok: false, why });
    const walk = (n, depth) => {
      if (depth > MAX_DEPTH) return bad("Groups nest at most " + MAX_DEPTH + " deep.");
      if (n.op !== "and" && n.op !== "or") return bad("A group is all of or any of.");
      if (typeof n.not !== "boolean") return bad("A group's NOT is on or off.");
      for (const it of n.items) {
        if (!it || typeof it !== "object") return bad("A filter item is not a condition or a group.");
        if (isGroup(it)) {
          const r = walk(it, depth + 1);
          if (!r.ok) return r;
          continue;
        }
        if (UNAVAILABLE.some((u) => u.f === it.f)) return bad(UNAVAILABLE_TITLE + ": " + it.f + ".");
        if (!Object.prototype.hasOwnProperty.call(PROP, it.f)) return bad("Unknown field.");
        if (opsFor(it.f).indexOf(it.op) < 0) return bad("That operator does not fit the field.");
        if (typeof it.not !== "boolean") return bad("A condition's NOT is on or off.");
        if (!(isNum(it.v) || (typeof it.v === "string" && it.v.length <= MAX_VALUE))) return bad("A value is a number or text of at most " + MAX_VALUE + " characters.");
      }
      return { ok: true, why: "" };
    };
    const r = walk(g, 1);
    if (!r.ok) return r;
    if (countConds(g) > MAX_CONDS) return bad("A filter has at most " + MAX_CONDS + " conditions.");
    return { ok: true, why: "" };
  }

  // The numeric reading of a value (null when it is not a number).
  function numOf(v) {
    if (isNum(v)) return v;
    if (typeof v === "string" && v.trim() !== "" && isFinite(Number(v))) return Number(v);
    return null;
  }

  // A condition that does not constrain anything yet: no value, or an amount that is not a number.
  function incomplete(c) {
    if (c.v === "" || c.v == null) return true;
    if (c.f === "amount" && numOf(c.v) === null) return true;
    return false;
  }

  function matchCond(c, ev) {
    const val = ev[PROP[c.f]];
    let r = false;
    if (c.f === "amount") {
      const n = numOf(c.v);
      if (isNum(val) && n !== null) r = c.op === "is" ? val === n : c.op === "ge" ? val >= n : val <= n;
    } else {
      const want = String(c.v).toLowerCase();
      const have = typeof val === "string" ? val.toLowerCase() : "";
      if (c.op === "has") r = have.indexOf(want) >= 0;
      else {
        r = have === want;
        // An ability can also be named by its spell id.
        if (!r && c.f === "ability" && isNum(ev.sid)) { const n = numOf(c.v); r = n !== null && n === ev.sid; }
      }
    }
    return c.not ? !r : r;
  }

  // Whether the group has anything to constrain: an incomplete condition, or a group with nothing in it, is left out.
  function live(n) {
    return isGroup(n) ? n.items.some(live) : !incomplete(n);
  }

  function matchGroup(g, ev) {
    const items = g.items.filter(live);
    if (!items.length) return true;
    const test = (it) => (isGroup(it) ? matchGroup(it, ev) : matchCond(it, ev));
    const r = g.op === "or" ? items.some(test) : items.every(test);
    return g.not ? !r : r;
  }

  function match(g, ev) {
    return !!ev && matchGroup(g, ev);
  }

  function isEmpty(g) {
    return !isGroup(g) || !live(g);
  }

  // The events inside the time range ({from, to} fight ms, or null for all) that the filter matches, in their order.
  function apply(g, events, range) {
    const V = view();
    const list = Array.isArray(events) ? events : [];
    const all = isEmpty(g);
    return list.filter((ev) => V.inRange(ev.t, range || null) && (all || matchGroup(g, ev)));
  }

  // ---- editing: new trees, the input is never changed. A path is "0.2.1" (child 0, its child 2, its child 1); "" is the root.
  function parsePath(p) {
    if (Array.isArray(p)) return p.slice();
    const s = String(p == null ? "" : p);
    return s === "" ? [] : s.split(".").map((x) => Number(x));
  }

  function nodeAt(g, path) {
    let n = g;
    for (const i of path) {
      if (!isGroup(n) || !Number.isInteger(i) || i < 0 || i >= n.items.length) return null;
      n = n.items[i];
    }
    return n;
  }

  // The node at the path (null when there is none).
  function at(g, path) {
    return nodeAt(g, parsePath(path));
  }

  // The node at the path with the patch's fields set.
  function setAt(g, path, patch) {
    const p = parsePath(path), root2 = clone(g), n = nodeAt(root2, p);
    if (!n) return root2;
    Object.keys(patch || {}).forEach((k) => { n[k] = patch[k]; });
    return root2;
  }

  // The node appended to the items of the group at the path.
  function addAt(g, path, node) {
    const root2 = clone(g), n = nodeAt(root2, parsePath(path));
    if (isGroup(n)) n.items.push(clone(node));
    return root2;
  }

  // The node at the path taken out (the root cannot be removed).
  function removeAt(g, path) {
    const p = parsePath(path), root2 = clone(g);
    if (!p.length) return root2;
    const parent = nodeAt(root2, p.slice(0, -1)), i = p[p.length - 1];
    if (isGroup(parent) && Number.isInteger(i) && i >= 0 && i < parent.items.length) parent.items.splice(i, 1);
    return root2;
  }

  function newCond() {
    return { f: "kind", op: "is", v: "", not: false };
  }

  function newGroup() {
    return { op: "and", not: false, items: [] };
  }

  // ---- the text form: compact JSON (o/n/i for a group, f/p/v/n for a condition) as UTF-8, base64url without padding, "1." first.
  const B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

  function toBytes(s) {
    return Array.from(new TextEncoder().encode(s));
  }

  function b64(bytes) {
    let out = "";
    for (let i = 0; i < bytes.length; i += 3) {
      const a = bytes[i], b = bytes[i + 1], c = bytes[i + 2];
      out += B64[a >> 2] + B64[((a & 3) << 4) | ((b || 0) >> 4)];
      if (b !== undefined) out += B64[((b & 15) << 2) | ((c || 0) >> 6)];
      if (c !== undefined) out += B64[c & 63];
    }
    return out;
  }

  function unb64(s) {
    if (!/^[A-Za-z0-9_-]*$/.test(s) || s.length % 4 === 1) return null;
    const out = [];
    for (let i = 0; i < s.length; i += 4) {
      const v = [0, 1, 2, 3].map((k) => (i + k < s.length ? B64.indexOf(s[i + k]) : -1));
      out.push((v[0] << 2) | (v[1] >> 4));
      if (v[2] >= 0) out.push(((v[1] & 15) << 4) | (v[2] >> 2));
      if (v[3] >= 0) out.push(((v[2] & 3) << 6) | v[3]);
    }
    return out;
  }

  function pack(n) {
    if (isGroup(n)) {
      const o = { o: n.op === "or" ? "o" : "a", i: n.items.map(pack) };
      if (n.not) o.n = true;
      return o;
    }
    const o = { f: n.f, p: n.op, v: n.v };
    if (n.not) o.n = true;
    return o;
  }

  function unpack(o) {
    if (!o || typeof o !== "object") return null;
    if (Array.isArray(o.i)) {
      const items = o.i.map(unpack);
      if (items.some((x) => x === null)) return null;
      return { op: o.o === "o" ? "or" : "and", not: o.n === true, items };
    }
    return { f: o.f, op: o.p, v: o.v, not: o.n === true };
  }

  // The filter as text for a URL; null when it is invalid or the text would pass MAX_ENCODED characters (the caller then
  // leaves the filter out).
  function encode(g) {
    if (!validate(g).ok) return null;
    const s = PREFIX + b64(toBytes(JSON.stringify(pack(g))));
    return s.length > MAX_ENCODED ? null : s;
  }

  // The group of an encoded filter; null for a wrong prefix, bad base64, bad JSON or a tree that does not validate.
  function decode(s) {
    if (typeof s !== "string" || s.indexOf(PREFIX) !== 0 || s.length > MAX_ENCODED) return null;
    try {
      const bytes = unb64(s.slice(PREFIX.length));
      if (!bytes) return null;
      const g = unpack(JSON.parse(new TextDecoder("utf-8", { fatal: true }).decode(new Uint8Array(bytes))));
      return g && validate(g).ok ? g : null;
    } catch (e) {
      return null;
    }
  }

  // ---- presets
  const cond = (f, op, v) => ({ f, op, v, not: false });
  const PRESETS = [
    { id: "casts", label: "Boss casts", group: { op: "and", not: false, items: [cond("kind", "is", "cast")] } },
    { id: "deaths", label: "Deaths and last hits", group: { op: "or", not: false, items: [cond("kind", "is", "death"), cond("kind", "is", "hit")] } },
    { id: "tankauras", label: "Auras on tanks", group: { op: "and", not: false, items: [cond("kind", "is", "aura_on"), cond("tgtRole", "is", "tank")] } },
    { id: "notrash", label: "Without target changes", group: { op: "and", not: false, items: [{ f: "kind", op: "is", v: "target", not: true }] } },
  ];

  function preset(id) {
    const p = PRESETS.find((x) => x.id === id);
    return p ? clone(p.group) : null;
  }

  // ---- a short English line for the panel header
  function fieldWord(f) {
    const d = FIELDS.find((x) => x.f === f);
    return d ? d.label.toLowerCase() : String(f);
  }

  function describe(g) {
    if (!isGroup(g)) return "";
    const part = (n, top) => {
      if (isGroup(n)) {
        const items = n.items.filter(live).map((x) => part(x, false)).filter(Boolean);
        if (!items.length) return "";
        const joined = items.join(n.op === "or" ? " or " : " and ");
        if (n.not) return "not (" + joined + ")";
        return !top && items.length > 1 ? "(" + joined + ")" : joined;
      }
      return (n.not ? "not " : "") + fieldWord(n.f) + " " + (n.op === "ge" ? "is at least" : n.op === "le" ? "is at most" : n.op) + " " + n.v;
    };
    return part(g, true) || "every event";
  }

  const api = {
    MAX_DEPTH, MAX_CONDS, MAX_VALUE, MAX_ENCODED, FIELDS, UNAVAILABLE, UNAVAILABLE_TITLE, OPS, PRESETS,
    empty, clone, validate, match, apply, isEmpty, opsFor, at, setAt, addAt, removeAt, newCond, newGroup, parsePath, depthOf, countConds,
    encode, decode, preset, describe,
  };
  if (typeof module !== "undefined" && module.exports) module.exports = api; else root.FdFilter = api;
})(typeof self !== "undefined" ? self : this);
