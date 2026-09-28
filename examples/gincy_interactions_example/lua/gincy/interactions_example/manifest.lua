return {
    id = "interactions_example",
    name = "gincy_interactions_example",
    author = "Gincy",
    version = "1.0.0",
    requires={gincy=">=3.1.0 <4.0.0",api=">=1.1.0 <2.0.0"},
    dependencies = {interactions = ">=1.0.0", characters = ">=1.0.0"},
    capabilities = {"interactions.register"}
}
