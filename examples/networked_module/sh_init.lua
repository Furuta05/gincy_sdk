function MODULE:Initialize(ctx)
    ctx.Network:Register("ping", {
        schema={sequence={type="integer",min=0,max=65535}},
        response={type="integer",min=0,max=65535},
        direction="c2s",rate=2,priority="REALTIME"
    }, function(_,data) return data.sequence end)
    if CLIENT then
        local sequence=0
        ctx:Timer("ping",5,0,function()
            sequence=(sequence+1)%65536
            Gincy.Network.Request(nil,"networked_module.ping",{sequence=sequence},function(ok,value)
                if ok then ctx:GetLogger():Debug("Echo " .. value) end
            end,3)
        end)
    end
end
