-- Structural bootstrap contract: required directories are created by runtime, not shipped.
local root = arg[1] or "."
local function exists(rel)
    local f = io.open(root .. "/" .. rel, "rb")
    if f then f:close() return true end
    return false
end
assert(exists("garrysmod/gamemodes/gincy/gamemode/shared.lua"), "gamemode")
assert(exists("garrysmod/gamemodes/gincy/gamemode/core/sh_gvm.lua"), "client vm")
assert(exists("garrysmod/gamemodes/gincy/gamemode/core/sh_loader.lua"), "loader")
assert(not exists("garrysmod/gincy_modules/.keep"), "runtime modules dir is not shipped")
assert(not exists("garrysmod/data/gincy/identity/private.pem"), "identity is not shipped")
local loader = assert(io.open(root .. "/garrysmod/gamemodes/gincy/gamemode/core/sh_loader.lua", "rb"))
local text = loader:read("*a")
loader:close()
assert(text:find("BootstrapFilesystem"), "bootstrap")
assert(text:find("gincy_dev/modules"), "dev tree")
assert(text:find("generate_identity"), "identity")
assert(text:find("FILE_NOT_FOUND"), "native diagnostics")
print("PASS bootstrap contract")
