#pragma once

// Hybrid post-quantum handshake (Signal PQXDH-style) that produces the 32-byte
// shared secret used to seed the Double Ratchet.
//
// The shared secret combines two independent key agreements:
//   * classical X25519 Diffie-Hellman, and
//   * a post-quantum KEM (ML-KEM-768 / Kyber768 via liboqs).
// They are concatenated and run through a BLAKE2b KDF. The result is at least
// as strong as the stronger primitive: an adversary must break BOTH X25519 and
// ML-KEM to recover the session key. This defends against "harvest now,
// decrypt later" -- traffic captured today stays sealed even against a future
// quantum computer.
//
// Build modes:
//   * AEROMESH_HAVE_PQ defined  -> true hybrid (X25519 + ML-KEM-768).
//   * AEROMESH_HAVE_PQ undefined -> graceful classical-only fallback (X25519),
//     so the project still builds where liboqs is unavailable. The KEM fields
//     are simply empty in that mode.

#include <array>
#include <cstddef>
#include <expected>
#include <span>
#include <vector>

namespace aeromesh {

inline constexpr std::size_t kHybridX25519Len = 32;
inline constexpr std::size_t kHybridSecretLen = 32;

enum class PqError {
    InvalidKeyLength,
    KemUnavailable,    // PQ requested but liboqs/ML-KEM not built in
    KeygenFailed,
    EncapsFailed,
    DecapsFailed,
    DhFailed,
    Malformed,
};

// Returns true when this build links a real ML-KEM implementation.
bool pq_available();

// Algorithm label for diagnostics, e.g. "X25519+ML-KEM-768" or
// "X25519 (classical only)".
const char* hybrid_suite_name();

// Responder's long-lived (or prekey) hybrid key material.
struct HybridResponderKeys {
    std::array<std::byte, kHybridX25519Len> x25519_pub{};
    std::array<std::byte, kHybridX25519Len> x25519_sec{};
    std::vector<std::byte> kem_pub;   // ML-KEM-768 public key (empty if no PQ)
    std::vector<std::byte> kem_sec;   // ML-KEM-768 secret key (empty if no PQ)
};

// The public half of HybridResponderKeys, published as a prekey bundle.
struct HybridPrekeyBundle {
    std::array<std::byte, kHybridX25519Len> x25519_pub{};
    std::vector<std::byte> kem_pub;   // empty if no PQ
};

// What the initiator sends to the responder, plus the derived secret.
struct HybridInitiation {
    std::array<std::byte, kHybridSecretLen> shared_secret{};
    std::array<std::byte, kHybridX25519Len> x25519_eph_pub{};
    std::vector<std::byte> kem_ciphertext;  // empty if no PQ
};

// Responder: generate fresh hybrid key material.
std::expected<HybridResponderKeys, PqError> generate_responder_keys();

// Extract the publishable prekey bundle from responder keys.
HybridPrekeyBundle prekey_bundle(const HybridResponderKeys& keys);

// Initiator: given the responder's bundle, derive the shared secret and the
// material to transmit (ephemeral X25519 public key + KEM ciphertext).
std::expected<HybridInitiation, PqError> hybrid_initiate(
    const HybridPrekeyBundle& bundle);

// Responder: recover the same shared secret from the initiator's message.
std::expected<std::array<std::byte, kHybridSecretLen>, PqError> hybrid_respond(
    const HybridResponderKeys& keys,
    std::span<const std::byte> initiator_x25519_eph_pub,
    std::span<const std::byte> kem_ciphertext);

} // namespace aeromesh
