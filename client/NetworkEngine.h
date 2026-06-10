#pragma once

// AeroMesh client network engine.
//
// This is the bridge that finally connects the tested, OS-independent core to
// the live network. It owns:
//   * a real UDP socket (aeromesh::platform::UdpSocket, Winsock/BSD), and
//   * the core Transport, which layers constant-rate cover traffic and
//     per-type packet dispatch on top of that socket.
//
// A Qt timer drives the engine: on every tick it drains inbound datagrams
// (receive) and emits at most one frame per peer link (pump) -- a queued real
// packet if there is one, otherwise an indistinguishable dummy. Higher layers
// (DHT bootstrap, session handshake, chat) install packet handlers and register
// peers on top of this in later steps.
//
// Only network-safe status is exposed to QML: whether we are online, the bound
// UDP port, and cumulative frame counters. No key material is ever exposed.

#include <QObject>
#include <QRandomGenerator>
#include <QString>
#include <QTimer>

#include <chrono>
#include <cstdint>
#include <memory>

#include "aeromesh/packet.hpp"
#include "aeromesh/transport.hpp"
#include "aeromesh/udp_socket.hpp"

class NetworkEngine : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool online READ online NOTIFY statusChanged)
    Q_PROPERTY(int port READ port NOTIFY statusChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY statusChanged)
    Q_PROPERTY(quint64 framesSent READ framesSent NOTIFY statsChanged)
    Q_PROPERTY(quint64 realReceived READ realReceived NOTIFY statsChanged)

public:
    explicit NetworkEngine(QObject* parent = nullptr) : QObject(parent) {
        connect(&timer_, &QTimer::timeout, this, &NetworkEngine::tick);
    }

    [[nodiscard]] bool online() const { return online_; }
    [[nodiscard]] int port() const { return static_cast<int>(port_); }
    [[nodiscard]] QString lastError() const { return lastError_; }
    [[nodiscard]] quint64 framesSent() const {
        return transport_ ? transport_->stats().frames_sent : 0;
    }
    [[nodiscard]] quint64 realReceived() const {
        return transport_ ? transport_->stats().real_received : 0;
    }

    // Bind a UDP socket and start the cover-traffic engine. A bindPort of 0
    // selects an ephemeral port. Returns "" on success or a short error code
    // ("bind") that QML maps to a localized message.
    Q_INVOKABLE QString start(int bindPort = 0) {
        if (online_) {
            return QString();
        }
        const std::uint16_t want =
            static_cast<std::uint16_t>(bindPort < 0 ? 0 : bindPort);
        auto sock = aeromesh::UdpSocket::bind("0.0.0.0", want);
        if (!sock) {
            lastError_ = QStringLiteral("bind");
            emit statusChanged();
            return lastError_;
        }
        socket_ = std::make_unique<aeromesh::UdpSocket>(std::move(*sock));
        port_ = socket_->local_port();

        // A CSPRNG-drawn seed selects this run's cover-traffic delay sequence;
        // each peer link derives a distinct seed downstream so links stay
        // uncorrelated.
        const std::uint64_t seed = QRandomGenerator::global()->generate64();
        transport_ = std::make_unique<aeromesh::Transport>(
            *socket_, kMinIntervalMs, kMaxIntervalMs, seed);

        online_ = true;
        lastError_.clear();
        timer_.start(kTickMs);
        emit statusChanged();
        return QString();
    }

    // Stop the engine, drop every link, and release the socket.
    Q_INVOKABLE void stop() {
        timer_.stop();
        transport_.reset();
        socket_.reset();
        online_ = false;
        port_ = 0;
        emit statusChanged();
    }

    // Register a peer link by "host:port" so the engine maintains a uniform
    // cover-traffic stream toward it. A no-op for an already-known peer.
    // Returns false if the engine is offline or the address is malformed.
    Q_INVOKABLE bool addPeer(const QString& hostPort) {
        if (!transport_) {
            return false;
        }
        const auto ep = aeromesh::Endpoint::parse(hostPort.toStdString());
        if (!ep) {
            return false;
        }
        transport_->add_peer(*ep, nowMs());
        return true;
    }

    // Non-owning access to the underlying core transport so the session/chat
    // layer (C++) can register typed packet handlers via on(), send packets,
    // and add peers. Returns nullptr while the engine is offline. Handlers must
    // be re-registered after a stop()/start() cycle, which rebuilds transport_.
    [[nodiscard]] aeromesh::Transport* transport() const {
        return transport_.get();
    }

    // The engine's monotonic clock in milliseconds. The session layer reuses
    // this same timeline for handshake/delivery timers so all scheduling stays
    // consistent with the cover-traffic pump.
    [[nodiscard]] static std::uint64_t clockMs() { return nowMs(); }

signals:
    void statusChanged();
    void statsChanged();

private slots:
    void tick() {
        if (!transport_) {
            return;
        }
        transport_->receive();
        transport_->pump(nowMs());
        emit statsChanged();
    }

private:
    // Cover-traffic cadence: each link's slot delay is sampled uniformly from
    // [min, max] ms, so the stream carries no timing signal about real chat.
    static constexpr std::uint64_t kMinIntervalMs = 200;
    static constexpr std::uint64_t kMaxIntervalMs = 1200;
    // How often we service the socket and schedulers.
    static constexpr int kTickMs = 20;

    static std::uint64_t nowMs() {
        using namespace std::chrono;
        return static_cast<std::uint64_t>(
            duration_cast<milliseconds>(
                steady_clock::now().time_since_epoch())
                .count());
    }

    QTimer timer_;
    // Declared before transport_ so that on destruction transport_ (which holds
    // a reference to *socket_) is torn down first.
    std::unique_ptr<aeromesh::UdpSocket> socket_;
    std::unique_ptr<aeromesh::Transport> transport_;
    bool online_ = false;
    std::uint16_t port_ = 0;
    QString lastError_;
};
