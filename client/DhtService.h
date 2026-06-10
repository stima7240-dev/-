#pragma once

// AeroMesh DHT service.
//
// This layer removes the need to type a peer's host:port by hand. It runs a
// Kademlia distributed hash table on top of the NetworkEngine (real UDP +
// cover traffic) and the core aeromesh::Dht logic, and resolves a contact's
// network address from nothing but their long-term key.
//
// How discovery works:
//   * Each node's id is BLAKE2b-256 of its public key, so a key uniquely fixes
//     a position in the DHT keyspace (NodeId::from_public_key).
//   * announce(): we send FIND_NODE(self) to the configured bootstrap node(s).
//     They learn our id and the address they observed us from, so other nodes
//     can later find us through them.
//   * resolve(share): we derive the target id from the peer's share string and
//     run an iterative lookup -- query the closest nodes we know, fold in the
//     closer nodes they report, and repeat until either a returned contact has
//     id == target or a queried node turns out to BE the target. We then emit
//     resolved(share, host, port); the UI hands that to ChatService.
//
// Wire format (carried as DhtQuery / DhtReply packet payloads, all ids raw 32
// bytes):
//   DhtQuery: [target id 32][sender id 32]
//   DhtReply: [target id 32][responder id 32][encode_contacts(...)]
// The responder always uses the *observed* source endpoint for the sender, so
// the mapping stays correct even behind NAT.
//
// All handlers run on the GUI thread (they fire from NetworkEngine's timer
// tick), so touching Qt state and emitting signals here is safe.

#include <QObject>
#include <QString>
#include <QTimer>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "aeromesh/identity.hpp"
#include "aeromesh/kademlia.hpp"
#include "aeromesh/node_id.hpp"
#include "aeromesh/packet.hpp"
#include "aeromesh/routing_table.hpp"
#include "aeromesh/transport.hpp"

#include "NetworkEngine.h"

class DhtService : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString bootstrap READ bootstrap NOTIFY bootstrapChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)

public:
    explicit DhtService(QObject* parent = nullptr) : QObject(parent) {
        roundTimer_.setInterval(600);
        connect(&roundTimer_, &QTimer::timeout, this, &DhtService::onTick);
    }

    // Provide the network engine and register packet handlers on its transport.
    // Safe to call again after the transport is (re)created.
    void attach(NetworkEngine* engine) {
        engine_ = engine;
        if (!engine_) {
            return;
        }
        aeromesh::Transport* t = engine_->transport();
        if (!t) {
            return;
        }
        t->on(aeromesh::PacketType::DhtQuery,
              [this](const aeromesh::Endpoint& from, const aeromesh::Packet& p) {
                  onQuery(from, p);
              });
        t->on(aeromesh::PacketType::DhtReply,
              [this](const aeromesh::Endpoint& from, const aeromesh::Packet& p) {
                  onReply(from, p);
              });
    }

    // Bind the DHT to the active local identity. Recomputes our node id and
    // resets the routing table. Pass nullptr on lock.
    void setIdentity(const aeromesh::Identity* self) {
        self_ = self;
        lookups_.clear();
        roundTimer_.stop();
        if (!self_) {
            dht_.reset();
            emit readyChanged();
            return;
        }
        const auto& pk = self_->public_key();
        selfId_ = aeromesh::NodeId::from_public_key(
            std::span<const std::byte>(pk.data(), pk.size()));
        dht_ = std::make_unique<aeromesh::Dht>(selfId_);
        emit readyChanged();
        announce();
    }

    [[nodiscard]] QString bootstrap() const {
        return QString::fromStdString(bootstrapJoined());
    }
    [[nodiscard]] bool ready() const { return dht_ != nullptr; }

    // Configure the bootstrap node address ("host:port"). This is the one-time
    // entry point into the network -- in production the project's server, and
    // for a local two-window test simply the other window's address. Once set,
    // adding friends only needs their key. Pass "" to clear.
    Q_INVOKABLE void setBootstrap(const QString& hostPort) {
        bootstrap_.clear();
        const QString trimmed = hostPort.trimmed();
        if (!trimmed.isEmpty()) {
            bootstrap_.push_back(trimmed.toStdString());
        }
        emit bootstrapChanged();
        announce();
    }

    // Tell the bootstrap node(s) we are online so others can discover us.
    Q_INVOKABLE void announce() {
        if (!dht_ || bootstrap_.empty() || !transportOrNull()) {
            return;
        }
        for (const auto& ep : bootstrap_) {
            sendQuery(ep, selfId_);
        }
    }

    // Resolve a peer's network address from their share string alone. Returns
    // "" once a lookup has started (result arrives via resolved/resolveFailed),
    // or an error code: "offline", "badkey", "nobootstrap".
    Q_INVOKABLE QString resolve(const QString& share) {
        if (!dht_ || !transportOrNull()) {
            return QStringLiteral("offline");
        }
        auto parsed =
            aeromesh::parse_share_string(share.trimmed().toStdString());
        if (!parsed) {
            return QStringLiteral("badkey");
        }
        const aeromesh::NodeId target = aeromesh::NodeId::from_public_key(
            std::span<const std::byte>(parsed->data(), parsed->size()));

        // Already in our routing table at full precision? Resolve instantly.
        const auto near = dht_->table().closest(target, 1);
        if (!near.empty() && near.front().id == target) {
            emitResolved(share, near.front().endpoint);
            return QString();
        }
        if (bootstrap_.empty() && dht_->table().empty()) {
            return QStringLiteral("nobootstrap");
        }

        auto lk = std::make_unique<Lookup>();
        lk->target = target;
        lk->targetHex = target.to_hex();
        lk->share = share;
        lk->budget = 8;
        for (const auto& c : dht_->table().closest(target, aeromesh::kBucketSize)) {
            lk->shortlist.push_back(c);
        }
        Lookup* raw = lk.get();
        lookups_.push_back(std::move(lk));

        // Always probe the bootstrap node(s) first; they are the entry point.
        for (const auto& ep : bootstrap_) {
            if (raw->queried.insert(ep).second) {
                sendQuery(ep, target);
            }
        }
        queryClosest(*raw, aeromesh::kAlpha);
        if (!roundTimer_.isActive()) {
            roundTimer_.start();
        }
        return QString();
    }

