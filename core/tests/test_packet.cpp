// Minimal dependency-free test runner for the core library. Returns non-zero
// on the first failure so CTest reports it.

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "aeromesh/identity.hpp"
#include "aeromesh/packet.hpp"

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

std::vector<std::byte> bytes_of(const std::string& s) {
    std::vector<std::byte> v(s.size());
    for (std::size_t i = 0; i < s.size(); ++i)
        v[i] = static_cast<std::byte>(s[i]);
    return v;
}

void test_frame_roundtrip() {
    std::printf("frame round-trip\n");
    using namespace aeromesh;
    Packet in{PacketType::Data, bytes_of("hello aeromesh")};
    auto enc = encode(in);
    check(enc.has_value(), "encode succeeds");
    if (!enc) return;
    check(enc->size() == kFrameSize, "frame is exactly kFrameSize bytes");
    auto out = decode(std::span<const std::byte, kFrameSize>(*enc));
    check(out.has_value(), "decode succeeds");
    if (!out) return;
    check(out->type == PacketType::Data, "type preserved");
    check(out->payload == in.payload, "payload preserved");
}

void test_oversized_payload_rejected() {
    std::printf("oversized payload rejected\n");
    using namespace aeromesh;
    Packet in{PacketType::FileChunk,
              std::vector<std::byte>(kMaxPayloadSize + 1, std::byte{0})};
    auto enc = encode(in);
    check(!enc.has_value(), "encode rejects > kMaxPayloadSize");
    check(!enc && enc.error() == FrameError::PayloadTooLarge, "correct error");
}

void test_dummy_frame_is_dummy() {
    std::printf("cover traffic frame\n");
    using namespace aeromesh;
    auto f = make_dummy_frame();
    check(f.size() == kFrameSize, "dummy frame is full size");
    auto out = decode(std::span<const std::byte, kFrameSize>(f));
    // Dummy decodes as a known type so peers can drop it cheaply.
    check(out.has_value() && out->type == PacketType::Dummy,
          "dummy decodes to PacketType::Dummy");
}

void test_identity() {
    std::printf("identity generation + share string\n");
    using namespace aeromesh;
    check(init_crypto(), "libsodium initialises");
    auto id = Identity::generate();
    check(id.has_value(), "identity generated");
    if (!id) return;
    auto share = id->share_string();
    auto parsed = parse_share_string(share);
    check(parsed.has_value(), "share string parses back");
    check(parsed && *parsed == id->public_key(),
          "parsed public key matches");
    check(id->fingerprint().size() >= 16, "fingerprint is non-trivial");

    auto restored = Identity::from_secret_b64(id->export_secret_b64());
    check(restored.has_value(), "identity restores from secret");
    check(restored && restored->public_key() == id->public_key(),
          "restored identity has same public key");
}

} // namespace

int main() {
    test_frame_roundtrip();
    test_oversized_payload_rejected();
    test_dummy_frame_is_dummy();
    test_identity();
    if (g_failures == 0) {
        std::printf("\nALL TESTS PASSED\n");
        return EXIT_SUCCESS;
    }
    std::printf("\n%d TEST(S) FAILED\n", g_failures);
    return EXIT_FAILURE;
}
