#include "aeromesh/node_id.hpp"

#include <sodium.h>

namespace aeromesh {

NodeId NodeId::from_public_key(std::span<const std::byte> pk) {
    NodeId id;
    // BLAKE2b with a 32-byte digest. Keyless, so it is a plain hash.
    crypto_generichash(
        reinterpret_cast<unsigned char*>(id.bytes_.data()), id.bytes_.size(),
        reinterpret_cast<const unsigned char*>(pk.data()), pk.size(),
        nullptr, 0);
    return id;
}

NodeId NodeId::random() {
    NodeId id;
    randombytes_buf(id.bytes_.data(), id.bytes_.size());
    return id;
}

std::optional<NodeId> NodeId::from_hex(std::string_view hex) {
    if (hex.size() != kIdBytes * 2) return std::nullopt;
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    Bytes b{};
    for (std::size_t i = 0; i < kIdBytes; ++i) {
        const int hi = nibble(hex[2 * i]);
        const int lo = nibble(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) return std::nullopt;
        b[i] = static_cast<std::byte>((hi << 4) | lo);
    }
    return NodeId(b);
}

NodeId NodeId::distance(const NodeId& a, const NodeId& b) noexcept {
    NodeId d;
    for (std::size_t i = 0; i < kIdBytes; ++i)
        d.bytes_[i] = a.bytes_[i] ^ b.bytes_[i];
    return d;
}

std::size_t NodeId::shared_prefix_length(const NodeId& a,
                                         const NodeId& b) noexcept {
    std::size_t count = 0;
    for (std::size_t i = 0; i < kIdBytes; ++i) {
        const auto x = std::to_integer<std::uint8_t>(a.bytes_[i] ^ b.bytes_[i]);
        if (x == 0) {
            count += 8;
            continue;
        }
        for (int bit = 7; bit >= 0; --bit) {
            if (x & (1u << bit)) return count;
            ++count;
        }
        break;
    }
    return count;
}

int NodeId::bit(std::size_t i) const noexcept {
    if (i >= kIdBits) return 0;
    const auto byte = std::to_integer<std::uint8_t>(bytes_[i / 8]);
    const int shift = 7 - static_cast<int>(i % 8);
    return (byte >> shift) & 1;
}

std::string NodeId::to_hex() const {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(kIdBytes * 2);
    for (const auto byte : bytes_) {
        const auto v = std::to_integer<std::uint8_t>(byte);
        out.push_back(digits[v >> 4]);
        out.push_back(digits[v & 0x0F]);
    }
    return out;
}

std::strong_ordering NodeId::operator<=>(const NodeId& other) const noexcept {
    for (std::size_t i = 0; i < kIdBytes; ++i) {
        const auto a = std::to_integer<std::uint8_t>(bytes_[i]);
        const auto b = std::to_integer<std::uint8_t>(other.bytes_[i]);
        if (a < b) return std::strong_ordering::less;
        if (a > b) return std::strong_ordering::greater;
    }
    return std::strong_ordering::equal;
}

bool CloserTo::operator()(const NodeId& a, const NodeId& b) const noexcept {
    return NodeId::distance(a, target) < NodeId::distance(b, target);
}

} // namespace aeromesh
