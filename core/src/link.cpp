#include "aeromesh/link.hpp"

#include <sodium.h>

#include <algorithm>
#include <cstring>

namespace aeromesh {
namespace {

// ChaCha20-Poly1305 (IETF) primitives.
constexpr std::size_t kAeadKeyLen = 32;   // crypto_aead_chacha20poly1305_ietf_KEYBYTES
constexpr std::size_t kAeadNonceLen = 12; // crypto_aead_chacha20poly1305_ietf_NPUBBYTES
constexpr std::size_t kEphLen = 32;       // X25519 public/secret length

// Handshake wire-message sizes.
//   msg1 (initiator -> responder): eph_pub(32)
//   msg2 (responder -> initiator): eph_pub(32) + identity_pub(32) + sig(64)
//   msg3 (initiator -> responder): identity_pub(32) + sig(64)
constexpr std::size_t kMsg1Len = kEphLen;
constexpr std::size_t kMsg2Len = kEphLen + kPublicKeyLen + kSignatureLen;
constexpr std::size_t kMsg3Len = kPublicKeyLen + kSignatureLen;

// Domain-separation labels. Distinct initiator/responder tags prevent a peer's
// signature from being reflected back as the other role's signature.
constexpr char kSigLabelInitiator[] = "aeromesh-link-v1-initiator";
constexpr char kSigLabelResponder[] = "aeromesh-link-v1-responder";
constexpr char kKdfLabel[] = "aeromesh-link-kdf-v1";

unsigned char* uc(std::byte* p) noexcept {
    return reinterpret_cast<unsigned char*>(p);
}
const unsigned char* uc(const std::byte* p) noexcept {
    return reinterpret_cast<const unsigned char*>(p);
}

void write_u64_be(std::uint64_t v, std::byte* out) noexcept {
    for (int i = 7; i >= 0; --i) {
        out[i] = static_cast<std::byte>(v & 0xFFu);
        v >>= 8;
    }
}

std::uint64_t read_u64_be(const std::byte* in) noexcept {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v = (v << 8) | static_cast<std::uint64_t>(std::to_integer<unsigned>(in[i]));
    }
    return v;
}

// Build the byte string that a given role signs: label ++ eph_i ++ eph_r.
std::vector<std::byte> sig_message(
    const char* label,
    std::span<const std::byte, kEphLen> eph_i,
    std::span<const std::byte, kEphLen> eph_r) {
    const std::size_t label_len = std::strlen(label);
    std::vector<std::byte> msg;
    msg.reserve(label_len + kEphLen + kEphLen);
    for (std::size_t i = 0; i < label_len; ++i) {
        msg.push_back(static_cast<std::byte>(label[i]));
    }
    msg.insert(msg.end(), eph_i.begin(), eph_i.end());
    msg.insert(msg.end(), eph_r.begin(), eph_r.end());
    return msg;
}

// Derive the two directional session keys from the ephemeral-ephemeral shared
// secret, bound to both ephemeral public keys (transcript binding).
bool derive_keys(
    std::span<const std::byte, kEphLen> shared,
    std::span<const std::byte, kEphLen> eph_i,
    std::span<const std::byte, kEphLen> eph_r,
    LinkSessionKey& key_i2r,
    LinkSessionKey& key_r2i) {
    // message = label ++ eph_i ++ eph_r ; key = shared ; out = 64 bytes.
    const std::size_t label_len = std::strlen(kKdfLabel);
    std::vector<unsigned char> msg;
    msg.reserve(label_len + kEphLen + kEphLen);
    for (std::size_t i = 0; i < label_len; ++i) {
        msg.push_back(static_cast<unsigned char>(kKdfLabel[i]));
    }
    msg.insert(msg.end(), uc(eph_i.data()), uc(eph_i.data()) + kEphLen);
    msg.insert(msg.end(), uc(eph_r.data()), uc(eph_r.data()) + kEphLen);

    std::array<unsigned char, 64> out{};
    if (crypto_generichash(
            out.data(), out.size(),
            msg.data(), msg.size(),
            uc(shared.data()), kEphLen) != 0) {
        return false;
    }
    std::memcpy(key_i2r.data(), out.data(), kAeadKeyLen);
    std::memcpy(key_r2i.data(), out.data() + kAeadKeyLen, kAeadKeyLen);
    sodium_memzero(out.data(), out.size());
    return true;
}

} // namespace

