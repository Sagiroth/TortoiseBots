--[==[
TortoiseBots Eluna test toolkit: `.tt` GM commands (TEST SERVERS ONLY).

Runs in the WORLD Lua state: the core calls PLAYER_EVENT_ON_COMMAND from
ChatHandler::ExecuteCommand for commands it does not know (core
src/game/Chat/Chat.cpp:1697-1713, `sWorld.GetEluna()`), on the world thread,
where the built-in GM commands (.npc, .die, .groupgo) also run. Returning false
marks the line handled.

Every reply is ONE line: `TTJSON {...}` sent to the caller with
Player:SendBroadcastMessage (a CHAT_MSG_SYSTEM line) and echoed to stdout
(docker logs). Fields always present: v, cmd, ok, id (echo of `#<id>`), ts.
Errors: ok=false, err=<code>, msg=<text>.

Commands (rank >= TT.CFG.min_gm_rank; anything else is refused):
  .tt help
  .tt q floor [x y [z]]                    floor (z-aware) and top surface
  .tt q units [r] [x y z] [all] [dead] [e=<entry>] [au=<id,id>]
  .tt q unit <0xGUID> [au=<id,id>]
  .tt q los <x> <y> <z> <0xGUID>...           sight from 2 yd above the point (1-8 guids)
  .tt q gos [r] [x y z] [e=<entry>]         doors/objects with GO state
  .tt q group                               the caller's group, floor under each
  .tt q inst                                map/instance; saved instance data*
  .tt q link <0xGUID>                       creature_linking group*  (*needs
                                            Eluna.UseUnsafeMethods = 1)
  .tt quiet <r> [x y z] [w=<s>] [dz=<yd>] [skip=<e,e>]
                                           owner's quiet rule, 2 reads w s apart; units more
                                           than dz (default 8, 0 = no limit) above/below the
                                           point and entries in skip do not count
  .tt clearto <map> <x> <y> <z> <r> [kill] [boss] [dz=<yd>] [skip=<e,e>] [all=1] [of=<name>]
                                           same dz/skip window; friendly NPCs are never
                                           touched unless all=1 (judged from a non-GM member
                                           of the caller's group, or of=<name>'s group)
  .tt place here|<x y z> [spread=<yd>] [force] [me]
  .tt reset [mapId]
Add `#<id>` anywhere to get it echoed back ("id") for request matching.
]==]

local TT = require("tt_lib")
local CFG = TT.CFG

local MAX_RANK = 4

-- ---------------------------------------------------------------------------
-- reply helpers
-- ---------------------------------------------------------------------------
local LIST_KEYS = { "units", "gos", "members", "near", "removed", "skipped", "roamers", "rows", "live", "los" }

local function fitLine(t)
    local line = "TTJSON " .. TT.json(t)
    local guard = 0
    while #line > CFG.max_line and guard < 200 do
        guard = guard + 1
        local bestKey, bestLen
        for _, k in ipairs(LIST_KEYS) do
            local l = t[k]
            if type(l) == "table" and #l > 0 and (not bestLen or #l > bestLen) then
                bestKey, bestLen = k, #l
            end
        end
        if not bestKey then break end
        table.remove(t[bestKey])
        t.trunc = true
        line = "TTJSON " .. TT.json(t)
    end
    return line
end

local function send(player, t)
    t.v = TT.VERSION
    t.ts = os.time()
    local line = fitLine(TT.obj(t))
    if player then
        player:SendBroadcastMessage(line)
    end
    TT.writeStdout(line)
end

local function fail(player, req, code, msg, extra)
    local t = extra or {}
    t.cmd = req.cmd
    t.id = req.id
    t.ok = false
    t.err = code
    t.msg = msg
    send(player, t)
    return false
end

local function okReply(player, req, t)
    t.cmd = req.cmd
    t.id = req.id
    if t.ok == nil then t.ok = true end
    send(player, t)
    return false
end

