local root=arg[1] or "."
SERVER,CLIENT=true,false
local hooks,timers,receivers,sent={},{},{},{}
local now=0
SysTime=os.clock CurTime=function() return now end
hook={Add=function(event,id,fn) hooks[event]=hooks[event] or {} hooks[event][id]=fn end,Remove=function(event,id) if hooks[event] then hooks[event][id]=nil end end,Run=function(event,...) for _,fn in pairs(hooks[event] or {}) do fn(...) end end}
timer={Create=function(id,_,_,fn) timers[id]=fn end,Remove=function(id) timers[id]=nil end}
concommand={Add=function() end,Remove=function() end}
ErrorNoHalt=function() end
IsValid=function(v) return type(v)=="table" and v.valid==true end
IsEntity=IsValid
Entity=function(v) return {valid=v==1,EntIndex=function() return v end} end
util={AddNetworkString=function() end,CRC=function(text) local hash=0 for n=1,#text do hash=(hash*31+text:byte(n))%4294967296 end return tostring(hash) end,SHA256=function(v) return string.rep(tostring(#v%10),64) end,TableToJSON=function() return "{}" end,JSONToTable=function() return {} end,Compress=function(v) return v end}
file={Read=function() end,Find=function() return {},{} end,Exists=function() return false end,CreateDir=function() end}
net={Receive=function(id,fn) receivers[id]=fn end,Start=function() end,WriteUInt=function() end,WriteData=function(data) sent[#sent+1]=data end,Send=function() end,SendToServer=function() end}
player={GetAll=function() return {} end}
CompileString=function(text,name) local fn,err=loadstring(text,name) return fn or err end
local c=root .. "/garrysmod/gamemodes/gincy/gamemode/core/"
for _,name in ipairs({"sh_version.lua","sh_api.lua","sh_schema.lua","sh_content.lua","sh_context.lua","sh_network.lua","sh_loader.lua"}) do dofile(c .. name) end
local I=Gincy.Internal
local assertions=0
local function check(ok,why) assert(ok,why) assertions=assertions+1 end
local function candidate(id,deps,source,caps)
    return {manifest={id=id,version="1.0.0",gincy=">=1.0.0 <2.0.0",dependencies=deps or {},capabilities=caps or {},reload="MODULE_HOT_SWAP",entrypoints={"sh_init.lua"}},files={{path="sh_init.lua",realm=2,data=source or "function MODULE:Initialize(ctx) ctx:Hook('test','a',function() end) end"}}}
end
local order,failures=I.Resolve({a=candidate("a",{b=">=1.0.0"}),b=candidate("b")})
check(#order==2 and order[1]=="b","dependency order")
order,failures=I.Resolve({a=candidate("a",{b=">=1.0.0"}),b=candidate("b",{a=">=1.0.0"})})
check(#order==0 and failures.a and failures.b,"cycle isolation")
order,failures=I.Resolve({hud=candidate("hud",{items=">=1.0.0"})})
check(failures.hud,"no implicit gameplay modules")
check(Gincy.Characters==nil and Gincy.Items==nil and I.DB==nil,"minimal no RPG or PostgreSQL")
I.Candidates={a=candidate("a",{b=">=1.0.0"}),b=candidate("b")}
I.Order={"b","a"}
local enabled=false
Gincy.Modules.Enable("a",function(ok) enabled=ok end)
check(enabled and I.Contexts.a and I.Contexts.b,"enable dependencies")
check(not Gincy.Modules.Disable("b"),"dependent blocker")
check(Gincy.Modules.Disable("b",true) and not next(I.Contexts) and not next(hooks.test),"cascade cleanup")
local schema={type="object",fields={name={type="string",maxLength=10},weight={type="number",min=0,default=1},tags={type="array",items="string",maxLength=4,optional=true}}}
check(Gincy.Content.RegisterType("fish_species",schema,"fishing"),"arbitrary type")
check(Gincy.Content.Register("fish_species","carp",{name="Carp"},"fishing"),"default normalized")
check(Gincy.Content.Get("fish_species","carp").weight==1,"schema defaults")
check(not Gincy.Content.Register("fish_species","bad",{name="x",weight=-1},"fishing"),"schema range")
check(not Gincy.Content.Reload({{kind="fish_species",id="carp",definition={name="Stolen"}}},"other"),"content ownership")
check(not Gincy.Content.Reload({{kind="fish_species",id="carp",definition={name="Changed"}},{kind="fish_species",id="broken",definition={name="Bad",weight=-1}}},"fishing"),"atomic prevalidation")
check(Gincy.Content.Get("fish_species","carp").name=="Carp","old content preserved")
check(Gincy.Content.Reload({{kind="fish_species",id="carp",definition={name="Changed"}}},"fishing"),"content reload")
check(Gincy.Content.Rollback("fish_species","carp","fishing") and Gincy.Content.Get("fish_species","carp").name=="Carp","content rollback")
check(not Gincy.Schema.Validate({type="array",items="integer",maxLength=3},{[1]=1,[3]=3}),"sparse array rejected")
local ctx=I.Context({id="owned",capabilities={"content.register","network.register"}},true)
ctx.Content:RegisterType("arbitrary",{type="object",fields={a="boolean"}})
ctx.Content:Register("arbitrary","one",{a=true})
ctx.Network:Register("sample",{schema={x={type="integer",min=0,max=9}},rate=4},function() end)
ctx:Hook("owned","one",function() end) ctx:Timer("one",1,0,function() end)
check(not Gincy.Content.Types.arbitrary and not Gincy.Network.Exists("owned.sample"),"staging isolated")
ctx:Activate()
check(Gincy.Content.Exists("arbitrary","one") and Gincy.Network.Exists("owned.sample"),"staging activated")
ctx:Shutdown()
check(not Gincy.Content.Types.arbitrary and not Gincy.Network.Exists("owned.sample") and not next(hooks.owned),"context cleanup")
I.Candidates={a=candidate("a",{},'function MODULE:Initialize(ctx) ctx:Hook("swap","h",function() return 1 end) end')}
I.Order={"a"}
Gincy.Modules.Enable("a",function(ok) check(ok,"reload baseline") end)
local before=I.Contexts.a
local bad=candidate("a",{},'function MODULE:Initialize(ctx) ctx:Hook("swap","h",function() end) error("candidate failed") end')
check(not Gincy.Modules.Reload("a",bad) and I.Contexts.a==before and before.context.active,"failed reload keeps old")
local good=candidate("a",{},'function MODULE:Initialize(ctx) ctx:Hook("swap","h",function() return 2 end) end')
check(Gincy.Modules.Reload("a",good) and I.Contexts.a~=before,"hot swap")
local count=0 for _ in pairs(hooks.swap) do count=count+1 end
check(count==1,"no duplicate hooks after swap")
local caps={"network.register"}
local netSource='function MODULE:Initialize(ctx) ctx.Network:Register("echo",{schema={value="boolean"},rate=2},function() end) end'
I.Candidates.route=candidate("route",{},netSource,caps) I.Order[#I.Order+1]="route"
Gincy.Modules.Enable("route",function(ok) check(ok,"route module") end)
check(Gincy.Modules.Reload("route",candidate("route",{},netSource,caps)),"route replacement")
check(Gincy.Network.Exists("route.echo"),"old cleanup did not erase new route")
check(I.NetMetrics.routes["route.echo"]~=nil,"route metrics survive replacement")
local conflict=candidate("route",{},'function MODULE:Initialize(ctx) ctx.Network:Register("echo",{schema={value="boolean"},rate=2},function() end) ctx.Content:RegisterType("fish_species",{type="object",fields={}}) end',{"network.register","content.register"})
check(not Gincy.Modules.Reload("route",conflict),"activation conflict rejected")
check(Gincy.Network.Exists("route.echo") and I.NetMetrics.routes["route.echo"],"route and metrics restored on activation failure")
local brokenLegacy=candidate("broken_legacy",{},'Gincy.LegacyProbe={created=true} Gincy.Modules.Probe=true error("legacy failure")')
brokenLegacy.firstparty=true
I.Candidates.broken_legacy=brokenLegacy
Gincy.Modules.Enable("broken_legacy",function(ok) check(not ok,"legacy initialization failure") end)
check(Gincy.LegacyProbe==nil and Gincy.Modules.Probe==nil,"partial legacy globals restored")
check(not I.Contexts.broken_legacy,"failed legacy context not installed")
local N=Gincy.Network
local wire={type="object",fields={a={type="integer",min=-10,max=100},b="boolean",v="number",s={type="string",maxLength=20},e={type="enum",values={"first","second"}},optional={type="varint",optional=true},arr={type="array",maxLength=4,items="number"}}}
for _,value in ipairs({0,1,-1,0.5,1e-200,1e200,2^-1074}) do
    local data,err=N.Encode(wire,{a=4,b=true,v=value,s="hello",e="second",arr={1,2,3}})
    check(data~=nil,err)
    local ok,out=N.Decode(wire,data)
    check(ok and out.v==value and out.a==4 and out.arr[3]==3,"binary roundtrip")
    check(not N.Decode(wire,data .. "x"),"trailing bytes rejected")
    check(not N.Decode(wire,data:sub(1,-2)),"truncation rejected")
end
check(not N.Encode({type="number"},math.huge),"nonfinite packet rejected")
check(not N.Decode({type="varint"},string.char(255,255,255,255,16)),"overflow varint")
local received=0
check(N.Register("test.event",{schema={value={type="integer",min=0,max=10}},rate=2,direction="both",handler=function() received=received+1 end},"test"),"generic route")
local peer={valid=true}
for n=1,4 do check(N.SendRoute(peer,"test.event",{value=n}),"queue event") end
N.Flush()
check(#sent==1,"batching messages")
check(N.Receive(sent[1],peer),"receive batch")
check(received==2 and I.NetMetrics.rejected>=2,"route rate enforcement")
check(not N.Receive(sent[1],peer),"sequence replay rejected")
check(not N.Receive(string.rep('x',50000),peer),"packet limit")
local full=N.Delta(nil,{a=1,b=2},0)
local ok,state,revision=N.ApplyDelta({},0,full)
check(ok and state.b==2 and revision==1,"full state")
local delta=N.Delta(state,{a=2},revision)
ok,state,revision=N.ApplyDelta(state,revision,delta)
check(ok and state.a==2 and state.b==nil,"generic delta removal")
check(not N.ApplyDelta({},0,delta),"resync detection")
local response
check(N.Register("test.rpc",{schema={x="boolean"},response={type="boolean"},direction="both",rate=10,handler=function() return true end},"test"),"RPC route")
check(N.Request(peer,"test.rpc",{x=true},function(ok,value) response=value end,0.1),"RPC request")
now=1 timers['Gincy.NetworkTimeouts']()
check(response=="TIMEOUT","RPC timeout cleanup")
check(N.StreamBegin("test","payload",6,util.SHA256("abcdef")),"stream begin")
check(N.StreamChunk("test","payload",0,"abc"),"stream chunk")
local success,payload=N.StreamChunk("test","payload",3,"def")
check(success and payload=="abcdef","stream completion")
check(not N.StreamBegin("test","big",1048577,string.rep('a',64)),"stream total limit")
check(N.StreamBegin("test","bad",6,string.rep('a',64)),"stream new")
check(not N.StreamChunk("test","bad",1,"abc"),"stream offset validation")
print('PASS runtime v3: ' .. assertions .. ' assertions')
