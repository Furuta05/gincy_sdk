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
if not exists(root .. "/garrysmod/gamemodes/gincy/gamemode/shared.lua") then
    root = root .. "/.."
end
local core = root .. "/garrysmod/gamemodes/gincy/gamemode/core/"
SERVER, CLIENT = true, false
local hooks = {}
hook = {Add = function(event, id, fn) hooks[event] = hooks[event] or {} hooks[event][id] = fn end, Remove = function(event, id) if hooks[event] then hooks[event][id] = nil end end, Run = function(event, ...) for _, fn in pairs(hooks[event] or {}) do fn(...) end end}
timer = {Create = function() end, Remove = function() end, Simple = function(_, fn) fn() end}
SysTime = os.clock
CurTime = function() return 0 end
ErrorNoHalt = function() end
IsValid = function(value) return type(value) == "table" and value.valid == true end
IsEntity = IsValid
player = {GetAll = function() return {} end}
CompileString = function(source, name) local fn, err = loadstring(source, name) return fn or err end
local files, blobs, sequence = {}, {}, 0
util = {
    AddNetworkString = function() end,
    SHA256 = function(value) return string.rep("a", 64) end,
    CRC = function() return "1" end,
    TableToJSON = function(value)
        sequence = sequence + 1
        local key = "J" .. sequence
        blobs[key] = value
        return key
    end,
    JSONToTable = function(value)
        local stored = blobs[value]
        if type(stored) ~= "table" then return {} end
        local copy = {}
        for key, child in pairs(stored) do copy[key] = child end
        return copy
    end
}
file = {
    Read = function(path) return files[path] end,
    Write = function(path, value) files[path] = value end,
    Delete = function(path) files[path] = nil end,
    Exists = function(path) return files[path] ~= nil end,
    CreateDir = function() end,
    Find = function() return {}, {} end,
    Size = function() return 0 end,
    Append = function() end
}
math.Clamp = function(value, low, high) return math.max(low, math.min(high, value)) end
for _, name in ipairs({"sh_version.lua", "sh_api.lua", "sh_schema.lua", "sh_content.lua", "sh_content_runtime.lua", "sh_scheduler.lua", "sh_context.lua", "sh_module.lua", "sh_upgrade.lua", "sv_database.lua"}) do
    dofile(core .. name)
end
local I = Gincy.Internal
local checks = 0
local function check(condition, name)
    if not condition then error("FAIL " .. name) end
    checks = checks + 1
end

local test = Gincy.Module("test")
local spawned = 0
test:Hook("PlayerSpawn", function() spawned = spawned + 1 end)
test:After(1, function() end)
test:Every(5, function() end)
hook.Run("PlayerSpawn")
check(spawned == 1, "simple hook")
check(Gincy.Scheduler.CountOwner("test") == 2, "simple tasks owned")
local generation = test:Context().generation
test:Unload()
hook.Run("PlayerSpawn")
check(spawned == 1 and Gincy.Scheduler.CountOwner("test") == 0, "module unload cleanup")

