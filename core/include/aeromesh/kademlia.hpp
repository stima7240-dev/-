#pragma once

// Kademlia node logic: FIND_NODE handling, iterative lookups, and bootstrap.
// The transport is abstracted behind QueryFn so the algorithm can be unit
// tested over an in-memory network and later wired to the real frame layer.

#include <cstddef>
#include <expected>
#include <functional>
#include <span>
#include <vector>

#include "aeromesh/node_id.hpp"
#include "aeromesh/routing_table.hpp"

namespace aeromesh {

// Lookup concurrency parameter "alpha": how many peers we query per round.
inline constexpr std::size_t kAlpha = 3;

enum class DhtError {
    Malformed,
    TooLarge,
};

// Wire (de)serialisation for a list of contacts. This is the body of a
// FIND_NODE reply once it is carried inside a DhtReply packet payload.
// Layout: u16 count, then per contact: id[32], u16 endpoint_len, endpoint bytes
// (all integers big-endian).
std::expected<std::vector<std::byte>, DhtError> encode_contacts(
    const std::vector<Contact>& contacts);
std::expected<std::vector<Contact>, DhtError> decode_contacts(
    std::span<const std::byte> data);

// Abstracts "send FIND_NODE(target) to peer and return the closest contacts the
// peer knows about". In production this is a framed DhtQuery -> DhtReply round
// trip; in tests it is a direct call into a simulated network.
using QueryFn = std::function<std::vector<Contact>(const Contact& peer,
                                                   const NodeId& target)>;

class Dht {
public:
    explicit Dht(NodeId self, std::size_t k = kBucketSize);

    const NodeId& self() const noexcept { return self_; }
    RoutingTable& table() noexcept { return table_; }
    const RoutingTable& table() const noexcept { return table_; }

    // Server side: answer a FIND_NODE for `target` from our own table.
    std::vector<Contact> handle_find_node(const NodeId& target) const;

    // Client side: iterative Kademlia lookup. Returns up to k contacts closest
    // to `target` discovered along the way; learned peers are folded into the
    // routing table as a side effect.
    std::vector<Contact> lookup(const NodeId& target, const QueryFn& query);

    // Seed the table from known contacts, then run a self-lookup to populate
    // the buckets. Returns the number of contacts known afterwards.
    std::size_t bootstrap(const std::vector<Contact>& seeds,
                          const QueryFn& query);

private:
    NodeId self_;
    RoutingTable table_;
    std::size_t k_;
};

} // namespace aeromesh
