#include "aeromesh/account_vault.hpp"

#include <sodium.h>

#include <array>

namespace aeromesh {
namespace {

// File header marker: the ASCII bytes "AMV1" (AeroMesh Vault).
constexpr std::array<std::byte, 4> kMagic = {
    std::byte{'A'}, std::byte{'M'}, std::byte{'V'}, std::byte{'1'}};

// Format version. v2 dropped the in-file KDF cost fields; the cost is now a
// fixed compile-time constant (see below).
constexpr std::uint8_t kVersion = 2;

// Bytes before the ciphertext: magic(4) + version(1) + salt + nonce.
constexpr std::size_t kHeaderLength =
    4 + 1 + crypto_pwhash_SALTBYTES + crypto_secretbox_NONCEBYTES;

// The header sizes promised in the public header must match libsodium.
static_assert(AccountVault::kSaltBytes == crypto_pwhash_SALTBYTES,
              "kSaltBytes must equal crypto_pwhash_SALTBYTES");
static_assert(AccountVault::kKeyBytes == crypto_secretbox_KEYBYTES,
              "kKeyBytes must equal crypto_secretbox_KEYBYTES");

// Fixed Argon2id cost. "Sensitive" is libsodium's strongest preset
// (~1 GiB memory, several iterations): chosen for maximum resistance to
// password cracking. These are pinned in code, never read from the file, so a
// tampered file cannot request an absurd memory cost to crash the app.
constexpr unsigned long long kOpsLimit = crypto_pwhash_OPSLIMIT_SENSITIVE;
constexpr std::size_t kMemLimit = crypto_pwhash_MEMLIMIT_SENSITIVE;

bool ensure_sodium() { return sodium_init() >= 0; }

// Derive a 256-bit key from a password and salt using fixed Argon2id cost.
std::expected<AccountVault::Key, VaultError> derive(std::string_view password,
                                                    const AccountVault::Salt& salt) {
    if (!ensure_sodium()) {
        return std::unexpected(VaultError::NotInitialized);
    }
    AccountVault::Key key{};
    const int rc = crypto_pwhash(
        reinterpret_cast<unsigned char*>(key.data()), key.size(),
        password.data(), password.size(),
        reinterpret_cast<const unsigned char*>(salt.data()), kOpsLimit, kMemLimit,
        crypto_pwhash_ALG_ARGON2ID13);
    if (rc != 0) {
        sodium_memzero(key.data(), key.size());
        return std::unexpected(VaultError::OutOfMemory);
    }
    return key;
}

}  // namespace

std::expected<AccountVault::Key, VaultError>
AccountVault::deriveKey(std::string_view password, Salt& out_salt) {
    if (!ensure_sodium()) {
        return std::unexpected(VaultError::NotInitialized);
    }
    if (password.size() < kMinPasswordLength) {
        return std::unexpected(VaultError::WeakPassword);
    }
    randombytes_buf(out_salt.data(), out_salt.size());
    return derive(password, out_salt);
}

std::expected<AccountVault::Key, VaultError>
AccountVault::deriveKeyWithSalt(std::string_view password, const Salt& salt) {
    return derive(password, salt);
}

std::expected<AccountVault::Salt, VaultError>
AccountVault::saltOf(std::span<const std::byte> blob) {
    if (blob.size() < kHeaderLength + crypto_secretbox_MACBYTES) {
        return std::unexpected(VaultError::Corrupt);
    }
    for (std::size_t i = 0; i < kMagic.size(); ++i) {
        if (blob[i] != kMagic[i]) {
            return std::unexpected(VaultError::Corrupt);
        }
    }
    if (std::to_integer<std::uint8_t>(blob[kMagic.size()]) != kVersion) {
        return std::unexpected(VaultError::Corrupt);
    }
    const std::size_t off = kMagic.size() + 1;
    Salt salt{};
    for (std::size_t i = 0; i < salt.size(); ++i) {
        salt[i] = std::to_integer<std::uint8_t>(blob[off + i]);
    }
    return salt;
}

std::expected<std::vector<std::byte>, VaultError>
AccountVault::sealWithKey(const Key& key, const Salt& salt,
                          std::string_view secret) {
    if (!ensure_sodium()) {
        return std::unexpected(VaultError::NotInitialized);
    }

    std::array<unsigned char, crypto_secretbox_NONCEBYTES> nonce{};
    randombytes_buf(nonce.data(), nonce.size());

    std::vector<unsigned char> cipher(secret.size() + crypto_secretbox_MACBYTES);
    const int rc = crypto_secretbox_easy(
        cipher.data(), reinterpret_cast<const unsigned char*>(secret.data()),
        secret.size(), nonce.data(),
        reinterpret_cast<const unsigned char*>(key.data()));
    if (rc != 0) {
        return std::unexpected(VaultError::InternalError);
    }

    std::vector<std::byte> out;
    out.reserve(kHeaderLength + cipher.size());
    for (std::byte b : kMagic) {
        out.push_back(b);
    }
    out.push_back(static_cast<std::byte>(kVersion));
    for (std::uint8_t c : salt) {
        out.push_back(static_cast<std::byte>(c));
    }
    for (unsigned char c : nonce) {
        out.push_back(static_cast<std::byte>(c));
    }
    for (unsigned char c : cipher) {
        out.push_back(static_cast<std::byte>(c));
    }
    return out;
}

std::expected<std::string, VaultError>
AccountVault::openWithKey(const Key& key, std::span<const std::byte> blob) {
    if (!ensure_sodium()) {
        return std::unexpected(VaultError::NotInitialized);
    }
    if (blob.size() < kHeaderLength + crypto_secretbox_MACBYTES) {
        return std::unexpected(VaultError::Corrupt);
    }
    for (std::size_t i = 0; i < kMagic.size(); ++i) {
        if (blob[i] != kMagic[i]) {
            return std::unexpected(VaultError::Corrupt);
        }
    }
    std::size_t off = kMagic.size();
    if (std::to_integer<std::uint8_t>(blob[off]) != kVersion) {
        return std::unexpected(VaultError::Corrupt);
    }
    off += 1;
    off += crypto_pwhash_SALTBYTES;  // salt only affects key derivation

    std::array<unsigned char, crypto_secretbox_NONCEBYTES> nonce{};
    for (std::size_t i = 0; i < nonce.size(); ++i) {
        nonce[i] = std::to_integer<unsigned char>(blob[off + i]);
    }
    off += nonce.size();

    const std::size_t cipher_len = blob.size() - off;
    std::vector<unsigned char> cipher(cipher_len);
    for (std::size_t i = 0; i < cipher_len; ++i) {
        cipher[i] = std::to_integer<unsigned char>(blob[off + i]);
    }

    std::vector<unsigned char> plain(cipher_len - crypto_secretbox_MACBYTES);
    const int rc = crypto_secretbox_open_easy(
        plain.data(), cipher.data(), cipher_len, nonce.data(),
        reinterpret_cast<const unsigned char*>(key.data()));
    if (rc != 0) {
        return std::unexpected(VaultError::WrongPassword);
    }

    std::string secret(reinterpret_cast<const char*>(plain.data()), plain.size());
    sodium_memzero(plain.data(), plain.size());
    return secret;
}

std::expected<std::vector<std::byte>, VaultError>
AccountVault::seal(std::string_view password, std::string_view secret) {
    Salt salt{};
    auto key = deriveKey(password, salt);
    if (!key) {
        return std::unexpected(key.error());
    }
    auto out = sealWithKey(*key, salt, secret);
    wipe(*key);
    return out;
}

std::expected<std::string, VaultError>
AccountVault::open(std::string_view password, std::span<const std::byte> blob) {
    auto salt = saltOf(blob);
    if (!salt) {
        return std::unexpected(salt.error());
    }
    auto key = deriveKeyWithSalt(password, *salt);
    if (!key) {
        return std::unexpected(key.error());
    }
    auto out = openWithKey(*key, blob);
    wipe(*key);
    return out;
}

void AccountVault::wipe(Key& key) noexcept {
    sodium_memzero(key.data(), key.size());
}

void AccountVault::wipe(Salt& salt) noexcept {
    sodium_memzero(salt.data(), salt.size());
}

}  // namespace aeromesh
