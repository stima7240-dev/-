// Tests for the onion routing layer: layered seal/peel, per-hop blindness,
// wrong-key rejection, and direct (single-hop) messages.

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <string>
#include <vector>

#include <sodium.h>

#include "aeromesh/identity.hpp"
#include "aeromesh/onion.hpp"

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

// A relay's X25519 keypair derived from a fresh identity.
struct Relay {
    std::array<std::byte, 32> pub{};
    std::array<std::byte, 32> sec{};
    std::string endpoint;
};

bool make_relay(const std::string& endpoint, Relay& out) {
    auto id = Identity::generate();
    if (!id) return false;
    auto pub = id->x25519_public();
    auto sec = id->x25519_secret();
    if (!pub || !sec) return false;
    out.pub = *pub;
    out.sec = *sec;
    out.endpoint = endpoint;
    return true;
}

void test_three_hop_path() {
    std::printf("three-hop onion\n");
    Relay h0, h1, h2;
    check(make_relay("10.0.0.1:5000", h0) && make_relay("10.0.0.2:5000", h1) &&
              make_relay("10.0.0.3:5000", h2),
          "relay keypairs derived");

    const std::string msg = "the eagle lands at midnight";
    std::vector<OnionHop> path{
        {h0.pub, h0.endpoint}, {h1.pub, h1.endpoint}, {h2.pub, h2.endpoint}};

    auto onion = build_onion(path, std::span<const std::byte>(bytes_of(msg)));
    check(onion.has_value(), "build_onion succeeds");
    if (!onion) return;

    // Hop 0 peels: learns only that the next hop is h1, not the payload.
    auto p0 = peel_onion(std::span<const std::byte>(*onion), h0.pub, h0.sec);
    check(p0.has_value(), "hop 0 opens its layer");
    if (!p0) return;
    check(!p0->is_exit, "hop 0 is a relay, not the exit");
    check(p0->next_endpoint == h1.endpoint, "hop 0 learns next hop = h1");
    check(p0->data != bytes_of(msg), "hop 0 cannot see the payload");

    // A relay must NOT be able to open a layer addressed to a different hop.
    auto wrong = peel_onion(std::span<const std::byte>(*onion), h1.pub, h1.sec);
    check(!wrong.has_value() && wrong.error() == OnionError::OpenFailed,
          "wrong key cannot open the outer layer");

    // Hop 1 peels the inner onion.
    auto p1 = peel_onion(std::span<const std::byte>(p0->data), h1.pub, h1.sec);
    check(p1.has_value(), "hop 1 opens its layer");
    if (!p1) return;
    check(!p1->is_exit, "hop 1 is a relay");
    check(p1->next_endpoint == h2.endpoint, "hop 1 learns next hop = h2");

    // Hop 2 is the exit and recovers the plaintext.
    auto p2 = peel_onion(std::span<const std::byte>(p1->data), h2.pub, h2.sec);
    check(p2.has_value(), "hop 2 opens its layer");
    if (!p2) return;
    check(p2->is_exit, "hop 2 is the exit");
    check(p2->data == bytes_of(msg), "exit recovers the original payload");
}

void test_direct_message() {
    std::printf("single-hop (direct) onion\n");
    Relay r;
    check(make_relay("127.0.0.1:9000", r), "recipient keypair derived");
    const std::string msg = "hi";
    std::vector<OnionHop> path{ OnionHop{r.pub, r.endpoint} };
    auto onion = build_onion(path, std::span<const std::byte>(bytes_of(msg)));
    check(onion.has_value(), "build_onion (1 hop) succeeds");
    if (!onion) return;
    auto p = peel_onion(std::span<const std::byte>(*onion), r.pub, r.sec);
    check(p.has_value() && p->is_exit, "recipient is the exit");
    check(p && p->data == bytes_of(msg), "direct payload recovered");
}

void test_edge_cases() {
    std::printf("onion edge cases\n");
    const auto empty_payload = bytes_of("");
    check(!build_onion({}, std::span<const std::byte>(empty_payload)).has_value(),
          "empty path rejected");

    Relay r;
    make_relay("x:1", r);
    std::vector<OnionHop> too_long(kMaxOnionHops + 1, OnionHop{r.pub, r.endpoint});
    check(!build_onion(too_long, std::span<const std::byte>(empty_payload))
               .has_value(),
          "over-long path rejected");

    std::vector<std::byte> garbage(crypto_box_SEALBYTES + 4, std::byte{0x7});
    check(!peel_onion(std::span<const std::byte>(garbage), r.pub, r.sec)
               .has_value(),
          "garbage layer fails to open");
}

} // namespace

int main() {
    if (!aeromesh::init_crypto()) {
        std::printf("  [FAIL] libsodium init\n");
        return EXIT_FAILURE;
    }
    test_three_hop_path();
    test_direct_message();
    test_edge_cases();
    if (g_failures == 0) {
        std::printf("\nALL TESTS PASSED\n");
        return EXIT_SUCCESS;
    }
    std::printf("\n%d TEST(S) FAILED\n", g_failures);
    return EXIT_FAILURE;
}
