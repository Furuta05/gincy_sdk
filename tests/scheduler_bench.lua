local root = arg[1] or "."
do
    local function compat(root)
        for _, path in ipairs({root .. "/source/tests/compat.lua", root .. "/tests/compat.lua"}) do
            local file = io.open(path, "rb")
            if file then file:close() dofile(path) return end
        end
    end
    compat(root)
end
local function exists(path)
    local file = io.open(path, "rb")
    if file then file:close() return true end
end
if not exists(root .. "/garrysmod/gamemodes/gincy/gamemode/core/sh_scheduler.lua") then root = root .. "/.." end
local core = root .. "/garrysmod/gamemodes/gincy/gamemode/core/"
SERVER = true
hook = {Add = function() end, Remove = function() end, Run = function() end}
timer = {Create = function() end, Remove = function() end}
SysTime = os.clock
CurTime = function() return 0 end
ErrorNoHalt = function() end
IsValid = function() return false end
player = {GetAll = function() return {} end}
for _, name in ipairs({"sh_version.lua", "sh_api.lua", "sh_schema.lua", "sh_content.lua", "sh_scheduler.lua"}) do
    dofile(core .. name)
end
print("Gincy Scheduler benchmark")
for _, count in ipairs({100, 1000, 10000}) do
    local result = Gincy.Scheduler.Benchmark(count)
    assert(result.checksum > 0, "benchmark callback did not run")
    assert(result.idleVisited == 1, "idle lookup scanned more than the heap root")
    assert(result.scheduleSeconds >= 0 and result.lookupSeconds >= 0 and result.execSeconds >= 0, "missing timings")
    assert(result.count == count, "benchmark count")
    print(string.format("  %5d  schedule %.4fs  idle %.4fs  lookup %.4fs  exec %.4fs  mem %.1fKB  checksum %d", count, result.scheduleSeconds, result.idleSeconds, result.lookupSeconds, result.execSeconds, result.memoryKb, result.checksum))
end
print("PASS scheduler benchmark")
