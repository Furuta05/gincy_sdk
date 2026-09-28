# Gincy 3.2.0

Исходный код native-модуля Gincy для Garry's Mod.

## Сборка

Требуется:

- CMake
- Visual Studio с C++ toolchain
- vcpkg
- Windows x86 или x64 toolchain

Перейдите в корень `source/`.

### Windows x86

```bat
cmake --preset win32-release
cmake --build --preset win32-release
```

Результат:

```text
garrysmod/lua/bin/gmsv_gincy_core_win32.dll
```

### Windows x64

```bat
cmake --preset win64-release
cmake --build --preset win64-release
```

Результат:

```text
garrysmod/lua/bin/gmsv_gincy_core_win64.dll
```

Для статической сборки используются:

```text
x86-windows-static
x64-windows-static
```

и:

```text
GINCY_STATIC_RUNTIME=ON
```

### Linux x64

```bash
cmake --preset linux64-release
cmake --build --preset linux64-release
```

## Версия

Версия проекта хранится в:

```text
version.json
```

Синхронизация файлов версии:

```bat
cmake -P tools/release.cmake
```

Установка новой версии:

```bat
cmake -DVERSION=3.2.1 -P tools/release.cmake
```

После сборки готовый native-модуль автоматически помещается в:

```text
garrysmod/lua/bin/
```
