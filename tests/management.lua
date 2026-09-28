local root=arg[1] or "."
dofile(root .. "/tests/runtime_v3.lua")
local I=Gincy.Internal
I.RuntimeConfig={enabled={}}
system={IsWindows=function() return false end}
game={GetMap=function() return "test" end}
local json,files,sequence={},{},0
util.TableToJSON=function(value) sequence=sequence+1 local key="json:" .. sequence json[key]=I.Copy(value) return key end
util.JSONToTable=function(value) return json[value] and I.Copy(json[value]) end
file.Write=function(path,value) files[path]=value end
file.Read=function(path) return files[path] end
file.Delete=function(path) files[path]=nil end
file.Exists=function(path) return files[path]~=nil end
file.CreateDir=function() end
file.Size=function(path) return files[path] and #files[path] or -1 end
file.Append=function(path,value) files[path]=(files[path] or "") .. value end
file.Rename=function(a,b) files[b]=files[a] files[a]=nil end
file.Find=function(pattern)
    local prefix,suffix=pattern:match("^(.-)%*(.*)$")
    local result={}
    for path in pairs(files) do if prefix and path:sub(1,#prefix)==prefix and path:sub(-#suffix)==suffix then result[#result+1]=path:sub(#prefix+1) end end
    return result,{}
end
dofile(root .. "/garrysmod/gamemodes/gincy/gamemode/core/sv_management.lua")
local count=0
local function call(op,args)
    local result
    Gincy.Management.Execute(op,args,"test",function(response) result=response end)
    assert(result,"missing management result") count=count+1
    return result
end
assert(call("status").ok)
assert(call("content.edit",{kind="fish_species",id="carp",definition={name="Edited",weight=2}}).ok)
assert(Gincy.Content.Get("fish_species","carp").name=="Edited")
local result=call("deploy",{entries={{kind="fish_species",id="carp",definition={name="Deployed",weight=3}}}})
assert(result.ok and result.value.state=="COMMITTED")
local id=result.value.id
assert(call("history").value[1].id==id)
assert(Gincy.Content.Get("fish_species","carp").weight==3)
assert(call("rollback",{id=id}).ok)
assert(Gincy.Content.Get("fish_species","carp").weight==2)
assert(not call("rollback",{id=id}).ok)
local bad=call("deploy",{entries={{kind="fish_species",id="carp",definition={name="Bad",weight=-1}}}})
assert(not bad.ok and Gincy.Content.Get("fish_species","carp").weight==2)
assert(call("content.edit",{kind="fish_species",id="fishing:carp",definition={name="Namespaced",weight=1}}).ok)
assert(call("content.rollback",{kind="fish_species",id="fishing:carp"}).ok)
assert(not Gincy.Content.Exists("fish_species","fishing:carp"))
assert(not call("unknown").ok)
assert(not call("watch",{enabled="yes"}).ok)
assert(call("watch",{enabled=false}).ok)
assert(call("version").ok and call("version").value.product==Gincy.Version.Product)
assert(call("doctor").ok and call("doctor").value.result)
local commands={}
concommand={Add=function(id,fn) commands[id]=fn end,Remove=function(id) commands[id]=nil end}
dofile(root .. "/garrysmod/gamemodes/gincy/gamemode/core/sv_console.lua")
local printed={}
print=function(text) printed[#printed+1]=tostring(text) end
commands.gincy(nil,"gincy",{})
local dashboard=table.concat(printed,"\n")
assert(dashboard:find("Gincy Framework"),"dashboard")
printed={}
commands.gincy(nil,"gincy",{"help","module","disable"})
assert(table.concat(printed,"\n"):find("cascade"),"generated help")
print=function(...) io.write(...) io.write("\n") end
print("PASS management: " .. count .. " backend operations, deployment, rollback, persistent content, invalid input")
