function MODULE:Initialize(ctx)
    local schemas={
        fish_species={type="object",fields={name={type="string",maxLength=80},weight={type="number",min=0,max=5000},rarity={type="enum",values={"common","rare","legendary"}},tags={type="array",items={type="string",maxLength=24},maxLength=12,default={}}}},
        rod_profiles={type="object",fields={name={type="string",maxLength=80},strength={type="number",min=0,max=1000}}},
        water_zones={type="object",fields={name={type="string",maxLength=80},depth={type="number",min=0,max=10000}}},
        bait_types={type="object",fields={name={type="string",maxLength=80},attraction={type="number",min=0,max=1}}}
    }
    for kind,schema in pairs(schemas) do ctx.Content:RegisterType(kind,schema) end
    ctx.Content:Register("fish_species","quantum_carp",{name="Quantum carp",weight=3,rarity="rare"})
    ctx.Content:Register("rod_profiles","starter",{name="Starter rod",strength=10})
    ctx.Content:Register("water_zones","harbour",{name="Harbour",depth=40})
    ctx.Content:Register("bait_types","worm",{name="Worm",attraction=0.4})
end
function MODULE:HealthCheck(ctx)
    return ctx.ID=="quantum_fishing"
end
