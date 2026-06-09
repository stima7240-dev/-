#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>

namespace aeromesh {

inline constexpr std::size_t kPublicKeyLen = 32; // Ed25519 public key
inline constexpr std::size_t kSecretKeyLen = 64; // Ed25519 secret key (libsodium layout)
inline constexpr std::size_t kSignatureLen = 64; // Ed25519 detached signature

enum class CryptoError : std::uint8_t {
    NotInitialized,
    InvalidEncoding,
    InvalidKeyLength,
    InternalError,
};

// A long-term cryptographic identity. The public key *is* the user's address:
// there are no phone numbers, emails, or server-side accounts. Contacts are
// exchanged out-of-band via share_string() / QR code.
class Identity {
public:
    using PublicKey = std::array<std::byte, kPublicKeyLen>;
    using SecretKey = std::array<std::byte, kSecretKeyLen>;

    // Generate a fresh random identity. init_crypto() must have run first.
    [[nodiscard]] static std::expected<Identity, CryptoError> generate();

    // Restore an identity from a base64 secret-key blob produced by
    // export_secret_b64() (read back from the local encrypted store).
    [[nodiscard]] static std::expected<Identity, CryptoError> from_secret_b64(
        std::string_view b64);

    [[nodiscard]] const PublicKey& public_key() const noexcept { return pk_; }

    // Short, human-comparable fingerprint for out-of-band verification,
    // e.g. "A1B2 C3D4 E5F6 0718" (first 8 bytes of the public key, hex).
    [[nodiscard]] std::string fingerprint() const;

    // The shareable contact string (base64 of the public key) for QR exchange.
    [[nodiscard]] std::string share_string() const;

    // Export the secret key (base64) for the LOCAL ENCRYPTED database only.
    // Never transmit this over the network.
    [[nodiscard]] std::string export_secret_b64() const;

    // Derive the X25519 key-agreement keypair from this Ed25519 identity,
    // used to seed the Double Ratchet session (see ratchet.hpp, planned).
    [[nodiscard]] std::expected<std::array<std::byte, 32>, CryptoError>
    x25519_public() const;

    // Derive the X25519 secret scalar from this Ed25519 identity. Needed
    // locally to open sealed onion layers and ratchet handshakes addressed to
    // us. This is key material -- never transmit it over the network.
    [[nodiscard]] std::expected<std::array<std::byte, 32>, CryptoError>
    x25519_secret() const;

    // Sign a message with this identity's Ed25519 secret key (detached).
    // Used to authenticate handshake transcripts so a man-in-the-middle
    // cannot substitute key material without invalidating the signature.
    [[nodiscard]] std::expected<std::array<std::byte, kSignatureLen>, CryptoError>
    sign(std::span<const std::byte> message) const;

    // Verify a detached Ed25519 signature against a peer's public key.
    [[nodiscard]] static bool verify(
        const PublicKey& pub,
        std::span<const std::byte> message,
        std::span<const std::byte, kSignatureLen> signature);

private:
    Identity() = default;
    PublicKey pk_{};
    SecretKey sk_{};
};

// Parse a peer's share_string() back into a raw public key.
[[nodiscard]] std::expected<Identity::PublicKey, CryptoError>
parse_share_string(std::string_view share);

// Initialise libsodium. Call once at process start before any other crypto.
[[nodiscard]] bool init_crypto() noexcept;

} // namespace aeromesh
