-- Mock test (plain Lua, no server): `.tt q los`, the sight read of the plan
-- scout. Run from the toolkit dir:  lua tests/mock_los_test.lua
--
-- Pins the answer's shape (g / in / d), the 1-8 guid limit, a per-guid
-- not_found and bad_guid, and the eye height: IsWithinLoS must be asked from
-- 2 yd above the stand (the plan scout's line of sight depends on it).
package.path = "lua_scripts/tt/?.lua;lua_scripts/tt/?.ext;" .. package.path
local handler
_G.RegisterPlayerEvent = function(_, fn) handler = fn end
_G.CreateLuaEvent = function(fn) fn() end
_G.RegisterServerEvent = function() end
io.write = function() end -- the stdout echo of every reply

local world = {}
local function creature(entry, counter, x, y, z, sees)
    local c = { entry = entry, counter = counter, x = x, y = y, z = z, sees = sees }
    c.GetX = function() return c.x end
    c.GetY = function() return c.y end
    c.GetZ = function() return c.z end
    c.GetEntry = function() return entry end
    c.GetGUID = function() return string.format("Creature (Entry: %d Guid: %d)", entry, counter) end
    c.GetGUIDLow = function() return counter end
    c.ToUnit = function() return c end
    c.IsWithinLoS = function(_, x, y, z)
        c.losX, c.losY, c.losZ = x, y, z
        return c.sees
    end
    return c
end

local function player(name, gm)
    local p = { sent = {} }
    p.GetGMRank = function() return gm and 3 or 0 end
    p.GetName = function() return name end
    p.GetX = function() return 0 end p.GetY = function() return 0 end p.GetZ = function() return 100 end
    p.IsInWorld = function() return true end
    p.GetGUID = function() return "Player " .. name .. " (Guid: 12)" end
    p.SendBroadcastMessage = function(_, s) p.sent[#p.sent + 1] = s end
    p.GetMap = function() return { GetMapId = function() return 309 end, GetInstanceId = function() return 7 end,
        IsDungeon = function() return true end } end
    p.GetCreaturesInRange = function(_, reach, entry, hostile, dead)
        local out = {}
        for _, c in ipairs(world) do
            if entry == 0 or entry == c.entry then out[#out + 1] = c end
        end
        return out
    end
    p.ToUnit = function() return p end
    return p
end

local gm = player("Cmd", true)
_G.__players = { Cmd = gm }

require("tt_lib")
dofile("lua_scripts/tt/tt_commands.lua")

local fails = 0
local function check(name, cond)
    if not cond then fails = fails + 1; print("FAIL " .. name) else print("ok   " .. name) end
end
local function last() return gm.sent[#gm.sent] or "" end
local function guidOf(e, counter) return string.format("0xF130%06X%06X", e, counter) end

local sighted = creature(1135, 101, 10, 0, 100, true)
local blind = creature(1136, 102, 20, 0, 100, false)
world = { sighted, blind }
local g1, g2 = guidOf(1135, 101), guidOf(1136, 102)

handler(42, gm, "tt q los 0 0 100 " .. g1 .. " " .. g2)
check("two answers", last():find('"n":2', 1, true))
check("in sight", last():find('"in":true', 1, true))
check("out of sight", last():find('"in":false', 1, true))
check("distance is 3D from the point", last():find('"d":10', 1, true))
check("eye height is 2 yd above the point", sighted.losZ == 102 and sighted.losX == 0 and sighted.losY == 0)

-- 8 guids answer; a 9th is refused before any lookup.
local eight = {}
for i = 1, 8 do eight[i] = guidOf(2000 + i, 200 + i) end
handler(42, gm, "tt q los 0 0 100 " .. table.concat(eight, " "))
check("eight guids", last():find('"n":8', 1, true))
handler(42, gm, "tt q los 0 0 100 " .. table.concat(eight, " ") .. " " .. g1)
check("nine guids refused", last():find('"err":"too_many"', 1, true))

-- An absent guid and a bad guid answer their own entry, not the whole line.
handler(42, gm, "tt q los 0 0 100 " .. g1 .. " " .. guidOf(4000, 400) .. " 0xzz")
local reply = last()
check("the found guid still answers", reply:find('"in":true', 1, true))
check("absent guid", reply:find('"err":"not_found"', 1, true))
check("bad guid", reply:find('"err":"bad_guid"', 1, true))
check("three entries", reply:find('"n":3', 1, true))

-- No guid at all: usage, nothing looked up.
handler(42, gm, "tt q los 0 0 100")
check("needs a guid", last():find('"err":"usage"', 1, true))

-- A point too far from the caller is refused like every other q command.
handler(42, gm, "tt q los 500 0 100 " .. g1)
check("far point refused", last():find('"err":"point_far"', 1, true))

if fails > 0 then os.exit(1) end
print("all q los mock tests passed")
