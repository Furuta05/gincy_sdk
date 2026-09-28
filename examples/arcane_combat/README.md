# Arcane Combat

Arcane Combat demonstrates a system module style addon using only Gincy public APIs. It defines a skillset, skills, an item, a status, a server-authoritative cast route, persistent character data, an interaction and a client notice.

`fireball` is content registered by the addon; changing its damage, mana, cooldown or range does not require rebuilding the framework or the `.gmod` container. In development mode the same definitions can be placed in `gincy_dev/content/` and reloaded transactionally.