-- ---------------------------------------------------------------------------
-- parsing
-- ---------------------------------------------------------------------------
local function parse(text)
    local req = { pos = {}, kv = {}, flags = {} }
    local first = true
    for tok in tostring(text):gmatch("%S+") do
        if first then
            req.head = tok:lower()
            first = false
        elseif tok:sub(1, 1) == "#" and #tok > 1 then
            req.id = tok:sub(2, 41)
        elseif tok:find("=", 1, true) then
            local k, v = tok:match("^([%w_]+)=(.*)$")
            if k then req.kv[k:lower()] = v end
        else
            req.pos[#req.pos + 1] = tok
        end
    end
    for _, p in ipairs(req.pos) do
        req.flags[p:lower()] = true
    end
    return req
end

local function num(s)
    local n = tonumber(s)
    if n and n == n and n > -1e7 and n < 1e7 then return n end
    return nil
end

-- the first three numbers of req.pos starting at index i
local function point(req, i)
    local x, y, z = num(req.pos[i]), num(req.pos[i + 1]), num(req.pos[i + 2])
    if x and y and z then return x, y, z end
    return nil
end

local function idList(s)
    local out = {}
    if not s then return out end
    for id in tostring(s):gmatch("%d+") do
        if #out >= 16 then break end
        out[#out + 1] = tonumber(id)
    end
    return out
end

local function clampR(r, default)
    r = r or default
    if r < 1 then r = 1 end
    if r > CFG.max_radius then r = CFG.max_radius end
    return r
end

local function mapInfo(map)
    return {
        map = map:GetMapId(),
        inst = map:GetInstanceId(),
        dungeon = map:IsDungeon() and true or false,
    }
end

-- GetPlayerGUID(counter) is a plain uint64 that GetPlayerByGUID refuses in the
-- pinned Eluna: keep the player's own ObjectGuid (a value, not the object) by counter.
local PGUID = {}
local function playerCounter(p)
    local c = p:GetGUIDLow()
    PGUID[c] = p:GetGUID()
    return c
end

local function findPlayer(counter)
    if not counter then return nil end
    local g = PGUID[counter]
    if not g then return nil end
    return GetPlayerByGUID(g)
end

-- point given (x y z) must be near the caller and on the caller's map
local function checkPoint(player, x, y)
    local d = TT.dist2(player:GetX(), player:GetY(), x, y)
    if d > CFG.max_point_dist then
        return false, string.format("point is %.0f yd from you (max %d): .go xyz there first", d, CFG.max_point_dist)
    end
    return true
end

-- ---------------------------------------------------------------------------
-- q: queries
-- ---------------------------------------------------------------------------
local Q = {}

function Q.floor(player, req)
    local map = player:GetMap()
    local px, py, pz = player:GetX(), player:GetY(), player:GetZ()
    local x, y = num(req.pos[2]), num(req.pos[3])
    local zWanted = num(req.pos[4])
    if not x or not y then x, y = px, py end
    local d = TT.dist2(px, py, x, y)
    if d > CFG.place_anchor_max then
        return fail(player, req, "anchor_far",
            string.format("the floor is searched from your own z: stand within %d yd (now %.0f)", CFG.place_anchor_max, d))
    end
    local fx, fy, fz = TT.floorNear(player, x, y)
    local t = mapInfo(map)
    t.at = { TT.round(x), TT.round(y), zWanted or TT.NULL }
    t.caller = TT.pos(player)
    t.floor = fz and TT.round(fz) or TT.NULL
    t.floor_xy = fx and { TT.round(fx), TT.round(fy) } or TT.NULL
    -- top: Map:GetHeight(x, y) searches from MAX_HEIGHT (the roof, or null on
    -- an indoor map); floor: searched down from YOUR z (like .gps FloorZ)
    local top = TT.topZ(map, x, y)
    t.top = top and TT.round(top) or TT.NULL
    if fz then
        t.caller_above = TT.round(pz - fz)
        if zWanted then t.dz_wanted = TT.round(fz - zWanted) end
    end
    t.multi = (fz and top and math.abs(top - fz) > 3) and true or false
    return okReply(player, req, t)
end

function Q.units(player, req)
    local map = player:GetMap()
    local r = clampR(num(req.pos[2]), 40)
    local x, y, z = point(req, 3)
    if not x then x, y, z = player:GetX(), player:GetY(), player:GetZ() end
    local ok, msg = checkPoint(player, x, y)
    if not ok then return fail(player, req, "point_far", msg) end
    local hostile = req.flags["all"] and 0 or (req.flags["friendly"] and 2 or 1)
    local dead = req.flags["dead"] and 0 or 1
    local entry = num(req.kv.e)
    local au = idList(req.kv.au)
    local list = TT.creaturesAt(player, x, y, z, r, hostile, dead, entry)
    local units = {}
    for i = 1, math.min(#list, CFG.max_list) do
        units[#units + 1] = TT.unitSnap(list[i].c, x, y, z, au, map)
    end
    local t = mapInfo(map)
    t.at = { TT.round(x), TT.round(y), TT.round(z) }
    t.r = r
    t.filter = hostile == 1 and "hostile" or (hostile == 2 and "friendly" or "all")
    t.n = #list
    t.units = units
    if #list > CFG.max_list then t.trunc = true end
    return okReply(player, req, t)
end

function Q.unit(player, req)
    local map = player:GetMap()
    local hex = req.pos[2]
    local o, why = TT.findByHex(map, hex, player)
    if not o then return fail(player, req, why or "not_found", "no such unit on your map: " .. tostring(hex)) end
    local u = o:ToUnit()
    if not u then return fail(player, req, "not_a_unit", tostring(hex)) end
    local t = mapInfo(map)
    t.unit = TT.unitSnap(u, player:GetX(), player:GetY(), player:GetZ(), idList(req.kv.au), map)
    -- the reply carries the guid asked for: the client drops a reply whose
    -- unit.g differs (wrong-unit protection)
    t.asked = hex
    return okReply(player, req, t)
end

-- q los <x> <y> <z> <0xGUID>... (1-8 guids): line of sight from an eye 2 yd
-- above the point to each unit. An absent guid answers its own not_found
-- entry; a found one carries "in" (WorldObject:IsWithinLoS) and the 3D
-- distance from the point.
function Q.los(player, req)
    local map = player:GetMap()
    local x, y, z = point(req, 2)
    local first = 5
    local count = #req.pos - first + 1
    if not x or count < 1 then
        return fail(player, req, "usage", "q los <x> <y> <z> <0xGUID>... (1-8 guids)")
    end
    if count > 8 then
        return fail(player, req, "too_many", "q los takes 1-8 guids")
    end
    local ok, msg = checkPoint(player, x, y)
    if not ok then return fail(player, req, "point_far", msg) end
    local eye = z + 2.0
    local los = {}
    for i = first, #req.pos do
        local hex = req.pos[i]
        local o, why = TT.findByHex(map, hex, player)
        local u = o and o:ToUnit()
        if not u then
            los[#los + 1] = { g = hex, err = why or "not_a_unit" }
        else
            local seen = false
            local good, res = pcall(function() return u:IsWithinLoS(x, y, eye) end)
            if good then seen = res and true or false end
            los[#los + 1] = { g = TT.objHex(u) or hex, ["in"] = seen,
                d = TT.round(TT.dist3(x, y, z, u:GetX(), u:GetY(), u:GetZ()), 1) }
        end
    end
    local t = mapInfo(map)
    t.at = { TT.round(x), TT.round(y), TT.round(z) }
    t.n = #los
    t.los = los
    return okReply(player, req, t)
end

function Q.gos(player, req)
    local map = player:GetMap()
    local r = clampR(num(req.pos[2]), 30)
    local x, y, z = point(req, 3)
    if not x then x, y, z = player:GetX(), player:GetY(), player:GetZ() end
    local ok, msg = checkPoint(player, x, y)
    if not ok then return fail(player, req, "point_far", msg) end
    local list = TT.gameObjectsAt(player, x, y, z, r, num(req.kv.e))
    local gos = {}
    for i = 1, math.min(#list, CFG.max_list) do
        local go = list[i].o
        gos[#gos + 1] = {
            g = TT.objHex(go), e = go:GetEntry(), n = go:GetName(), p = TT.pos(go),
            d = TT.round(list[i].d, 1),
            st = go:GetGoState(),          -- 0 active/open, 1 ready/closed, 2 alternative
            ls = go:GetLootState(),
            sp = go:IsSpawned() and true or false,
            act = go:IsActive() and true or false,
            db = go:GetDBTableGUIDLow(),
        }
    end
    local t = mapInfo(map)
    t.at = { TT.round(x), TT.round(y), TT.round(z) }
    t.r = r
    t.n = #list
    t.gos = gos
    return okReply(player, req, t)
end

function Q.group(player, req)
    local members = TT.groupMembers(player, req.kv.of)
    local out = {}
    local myMap, myInst = player:GetMapId(), player:GetInstanceId()
    for i = 1, #members do
        local m = members[i]
        local s = {
            g = TT.objHex(m), n = m:GetName(), map = m:GetMapId(), inst = m:GetInstanceId(),
            p = TT.pos(m), hp = TT.round(m:GetHealthPct(), 1),
            alive = m:IsAlive() and true or false, cb = m:IsInCombat() and true or false,
            gm = (m:GetGMRank() >= CFG.min_gm_rank) and true or false,
        }
        if m:GetMapId() == myMap and m:GetInstanceId() == myInst then
            s.d = TT.round(TT.dist2(player:GetX(), player:GetY(), m:GetX(), m:GetY()), 1)
            local _, _, fz = TT.floorNear(m, m:GetX(), m:GetY())
            if fz then s.above_floor = TT.round(m:GetZ() - fz) end
        end
        out[#out + 1] = s
    end
    local t = mapInfo(player:GetMap())
    t.members = out
    return okReply(player, req, t)
end

-- DB queries are METHOD_FLAG_UNSAFE in the pinned Eluna (GlobalMethods.h:
-- WorldDBQuery/CharDBQuery); with Eluna.UseUnsafeMethods = 0 they raise.
local function dbQuery(fnName, sql)
    local f = rawget(_G, fnName)
    if not f then return nil, "no_" .. fnName end
    local ok, res = pcall(f, sql)
    if not ok then
        if tostring(res):find("unsafe", 1, true) then return nil, "unsafe_methods_off" end
        return nil, tostring(res)
    end
    return res
end

function Q.inst(player, req)
    local map = player:GetMap()
    local t = mapInfo(map)
    t.players = map:GetPlayerCount()
    t.raid = map:IsRaid() and true or false
    -- Map:GetInstanceData only returns Lua (ElunaInstanceAI) data; this core's
    -- instance scripts are C++, so the live encounter state is not readable.
    -- What is readable: the last SAVED instance data string.
    t.live_state = "not_exposed"
    if t.inst and t.inst > 0 then
        local q, err = dbQuery("CharDBQuery",
            string.format("SELECT data FROM instance WHERE id = %d", t.inst))
        if q then
            t.saved = q:GetString(0)
        elseif err then
            t.saved = TT.NULL
            t.saved_err = err
        else
            t.saved = TT.NULL
        end
    end
    return okReply(player, req, t)
end

function Q.link(player, req)
    local map = player:GetMap()
    local hex = req.pos[2]
    local db = num(req.kv.db)
    if not db then
        local o, why = TT.findByHex(map, hex, player)
        if not o or not o:ToCreature() then return fail(player, req, why or "not_found", tostring(hex)) end
        db = o:ToCreature():GetDBTableGUIDLow()
    end
    if not db or db == 0 then return fail(player, req, "no_db_guid", "temporary summon: no DB guid") end
    db = math.floor(db)
    local q, err = dbQuery("WorldDBQuery",
        string.format("SELECT master_guid, flag FROM creature_linking WHERE guid = %d", db))
    if err then return fail(player, req, err, "creature_linking needs Eluna.UseUnsafeMethods = 1") end
    local master = db
    local myFlag
    if q then
        master = math.floor(tonumber(q:GetString(0)) or db)
        myFlag = tonumber(q:GetString(1))
    end
    local rows = {}
    local q2 = dbQuery("WorldDBQuery", string.format(
        "SELECT l.guid, l.flag, c.id, c.id2, c.id3 FROM creature_linking l LEFT JOIN creature c ON c.guid = l.guid " ..
        "WHERE l.master_guid = %d LIMIT 40", master))
    if q2 then
        repeat
            local g = math.floor(tonumber(q2:GetString(0)) or 0)
            rows[#rows + 1] = { db = g, flag = tonumber(q2:GetString(1)),
                ids = { tonumber(q2:GetString(2)) or 0, tonumber(q2:GetString(3)) or 0, tonumber(q2:GetString(4)) or 0 } }
        until not q2:NextRow()
    end
    -- master's entries
    local q3 = dbQuery("WorldDBQuery", string.format("SELECT id, id2, id3 FROM creature WHERE guid = %d", master))
    local masterIds = {}
    if q3 then masterIds = { tonumber(q3:GetString(0)) or 0, tonumber(q3:GetString(1)) or 0, tonumber(q3:GetString(2)) or 0 } end
    -- live state of each, looked up by guid (no scan)
    local live = {}
    local function liveOf(dbg, entries)
        for _, e in ipairs(entries) do
            if e and e > 0 then
                local o = TT.findCreatureByDb(player, dbg, { e })
                if o and o:ToUnit() then
                    live[#live + 1] = TT.unitSnap(o:ToUnit(), player:GetX(), player:GetY(), player:GetZ(), nil, map)
                    return
                end
            end
        end
        live[#live + 1] = { db = dbg, absent = true }
    end
    liveOf(master, masterIds)
    for _, r in ipairs(rows) do liveOf(r.db, r.ids) end
    local t = mapInfo(map)
    t.asked_db = db
    t.master = master
    t.flag = myFlag or TT.NULL
    t.rows = rows
    t.live = live
    return okReply(player, req, t)
end

-- dz=<yd> (default CFG.default_dz = 8; 0 = no vertical limit, the old 2D rule) and
-- skip=<entry,entry,...>: shared by quiet and clearto.
local function windowOf(req)
    local dz = num(req.kv.dz)
    if dz == nil then dz = CFG.default_dz end
    if dz < 0 then dz = -dz end
    local skip, skiplist = {}, {}
    for _, e in ipairs(idList(req.kv.skip)) do
        skip[e] = true
        skiplist[#skiplist + 1] = e
    end
    return { dz = dz, skip = skip, skiplist = skiplist }
end

-- ---------------------------------------------------------------------------
-- quiet: the owner's quiet rule (TARGETED-TEST-METHOD.md step 3b)
--   no hostile alive within r (2D, within dz yd of the point's height; dz=0: any height;
--   entries in skip= never count), and no hostile within
--   TT.CFG.quiet_roam_r that moved between two reads w s apart.
-- ---------------------------------------------------------------------------
-- window = { dz = <yd or 0>, skip = { [entry] = true } } (see windowOf)
local function quietRead(player, x, y, z, r, window)
    local scope = math.max(r, CFG.quiet_roam_r)
    local list = TT.windowFilter(TT.creaturesAt(player, x, y, z, scope, 1, 1), z, window.dz, window.skip)
    local near, seen = {}, {}
    for i = 1, #list do
        local c, d = list[i].c, list[i].d
        local hex = TT.objHex(c)
        if hex then seen[hex] = { c:GetX(), c:GetY(), c:GetEntry(), d } end
        if d <= r then near[#near + 1] = TT.unitSnap(c, x, y, z) end
    end
    return near, seen, #list
end

local function cmdQuiet(player, req)
    local r = clampR(num(req.pos[1]), 50)
    local x, y, z = point(req, 2)
    if not x then x, y, z = player:GetX(), player:GetY(), player:GetZ() end
    local ok, msg = checkPoint(player, x, y)
    if not ok then return fail(player, req, "point_far", msg) end
    local window = windowOf(req)
    local w = num(req.kv.w) or 20
    if w < 0 then w = 0 end
    if w > CFG.quiet_wait_max_s then w = CFG.quiet_wait_max_s end
    local near1, seen1, scopeN = quietRead(player, x, y, z, r, window)
    local mapId, inst = player:GetMapId(), player:GetInstanceId()
    local t = { map = mapId, inst = inst, at = { TT.round(x), TT.round(y), TT.round(z) }, r = r,
        read = 1, final = (w == 0), quiet_now = (#near1 == 0), n_near = #near1, n_scope = scopeN, near = near1,
        dz = window.dz, skip = window.skiplist }
    if w == 0 then t.quiet = (#near1 == 0) end
    okReply(player, req, t)
    if w == 0 then return false end
    local counter = playerCounter(player)
    CreateLuaEvent(function()
        TT.guard("quiet.read2", function()
            local p = findPlayer(counter)
            if not p or p:GetMapId() ~= mapId or p:GetInstanceId() ~= inst then
                TT.writeStdout("TTJSON " .. TT.json(TT.obj({ v = TT.VERSION, cmd = req.cmd, id = req.id,
                    ok = false, err = "caller_gone", read = 2, final = true })))
                return
            end
            local near2, seen2 = quietRead(p, x, y, z, r, window)
            local roamers = {}
            for hex, a in pairs(seen2) do
                local b = seen1[hex]
                if not b then
                    roamers[#roamers + 1] = { g = hex, e = a[3], why = "arrived", d = TT.round(a[4], 1) }
                else
                    local moved = TT.dist2(a[1], a[2], b[1], b[2])
                    if moved > CFG.quiet_roam_moved then
                        roamers[#roamers + 1] = { g = hex, e = a[3], why = "moved", moved = TT.round(moved, 1), d = TT.round(a[4], 1) }
                    end
                end
            end
            local quiet = (#near1 == 0) and (#near2 == 0) and (#roamers == 0)
            okReply(p, req, { map = mapId, inst = inst, at = { TT.round(x), TT.round(y), TT.round(z) }, r = r,
                read = 2, final = true, wait_s = w, quiet = quiet, n_near = #near2, near = near2, roamers = roamers,
                dz = window.dz, skip = window.skiplist })
        end)
    end, math.floor(w * 1000), 1)
    return false
end

-- ---------------------------------------------------------------------------
-- clearto <map> <x> <y> <z> <r> [kill] [boss]
-- Default: Creature:DespawnOrUnsummon(0) = ForcedDespawn (Creature.cpp:2155):
-- no death event, no loot, no creature_linking trigger; the creature comes back
-- after its respawn delay or with a fresh instance. `kill`: Unit:Kill (the
-- caller deals the damage: death scripts and links DO fire).
-- Bosses (rank 3 / world boss) and owned units (pets, guardians) are skipped
-- unless `boss` is given (owned units are always skipped).
-- dz=<yd> (default 8; 0 = any height) and skip=<entry,...> leave units untouched like
-- quiet does. FRIENDLY NPCs (not hostile to a non-GM member of the caller's group, or of
-- `of=<name>`'s group) are never touched unless `all=1`; without such a member the
-- command refuses (no_reference) rather than guess.
-- ---------------------------------------------------------------------------
local function flagOn(req, name)
    local v = req.kv[name]
    return req.flags[name] or (v ~= nil and v ~= "" and v ~= "0" and v:lower() ~= "false")
end

-- Set of guid hexes hostile to a non-GM group member (rank below min_gm_rank AND not in GM mode:
-- TT.creaturesAt treats any IsGM() reference as hostile to everything). (A GM caller sees everything as
-- friendly, and Eluna has no IsHostileTo). nil, code, msg when no usable member.
local function hostileSetOf(player, of, x, y, z, r)
    local mapId, inst = player:GetMapId(), player:GetInstanceId()
    local members = TT.groupMembers(player, of)
    for i = 1, #members do
        local m = members[i]
        if m:GetMapId() == mapId and m:GetInstanceId() == inst and m:IsAlive()
            and m:GetGMRank() < CFG.min_gm_rank and not (m.IsGM and m:IsGM()) then
            if TT.dist3(m:GetX(), m:GetY(), m:GetZ(), x, y, z) + r + 30 > CFG.max_search then
                return nil, "reference_far", string.format("%s is too far from the point to judge friendliness", m:GetName())
            end
            local set = {}
            for _, e in ipairs(TT.creaturesAt(m, x, y, z, r, 1, 1)) do
                local hex = TT.objHex(e.c)
                if hex then set[hex] = true end
            end
            return set
        end
    end
    return nil, "no_reference", "no non-GM group member on this map to judge friendliness: pass of=<lead bot> or all=1"
end

local function cmdClearTo(player, req)
    local mapArg = num(req.pos[1])
    local x, y, z = point(req, 2)
    local r = num(req.pos[5])
    if not mapArg or not x or not r then
        return fail(player, req, "usage", ".tt clearto <map> <x> <y> <z> <radius> [kill] [boss]")
    end
    if mapArg ~= player:GetMapId() then
        return fail(player, req, "wrong_map", string.format("you are on map %d, not %d", player:GetMapId(), mapArg))
    end
    r = clampR(r, r)
    local ok, msg = checkPoint(player, x, y)
    if not ok then return fail(player, req, "point_far", msg) end
    local kill = req.flags["kill"] and true or false
    local bosses = req.flags["boss"] and true or false
    local window = windowOf(req)
    local everything = flagOn(req, "all") and true or false
    local hostileSet
    if not everything then
        local set, code, emsg = hostileSetOf(player, req.kv.of, x, y, z, r)
        if not set then return fail(player, req, code, emsg) end
        hostileSet = set
    end
    local list = TT.creaturesAt(player, x, y, z, r, 1, 1)
    local removed, skipped = {}, {}
    for i = 1, #list do
        local c, d = list[i].c, list[i].d
        local hex = TT.objHex(c)
        local item = { g = hex, e = c:GetEntry(), n = c:GetName(), d = TT.round(d, 1) }
        if window.skip[item.e] then
            item.why = "skip"; skipped[#skipped + 1] = item
        elseif window.dz > 0 and math.abs(c:GetZ() - z) > window.dz then
            item.why = "dz"; item.dz = TT.round(c:GetZ() - z, 1); skipped[#skipped + 1] = item
        elseif hostileSet and not (hex and hostileSet[hex]) then
            item.why = "friendly"; skipped[#skipped + 1] = item
        elseif c:GetOwner() then
            item.why = "owned"; skipped[#skipped + 1] = item
        elseif (c:GetRank() >= 3 or c:IsWorldBoss()) and not bosses then
            item.why = "boss"; skipped[#skipped + 1] = item
        elseif #removed >= CFG.max_clear then
            item.why = "cap"; skipped[#skipped + 1] = item
        else
            if kill then player:Kill(c, false) else c:DespawnOrUnsummon(0) end
            removed[#removed + 1] = item
        end
    end
    return okReply(player, req, { map = mapArg, inst = player:GetInstanceId(), at = { x, y, z }, r = r,
        mode = kill and "kill" or "despawn", n = #removed, removed = removed, skipped = skipped,
        dz = window.dz, skip = window.skiplist, all = everything })
end

-- ---------------------------------------------------------------------------
-- place here|<x y z> [spread=N] [force] [me]
-- Moves the caller's group members (same map + instance, alive) onto the floor
-- at a point in ONE burst, each on its own ring spot, facing the centre.
-- The floor is found from the CALLER (see TT.floorNear): stand at the point
-- (.go xyz X Y Z+2) first or within place_anchor_max yd at about its height.
-- Refuses when a member is in combat (unless `force`), like .bot summon.
-- A second reply (read=2) after place_verify_ms checks where everyone landed.
-- Note: NearTeleport is not .bot summon: no follow restore. Send
-- `.bot action stay` yourself (TARGETED-TEST-METHOD.md: STAY BEFORE SUMMON).
-- ---------------------------------------------------------------------------
local function cmdPlace(player, req)
    local map = player:GetMap()
    local px, py, pz = player:GetX(), player:GetY(), player:GetZ()
    local here = (req.pos[1] == nil) or (req.pos[1]:lower() == "here")
    local ax, ay, fz
    if here then
        local _, _, f = TT.floorNear(player, px, py)
        if not f then return fail(player, req, "no_floor", "no floor found under you (under the terrain?)") end
        local drop = pz - f
        if drop > CFG.place_max_drop then
            return fail(player, req, "caller_high", string.format("you are %.1f yd above the floor found", drop),
                { floor = TT.round(f), caller = TT.pos(player) })
        end
        ax, ay, fz = px, py, f
    else
        local x, y, z = point(req, 1)
        if not x then return fail(player, req, "usage", ".tt place here | .tt place <x> <y> <z>") end
        local d = TT.dist2(px, py, x, y)
        if d > CFG.place_anchor_max then
            return fail(player, req, "anchor_far", string.format(
                "stand within %d yd of the point (now %.0f): the floor is searched from your z", CFG.place_anchor_max, d))
        end
        if pz < z - 1.5 or pz > z + CFG.place_anchor_dz then
            return fail(player, req, "anchor_dz", string.format("your z %.1f must be in [%.1f, %.1f]", pz, z - 1.5, z + CFG.place_anchor_dz))
        end
        local _, _, f = TT.floorNear(player, x, y)
        if not f then return fail(player, req, "no_floor", "no floor found at the point") end
        if math.abs(f - z) > CFG.place_floor_tol then
            return fail(player, req, "floor_mismatch", string.format("floor %.2f vs z %.2f: another level?", f, z),
                { floor = TT.round(f), top = TT.topZ(map, x, y) or TT.NULL })
        end
        ax, ay, fz = x, y, f
    end
    -- a floor ABOVE the caller means the caller stands under the terrain or
    -- under another level: the r10 trap (TARGETED-TEST-METHOD.md ROOT CAUSE)
    if fz - pz > 2.5 then
        return fail(player, req, "caller_under_floor", string.format(
            "the floor found (%.2f) is above you (%.2f): you are under the ground or a level; .go xyz to the floor first", fz, pz),
            { floor = TT.round(fz), caller = TT.pos(player) })
    end

    local spread = num(req.kv.spread) or CFG.place_spread
    if spread < 0 then spread = 0 end
    if spread > 8 then spread = 8 end
    local includeMe = req.flags["me"] and true or false
    local force = req.flags["force"] and true or false
    local mapId, inst = player:GetMapId(), player:GetInstanceId()
    local myCounter = playerCounter(player)

    local movers, skipped, fighting = {}, {}, {}
    local members = TT.groupMembers(player, req.kv.of)
    for i = 1, #members do
        local m = members[i]
        local isMe = (m:GetGUIDLow() == myCounter)
        if isMe and not includeMe then
            skipped[#skipped + 1] = { n = m:GetName(), why = "caller" } -- add `me` to move yourself too
        elseif m:GetMapId() ~= mapId or m:GetInstanceId() ~= inst then
            skipped[#skipped + 1] = { n = m:GetName(), why = "other_map" }
        elseif not m:IsAlive() then
            skipped[#skipped + 1] = { n = m:GetName(), why = "dead" }
        else
            if m:IsInCombat() then fighting[#fighting + 1] = m:GetName() end
            movers[#movers + 1] = m
        end
    end
    if #movers == 0 then return fail(player, req, "no_members", "no group member to move", { skipped = skipped }) end
    if #fighting > 0 and not force then
        return fail(player, req, "in_combat", "members in combat (add `force` to move anyway)", { fighting = fighting })
    end

    local plan, out = {}, {}
    local n = #movers
    for i = 1, n do
        local m = movers[i]
        local ang = 2 * math.pi * (i - 1) / n
        local tx, ty = ax + spread * math.cos(ang), ay + spread * math.sin(ang)
        local sx, sy, sz = tx, ty, nil
        if spread > 0 then
            local fx, fy, f = TT.floorNear(player, tx, ty)
            if f and math.abs(f - fz) <= 2 then sx, sy, sz = fx, fy, f end
        end
        if not sz then sx, sy, sz = ax, ay, fz end -- fall back to the centre (like .bot summon's 4 yd rule)
        local o = TT.atan2(ay - sy, ax - sx)
        local from = TT.pos(m)
        m:NearTeleport(sx, sy, sz + 0.1, o)
        plan[#plan + 1] = { c = playerCounter(m), x = sx, y = sy, z = sz, n = m:GetName() }
        out[#out + 1] = { g = TT.objHex(m), n = m:GetName(), from = from,
            to = { TT.round(sx), TT.round(sy), TT.round(sz) }, cb = m:IsInCombat() and true or false }
    end
    okReply(player, req, { map = mapId, inst = inst, read = 1, final = false,
        center = { TT.round(ax), TT.round(ay), TT.round(fz) }, floor = TT.round(fz),
        top = TT.topZ(map, ax, ay) or TT.NULL, spread = spread, members = out, skipped = skipped })

    CreateLuaEvent(function()
        TT.guard("place.verify", function()
            local res, allOk = {}, true
            for _, pl in ipairs(plan) do
                local m = findPlayer(pl.c)
                if not m then
                    res[#res + 1] = { n = pl.n, ok = false, why = "gone" }; allOk = false
                else
                    local dz = m:GetZ() - pl.z
                    local d = TT.dist2(m:GetX(), m:GetY(), pl.x, pl.y)
                    local good = m:GetMapId() == mapId and math.abs(dz) <= 2 and d <= 5 and m:IsAlive()
                    if not good then allOk = false end
                    res[#res + 1] = { g = TT.objHex(m), n = pl.n, ok = good and true or false, p = TT.pos(m),
                        dz = TT.round(dz), d = TT.round(d, 1), hp = TT.round(m:GetHealthPct(), 1),
                        cb = m:IsInCombat() and true or false, alive = m:IsAlive() and true or false }
                end
            end
            local caller = findPlayer(myCounter)
            okReply(caller, req, { map = mapId, inst = inst, read = 2, final = true, placed = allOk, members = res })
        end)
    end, CFG.place_verify_ms, 1)
    return false
end

-- ---------------------------------------------------------------------------
-- reset [mapId]: Player:UnbindInstance(mapId) for the caller and every online
-- group member (PlayerMethods.h UnbindInstance -> Player::UnbindInstance,
-- the same as `.instance unbind <map>` per player). Refused while any of them
-- is inside that map. GROUP binds (Group::UnbindInstance, Group.h:370) are not
-- exposed by the pinned Eluna: a group that holds a group bind stays bound;
-- leave the group (or reset with the leader) for a truly fresh instance.
-- ---------------------------------------------------------------------------
local function cmdReset(player, req)
    local mapId = num(req.pos[1])
    if not mapId then
        return fail(player, req, "usage", ".tt reset <mapId> (run it from OUTSIDE the instance)")
    end
    mapId = math.floor(mapId)
    if mapId <= 1 then return fail(player, req, "not_instance", "maps 0/1 have no binds") end
    local list = TT.groupMembers(player, req.kv.of)
    local people, seen = {}, {}
    local function add(p)
        local c = p:GetGUIDLow()
        if not seen[c] then seen[c] = true; people[#people + 1] = p end
    end
    add(player)
    for i = 1, #list do add(list[i]) end
    local inside = {}
    for _, p in ipairs(people) do
        if p:GetMapId() == mapId then inside[#inside + 1] = p:GetName() end
    end
    if #inside > 0 then
        return fail(player, req, "inside", "these are still inside map " .. mapId, { inside = inside })
    end
    local done = {}
    for _, p in ipairs(people) do
        p:UnbindInstance(mapId)
        done[#done + 1] = p:GetName()
    end
    return okReply(player, req, { map = mapId, unbound = done, group = player:GetGroup() and true or false,
        group_bind = "not_reachable_in_eluna" })
end

-- ---------------------------------------------------------------------------
-- dispatch
-- ---------------------------------------------------------------------------
local HELP = "tt: q floor|units|unit|los|gos|group|inst|link, quiet, clearto, place, reset; quiet/clearto take dz=<yd> (default 8, 0=any) and skip=<entry,..>; clearto never touches friendly NPCs unless all=1 (of=<lead> picks the group); `#id` echoes; see README"

local function dispatch(player, req)
    local sub = (req.pos[1] or "help"):lower()
    if sub == "help" then
        return okReply(player, { cmd = "help", id = req.id }, { help = HELP })
    end
    -- console (player nil): only help; everything else needs a caller in the world
    if not player then
        TT.writeStdout("TTJSON " .. TT.json(TT.obj({ v = TT.VERSION, cmd = sub, ok = false, err = "needs_player" })))
        return false
    end
    if not player:IsInWorld() then return fail(player, { cmd = sub, id = req.id }, "not_in_world", "") end
    -- shift positional args so each handler sees its own args at pos[1]
    table.remove(req.pos, 1)
    if sub == "q" then
        local what = (req.pos[1] or ""):lower()
        req.cmd = "q." .. what
        local h = Q[what]
        if not h then return fail(player, req, "usage", "q floor|units|unit|los|gos|group|inst|link") end
        return h(player, req)
    end
    req.cmd = sub
    if sub == "quiet" then return cmdQuiet(player, req) end
    if sub == "clearto" then return cmdClearTo(player, req) end
    if sub == "place" then return cmdPlace(player, req) end
    if sub == "reset" then return cmdReset(player, req) end
    return fail(player, req, "usage", HELP)
end

local function onCommand(event, player, text)
    local req = parse(text)
    if req.head ~= "tt" then
        return true -- not ours: the core goes on ("There is no such command")
    end
    if player then
        local rank = player:GetGMRank()
        if rank < CFG.min_gm_rank or rank > MAX_RANK then
            -- refuse; mark handled so nothing else answers
            player:SendBroadcastMessage("TTJSON " .. TT.json(TT.obj({ v = TT.VERSION, cmd = "tt", id = req.id,
                ok = false, err = "not_allowed", msg = "GM rank " .. CFG.min_gm_rank .. "+ only" })))
            return false
        end
    end
    local ok, res = pcall(dispatch, player, req)
    if not ok then
        TT.log("tt command error: " .. tostring(res))
        if player then
            player:SendBroadcastMessage("TTJSON " .. TT.json(TT.obj({ v = TT.VERSION, cmd = req.cmd or "tt",
                id = req.id, ok = false, err = "lua_error", msg = tostring(res):sub(1, 300) })))
        end
    end
    return false
end

RegisterPlayerEvent(TT.EV.PLAYER_ON_COMMAND, onCommand)
