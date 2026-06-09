#include "aeromesh/routing_table.hpp"

#include <algorithm>

namespace aeromesh {

RoutingTable::RoutingTable(NodeId self, std::size_t k)
    : self_(self), k_(k) {}

std::size_t RoutingTable::bucket_index(const NodeId& id) const noexcept {
    const std::size_t spl = NodeId::shared_prefix_length(self_, id);
    return spl >= kIdBits ? kIdBits - 1 : spl;
}

bool RoutingTable::update(const Contact& c) {
    if (c.id == self_) return false;
    auto& bucket = buckets_[bucket_index(c.id)];
    const auto it = std::find_if(bucket.begin(), bucket.end(),
                                 [&](const Contact& x) { return x.id == c.id; });
    if (it != bucket.end()) {
        bucket.erase(it);
        bucket.push_back(c);  // refresh: most-recently seen goes to the back
        return true;
    }
    if (bucket.size() < k_) {
        bucket.push_back(c);
        return true;
    }
    return false;  // bucket full
}

std::vector<Contact> RoutingTable::closest(const NodeId& target,
                                           std::size_t count) const {
    std::vector<Contact> all;
    for (const auto& bucket : buckets_)
        all.insert(all.end(), bucket.begin(), bucket.end());
    const CloserTo cmp{target};
    std::sort(all.begin(), all.end(),
              [&](const Contact& a, const Contact& b) { return cmp(a.id, b.id); });
    if (all.size() > count) all.resize(count);
    return all;
}

std::size_t RoutingTable::size() const noexcept {
    std::size_t n = 0;
    for (const auto& b : buckets_) n += b.size();
    return n;
}

} // namespace aeromesh
