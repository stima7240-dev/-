#pragma once

// AeroMesh chat service.
//
// This is the layer that finally makes two people talk. It sits on top of the
// NetworkEngine (real UDP + cover traffic) and the core SecureSession / Double
// Ratchet, and it owns the live conversation state the UI shows.
//
// Responsibilities:
//   * Run the authenticated handshake with a peer learned out-of-band (their
//     share string + address), pinning their long-term identity so a
//     man-in-the-middle cannot substitute keys.
//   * Keep one established SecureSession per contact and push every chat
//     message through the ratchet (forward secrecy + post-compromise security).
//   * Expose contacts and per-contact message history to QML, and accept
//     outgoing messages from QML.
//
// Handshake protocol (carried inside Hello packets; first payload byte is the
// sub-type, the rest is the wire-encoded body):
//   initiator --REQUEST(self id)-->  responder
//   initiator <--BUNDLE(prekey)----  responder   (responder.create_responder)
//   initiator --INIT(initiation)-->  responder   (initiator.initiate)
//   both establish; responder.accept finishes its side.
// Chat messages travel as Data packets carrying a wire-encoded RatchetMessage.
//
// All handlers run on the GUI thread (they fire from NetworkEngine's timer
// tick), so touching Qt models and emitting signals here is safe.

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "aeromesh/identity.hpp"
#include "aeromesh/packet.hpp"
#include "aeromesh/session.hpp"
#include "aeromesh/transport.hpp"

#include "NetworkEngine.h"
#include "Wire.h"

class ChatService : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList contacts READ contacts NOTIFY contactsChanged)

public:
    explicit ChatService(QObject* parent = nullptr) : QObject(parent) {}

    // Provide the network engine and register packet handlers on its transport.
    // Call again after the active identity changes; handlers are idempotent.
    void attach(NetworkEngine* engine) {
        engine_ = engine;
        if (!engine_) {
            return;
        }
        aeromesh::Transport* t = engine_->transport();
        if (!t) {
            return;
        }
        t->on(aeromesh::PacketType::Hello,
              [this](const aeromesh::Endpoint& from, const aeromesh::Packet& p) {
                  onHello(from, p);
              });
        t->on(aeromesh::PacketType::Data,
              [this](const aeromesh::Endpoint& from, const aeromesh::Packet& p) {
                  onData(from, p);
              });
    }

    // Set (or clear) the active local identity used for handshakes. The pointer
    // must outlive its use; the Backend owns it and clears this on lock.
    void setIdentity(const aeromesh::Identity* self) { self_ = self; }

    // Reset all conversation state, e.g. when switching/locking accounts.
    void reset() {
        peers_.clear();
        emit contactsChanged();
    }

    [[nodiscard]] QVariantList contacts() const {
        QVariantList list;
        for (const auto& peer : peers_) {
            QVariantMap m;
            m[QStringLiteral("name")] = peer->name;
            m[QStringLiteral("fingerprint")] = fingerprintOf(peer->id);
            m[QStringLiteral("established")] =
                (peer->phase == Phase::Established);
            list.append(m);
        }
        return list;
    }

    // Begin (or restart) a handshake with a peer identified by their share
    // string and reachable at host:port. Returns "" on success or an error code
    // ("offline", "badkey", "send").
    Q_INVOKABLE QString connectToPeer(const QString& share, const QString& host,
                                      int port) {
        aeromesh::Transport* t = transportOrNull();
        if (!t || !self_) {
            return QStringLiteral("offline");
        }
        auto parsed = aeromesh::parse_share_string(share.trimmed().toStdString());
        if (!parsed) {
            return QStringLiteral("badkey");
        }
        const aeromesh::Endpoint ep{
            host.trimmed().toStdString(),
            static_cast<std::uint16_t>(port < 0 ? 0 : port)};
        Peer* peer = findByEndpoint(ep);
        if (!peer) {
            peer = addPeer(*parsed, ep);
        } else {
            peer->id = *parsed;
        }
        peer->phase = Phase::Requested;
        peer->responder.reset();
        peer->session.reset();
        t->add_peer(ep, NetworkEngine::clockMs());

        // REQUEST body: our long-term identity public key so the responder can
        // pin who is initiating before it ever produces key material.
        std::vector<std::byte> body;
        body.push_back(static_cast<std::byte>(kHelloRequest));
        appendKey(body, self_->public_key());
        if (!sendHello(ep, body)) {
            return QStringLiteral("send");
        }
        emit contactsChanged();
        return QString();
    }

    // The message history for a contact, as a list of { mine, text, ts }.
    Q_INVOKABLE QVariantList messages(int index) const {
        if (index < 0 || index >= static_cast<int>(peers_.size())) {
            return {};
        }
        return peers_[static_cast<std::size_t>(index)]->messages;
    }

    // Encrypt and send a chat message to a contact. Returns "" on success or an
    // error code ("range", "nosession", "encrypt", "send").
    Q_INVOKABLE QString sendMessage(int index, const QString& text) {
        if (index < 0 || index >= static_cast<int>(peers_.size())) {
            return QStringLiteral("range");
        }
        Peer* peer = peers_[static_cast<std::size_t>(index)].get();
        if (peer->phase != Phase::Established || !peer->session) {
            return QStringLiteral("nosession");
        }
        const QByteArray utf8 = text.toUtf8();
        const auto* bytes = reinterpret_cast<const std::byte*>(utf8.constData());
        auto enc = peer->session->encrypt(
            std::span<const std::byte>(bytes,
                                       static_cast<std::size_t>(utf8.size())));
        if (!enc) {
            return QStringLiteral("encrypt");
        }
        aeromesh::Transport* t = transportOrNull();
        if (!t) {
            return QStringLiteral("offline");
        }
        aeromesh::Packet pkt;
        pkt.type = aeromesh::PacketType::Data;
        pkt.payload = aeromesh::wire::encode_ratchet(*enc);
        if (!t->send(peer->endpoint, pkt)) {
            return QStringLiteral("send");
        }
        appendMessage(*peer, true, text);
        return QString();
    }

