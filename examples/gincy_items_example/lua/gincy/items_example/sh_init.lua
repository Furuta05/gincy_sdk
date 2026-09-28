function MODULE:Initialize(ctx)
    ctx.Equipment:Register("body", {name = "Body"})
    ctx.Items:Register("canvas_coat", {name = "Canvas coat", stack = 1, weight = 2, model = "models/props_c17/SuitCase_Passenger_Physics.mdl", slot = "body", movement = {speed = 0.95}, damageScale = 0.9, attributes = {endurance = 2}})
    ctx.Items:Register("copper_token", {name = "Copper token", stack = 100, weight = 0.01, model = "models/props_lab/box01a.mdl"})
end
