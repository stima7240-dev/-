// Tests for the ephemeral payload store: ACK-destroy (single and multi-ack),
// 12h-style TTL garbage collection with the right destroy reason, manual
// destroy, error paths, next-expiry scheduling, and panic wipe.

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "aeromesh/ephemeral.hpp"
#include "aeromesh/identity.hpp"

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

EntryId make_id(std::uint8_t seed) {
    EntryId id{};
    for (std::size_t i = 0; i < id.size(); ++i)
        id[i] = static_cast<std::byte>(seed + static_cast<std::uint8_t>(i));
    return id;
}

std::vector<std::byte> payload(std::size_t n, std::uint8_t v) {
    return std::vector<std::byte>(n, std::byte{v});
}

void test_put_get() {
    std::printf("put / get / contains\n");
    EphemeralStore store;
    auto id = make_id(1);
    auto p = store.put(id, payload(16, 0xAB), 1, 1000);
    check(p.has_value(), "put succeeds");
    check(store.contains(id), "contains after put");
    check(store.size() == 1, "size == 1");
    auto got = store.get(id);
    check(got.has_value() && got->size() == 16 &&
              (*got)[0] == std::byte{0xAB},
          "get returns the payload");
}

void test_duplicate_and_invalid() {
    std::printf("duplicate / invalid params\n");
    EphemeralStore store;
    auto id = make_id(2);
    (void)store.put(id, payload(4, 1), 1, 0);
    auto dup = store.put(id, payload(4, 2), 1, 0);
    check(!dup.has_value() && dup.error() == StoreError::Duplicate,
          "duplicate put rejected");
    auto bad = store.put(make_id(3), payload(4, 1), 0, 0);
    check(!bad.has_value() && bad.error() == StoreError::InvalidParams,
          "acks_required == 0 rejected");
}

void test_ack_destroy_single() {
    std::printf("single-ack destroy\n");
    EphemeralStore store;
    auto id = make_id(4);
    (void)store.put(id, payload(32, 0x7F), 1, 0);
    auto r = store.ack(id);
    check(r.has_value() && *r == true, "ack destroys at required count");
    check(!store.contains(id), "entry gone after ack");
    auto again = store.ack(id);
    check(!again.has_value() && again.error() == StoreError::NotFound,
          "ack on destroyed entry is NotFound");
}

void test_ack_destroy_multi() {
    std::printf("multi-ack destroy\n");
    EphemeralStore store;
    auto id = make_id(5);
    (void)store.put(id, payload(8, 0x11), 3, 0);
    auto a1 = store.ack(id);
    auto a2 = store.ack(id);
    check(a1.has_value() && *a1 == false, "1st ack does not destroy");
    check(a2.has_value() && *a2 == false, "2nd ack does not destroy");
    check(store.contains(id), "still present before final ack");
    auto a3 = store.ack(id);
    check(a3.has_value() && *a3 == true, "3rd ack destroys");
    check(!store.contains(id), "gone after final ack");
}

void test_ttl_expiry() {
    std::printf("TTL expiry\n");
    EphemeralStore store(1000);  // 1s TTL for the test
    auto id = make_id(6);
    (void)store.put(id, payload(64, 0x22), 5, 10000);

    auto none = store.collect_expired(10500);  // 500ms old, not expired
    check(none.empty(), "nothing collected before TTL");
    check(store.contains(id), "still present before TTL");

    auto gone = store.collect_expired(11000);  // exactly TTL old
    check(gone.size() == 1, "one entry collected at TTL");
    check(gone.size() == 1 && gone[0].reason == DestroyReason::Expired,
          "destroy reason is Expired");
    check(!store.contains(id), "entry wiped after TTL");
}

void test_next_expiry() {
    std::printf("next expiry scheduling\n");
    EphemeralStore store(1000);
    check(!store.next_expiry_ms(0).has_value(), "no expiry when empty");
    (void)store.put(make_id(7), payload(4, 1), 1, 5000);
    (void)store.put(make_id(8), payload(4, 1), 1, 6000);
    auto next = store.next_expiry_ms(5500);
    // earliest deadline = 5000+1000 = 6000; remaining from 5500 = 500.
    check(next.has_value() && *next == 500, "soonest remaining ms");
}

void test_destroy_and_wipe_all() {
    std::printf("manual destroy / wipe_all\n");
    EphemeralStore store;
    auto id = make_id(9);
    (void)store.put(id, payload(16, 0x33), 2, 0);
    check(store.destroy(id), "destroy returns true for present");
    check(!store.destroy(id), "destroy returns false for absent");
    (void)store.put(make_id(10), payload(4, 1), 1, 0);
    (void)store.put(make_id(11), payload(4, 1), 1, 0);
    store.wipe_all();
    check(store.size() == 0, "wipe_all empties the store");
}

} // namespace

int main() {
    if (!aeromesh::init_crypto()) {
        std::printf("  [FAIL] init_crypto\n");
        return EXIT_FAILURE;
    }
    test_put_get();
    test_duplicate_and_invalid();
    test_ack_destroy_single();
    test_ack_destroy_multi();
    test_ttl_expiry();
    test_next_expiry();
    test_destroy_and_wipe_all();
    if (g_failures == 0) {
        std::printf("\nALL TESTS PASSED\n");
        return EXIT_SUCCESS;
    }
    std::printf("\n%d TEST(S) FAILED\n", g_failures);
    return EXIT_FAILURE;
}
