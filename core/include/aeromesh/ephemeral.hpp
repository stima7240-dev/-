#pragma once

// Ephemeral payload store with ACK-destroy and a hard TTL.
//
// Outgoing messages and relayed file chunks are held only as long as needed:
//   * once every required acknowledgement has arrived, the payload is securely
//     zeroized and dropped (ACK-destroy);
//   * regardless of ACKs, nothing survives past the TTL (12 hours per the
//     AeroMesh spec) -- it is wiped on the next garbage-collection pass.
//
// The store never reads the wall clock itself: callers pass an explicit
// millisecond timestamp (matching CoverScheduler), so behaviour is fully
// deterministic and testable. All wipes use sodium_memzero.

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace aeromesh {

// 12 hours, per the AeroMesh spec.
inline constexpr std::uint64_t kEphemeralTtlMs = 12ull * 60 * 60 * 1000;

enum class StoreError {
    NotFound,
    Duplicate,
    InvalidParams,
};

enum class DestroyReason {
    Acknowledged,  // all required acknowledgements received
    Expired,       // TTL elapsed before full acknowledgement
};

// Opaque 32-byte identifier (e.g. a message id or chunk tag).
using EntryId = std::array<std::byte, 32>;

struct DestroyedEntry {
    EntryId id{};
    DestroyReason reason = DestroyReason::Expired;
};

class EphemeralStore {
public:
    explicit EphemeralStore(std::uint64_t ttl_ms = kEphemeralTtlMs);
    ~EphemeralStore();

    EphemeralStore(const EphemeralStore&) = delete;
    EphemeralStore& operator=(const EphemeralStore&) = delete;

    std::uint64_t ttl_ms() const { return ttl_ms_; }
    std::size_t size() const { return entries_.size(); }
    bool contains(const EntryId& id) const;

    // Store a payload that needs `acks_required` (>= 1) acknowledgements.
    // `created_ms` is the store time used for TTL accounting.
    std::expected<void, StoreError> put(const EntryId& id,
                                        std::vector<std::byte> data,
                                        std::uint32_t acks_required,
                                        std::uint64_t created_ms);

    // Borrow the stored payload (e.g. to retransmit). Valid until the entry is
    // destroyed or the store is mutated.
    std::optional<std::span<const std::byte>> get(const EntryId& id) const;

    // Record one acknowledgement. When the count reaches acks_required the
    // entry is zeroized and removed. Returns true iff it was destroyed now.
    std::expected<bool, StoreError> ack(const EntryId& id);

    // Zeroize and drop every entry older than the TTL. Returns what was wiped
    // (reason == Expired) so the caller can log or notify.
    std::vector<DestroyedEntry> collect_expired(std::uint64_t now_ms);

    // Milliseconds until the soonest expiry, for scheduling the next GC.
    std::optional<std::uint64_t> next_expiry_ms(std::uint64_t now_ms) const;

    // Force-destroy a single entry (manual delete / panic wipe).
    bool destroy(const EntryId& id);

    // Zeroize and drop everything immediately (panic button).
    void wipe_all();

private:
    struct Entry {
        std::vector<std::byte> data;
        std::uint32_t acks_required = 1;
        std::uint32_t acks = 0;
        std::uint64_t created_ms = 0;
    };
    void wipe(Entry& e);

    std::uint64_t ttl_ms_;
    std::map<EntryId, Entry> entries_;
};

} // namespace aeromesh
