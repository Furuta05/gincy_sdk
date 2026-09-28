local function evaluate(value)
    return math.floor(math.sqrt(value*value+17)*100)/100
end
function MODULE:Initialize(ctx)
    ctx:Timer("calculation",10,0,function()
        ctx:GetLogger():Debug("Result: " .. evaluate(CurTime()))
    end)
end
