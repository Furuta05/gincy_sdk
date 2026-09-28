# Gincy security model

This document separates what Gincy actually guarantees from what it does not.
It does not use claims such as uncrackable, impossible to dump, or 100% secure.

## Layers

| Layer | Goal | Trust boundary |
| --- | --- | --- |
| Server-side confidentiality | Protected server Lua is decrypted in native memory and loaded into the server Lua VM. Plaintext is not written to disk. | The SRCDS host is trusted more than players, but the machine owner can still instrument the process. |
| Client-side anti-extraction | Client logic is compiled to GVM bytecode, delivered in RAM, optionally session-mapped, and never stored as reusable `.lua` in `addons/`, `data/`, `download/`, or `cache/`. | The player process is untrusted. |
| Package integrity | Per-entry SHA-256 plus AES-GCM tags. Tamper fails closed. | Authenticated metadata and payloads. |
| Package authenticity | Ed25519 signatures over the whole container, including protection policy. | Trusted publisher keys in `data/gincy/trust/`. |
| DRM / entitlement | Optional offline lease on top of protection. Marketplace is not required. A missing or expired lease refuses that package, not the whole server. | Signed lease files under `data/gincy/trust/leases/`. |
| Fingerprinting | Optional build or recipient-bound digest mixed into signed metadata. Removing one string does not remove the fingerprint. | Present only when the developer enables it. |

Protection policy (`open` / `protected` / `maximum`, `drm`, `unpack`) is part of signed metadata. Changing `maximum` to `open` after signing invalidates the package.

## Protection modes

- `open` — plaintext payload, still signed.
- `protected` — comments stripped, per-entry authenticated encryption, client GVM when the subset compiles. Fallback GLua is allowed only with `--allow-fallback`.
- `maximum` — same pipeline; unsupported GLua fails the build. Session representation is used when sending client programs.
- `drm` — optional entitlement layer. Not always-online.

## Threats

### Threat A — file / cache copier

A player copies `addons/`, `data/`, `download/`, and `cache/`.
Protected client source must not be there in reusable plaintext form.
The client runtime is ordinary GLua shipped with the gamemode. It does not contain package master keys.

### Threat B — network capture

A player captures ordinary traffic.
They should not receive original GLua source. They may receive session-specific GVM bytecode.

### Threat C — runtime analysis

A player inspects the Gincy client VM.
The system raises the cost of recovering a reusable canonical package. It does not hide the fact that the CPU must execute something.

### Threat D — process instrumentation

An attacker who controls the Garry's Mod process can dump memory and hook APIs.
This is assumed possible. Client protection is anti-extraction, not confidentiality.

### Threat E — SRCDS owner

The person who owns the server machine can attach a debugger to SRCDS.
Protected packages raise extraction cost. They do not make reverse engineering physically impossible.

## What developers should keep on the server

Authoritative logic, secrets, economy validation, permissions, anti-cheat decisions, and valuable algorithms belong on the server.

Client code should stay UI, presentation, input, animation, non-sensitive prediction, and temporary protected control logic.

## Cryptography

Gincy uses OpenSSL primitives only: Ed25519, RSA-OAEP, AES-256-GCM, SHA-256, CSPRNG.
There is no homemade cipher.
Private identity material is created under `data/gincy/identity/`, is not logged, is not shown in WebUI, and is not included in diagnostic dumps.
