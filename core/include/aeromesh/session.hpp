#pragma once

// SecureSession: the authenticated, post-quantum, forward-secret channel that
// ties the project's cryptographic primitives into one end-to-end secure
// session between two long-term identities.
//
// Threats addressed here:
//   * Man-in-the-middle: the responder's prekey bundle and the initiator's
//     initiation are each signed by their long-term Ed25519 identity key. The
//     initiator's signature additionally covers a transcript that binds the
//     responder's exact key material, and the initiator pins the responder's
//     identity out-of-band. Swapping any key in transit breaks a signature.
//   * Impersonation: each side learns and verifies the other's long-term
//     identity public key, so you always know who you are talking to.
//   * Harvest-now-decrypt-later: the shared secret is the hybrid X25519 +
//     ML-KEM-768 output (see pqhandshake.hpp).
//   * Key compromise: the session is driven by the Double Ratchet, giving
//     forward secrecy and post-compromise security on every message.
//
// Handshake (one round trip):
//   responder: create_responder(self)        -> publish bundle()
//   initiator: initiate(self, peer_id, bundle) -> {session, initiation}
//   responder: accept(responder, initiation)  -> session
// After that both sides use encrypt() / decrypt().

#include <array>
#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "aeromesh/identity.hpp"
#include "aeromesh/pqhandshake.hpp"
#include "aeromesh/ratchet.hpp"

namespace aeromesh {

enum class SessionError {
    HandshakeFailed,
    BadSignature,    // a signature did not verify / identity mismatch (MITM)
    Malformed,
    NotEstablished,
    EncryptFailed,
    DecryptFailed,
    CryptoFailure,
};

// Published by the responder. Every field is public and safe to transmit or
// store in the DHT. Authenticated by `signature` under `identity_pub`.
struct SignedPrekeyBundle {
    std::array<std::byte, kPublicKeyLen> identity_pub{};   // responder Ed25519
    std::array<std::byte, kHybridX25519Len> x25519_pub{};  // hybrid X25519 prekey
    std::vector<std::byte> kem_pub;                        // ML-KEM pub (may be empty)
    RatchetKey ratchet_pub{};                              // responder initial ratchet pub
    std::array<std::byte, kSignatureLen> signature{};
};

// Sent by the initiator in reply to a bundle. Public; authenticated by
// `signature` under `identity_pub`, which also binds the responder's bundle.
struct SessionInitiation {
    std::array<std::byte, kPublicKeyLen> identity_pub{};      // initiator Ed25519
    std::array<std::byte, kHybridX25519Len> x25519_eph_pub{};
    std::vector<std::byte> kem_ciphertext;                   // may be empty
    std::array<std::byte, kSignatureLen> signature{};
};

class SecureSession {
public:
    // Responder-side handshake state held between publishing the bundle and
    // receiving the initiation. Holds private key material; move-only in
    // practice (copyable members, but treat as one-shot).
    class Responder {
    public:
        const SignedPrekeyBundle& bundle() const { return bundle_; }

    private:
        friend class SecureSession;
        SignedPrekeyBundle bundle_;
        HybridResponderKeys hybrid_keys_;
        RatchetKeyPair ratchet_keys_;
        Identity::PublicKey self_id_{};
    };

    // Responder: generate fresh hybrid + ratchet key material and a signed
    // prekey bundle to publish.
    static std::expected<Responder, SessionError> create_responder(
        const Identity& self);

    // Initiator: pin and verify the responder's signed bundle, run the hybrid
    // handshake, and produce both the established session and the signed
    // initiation to send back. `expected_peer` is the responder identity
    // learned out-of-band (QR / share string); it must match the bundle.
    static std::expected<std::pair<SecureSession, SessionInitiation>, SessionError>
    initiate(const Identity& self,
             const Identity::PublicKey& expected_peer,
             const SignedPrekeyBundle& bundle);

    // Responder: verify the initiation (signature + transcript binding to our
    // own bundle) and finish establishing the session.
    static std::expected<SecureSession, SessionError> accept(
        Responder&& responder, const SessionInitiation& initiation);

    // The verified long-term identity of the peer on the other end.
    const Identity::PublicKey& peer_identity() const { return peer_id_; }

    std::expected<RatchetMessage, SessionError> encrypt(
        std::span<const std::byte> plaintext);
    std::expected<std::vector<std::byte>, SessionError> decrypt(
        const RatchetMessage& message);

private:
    SecureSession() = default;

    Identity::PublicKey peer_id_{};
    std::optional<Ratchet> ratchet_;
};

} // namespace aeromesh
