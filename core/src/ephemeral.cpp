#include "aeromesh/ephemeral.hpp"

#include <sodium.h>

namespace aeromesh {

EphemeralStore::EphemeralStore(std::uint64_t ttl_ms)
    : ttl_ms_(ttl_ms == 0 ? kEphemeralTtlMs : ttl_ms) {}

EphemeralStore::~EphemeralStore() { wipe_all(); }

void EphemeralStore::wipe(Entry& e) {
    if (!e.data.empty())
        sodium_memzero(e.data.data(), e.data.size());
    e.data.clear();
    e.acks = 0;
    e.acks_required = 0;
}

bool EphemeralStore::contains(const EntryId& id) const {
    return entries_.find(id) != entries_.end();
}

std::expected<void, StoreError> EphemeralStore::put(const EntryId& id,
                                                    std::vector<std::byte> data,
                                                    std::uint32_t acks_required,
                                                    std::uint64_t created_ms) {
    if (acks_required < 1)
        return std::unexpected(StoreError::InvalidParams);
    if (entries_.find(id) != entries_.end())
        return std::unexpected(StoreError::Duplicate);

    Entry e;
    e.data = std::move(data);
    e.acks_required = acks_required;
    e.acks = 0;
    e.created_ms = created_ms;
    entries_.emplace(id, std::move(e));
    return {};
}

std::optional<std::span<const std::byte>> EphemeralStore::get(
    const EntryId& id) const {
    auto it = entries_.find(id);
    if (it == entries_.end())
        return std::nullopt;
    return std::span<const std::byte>(it->second.data);
}

std::expected<bool, StoreError> EphemeralStore::ack(const EntryId& id) {
    auto it = entries_.find(id);
    if (it == entries_.end())
        return std::unexpected(StoreError::NotFound);

    ++it->second.acks;
    if (it->second.acks >= it->second.acks_required) {
        wipe(it->second);
        entries_.erase(it);
        return true;  // destroyed via ACK
    }
    return false;
}

std::vector<DestroyedEntry> EphemeralStore::collect_expired(
    std::uint64_t now_ms) {
    std::vector<DestroyedEntry> destroyed;
    for (auto it = entries_.begin(); it != entries_.end();) {
        // age = now - created; guard against clock going backwards.
        const bool expired =
            now_ms >= it->second.created_ms &&
            (now_ms - it->second.created_ms) >= ttl_ms_;
        if (expired) {
            DestroyedEntry d;
            d.id = it->first;
            d.reason = DestroyReason::Expired;
            wipe(it->second);
            it = entries_.erase(it);
            destroyed.push_back(d);
        } else {
            ++it;
        }
    }
    return destroyed;
}

std::optional<std::uint64_t> EphemeralStore::next_expiry_ms(
    std::uint64_t now_ms) const {
    std::optional<std::uint64_t> soonest;
    for (const auto& kv : entries_) {
        const std::uint64_t deadline = kv.second.created_ms + ttl_ms_;
        const std::uint64_t remaining = deadline > now_ms ? deadline - now_ms : 0;
        if (!soonest || remaining < *soonest)
            soonest = remaining;
    }
    return soonest;
}

bool EphemeralStore::destroy(const EntryId& id) {
    auto it = entries_.find(id);
    if (it == entries_.end())
        return false;
    wipe(it->second);
    entries_.erase(it);
    return true;
}

void EphemeralStore::wipe_all() {
    for (auto& kv : entries_)
        wipe(kv.second);
    entries_.clear();
}

} // namespace aeromesh
