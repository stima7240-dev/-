#pragma once

// Double Ratchet (Signal-style) for end-to-end message encryption.
//
// Two guarantees:
//   * Forward secrecy -- each message uses a one-time key derived from a
//     symmetric KDF chain; compromising current state never reveals past
//     messages.
//   * Post-compromise security -- every time the peer's ratchet public key
//     changes, a fresh X25519 Diffie-Hellman output is mixed into the root
//     key, so the session "heals" after a key compromise.
//
// Out-of-order and dropped messages are handled by caching skipped message
// keys (bounded by kRatchetMaxSkip to resist memory-exhaustion abuse).
//
// Crypto: X25519 (crypto_scalarmult) for the DH ratchet, keyed BLAKE2b for the
// root/chain KDFs, and XChaCha20-Poly1305 AEAD for message encryption. The
// post-quantum (Kyber) hybrid is layered on top in a later step.

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <span>
#include <utility>
#include <vector>

namespace aeromesh {

inline constexpr std::size_t kRatchetKeyLen = 32;
inline constexpr std::uint32_t kRatchetMaxSkip = 256;

enum class RatchetError {
    InvalidKeyLength,
    NotSending,    // tried to encrypt before a sending chain exists
    EncryptFailed,
    DecryptFailed,
    TooManySkipped,
    DhFailed,
};

using RatchetKey = std::array<std::byte, kRatchetKeyLen>;

struct RatchetKeyPair {
    RatchetKey pub{};
    RatchetKey sec{};
};

// Generate a fresh X25519 ratchet key pair.
RatchetKeyPair generate_ratchet_keypair();

// A ciphertext together with the ratchet header needed to decrypt it.
struct RatchetMessage {
    RatchetKey dh_pub{};      // sender's current ratchet public key
    std::uint32_t pn = 0;     // number of messages in the previous sending chain
    std::uint32_t n = 0;      // message number within the current sending chain
    std::vector<std::byte> ciphertext;
};

class Ratchet {
public:
    // Initiator ("Alice") -- sends first. Both sides begin from a shared secret
    // (from the initial handshake); the initiator also receives the peer's
    // initial ratchet public key.
    static std::expected<Ratchet, RatchetError> init_initiator(
        std::span<const std::byte> shared_secret,
        std::span<const std::byte> peer_dh_pub);

    // Responder ("Bob") -- receives first, using its own initial ratchet key
    // pair (whose public half the initiator was given).
    static std::expected<Ratchet, RatchetError> init_responder(
        std::span<const std::byte> shared_secret,
        const RatchetKeyPair& dh_keypair);

    // Encrypt the next outbound message, advancing the sending chain.
    std::expected<RatchetMessage, RatchetError> encrypt(
        std::span<const std::byte> plaintext,
        std::span<const std::byte> associated_data = {});

    // Decrypt an inbound message, performing a DH ratchet step and/or skipping
    // message keys as needed.
    std::expected<std::vector<std::byte>, RatchetError> decrypt(
        const RatchetMessage& msg,
        std::span<const std::byte> associated_data = {});

private:
    Ratchet() = default;

    // Core decrypt logic. Mutates state; the public decrypt() runs this on a
    // throwaway copy and only commits when it succeeds, so a forged message
    // that fails authentication cannot desynchronize the ratchet.
    std::expected<std::vector<std::byte>, RatchetError> decrypt_impl(
        const RatchetMessage& msg, std::span<const std::byte> associated_data);

    // Decrypt msg under a specific message key (verifies the bound header).
    std::expected<std::vector<std::byte>, RatchetError> try_decrypt(
        const RatchetKey& mk, const RatchetMessage& msg,
        std::span<const std::byte> associated_data);

    // Advance the receiving chain up to `until`, caching skipped message keys.
    std::expected<void, RatchetError> skip_message_keys(std::uint32_t until);

    // Perform a DH ratchet step toward the peer's new ratchet public key.
    std::expected<void, RatchetError> dh_ratchet(const RatchetKey& peer_pub);

    RatchetKeyPair dhs_;        // our current ratchet key pair
    RatchetKey dhr_{};         // peer's current ratchet public key
    bool have_dhr_ = false;
    RatchetKey rk_{};          // root key
    RatchetKey cks_{};         // sending chain key
    RatchetKey ckr_{};         // receiving chain key
    bool have_cks_ = false;
    bool have_ckr_ = false;
    std::uint32_t ns_ = 0;     // sending message number
    std::uint32_t nr_ = 0;     // receiving message number
    std::uint32_t pn_ = 0;     // previous sending chain length
    std::map<std::pair<RatchetKey, std::uint32_t>, RatchetKey> skipped_;
};

} // namespace aeromesh
