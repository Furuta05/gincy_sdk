function MODULE:Initialize(ctx)
    hook.Add("GincyNetworkMessage", "ArcaneCombatNotice", function(topic, data)
        if topic == "arcane.cast" then chat.AddText(Color(190, 120, 255), data.spell, Color(220, 220, 220), " ready in ", tostring(data.cooldown), " seconds") end
    end)
    ctx:GetLogger():Info("Arcane combat UI ready")
end
