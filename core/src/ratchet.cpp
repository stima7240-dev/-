#include "aeromesh/ratchet.hpp"

#include <sodium.h>

#include <array>
#include <cstring>

namespace aeromesh {
namespace {

constexpr std::size_t kAeadKeyLen = crypto_aead_xchacha20poly1305_ietf_KEYBYTES;
constexpr std::size_t kAeadNonceLen = crypto_aead_xchacha20poly1305_ietf_NPUBBYTES;
constexpr std::size_t kAeadTagLen = crypto_aead_xchacha20poly1305_ietf_ABYTES;

const unsigned char* uc(const std::byte* p) {
    return reinterpret_cast<const unsigned char*>(p);
}
unsigned char* uc(std::byte* p) {
    return reinterpret_cast<unsigned char*>(p);
}

// X25519 Diffie-Hellman: shared = scalarmult(my_secret, their_public).
bool dh(const RatchetKey& my_secret, const RatchetKey& their_public,
        RatchetKey& out) {
    return crypto_scalarmult(uc(out.data()), uc(my_secret.data()),
                             uc(their_public.data())) == 0;
}

// Root KDF: (rk, dh_out) -> (rk', ck). Keyed BLAKE2b with rk as the key.
void kdf_rk(const RatchetKey& rk, const RatchetKey& dh_out, RatchetKey& new_rk,
            RatchetKey& new_ck) {
    std::array<unsigned char, 64> out{};
    crypto_generichash(out.data(), out.size(), uc(dh_out.data()),
                       dh_out.size(), uc(rk.data()), rk.size());
    std::memcpy(new_rk.data(), out.data(), 32);
    std::memcpy(new_ck.data(), out.data() + 32, 32);
    sodium_memzero(out.data(), out.size());
}

// Chain KDF: ck -> (ck', mk). Distinct constants keep mk and ck' independent.
void kdf_ck(const RatchetKey& ck, RatchetKey& new_ck, RatchetKey& mk) {
    const unsigned char mk_in = 0x01;
    const unsigned char ck_in = 0x02;
    crypto_generichash(uc(mk.data()), mk.size(), &mk_in, 1, uc(ck.data()),
                       ck.size());
    crypto_generichash(uc(new_ck.data()), new_ck.size(), &ck_in, 1,
                       uc(ck.data()), ck.size());
}

// Derive an AEAD key and nonce from a one-time message key.
void derive_aead(const RatchetKey& mk, std::array<unsigned char, kAeadKeyLen>& key,
                 std::array<unsigned char, kAeadNonceLen>& nonce) {
    std::array<unsigned char, kAeadKeyLen + kAeadNonceLen> out{};
    static const unsigned char ctx[] = {'a', 'e', 'r', 'o', 'm', 'e',
                                        's', 'h', '-', 'd', 'r'};
    crypto_generichash(out.data(), out.size(), ctx, sizeof(ctx), uc(mk.data()),
                       mk.size());
    std::memcpy(key.data(), out.data(), kAeadKeyLen);
    std::memcpy(nonce.data(), out.data() + kAeadKeyLen, kAeadNonceLen);
    sodium_memzero(out.data(), out.size());
}

void put_u32(std::vector<std::byte>& out, std::uint32_t v) {
    out.push_back(static_cast<std::byte>((v >> 24) & 0xFF));
    out.push_back(static_cast<std::byte>((v >> 16) & 0xFF));
    out.push_back(static_cast<std::byte>((v >> 8) & 0xFF));
    out.push_back(static_cast<std::byte>(v & 0xFF));
}

// Associated data bound to each message: the ratchet header followed by any
// caller-supplied AD. Authenticating the header prevents header tampering.
std::vector<std::byte> make_ad(const RatchetKey& dh_pub, std::uint32_t pn,
                               std::uint32_t n,
                               std::span<const std::byte> extra) {
    std::vector<std::byte> ad;
    ad.reserve(kRatchetKeyLen + 8 + extra.size());
    ad.insert(ad.end(), dh_pub.begin(), dh_pub.end());
    put_u32(ad, pn);
    put_u32(ad, n);
    ad.insert(ad.end(), extra.begin(), extra.end());
    return ad;
}

} // namespace

RatchetKeyPair generate_ratchet_keypair() {
    RatchetKeyPair kp;
    randombytes_buf(uc(kp.sec.data()), kp.sec.size());
    crypto_scalarmult_base(uc(kp.pub.data()), uc(kp.sec.data()));
    return kp;
}

std::expected<Ratchet, RatchetError> Ratchet::init_initiator(
    std::span<const std::byte> shared_secret,
    std::span<const std::byte> peer_dh_pub) {
    if (shared_secret.size() != kRatchetKeyLen ||
        peer_dh_pub.size() != kRatchetKeyLen)
        return std::unexpected(RatchetError::InvalidKeyLength);

    Ratchet r;
    r.dhs_ = generate_ratchet_keypair();
    std::memcpy(r.dhr_.data(), peer_dh_pub.data(), kRatchetKeyLen);
    r.have_dhr_ = true;

    RatchetKey sk;
    std::memcpy(sk.data(), shared_secret.data(), kRatchetKeyLen);

    RatchetKey dh_out;
    if (!dh(r.dhs_.sec, r.dhr_, dh_out))
        return std::unexpected(RatchetError::DhFailed);
    kdf_rk(sk, dh_out, r.rk_, r.cks_);
    r.have_cks_ = true;
    return r;
}

std::expected<Ratchet, RatchetError> Ratchet::init_responder(
    std::span<const std::byte> shared_secret,
    const RatchetKeyPair& dh_keypair) {
    if (shared_secret.size() != kRatchetKeyLen)
        return std::unexpected(RatchetError::InvalidKeyLength);

    Ratchet r;
    r.dhs_ = dh_keypair;
    r.have_dhr_ = false;
    std::memcpy(r.rk_.data(), shared_secret.data(), kRatchetKeyLen);
    // No sending chain until the first DH ratchet (triggered on first receive).
    return r;
}

std::expected<RatchetMessage, RatchetError> Ratchet::encrypt(
    std::span<const std::byte> plaintext,
    std::span<const std::byte> associated_data) {
    if (!have_cks_)
        return std::unexpected(RatchetError::NotSending);

    RatchetKey mk;
    RatchetKey next_ck;
    kdf_ck(cks_, next_ck, mk);
    cks_ = next_ck;

    RatchetMessage msg;
    msg.dh_pub = dhs_.pub;
    msg.pn = pn_;
    msg.n = ns_;
    ++ns_;

    const auto ad = make_ad(msg.dh_pub, msg.pn, msg.n, associated_data);

    std::array<unsigned char, kAeadKeyLen> key{};
    std::array<unsigned char, kAeadNonceLen> nonce{};
    derive_aead(mk, key, nonce);

    msg.ciphertext.resize(plaintext.size() + kAeadTagLen);
    unsigned long long clen = 0;
    const int rc = crypto_aead_xchacha20poly1305_ietf_encrypt(
        uc(msg.ciphertext.data()), &clen, uc(plaintext.data()),
        plaintext.size(), uc(ad.data()), ad.size(), nullptr, nonce.data(),
        key.data());
    sodium_memzero(key.data(), key.size());
    sodium_memzero(mk.data(), mk.size());
    if (rc != 0)
        return std::unexpected(RatchetError::EncryptFailed);
    msg.ciphertext.resize(static_cast<std::size_t>(clen));
    return msg;
}

std::expected<std::vector<std::byte>, RatchetError> Ratchet::decrypt(
    const RatchetMessage& msg, std::span<const std::byte> associated_data) {
    // Run the ratchet step on a throwaway copy and only commit the advanced
    // state when authentication succeeds. A forged or replayed message that
    // fails AEAD therefore leaves the live session untouched (no desync DoS).
    Ratchet trial = *this;
    auto result = trial.decrypt_impl(msg, associated_data);
    if (result)
        *this = std::move(trial);
    return result;
}

std::expected<std::vector<std::byte>, RatchetError> Ratchet::decrypt_impl(
    const RatchetMessage& msg, std::span<const std::byte> associated_data) {
    // 1) Maybe this is a message whose key we already skipped and cached.
    const auto skipped_key = std::make_pair(msg.dh_pub, msg.n);
    if (auto it = skipped_.find(skipped_key); it != skipped_.end()) {
        RatchetKey mk = it->second;
        auto pt = try_decrypt(mk, msg, associated_data);
        if (pt) {
            skipped_.erase(it);
            return pt;
        }
        return std::unexpected(RatchetError::DecryptFailed);
    }

    // 2) New peer ratchet key? Skip the rest of the current receiving chain,
    //    then perform a DH ratchet step.
    if (!have_dhr_ || msg.dh_pub != dhr_) {
        if (auto e = skip_message_keys(msg.pn); !e)
            return std::unexpected(e.error());
        if (auto e = dh_ratchet(msg.dh_pub); !e)
            return std::unexpected(e.error());
    }

    // 3) Skip up to this message's number within the current chain.
    if (auto e = skip_message_keys(msg.n); !e)
        return std::unexpected(e.error());

    // 4) Derive this message's key and decrypt.
    RatchetKey mk;
    RatchetKey next_ck;
    kdf_ck(ckr_, next_ck, mk);
    ckr_ = next_ck;
    ++nr_;

    auto pt = try_decrypt(mk, msg, associated_data);
    if (!pt)
        return std::unexpected(RatchetError::DecryptFailed);
    return pt;
}

std::expected<std::vector<std::byte>, RatchetError> Ratchet::try_decrypt(
    const RatchetKey& mk, const RatchetMessage& msg,
    std::span<const std::byte> associated_data) {
    const auto ad = make_ad(msg.dh_pub, msg.pn, msg.n, associated_data);

    std::array<unsigned char, kAeadKeyLen> key{};
    std::array<unsigned char, kAeadNonceLen> nonce{};
    derive_aead(mk, key, nonce);

    if (msg.ciphertext.size() < kAeadTagLen) {
        sodium_memzero(key.data(), key.size());
        return std::unexpected(RatchetError::DecryptFailed);
    }
    std::vector<std::byte> pt(msg.ciphertext.size() - kAeadTagLen);
    unsigned long long mlen = 0;
    const int rc = crypto_aead_xchacha20poly1305_ietf_decrypt(
        uc(pt.data()), &mlen, nullptr, uc(msg.ciphertext.data()),
        msg.ciphertext.size(), uc(ad.data()), ad.size(), nonce.data(),
        key.data());
    sodium_memzero(key.data(), key.size());
    if (rc != 0)
        return std::unexpected(RatchetError::DecryptFailed);
    pt.resize(static_cast<std::size_t>(mlen));
    return pt;
}

std::expected<void, RatchetError> Ratchet::skip_message_keys(std::uint32_t until) {
    if (!have_ckr_)
        return {};
    if (until > nr_ + kRatchetMaxSkip)
        return std::unexpected(RatchetError::TooManySkipped);
    while (nr_ < until) {
        RatchetKey mk;
        RatchetKey next_ck;
        kdf_ck(ckr_, next_ck, mk);
        ckr_ = next_ck;
        skipped_.emplace(std::make_pair(dhr_, nr_), mk);
        ++nr_;
    }
    return {};
}

std::expected<void, RatchetError> Ratchet::dh_ratchet(const RatchetKey& peer_pub) {
    pn_ = ns_;
    ns_ = 0;
    nr_ = 0;
    dhr_ = peer_pub;
    have_dhr_ = true;

    RatchetKey dh_out;
    if (!dh(dhs_.sec, dhr_, dh_out))
        return std::unexpected(RatchetError::DhFailed);
    kdf_rk(rk_, dh_out, rk_, ckr_);
    have_ckr_ = true;

    dhs_ = generate_ratchet_keypair();
    if (!dh(dhs_.sec, dhr_, dh_out))
        return std::unexpected(RatchetError::DhFailed);
    kdf_rk(rk_, dh_out, rk_, cks_);
    have_cks_ = true;
    return {};
}

} // namespace aeromesh
