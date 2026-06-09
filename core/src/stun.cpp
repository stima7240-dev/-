#include "aeromesh/stun.hpp"

#include <optional>
#include <string>
#include <vector>

#include <sodium.h>

namespace aeromesh {

namespace {

// Read a 16-bit big-endian integer at byte offset `off`.
std::uint16_t rd16(std::span<const std::byte> b, std::size_t off) {
    return static_cast<std::uint16_t>(
        (std::to_integer<std::uint32_t>(b[off]) << 8) |
        std::to_integer<std::uint32_t>(b[off + 1]));
}

constexpr std::array<std::uint8_t, 4> kCookieBytes = {0x21, 0x12, 0xA4, 0x42};

// Parse a (XOR-)MAPPED-ADDRESS attribute value into an Endpoint. IPv4 only.
std::expected<Endpoint, StunError> parse_mapped(
    std::span<const std::byte> v, bool xored) {
    if (v.size() < 4) {
        return std::unexpected(StunError::Malformed);
    }
    const std::uint8_t family = std::to_integer<std::uint8_t>(v[1]);
    const std::uint16_t raw_port = rd16(v, 2);
    const std::uint16_t port = xored
        ? static_cast<std::uint16_t>(raw_port ^ 0x2112u)
        : raw_port;
    if (family != 0x01) {
        return std::unexpected(StunError::UnsupportedFamily);
    }
    if (v.size() < 8) {
        return std::unexpected(StunError::Malformed);
    }
    std::array<std::uint8_t, 4> addr{};
    for (std::size_t i = 0; i < 4; ++i) {
        const std::uint8_t raw = std::to_integer<std::uint8_t>(v[4 + i]);
        addr[i] = xored ? static_cast<std::uint8_t>(raw ^ kCookieBytes[i]) : raw;
    }
    Endpoint ep;
    ep.host = std::to_string(addr[0]) + "." + std::to_string(addr[1]) + "." +
              std::to_string(addr[2]) + "." + std::to_string(addr[3]);
    ep.port = port;
    return ep;
}

} // namespace

StunBindingRequest make_binding_request(const StunTxnId& txn_id) {
    StunBindingRequest req;
    req.txn_id = txn_id;
    auto& d = req.datagram;
    d[0] = std::byte{0x00};
    d[1] = std::byte{0x01}; // Binding request (0x0001)
    d[2] = std::byte{0x00};
    d[3] = std::byte{0x00}; // message length 0 (no attributes)
    d[4] = std::byte{0x21};
    d[5] = std::byte{0x12};
    d[6] = std::byte{0xA4};
    d[7] = std::byte{0x42}; // magic cookie 0x2112A442
    for (std::size_t i = 0; i < kStunTxnIdLen; ++i) {
        d[8 + i] = txn_id[i];
    }
    return req;
}

StunBindingRequest make_binding_request() {
    StunTxnId id{};
    randombytes_buf(id.data(), id.size());
    return make_binding_request(id);
}

std::expected<Endpoint, StunError> parse_binding_response(
    std::span<const std::byte> d, const StunTxnId& expected_txn_id) {
    if (d.size() < kStunHeaderLen) {
        return std::unexpected(StunError::Malformed);
    }
    if (rd16(d, 0) != kStunBindingSuccess) {
        return std::unexpected(StunError::NotASuccessResponse);
    }
    const std::size_t msg_len = rd16(d, 2);
    for (std::size_t i = 0; i < 4; ++i) {
        if (std::to_integer<std::uint8_t>(d[4 + i]) != kCookieBytes[i]) {
            return std::unexpected(StunError::Malformed);
        }
    }
    for (std::size_t i = 0; i < kStunTxnIdLen; ++i) {
        if (d[8 + i] != expected_txn_id[i]) {
            return std::unexpected(StunError::TransactionMismatch);
        }
    }
    if (d.size() < kStunHeaderLen + msg_len) {
        return std::unexpected(StunError::Malformed);
    }

    std::optional<Endpoint> xor_result;
    std::optional<Endpoint> plain_result;
    bool saw_unsupported = false;

    std::size_t off = kStunHeaderLen;
    const std::size_t end = kStunHeaderLen + msg_len;
    while (off + 4 <= end) {
        const std::uint16_t atype = rd16(d, off);
        const std::uint16_t alen = rd16(d, off + 2);
        const std::size_t vstart = off + 4;
        if (vstart + alen > end) {
            break;
        }
        const std::span<const std::byte> value = d.subspan(vstart, alen);
        if (atype == kStunAttrXorMappedAddress) {
            auto r = parse_mapped(value, true);
            if (r) {
                xor_result = *r;
            } else if (r.error() == StunError::UnsupportedFamily) {
                saw_unsupported = true;
            }
        } else if (atype == kStunAttrMappedAddress) {
            auto r = parse_mapped(value, false);
            if (r) {
                plain_result = *r;
            } else if (r.error() == StunError::UnsupportedFamily) {
                saw_unsupported = true;
            }
        }
        // Attributes are padded to a 4-byte boundary.
        const std::size_t padded =
            (static_cast<std::size_t>(alen) + 3u) & ~static_cast<std::size_t>(3u);
        off = vstart + padded;
    }

    if (xor_result) {
        return *xor_result;
    }
    if (plain_result) {
        return *plain_result;
    }
    if (saw_unsupported) {
        return std::unexpected(StunError::UnsupportedFamily);
    }
    return std::unexpected(StunError::NoMappedAddress);
}

std::expected<Endpoint, StunError> discover_reflexive_address(
    IDatagramSocket& socket, const Endpoint& stun_server, int max_polls) {
    const StunBindingRequest req = make_binding_request();
    if (!socket.send(stun_server, req.datagram)) {
        return std::unexpected(StunError::SendFailed);
    }
    for (int i = 0; i < max_polls; ++i) {
        Endpoint from;
        std::vector<std::byte> buf;
        if (!socket.poll(from, buf)) {
            continue;
        }
        auto parsed = parse_binding_response(buf, req.txn_id);
        if (parsed) {
            return *parsed;
        }
        // Unrelated or stale datagram; keep polling.
    }
    return std::unexpected(StunError::Timeout);
}

} // namespace aeromesh
