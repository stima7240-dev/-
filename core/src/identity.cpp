#include "aeromesh/identity.hpp"

#include <array>
#include <vector>

#include <sodium.h>

namespace aeromesh {
namespace {

constexpr int kB64Variant = sodium_base64_VARIANT_ORIGINAL;

std::string to_b64(const std::byte* data, std::size_t len) {
    const std::size_t cap = sodium_base64_encoded_len(len, kB64Variant);
    std::string out(cap, '\0');
    sodium_bin2base64(out.data(), cap,
                      reinterpret_cast<const unsigned char*>(data), len,
                      kB64Variant);
    // sodium_bin2base64 writes a trailing NUL within cap; trim to it.
    out.resize(std::char_traits<char>::length(out.c_str()));
    return out;
}

} // namespace

bool init_crypto() noexcept {
    // sodium_init() is idempotent: returns 1 if already initialised, 0 on the
    // first successful call, -1 on failure.
    return sodium_init() >= 0;
}

std::expected<Identity, CryptoError> Identity::generate() {
    if (sodium_init() < 0) {
        return std::unexpected(CryptoError::NotInitialized);
    }
    Identity id;
    unsigned char pk[crypto_sign_PUBLICKEYBYTES];
    unsigned char sk[crypto_sign_SECRETKEYBYTES];
    static_assert(crypto_sign_PUBLICKEYBYTES == kPublicKeyLen);
    static_assert(crypto_sign_SECRETKEYBYTES == kSecretKeyLen);
    if (crypto_sign_keypair(pk, sk) != 0) {
        return std::unexpected(CryptoError::InternalError);
    }
    std::memcpy(id.pk_.data(), pk, kPublicKeyLen);
    std::memcpy(id.sk_.data(), sk, kSecretKeyLen);
    sodium_memzero(sk, sizeof sk);
    return id;
}

std::expected<Identity, CryptoError> Identity::from_secret_b64(
    std::string_view b64) {
    if (sodium_init() < 0) {
        return std::unexpected(CryptoError::NotInitialized);
    }
    std::array<unsigned char, kSecretKeyLen> sk{};
    std::size_t bin_len = 0;
    if (sodium_base642bin(sk.data(), sk.size(), b64.data(), b64.size(),
                          nullptr, &bin_len, nullptr, kB64Variant) != 0) {
        return std::unexpected(CryptoError::InvalidEncoding);
    }
    if (bin_len != kSecretKeyLen) {
        return std::unexpected(CryptoError::InvalidKeyLength);
    }
    Identity id;
    std::memcpy(id.sk_.data(), sk.data(), kSecretKeyLen);
    // The Ed25519 public key is the trailing 32 bytes of the libsodium secret key.
    std::memcpy(id.pk_.data(), sk.data() + 32, kPublicKeyLen);
    sodium_memzero(sk.data(), sk.size());
    return id;
}

std::string Identity::fingerprint() const {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(8 * 2 + 3);
    for (std::size_t i = 0; i < 8; ++i) {
        const auto b = std::to_integer<std::uint8_t>(pk_[i]);
        out.push_back(hex[b >> 4]);
        out.push_back(hex[b & 0x0F]);
        if (i % 2 == 1 && i != 7) out.push_back(' ');
    }
    return out;
}

std::string Identity::share_string() const {
    return to_b64(pk_.data(), pk_.size());
}

std::string Identity::export_secret_b64() const {
    return to_b64(sk_.data(), sk_.size());
}

std::expected<std::array<std::byte, 32>, CryptoError>
Identity::x25519_public() const {
    std::array<unsigned char, crypto_scalarmult_curve25519_BYTES> x{};
    if (crypto_sign_ed25519_pk_to_curve25519(
            x.data(), reinterpret_cast<const unsigned char*>(pk_.data())) != 0) {
        return std::unexpected(CryptoError::InternalError);
    }
    std::array<std::byte, 32> out{};
    std::memcpy(out.data(), x.data(), out.size());
    return out;
}

std::expected<std::array<std::byte, 32>, CryptoError>
Identity::x25519_secret() const {
    std::array<unsigned char, crypto_scalarmult_curve25519_BYTES> x{};
    if (crypto_sign_ed25519_sk_to_curve25519(
            x.data(), reinterpret_cast<const unsigned char*>(sk_.data())) != 0) {
        return std::unexpected(CryptoError::InternalError);
    }
    std::array<std::byte, 32> out{};
    std::memcpy(out.data(), x.data(), out.size());
    sodium_memzero(x.data(), x.size());
    return out;
}

std::expected<std::array<std::byte, kSignatureLen>, CryptoError>
Identity::sign(std::span<const std::byte> message) const {
    static_assert(crypto_sign_BYTES == kSignatureLen);
    std::array<std::byte, kSignatureLen> sig{};
    if (crypto_sign_detached(
            reinterpret_cast<unsigned char*>(sig.data()), nullptr,
            reinterpret_cast<const unsigned char*>(message.data()),
            message.size(),
            reinterpret_cast<const unsigned char*>(sk_.data())) != 0) {
        return std::unexpected(CryptoError::InternalError);
    }
    return sig;
}

bool Identity::verify(const PublicKey& pub, std::span<const std::byte> message,
                      std::span<const std::byte, kSignatureLen> signature) {
    return crypto_sign_verify_detached(
               reinterpret_cast<const unsigned char*>(signature.data()),
               reinterpret_cast<const unsigned char*>(message.data()),
               message.size(),
               reinterpret_cast<const unsigned char*>(pub.data())) == 0;
}

std::expected<Identity::PublicKey, CryptoError> parse_share_string(
    std::string_view share) {
    if (sodium_init() < 0) {
        return std::unexpected(CryptoError::NotInitialized);
    }
    Identity::PublicKey pk{};
    std::size_t bin_len = 0;
    if (sodium_base642bin(reinterpret_cast<unsigned char*>(pk.data()), pk.size(),
                          share.data(), share.size(), nullptr, &bin_len, nullptr,
                          kB64Variant) != 0) {
        return std::unexpected(CryptoError::InvalidEncoding);
    }
    if (bin_len != kPublicKeyLen) {
        return std::unexpected(CryptoError::InvalidKeyLength);
    }
    return pk;
}

} // namespace aeromesh
