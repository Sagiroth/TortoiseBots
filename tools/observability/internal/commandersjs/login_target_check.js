"use strict";
// Run by commandersjs_test.go under node: where login.html sends the browser after a login. The script's own dashboardTarget() is cut
// out of the page and run on hostile and plain fragments: the target is always /dashboard on this origin, with at most a #/ route
// (a route over 4096 characters becomes the #/?toolong=1 flag); nothing else of the fragment ever reaches the address.
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");
const html = fs.readFileSync(path.resolve(process.argv[2]), "utf8");
const m = /^ {4}function dashboardTarget\(h\) \{\n[\s\S]*?\n {4}\}\n/m.exec(html);
assert.ok(m, "dashboardTarget not found in login.html");
const ctx = vm.createContext({});
vm.runInContext(m[0], ctx);
const target = (h) => vm.runInContext("dashboardTarget(h)", Object.assign(ctx, { h }));
const BASE = "https://dash.test";
const check = (h, want, why) => {
  const t = target(h);
  assert.strictEqual(t, want, why + ": " + t);
  const u = new URL(t, BASE);
  assert.strictEqual(u.origin, BASE, "same origin: " + t);
  assert.strictEqual(u.pathname, "/dashboard", "still the dashboard: " + t);
  assert.ok(u.hash === "" || u.hash.startsWith("#/"), "no fragment but a route: " + t);
  assert.ok(!t.startsWith("//") && !/^[a-z][a-z0-9+.-]*:/i.test(t), "never a scheme or a protocol-relative target: " + t);
};
check("#/commanders/X", "/dashboard#/commanders/X", "a plain route is kept");
check("#/commanders/Tank/fight/live?lw=120", "/dashboard#/commanders/Tank/fight/live?lw=120", "a live link is kept");
check("#//evil.example", "/dashboard#//evil.example", "a protocol-relative fragment is only a fragment of /dashboard");
check("#javascript:alert(1)", "/dashboard", "not a route: dropped");
check("#/javascript:alert(1)", "/dashboard#/javascript:alert(1)", "a route that looks odd is still only a fragment");
check("#\\evil", "/dashboard", "a backslash after the hash is not a route");
check("#/\\evil.example", "/dashboard#/\\evil.example", "still a fragment");
check("", "/dashboard", "no fragment");
check("#", "/dashboard", "an empty fragment");
check("#/", "/dashboard#/", "the bare route");
check(undefined, "/dashboard", "no string");
check(null, "/dashboard", "null");
check({ startsWith: () => true, length: 5 }, "/dashboard", "an object is not a string");
check("#/" + "a".repeat(4094), "/dashboard#/" + "a".repeat(4094), "4096 characters is kept");
check("#/" + "a".repeat(4095), "/dashboard#/?toolong=1", "4097 characters becomes the flag");
check("#/" + "a".repeat(100000), "/dashboard#/?toolong=1", "a huge route becomes the flag, not the route");
check("#/?toolong=1", "/dashboard#/?toolong=1", "the flag passes through a second login");
assert.ok(/window\.location\.href = dashboardTarget\(window\.location\.hash\);/.test(html), "the redirect uses it, and nothing else builds the target");
assert.ok(!/location\.href = '\/dashboard'/.test(html), "no other redirect to the dashboard");
console.log("login_target_check.js ok");
