// Tests for Reed-Solomon erasure coding over GF(256): systematic layout,
// reconstruction from data/parity erasures up to the parity budget, and
// rejection when too few shards remain.

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "aeromesh/reed_solomon.hpp"

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

// Deterministic pseudo-random shard contents.
std::vector<std::vector<std::byte>> make_data(int data_shards, std::size_t len,
                                              int total) {
    std::vector<std::vector<std::byte>> shards(static_cast<std::size_t>(total));
    std::uint32_t state = 0x1234567u;
    for (int s = 0; s < data_shards; ++s) {
        shards[s].resize(len);
        for (std::size_t i = 0; i < len; ++i) {
            state = state * 1664525u + 1013904223u;
            shards[s][i] = static_cast<std::byte>((state >> 24) & 0xFF);
        }
    }
    return shards;
}

bool shards_equal(const std::vector<std::byte>& a,
                  const std::vector<std::byte>& b) {
    return a == b;
}

void test_systematic() {
    std::printf("systematic layout\n");
    auto rs = ReedSolomon::create(4, 2);
    check(rs.has_value(), "create 4+2");
    if (!rs) return;
    const std::size_t len = 64;
    auto shards = make_data(4, len, rs->total_shards());
    auto original = shards;  // copy of data shards
    auto e = rs->encode(shards);
    check(e.has_value(), "encode succeeds");
    bool same = true;
    for (int i = 0; i < 4; ++i)
        same = same && shards_equal(shards[i], original[i]);
    check(same, "data shards unchanged (systematic)");
}

void recover_and_verify(int data_shards, int parity_shards,
                        const std::vector<int>& erase, const char* label) {
    std::printf("%s\n", label);
    auto rs = ReedSolomon::create(data_shards, parity_shards);
    check(rs.has_value(), "create");
    if (!rs) return;
    const std::size_t len = 128;
    auto shards = make_data(data_shards, len, rs->total_shards());
    auto reference = shards;  // keep data shards for comparison
    auto e = rs->encode(shards);
    check(e.has_value(), "encode");
    if (!e) return;
    auto full = shards;  // complete set (data + parity)

    std::vector<bool> present(static_cast<std::size_t>(rs->total_shards()),
                              true);
    for (int idx : erase) {
        present[static_cast<std::size_t>(idx)] = true;  // reset guard
    }
    for (int idx : erase) {
        present[static_cast<std::size_t>(idx)] = false;
        shards[static_cast<std::size_t>(idx)].clear();
    }

    auto r = rs->reconstruct(shards, present);
    check(r.has_value(), "reconstruct succeeds");
    if (!r) return;
    bool ok = true;
    for (int i = 0; i < rs->total_shards(); ++i)
        ok = ok && shards_equal(shards[static_cast<std::size_t>(i)],
                                full[static_cast<std::size_t>(i)]);
    check(ok, "all shards match the originals");
}

void test_too_few() {
    std::printf("too few shards rejected\n");
    auto rs = ReedSolomon::create(4, 2);
    if (!rs) { check(false, "create"); return; }
    const std::size_t len = 32;
    auto shards = make_data(4, len, rs->total_shards());
    auto e = rs->encode(shards);
    if (!e) { check(false, "encode"); return; }

    // Erase 3 of 6 -> only 3 present, need 4.
    std::vector<bool> present(6, true);
    for (int idx : {0, 1, 4}) {
        present[static_cast<std::size_t>(idx)] = false;
        shards[static_cast<std::size_t>(idx)].clear();
    }
    auto r = rs->reconstruct(shards, present);
    check(!r.has_value() && r.error() == RsError::TooFewShards,
          "reconstruct rejects fewer than data_shards");
}

} // namespace

int main() {
    test_systematic();
    recover_and_verify(4, 2, {0, 1}, "erase 2 data shards (4+2)");
    recover_and_verify(4, 2, {4, 5}, "erase 2 parity shards (4+2)");
    recover_and_verify(4, 2, {1, 5}, "erase 1 data + 1 parity (4+2)");
    recover_and_verify(6, 3, {0, 3, 7}, "erase 3 mixed shards (6+3)");
    recover_and_verify(10, 4, {2, 5, 9, 12}, "erase 4 mixed shards (10+4)");
    test_too_few();
    if (g_failures == 0) {
        std::printf("\nALL TESTS PASSED\n");
        return EXIT_SUCCESS;
    }
    std::printf("\n%d TEST(S) FAILED\n", g_failures);
    return EXIT_FAILURE;
}
