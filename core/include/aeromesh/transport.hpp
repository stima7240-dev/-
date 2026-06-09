#pragma once

// OS-independent datagram transport and packet router.
//
// aeromesh_core must stay portable, so it never touches a real socket. Instead
// it talks to an abstract IDatagramSocket; the real UDP implementation
// (Winsock / BSD sockets, plus STUN/TURN/ICE NAT traversal) lives in a separate
// platform module, and tests use an in-memory loopback. This keeps all routing
// logic deterministic and unit-testable.
//
// Transport ties three things together:
//   * constant-rate cover traffic (one CoverScheduler per peer link, so every
//     link emits a uniform frame stream regardless of real activity);
//   * outbound queueing (real packets ride the next scheduled slot);
//   * inbound dispatch (frames are decoded, dummies dropped, and real packets
//     routed to per-type handlers -- e.g. DhtQuery -> Kademlia, Data -> chat).

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "aeromesh/cover.hpp"
#include "aeromesh/packet.hpp"

namespace aeromesh {

// A network address: host (IP literal or name) plus UDP port.
struct Endpoint {
    std::string host;
    std::uint16_t port = 0;

    std::string to_string() const;                       // "host:port"
    static std::optional<Endpoint> parse(std::string_view s);

    bool operator==(const Endpoint& o) const {
        return port == o.port && host == o.host;
    }
    // Ordering so Endpoint can key a std::map.
    bool operator<(const Endpoint& o) const {
        return host < o.host || (host == o.host && port < o.port);
    }
};

// Abstract best-effort datagram socket. Implementations: real UDP (platform
// module) and the in-memory loopback used in tests.
class IDatagramSocket {
public:
    virtual ~IDatagramSocket() = default;

    // Best-effort send (like UDP). Returns false only on a hard local error.
    virtual bool send(const Endpoint& to, std::span<const std::byte> data) = 0;

    // Non-blocking receive of one datagram. Returns false if none is ready.
    virtual bool poll(Endpoint& from, std::vector<std::byte>& out) = 0;
};

// Cumulative counters, handy for tests and diagnostics.
struct TransportStats {
    std::uint64_t frames_sent = 0;     // total frames put on the wire
    std::uint64_t real_sent = 0;       // frames carrying a real packet
    std::uint64_t dummy_sent = 0;      // cover frames
    std::uint64_t real_received = 0;   // valid non-dummy frames dispatched
    std::uint64_t dropped = 0;         // malformed / wrong-size / dummy inbound
};

class Transport {
public:
    using Handler = std::function<void(const Endpoint& from, const Packet&)>;

    // Per-link cover cadence: each link's slot delay is sampled uniformly from
    // [min_interval_ms, max_interval_ms] (clamped to 1 <= min <= max
    // downstream). `seed` selects the deterministic delay sequence; each peer
    // link gets a distinct derived seed so their streams stay uncorrelated.
    Transport(IDatagramSocket& socket, std::uint64_t min_interval_ms,
              std::uint64_t max_interval_ms, std::uint64_t seed);

    // Register a peer link, starting its cover scheduler at start_ms. Adding an
    // existing peer is a no-op.
    void add_peer(const Endpoint& peer, std::uint64_t start_ms);
    bool has_peer(const Endpoint& peer) const;
    std::size_t pending(const Endpoint& peer) const;

    // Install a handler for a packet type. Replaces any previous handler.
    void on(PacketType type, Handler handler);

    // Queue a real packet for `peer`'s next slot. Returns false if the peer is
    // unknown or the payload does not fit a frame.
    bool send(const Endpoint& peer, const Packet& packet);

    // Drive every link's scheduler to now_ms, sending at most one frame per
    // link per call (the queued real packet if any, otherwise a dummy) through
    // the socket. Call this frequently from a timer; each link's randomized
    // schedule decides when a frame is actually due.
    void pump(std::uint64_t now_ms);

    // Drain all ready inbound datagrams, decode, drop dummies/garbage, and
    // dispatch real packets to their handler.
    void receive();

    const TransportStats& stats() const { return stats_; }

private:
    IDatagramSocket& socket_;
    std::uint64_t min_interval_ms_;
    std::uint64_t max_interval_ms_;
    std::uint64_t seed_base_;
    std::uint64_t peer_seq_ = 0;
    std::map<Endpoint, CoverScheduler> links_;
    std::map<PacketType, Handler> handlers_;
    TransportStats stats_;
};

} // namespace aeromesh