// --- LinkHandshake -------------------------------------------------------

std::expected<LinkHandshake, LinkError> LinkHandshake::create(
    LinkRole role,
    const Identity& self,
    const Identity::PublicKey& expected_peer) {
    LinkHandshake hs;
    hs.role_ = role;
    hs.self_ = self;
    hs.expected_peer_ = expected_peer;

    // Generate the ephemeral X25519 keypair for forward secrecy.
    randombytes_buf(uc(hs.eph_sec_.data()), kEphLen);
    if (crypto_scalarmult_base(uc(hs.eph_pub_.data()), uc(hs.eph_sec_.data())) != 0) {
        return std::unexpected(LinkError::CryptoFailure);
    }
    return hs;
}

bool LinkHandshake::done() const noexcept {
    return done_;
}

std::expected<LinkKeys, LinkError> LinkHandshake::keys() const {
    if (!done_) {
        return std::unexpected(LinkError::NotEstablished);
    }
    return keys_;
}

std::expected<Identity::PublicKey, LinkError> LinkHandshake::peer_identity() const {
    if (!done_) {
        return std::unexpected(LinkError::NotEstablished);
    }
    return peer_identity_;
}

std::expected<std::vector<std::byte>, LinkError> LinkHandshake::advance(
    std::span<const std::byte> incoming) {
    if (done_) {
        return std::unexpected(LinkError::AlreadyDone);
    }
    if (!self_.has_value()) {
        return std::unexpected(LinkError::NotEstablished);
    }

    const std::span<const std::byte, kEphLen> my_eph{eph_pub_};

    if (role_ == LinkRole::Initiator) {
        if (step_ == 0) {
            // Produce msg1 = eph_i_pub.
            std::vector<std::byte> msg(eph_pub_.begin(), eph_pub_.end());
            step_ = 1;
            return msg;
        }
        if (step_ == 1) {
            // Consume msg2 = eph_r_pub ++ id_r ++ sig_r ; produce msg3.
            if (incoming.size() != kMsg2Len) {
                return std::unexpected(LinkError::Malformed);
            }
            std::memcpy(peer_eph_pub_.data(), incoming.data(), kEphLen);
            Identity::PublicKey id_r{};
            std::memcpy(id_r.data(), incoming.data() + kEphLen, kPublicKeyLen);
            std::array<std::byte, kSignatureLen> sig_r{};
            std::memcpy(sig_r.data(), incoming.data() + kEphLen + kPublicKeyLen, kSignatureLen);

            // The responder must be exactly who we intended to talk to.
            if (sodium_memcmp(id_r.data(), expected_peer_.data(), kPublicKeyLen) != 0) {
                return std::unexpected(LinkError::UnexpectedPeer);
            }

            const std::span<const std::byte, kEphLen> peer_eph{peer_eph_pub_};
            const auto signed_r = sig_message(kSigLabelResponder, my_eph, peer_eph);
            if (!Identity::verify(id_r, signed_r, sig_r)) {
                return std::unexpected(LinkError::BadSignature);
            }
            peer_identity_ = id_r;

            // ee shared secret + key schedule.
            std::array<std::byte, kEphLen> shared{};
            if (crypto_scalarmult(uc(shared.data()), uc(eph_sec_.data()), uc(peer_eph_pub_.data())) != 0) {
                return std::unexpected(LinkError::HandshakeFailed);
            }
            LinkSessionKey key_i2r{};
            LinkSessionKey key_r2i{};
            if (!derive_keys(shared, my_eph, peer_eph, key_i2r, key_r2i)) {
                sodium_memzero(shared.data(), shared.size());
                return std::unexpected(LinkError::CryptoFailure);
            }
            sodium_memzero(shared.data(), shared.size());
            keys_.tx = key_i2r; // initiator sends with i2r
            keys_.rx = key_r2i; // initiator receives with r2i

            // Sign our own transcript view and emit msg3.
            const auto signed_i = sig_message(kSigLabelInitiator, my_eph, peer_eph);
            auto sig_i = self_->sign(signed_i);
            if (!sig_i.has_value()) {
                return std::unexpected(LinkError::CryptoFailure);
            }
            std::vector<std::byte> msg;
            msg.reserve(kMsg3Len);
            const auto& self_pub = self_->public_key();
            msg.insert(msg.end(), self_pub.begin(), self_pub.end());
            msg.insert(msg.end(), sig_i->begin(), sig_i->end());

            done_ = true;
            step_ = 2;
            return msg;
        }
        return std::unexpected(LinkError::OutOfOrder);
    }

    // Responder.
    if (step_ == 0) {
        // Consume msg1 = eph_i_pub ; produce msg2.
        if (incoming.size() != kMsg1Len) {
            return std::unexpected(LinkError::Malformed);
        }
        std::memcpy(peer_eph_pub_.data(), incoming.data(), kEphLen);
        const std::span<const std::byte, kEphLen> peer_eph{peer_eph_pub_};

        // ee shared secret + key schedule. Here eph_i is the peer, eph_r is us.
        std::array<std::byte, kEphLen> shared{};
        if (crypto_scalarmult(uc(shared.data()), uc(eph_sec_.data()), uc(peer_eph_pub_.data())) != 0) {
            return std::unexpected(LinkError::HandshakeFailed);
        }
        LinkSessionKey key_i2r{};
        LinkSessionKey key_r2i{};
        if (!derive_keys(shared, peer_eph, my_eph, key_i2r, key_r2i)) {
            sodium_memzero(shared.data(), shared.size());
            return std::unexpected(LinkError::CryptoFailure);
        }
        sodium_memzero(shared.data(), shared.size());
        keys_.tx = key_r2i; // responder sends with r2i
        keys_.rx = key_i2r; // responder receives with i2r

        // Sign transcript (label_responder ++ eph_i ++ eph_r).
        const auto signed_r = sig_message(kSigLabelResponder, peer_eph, my_eph);
        auto sig_r = self_->sign(signed_r);
        if (!sig_r.has_value()) {
            return std::unexpected(LinkError::CryptoFailure);
        }
        std::vector<std::byte> msg;
        msg.reserve(kMsg2Len);
        msg.insert(msg.end(), eph_pub_.begin(), eph_pub_.end());
        const auto& self_pub = self_->public_key();
        msg.insert(msg.end(), self_pub.begin(), self_pub.end());
        msg.insert(msg.end(), sig_r->begin(), sig_r->end());
        step_ = 1;
        return msg;
    }
    if (step_ == 1) {
        // Consume msg3 = id_i ++ sig_i ; finish.
        if (incoming.size() != kMsg3Len) {
            return std::unexpected(LinkError::Malformed);
        }
        Identity::PublicKey id_i{};
        std::memcpy(id_i.data(), incoming.data(), kPublicKeyLen);
        std::array<std::byte, kSignatureLen> sig_i{};
        std::memcpy(sig_i.data(), incoming.data() + kPublicKeyLen, kSignatureLen);

        if (sodium_memcmp(id_i.data(), expected_peer_.data(), kPublicKeyLen) != 0) {
            return std::unexpected(LinkError::UnexpectedPeer);
        }
        const std::span<const std::byte, kEphLen> peer_eph{peer_eph_pub_};
        const auto signed_i = sig_message(kSigLabelInitiator, peer_eph, my_eph);
        if (!Identity::verify(id_i, signed_i, sig_i)) {
            return std::unexpected(LinkError::BadSignature);
        }
        peer_identity_ = id_i;
        done_ = true;
        step_ = 2;
        return std::vector<std::byte>{}; // no further message
    }
    return std::unexpected(LinkError::OutOfOrder);
}

