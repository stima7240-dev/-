#include "aeromesh/identity.hpp"
#include "aeromesh/session.hpp"

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace {

int g_failures = 0;

void check(bool cond, const char* what) {
    if (cond) {
        std::cout << "[ ok ] " << what << "\n";
    } else {
        std::cout << "[FAIL] " << what << "\n";
        ++g_failures;
    }
}

std::vector<std::byte> to_bytes(std::string_view s) {
    std::vector<std::byte> b;
    b.reserve(s.size());
    for (char c : s) {
        b.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
    }
    return b;
}

bool bytes_equal(std::span<const std::byte> a, std::span<const std::byte> b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
}

std::byte flip(std::byte v) {
    return static_cast<std::byte>(std::to_integer<unsigned>(v) ^ 0xFFu);
}

} // namespace

int main() {
    using namespace aeromesh;

    if (!init_crypto()) {
        std::cout << "[FAIL] init_crypto\n";
        return EXIT_FAILURE;
    }

    auto alice_r = Identity::generate();
    auto bob_r = Identity::generate();
    check(alice_r.has_value() && bob_r.has_value(), "generate two identities");
    if (!alice_r || !bob_r) {
        return EXIT_FAILURE;
    }
    const Identity& alice = alice_r.value();
    const Identity& bob = bob_r.value();

    // Bob (responder) publishes a signed prekey bundle.
    auto resp = SecureSession::create_responder(bob);
    check(resp.has_value(), "responder creates a signed bundle");
    if (!resp) {
        return EXIT_FAILURE;
    }
    const SignedPrekeyBundle bundle = resp.value().bundle();

    // Alice (initiator) pins Bob's identity and runs the handshake.
    auto ir = SecureSession::initiate(alice, bob.public_key(), bundle);
    check(ir.has_value(), "initiator completes the handshake");
    if (!ir) {
        return EXIT_FAILURE;
    }
    SecureSession sess_a = std::move(ir.value().first);
    const SessionInitiation initiation = ir.value().second;

    // Bob accepts the initiation.
    auto sb = SecureSession::accept(std::move(resp.value()), initiation);
    check(sb.has_value(), "responder accepts the initiation");
    if (!sb) {
        return EXIT_FAILURE;
    }
    SecureSession sess_b = std::move(sb.value());

    // Mutual authentication: each side learned the other's true identity.
    check(sess_a.peer_identity() == bob.public_key(),
          "Alice ends up with Bob's verified identity");
    check(sess_b.peer_identity() == alice.public_key(),
          "Bob ends up with Alice's verified identity");

    // Alice -> Bob.
    const std::vector<std::byte> m1 = to_bytes("hello bob -- e2e + post-quantum");
    auto ct1 = sess_a.encrypt(m1);
    check(ct1.has_value(), "Alice encrypts message 1");
    if (ct1) {
        auto pt1 = sess_b.decrypt(ct1.value());
        check(pt1.has_value() && bytes_equal(pt1.value(), m1),
              "Bob decrypts message 1 correctly");
    }

    // Bob -> Alice (exercises the DH ratchet in the reverse direction).
    const std::vector<std::byte> m2 = to_bytes("hi alice, the ratchet stepped");
    auto ct2 = sess_b.encrypt(m2);
    check(ct2.has_value(), "Bob encrypts message 2");
    if (ct2) {
        auto pt2 = sess_a.decrypt(ct2.value());
        check(pt2.has_value() && bytes_equal(pt2.value(), m2),
              "Alice decrypts message 2 correctly");
    }

    // MITM #1: a bundle pinned to the wrong identity is rejected.
    {
        auto bad = SecureSession::initiate(alice, alice.public_key(), bundle);
        check(!bad.has_value(), "rejects bundle pinned to the wrong identity");
    }

    // MITM #2: a tampered bundle signature is rejected.
    {
        SignedPrekeyBundle tampered = bundle;
        tampered.signature[0] = flip(tampered.signature[0]);
        auto bad = SecureSession::initiate(alice, bob.public_key(), tampered);
        check(!bad.has_value(), "rejects a tampered bundle signature");
    }

    // MITM #3: a tampered initiation signature is rejected by the responder.
    {
        auto resp2 = SecureSession::create_responder(bob);
        check(resp2.has_value(), "setup: second responder bundle");
        if (resp2) {
            const SignedPrekeyBundle b2 = resp2.value().bundle();
            auto ir2 = SecureSession::initiate(alice, bob.public_key(), b2);
            check(ir2.has_value(), "setup: second initiation");
            if (ir2) {
                SessionInitiation tampered = ir2.value().second;
                tampered.signature[0] = flip(tampered.signature[0]);
                auto bad =
                    SecureSession::accept(std::move(resp2.value()), tampered);
                check(!bad.has_value(),
                      "responder rejects a tampered initiation");
            }
        }
    }

    if (g_failures == 0) {
        std::cout << "All secure session tests passed.\n";
        return EXIT_SUCCESS;
    }
    std::cout << g_failures << " secure session test(s) failed.\n";
    return EXIT_FAILURE;
}
