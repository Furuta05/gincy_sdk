function MODULE:Initialize(ctx)
    self:RegisterDefinitions(ctx)
    local config = ctx.Config:Register({gather_duration = {type = "number", default = 2, min = 0.5, max = 10}})
    local log = ctx:GetLogger()
    local function resource(context)
        local handle = Gincy.WorldObjects.GetByEntity(context.target)
        return handle and handle:GetType() == "resource_node" and handle:HasPermission(context.character, "gather")
    end
    ctx.Interactions:Register("gather", {
        name = "Собрать древесину", range = 110, duration = config.gather_duration, cooldown = 2,
        CanUse = resource,
        Execute = function(context)
            local handle = Gincy.WorldObjects.GetByEntity(context.target)
            handle:GetInventory(function(ok, err, inventory)
                if not ok then context.player:ChatPrint(err.code) return end
                inventory:Transfer(context.character:GetInventory(), "wood", 1, function(transferred, failure)
                    if not transferred then if IsValid(context.player) then context.player:ChatPrint(failure.code) end return end
                    context.character:AddSkillXP("gathering", 10, function(saved)
                        if saved then context.character:AddStatus("exhausted", {duration = 8, source = "gathering"}) end
                    end)
                end)
            end)
        end
    })
    ctx.Interactions:Register("deliver", {
        name = "Сдать древесину", range = 110, duration = 1, cooldown = 1,
        CanUse = resource,
        Execute = function(context)
            Gincy.Activities.List(context.character, function(ok, err, activities)
                if not ok then return end
                if not activities[1] then context.player:ChatPrint("Сначала: survival_example.job") return end
                activities[1]:Complete(context.character, function(completed, failure) if IsValid(context.player) then context.player:ChatPrint(completed and "Поставка завершена: +25" or failure.code) end end)
            end)
        end
    })
    ctx.Permissions:Register("survival.spawn", {default = "superadmin"})
    ctx.Permissions:Register("survival.job", {default = "everyone"})
    ctx.Commands:Register("node", {
        permission = "survival.spawn", arguments = {},
        Execute = function(command)
            if not IsValid(command.player) or not command.character then command:Reply("Select a character first") return end
            local trace = command.player:GetEyeTrace()
            if command.player:GetPos():DistToSqr(trace.HitPos) > 400 ^ 2 then return end
            Gincy.WorldObjects.Create("resource_node", {position = trace.HitPos + Vector(0, 0, 16), owner = command.character, permissions = {gather = "everyone"}}, function(ok, err, handle)
                if not ok then command:Reply(err.code) return end
                handle:GetInventory(function(loaded, failure, inventory) if loaded then inventory:Add("wood", 20, function(added, errorValue) command:Reply(added and "Resource node ready" or errorValue.code) end) end end)
            end)
        end
    })
    ctx.Commands:Register("job", {
        permission = "survival.job", arguments = {},
        Execute = function(command)
            if not command.character then command:Reply("Select a character first") return end
            Gincy.Activities.List(command.character, function(ok, err, existing)
                if not ok or #existing > 0 then command:Reply("An active delivery already exists, or database unavailable") return end
                Gincy.Activities.Create("resource_delivery", command.character, {}, function(created, failure) command:Reply(created and "Deliver 5 wood to a resource node" or failure.code) end)
            end)
        end
    })
    ctx.Director:RegisterRule("survival.audit", {
        priority = 1, cooldown = 60,
        condition = function() return #player.GetAll() > 0 end,
        action = function() ctx.Storage:Set("last_active", {time = os.time()}, function(ok, err) if not ok then log:Warn(err.code) end end) end
    })
    log:Info("Survival addon ready")
end
