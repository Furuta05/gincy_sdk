local root = arg[1] or "."
SERVER, CLIENT = true, false
local unpack = unpack or table.unpack
SysTime = os.clock
CurTime = os.clock
util = { AddNetworkString = function() end, SHA256 = function(v) return string.rep("a", 64) end }
net = { Receive = function() end, Start = function() end, WriteString = function() end, WriteUInt = function() end, WriteData = function() end, Send = function() end, ReadString = function() return "" end, ReadUInt = function() return 0 end, ReadData = function() return "" end }
IsValid = function() return false end
dofile(root .. "/garrysmod/gamemodes/gincy/gamemode/core/sh_gvm.lua")
local GVM = Gincy.GVM
local assertions = 0
local function check(ok, why) assert(ok, why) assertions = assertions + 1 end
local bytes = GVM.Assemble({
    k = {2, 3},
    code = {
        {op = "PUSHK", a = 0},
        {op = "PUSHK", a = 1},
        {op = "ADD"},
        {op = "RETURN", a = 1}
    }
})
check(bytes:sub(1, 4) == "GVM1", "magic")
GVM.Load("math", bytes)
local result = GVM.Execute("math", {})
check(result == 5, "2+3")
local session = "0123456789abcdef"
local mapped = GVM.SessionMap(bytes, session)
check(mapped ~= bytes, "session map")
check(GVM.SessionUnmap(mapped, session) == bytes, "session unmap")
GVM.Destroy("math")
check(#GVM.List() == 0, "destroyed")
local ok, err = pcall(GVM.Parse, "XXXX")
check(not ok, "malformed")
local profile = GVM.Profile()
check(profile.calls >= 1, "profile calls")
print("PASS gvm runtime: " .. assertions .. " assertions")
