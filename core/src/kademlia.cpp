#include "aeromesh/kademlia.hpp"

#include <algorithm>
#include <cstring>
#include <unordered_set>

namespace aeromesh {

namespace {

void put_u16(std::vector<std::byte>& out, std::uint16_t v) {
    out.push_back(static_cast<std::byte>((v >> 8) & 0xFF));
    out.push_back(static_cast<std::byte>(v & 0xFF));
}

std::uint16_t get_u16(std::span<const std::byte> d, std::size_t off) noexcept {
    return static_cast<std::uint16_t>(
        (std::to_integer<std::uint16_t>(d[off]) << 8) |
        std::to_integer<std::uint16_t>(d[off + 1]));
}

} // namespace

std::expected<std::vector<std::byte>, DhtError> encode_contacts(
    const std::vector<Contact>& contacts) {
    if (contacts.size() > 0xFFFF) return std::unexpected(DhtError::TooLarge);
    std::vector<std::byte> out;
    put_u16(out, static_cast<std::uint16_t>(contacts.size()));
    for (const auto& c : contacts) {
        if (c.endpoint.size() > 0xFFFF) return std::unexpected(DhtError::TooLarge);
        out.insert(out.end(), c.id.bytes().begin(), c.id.bytes().end());
        put_u16(out, static_cast<std::uint16_t>(c.endpoint.size()));
        for (const char ch : c.endpoint)
            out.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
    }
    return out;
}

std::expected<std::vector<Contact>, DhtError> decode_contacts(
    std::span<const std::byte> data) {
    if (data.size() < 2) return std::unexpected(DhtError::Malformed);
    std::size_t off = 0;
    const std::uint16_t count = get_u16(data, off);
    off += 2;
    std::vector<Contact> out;
    out.reserve(count);
    for (std::uint16_t i = 0; i < count; ++i) {
        if (off + kIdBytes + 2 > data.size())
            return std::unexpected(DhtError::Malformed);
        NodeId::Bytes idb{};
        std::memcpy(idb.data(), data.data() + off, kIdBytes);
        off += kIdBytes;
        const std::uint16_t elen = get_u16(data, off);
        off += 2;
        if (off + elen > data.size())
            return std::unexpected(DhtError::Malformed);
        std::string ep(reinterpret_cast<const char*>(data.data() + off), elen);
        off += elen;
        out.push_back(Contact{NodeId(idb), std::move(ep)});
    }
    return out;
}

Dht::Dht(NodeId self, std::size_t k)
    : self_(self), table_(self, k), k_(k) {}

std::vector<Contact> Dht::handle_find_node(const NodeId& target) const {
    return table_.closest(target, k_);
}

std::vector<Contact> Dht::lookup(const NodeId& target, const QueryFn& query) {
    const CloserTo cmp{target};
    const auto closer = [&](const Contact& a, const Contact& b) {
        return cmp(a.id, b.id);
    };

    std::vector<Contact> shortlist = table_.closest(target, k_);
    std::unordered_set<std::string> seen;     // contact ids already in shortlist
    std::unordered_set<std::string> queried;  // contact ids already queried
    for (const auto& c : shortlist) seen.insert(c.id.to_hex());

    while (true) {
        std::sort(shortlist.begin(), shortlist.end(), closer);
        if (shortlist.size() > k_) shortlist.resize(k_);

        // Pick up to alpha of the closest not-yet-queried contacts.
        std::vector<Contact> batch;
        for (const auto& c : shortlist) {
            if (!queried.contains(c.id.to_hex())) {
                batch.push_back(c);
                if (batch.size() >= kAlpha) break;
            }
        }
        if (batch.empty()) break;  // every node in the k-closest set is queried

        for (const auto& peer : batch) {
            queried.insert(peer.id.to_hex());
            for (const auto& r : query(peer, target)) {
                if (r.id == self_) continue;
                table_.update(r);
                if (seen.insert(r.id.to_hex()).second) shortlist.push_back(r);
            }
        }
    }

    std::sort(shortlist.begin(), shortlist.end(), closer);
    if (shortlist.size() > k_) shortlist.resize(k_);
    return shortlist;
}

std::size_t Dht::bootstrap(const std::vector<Contact>& seeds,
                           const QueryFn& query) {
    for (const auto& s : seeds) table_.update(s);
    lookup(self_, query);  // self-lookup fills the buckets around our own id
    return table_.size();
}

} // namespace aeromesh
