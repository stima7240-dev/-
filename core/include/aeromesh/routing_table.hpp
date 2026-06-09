#pragma once

// Kademlia routing table: kIdBits buckets keyed by shared-prefix length with
// self, each holding up to k contacts.

#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include "aeromesh/node_id.hpp"

namespace aeromesh {

// A reachable peer: its id plus a transport-specific locator (e.g. host:port).
struct Contact {
    NodeId id;
    std::string endpoint;
    bool operator==(const Contact& o) const noexcept { return id == o.id; }
};

// Kademlia replication parameter "k".
inline constexpr std::size_t kBucketSize = 20;

class RoutingTable {
public:
    explicit RoutingTable(NodeId self, std::size_t k = kBucketSize);

    // Insert or refresh a contact. Self is never stored. If the contact is
    // already known its entry is moved to the back (most-recently seen). When
    // the target bucket is full the new contact is dropped and false is
    // returned -- real Kademlia would ping the least-recently-seen node and
    // evict it only if it is dead; that needs live transport and arrives later.
    bool update(const Contact& c);

    // Up to `count` known contacts closest to `target` by XOR distance,
    // sorted closest-first.
    std::vector<Contact> closest(const NodeId& target, std::size_t count) const;

    std::size_t size() const noexcept;
    bool empty() const noexcept { return size() == 0; }
    const NodeId& self() const noexcept { return self_; }

    // Index of the bucket the id falls into (0..kIdBits-1).
    std::size_t bucket_index(const NodeId& id) const noexcept;

private:
    NodeId self_;
    std::size_t k_;
    std::array<std::vector<Contact>, kIdBits> buckets_{};
};

} // namespace aeromesh
