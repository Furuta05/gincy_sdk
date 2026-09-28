function MODULE:Initialize(ctx)
    ctx.Content:Register("skillset", "blood_magic", {name = "Blood Magic", tags = {"arcane", "vital"}})
    ctx.Content:Register("skill", "fireball", {name = "Fireball", skillset = "blood_magic", damage = 75, mana = 30, cooldown = 4, range = 850})
    ctx.Content:Register("item", "arcane_focus", {name = "Arcane Focus", stack = 1, weight = 1, movement = {speed = 0.98}})
    ctx.Content:Register("status", "burning", {name = "Burning", duration = 8, intensity = 2})
end