signals:
    void contactsChanged();
    void messagesChanged(int index);

private:
    enum class Phase { Idle, Requested, Established };

    // Hello sub-types (first payload byte).
    static constexpr std::uint8_t kHelloRequest = 0x01;
    static constexpr std::uint8_t kHelloBundle = 0x02;
    static constexpr std::uint8_t kHelloInit = 0x03;

    struct Peer {
        aeromesh::Identity::PublicKey id{};
        aeromesh::Endpoint endpoint;
        QString name;
        Phase phase = Phase::Idle;
        std::optional<aeromesh::SecureSession> session;
        std::optional<aeromesh::SecureSession::Responder> responder;
        QVariantList messages;
    };

    [[nodiscard]] aeromesh::Transport* transportOrNull() const {
        return engine_ ? engine_->transport() : nullptr;
    }

    [[nodiscard]] Peer* findByEndpoint(const aeromesh::Endpoint& ep) const {
        for (const auto& peer : peers_) {
            if (peer->endpoint.host == ep.host &&
                peer->endpoint.port == ep.port) {
                return peer.get();
            }
        }
        return nullptr;
    }

    [[nodiscard]] int indexOf(const Peer* target) const {
        for (std::size_t i = 0; i < peers_.size(); ++i) {
            if (peers_[i].get() == target) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    Peer* addPeer(const aeromesh::Identity::PublicKey& id,
                  const aeromesh::Endpoint& ep) {
        auto peer = std::make_unique<Peer>();
        peer->id = id;
        peer->endpoint = ep;
        peer->name = fingerprintOf(id);
        peers_.push_back(std::move(peer));
        return peers_.back().get();
    }

    bool sendHello(const aeromesh::Endpoint& ep,
                   const std::vector<std::byte>& body) {
        aeromesh::Transport* t = transportOrNull();
        if (!t) {
            return false;
        }
        aeromesh::Packet pkt;
        pkt.type = aeromesh::PacketType::Hello;
        pkt.payload = body;
        return t->send(ep, pkt);
    }

    // ---- inbound handlers (run on the GUI thread) ------------------------

    void onHello(const aeromesh::Endpoint& from, const aeromesh::Packet& p) {
        if (p.payload.empty() || !self_) {
            return;
        }
        const auto sub = std::to_integer<std::uint8_t>(p.payload.front());
        const std::span<const std::byte> body(p.payload.data() + 1,
                                              p.payload.size() - 1);
        switch (sub) {
            case kHelloRequest:
                onRequest(from, body);
                break;
            case kHelloBundle:
                onBundle(from, body);
                break;
            case kHelloInit:
                onInit(from, body);
                break;
            default:
                break;
        }
    }

    // Responder side: a peer wants to talk. Mint a fresh signed prekey bundle
    // and send it back, remembering the responder state to finish later.
    void onRequest(const aeromesh::Endpoint& from,
                   std::span<const std::byte> body) {
        aeromesh::Identity::PublicKey initiator{};
        if (body.size() < initiator.size()) {
            return;
        }
        for (std::size_t i = 0; i < initiator.size(); ++i) {
            initiator[i] = body[i];
        }
        auto responder = aeromesh::SecureSession::create_responder(*self_);
        if (!responder) {
            return;
        }
        Peer* peer = findByEndpoint(from);
        if (!peer) {
            peer = addPeer(initiator, from);
        } else {
            peer->id = initiator;
        }
        const auto bundleBytes =
            aeromesh::wire::encode_bundle(responder->bundle());
        peer->responder.emplace(std::move(*responder));
        peer->session.reset();
        peer->phase = Phase::Requested;

        std::vector<std::byte> reply;
        reply.push_back(static_cast<std::byte>(kHelloBundle));
        reply.insert(reply.end(), bundleBytes.begin(), bundleBytes.end());
        sendHello(from, reply);
        emit contactsChanged();
    }

    // Initiator side: we received the responder's bundle. Run the handshake and
    // send back our signed initiation; our session is now live.
    void onBundle(const aeromesh::Endpoint& from,
                  std::span<const std::byte> body) {
        Peer* peer = findByEndpoint(from);
        if (!peer || peer->phase != Phase::Requested) {
            return;
        }
        auto bundle = aeromesh::wire::decode_bundle(body);
        if (!bundle) {
            return;
        }
        auto result =
            aeromesh::SecureSession::initiate(*self_, peer->id, *bundle);
        if (!result) {
            return;
        }
        auto& session = result->first;
        const auto initBytes =
            aeromesh::wire::encode_initiation(result->second);
        peer->session.emplace(std::move(session));
        peer->phase = Phase::Established;

        std::vector<std::byte> reply;
        reply.push_back(static_cast<std::byte>(kHelloInit));
        reply.insert(reply.end(), initBytes.begin(), initBytes.end());
        sendHello(from, reply);
        emit contactsChanged();
    }

    // Responder side: the initiator's signed initiation arrived. Verify and
    // finish establishing our session.
    void onInit(const aeromesh::Endpoint& from,
                std::span<const std::byte> body) {
        Peer* peer = findByEndpoint(from);
        if (!peer || !peer->responder) {
            return;
        }
        auto initiation = aeromesh::wire::decode_initiation(body);
        if (!initiation) {
            return;
        }
        // Pin: the initiation must be signed by the identity that opened the
        // handshake, otherwise drop it.
        if (initiation->identity_pub != peer->id) {
            return;
        }
        auto session = aeromesh::SecureSession::accept(
            std::move(*peer->responder), *initiation);
        peer->responder.reset();
        if (!session) {
            peer->phase = Phase::Idle;
            return;
        }
        peer->session.emplace(std::move(*session));
        peer->phase = Phase::Established;
        emit contactsChanged();
    }

    // A chat message arrived: decrypt through the ratchet and surface it.
    void onData(const aeromesh::Endpoint& from, const aeromesh::Packet& p) {
        Peer* peer = findByEndpoint(from);
        if (!peer || peer->phase != Phase::Established || !peer->session) {
            return;
        }
        auto msg = aeromesh::wire::decode_ratchet(p.payload);
        if (!msg) {
            return;
        }
        auto plain = peer->session->decrypt(*msg);
        if (!plain) {
            return;
        }
        const QString text = QString::fromUtf8(
            reinterpret_cast<const char*>(plain->data()),
            static_cast<int>(plain->size()));
        appendMessage(*peer, false, text);
    }

    void appendMessage(Peer& peer, bool mine, const QString& text) {
        QVariantMap m;
        m[QStringLiteral("mine")] = mine;
        m[QStringLiteral("text")] = text;
        m[QStringLiteral("ts")] = QDateTime::currentMSecsSinceEpoch();
        peer.messages.append(m);
        emit messagesChanged(indexOf(&peer));
    }

    static void appendKey(std::vector<std::byte>& out,
                          const aeromesh::Identity::PublicKey& key) {
        out.insert(out.end(), key.begin(), key.end());
    }

    // First 8 bytes of a public key as "A1B2 C3D4 E5F6 0718" -- the same short
    // fingerprint shown elsewhere, used as a default contact label.
    static QString fingerprintOf(const aeromesh::Identity::PublicKey& pk) {
        QString hex;
        for (std::size_t i = 0; i < 8 && i < pk.size(); ++i) {
            hex += QString("%1").arg(
                static_cast<unsigned>(std::to_integer<unsigned char>(pk[i])),
                2, 16, QChar('0'));
            if (i % 2 == 1 && i != 7) {
                hex += QChar(' ');
            }
        }
        return hex.toUpper();
    }

    NetworkEngine* engine_ = nullptr;
    const aeromesh::Identity* self_ = nullptr;
    std::vector<std::unique_ptr<Peer>> peers_;
};
