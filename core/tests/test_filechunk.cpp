// Tests for chunked, erasure-coded file transfer: round trips, recovery from
// dropped chunks within the parity budget, corrupted chunks treated as losses,
// multi-stripe files, insufficient chunks, and end-to-end hash verification.

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <vector>

#include <sodium.h>

#include "aeromesh/filechunk.hpp"
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

std::vector<std::byte> make_file(std::size_t len) {
    std::vector<std::byte> v(len);
    std::uint32_t state = 0x9e3779b9u;
    for (std::size_t i = 0; i < len; ++i) {
        state = state * 1664525u + 1013904223u;
        v[i] = static_cast<std::byte>((state >> 23) & 0xFF);
    }
    return v;
}

void test_round_trip_full() {
    std::printf("round trip, all chunks present\n");
    auto file = make_file(5000);
    auto enc = encode_file(file, 512, 8, 3);
    check(enc.has_value(), "encode");
    if (!enc) return;
    auto dec = decode_file(enc->manifest, enc->chunks);
    check(dec.has_value(), "decode");
    if (!dec) return;
    check(*dec == file, "decoded file equals original");
}

void test_recovery_within_budget() {
    std::printf("recover after dropping parity-budget chunks per stripe\n");
    auto file = make_file(4096);
    auto enc = encode_file(file, 256, 6, 3);  // 16 data bytes*... 6+3 per stripe
    check(enc.has_value(), "encode");
    if (!enc) return;

    // Drop up to 3 chunks from each stripe (parity budget = 3).
    std::vector<Chunk> kept;
    std::map<std::uint32_t, int> dropped;
    for (const auto& c : enc->chunks) {
        if (dropped[c.id.stripe] < 3) {
            ++dropped[c.id.stripe];
            continue;  // drop this chunk
        }
        kept.push_back(c);
    }
    auto dec = decode_file(enc->manifest, kept);
    check(dec.has_value(), "decode from survivors");
    if (dec) check(*dec == file, "recovered file equals original");
}

void test_corrupted_chunk_ignored() {
    std::printf("corrupted chunk is treated as missing\n");
    auto file = make_file(2000);
    auto enc = encode_file(file, 500, 4, 2);
    check(enc.has_value(), "encode");
    if (!enc) return;

    // Corrupt one data chunk's payload (tag will no longer match) and also
    // drop one other chunk; 2 parity should still cover both as losses.
    auto chunks = enc->chunks;
    bool corrupted = false;
    bool removed = false;
    std::vector<Chunk> out;
    for (auto& c : chunks) {
        if (!corrupted && c.id.stripe == 0 && c.id.shard == 0) {
            c.data[0] ^= std::byte{0xFF};  // flip a byte, leave tag stale
            corrupted = true;
            out.push_back(c);
            continue;
        }
        if (!removed && c.id.stripe == 0 && c.id.shard == 1) {
            removed = true;
            continue;  // drop
        }
        out.push_back(c);
    }
    auto dec = decode_file(enc->manifest, out);
    check(dec.has_value(), "decode despite 1 corrupt + 1 missing");
    if (dec) check(*dec == file, "file intact after ignoring corruption");
}

void test_multi_stripe() {
    std::printf("multi-stripe file\n");
    // 4 data shards * 100 bytes = 400 bytes/stripe; 1000 bytes -> 3 stripes.
    auto file = make_file(1000);
    auto enc = encode_file(file, 100, 4, 2);
    check(enc.has_value(), "encode");
    if (!enc) return;
    check(enc->manifest.stripe_count == 3, "stripe count = 3");
    auto dec = decode_file(enc->manifest, enc->chunks);
    check(dec.has_value() && *dec == file, "multi-stripe round trip");
}

void test_too_few() {
    std::printf("insufficient chunks rejected\n");
    auto file = make_file(800);
    auto enc = encode_file(file, 200, 4, 1);  // 1 stripe, 5 chunks
    check(enc.has_value(), "encode");
    if (!enc) return;
    // Keep only 3 of 5 -> below data_shards (4).
    std::vector<Chunk> few(enc->chunks.begin(), enc->chunks.begin() + 3);
    auto dec = decode_file(enc->manifest, few);
    check(!dec.has_value() && dec.error() == FileError::ShardMissing,
          "decode rejects too few chunks");
}

void test_hash_mismatch() {
    std::printf("tampered manifest hash detected\n");
    auto file = make_file(1500);
    auto enc = encode_file(file, 512, 3, 2);
    check(enc.has_value(), "encode");
    if (!enc) return;
    auto manifest = enc->manifest;
    manifest.file_id[0] ^= std::byte{0x01};
    auto dec = decode_file(manifest, enc->chunks);
    check(!dec.has_value() && dec.error() == FileError::HashMismatch,
          "decode rejects wrong file_id");
}

void test_single_byte() {
    std::printf("single-byte file\n");
    std::vector<std::byte> file{std::byte{0x42}};
    auto enc = encode_file(file, 64, 2, 1);
    check(enc.has_value(), "encode");
    if (!enc) return;
    auto dec = decode_file(enc->manifest, enc->chunks);
    check(dec.has_value() && *dec == file, "single byte round trip");
}

} // namespace

int main() {
    if (!aeromesh::init_crypto()) {
        std::printf("  [FAIL] init_crypto\n");
        return EXIT_FAILURE;
    }
    test_round_trip_full();
    test_recovery_within_budget();
    test_corrupted_chunk_ignored();
    test_multi_stripe();
    test_too_few();
    test_hash_mismatch();
    test_single_byte();
    if (g_failures == 0) {
        std::printf("\nALL TESTS PASSED\n");
        return EXIT_SUCCESS;
    }
    std::printf("\n%d TEST(S) FAILED\n", g_failures);
    return EXIT_FAILURE;
}
