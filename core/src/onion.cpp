#include "aeromesh/onion.hpp"

#include "aeromesh/packet.hpp" // kMaxPayloadSize

#include <sodium.h>

#include <cstdint>
#include <utility>

namespace aeromesh {
namespace {

// Layer plaintext layout:
//   byte 0 : type (kTypeRelay or kTypeExit)
//   relay  : u16 endpoint_len (big-endian) + endpoint + inner onion
//   exit   : payload
constexpr std::byte kTypeRelay = static_cast<std::byte>(0x01);
constexpr std::byte kTypeExit = static_cast<std::byte>(0x02);

void put_u16(std::vector<std::byte>& out, std::uint16_t v) {
    out.push_back(static_cast<std::byte>((v >> 8) & 0xFF));
    out.push_back(static_cast<std::byte>(v & 0xFF));
}

std::expected<std::vector<std::byte>, OnionError> seal_layer(
    std::span<const std::byte> plaintext,
    std::span<const std::byte> recipient_pub) {
    if (recipient_pub.size() != kX25519KeyLen)
        return std::unexpected(OnionError::InvalidKeyLength);
    std::vector<std::byte> out(plaintext.size() + crypto_box_SEALBYTES);
    const int rc = crypto_box_seal(
        reinterpret_cast<unsigned char*>(out.data()),
        reinterpret_cast<const unsigned char*>(plaintext.data()),
        plaintext.size(),
        reinterpret_cast<const unsigned char*>(recipient_pub.data()));
    if (rc != 0)
        return std::unexpected(OnionError::SealFailed);
    return out;
}

} // namespace

std::expected<std::vector<std::byte>, OnionError> build_onion(
    const std::vector<OnionHop>& path, std::span<const std::byte> payload) {
    if (path.empty())
        return std::unexpected(OnionError::EmptyPath);
    if (path.size() > kMaxOnionHops)
        return std::unexpected(OnionError::PathTooLong);

    // Innermost (exit) layer: addressed to the final recipient, carries payload.
    std::vector<std::byte> exit_pt;
    exit_pt.reserve(1 + payload.size());
    exit_pt.push_back(kTypeExit);
    exit_pt.insert(exit_pt.end(), payload.begin(), payload.end());

    auto sealed = seal_layer(exit_pt, path.back().x25519_pub);
    if (!sealed)
        return std::unexpected(sealed.error());
    std::vector<std::byte> inner = std::move(*sealed);

    // Wrap relay layers from the second-to-last hop outward to the first hop.
    // The layer opened by hop (i-1) tells it to forward to hop i.
    for (std::size_t i = path.size(); i-- > 1;) {
        const std::string& next_ep = path[i].endpoint;
        if (next_ep.size() > 0xFFFF)
            return std::unexpected(OnionError::Malformed);

        std::vector<std::byte> relay_pt;
        relay_pt.reserve(3 + next_ep.size() + inner.size());
        relay_pt.push_back(kTypeRelay);
        put_u16(relay_pt, static_cast<std::uint16_t>(next_ep.size()));
        for (char c : next_ep)
            relay_pt.push_back(
                static_cast<std::byte>(static_cast<unsigned char>(c)));
        relay_pt.insert(relay_pt.end(), inner.begin(), inner.end());

        auto layer = seal_layer(relay_pt, path[i - 1].x25519_pub);
        if (!layer)
            return std::unexpected(layer.error());
        inner = std::move(*layer);
    }

    if (inner.size() > kMaxPayloadSize)
        return std::unexpected(OnionError::PayloadTooLarge);
    return inner;
}

std::expected<Peeled, OnionError> peel_onion(
    std::span<const std::byte> onion,
    std::span<const std::byte> my_x25519_pub,
    std::span<const std::byte> my_x25519_secret) {
    if (my_x25519_pub.size() != kX25519KeyLen ||
        my_x25519_secret.size() != kX25519KeyLen)
        return std::unexpected(OnionError::InvalidKeyLength);
    if (onion.size() < crypto_box_SEALBYTES + 1)
        return std::unexpected(OnionError::Malformed);

    std::vector<std::byte> opened(onion.size() - crypto_box_SEALBYTES);
    const int rc = crypto_box_seal_open(
        reinterpret_cast<unsigned char*>(opened.data()),
        reinterpret_cast<const unsigned char*>(onion.data()),
        onion.size(),
        reinterpret_cast<const unsigned char*>(my_x25519_pub.data()),
        reinterpret_cast<const unsigned char*>(my_x25519_secret.data()));
    if (rc != 0)
        return std::unexpected(OnionError::OpenFailed);
    if (opened.empty())
        return std::unexpected(OnionError::Malformed);

    Peeled result;
    const std::byte type = opened[0];
    if (type == kTypeExit) {
        result.is_exit = true;
        result.data.assign(opened.begin() + 1, opened.end());
        return result;
    }
    if (type == kTypeRelay) {
        if (opened.size() < 3)
            return std::unexpected(OnionError::Malformed);
        const std::uint16_t ep_len = static_cast<std::uint16_t>(
            (std::to_integer<unsigned int>(opened[1]) << 8) |
            std::to_integer<unsigned int>(opened[2]));
        if (opened.size() < static_cast<std::size_t>(3) + ep_len)
            return std::unexpected(OnionError::Malformed);
        result.is_exit = false;
        result.next_endpoint.assign(
            reinterpret_cast<const char*>(opened.data()) + 3, ep_len);
        result.data.assign(opened.begin() + 3 + ep_len, opened.end());
        return result;
    }
    return std::unexpected(OnionError::Malformed);
}

} // namespace aeromesh
