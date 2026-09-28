function MODULE:Initialize(ctx)
    ctx.Interactions:Register("knock", {
        name = "Постучать", range = 100, duration = 0.3, cooldown = 1,
        CanUse = function(context) return context.target:GetClass() == "prop_door_rotating" or context.target:GetClass() == "func_door" end,
        Execute = function(context) context.target:EmitSound("physics/wood/wood_crate_impact_soft1.wav") end
    })
end
