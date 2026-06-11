#pragma once

// Kademlia routing table: kIdBits buckets keyed by shared-prefix length with
// self, each holding up to k contacts.

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "aeromesh/node_id.hpp"

namespace aeromesh {

// A reachable peer: its id plus a transport-specific locator (e.g. host:port).
struct Contact {
    NodeId id;
    std::string endpoint;
    
    // Security: proof-of-work nonce to prevent Sybil attacks
    std::uint64_t pow_nonce{0};
    
    // Security: reputation score (0-100), starts at 50
    std::uint8_t reputation{50};
    
    // Security: timestamp of last interaction
    std::chrono::steady_clock::time_point last_seen{};
    
    // Security: number of successful interactions
    std::uint32_t success_count{0};
    
    // Security: number of failed interactions
    std::uint32_t failure_count{0};
    
    bool operator==(const Contact& o) const noexcept { return id == o.id; }
};

// Kademlia replication parameter "k".
inline constexpr std::size_t kBucketSize = 20;

// Security: minimum proof-of-work difficulty (leading zero bits)
inline constexpr std::size_t kMinPowDifficulty = 16;

// Security: reputation thresholds
inline constexpr std::uint8_t kMinReputation = 20;
inline constexpr std::uint8_t kMaxReputation = 100;

class RoutingTable {
public:
    explicit RoutingTable(NodeId self, std::size_t k = kBucketSize);

    // Insert or refresh a contact. Self is never stored. If the contact is
    // already known its entry is moved to the back (most-recently seen). When
    // the target bucket is full the new contact is dropped and false is
    // returned -- real Kademlia would ping the least-recently-seen node and
    // evict it only if it is dead; that needs live transport and arrives later.
    // Security: now validates proof-of-work and reputation before accepting.
    bool update(const Contact& c);

    // Up to `count` known contacts closest to `target` by XOR distance,
    // sorted closest-first.
    // Security: filters out low-reputation nodes.
    std::vector<Contact> closest(const NodeId& target, std::size_t count) const;

    std::size_t size() const noexcept;
    bool empty() const noexcept { return size() == 0; }
    const NodeId& self() const noexcept { return self_; }

    // Index of the bucket the id falls into (0..kIdBits-1).
    std::size_t bucket_index(const NodeId& id) const noexcept;
    
    // Security: verify proof-of-work for a node ID
    static bool verify_pow(const NodeId& id, std::uint64_t nonce, 
                          std::size_t difficulty = kMinPowDifficulty);
    
    // Security: update reputation after interaction
    void update_reputation(const NodeId& id, bool success);
    
    // Security: get contact by ID (for reputation updates)
    Contact* find_contact(const NodeId& id);
    
    // Security: evict low-reputation nodes
    void prune_low_reputation();
    
    // Security: diversify bucket to prevent eclipse attacks
    void enforce_diversity();

private:
    NodeId self_;
    std::size_t k_;
    std::array<std::vector<Contact>, kIdBits> buckets_{};
};

} // namespace aeromesh
