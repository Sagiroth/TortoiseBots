-- luacheck config for the Eluna test toolkit (Eluna's default Lua is 5.2)
std = "lua52"
max_line_length = false
unused_args = false
globals = { "TT" }
read_globals = {
    "GetCurrTime", "GetUnitGUID", "GetObjectGUID", "GetPlayerGUID", "GetPlayerByGUID", "GetPlayerByName",
    "GetMapById", "CreateLuaEvent", "RemoveEventById", "RegisterPlayerEvent",
    "RegisterServerEvent", "CharDBQuery", "WorldDBQuery",
}
files["lua_scripts/tt/tt_lib.ext"] = {}
