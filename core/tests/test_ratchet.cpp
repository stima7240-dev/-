// Tests for the Double Ratchet: bidirectional round-trips, out-of-order
// delivery via skipped-key caching, repeated DH ratchet steps (ping-pong),
// ciphertext tamper detection, and associated-data binding.

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <string>
#include <vector>

#include <sodium.h>

#include "aeromesh/identity.hpp"
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

bool same(const std::vector<std::byte>& a, const std::string& s) {
    return a == bytes_of(s);
}

// Build an Alice/Bob pair sharing a random initial secret.
struct Pair {
    Ratchet alice;
    Ratchet bob;
};

Pair make_pair() {
    std::array<std::byte, kRatchetKeyLen> sk{};
    randombytes_buf(reinterpret_cast<unsigned char*>(sk.data()), sk.size());
    RatchetKeyPair bob_kp = generate_ratchet_keypair();

    auto alice = Ratchet::init_initiator(std::span<const std::byte>(sk),
                                         std::span<const std::byte>(bob_kp.pub));
    auto bob = Ratchet::init_responder(std::span<const std::byte>(sk), bob_kp);
    // Caller checks success; if either failed we still must return something.
    return Pair{std::move(*alice), std::move(*bob)};
}

void test_basic_roundtrip() {
    std::printf("basic A->B round trip\n");
    auto p = make_pair();
    auto m = p.alice.encrypt(std::span<const std::byte>(bytes_of("hello bob")));
    check(m.has_value(), "alice encrypts");
    if (!m) return;
    auto pt = p.bob.decrypt(*m);
    check(pt.has_value() && same(*pt, "hello bob"), "bob recovers plaintext");
}

void test_bidirectional() {
    std::printf("bidirectional ping-pong\n");
    auto p = make_pair();
    for (int round = 0; round < 4; ++round) {
        const std::string am = "a" + std::to_string(round);
        auto m1 = p.alice.encrypt(std::span<const std::byte>(bytes_of(am)));
        check(m1.has_value(), "alice encrypts round");
        if (!m1) return;
        auto r1 = p.bob.decrypt(*m1);
        check(r1.has_value() && same(*r1, am), "bob decrypts round");

        const std::string bm = "b" + std::to_string(round);
        auto m2 = p.bob.encrypt(std::span<const std::byte>(bytes_of(bm)));
        check(m2.has_value(), "bob encrypts round");
        if (!m2) return;
        auto r2 = p.alice.decrypt(*m2);
        check(r2.has_value() && same(*r2, bm), "alice decrypts round");
    }
}

void test_out_of_order() {
    std::printf("out-of-order delivery\n");
    auto p = make_pair();
    auto m0 = p.alice.encrypt(std::span<const std::byte>(bytes_of("msg0")));
    auto m1 = p.alice.encrypt(std::span<const std::byte>(bytes_of("msg1")));
    auto m2 = p.alice.encrypt(std::span<const std::byte>(bytes_of("msg2")));
    check(m0 && m1 && m2, "alice encrypts three");
    if (!(m0 && m1 && m2)) return;

    // Bob receives them reversed: 2, then 0, then 1.
    auto r2 = p.bob.decrypt(*m2);
    check(r2.has_value() && same(*r2, "msg2"), "decrypt msg2 first (skips 0,1)");
    auto r0 = p.bob.decrypt(*m0);
    check(r0.has_value() && same(*r0, "msg0"), "decrypt cached msg0");
    auto r1 = p.bob.decrypt(*m1);
    check(r1.has_value() && same(*r1, "msg1"), "decrypt cached msg1");
}

void test_tamper_detected() {
    std::printf("ciphertext tamper detection\n");
    auto p = make_pair();
    auto m = p.alice.encrypt(std::span<const std::byte>(bytes_of("secret")));
    check(m.has_value(), "alice encrypts");
    if (!m) return;
    RatchetMessage tampered = *m;
    if (!tampered.ciphertext.empty())
        tampered.ciphertext[0] ^= std::byte{0x01};
    auto pt = p.bob.decrypt(tampered);
    check(!pt.has_value(), "flipped ciphertext byte is rejected");
}

void test_associated_data() {
    std::printf("associated-data binding\n");
    auto p = make_pair();
    const auto ad_ok = bytes_of("conv-42");
    const auto ad_bad = bytes_of("conv-99");
    auto m = p.alice.encrypt(std::span<const std::byte>(bytes_of("bound")),
                             std::span<const std::byte>(ad_ok));
    check(m.has_value(), "alice encrypts with AD");
    if (!m) return;
    auto wrong = p.bob.decrypt(*m, std::span<const std::byte>(ad_bad));
    check(!wrong.has_value(), "wrong AD is rejected");
    auto right = p.bob.decrypt(*m, std::span<const std::byte>(ad_ok));
    check(right.has_value() && same(*right, "bound"), "correct AD decrypts");
}

} // namespace

int main() {
    if (!aeromesh::init_crypto()) {
        std::printf("  [FAIL] libsodium init\n");
        return EXIT_FAILURE;
    }
    test_basic_roundtrip();
    test_bidirectional();
    test_out_of_order();
    test_tamper_detected();
    test_associated_data();
    if (g_failures == 0) {
        std::printf("\nALL TESTS PASSED\n");
        return EXIT_SUCCESS;
    }
    std::printf("\n%d TEST(S) FAILED\n", g_failures);
    return EXIT_FAILURE;
}
