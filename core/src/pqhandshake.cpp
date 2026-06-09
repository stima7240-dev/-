#include "aeromesh/pqhandshake.hpp"

#include <sodium.h>

#include <array>
#include <cstring>
#include <vector>

#ifdef AEROMESH_HAVE_PQ
#include <oqs/oqs.h>
#endif

namespace aeromesh {
namespace {

const unsigned char* uc(const std::byte* p) {
    return reinterpret_cast<const unsigned char*>(p);
}
unsigned char* uc(std::byte* p) {
    return reinterpret_cast<unsigned char*>(p);
}

// X25519 base point multiply: derive a public key from a secret scalar.
void x25519_base(const std::array<std::byte, 32>& sec,
                 std::array<std::byte, 32>& pub) {
    crypto_scalarmult_base(uc(pub.data()), uc(sec.data()));
}

// X25519 Diffie-Hellman.
bool x25519_dh(const std::array<std::byte, 32>& sec,
               std::span<const std::byte> peer_pub,
               std::array<std::byte, 32>& out) {
    if (peer_pub.size() != 32)
        return false;
    return crypto_scalarmult(uc(out.data()), uc(sec.data()),
                             uc(peer_pub.data())) == 0;
}

// Combine the classical and post-quantum shared secrets into one 32-byte key.
// Keyed BLAKE2b over (x25519_ss || kem_ss) binds both contributions; the key
// acts as a domain-separation context so this output can't collide with other
// uses of the same DH/KEM material.
void combine_secrets(std::span<const std::byte> x25519_ss,
                     std::span<const std::byte> kem_ss,
                     std::array<std::byte, kHybridSecretLen>& out) {
    static const unsigned char ctx[] = {'a', 'e', 'r', 'o', 'm', 'e', 's', 'h',
                                        '-', 'p', 'q', 'x', 'd', 'h', '-', 'v',
                                        '1'};
    std::vector<unsigned char> in;
    in.reserve(x25519_ss.size() + kem_ss.size());
    in.insert(in.end(), uc(x25519_ss.data()),
              uc(x25519_ss.data()) + x25519_ss.size());
    in.insert(in.end(), uc(kem_ss.data()),
              uc(kem_ss.data()) + kem_ss.size());
    crypto_generichash(uc(out.data()), out.size(), in.data(), in.size(), ctx,
                       sizeof(ctx));
    sodium_memzero(in.data(), in.size());
}

#ifdef AEROMESH_HAVE_PQ
constexpr bool kHavePq = true;

// RAII wrapper around an OQS_KEM handle for ML-KEM-768.
struct Kem {
    OQS_KEM* kem = nullptr;
    Kem() { kem = OQS_KEM_new(OQS_KEM_alg_ml_kem_768); }
    ~Kem() {
        if (kem)
            OQS_KEM_free(kem);
    }
    explicit operator bool() const { return kem != nullptr; }
};
#else
constexpr bool kHavePq = false;
#endif

} // namespace

bool pq_available() { return kHavePq; }

const char* hybrid_suite_name() {
    return kHavePq ? "X25519+ML-KEM-768" : "X25519 (classical only)";
}

std::expected<HybridResponderKeys, PqError> generate_responder_keys() {
    HybridResponderKeys keys;
    randombytes_buf(uc(keys.x25519_sec.data()), keys.x25519_sec.size());
    x25519_base(keys.x25519_sec, keys.x25519_pub);

#ifdef AEROMESH_HAVE_PQ
    Kem kem;
    if (!kem)
        return std::unexpected(PqError::KemUnavailable);
    keys.kem_pub.resize(kem.kem->length_public_key);
    keys.kem_sec.resize(kem.kem->length_secret_key);
    if (OQS_KEM_keypair(kem.kem, uc(keys.kem_pub.data()),
                        uc(keys.kem_sec.data())) != OQS_SUCCESS)
        return std::unexpected(PqError::KeygenFailed);
#endif
    return keys;
}

HybridPrekeyBundle prekey_bundle(const HybridResponderKeys& keys) {
    HybridPrekeyBundle bundle;
    bundle.x25519_pub = keys.x25519_pub;
    bundle.kem_pub = keys.kem_pub;
    return bundle;
}

std::expected<HybridInitiation, PqError> hybrid_initiate(
    const HybridPrekeyBundle& bundle) {
    HybridInitiation out;

    // Ephemeral classical key pair.
    std::array<std::byte, 32> eph_sec{};
    randombytes_buf(uc(eph_sec.data()), eph_sec.size());
    x25519_base(eph_sec, out.x25519_eph_pub);

    std::array<std::byte, 32> x25519_ss{};
    if (!x25519_dh(eph_sec, std::span<const std::byte>(bundle.x25519_pub),
                   x25519_ss)) {
        sodium_memzero(uc(eph_sec.data()), eph_sec.size());
        return std::unexpected(PqError::DhFailed);
    }

    std::vector<std::byte> kem_ss;
#ifdef AEROMESH_HAVE_PQ
    Kem kem;
    if (!kem) {
        sodium_memzero(uc(eph_sec.data()), eph_sec.size());
        return std::unexpected(PqError::KemUnavailable);
    }
    if (bundle.kem_pub.size() != kem.kem->length_public_key) {
        sodium_memzero(uc(eph_sec.data()), eph_sec.size());
        return std::unexpected(PqError::Malformed);
    }
    out.kem_ciphertext.resize(kem.kem->length_ciphertext);
    kem_ss.resize(kem.kem->length_shared_secret);
    if (OQS_KEM_encaps(kem.kem, uc(out.kem_ciphertext.data()),
                       uc(kem_ss.data()), uc(bundle.kem_pub.data())) !=
        OQS_SUCCESS) {
        sodium_memzero(uc(eph_sec.data()), eph_sec.size());
        return std::unexpected(PqError::EncapsFailed);
    }
#endif

    combine_secrets(std::span<const std::byte>(x25519_ss),
                    std::span<const std::byte>(kem_ss), out.shared_secret);
    sodium_memzero(uc(eph_sec.data()), eph_sec.size());
    sodium_memzero(uc(x25519_ss.data()), x25519_ss.size());
    if (!kem_ss.empty())
        sodium_memzero(kem_ss.data(), kem_ss.size());
    return out;
}

std::expected<std::array<std::byte, kHybridSecretLen>, PqError> hybrid_respond(
    const HybridResponderKeys& keys,
    std::span<const std::byte> initiator_x25519_eph_pub,
    std::span<const std::byte> kem_ciphertext) {
    if (initiator_x25519_eph_pub.size() != 32)
        return std::unexpected(PqError::InvalidKeyLength);

    std::array<std::byte, 32> x25519_ss{};
    if (!x25519_dh(keys.x25519_sec, initiator_x25519_eph_pub, x25519_ss))
        return std::unexpected(PqError::DhFailed);

    std::vector<std::byte> kem_ss;
#ifdef AEROMESH_HAVE_PQ
    Kem kem;
    if (!kem)
        return std::unexpected(PqError::KemUnavailable);
    if (kem_ciphertext.size() != kem.kem->length_ciphertext ||
        keys.kem_sec.size() != kem.kem->length_secret_key) {
        sodium_memzero(uc(x25519_ss.data()), x25519_ss.size());
        return std::unexpected(PqError::Malformed);
    }
    kem_ss.resize(kem.kem->length_shared_secret);
    if (OQS_KEM_decaps(kem.kem, uc(kem_ss.data()), uc(kem_ciphertext.data()),
                       uc(keys.kem_sec.data())) != OQS_SUCCESS) {
        sodium_memzero(uc(x25519_ss.data()), x25519_ss.size());
        return std::unexpected(PqError::DecapsFailed);
    }
#else
    (void)kem_ciphertext;
#endif

    std::array<std::byte, kHybridSecretLen> shared{};
    combine_secrets(std::span<const std::byte>(x25519_ss),
                    std::span<const std::byte>(kem_ss), shared);
    sodium_memzero(uc(x25519_ss.data()), x25519_ss.size());
    if (!kem_ss.empty())
        sodium_memzero(kem_ss.data(), kem_ss.size());
    return shared;
}

} // namespace aeromesh
