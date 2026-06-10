#pragma once

// AeroMesh client wire codec.
//
// The core defines the handshake/message *structures* (SignedPrekeyBundle,
// SessionInitiation, RatchetMessage) but deliberately leaves their on-wire byte
// layout to the transport layer. This header is that layer for the client: a
// small, self-contained, big-endian codec that turns those structures into the
// payload bytes carried inside Hello/Data packets, and back.
//
// Conventions:
//   * Integers are big-endian (matching the packet header and Kademlia wire).
//   * Fixed-size key/signature arrays are written raw; their length is implied
//     by the type, so it never travels on the wire.
//   * Variable-size fields (post-quantum KEM blobs, ciphertext) are length-
//     prefixed with a u16, which is ample for a single 1400-byte frame.
//   * Every decoder is total: malformed or truncated input yields std::nullopt
//     rather than reading out of bounds.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "aeromesh/ratchet.hpp"
#include "aeromesh/session.hpp"

namespace aeromesh::wire {

// ---- writers -------------------------------------------------------------

inline void put_u16(std::vector<std::byte>& out, std::uint16_t v) {
    out.push_back(static_cast<std::byte>((v >> 8) & 0xFF));
    out.push_back(static_cast<std::byte>(v & 0xFF));
}

inline void put_u32(std::vector<std::byte>& out, std::uint32_t v) {
    out.push_back(static_cast<std::byte>((v >> 24) & 0xFF));
    out.push_back(static_cast<std::byte>((v >> 16) & 0xFF));
    out.push_back(static_cast<std::byte>((v >> 8) & 0xFF));
    out.push_back(static_cast<std::byte>(v & 0xFF));
}

template <std::size_t N>
inline void put_array(std::vector<std::byte>& out,
                      const std::array<std::byte, N>& a) {
    out.insert(out.end(), a.begin(), a.end());
}

inline void put_len_prefixed(std::vector<std::byte>& out,
                             std::span<const std::byte> b) {
    put_u16(out, static_cast<std::uint16_t>(b.size()));
    out.insert(out.end(), b.begin(), b.end());
}

// ---- reader --------------------------------------------------------------

// A bounds-checked cursor over an immutable byte span. Once any read fails,
// `ok` latches false and every subsequent read is a no-op, so a single check
// at the end is sufficient.
struct Reader {
    std::span<const std::byte> data;
    std::size_t pos = 0;
    bool ok = true;

    [[nodiscard]] bool remaining(std::size_t n) const {
        return ok && pos + n <= data.size();
    }

    std::uint16_t u16() {
        if (!remaining(2)) {
            ok = false;
            return 0;
        }
        const std::uint16_t v =
            static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(
                    std::to_integer<unsigned char>(data[pos])) << 8) |
            static_cast<std::uint16_t>(
                std::to_integer<unsigned char>(data[pos + 1]));
        pos += 2;
        return v;
    }

    std::uint32_t u32() {
        if (!remaining(4)) {
            ok = false;
            return 0;
        }
        std::uint32_t v = 0;
        for (std::size_t i = 0; i < 4; ++i) {
            v = (v << 8) |
                static_cast<std::uint32_t>(
                    std::to_integer<unsigned char>(data[pos + i]));
        }
        pos += 4;
        return v;
    }

    template <std::size_t N>
    void read_array(std::array<std::byte, N>& a) {
        if (!remaining(N)) {
            ok = false;
            return;
        }
        for (std::size_t i = 0; i < N; ++i) {
            a[i] = data[pos + i];
        }
        pos += N;
    }

    std::vector<std::byte> read_len_prefixed() {
        const std::uint16_t n = u16();
        if (!remaining(n)) {
            ok = false;
            return {};
        }
        const auto sub = data.subspan(pos, n);
        pos += n;
        return std::vector<std::byte>(sub.begin(), sub.end());
    }
};

// ---- structure codecs ----------------------------------------------------

inline std::vector<std::byte> encode_bundle(const SignedPrekeyBundle& b) {
    std::vector<std::byte> out;
    put_array(out, b.identity_pub);
    put_array(out, b.x25519_pub);
    put_len_prefixed(out, b.kem_pub);
    put_array(out, b.ratchet_pub);
    put_array(out, b.signature);
    return out;
}

inline std::optional<SignedPrekeyBundle> decode_bundle(
    std::span<const std::byte> in) {
    Reader r{in};
    SignedPrekeyBundle b;
    r.read_array(b.identity_pub);
    r.read_array(b.x25519_pub);
    b.kem_pub = r.read_len_prefixed();
    r.read_array(b.ratchet_pub);
    r.read_array(b.signature);
    if (!r.ok) {
        return std::nullopt;
    }
    return b;
}

inline std::vector<std::byte> encode_initiation(const SessionInitiation& s) {
    std::vector<std::byte> out;
    put_array(out, s.identity_pub);
    put_array(out, s.x25519_eph_pub);
    put_len_prefixed(out, s.kem_ciphertext);
    put_array(out, s.signature);
    return out;
}

inline std::optional<SessionInitiation> decode_initiation(
    std::span<const std::byte> in) {
    Reader r{in};
    SessionInitiation s;
    r.read_array(s.identity_pub);
    r.read_array(s.x25519_eph_pub);
    s.kem_ciphertext = r.read_len_prefixed();
    r.read_array(s.signature);
    if (!r.ok) {
        return std::nullopt;
    }
    return s;
}

inline std::vector<std::byte> encode_ratchet(const RatchetMessage& m) {
    std::vector<std::byte> out;
    put_array(out, m.dh_pub);
    put_u32(out, m.pn);
    put_u32(out, m.n);
    put_len_prefixed(out, m.ciphertext);
    return out;
}

inline std::optional<RatchetMessage> decode_ratchet(
    std::span<const std::byte> in) {
    Reader r{in};
    RatchetMessage m;
    r.read_array(m.dh_pub);
    m.pn = r.u32();
    m.n = r.u32();
    m.ciphertext = r.read_len_prefixed();
    if (!r.ok) {
        return std::nullopt;
    }
    return m;
}

} // namespace aeromesh::wire
