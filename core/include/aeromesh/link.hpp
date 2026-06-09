// Link-layer channel encryption (Milestone 16).
//
// Wraps every 1400-byte frame in an authenticated, constant-size ciphertext so
// that a passive network observer cannot distinguish cover (Dummy) frames from
// real ones, cannot read the packet type, and cannot read the true payload
// length. This is the foundation that makes the cover-traffic scheme actually
// work (see the AeroMesh security audit, finding #1).
//
// Design (OS-independent, testable in-memory):
//   * A SIGMA-like mutually authenticated handshake establishes per-direction
//     session keys with forward secrecy: each side contributes an ephemeral
//     X25519 key (ee gives forward secrecy), and each side signs the handshake
//     transcript with its long-term Ed25519 identity key (gives mutual
//     authentication and binds the channel to the expected peer identity).
//   * After the handshake, SecureLink seals each frame with
//     ChaCha20-Poly1305 under the send key and a monotonic 64-bit counter
//     nonce, and verifies + anti-replays received frames with a sliding
//     window. Every sealed frame is exactly kSealedFrameLen bytes, so dummy
//     and real frames are indistinguishable on the wire.

#pragma once

#include "aeromesh/identity.hpp"
#include "aeromesh/packet.hpp" // Frame, kFrameSize

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <vector>

namespace aeromesh {

enum class LinkError {
    InvalidKeyLength,
    HandshakeFailed,
    BadSignature,
    UnexpectedPeer,
    Malformed,
    NotEstablished,
    AlreadyDone,
    OutOfOrder,
    EncryptFailed,
    DecryptFailed,
    Replay,
    CryptoFailure,
};

inline constexpr std::size_t kLinkKeyLen = 32;     // ChaCha20-Poly1305 key
inline constexpr std::size_t kLinkTagLen = 16;     // Poly1305 tag
inline constexpr std::size_t kLinkCounterLen = 8;  // 64-bit big-endian nonce counter

// Every sealed frame on the wire is exactly this many bytes:
//   counter(8) + ciphertext(kFrameSize) + tag(16).
// Constant size is what makes cover frames indistinguishable from real ones.
inline constexpr std::size_t kSealedFrameLen =
    kLinkCounterLen + kFrameSize + kLinkTagLen;

using LinkSessionKey = std::array<std::byte, kLinkKeyLen>;

// Per-direction session keys derived from the handshake.
struct LinkKeys {
    LinkSessionKey tx;  // key used to seal frames this endpoint sends
    LinkSessionKey rx;  // key used to open frames this endpoint receives
};

enum class LinkRole {
    Initiator,
    Responder,
};

// SIGMA-like mutually authenticated key exchange with forward secrecy.
//
// Usage (driven by the caller, transport-agnostic):
//   auto hs = LinkHandshake::create(role, self, expected_peer);
//   // Initiator starts with an empty incoming span; responder is fed msg1.
//   auto out = hs->advance(incoming);  // returns bytes to send (may be empty)
//   ... exchange messages until both sides report done() ...
//   LinkKeys keys = hs->keys();
class LinkHandshake {
public:
    // self     : this node's long-term identity (Ed25519 + X25519).
    // expected : the peer identity public key we require to authenticate.
    static std::expected<LinkHandshake, LinkError> create(
        LinkRole role,
        const Identity& self,
        const Identity::PublicKey& expected_peer);

    // Consume the peer's latest message (empty span when there is none yet)
    // and produce the next message to send (empty vector when there is none).
    std::expected<std::vector<std::byte>, LinkError> advance(
        std::span<const std::byte> incoming);

    bool done() const noexcept;

    // Valid only once done() is true; otherwise returns NotEstablished.
    std::expected<LinkKeys, LinkError> keys() const;

    // Authenticated peer identity; valid only once done().
    std::expected<Identity::PublicKey, LinkError> peer_identity() const;

private:
    LinkHandshake() = default;

    LinkRole role_{LinkRole::Initiator};
    int step_ = 0;
    bool done_ = false;

    // Long-term identity material for authentication. The full Identity is
    // retained because the handshake transcript is signed after create().
    std::optional<Identity> self_{};
    Identity::PublicKey expected_peer_{};
    Identity::PublicKey peer_identity_{};

    // Ephemeral X25519 material for this handshake (forward secrecy).
    std::array<std::byte, 32> eph_pub_{};
    std::array<std::byte, 32> eph_sec_{};
    std::array<std::byte, 32> peer_eph_pub_{};

    LinkKeys keys_{};
};

// Post-handshake authenticated, anti-replayed, constant-size frame channel.
class SecureLink {
public:
    explicit SecureLink(const LinkKeys& keys);

    // Seal one fixed-size frame into a constant-size wire blob of exactly
    // kSealedFrameLen bytes. Dummy and real frames produce identical sizes.
    std::expected<std::vector<std::byte>, LinkError> seal(const Frame& frame);

    // Open a received wire blob: authenticates, rejects replays and reordered
    // duplicates via a sliding window, and returns the recovered frame.
    std::expected<Frame, LinkError> open(std::span<const std::byte> wire);

    std::uint64_t sent_counter() const noexcept { return send_counter_; }
    std::uint64_t highest_received() const noexcept { return recv_max_; }

private:
    LinkKeys keys_;
    std::uint64_t send_counter_ = 0;
    // Sliding replay window: recv_max_ is the highest counter accepted so far,
    // recv_window_ is a bitmask of the 64 counters at or below it.
    std::uint64_t recv_max_ = 0;
    std::uint64_t recv_window_ = 0;
    bool received_any_ = false;
};

} // namespace aeromesh
