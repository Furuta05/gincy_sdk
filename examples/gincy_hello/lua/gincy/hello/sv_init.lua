function MODULE:Initialize(ctx)
    ctx.Interactions:Register("hello", {
        name = "Поздороваться", range = 100, duration = 0.3, cooldown = 1,
        CanUse = function(context) return context.target:IsPlayer() end,
        Execute = function(context) context.player:ChatPrint("Hello, " .. context.character:GetName() .. "!") end
    })
    ctx:GetLogger():Info("Hello addon ready")
end
