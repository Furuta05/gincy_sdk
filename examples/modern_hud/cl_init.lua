function MODULE:Initialize(ctx)
    ctx:Hook("HUDPaint", "identity", function()
        local ply=LocalPlayer()
        if not IsValid(ply) then return end
        surface.SetDrawColor(18,23,32,225)
        surface.DrawRect(24,ScrH()-80,280,52)
        draw.SimpleText(ply:Nick(),"DermaDefaultBold",40,ScrH()-64,Color(218,231,251))
        draw.SimpleText("Gincy / " .. game.GetMap(),"DermaDefault",40,ScrH()-45,Color(128,154,193))
    end)
end
