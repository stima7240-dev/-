#pragma once

// Onion (layered) encryption for sender/recipient unlinkability.
//
// A message is wrapped in one sealed-box layer per relay on its path. Each
// relay can open only its own layer, learning just the *next* hop -- never the
// origin, the final destination, or the plaintext. The innermost layer is the
// exit, which yields the original payload.
//
// Each layer is a libsodium anonymous sealed box (ephemeral X25519 +
// XSalsa20-Poly1305): the sender leaves no static key on the wire, so relays
// cannot link a layer back to a sender by key. Because the surrounding frame
// layer always pads to kFrameSize, the onion's shrinking size as layers are
// peeled is NOT observable on the wire, which would otherwise leak a relay's
// position in the path.

#include <array>
#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace aeromesh {

inline constexpr std::size_t kX25519KeyLen = 32;
inline constexpr std::size_t kMaxOnionHops = 5;

enum class OnionError {
    EmptyPath,
    PathTooLong,
    PayloadTooLarge,
    InvalidKeyLength,
    Malformed,
    SealFailed,
    OpenFailed,
};

// One hop on an onion path: its X25519 sealing key and the locator the
// *previous* hop uses to reach it.
struct OnionHop {
    std::array<std::byte, kX25519KeyLen> x25519_pub{};
    std::string endpoint;
};

// Build a layered onion of `payload` for `path`. path.front() is the first
// relay (the sender sends the result there); path.back() is the exit that
// recovers `payload`. A single-element path is a direct sealed message to the
// recipient. The returned blob is sized to fit inside one frame payload
// (kMaxPayloadSize); otherwise PayloadTooLarge is returned.
std::expected<std::vector<std::byte>, OnionError> build_onion(
    const std::vector<OnionHop>& path, std::span<const std::byte> payload);

// Result of peeling a single layer.
struct Peeled {
    bool is_exit = false;           // true: `data` is the final payload
    std::string next_endpoint;      // where to forward (valid when !is_exit)
    std::vector<std::byte> data;    // inner onion to forward, or final payload
};

// Peel the outermost layer addressed to the keypair (my_pub, my_secret). On a
// relay layer, returns the inner onion plus the next hop's endpoint. On the
// exit layer, returns the recovered payload. Fails with OpenFailed if the layer
// is not addressed to this key (wrong recipient or tampering).
std::expected<Peeled, OnionError> peel_onion(
    std::span<const std::byte> onion,
    std::span<const std::byte> my_x25519_pub,
    std::span<const std::byte> my_x25519_secret);

} // namespace aeromesh
