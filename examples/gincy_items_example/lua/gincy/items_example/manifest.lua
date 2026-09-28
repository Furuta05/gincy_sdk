return {
    id = "items_example",
    name = "gincy_items_example",
    author = "Gincy",
    version = "1.0.0",
    requires={gincy=">=3.1.0 <4.0.0",api=">=1.1.0 <2.0.0"},
    dependencies = {items = ">=1.0.0", equipment = ">=1.0.0"},
    capabilities = {"items.register", "equipment.register"}
}
