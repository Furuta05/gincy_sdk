# Gincy 3.2.0

Self-contained Garry's Mod platform. Production does not need Python, pip, extra runtime DLLs next to the module (when built static), or a separate WebUI process.

## Install (server owner)

Copy `garrysmod/` into `<SRCDS>/garrysmod/`, then start SRCDS with `+gamemode gincy`.

Required files:

```text
garrysmod/gamemodes/gincy/
garrysmod/lua/bin/gmsv_gincy_core_<platform>.dll
```

Windows x86: `gmsv_gincy_core_win32.dll`
Windows x64: `gmsv_gincy_core_win64.dll`
Linux x86: `gmsv_gincy_core_linux.dll`
Linux x64: `gmsv_gincy_core_linux64.dll`

Players do not install a Gincy client DLL.

On first start Gincy creates `gincy_modules/`, `gincy_content/`, `gincy_dev/`, and `data/gincy/...`. PostgreSQL is optional. If no persistence module is enabled, storage reports `DISABLED - not required`.

## Native build (framework developer)

From `source/` (or `native/` in a checkout that still has that layout):

```text
cmake --preset win32-release
cmake --build --preset win32-release

cmake --preset win64-release
cmake --build --preset win64-release
```

Static Windows builds expect vcpkg `x86-windows-static` / `x64-windows-static` and `GINCY_STATIC_RUNTIME=ON`.
Release binaries are copied to `garrysmod/lua/bin/` by the CMake POST_BUILD step.

Linux:

```text
cmake --preset linux64-release
cmake --build --preset linux64-release
```

Versions come from `version.json`. Sync derived files:

```text
cmake -P tools/release.cmake
cmake -DVERSION=3.2.1 -P tools/release.cmake
```

## Developer workflow

Work in `gincy_dev/modules/MyModule/` with ordinary GLua (`manifest.lua`, `sv_*.lua`, `cl_*.lua`).
Dev mode stays plaintext with hot reload and normal stack traces.

Production package:

```text
gincy package build MyModule --key signer.pem --output MyModule-1.0.0.gmod --maximum
gincy package inspect MyModule-1.0.0.gmod --public-key signer.pub.pem
gincy package verify MyModule-1.0.0.gmod --public-key signer.pub.pem
```

Put the `.gmod` in `garrysmod/gincy_modules/` and trust the public key in `data/gincy/trust/`.
Gincy verifies, then loads. Decrypted source is not written to disk.

## Protection

- `open` — signed plaintext
- `protected` — per-entry AES-GCM, server Lua stripped, client GVM
- `maximum` — same, build fails on unsupported client GLua
- `drm` — optional offline entitlement lease

Client protection is anti-extraction, not confidentiality. See `docs/SECURITY.md`.

## Console

ASCII-only on Windows SRCDS:

```text
gincy
gincy help
gincy status
gincy doctor
gincy version
gincy clientvm profile
gincy errors explain GINCY-PKG-SIGNATURE-INVALID
```

JSON only with `--json`.