signals:
    void bootstrapChanged();
    void readyChanged();
    // Address found for the peer with this share string.
    void resolved(const QString& share, const QString& host, int port);
    // Lookup gave up; reason is a short code ("timeout").
    void resolveFailed(const QString& share, const QString& reason);

private:
    struct Lookup {
        aeromesh::NodeId target;
        std::string targetHex;
        QString share;
        std::vector<aeromesh::Contact> shortlist;
        std::set<std::string> queried;
        int budget = 8;
    };

    [[nodiscard]] aeromesh::Transport* transportOrNull() const {
        return engine_ ? engine_->transport() : nullptr;
    }

    [[nodiscard]] std::string bootstrapJoined() const {
        std::string out;
        for (std::size_t i = 0; i < bootstrap_.size(); ++i) {
            if (i) {
                out += ", ";
            }
            out += bootstrap_[i];
        }
        return out;
    }

    // ---- outbound -------------------------------------------------------

    bool sendQuery(const std::string& endpoint, const aeromesh::NodeId& target) {
        aeromesh::Transport* t = transportOrNull();
        if (!t || !dht_) {
            return false;
        }
        const auto ep = aeromesh::Endpoint::parse(endpoint);
        if (!ep) {
            return false;
        }
        t->add_peer(*ep, NetworkEngine::clockMs());
        std::vector<std::byte> payload;
        appendId(payload, target);
        appendId(payload, selfId_);
        aeromesh::Packet pkt;
        pkt.type = aeromesh::PacketType::DhtQuery;
        pkt.payload = std::move(payload);
        return t->send(*ep, pkt);
    }

    void sendReply(const aeromesh::Endpoint& to, const aeromesh::NodeId& target,
                   const std::vector<aeromesh::Contact>& contacts) {
        aeromesh::Transport* t = transportOrNull();
        if (!t || !dht_) {
            return;
        }
        auto encoded = aeromesh::encode_contacts(contacts);
        if (!encoded) {
            return;
        }
        std::vector<std::byte> payload;
        appendId(payload, target);
        appendId(payload, selfId_);
        payload.insert(payload.end(), encoded->begin(), encoded->end());
        t->add_peer(to, NetworkEngine::clockMs());
        aeromesh::Packet pkt;
        pkt.type = aeromesh::PacketType::DhtReply;
        pkt.payload = std::move(payload);
        t->send(to, pkt);
    }

    // Query up to maxNew closest-not-yet-queried nodes in the shortlist.
    void queryClosest(Lookup& lk, std::size_t maxNew) {
        std::sort(lk.shortlist.begin(), lk.shortlist.end(),
                  [&](const aeromesh::Contact& a, const aeromesh::Contact& b) {
                      return aeromesh::CloserTo{lk.target}(a.id, b.id);
                  });
        std::size_t sent = 0;
        for (const auto& c : lk.shortlist) {
            if (sent >= maxNew) {
                break;
            }
            if (c.endpoint.empty() || lk.queried.count(c.endpoint)) {
                continue;
            }
            if (sendQuery(c.endpoint, lk.target)) {
                lk.queried.insert(c.endpoint);
                ++sent;
            }
        }
    }

    // ---- inbound handlers (GUI thread) ----------------------------------

    void onQuery(const aeromesh::Endpoint& from, const aeromesh::Packet& p) {
        if (!dht_ || p.payload.size() < 2 * aeromesh::kIdBytes) {
            return;
        }
        const aeromesh::NodeId target = readId(p.payload, 0);
        const aeromesh::NodeId sender = readId(p.payload, aeromesh::kIdBytes);
        dht_->table().update(aeromesh::Contact{sender, from.to_string()});
        const auto results = dht_->handle_find_node(target);
        sendReply(from, target, results);
    }

    void onReply(const aeromesh::Endpoint& from, const aeromesh::Packet& p) {
        if (!dht_ || p.payload.size() < 2 * aeromesh::kIdBytes) {
            return;
        }
        const aeromesh::NodeId target = readId(p.payload, 0);
        const aeromesh::NodeId responder = readId(p.payload, aeromesh::kIdBytes);
        const std::string responderEp = from.to_string();
        dht_->table().update(aeromesh::Contact{responder, responderEp});

        std::vector<aeromesh::Contact> learned;
        if (p.payload.size() > 2 * aeromesh::kIdBytes) {
            const std::span<const std::byte> rest(
                p.payload.data() + 2 * aeromesh::kIdBytes,
                p.payload.size() - 2 * aeromesh::kIdBytes);
            auto decoded = aeromesh::decode_contacts(rest);
            if (decoded) {
                for (const auto& c : *decoded) {
                    dht_->table().update(c);
                    learned.push_back(c);
                }
            }
        }

        Lookup* lk = findLookup(target.to_hex());
        if (!lk) {
            return; // e.g. a reply to our announce; table is already updated.
        }
        addToShortlist(*lk, aeromesh::Contact{responder, responderEp});
        for (const auto& c : learned) {
            addToShortlist(*lk, c);
        }
        // The node we queried is itself the target.
        if (responder == lk->target) {
            finishResolved(lk, responderEp);
            return;
        }
        // A returned contact is the target.
        for (const auto& c : learned) {
            if (c.id == lk->target && !c.endpoint.empty()) {
                finishResolved(lk, c.endpoint);
                return;
            }
        }
        queryClosest(*lk, aeromesh::kAlpha);
    }

    void onTick() {
        for (std::size_t i = 0; i < lookups_.size();) {
            Lookup* lk = lookups_[i].get();
            if (--lk->budget <= 0) {
                emit resolveFailed(lk->share, QStringLiteral("timeout"));
                lookups_.erase(lookups_.begin() +
                               static_cast<std::ptrdiff_t>(i));
                continue;
            }
            queryClosest(*lk, aeromesh::kAlpha);
            ++i;
        }
        if (lookups_.empty()) {
            roundTimer_.stop();
        }
    }

    // ---- helpers --------------------------------------------------------

    Lookup* findLookup(const std::string& targetHex) {
        for (auto& lk : lookups_) {
            if (lk->targetHex == targetHex) {
                return lk.get();
            }
        }
        return nullptr;
    }

    static void addToShortlist(Lookup& lk, const aeromesh::Contact& c) {
        if (c.endpoint.empty()) {
            return;
        }
        for (const auto& existing : lk.shortlist) {
            if (existing.id == c.id) {
                return;
            }
        }
        lk.shortlist.push_back(c);
    }

    void finishResolved(Lookup* lk, const std::string& endpoint) {
        const QString share = lk->share;
        removeLookup(lk);
        emitResolved(share, endpoint);
        if (lookups_.empty()) {
            roundTimer_.stop();
        }
    }

    void removeLookup(Lookup* lk) {
        for (std::size_t i = 0; i < lookups_.size(); ++i) {
            if (lookups_[i].get() == lk) {
                lookups_.erase(lookups_.begin() +
                               static_cast<std::ptrdiff_t>(i));
                return;
            }
        }
    }

    void emitResolved(const QString& share, const std::string& endpoint) {
        const auto ep = aeromesh::Endpoint::parse(endpoint);
        if (!ep) {
            emit resolveFailed(share, QStringLiteral("timeout"));
            return;
        }
        emit resolved(share, QString::fromStdString(ep->host),
                      static_cast<int>(ep->port));
    }

    static aeromesh::NodeId readId(const std::vector<std::byte>& buf,
                                   std::size_t offset) {
        aeromesh::NodeId::Bytes b{};
        for (std::size_t i = 0; i < aeromesh::kIdBytes; ++i) {
            b[i] = buf[offset + i];
        }
        return aeromesh::NodeId(b);
    }

    static void appendId(std::vector<std::byte>& out,
                         const aeromesh::NodeId& id) {
        out.insert(out.end(), id.bytes().begin(), id.bytes().end());
    }

    NetworkEngine* engine_ = nullptr;
    const aeromesh::Identity* self_ = nullptr;
    aeromesh::NodeId selfId_{};
    std::unique_ptr<aeromesh::Dht> dht_;
    std::vector<std::string> bootstrap_;
    std::vector<std::unique_ptr<Lookup>> lookups_;
    QTimer roundTimer_;
};
