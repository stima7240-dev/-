#include "aeromesh/session.hpp"

#include <string_view>
#include <utility>

namespace aeromesh {
namespace {

constexpr std::string_view kBundleLabel = "aeromesh-session-bundle-v1";
constexpr std::string_view kInitLabel = "aeromesh-session-init-v1";

void append(std::vector<std::byte>& buf, std::span<const std::byte> data) {
    buf.insert(buf.end(), data.begin(), data.end());
}

void append_str(std::vector<std::byte>& buf, std::string_view s) {
    for (char c : s) {
        buf.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
    }
}

// Transcript the responder signs over its own published bundle.
std::vector<std::byte> bundle_transcript(const SignedPrekeyBundle& b) {
    std::vector<std::byte> t;
    append_str(t, kBundleLabel);
    append(t, b.identity_pub);
    append(t, b.x25519_pub);
    append(t, b.kem_pub);
    append(t, b.ratchet_pub);
    return t;
}

// Transcript the initiator signs; binds the full responder bundle plus the
// initiator's own contribution, so a tampered handshake fails verification.
std::vector<std::byte> init_transcript(const SignedPrekeyBundle& b,
                                       const SessionInitiation& i) {
    std::vector<std::byte> t;
    append_str(t, kInitLabel);
    append(t, b.identity_pub);
    append(t, b.x25519_pub);
    append(t, b.kem_pub);
    append(t, b.ratchet_pub);
    append(t, i.identity_pub);
    append(t, i.x25519_eph_pub);
    append(t, i.kem_ciphertext);
    return t;
}

} // namespace

std::expected<SecureSession::Responder, SessionError>
SecureSession::create_responder(const Identity& self) {
    auto hybrid = generate_responder_keys();
    if (!hybrid) {
        return std::unexpected(SessionError::CryptoFailure);
    }

    const RatchetKeyPair ratchet_keys = generate_ratchet_keypair();

    Responder r;
    r.hybrid_keys_ = std::move(hybrid.value());
    r.ratchet_keys_ = ratchet_keys;
    r.self_id_ = self.public_key();

    SignedPrekeyBundle b;
    b.identity_pub = self.public_key();
    b.x25519_pub = r.hybrid_keys_.x25519_pub;
    b.kem_pub = r.hybrid_keys_.kem_pub;
    b.ratchet_pub = ratchet_keys.pub;

    auto sig = self.sign(bundle_transcript(b));
    if (!sig) {
        return std::unexpected(SessionError::CryptoFailure);
    }
    b.signature = sig.value();

    r.bundle_ = std::move(b);
    return r;
}

std::expected<std::pair<SecureSession, SessionInitiation>, SessionError>
SecureSession::initiate(const Identity& self,
                        const Identity::PublicKey& expected_peer,
                        const SignedPrekeyBundle& bundle) {
    // Pin the responder identity learned out-of-band: defeats a MITM that
    // substitutes its own (validly signed) bundle under a different identity.
    if (bundle.identity_pub != expected_peer) {
        return std::unexpected(SessionError::BadSignature);
    }
    // The bundle must be authentic under that identity.
    if (!Identity::verify(
            bundle.identity_pub, bundle_transcript(bundle),
            std::span<const std::byte, kSignatureLen>(bundle.signature))) {
        return std::unexpected(SessionError::BadSignature);
    }

    HybridPrekeyBundle hb;
    hb.x25519_pub = bundle.x25519_pub;
    hb.kem_pub = bundle.kem_pub;
    auto init = hybrid_initiate(hb);
    if (!init) {
        return std::unexpected(SessionError::HandshakeFailed);
    }

    auto ratchet =
        Ratchet::init_initiator(init->shared_secret, bundle.ratchet_pub);
    if (!ratchet) {
        return std::unexpected(SessionError::HandshakeFailed);
    }

    SessionInitiation si;
    si.identity_pub = self.public_key();
    si.x25519_eph_pub = init->x25519_eph_pub;
    si.kem_ciphertext = init->kem_ciphertext;

    auto sig = self.sign(init_transcript(bundle, si));
    if (!sig) {
        return std::unexpected(SessionError::CryptoFailure);
    }
    si.signature = sig.value();

    SecureSession session;
    session.peer_id_ = bundle.identity_pub;
    session.ratchet_ = std::move(ratchet.value());

    return std::make_pair(std::move(session), std::move(si));
}

std::expected<SecureSession, SessionError> SecureSession::accept(
    Responder&& responder, const SessionInitiation& initiation) {
    // Verify the initiation signature, which binds OUR exact bundle. If a MITM
    // altered the bundle the initiator saw, this transcript will not match.
    const std::vector<std::byte> transcript =
        init_transcript(responder.bundle_, initiation);
    if (!Identity::verify(
            initiation.identity_pub, transcript,
            std::span<const std::byte, kSignatureLen>(initiation.signature))) {
        return std::unexpected(SessionError::BadSignature);
    }

    auto secret = hybrid_respond(responder.hybrid_keys_,
                                 initiation.x25519_eph_pub,
                                 initiation.kem_ciphertext);
    if (!secret) {
        return std::unexpected(SessionError::HandshakeFailed);
    }

    auto ratchet =
        Ratchet::init_responder(secret.value(), responder.ratchet_keys_);
    if (!ratchet) {
        return std::unexpected(SessionError::HandshakeFailed);
    }

    SecureSession session;
    session.peer_id_ = initiation.identity_pub;
    session.ratchet_ = std::move(ratchet.value());
    return session;
}

std::expected<RatchetMessage, SessionError> SecureSession::encrypt(
    std::span<const std::byte> plaintext) {
    if (!ratchet_) {
        return std::unexpected(SessionError::NotEstablished);
    }
    auto msg = ratchet_->encrypt(plaintext);
    if (!msg) {
        return std::unexpected(SessionError::EncryptFailed);
    }
    return std::move(msg.value());
}

std::expected<std::vector<std::byte>, SessionError> SecureSession::decrypt(
    const RatchetMessage& message) {
    if (!ratchet_) {
        return std::unexpected(SessionError::NotEstablished);
    }
    auto pt = ratchet_->decrypt(message);
    if (!pt) {
        return std::unexpected(SessionError::DecryptFailed);
    }
    return std::move(pt.value());
}

} // namespace aeromesh
