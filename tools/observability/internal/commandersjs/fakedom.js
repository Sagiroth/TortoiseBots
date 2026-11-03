"use strict";
// A minimal DOM for the checks that must run app.js's page code under node: elements, class lists, datasets, children,
// insertBefore/remove and the few selectors those functions use. No layout, no events, no HTML parsing: innerHTML is only
// remembered, and setting it empty clears the children (the page's way of emptying a box).
//
// getElementById walks the document tree like the browser's, so an element inside a removed card is no longer found: that is
// what the layer-panel check needs to see.

function simpleTest(sel) {
  const m = /^([a-zA-Z][\w-]*)?((?:[.#][\w-]+)*)$/.exec(sel);
  if (!m) throw new Error("fakedom: unsupported selector " + sel);
  const tag = m[1] ? m[1].toLowerCase() : null;
  const classes = Array.from(m[2].matchAll(/\.([\w-]+)/g), (x) => x[1]);
  const id = /#([\w-]+)/.exec(m[2]);
  return (el) => (!tag || el.tagName === tag) && (!id || el.id === id[1]) && classes.every((c) => el.classList.contains(c));
}

function Element(tag) {
  this.tagName = String(tag).toLowerCase();
  this.id = "";
  this.children = [];
  this.parentElement = null;
  this.dataset = {};
  this.style = {};
  this.attributes = {};
  this.textContent = "";
  this.hidden = false;
  this._classes = new Set();
  this._html = "";
  const set = this._classes;
  this.classList = {
    add: (...cs) => cs.forEach((c) => set.add(c)),
    remove: (...cs) => cs.forEach((c) => set.delete(c)),
    contains: (c) => set.has(c),
    toggle: (c, force) => {
      const on = force === undefined ? !set.has(c) : !!force;
      if (on) set.add(c); else set.delete(c);
      return on;
    },
  };
}

Object.defineProperties(Element.prototype, {
  className: {
    get() { return Array.from(this._classes).join(" "); },
    set(v) {
      this._classes.clear();
      String(v).split(/\s+/).filter(Boolean).forEach((c) => this._classes.add(c));
    },
  },
  firstChild: { get() { return this.children[0] || null; } },
  parentNode: { get() { return this.parentElement; } },
  innerHTML: {
    get() { return this._html; },
    set(v) {
      this._html = String(v);
      if (!v) {
        this.children.forEach((c) => { c.parentElement = null; });
        this.children = [];
      }
    },
  },
});

Element.prototype.appendChild = function (child) {
  if (child.parentElement) child.remove();
  child.parentElement = this;
  this.children.push(child);
  return child;
};

Element.prototype.insertBefore = function (child, ref) {
  if (child === ref) return child;
  if (child.parentElement) child.remove();
  child.parentElement = this;
  const i = ref ? this.children.indexOf(ref) : -1;
  if (i < 0) this.children.push(child); else this.children.splice(i, 0, child);
  return child;
};

Element.prototype.remove = function () {
  const p = this.parentElement;
  if (!p) return;
  p.children = p.children.filter((c) => c !== this);
  this.parentElement = null;
};

Element.prototype.setAttribute = function (k, v) { this.attributes[k] = String(v); };

Element.prototype.getAttribute = function (k) { return k in this.attributes ? this.attributes[k] : null; };

Element.prototype.contains = function (el) {
  for (let n = el; n; n = n.parentElement) if (n === this) return true;
  return false;
};

Element.prototype.querySelectorAll = function (sel) {
  const s = String(sel).trim();
  const scope = s.startsWith(":scope");
  let rest = scope ? s.slice(6).trim() : s;
  const direct = rest.startsWith(">");
  if (direct) rest = rest.slice(1).trim();
  const test = simpleTest(rest);
  const out = [];
  const walk = (el) => {
    if (test(el)) out.push(el);
    el.children.forEach(walk);
  };
  if (scope && direct) this.children.forEach((el) => { if (test(el)) out.push(el); });
  else this.children.forEach(walk);
  return out;
};

Element.prototype.querySelector = function (sel) {
  return this.querySelectorAll(sel)[0] || null;
};

function Document() {
  this.documentElement = new Element("html");
  this.body = new Element("body");
  this.documentElement.appendChild(this.body);
}

Document.prototype.createElement = function (tag) { return new Element(tag); };

Document.prototype.getElementById = function (id) {
  let hit = null;
  const walk = (el) => {
    if (hit) return;
    if (el.id === id) { hit = el; return; }
    el.children.forEach(walk);
  };
  walk(this.documentElement);
  return hit;
};

Document.prototype.querySelector = function (sel) { return this.documentElement.querySelector(sel); };

Document.prototype.querySelectorAll = function (sel) { return this.documentElement.querySelectorAll(sel); };

// createDocument returns the document plus the helpers a check needs to build the page: element(tag, id, className) creates
// a childless element, attach(parent, el) puts it in the page.
function createDocument() {
  const doc = new Document();
  const element = (tag, id, className) => {
    const el = doc.createElement(tag);
    if (id) el.id = id;
    if (className) el.className = className;
    return el;
  };
  return { document: doc, element, attach: (parent, el) => parent.appendChild(el) };
}

module.exports = { createDocument, Element, Document };
