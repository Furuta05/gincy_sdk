return {
    id = "survival_example",
    name = "gincy_survival_example",
    author = "Gincy",
    version = "1.0.0",
    requires={gincy=">=3.1.0 <4.0.0",api=">=1.1.0 <2.0.0"},
    dependencies = {characters = ">=1.0.0", inventory = ">=1.0.0", items = ">=1.0.0", attributes = ">=1.0.0", skills = ">=1.0.0", status = ">=1.0.0", worldobjects = ">=1.0.0", activities = ">=1.0.0", interactions = ">=1.0.0", director = ">=1.0.0"},
    capabilities = {"items.register", "attributes.register", "skills.register", "status.register", "world.register", "activities.register", "interactions.register", "commands.register", "permissions.register", "director.register", "persistence.read", "persistence.write"}
}
