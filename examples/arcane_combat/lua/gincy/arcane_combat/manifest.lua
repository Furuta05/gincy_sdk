return {
    id = "arcane_combat",
    name = "Arcane Combat",
    author = "Gincy",
    version = "1.0.0",
    requires={gincy=">=3.1.0 <4.0.0",api=">=1.1.0 <2.0.0"},
    dependencies = {skills = ">=1.0.0", characters = ">=1.0.0", attributes = ">=1.0.0", interactions = ">=1.0.0"},
    capabilities = {"content.register", "network.register", "interactions.register", "persistence.read", "persistence.write"}
}
