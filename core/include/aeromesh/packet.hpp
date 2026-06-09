#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <vector>

namespace aeromesh {

// Fixed on-wire frame size in bytes. Every AeroMesh packet -- real or cover --
// is padded to exactly this many bytes so a passive observer (DPI) only ever
// sees a uniform, monotonous stream of identical frames.
// See docs/ARCHITECTURE.md and spec sections 2.2 / 4.2.
inline constexpr std::size_t kFrameSize = 1400;

// Frame header: [1 byte type][2 bytes big-endian payload length].
inline constexpr std::size_t kHeaderSize = 3;

// Largest payload that fits in a single frame.
inline constexpr std::size_t kMaxPayloadSize = kFrameSize - kHeaderSize;

enum class PacketType : std::uint8_t {
    Hello     = 0x01, // handshake / capability announce
    Data      = 0x02, // E2E-encrypted chat payload
    Ack       = 0x03, // delivery / ACK-destroy receipt
    FileChunk = 0x04, // Reed-Solomon encoded file shard
    DhtQuery  = 0x10, // Kademlia FIND_NODE / FIND_VALUE
    DhtReply  = 0x11,
    Ping      = 0x20,
    Pong      = 0x21,
    Dummy     = 0xFF, // cover traffic; payload is random and must be dropped
};

enum class FrameError : std::uint8_t {
    PayloadTooLarge,
    UnknownType,
    LengthMismatch,
};

struct Packet {
    PacketType type{};
    std::vector<std::byte> payload{};
};

using Frame = std::array<std::byte, kFrameSize>;

// Serialize a packet into a fixed-size, randomly padded frame.
[[nodiscard]] std::expected<Frame, FrameError> encode(const Packet& pkt);

// Parse a fixed-size frame back into a packet.
[[nodiscard]] std::expected<Packet, FrameError> decode(
    std::span<const std::byte, kFrameSize> frame);

// Build a cover-traffic frame filled with cryptographically random bytes.
[[nodiscard]] Frame make_dummy_frame();

// Whether a raw type byte maps to a known PacketType.
[[nodiscard]] bool is_known_type(std::uint8_t raw) noexcept;

[[nodiscard]] std::string_view to_string(PacketType t) noexcept;

} // namespace aeromesh
