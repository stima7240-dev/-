#pragma once

// UDP hole punching for NAT traversal.
//
// Once two peers know each other's server-reflexive endpoints (learned out of
// band via a mutually reachable coordinator -- e.g. a DHT rendezvous node over
// the authenticated/onion control channel), they punch a hole by sending UDP
// probes to each other roughly simultaneously. The first probes prime the
// local NAT bindings; subsequent probes get through once both sides have one.
//
// SECURITY NOTE:
// Hole punching establishes CONNECTIVITY, not TRUST. The 16-byte rendezvous
// token only correlates probes and blocks off-path spoofing; it does NOT
// authenticate the peer. Peer identity MUST still be verified by the
// SecureSession handshake (see session.hpp) once the path is open. The
// coordinator that hands out the token is not trusted for authentication.
//
// OS-independent: drives the abstract IDatagramSocket, so the real UdpSocket
// and the in-memory test socket both work. The caller pumps tick() and
// on_datagram() from its event loop using a monotonic millisecond clock.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "aeromesh/transport.hpp"

namespace aeromesh {

inline constexpr std::size_t kPunchTokenLen = 16;
inline constexpr std::size_t kPunchPacketLen = 22;

using PunchToken = std::array<std::byte, kPunchTokenLen>;

enum class PunchState {
    Punching,
    Established,
    Failed,
};

enum class PunchType {
    Probe,
    Ack,
};

struct PunchConfig {
    Endpoint peer;                     // peer's reflexive endpoint (coordinator)
    PunchToken token;                  // shared rendezvous token (correlator)
    std::uint64_t interval_ms = 200;   // probe cadence
    std::uint64_t timeout_ms = 10000;  // give up after this long
};

// Generate a random rendezvous token (coordinator side).
PunchToken generate_punch_token();

// Encode a punch packet (probe or ack) carrying the given token.
std::array<std::byte, kPunchPacketLen> encode_punch(PunchType type,
                                                    const PunchToken& token);

// Decoded punch packet.
struct PunchPacket {
    PunchType type;
};

// Parse and validate a punch packet against the expected token. Returns the
// decoded packet only if magic, version, type, and token all match. Token
// comparison is constant-time.
std::optional<PunchPacket> parse_punch(std::span<const std::byte> data,
                                       const PunchToken& expected);

class HolePuncher {
public:
    HolePuncher(IDatagramSocket& socket, PunchConfig config,
                std::uint64_t now_ms);

    // Drive the state machine: emit a probe when due, fail on timeout.
    void tick(std::uint64_t now_ms);

    // Feed one inbound datagram. Returns true if it was a valid punch packet
    // for this session (and was consumed).
    bool on_datagram(const Endpoint& from, std::span<const std::byte> data);

    PunchState state() const { return state_; }

    // The endpoint we actually heard the peer on (may differ from the
    // configured reflexive endpoint under some NATs). Valid once a probe or ack
    // has been received.
    const Endpoint& confirmed_peer() const { return confirmed_peer_; }

    std::uint32_t probes_sent() const { return probes_sent_; }
    std::uint32_t acks_sent() const { return acks_sent_; }

private:
    void send_probe(std::uint64_t now_ms);

    IDatagramSocket& socket_;
    PunchConfig config_;
    PunchState state_ = PunchState::Punching;
    std::uint64_t start_ms_ = 0;
    std::uint64_t last_send_ms_ = 0;
    bool first_probe_sent_ = false;
    Endpoint confirmed_peer_;
    std::uint32_t probes_sent_ = 0;
    std::uint32_t acks_sent_ = 0;
};

} // namespace aeromesh