// --- SecureLink ----------------------------------------------------------

SecureLink::SecureLink(const LinkKeys& keys) : keys_(keys) {}

std::expected<std::vector<std::byte>, LinkError> SecureLink::seal(const Frame& frame) {
    const std::uint64_t counter = send_counter_;

    // Nonce = 4 zero bytes ++ 8-byte big-endian counter.
    std::array<unsigned char, kAeadNonceLen> nonce{};
    std::array<std::byte, kLinkCounterLen> ctr_be{};
    write_u64_be(counter, ctr_be.data());
    std::memcpy(nonce.data() + (kAeadNonceLen - kLinkCounterLen), ctr_be.data(), kLinkCounterLen);

    std::vector<std::byte> wire(kSealedFrameLen);
    std::memcpy(wire.data(), ctr_be.data(), kLinkCounterLen);

    unsigned long long clen = 0;
    if (crypto_aead_chacha20poly1305_ietf_encrypt(
            uc(wire.data()) + kLinkCounterLen, &clen,
            uc(frame.data()), frame.size(),
            nullptr, 0, // no additional data
            nullptr,    // no secret nonce
            nonce.data(),
            uc(keys_.tx.data())) != 0) {
        return std::unexpected(LinkError::EncryptFailed);
    }
    if (clen != static_cast<unsigned long long>(kFrameSize + kLinkTagLen)) {
        return std::unexpected(LinkError::EncryptFailed);
    }
    ++send_counter_;
    return wire;
}

