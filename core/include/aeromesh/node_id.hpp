#pragma once

// 256-bit Kademlia node identifier and XOR distance metric.

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace aeromesh {

inline constexpr std::size_t kIdBytes = 32;
inline constexpr std::size_t kIdBits = kIdBytes * 8;

// A node id is a 256-bit value. In AeroMesh it is derived from a peer's
// long-term public key (BLAKE2b-256), so identity and DHT position are bound
// together and a node cannot cheaply choose its own id region.
class NodeId {
public:
    using Bytes = std::array<std::byte, kIdBytes>;

    NodeId() = default;
    explicit NodeId(const Bytes& b) noexcept : bytes_(b) {}

    // Derive an id from a public key (or any byte string) via BLAKE2b-256.
    static NodeId from_public_key(std::span<const std::byte> pk);
    // Uniformly random id (for tests and for target ids during lookups).
    static NodeId random();
    // Parse 64 lowercase/uppercase hex chars; std::nullopt on malformed input.
    static std::optional<NodeId> from_hex(std::string_view hex);

    const Bytes& bytes() const noexcept { return bytes_; }

    // XOR distance: the Kademlia metric. distance(a, a) == NodeId{} (all zero).
    static NodeId distance(const NodeId& a, const NodeId& b) noexcept;

    // Number of leading bits a and b have in common (0..kIdBits). Used to pick
    // the routing-table bucket for a contact relative to self.
    static std::size_t shared_prefix_length(const NodeId& a,
                                            const NodeId& b) noexcept;

    // Bit at position i, where 0 is the most-significant bit of byte 0.
    int bit(std::size_t i) const noexcept;

    std::string to_hex() const;

    bool operator==(const NodeId&) const noexcept = default;
    // Lexicographic big-endian ordering, i.e. ids compared as 256-bit numbers.
    std::strong_ordering operator<=>(const NodeId& other) const noexcept;

private:
    Bytes bytes_{};
};

// Comparator that orders ids by XOR distance to a fixed target (closer first).
struct CloserTo {
    NodeId target;
    bool operator()(const NodeId& a, const NodeId& b) const noexcept;
};

} // namespace aeromesh
