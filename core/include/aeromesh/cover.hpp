#pragma once

// Adaptive (randomized-rate) cover-traffic scheduler for traffic-analysis
// resistance.
//
// Onion routing hides *who talks to whom*, but a global passive observer can
// still try to correlate senders and receivers by the *timing* and *volume* of
// their traffic. This scheduler fights that signal by interleaving real and
// dummy frames on a stream whose timing carries no information about real
// activity.
//
// Design (inspired by Tor's WTF-PAD / circuit-padding work):
//   * Randomized intervals. Each slot's delay is sampled uniformly from
//     [min_interval_ms, max_interval_ms] instead of a fixed cadence. A fixed
//     period is itself a fingerprint; jittered timing is much harder to match.
//     Setting min == max recovers an exact fixed cadence (useful for tests).
//   * No catch-up burst. poll() emits AT MOST ONE frame and always reschedules
//     the next slot relative to *now*. An idle gap therefore can never build up
//     a backlog that later flushes as a tell-tale burst.
//   * Bounded queue. Real outbound packets wait in a queue capped at
//     kMaxQueueDepth; past that, enqueue() refuses new packets rather than
//     growing without bound (back-pressure and basic anti-DoS).
//
// Real packets ride scheduled slots in FIFO order; every slot with nothing real
// to send carries an indistinguishable dummy frame.
//
// The scheduler is clock-agnostic: callers drive it with explicit millisecond
// timestamps, which keeps it deterministic, testable, and free of any OS/timer
// dependency (the core stays platform-independent). The delay sequence is
// seeded explicitly: inject a CSPRNG-drawn seed in production and a fixed seed
// in tests for reproducible timing.

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

#include "aeromesh/packet.hpp"

namespace aeromesh {

class CoverScheduler {
public:
    // Hard cap on queued real packets. Beyond this, enqueue() refuses new
    // packets instead of letting the queue grow without bound.
    static constexpr std::size_t kMaxQueueDepth = 1024;

    // Each slot delay is sampled uniformly from [min_interval_ms,
    // max_interval_ms]. Values are clamped so that 1 <= min <= max. `seed`
    // selects the deterministic delay sequence. The first slot fires at
    // start_ms plus the first sampled delay.
    CoverScheduler(std::uint64_t min_interval_ms,
                   std::uint64_t max_interval_ms,
                   std::uint64_t start_ms,
                   std::uint64_t seed);

    // Queue a real packet for the next slot. Returns false (and queues nothing)
    // if the payload cannot fit a single frame, or if the queue is already at
    // kMaxQueueDepth.
    bool enqueue(const Packet& pkt);

    // Real packets queued but not yet emitted.
    std::size_t pending() const noexcept;

    // True once the queue has reached kMaxQueueDepth.
    bool full() const noexcept;

    // Absolute time of the next scheduled emission.
    std::uint64_t next_due_ms() const noexcept;

    // Advance to now_ms. Emits AT MOST ONE frame: if the current slot is due
    // (now_ms >= next_due_ms()), emit one frame -- the queued real packet if
    // any, otherwise a dummy -- and reschedule the next slot at now_ms plus a
    // freshly sampled delay. Returns an empty vector if the slot is not yet
    // due. Because missed slots are never replayed, an idle gap cannot produce
    // a catch-up burst.
    std::vector<Frame> poll(std::uint64_t now_ms);

private:
    std::uint64_t sample_delay() noexcept;

    std::uint64_t min_interval_ms_;
    std::uint64_t max_interval_ms_;
    std::uint64_t next_slot_ms_;
    std::uint64_t rng_state_;
    std::deque<Packet> queue_;
};

} // namespace aeromesh