std::expected<Frame, LinkError> SecureLink::open(std::span<const std::byte> wire) {
    if (wire.size() != kSealedFrameLen) {
        return std::unexpected(LinkError::Malformed);
    }
    const std::uint64_t counter = read_u64_be(wire.data());

    // Replay decision (without mutating state) using a 64-entry sliding window.
    if (received_any_) {
        if (counter <= recv_max_) {
            const std::uint64_t diff = recv_max_ - counter;
            if (diff >= 64) {
                return std::unexpected(LinkError::Replay);
            }
            if ((recv_window_ >> diff) & 1ull) {
                return std::unexpected(LinkError::Replay);
            }
        }
    }

    std::array<unsigned char, kAeadNonceLen> nonce{};
    std::memcpy(nonce.data() + (kAeadNonceLen - kLinkCounterLen), wire.data(), kLinkCounterLen);

    Frame frame{};
    unsigned long long mlen = 0;
    if (crypto_aead_chacha20poly1305_ietf_decrypt(
            uc(frame.data()), &mlen,
            nullptr, // no secret nonce
            uc(wire.data()) + kLinkCounterLen, kFrameSize + kLinkTagLen,
            nullptr, 0, // no additional data
            nonce.data(),
            uc(keys_.rx.data())) != 0) {
        return std::unexpected(LinkError::DecryptFailed);
    }
    if (mlen != static_cast<unsigned long long>(kFrameSize)) {
        return std::unexpected(LinkError::DecryptFailed);
    }

    // Commit the replay window now that authentication has succeeded.
    if (!received_any_) {
        received_any_ = true;
        recv_max_ = counter;
        recv_window_ = 1ull; // bit 0 tracks recv_max_ itself
    } else if (counter > recv_max_) {
        const std::uint64_t shift = counter - recv_max_;
        if (shift >= 64) {
            recv_window_ = 1ull;
        } else {
            recv_window_ = (recv_window_ << shift) | 1ull;
        }
        recv_max_ = counter;
    } else {
        const std::uint64_t diff = recv_max_ - counter;
        recv_window_ |= (1ull << diff);
    }
    return frame;
}

} // namespace aeromesh
