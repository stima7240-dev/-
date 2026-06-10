// Password-protected account storage for AeroMesh.
//
// An account vault encrypts a small secret (the user's identity secret key, in
// base64) under a user-chosen password. The password never touches disk: it is
// stretched with Argon2id (libsodium crypto_pwhash) into a 256-bit key, and the
// secret is sealed with XSalsa20-Poly1305 (libsodium crypto_secretbox).
//
// On-disk layout (single file, e.g. "account.amv"), format version 2:
//
//   offset  size  field
//   ------  ----  -----------------------------------------------------------
//   0       4     magic "AMV1"
//   4       1     format version (currently 2)
//   5       16    Argon2id salt        (crypto_pwhash_SALTBYTES)
//   21      24    secretbox nonce      (crypto_secretbox_NONCEBYTES)
//   45      ...   ciphertext (plaintext length + crypto_secretbox_MACBYTES)
//
// SECURITY NOTE: the KDF cost parameters (opslimit/memlimit) are NOT stored in
// the file. They are fixed compile-time constants (Argon2id "sensitive":
// ~1 GiB memory, several iterations). An older design stored them in the
// unauthenticated header, which let a tampered file request a huge memory cost
// and crash the app on open. Pinning them in code removes that attack and
// matches how mature clients (e.g. Tox toxencryptsave) work. The format is
// versioned, so if the recommended cost ever changes we bump the version.

#ifndef AEROMESH_ACCOUNT_VAULT_HPP
#define AEROMESH_ACCOUNT_VAULT_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace aeromesh {

enum class VaultError : std::uint8_t {
    NotInitialized,  // init_crypto() was not called / libsodium not ready
    WeakPassword,    // password shorter than kMinPasswordLength
    Corrupt,         // blob is truncated, has a bad magic, or bad version
    WrongPassword,   // authentication failed: wrong password or tampered file
    OutOfMemory,     // Argon2id could not allocate its memory cost
    InternalError,   // unexpected libsodium failure
};

class AccountVault {
public:
    // Minimum password length enforced when deriving a fresh key.
    static constexpr std::size_t kMinPasswordLength = 8;

    // Sizes mirrored from libsodium (checked with static_assert in the .cpp so
    // this header does not need to include <sodium.h>).
    static constexpr std::size_t kSaltBytes = 16;  // crypto_pwhash_SALTBYTES
    static constexpr std::size_t kKeyBytes = 32;   // crypto_secretbox_KEYBYTES

    using Key = std::array<std::uint8_t, kKeyBytes>;
    using Salt = std::array<std::uint8_t, kSaltBytes>;

    // ---- Simple API: password in, bytes out (used by tests / one-shot use) --

    // Encrypt `secret` under `password`. A fresh random salt and nonce are
    // generated on every call, so sealing the same secret twice yields two
    // different blobs. Returns the serialized vault bytes.
    [[nodiscard]] static std::expected<std::vector<std::byte>, VaultError>
    seal(std::string_view password, std::string_view secret);

    // Decrypt a serialized vault produced by seal(). Returns the recovered
    // secret on success, WrongPassword if the password is wrong or the file was
    // tampered with, or Corrupt if the blob is structurally invalid.
    [[nodiscard]] static std::expected<std::string, VaultError>
    open(std::string_view password, std::span<const std::byte> blob);

    // ---- Advanced API: derive the key once, then reuse it ------------------
    //
    // This lets a caller keep the derived key in memory instead of the plain
    // password, so the password can be wiped right after login. Re-sealing a
    // new secret later (e.g. regenerating the identity) reuses the cached key
    // with a fresh nonce, exactly like Tox's Tox_Pass_Key.

    // Derive a fresh key from `password`. A random salt is generated and copied
    // into `out_salt`; cache both the returned key and that salt to re-seal.
    [[nodiscard]] static std::expected<Key, VaultError>
    deriveKey(std::string_view password, Salt& out_salt);

    // Derive the key for an existing salt (used on unlock, where the salt comes
    // from the stored blob). Does not enforce the minimum length.
    [[nodiscard]] static std::expected<Key, VaultError>
    deriveKeyWithSalt(std::string_view password, const Salt& salt);

    // Read the salt stored in a serialized vault blob (so unlock can derive the
    // key without first decrypting). Returns Corrupt for a malformed blob.
    [[nodiscard]] static std::expected<Salt, VaultError>
    saltOf(std::span<const std::byte> blob);

    // Seal `secret` with a pre-derived key and its salt. A fresh random nonce is
    // used on every call.
    [[nodiscard]] static std::expected<std::vector<std::byte>, VaultError>
    sealWithKey(const Key& key, const Salt& salt, std::string_view secret);

    // Open a blob with a pre-derived key.
    [[nodiscard]] static std::expected<std::string, VaultError>
    openWithKey(const Key& key, std::span<const std::byte> blob);

    // Securely wipe a cached key or salt from memory (sodium_memzero).
    static void wipe(Key& key) noexcept;
    static void wipe(Salt& salt) noexcept;
};

}  // namespace aeromesh

#endif  // AEROMESH_ACCOUNT_VAULT_HPP
