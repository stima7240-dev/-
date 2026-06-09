// Tests for the link-layer channel encryption (link.hpp / link.cpp).

#include "aeromesh/identity.hpp"
#include "aeromesh/link.hpp"
#include "aeromesh/packet.hpp"

#include <array>
#include <cstdio>
#include <span>
#include <vector>

namespace {

int g_failures = 0;

void check(bool cond, const char* what) {
    if (cond) {
        std::printf("[ ok ] %s\n", what);
    } else {
        std::printf("[FAIL] %s\n", what);
        ++g_failures;
    }
}

using aeromesh::Frame;
using aeromesh::Identity;
using aeromesh::LinkError;
using aeromesh::LinkHandshake;
using aeromesh::LinkKeys;
using aeromesh::LinkRole;
using aeromesh::SecureLink;

bool keys_equal(const aeromesh::LinkSessionKey& a, const aeromesh::LinkSessionKey& b) {
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
}

bool frames_equal(const Frame& a, const Frame& b) {
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
}

// Run a full 3-message handshake between two prepared handshakes.
// Returns true if both sides finished.
bool run_handshake(LinkHandshake& initiator, LinkHandshake& responder) {
    auto msg1 = initiator.advance({});
    if (!msg1.has_value()) {
        return false;
    }
    auto msg2 = responder.advance(std::span<const std::byte>(*msg1));
    if (!msg2.has_value()) {
        return false;
    }
    auto msg3 = initiator.advance(std::span<const std::byte>(*msg2));
    if (!msg3.has_value()) {
        return false;
    }
    auto fin = responder.advance(std::span<const std::byte>(*msg3));
    if (!fin.has_value()) {
        return false;
    }
    return initiator.done() && responder.done();
}

void test_handshake_agreement() {
    auto alice = Identity::generate();
    auto bob = Identity::generate();
    check(alice.has_value() && bob.has_value(), "identities generated");

    auto hi = LinkHandshake::create(LinkRole::Initiator, *alice, bob->public_key());
    auto hr = LinkHandshake::create(LinkRole::Responder, *bob, alice->public_key());
    check(hi.has_value() && hr.has_value(), "handshakes created");

    const bool ok = run_handshake(*hi, *hr);
    check(ok, "handshake completed on both sides");

    auto ki = hi->keys();
    auto kr = hr->keys();
    check(ki.has_value() && kr.has_value(), "both sides produced keys");

    // The initiator's send key must equal the responder's receive key, etc.
    check(keys_equal(ki->tx, kr->rx), "initiator tx matches responder rx");
    check(keys_equal(ki->rx, kr->tx), "initiator rx matches responder tx");

    // Each side authenticated the other's real identity.
    auto pi = hi->peer_identity();
    auto pr = hr->peer_identity();
    check(pi.has_value() && pr.has_value(), "peer identities resolved");
    check(pi.value() == bob->public_key(), "initiator authenticated bob");
    check(pr.value() == alice->public_key(), "responder authenticated alice");
}

void test_round_trip_and_size() {
    auto alice = Identity::generate();
    auto bob = Identity::generate();
    auto hi = LinkHandshake::create(LinkRole::Initiator, *alice, bob->public_key());
    auto hr = LinkHandshake::create(LinkRole::Responder, *bob, alice->public_key());
    run_handshake(*hi, *hr);

    SecureLink a_link(hi->keys().value());
    SecureLink b_link(hr->keys().value());

    aeromesh::Packet pkt;
    pkt.type = aeromesh::PacketType::Data;
    pkt.payload = std::vector<std::byte>(64, std::byte{0x5A});
    auto frame = aeromesh::encode(pkt);
    check(frame.has_value(), "packet encoded to frame");

    auto wire = a_link.seal(*frame);
    check(wire.has_value(), "frame sealed");
    check(wire->size() == aeromesh::kSealedFrameLen, "sealed frame is constant size");

    auto opened = b_link.open(std::span<const std::byte>(*wire));
    check(opened.has_value(), "peer opened sealed frame");
    check(opened.has_value() && frames_equal(*opened, *frame), "opened frame matches original");

    // A cover (dummy) frame seals to the exact same size as a real one, so a
    // passive observer cannot tell them apart.
    Frame dummy = aeromesh::make_dummy_frame();
    auto dummy_wire = a_link.seal(dummy);
    check(dummy_wire.has_value(), "dummy frame sealed");
    check(dummy_wire.has_value() && dummy_wire->size() == aeromesh::kSealedFrameLen,
          "dummy frame is same size as real frame");
}

void test_replay_and_reorder() {
    auto alice = Identity::generate();
    auto bob = Identity::generate();
    auto hi = LinkHandshake::create(LinkRole::Initiator, *alice, bob->public_key());
    auto hr = LinkHandshake::create(LinkRole::Responder, *bob, alice->public_key());
    run_handshake(*hi, *hr);
    SecureLink a_link(hi->keys().value());
    SecureLink b_link(hr->keys().value());

    Frame f0 = aeromesh::make_dummy_frame();
    Frame f1 = aeromesh::make_dummy_frame();
    auto w0 = a_link.seal(f0);
    auto w1 = a_link.seal(f1);
    check(w0.has_value() && w1.has_value(), "two frames sealed");

    // Out-of-order delivery within the window is accepted.
    auto o1 = b_link.open(std::span<const std::byte>(*w1));
    check(o1.has_value(), "later counter accepted first");
    auto o0 = b_link.open(std::span<const std::byte>(*w0));
    check(o0.has_value(), "earlier counter still accepted (reorder window)");

    // Replaying an already-seen frame is rejected.
    auto replay = b_link.open(std::span<const std::byte>(*w0));
    check(!replay.has_value() && replay.error() == LinkError::Replay,
          "replayed frame rejected");

    // A tampered ciphertext fails authentication.
    auto w2 = a_link.seal(aeromesh::make_dummy_frame());
    std::vector<std::byte> bad = *w2;
    bad[aeromesh::kLinkCounterLen + 5] ^= std::byte{0x01};
    auto tampered = b_link.open(std::span<const std::byte>(bad));
    check(!tampered.has_value() && tampered.error() == LinkError::DecryptFailed,
          "tampered frame rejected");
}

void test_wrong_direction_key() {
    auto alice = Identity::generate();
    auto bob = Identity::generate();
    auto hi = LinkHandshake::create(LinkRole::Initiator, *alice, bob->public_key());
    auto hr = LinkHandshake::create(LinkRole::Responder, *bob, alice->public_key());
    run_handshake(*hi, *hr);
    SecureLink a_link(hi->keys().value());

    // A frame sealed with the initiator's tx key cannot be opened by the same
    // endpoint (its rx key is the other direction) -- proves directional keys.
    auto wire = a_link.seal(aeromesh::make_dummy_frame());
    auto self_open = a_link.open(std::span<const std::byte>(*wire));
    check(!self_open.has_value() && self_open.error() == LinkError::DecryptFailed,
          "frame not openable with wrong-direction key");
}

void test_unexpected_peer_rejected() {
    auto alice = Identity::generate();
    auto bob = Identity::generate();
    auto eve = Identity::generate();

    // Alice expects to talk to Eve, but the responder is actually Bob.
    auto hi = LinkHandshake::create(LinkRole::Initiator, *alice, eve->public_key());
    auto hr = LinkHandshake::create(LinkRole::Responder, *bob, alice->public_key());

    auto msg1 = hi->advance({});
    check(msg1.has_value(), "msg1 produced");
    auto msg2 = hr->advance(std::span<const std::byte>(*msg1));
    check(msg2.has_value(), "msg2 produced");
    auto res = hi->advance(std::span<const std::byte>(*msg2));
    check(!res.has_value() && res.error() == LinkError::UnexpectedPeer,
          "impersonating responder rejected (identity mismatch)");
}

void test_tampered_signature_rejected() {
    auto alice = Identity::generate();
    auto bob = Identity::generate();
    auto hi = LinkHandshake::create(LinkRole::Initiator, *alice, bob->public_key());
    auto hr = LinkHandshake::create(LinkRole::Responder, *bob, alice->public_key());

    auto msg1 = hi->advance({});
    auto msg2 = hr->advance(std::span<const std::byte>(*msg1));
    check(msg2.has_value(), "msg2 produced");

    // Flip a byte inside the responder's signature.
    std::vector<std::byte> bad = *msg2;
    bad[bad.size() - 1] ^= std::byte{0x01};
    auto res = hi->advance(std::span<const std::byte>(bad));
    check(!res.has_value() && res.error() == LinkError::BadSignature,
          "tampered handshake signature rejected");
}

void test_malformed_messages() {
    auto alice = Identity::generate();
    auto bob = Identity::generate();
    auto hr = LinkHandshake::create(LinkRole::Responder, *bob, alice->public_key());
    std::vector<std::byte> too_short(10, std::byte{0});
    auto res = hr->advance(std::span<const std::byte>(too_short));
    check(!res.has_value() && res.error() == LinkError::Malformed,
          "malformed handshake message rejected");

    // SecureLink.open must reject a wire blob of the wrong size.
    auto hi = LinkHandshake::create(LinkRole::Initiator, *alice, bob->public_key());
    auto hr2 = LinkHandshake::create(LinkRole::Responder, *bob, alice->public_key());
    run_handshake(*hi, *hr2);
    SecureLink a_link(hi->keys().value());
    std::vector<std::byte> short_wire(8, std::byte{0});
    auto bad_open = a_link.open(std::span<const std::byte>(short_wire));
    check(!bad_open.has_value() && bad_open.error() == LinkError::Malformed,
          "wrong-size sealed frame rejected");
}

} // namespace

int main() {
    if (!aeromesh::init_crypto()) {
        std::printf("[FAIL] init_crypto\n");
        return 1;
    }
    test_handshake_agreement();
    test_round_trip_and_size();
    test_replay_and_reorder();
    test_wrong_direction_key();
    test_unexpected_peer_rejected();
    test_tampered_signature_rejected();
    test_malformed_messages();
    return g_failures == 0 ? 0 : 1;
}
