# Gincy 3.2.0 — проверка поставки

Дата: 2026-09-28. Product 3.2.0 поверх существующего Core 3.1.

Проверено в source:

- Lua: console ASCII, bootstrap contract, GVM runtime, runtime v3 (85), disk protection
- Native win64: gvm_test, package_v2_test, `gincy doctor`, POST_BUILD deploy в `garrysmod/lua/bin`
- CMake presets win32/win64/linux32/linux64, `GINCY_EXPECT_ARCH`, static runtime option, delay-load libpq

Linux binaries в релизе — последние успешные native builds этой машины; после изменения GVM их нужно пересобрать из `source/`.

Python не требуется для production.
