# AeroMesh 🛡️

> Decentralized, end-to-end encrypted, censorship-resistant P2P messenger.
> No central servers, no phone numbers. Inspired by Briar, Tor and Signal.

**Status:** early development (v0.1) — core framing + crypto identity landed.
This is a from-scratch **C++23** core with a planned **Qt** desktop client.

## Why

Centralized messengers can be blocked, surveilled, and pressured. AeroMesh moves
routing and storage to a peer-to-peer overlay so there is no single point to
block or seize. Your identity is a cryptographic key, not a phone number.

## What works today

- **Uniform frame layer** (`core/.../packet.hpp`) — every packet, real or cover,
  is padded to a fixed 1400 bytes so a passive observer sees one monotonous
  stream. Includes cover-traffic (dummy) frames.
- **Cryptographic identity** (`core/.../identity.hpp`) — Ed25519 identity keys,
  QR/share-string contact exchange, fingerprints, and X25519 derivation for the
  (planned) Double Ratchet session layer. Built on libsodium.
- A dependency-free **test suite** (`ctest`).

## Roadmap (per the AeroMesh spec)

See `docs/ARCHITECTURE.md` for the full design and `docs/THREAT_MODEL.md` for an
honest statement of what AeroMesh does and does **not** protect against.

| Stage | Module | Status |
|------|--------|--------|
| 1 | Frame layer + identity | ✅ done |
| 1 | Kademlia DHT peer discovery | 🔜 next |
| 2 | Double Ratchet E2E session (Kyber/X25519 hybrid) | planned |
| 2 | File sharding (Reed-Solomon) + ACK-destroy delivery | planned |
| 2 | NAT traversal (STUN/TURN/ICE) | planned |
| 3 | Onion/relay transport + traffic obfuscation | research |
| 3 | AeroCoin ledger (Proof-of-Storage/Bandwidth) | research |
| 3 | Fiat P2P exchange / escrow | ⚠️ see note below |
| 4 | Qt desktop client | planned |
| 4 | Mobile clients (interval polling) | planned |

## Build

Requires a C++23 compiler (GCC 13+/Clang 17+/MSVC 19.38+), CMake ≥ 3.25, and
libsodium.

```bash
# install libsodium first (apt: libsodium-dev / brew: libsodium / vcpkg: libsodium)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## ⚠️ Note on the built-in coin and fiat exchange

The spec includes an in-app privacy coin (AeroCoin) and a fiat↔crypto P2P
exchange. Building the code is one thing; **operating** an anonymous fiat
on-ramp is regulated money-transmission in most jurisdictions and carries real
AML/licensing obligations. Treat the `exchange/` module as research and get
legal review before any public deployment.

## License

GPL-3.0 (see `LICENSE`).
"# -" 
