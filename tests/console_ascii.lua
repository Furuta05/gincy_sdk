local root = arg[1] or "."
local path = root .. "/garrysmod/gamemodes/gincy/gamemode/core/sv_console.lua"
local f = assert(io.open(path, "rb"))
local source = f:read("*a")
f:close()
assert(not source:find("[\128-\255]"), "Windows SRCDS console renderer must stay ASCII")
assert(source:find("rule%(%) return \" ----------------------------------------\""), "ASCII rule")
print("PASS console ascii")
