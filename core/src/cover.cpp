#include "aeromesh/cover.hpp"

namespace aeromesh {

namespace {
constexpr std::uint64_t clamp_min(std::uint64_t v) noexcept {
    return v == 0 ? 1 : v;
}
constexpr std::uint64_t clamp_max(std::uint64_t lo, std::uint64_t hi) noexcept {
    return hi < lo ? lo : hi;
}
} // namespace

CoverScheduler::CoverScheduler(std::uint64_t min_interval_ms,
                               std::uint64_t max_interval_ms,
                               std::uint64_t start_ms,
                               std::uint64_t seed)
    : min_interval_ms_(clamp_min(min_interval_ms)),
      max_interval_ms_(clamp_max(clamp_min(min_interval_ms), max_interval_ms)),
      next_slot_ms_(0),
      rng_state_(seed) {
    // Randomize the very first slot too, relative to the supplied start time.
    next_slot_ms_ = start_ms + sample_delay();
}

std::uint64_t CoverScheduler::sample_delay() noexcept {
    // splitmix64: small, fast, and well-distributed. Deterministic given the
    // seed, so timing is reproducible in tests yet unpredictable in production
    // (where the seed is drawn from a CSPRNG).
    rng_state_ += 0x9E3779B97F4A7C15ULL;
    std::uint64_t z = rng_state_;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    z = z ^ (z >> 31);
    const std::uint64_t span = max_interval_ms_ - min_interval_ms_ + 1;
    return min_interval_ms_ + (z % span);
}

bool CoverScheduler::enqueue(const Packet& pkt) {
    if (pkt.payload.size() > kMaxPayloadSize)
        return false;
    if (queue_.size() >= kMaxQueueDepth)
        return false;
    queue_.push_back(pkt);
    return true;
}

std::size_t CoverScheduler::pending() const noexcept {
    return queue_.size();
}

bool CoverScheduler::full() const noexcept {
    return queue_.size() >= kMaxQueueDepth;
}

std::uint64_t CoverScheduler::next_due_ms() const noexcept {
    return next_slot_ms_;
}

std::vector<Frame> CoverScheduler::poll(std::uint64_t now_ms) {
    std::vector<Frame> frames;
    if (now_ms < next_slot_ms_)
        return frames;

    if (!queue_.empty()) {
        auto encoded = encode(queue_.front());
        queue_.pop_front();
        if (encoded) {
            frames.push_back(*encoded);
        } else {
            // Validated at enqueue, so this should not happen. Never leak a
            // malformed/real frame onto the wire -- emit cover instead.
            frames.push_back(make_dummy_frame());
        }
    } else {
        frames.push_back(make_dummy_frame());
    }

    // Reschedule from *now*, never from the missed slot, so a long idle gap
    // cannot accumulate a backlog that later flushes as a burst.
    next_slot_ms_ = now_ms + sample_delay();
    return frames;
}

} // namespace aeromesh
