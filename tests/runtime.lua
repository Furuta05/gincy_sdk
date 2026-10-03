local root = arg[1] or "."
local function compat(root)
    local candidates = {root .. "/source/tests/compat.lua", root .. "/tests/compat.lua"}
    for _, path in ipairs(candidates) do
        local file = io.open(path, "rb")
        if file then file:close() dofile(path) return end
    end
end
compat(root)
local core = root .. "/garrysmod/gamemodes/gincy/gamemode/core/"
SERVER, CLIENT = true, false
local hooks, timers, commands = {}, {}, {}
hook = {Add = function(event, id, fn) hooks[event] = hooks[event] or {} hooks[event][id] = fn end, Remove = function(event,id) if hooks[event] then hooks[event][id] = nil end end, Run = function(event, ...) for _, fn in pairs(hooks[event] or {}) do local value = fn(...) if value ~= nil then return value end end end}
timer = {Create = function(id,delay,reps,fn) timers[id] = fn end, Remove = function(id) timers[id] = nil end, Simple = function(_,fn) fn() end}
concommand = {Add = function(id,fn) commands[id] = fn end, Remove = function(id) commands[id] = nil end}
SysTime = os.clock
CurTime = os.clock
ErrorNoHalt = function(value) io.stderr:write(value) end
IsValid = function(value) return type(value) == "table" and value.valid == true end
Entity = function(id) return {valid = id == 1} end
IsEntity = function() return false end
local function encode(value)
    if type(value) == "nil" then return "null" end
    if type(value) == "string" then return string.format("%q", value) end
    if type(value) ~= "table" then return tostring(value) end
    local entries = {} for key, child in pairs(value) do entries[#entries + 1] = encode(tostring(key)) .. ":" .. encode(child) end table.sort(entries) return "{" .. table.concat(entries, ",") .. "}"
end
util = {Compress = function(v) return v end, AddNetworkString = function() end, JSONToTable = function(value) return type(value) == "table" and value or {} end, TableToJSON = encode, SHA256 = function(value) return string.rep("a",64) end}
file = {Read = function() return "{}" end, Find = function() return {}, {} end}
net = {Receive = function() end, Start = function() end, WriteUInt = function() end, WriteData = function() end, Send = function() end}
player = {GetAll = function() return {} end}
math.Clamp = function(v,a,b) return math.max(a,math.min(b,v)) end
table.HasValue = function(t,value) for _,v in pairs(t) do if v==value then return true end end return false end
CompileString = function(source,name) local fn, err = loadstring(source,name) return fn or err end
utf8 = {len = string.len}
game = {GetMap = function() return "test_map" end}
local assertions = 0
local function check(condition, name) assert(condition, name) assertions = assertions + 1 end
for _, name in ipairs({"sh_version.lua","sh_api.lua","sh_schema.lua","sh_content.lua","sh_content_runtime.lua","sh_scheduler.lua","sh_context.lua","sh_loader.lua","sv_database.lua"}) do dofile(core .. name) end
local gincy = root .. "/garrysmod/lua/gincy/"
for _,name in ipairs({"attributes/sh_attributes.lua","skills/sh_skills.lua","items/sh_items.lua","inventory/sh_inventory.lua","characters/sh_characters.lua","characters/sh_network.lua","interactions/sh_interactions.lua","status/sh_status.lua","worldobjects/sh_worldobjects.lua","activities/sh_activities.lua","organizations/sh_organizations.lua","director/sh_director.lua","inventory/sv_inventory.lua","characters/sv_characters.lua"}) do dofile(gincy .. name) end
local I = Gincy.Internal
check(Gincy.API.IsCompatible(">=1.1.0 <2.0.0"), "range")
check(not Gincy.API.IsCompatible(">=2.0.0"), "incompatible")
check(not Gincy.API.IsCompatible("==1.0.0"), "invalid operator")
check(not Gincy.API.IsCompatible("1.0"), "full version required")
check(not Gincy.API.IsCompatible("=01.0.0"), "leading zero")
local candidate = function(id,deps) return {manifest = {id=id,version="1.0.0",gincy=">=1.0.0 <2.0.0",dependencies=deps or {},capabilities={}},files={}} end
local order, failures = I.Resolve({alpha=candidate("alpha",{beta=">=1.0.0"}),beta=candidate("beta")})
check(order[1]=="beta" and order[2]=="alpha", "topological order")
order, failures = I.Resolve({alpha=candidate("alpha",{beta=">=1.0.0"}),beta=candidate("beta",{alpha=">=1.0.0"}),good=candidate("good")})
check(#order==1 and order[1]=="good" and failures.alpha and failures.beta, "cycle isolation")
order, failures = I.Resolve({alpha=candidate("alpha",{missing=">=1.0.0"})})
check(#order==0 and failures.alpha:find("missing"), "missing dependency")
check(Gincy.Items.Register("test_item",{name="Test",stack=20,weight=.2},"owner"), "register")
local ok, err = Gincy.Items.Unregister("test_item","other")
check(not ok and err.code=="NOT_OWNER", "registration ownership")
local copy=Gincy.Items.Get("test_item") copy.weight=99
check(Gincy.Items.Get("test_item").weight==.2, "defensive definitions")
local ctx = I.Context({id="test",capabilities={"items.register","network.register"}})
ctx.Items:Register("temporary",{name="Temporary",stack=1,weight=0})
ctx:Hook("TestEvent","owned",function() end)
ctx:Timer("owned",1,0,function() end)
ctx.Network:Register("intent",{schema={text="string"},rate=2},function() end)
check(Gincy.Network.Exists("test.intent"), "namespace")
check(not pcall(function() ctx.Interactions:Register("bad",{}) end), "capability denied")
ctx:Shutdown()
check(not Gincy.Items.Exists("temporary") and not Gincy.Network.Exists("test.intent") and not next(hooks.TestEvent) and not timers["Gincy.test.owned"], "scoped cleanup")
check(I.ValidateLegacyNetwork({target="entity?",count={type="integer",min=1,max=3}},{count=2}), "optional schema")
check(not I.ValidateLegacyNetwork({count="integer"},{count=1/0}), "nonfinite")
check(not I.ValidateLegacyNetwork({count="integer"},{count=1,extra=1}), "unknown field")
check(not I.ValidateLegacyNetwork({target="entity"},{target=2}), "invalid entity")
check(I.ValidateLegacyNetwork({target="entity"},{target=1}), "valid entity")
local contentOK, contentStage = Gincy.Content.Stage({{kind="skill", id="fireball", definition={name="Fireball", damage=75, cooldown=4}}})
check(contentOK and not Gincy.Content.Exists("skill", "fireball"), "content staging isolation")
check(Gincy.Content.Apply(contentStage, "test"), "content apply")
check(Gincy.Content.Get("skill", "fireball").damage == 75, "content active")
local reloadOK = Gincy.Content.Reload({{kind="skill", id="fireball", definition={name="Fireball", damage=65, cooldown=4}}}, "test")
check(reloadOK and Gincy.Content.Get("skill", "fireball").damage == 65, "content atomic reload")
local invalidReload = Gincy.Content.Reload({{kind="skill", id="fireball", definition={name="Fireball", damage=-1, cooldown=4}}}, "test")
check(not invalidReload and Gincy.Content.Get("skill", "fireball").damage == 65, "content rollback protection")
check(Gincy.Content.Rollback("skill", "fireball", "test") and Gincy.Content.Get("skill", "fireball").damage == 75, "content revision rollback")
local calls=0
I.Done(function(success,errorValue,result) calls=calls+1 check(success and errorValue==nil and result==7,"success contract") end,true,7)
I.Done(function(success,errorValue) calls=calls+1 check(not success and errorValue.code=="BUSY","error contract") end,false,nil,"BUSY")
check(calls==2,"callbacks once")
Gincy.Attributes.Register("strength",{default=10,min=1,max=100})
Gincy.Skills.Register("gathering",{maxLevel=20,xpPerLevel=25})
local ply={valid=true}
local character=I.Hydrate({id="1",name="Alice",owner="account",money=25,container="9",capacity=60,slots=64,values={{kind="skill",key="gathering",value=100}},inventory={{instance="11",item="test_item",amount=4,weight=.2,stack=20}}},ply)
I.Active[ply]=character
check(character:GetID()=="1" and character:GetName()=="Alice" and character:GetMoney()==25,"character object")
check(character:GetSkill("gathering")==2,"xp curve")
check(character:GetInventory():Count("test_item")==4 and character:GetInventory():Has("test_item",3),"inventory view")
check(character:GetInventory():GetWeight()==.8,"weight")
character:AddAttributeModifier("strength","equipment:test",2)
check(character:GetAttribute("strength")==12,"attribute modifier")
character:RemoveAttributeModifier("strength","equipment:test")
check(character:GetAttribute("strength")==10,"remove modifier")
local pending
I.DB.Submit=function(statements,callback) pending=callback end
I.Run({"same"},{{sql="test"}},function() end)
local blocked
I.Run({"same"},{{sql="test"}},function(ok,rows,code) blocked=not ok and code=="BUSY" end)
check(blocked,"overlap protection")
pending(true,{})
Gincy.Skills.Unregister("gathering")
local fixturePath = root .. "/tests/package_fixture.lua"
local fixtureFile = io.open(fixturePath, "rb")
if not fixtureFile then fixturePath = root .. "/source/tests/package_fixture.lua" else fixtureFile:close() end
local fixture=dofile(fixturePath)
I.DB.Ready=true
I.Candidates[fixture.manifest.id]=fixture
I.MigrateModule=function(_,callback) callback(true) end
I.LoadCandidate(fixture,function(success) check(success,"verified package module initialization") end)
check(Gincy.Items.Exists("wood") and Gincy.Items.Exists("apple") and Gincy.Skills.Exists("gathering"),"survival registries")
check(Gincy.Interactions.Exists("survival_example.gather") and Gincy.Interactions.Exists("survival_example.deliver"),"survival interactions")
check(commands["survival_example.node"] and commands["survival_example.job"],"scoped commands")
check(Gincy.Modules.Unload("survival_example"),"unload")
check(not Gincy.Items.Exists("wood") and not commands["survival_example.node"],"unload cleanup")
print("GLua API assertions: " .. assertions)
