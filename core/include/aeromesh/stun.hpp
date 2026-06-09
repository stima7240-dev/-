#pragma once

// STUN client (RFC 5389 / RFC 8489) for NAT-traversal address discovery.
//
// SECURITY / ANONYMITY NOTE:
// A STUN Binding request reveals this node's source IP:port to the STUN
// server. It is therefore only appropriate in DIRECT-connection mode. When
// traffic is routed through the onion layer, reflexive-address discovery MUST
// be disabled -- the onion circuit, not STUN, provides the reachable address.
// This module is an OPTIONAL connectivity helper and must never be engaged
// automatically while anonymity routing is active.
//
// The protocol logic is OS-independent: it speaks through the abstract
// IDatagramSocket, so the real UdpSocket (platform module) and the in-memory
// test socket both drive it. Only Binding requests over UDP are implemented
// (no MESSAGE-INTEGRITY / TLS); that is all server-reflexive discovery needs.

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

#include "aeromesh/transport.hpp"

namespace aeromesh {

enum class StunError {
    EncodeFailed,
    SendFailed,
    Timeout,
    Malformed,
    NotASuccessResponse,
    TransactionMismatch,
    NoMappedAddress,
    UnsupportedFamily,
};

inline constexpr std::size_t kStunHeaderLen = 20;
inline constexpr std::size_t kStunTxnIdLen = 12;
inline constexpr std::uint16_t kStunBindingRequest = 0x0001;
inline constexpr std::uint16_t kStunBindingSuccess = 0x0101;
inline constexpr std::uint16_t kStunAttrMappedAddress = 0x0001;
inline constexpr std::uint16_t kStunAttrXorMappedAddress = 0x0020;
inline constexpr std::uint32_t kStunMagicCookie = 0x2112A442u;

using StunTxnId = std::array<std::byte, kStunTxnIdLen>;

// A Binding request ready to send, paired with its transaction id so the
// matching response can be authenticated against it.
struct StunBindingRequest {
    StunTxnId txn_id;
    std::array<std::byte, kStunHeaderLen> datagram;
};

// Build a Binding request with a cryptographically random transaction id.
StunBindingRequest make_binding_request();

// Build a Binding request with a caller-supplied transaction id (testing).
StunBindingRequest make_binding_request(const StunTxnId& txn_id);

// Parse a Binding success response and return the reflexive (public) Endpoint.
// Accepts XOR-MAPPED-ADDRESS (preferred) or MAPPED-ADDRESS; IPv4 only.
std::expected<Endpoint, StunError> parse_binding_response(
    std::span<const std::byte> datagram, const StunTxnId& expected_txn_id);

// Discover this node's server-reflexive address by querying `stun_server`
// through `socket`. Sends one Binding request, then polls up to `max_polls`
// times for a matching response (best-effort, no retransmit). Datagrams that
// do not match the transaction are ignored. Returns Timeout if none arrives.
std::expected<Endpoint, StunError> discover_reflexive_address(
    IDatagramSocket& socket, const Endpoint& stun_server, int max_polls = 500);

} // namespace aeromesh
