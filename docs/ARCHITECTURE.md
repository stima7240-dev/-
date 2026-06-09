# AeroMesh Architecture

This document tracks the realized design and the planned path. It follows the
AeroMesh technical spec, staged so that each layer is testable before the next
is built.

## Layered overview

```
  +---------------------------------------------------------+
  |  Qt desktop client  /  mobile light client             |  (stage 4)
  +---------------------------------------------------------+
  |  Messaging: chat sessions, contacts, local store       |  (stage 2)
  |  E2E session: Double Ratchet (X25519 + Kyber hybrid)    |
  +---------------------------------------------------------+
  |  Overlay: Kademlia DHT, mailbox/store-and-forward,      |  (stage 1-3)
  |  NAT traversal (STUN/TURN/ICE), onion/relay transport   |
  +---------------------------------------------------------+
  |  Frame layer: fixed 1400-byte padded frames + cover     |  (DONE)
  |  traffic                                                |
  +---------------------------------------------------------+
  |  Crypto primitives: libsodium (Ed25519 / X25519 /       |  (DONE)
  |  XChaCha20-Poly1305 / BLAKE2)                            |
  +---------------------------------------------------------+
```

## Implemented modules

### Frame layer (`core/include/aeromesh/packet.hpp`)

- Every frame is exactly `kFrameSize` (1400) bytes.
- Layout: `[1B type][2B big-endian payload length][payload][random padding]`.
- The entire frame is pre-filled with CSPRNG bytes, so the padding tail is
  indistinguishable from ciphertext. A 10-byte ACK and a 1397-byte file shard
  are byte-for-byte identical in size on the wire.
- `make_dummy_frame()` produces cover traffic. Combined with randomized send
  timing (planned in the transport layer) this defeats simple traffic-volume
  and timing-correlation analysis.

### Cryptographic identity (`core/include/aeromesh/identity.hpp`)

- Identity = an Ed25519 keypair. The public key **is** the address; there is no
  registration, phone number, or email.
- `share_string()` / `parse_share_string()` encode the public key for QR or
  copy-paste contact exchange.
- `fingerprint()` gives a short human-comparable value for out-of-band identity
  verification (defeats MITM during contact add).
- `x25519_public()` converts the Ed25519 identity to a Curve25519 key to seed
  the key-agreement / ratchet layer.
- Secret keys are zeroized after use (`sodium_memzero`) and only ever persisted
  to the local encrypted store, never sent over the network.

## Planned modules (next)

1. **Kademlia DHT** (`dht.hpp`) — XOR-distance routing table, `FIND_NODE` /
   `FIND_VALUE`, iterative lookups. Bootstrap via seed nodes, then mDNS/UDP
   broadcast for local discovery, then DNS TXT fallback.
2. **Double Ratchet session** (`ratchet.hpp`) — Signal-style ratchet for forward
   secrecy + break-in recovery, with an X25519 + Kyber hybrid handshake for
   post-quantum resistance.
3. **File pipeline** — local AEAD encryption, fixed-size chunking, Reed-Solomon
   erasure coding (N data + M parity), and the ACK-destroy delivery protocol
   (12h TTL on relays, wipe on signed ACK).
4. **Transport** — framed TCP/QUIC with TLS-style obfuscation, relay fallback
   when direct P2P fails, cover-traffic scheduler.

## Design rules

- Crypto comes from libsodium; we do not hand-roll primitives.
- No assembly until a profiler proves a hot path needs it. The spec's AVX-512 /
  NASM goals are deferred — correctness and a clean, testable API first.
- Every layer ships with tests before the next layer depends on it.
