#include "aeromesh/routing_table.hpp"

#include <algorithm>
#include <openssl/sha.h>
#include <unordered_map>

namespace aeromesh {

RoutingTable::RoutingTable(NodeId self, std::size_t k)
    : self_(self), k_(k) {}

std::size_t RoutingTable::bucket_index(const NodeId& id) const noexcept {
    const std::size_t spl = NodeId::shared_prefix_length(self_, id);
    return spl >= kIdBits ? kIdBits - 1 : spl;
}

bool RoutingTable::verify_pow(const NodeId& id, std::uint64_t nonce, 
                               std::size_t difficulty) {
    // Security: verify proof-of-work to prevent Sybil attacks
    // Hash(node_id || nonce) must have at least 'difficulty' leading zero bits
    std::array<std::byte, kIdBytes + sizeof(nonce)> input;
    std::memcpy(input.data(), id.bytes().data(), kIdBytes);
    std::memcpy(input.data() + kIdBytes, &nonce, sizeof(nonce));
    
    std::array<std::byte, SHA256_DIGEST_LENGTH> hash;
    SHA256(reinterpret_cast<const unsigned char*>(input.data()), 
           input.size(),
           reinterpret_cast<unsigned char*>(hash.data()));
    
    // Count leading zero bits
    std::size_t zero_bits = 0;
    for (const auto byte : hash) {
        if (byte == std::byte{0}) {
            zero_bits += 8;
        } else {
            // Count leading zeros in this byte
            auto b = std::to_integer<unsigned char>(byte);
            while ((b & 0x80) == 0 && zero_bits < difficulty) {
                zero_bits++;
                b <<= 1;
            }
            break;
        }
        if (zero_bits >= difficulty) break;
    }
    
    return zero_bits >= difficulty;
}

bool RoutingTable::update(const Contact& c) {
    if (c.id == self_) return false;
    
    // Security: verify proof-of-work before accepting new nodes
    if (!verify_pow(c.id, c.pow_nonce)) {
        return false;
    }
    
    // Security: reject nodes with too low reputation
    if (c.reputation < kMinReputation) {
        return false;
    }
    
    auto& bucket = buckets_[bucket_index(c.id)];
    const auto it = std::find_if(bucket.begin(), bucket.end(),
                                 [&](const Contact& x) { return x.id == c.id; });
    if (it != bucket.end()) {
        // Update existing contact, preserve reputation history
        it->endpoint = c.endpoint;
        it->last_seen = std::chrono::steady_clock::now();
        
        // Move to back (most-recently seen)
        auto updated = *it;
        bucket.erase(it);
        bucket.push_back(updated);
        return true;
    }
    
    if (bucket.size() < k_) {
        auto new_contact = c;
        new_contact.last_seen = std::chrono::steady_clock::now();
        bucket.push_back(new_contact);
        return true;
    }
    
    // Security: if bucket is full, try to evict low-reputation node
    auto lowest_rep = std::min_element(bucket.begin(), bucket.end(),
        [](const Contact& a, const Contact& b) {
            return a.reputation < b.reputation;
        });
    
    if (lowest_rep != bucket.end() && lowest_rep->reputation < c.reputation) {
        *lowest_rep = c;
        lowest_rep->last_seen = std::chrono::steady_clock::now();
        return true;
    }
    
    return false;  // bucket full with better nodes
}

std::vector<Contact> RoutingTable::closest(const NodeId& target,
                                           std::size_t count) const {
    std::vector<Contact> all;
    for (const auto& bucket : buckets_) {
        for (const auto& contact : bucket) {
            // Security: filter out low-reputation nodes
            if (contact.reputation >= kMinReputation) {
                all.push_back(contact);
            }
        }
    }
    
    const CloserTo cmp{target};
    std::sort(all.begin(), all.end(),
              [&](const Contact& a, const Contact& b) { return cmp(a.id, b.id); });
    if (all.size() > count) all.resize(count);
    return all;
}

void RoutingTable::update_reputation(const NodeId& id, bool success) {
    // Security: update reputation based on interaction outcome
    auto* contact = find_contact(id);
    if (!contact) return;
    
    if (success) {
        contact->success_count++;
        // Increase reputation, cap at max
        if (contact->reputation < kMaxReputation) {
            contact->reputation = std::min<std::uint8_t>(
                kMaxReputation, 
                contact->reputation + 1
            );
        }
    } else {
        contact->failure_count++;
        // Decrease reputation more aggressively
        if (contact->reputation > 0) {
            contact->reputation = contact->reputation > 3 
                ? contact->reputation - 3 
                : 0;
        }
    }
}

Contact* RoutingTable::find_contact(const NodeId& id) {
    auto& bucket = buckets_[bucket_index(id)];
    auto it = std::find_if(bucket.begin(), bucket.end(),
                          [&](const Contact& x) { return x.id == id; });
    return it != bucket.end() ? &(*it) : nullptr;
}

void RoutingTable::prune_low_reputation() {
    // Security: remove nodes with very low reputation
    for (auto& bucket : buckets_) {
        bucket.erase(
            std::remove_if(bucket.begin(), bucket.end(),
                [](const Contact& c) { return c.reputation < kMinReputation; }),
            bucket.end()
        );
    }
}

void RoutingTable::enforce_diversity() {
    // Security: prevent eclipse attacks by ensuring IP diversity
    // Track /24 subnets to prevent too many nodes from same network
    for (auto& bucket : buckets_) {
        std::unordered_map<std::string, std::size_t> subnet_count;
        
        for (const auto& contact : bucket) {
            // Extract /24 subnet from endpoint (simplified)
            auto colon_pos = contact.endpoint.find(':');
            if (colon_pos != std::string::npos) {
                auto ip = contact.endpoint.substr(0, colon_pos);
                auto last_dot = ip.rfind('.');
                if (last_dot != std::string::npos) {
                    auto subnet = ip.substr(0, last_dot);
                    subnet_count[subnet]++;
                }
            }
        }
        
        // Remove excess nodes from over-represented subnets
        for (const auto& [subnet, count] : subnet_count) {
            if (count > 3) {  // Max 3 nodes per /24
                std::size_t removed = 0;
                bucket.erase(
                    std::remove_if(bucket.begin(), bucket.end(),
                        [&](const Contact& c) {
                            if (removed >= count - 3) return false;
                            auto colon_pos = c.endpoint.find(':');
                            if (colon_pos != std::string::npos) {
                                auto ip = c.endpoint.substr(0, colon_pos);
                                auto last_dot = ip.rfind('.');
                                if (last_dot != std::string::npos) {
                                    auto c_subnet = ip.substr(0, last_dot);
                                    if (c_subnet == subnet) {
                                        removed++;
                                        return true;
                                    }
                                }
                            }
                            return false;
                        }),
                    bucket.end()
                );
            }
        }
    }
}

std::size_t RoutingTable::size() const noexcept {
    std::size_t n = 0;
    for (const auto& b : buckets_) n += b.size();
    return n;
}

} // namespace aeromesh
