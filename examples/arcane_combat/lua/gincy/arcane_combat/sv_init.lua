function MODULE:Initialize(ctx)
    local config = ctx.Config:Register({cooldown_multiplier = {type = "number", default = 1, min = 0.1, max = 5}})
    ctx.Network:Register("cast", {schema = {spell = "string", target = "entity?"}, rate = 8}, function(client, data)
        local character = Gincy.Characters.Get(client)
        local spell = Gincy.Content.Get("skill", data.spell)
        if not character or not spell or not Gincy.Content.Exists("skillset", spell.skillset) then return end
        character:SetData("arcane.last_cast", {spell = data.spell, at = os.time()}, function(ok, err)
            if ok then Gincy.Network.Send(client, "arcane.cast", {spell = data.spell, cooldown = spell.cooldown * config.cooldown_multiplier}) end
        end)
    end)
    ctx.Interactions:Register("focus", {name = "Focus", range = 96, duration = 0.5, cooldown = 2, CanUse = function(context) return context.target:IsPlayer() end, Execute = function(context) context.player:ChatPrint("Arcane focus ready") end})
    ctx:GetLogger():Info("Arcane combat ready")
end
