# Gincy 3.2.0 native build

Work from this `source/` directory (or `native/` in a mixed checkout).

## Windows

vcpkg triplets: `x86-windows-static` and `x64-windows-static`.
Set `CMAKE_TOOLCHAIN_FILE` to `vcpkg/scripts/buildsystems/vcpkg.cmake`.

```text
cmake -S . -B ../build-win32 -A Win32 -DGINCY_EXPECT_ARCH=32 -DGINCY_STATIC_RUNTIME=ON -DVCPKG_TARGET_TRIPLET=x86-windows-static -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake
cmake --build ../build-win32 --config Release

cmake -S . -B ../build-win64 -A x64 -DGINCY_EXPECT_ARCH=64 -DGINCY_STATIC_RUNTIME=ON -DVCPKG_TARGET_TRIPLET=x64-windows-static -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake
cmake --build ../build-win64 --config Release
```

`GINCY_EXPECT_ARCH` refuses a 64-bit generator when you asked for win32.
Release copies `gmsv_gincy_core_win32.dll` / `gmsv_gincy_core_win64.dll` into `garrysmod/lua/bin/`.

`/MT` is used when `GINCY_STATIC_RUNTIME=ON`. `libpq` is delay-loaded so the module starts without PostgreSQL DLLs.

## Linux

```text
cmake -S . -B ../build-linux64 -DCMAKE_BUILD_TYPE=Release -DGINCY_EXPECT_ARCH=64
cmake --build ../build-linux64
```

32-bit Linux needs a 32-bit toolchain (`-m32` and 32-bit libssl/libpq).

## Tests

```text
ctest --test-dir ../build-win64 -C Release
lua51 tests/gvm_runtime.lua .
lua51 tests/console_ascii.lua .
lua51 tests/bootstrap.lua .
lua51 tests/runtime_v3.lua .
```