local netmod = Gincy.Module("netmod")
netmod:Net("select_skill", {skill = "string"})
local received
netmod:Receive("select_skill", function(_, data) received = data.skill end)
local handler = I.Registries.Network.records["netmod.select_skill"].handler
handler({valid = true}, {skill = "fireball"})
check(received == "fireball", "simple receive")
handler({valid = true}, {skill = 5})
check(received == "fireball", "invalid payload rejected")
local sent, value = netmod:Send("select_skill", {valid = true}, {skill = "dash"})
check(sent and value.skill == "dash" and I.SimpleNet[#I.SimpleNet].data.skill == "dash", "simple send")
check(not netmod:Send("select_skill", {valid = true}, {skill = 1}), "send schema")
netmod:Unload()
check(not Gincy.Network.Exists("netmod.select_skill"), "network ownership cleanup")

local combat = Gincy.Module("combat")
local combatGeneration = combat:Context().generation
combat:Content("skill", {name = "string", damage = "number", cooldown = "number"})
check(combat:LoadContent("skill", "fireball", {name = "Fireball", damage = 50, cooldown = 4}), "week 1 fireball")
check(combat:LoadContent("skill", "dash", {name = "Dash", damage = 0, cooldown = 2}), "week 2 dash")
check(combat:LoadContent("skill", "uppercut", {name = "Uppercut", damage = 20, cooldown = 3}), "week 2 uppercut")
check(combat:LoadContent("skill", "fireball", {name = "Fireball", damage = 45, cooldown = 4}), "week 3 retune")
check(combat:LoadContent("skill", "lightning", {name = "Lightning", damage = 70, cooldown = 6}), "week 3 lightning")
check(combat:Context().generation == combatGeneration and combat:Context().active, "content reload keeps module")
check(Gincy.Content.Get("combat.skill", "fireball").damage == 45, "retuned fireball")
local kept = combat:LoadContent("skill", "fireball", {name = "Fireball", damage = 45, cooldown = "fast"})
check(not kept and Gincy.Content.Get("combat.skill", "fireball").damage == 45, "failed reload keeps previous")
local human = Gincy.Content.Failures["combat.skill/fireball"].human
check(human:find("cooldown", 1, true) and human:find("number", 1, true) and human:find("fast", 1, true), "human content error")
check(not pcall(function() Gincy.Module("inventory"):Content("combat.skill", {name = "string"}) end), "namespace conflict")

local ran = 0
combat:Content("effect", {
    schema = {name = "string"},
    capabilities = {Schedule = true, Explode = function() ran = ran + 10 end},
    prepare = function() end
})
check(combat:LoadContent("effect", "boom", {name = "Boom", cast = function(skill) skill:After(1, function() skill:Explode() end) end}), "content script")
Gincy.Scheduler.SetNow(0)
check(Gincy.Content.Call("combat.effect", "boom", "cast"), "content call")
check(Gincy.Scheduler.CountOwner("combat") >= 1, "content owns task")
Gincy.Scheduler.Tick(1)
check(ran == 10, "content task runs")
check(Gincy.Content.Call("combat.effect", "boom", "cast"), "content call again")
check(Gincy.Scheduler.CountOwner("combat") >= 1, "replacement content task")
check(combat:LoadContent("effect", "boom", {name = "Boom", cast = function() end}), "content reload")
Gincy.Scheduler.Tick(5)
check(ran == 10 and Gincy.Scheduler.CountOwner("combat") == 0, "content reload cancels tasks")

local secret = {implementation = "private-hud"}
local hud = Gincy.Module("modernhud")
hud:Content("config", {schema = {scale = "number"}, capabilities = {Scale = function(cfg) return cfg.scale end}})
check(hud:LoadContent("config", "theme", {scale = 2, apply = function(cfg) return cfg:Scale(), cfg.implementation, secret.implementation end}), "hud content")
local proxy = Gincy.Content.Proxy(Gincy.Content.Active["modernhud.config/theme"])
check(proxy:Scale() == 2 and proxy.implementation == nil and getmetatable(proxy) == "gincy-content", "private hud stays private")

local inventory = Gincy.Module("inventory")
inventory:Content("item", {name = "string"})
check(not inventory:LoadContent("item", "ak74", {name = "AK74", depends = {"inventory.item/ammo_545"}}), "missing dependency")
check(not Gincy.Content.Exists("inventory.item", "ak74"), "invalid content inactive")
check(inventory:LoadContent("item", "ammo_545", {name = "5.45"}), "dependency content")
check(inventory:LoadContent("item", "ak74", {name = "AK74", depends = {"inventory.item/ammo_545"}}), "dependency satisfied")

local dev = Gincy.Content.LoadTree({
    {kind = "combat.skill", id = "nova", raw = "return {name='Nova', damage=10, cooldown=1}", lua = true, origin = "content", path = "gincy_content/combat/skill/nova.lua"},
    {kind = "combat.skill", id = "nova", raw = "return {name='Nova', damage=12, cooldown=1}", lua = true, origin = "dev", path = "gincy_dev/content/combat/skill/nova.lua"}
})
check(dev ~= false and Gincy.Content.Get("combat.skill", "nova").damage == 12, "dev override")
check(Gincy.Content.Overrides["combat.skill.nova"].development:find("gincy_dev", 1, true) ~= nil, "override visible")
check(Gincy.Content.Active["combat.skill/nova"] ~= nil, "single active content")

local idle = Gincy.Module("idlemod")
Gincy.Scheduler.SetNow(0)
for index = 1, 1000 do idle:Every("idle" .. index, 50, function() end) end
local before = Gincy.Scheduler.Stats().rootPeeks
Gincy.Scheduler.Tick(0)
local stats = Gincy.Scheduler.Stats()
check(stats.lastVisited == 1 and stats.rootPeeks == before + 1 and stats.active >= 1000, "idle tick is not a linear scan")
idle:Unload()

local heavy = Gincy.Module("heavy")
for index = 1, 100 do heavy:Every("job" .. index, 30, function() end) end
check(Gincy.Scheduler.CountOwner("heavy") == 100, "100 owned tasks")
heavy:Unload()
check(Gincy.Scheduler.CountOwner("heavy") == 0, "module task cleanup")

local ply = {valid = true}
local hits = 0
local owner = Gincy.Module("playerwork")
owner:Every(ply, "regen", 1, function() hits = hits + 1 end)
Gincy.Scheduler.SetNow(0)
Gincy.Scheduler.Tick(1)
check(hits == 1, "player task")
ply.valid = false
hook.Run("PlayerDisconnected", ply)
Gincy.Scheduler.Tick(2)
check(hits == 1 and Gincy.Scheduler.CountOwner("playerwork") == 0, "disconnect cancels player task")

local people = {}
for index = 1, 10 do people[index] = {valid = true} end
player.GetAll = function() return people end
local seen = 0
local spread = Gincy.Module("spread")
Gincy.Scheduler.SetNow(0)
spread:PlayersEvery("status", 1, {spread = true, batch = 4}, function() seen = seen + 1 end)
Gincy.Scheduler.Tick(1)
check(seen == 4, "spread first slice")
Gincy.Scheduler.Tick(1)
check(seen == 8, "spread second slice")
Gincy.Scheduler.Tick(1)
check(seen == 10, "spread finishes cohort")
spread:Unload()

Gincy.Scheduler.Configure({budget = 0})
Gincy.Scheduler.SetNow(0)
local exact, relaxed = 0, 0
Gincy.Scheduler.Schedule({owner = "budget", moduleGeneration = 1, name = "exact", delay = 1, reps = 1, precision = "exact", class = "gameplay", source = "test", fn = function() exact = exact + 1 end})
Gincy.Scheduler.Schedule({owner = "budget", moduleGeneration = 1, name = "relax", delay = 1, reps = 1, precision = "relaxed", class = "maintenance", source = "test", fn = function() relaxed = relaxed + 1 end})
Gincy.Scheduler.Tick(1)
check(exact == 1 and relaxed == 0, "budget keeps exact work")
Gincy.Scheduler.Configure({budget = 1})
Gincy.Scheduler.Tick(1)
check(relaxed == 1, "deferred work runs later")

local calls = 0
Gincy.Scheduler.SetNow(0)
Gincy.Scheduler.Schedule({owner = "errors", moduleGeneration = 1, name = "broken", interval = 1, precision = "exact", class = "normal", source = "test", fn = function() calls = calls + 1 error("boom") end})
for step = 1, 8 do Gincy.Scheduler.Tick(step) end
check(calls == 8 and Gincy.Scheduler.Inspect("broken") == nil, "repeated task errors disable the task")
check(I.Errors and next(I.Errors) ~= nil, "task error attributed")

Gincy.Scheduler.SetNow(0)
Gincy.Scheduler.Schedule({owner = "drift", moduleGeneration = 1, name = "pulse", interval = 1, precision = "exact", class = "normal", source = "test", fn = function() end})
Gincy.Scheduler.Tick(10)
local pulse = Gincy.Scheduler.Inspect("pulse")
check(pulse and pulse.calls == 1 and pulse.missed >= 8 and pulse.next > 10, "missed runs do not burst")
Gincy.Scheduler.CancelOwner("drift")

Gincy.Scheduler.SetNow(0)
local slow = Gincy.Scheduler.Schedule({owner = "slow", moduleGeneration = 1, name = "slow", delay = 1, reps = 1, precision = "exact", class = "gameplay", source = "test", fn = function() end})
local original = Gincy.Scheduler.Measure
Gincy.Scheduler.Measure = function(callback)
    local _, ok, err = original(callback)
    return 0.02, ok, err
end
Gincy.Scheduler.Tick(1)
Gincy.Scheduler.Measure = original
local profile = I.Metrics.scheduler
check(profile and profile.calls >= 1 and profile.slow >= 1, "scheduler metrics feed profile")

files["gincy/identity/private.pem"] = "SECRET"
files["gincy/trust/publisher.pem"] = "TRUST"
check(I.EnsureDatabaseConfig() ~= nil, "template created")
check(I.DB.Canonical == "NOT_CONFIGURED", "storage not configured")
local encoded = util.TableToJSON(I.StorageTest())
check(not tostring(encoded):find("s3cret", 1, true), "password redacted before setup")
local ready
I.Native = {
    configure = function(config) return config.password == "s3cret" end,
    submit = function() return 7 end,
    poll = function() return {{id = 7, ok = true, rows = {}, milliseconds = 2, code = "OK"}} end,
    storage_probe = function(config)
        return {tcp = true, handshake = true, auth = config.password == "s3cret", database = true, permissions = true, migrations = true, version = "PostgreSQL 16.2", latencyMs = 4}
    end
}
I.StorageSetup({host = "127.0.0.1", port = 5432, database = "gincy", user = "gincy", password = "s3cret", sslmode = "prefer"}, function(ok) ready = ok end)
check(ready and I.DB.Canonical == "READY", "storage setup reaches ready")
local probed = I.StorageTest()
check(probed.version == "PostgreSQL 16.2" and probed.config.password == "", "storage test hides password")
check(files["gincy/identity/private.pem"] == "SECRET", "setup keeps identity")

files["gincy/runtime/state.json"] = nil
check(Gincy.Upgrade.Run(), "fresh state")
files["gincy/runtime/state.json"] = util.TableToJSON({product = "3.2.0", settings = {theme = "dark", note = "keep"}})
local upgraded, report = Gincy.Upgrade.Run()
check(upgraded and report.product == "3.3.0", "3.2 upgrades to 3.3")
local stored = util.JSONToTable(files["gincy/runtime/state.json"])
check(stored.product == "3.3.0" and stored.settings.theme == "dark" and stored.settings.note == "keep", "settings preserved")
check(files["gincy/trust/publisher.pem"] == "TRUST", "trust preserved")
local backup = false
for path in pairs(files) do if path:find("gincy/runtime/backups/", 1, true) then backup = true end end
check(backup, "upgrade backup")
Gincy.Version.Product = "3.2.0"
local denied, why = Gincy.Upgrade.Check()
check(not denied and why.code == "GINCY-UPGRADE-DOWNGRADE", "downgrade refused")
Gincy.Version.Product = "3.3.0"
check(stored.product == "3.3.0", "downgrade did not rewrite state")

check(I.ScaffoldModule("samplemod"), "scaffold module")
check(tostring(files["gincy_dev/modules/samplemod/sh_init.lua"]):find("Gincy.Module", 1, true) ~= nil, "module template")
check(I.ScaffoldContent("combat", "skill", "frost"), "scaffold content")
check(tostring(files["gincy_dev/content/combat/skill/frost.lua"]):find("cooldown", 1, true) ~= nil, "content template uses schema")

check(generation ~= nil and slow ~= nil, "bookkeeping")
print("PASS gincy 3.3 (" .. checks .. " checks)")
