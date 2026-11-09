"use strict";
// FdLayout: the pure side of the Fight debugger's panel layouts. A layout is the order, the width and the visibility of the debugger's
// panels: { v: 1, panels: [{ id, span, hide }] } in display order, on a 12-column grid. This file knows the panels, normalises a layout
// (so nothing read from storage or from a link can name a panel that does not exist), writes it as the short code a share link carries,
// and keeps the viewer's named layouts in the browser's storage. No DOM, no fetch, no timers: unit-tested by internal/commandersjs/
// fdlayout_check.js under node. Everything that comes in from outside (storage, a link, a caller) is checked and bounded here.
(function (root) {
  // The panels, in the default order. `code` is the two letters a link uses; `span` is the default width in grid columns (of 12).
  const PANELS = [
    { id: "run", code: "ru", label: "Run timeline", span: 12 },
    { id: "frames", code: "fr", label: "Raid frames", span: 12 },
    { id: "controls", code: "co", label: "Run controls", span: 12 },
    { id: "bosses", code: "bo", label: "Boss abilities", span: 12 },
    { id: "rail", code: "ra", label: "Fights", span: 3 },
    { id: "map", code: "ma", label: "Map", span: 6 },
    { id: "meters", code: "me", label: "Meters", span: 6 },
    { id: "checks", code: "ch", label: "Checks", span: 6 },
    { id: "timeline", code: "tl", label: "Timeline", span: 6 },
    { id: "auras", code: "au", label: "Auras", span: 6 },
    { id: "events", code: "ev", label: "Events", span: 6 },
    { id: "live", code: "li", label: "Live events", span: 3 },
    { id: "roles", code: "ro", label: "Role tables", span: 3 },
    { id: "under", code: "un", label: "Underperforming bots", span: 12 },
    { id: "mech", code: "mc", label: "Boss mechanics", span: 12 },
  ];
  const SPANS = [3, 4, 6, 8, 12];
  const SPAN_LABELS = { 3: "Quarter", 4: "Third", 6: "Half", 8: "Two thirds", 12: "Full" };
  const STORAGE_KEY = "tbdash.fd.layouts.v1";
  const MAX_LAYOUTS = 20;
  const MAX_NAME = 40;
  const MAX_CODE = 400;      // a link's layout code is never longer than this (15 items of "xx.12.h~" are 120)
  const MAX_ITEMS = 60;      // items or panel entries looked at: a hostile list is cut here, not walked
  const MAX_STORED = 200000; // characters of stored JSON that are read at all

  const byId = {}, byCode = {};
  PANELS.forEach((p) => { byId[p.id] = p; byCode[p.code] = p; });
  const has = (o, k) => Object.prototype.hasOwnProperty.call(o, k);
  const pick = (map, k) => (typeof k === "string" && has(map, k) ? map[k] : null);

  function spanOk(n) {
    return typeof n === "number" && SPANS.indexOf(n) >= 0;
  }

  // Any value -> a layout of exactly the known panels, each once: unknown ids and repeats dropped, a span that is not allowed replaced by
  // the panel's default, a missing panel appended (default width, shown). The input is never changed and never trusted.
  function normalize(l) {
    const list = l && typeof l === "object" && Array.isArray(l.panels) ? l.panels : [];
    const seen = {}, out = [];
    for (let i = 0; i < list.length && i < MAX_ITEMS; i++) {
      const e = list[i];
      if (!e || typeof e !== "object") continue;
      const p = pick(byId, e.id);
      if (!p || has(seen, p.id)) continue;
      seen[p.id] = true;
      out.push({ id: p.id, span: spanOk(e.span) ? e.span : p.span, hide: e.hide === true });
    }
    PANELS.forEach((p) => { if (!has(seen, p.id)) out.push({ id: p.id, span: p.span, hide: false }); });
    // A layout that hides every panel would leave an empty page: the first panel stays shown.
    if (out.every((e) => e.hide)) out[0].hide = false;
    return { v: 1, panels: out };
  }

  function defaults() {
    return normalize(null);
  }

  // A layout -> "ma.8~tl.12~au.6.h" (code, span, ".h" when hidden), items joined by "~". The layout is normalised first.
  function encode(l) {
    return normalize(l).panels.map((e) => byId[e.id].code + "." + e.span + (e.hide ? ".h" : "")).join("~");
  }

  // The code -> a normalised layout, or null for a code that is not text, is empty or longer than MAX_CODE, has more than MAX_ITEMS
  // items or has an item that is not "xx.N" or "xx.N.h". A well-formed item with an unknown code or a span that is not allowed is
  // dropped or coerced by normalize, like any other layout.
  function decode(s) {
    if (typeof s !== "string" || s.length === 0 || s.length > MAX_CODE) return null;
    const items = s.split("~");
    if (items.length > MAX_ITEMS) return null;
    const panels = [];
    for (let i = 0; i < items.length; i++) {
      const m = /^([a-z]{2})\.(\d{1,2})(\.h)?$/.exec(items[i]);
      if (!m) return null;
      const p = pick(byCode, m[1]);
      if (p) panels.push({ id: p.id, span: Number(m[2]), hide: !!m[3] });
    }
    return normalize({ panels });
  }

  // The renderer's view: { id: { order, span, hide } } (order 0 is first).
  function apply(l) {
    const out = {};
    normalize(l).panels.forEach((e, i) => { out[e.id] = { order: i, span: e.span, hide: e.hide }; });
    return out;
  }

  // ---- the arrange dialog's edits: each returns a new normalised layout and leaves its input alone. An id that is not a panel, a move
  // past either end and a span that is not allowed change nothing.
  function move(l, id, dir) {
    const n = normalize(l), i = n.panels.findIndex((e) => e.id === id), j = i + (dir < 0 ? -1 : 1);
    if (i >= 0 && j >= 0 && j < n.panels.length) { const t = n.panels[i]; n.panels[i] = n.panels[j]; n.panels[j] = t; }
    return n;
  }

  function resize(l, id, span) {
    const n = normalize(l), e = n.panels.find((x) => x.id === id);
    if (e && spanOk(span)) e.span = span;
    return n;
  }

  // The last visible panel cannot be hidden (the page would be empty): that edit changes nothing.
  function show(l, id, shown) {
    const n = normalize(l), e = n.panels.find((x) => x.id === id);
    if (e && (shown || n.panels.some((x) => x !== e && !x.hide))) e.hide = !shown;
    return n;
  }

  // The id of the only visible panel, or null when two or more are shown (the dialog disables that one's Show box).
  function lastVisible(l) {
    const v = normalize(l).panels.filter((e) => !e.hide);
    return v.length === 1 ? v[0].id : null;
  }

  // ---- the viewer's named layouts: { active: "" | name, layouts: { name: layout } }, kept in the browser (a storage that throws, or
  // junk in it, only means the layouts are not there).
  // control characters, line separators, zero-width characters and the bidi overrides and isolates that make a name read as another one
  const CTRL = /[\u0000-\u001f\u007f-\u009f\u200b-\u200f\u2028-\u202e\u2060\u2066-\u2069\ufeff]/;

  // A layout name as the viewer typed it -> the trimmed name, or null when it is empty, longer than 40 characters, holds a control
  // character, or is the one name the layouts map cannot hold.
  function cleanName(s) {
    if (typeof s !== "string") return null;
    const n = s.trim();
    if (n.length < 1 || n.length > MAX_NAME || CTRL.test(n) || n === "__proto__") return null;
    return n;
  }

  function emptyState() {
    return { active: "", layouts: {} };
  }

  // Any value -> a state that holds only clean names (at most 20), normalised layouts and an active name that exists.
  function sanitize(s) {
    const out = emptyState();
    const src = s && typeof s === "object" && s.layouts && typeof s.layouts === "object" && !Array.isArray(s.layouts) ? s.layouts : null;
    if (src) {
      let n = 0;
      const names = Object.keys(src);
      for (let i = 0; i < names.length && n < MAX_LAYOUTS; i++) {
        const name = cleanName(names[i]);
        if (name === null || name !== names[i] || has(out.layouts, name)) continue;
        out.layouts[name] = normalize(src[names[i]]);
        n++;
      }
    }
    const a = s && typeof s === "object" ? cleanName(s.active) : null;
    out.active = a !== null && has(out.layouts, a) ? a : "";
    return out;
  }

  function load(storage) {
    try {
      if (!storage) return emptyState();
      const raw = storage.getItem(STORAGE_KEY);
      if (typeof raw !== "string" || raw.length === 0 || raw.length > MAX_STORED) return emptyState();
      return sanitize(JSON.parse(raw));
    } catch (e) {
      return emptyState();
    }
  }

  // Remember the state. True when it was stored; a storage that throws only means the layouts last until the page is closed.
  function save(storage, state) {
    try {
      if (!storage) return false;
      storage.setItem(STORAGE_KEY, JSON.stringify(sanitize(state)));
      return true;
    } catch (e) {
      return false;
    }
  }

  // Save `layout` under `name` and make it the active one. Pure: returns { ok, state, name } or { ok: false, why }. A new name is refused
  // when 20 layouts are already kept; an existing name is replaced.
  function put(state, name, layout) {
    const nm = cleanName(name);
    if (nm === null) return { ok: false, why: "A name is 1 to " + MAX_NAME + " characters, no control, zero-width or bidi-override characters" };
    const cur = sanitize(state);
    if (!has(cur.layouts, nm) && Object.keys(cur.layouts).length >= MAX_LAYOUTS) return { ok: false, why: "At most " + MAX_LAYOUTS + " layouts" };
    cur.layouts[nm] = normalize(layout);
    cur.active = nm;
    return { ok: true, state: cur, name: nm };
  }

  // The state without that layout (the active name is cleared when it was the one).
  function remove(state, name) {
    const cur = sanitize(state);
    if (typeof name === "string" && has(cur.layouts, name)) delete cur.layouts[name];
    if (!has(cur.layouts, cur.active)) cur.active = "";
    return cur;
  }

  // The state with `name` active ("" is the default layout); a name that is not kept makes it "".
  function setActive(state, name) {
    const cur = sanitize(state);
    cur.active = typeof name === "string" && has(cur.layouts, name) ? name : "";
    return cur;
  }

  // The layout the state calls active, or null (the default grid).
  function activeOf(state) {
    const cur = sanitize(state);
    return cur.active && has(cur.layouts, cur.active) ? cur.layouts[cur.active] : null;
  }

  const api = { PANELS, SPANS, SPAN_LABELS, STORAGE_KEY, MAX_LAYOUTS, MAX_NAME, MAX_CODE, normalize, defaults, encode, move, resize, show, lastVisible, decode, apply, cleanName, emptyState, sanitize, load, save, put, remove, setActive, activeOf };
  if (typeof module !== "undefined" && module.exports) module.exports = api; else root.FdLayout = api;
})(typeof self !== "undefined" ? self : this);
