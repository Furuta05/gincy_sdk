local root=arg[1] or "."
local function existing(path)
    local file=io.open(path,"rb")
    if file then file:close() return path end
end
dofile(existing(root .. "/tests/runtime_v3.lua") or existing(root .. "/source/tests/runtime_v3.lua"))
local I=Gincy.Internal
Gincy.Content.Cleanup("fishing")
local function candidate(id)
    local directory=(existing(root .. "/examples/" .. id .. "/manifest.lua") or existing(root .. "/source/examples/" .. id .. "/manifest.lua")):gsub("manifest.lua$","")
    local manifest=assert(loadfile(directory .. "manifest.lua"))()
    local files={}
    for _,name in ipairs(manifest.entrypoints) do
        local input=assert(io.open(directory .. name,"rb"))
        files[#files+1]={path=name,realm=({sh_=2,sv_=1,cl_=3})[name:sub(1,3)],data=input:read("*a")}
        input:close()
    end
    return {manifest=manifest,files=files}
end
I.Candidates.modern_hud=candidate("modern_hud")
I.Candidates.quantum_fishing=candidate("quantum_fishing")
I.Order=I.Resolve(I.Candidates)
Gincy.Modules.Enable("quantum_fishing",function(ok,err) assert(ok,err and err.message) end)
assert(Gincy.Content.Get("fish_species","quantum_carp").weight==3)
assert(Gincy.Content.Types.rod_profiles and Gincy.Content.Types.water_zones and Gincy.Content.Types.bait_types)
assert(Gincy.Modules.Reload("quantum_fishing",candidate("quantum_fishing")))
assert(Gincy.Content.Exists("fish_species","quantum_carp"))
assert(Gincy.Modules.Disable("quantum_fishing"))
assert(not Gincy.Content.Types.fish_species)
SERVER,CLIENT=false,true
Gincy.Modules.Enable("modern_hud",function(ok,err) assert(ok,err and err.message) end)
assert(I.Contexts.modern_hud and not I.DB and not Gincy.Characters and not Gincy.Items)
assert(Gincy.Modules.Reload("modern_hud",candidate("modern_hud")))
assert(Gincy.Modules.Disable("modern_hud"))
assert(not I.Contexts.modern_hud)
print("PASS acceptance: ModernHUD client-only without database/RPG, QuantumFishing arbitrary schemas and reload")
