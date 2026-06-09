// Tests for the hybrid post-quantum handshake (X25519 + ML-KEM-768): that
// initiator and responder derive the same secret, that distinct sessions
// produce distinct secrets, that tampering breaks agreement, and that the
// derived secret correctly seeds the Double Ratchet end to end.

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <string>
#include <vector>

#include "aeromesh/identity.hpp"
#include "aeromesh/pqhandshake.hpp"
#include "aeromesh/ratchet.hpp"

namespace {

int g_failures = 0;

void check(bool cond, const char* what) {
    if (!cond) {
        std::printf("  [FAIL] %s\n", what);
        ++g_failures;
    } else {
        std::printf("  [ ok ] %s\n", what);
    }
}

using namespace aeromesh;

std::vector<std::byte> bytes_of(const std::string& s) {
    std::vector<std::byte> v(s.size());
    for (std::size_t i = 0; i < s.size(); ++i)
        v[i] = static_cast<std::byte>(static_cast<unsigned char>(s[i]));
    return v;
}

void test_suite_reporting() {
    std::printf("suite reporting (PQ %s)\n", pq_available() ? "ON" : "OFF");
    check(hybrid_suite_name() != nullptr, "suite name is present");
}

void test_hybrid_agreement() {
    std::printf("hybrid key agreement\n");
    auto bob = generate_responder_keys();
    check(bob.has_value(), "bob generates hybrid keys");
    if (!bob) return;

    auto bundle = prekey_bundle(*bob);
    auto init = hybrid_initiate(bundle);
    check(init.has_value(), "alice initiates");
    if (!init) return;

    auto bob_secret = hybrid_respond(
        *bob, std::span<const std::byte>(init->x25519_eph_pub),
        std::span<const std::byte>(init->kem_ciphertext));
    check(bob_secret.has_value(), "bob responds");
    if (!bob_secret) return;

    check(init->shared_secret == *bob_secret,
          "both sides derive the same shared secret");

    if (pq_available())
        check(!init->kem_ciphertext.empty(),
              "KEM ciphertext present when PQ enabled");
}

void test_session_uniqueness() {
    std::printf("session uniqueness\n");
    auto bob = generate_responder_keys();
    if (!bob) { check(false, "bob keygen"); return; }
    auto bundle = prekey_bundle(*bob);

    auto a = hybrid_initiate(bundle);
    auto b = hybrid_initiate(bundle);
    check(a && b, "two initiations succeed");
    if (!(a && b)) return;
    check(a->shared_secret != b->shared_secret,
          "independent sessions yield distinct secrets");
}

void test_tamper_breaks_agreement() {
    std::printf("tamper breaks agreement\n");
    auto bob = generate_responder_keys();
    if (!bob) { check(false, "bob keygen"); return; }
    auto bundle = prekey_bundle(*bob);
    auto init = hybrid_initiate(bundle);
    if (!init) { check(false, "alice initiate"); return; }

    // Flip a byte in the ephemeral X25519 public key.
    auto bad_eph = init->x25519_eph_pub;
    bad_eph[0] ^= std::byte{0x01};
    auto s1 = hybrid_respond(*bob, std::span<const std::byte>(bad_eph),
                             std::span<const std::byte>(init->kem_ciphertext));
    // DH still yields a value, but a different one -> secrets must not match.
    check(!s1.has_value() || *s1 != init->shared_secret,
          "tampered ephemeral key breaks agreement");
}

void test_seeds_ratchet() {
    std::printf("hybrid secret seeds the Double Ratchet\n");
    auto bob = generate_responder_keys();
    if (!bob) { check(false, "bob keygen"); return; }
    auto bundle = prekey_bundle(*bob);
    auto init = hybrid_initiate(bundle);
    if (!init) { check(false, "alice initiate"); return; }
    auto bob_secret = hybrid_respond(
        *bob, std::span<const std::byte>(init->x25519_eph_pub),
        std::span<const std::byte>(init->kem_ciphertext));
    if (!bob_secret) { check(false, "bob respond"); return; }

    // Bob also publishes an initial ratchet key pair; Alice seeds from it.
    RatchetKeyPair bob_ratchet = generate_ratchet_keypair();
    auto alice = Ratchet::init_initiator(
        std::span<const std::byte>(init->shared_secret),
        std::span<const std::byte>(bob_ratchet.pub));
    auto bobr = Ratchet::init_responder(
        std::span<const std::byte>(*bob_secret), bob_ratchet);
    check(alice.has_value() && bobr.has_value(), "both ratchets initialize");
    if (!(alice && bobr)) return;

    auto msg = alice->encrypt(std::span<const std::byte>(bytes_of("pq-sealed")));
    check(msg.has_value(), "alice encrypts over hybrid-seeded ratchet");
    if (!msg) return;
    auto pt = bobr->decrypt(*msg);
    check(pt.has_value() && *pt == bytes_of("pq-sealed"),
          "bob decrypts -- hybrid handshake seeded the ratchet correctly");
}

} // namespace

int main() {
    if (!aeromesh::init_crypto()) {
        std::printf("  [FAIL] libsodium init\n");
        return EXIT_FAILURE;
    }
    test_suite_reporting();
    test_hybrid_agreement();
    test_session_uniqueness();
    test_tamper_breaks_agreement();
    test_seeds_ratchet();
    if (g_failures == 0) {
        std::printf("\nALL TESTS PASSED\n");
        return EXIT_SUCCESS;
    }
    std::printf("\n%d TEST(S) FAILED\n", g_failures);
    return EXIT_FAILURE;
}
