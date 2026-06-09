#pragma once

// TURN-like relay fallback for NAT traversal.
//
// When direct connectivity fails (e.g. both peers are behind symmetric NATs
// that hole punching cannot defeat), the two peers fall back to a mutually
// reachable relay node. Each peer BINDs to the relay under a shared session id
// (distributed out of band via the coordinator). The relay pairs the two
// bindings and forwards opaque datagrams between them.
//
// SECURITY / ANONYMITY NOTE:
// The relay sees METADATA -- the two peers' IP:port pairs and that they are
// communicating -- but NEVER plaintext: every forwarded payload is already a
// SecureSession/ratchet ciphertext (see session.hpp). The session id only
// correlates the pairing; it does NOT authenticate identity, which remains the
// job of the SecureSession handshake carried inside the relayed payloads. Run
// the relay as an untrusted forwarder; for stronger metadata protection, route
// relay traffic through the onion layer.
//
// OS-independent: both server and client drive the abstract IDatagramSocket,
// so the real UdpSocket and the in-memory test socket work unchanged.

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <vector>

#include "aeromesh/transport.hpp"

namespace aeromesh {

inline constexpr std::size_t kRelaySessionIdLen = 16;
inline constexpr std::size_t kRelayHeaderLen = 6; // magic(4) + version(1) + type(1)

using RelaySessionId = std::array<std::byte, kRelaySessionIdLen>;

enum class RelayMessageType {
    Bind,    // peer -> relay: register under a session
    BindOk,  // relay -> peer: registration acknowledged
    Data,    // peer <-> relay: opaque payload to forward to the paired peer
    Unknown,
};

struct RelayMessage {
    RelayMessageType type = RelayMessageType::Unknown;
    RelaySessionId session{};
    std::vector<std::byte> payload; // populated only for Data
};

// Generate a random relay session id (coordinator side).
RelaySessionId generate_relay_session_id();

// Encoders.
std::vector<std::byte> encode_relay_bind(const RelaySessionId& session);
std::vector<std::byte> encode_relay_bind_ok(const RelaySessionId& session);
std::vector<std::byte> encode_relay_data(const RelaySessionId& session,
                                         std::span<const std::byte> payload);

// Parse any relay message. Returns nullopt if it is not a valid relay packet.
std::optional<RelayMessage> parse_relay_message(std::span<const std::byte> data);

// --- Relay server ------------------------------------------------------------

struct RelayStats {
    std::uint64_t binds = 0;      // BIND messages accepted
    std::uint64_t forwarded = 0;  // DATA messages relayed to a peer
    std::uint64_t dropped = 0;    // unknown / unpaired / non-member / garbage
};

class RelayServer {
public:
    explicit RelayServer(IDatagramSocket& socket);

    // Process one inbound datagram: BIND registers the sender and replies
    // BIND_OK; DATA from a paired member is forwarded to the other member.
    // Returns true if it was a valid relay message.
    bool on_datagram(const Endpoint& from, std::span<const std::byte> data);

    // Drain all ready inbound datagrams from the socket.
    void pump();

    std::size_t session_count() const { return allocations_.size(); }
    const RelayStats& stats() const { return stats_; }

private:
    struct Allocation {
        std::array<Endpoint, 2> peers;
        std::size_t count = 0;
    };

    IDatagramSocket& socket_;
    std::map<RelaySessionId, Allocation> allocations_;
    RelayStats stats_;
};

// --- Relay client ------------------------------------------------------------

enum class RelayClientState {
    Binding,
    Bound,
    Failed,
};

class RelayClient {
public:
    RelayClient(IDatagramSocket& socket, Endpoint relay, RelaySessionId session,
                std::uint64_t now_ms, std::uint64_t retry_ms = 250,
                std::uint64_t timeout_ms = 10000);

    // Resend BIND until BIND_OK arrives or the timeout elapses.
    void tick(std::uint64_t now_ms);

    // Feed one inbound datagram. BIND_OK transitions to Bound; DATA payloads
    // are queued for poll_received(). Returns true if consumed.
    bool on_datagram(const Endpoint& from, std::span<const std::byte> data);

    // Send an application payload to the paired peer via the relay. Returns
    // false unless Bound.
    bool send_payload(std::span<const std::byte> payload);

    // Pop the next received payload, if any.
    bool poll_received(std::vector<std::byte>& out);

    RelayClientState state() const { return state_; }

private:
    void send_bind(std::uint64_t now_ms);

    IDatagramSocket& socket_;
    Endpoint relay_;
    RelaySessionId session_;
    RelayClientState state_ = RelayClientState::Binding;
    std::uint64_t start_ms_ = 0;
    std::uint64_t last_bind_ms_ = 0;
    bool first_bind_sent_ = false;
    std::uint64_t retry_ms_;
    std::uint64_t timeout_ms_;
    std::vector<std::vector<std::byte>> received_;
};

} // namespace aeromesh
