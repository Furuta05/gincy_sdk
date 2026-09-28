function MODULE:Initialize(ctx)
    self:RegisterDefinitions(ctx)
    ctx:GetLogger():Info("Use gincy_interact while aiming at a resource node")
end
