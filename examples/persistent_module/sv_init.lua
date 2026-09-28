function MODULE:Initialize(ctx)
    ctx:Timer("record_uptime",30,0,function()
        ctx.Storage:Set("heartbeat",{map=game.GetMap(),at=os.time()},function(ok,err)
            if not ok then ctx:GetLogger():Warn(err.code) end
        end)
    end)
end
