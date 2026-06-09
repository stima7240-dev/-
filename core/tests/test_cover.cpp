// Tests for the adaptive (randomized-rate) cover-traffic scheduler:
//   * setting min == max gives an exact, deterministic fixed cadence;
//   * with min < max, delays are randomized but stay within [min, max];
//   * poll() emits at most one frame and never replays missed slots, so an
//     idle gap cannot produce a catch-up burst;
//   * real packets ride slots FIFO, idle slots carry dummies;
//   * the queue is hard-capped at kMaxQueueDepth;
//   * the same seed yields the same delay sequence (reproducible timing).

#include <cstdio>
#include <cstdlib>
#include <span>
#include <string>
#include <vector>

#include "aeromesh/cover.hpp"
#include "aeromesh/identity.hpp"
#include "aeromesh/packet.hpp"

namespace {

int g_failures = 0;

void check(bool cond, const char* what) {
    if (!cond) {
        std::printf("  [FAIL] %s\n", what);
        ++g_failures;
    } else {
        std::printf("  [ ok ] %s\n", what);
    }
}

using namespace aeromesh;

std::vector<std::byte> bytes_of(const std::string& s) {
    std::vector<std::byte> v(s.size());
    for (std::size_t i = 0; i < s.size(); ++i)
        v[i] = static_cast<std::byte>(static_cast<unsigned char>(s[i]));
    return v;
}

Packet data_packet(const std::string& s) {
    Packet p;
    p.type = PacketType::Data;
    p.payload = bytes_of(s);
    return p;
}

std::expected<Packet, FrameError> decode_frame(const Frame& f) {
    return decode(std::span<const std::byte, kFrameSize>(f));
}

bool is_dummy(const Frame& f) {
    auto p = decode_frame(f);
    return p && p->type == PacketType::Dummy;
}

// With min == max the sampled delay is always exactly that value, so the
// scheduler behaves as a deterministic fixed-cadence emitter.
void test_fixed_cadence_when_min_equals_max() {
    std::printf("fixed cadence when min == max\n");
    CoverScheduler sched(100, 100, 0, 1);
    check(sched.next_due_ms() == 100, "first slot at start + interval");
    check(sched.poll(50).empty(), "no frame before the first slot");

    auto a = sched.poll(100);
    check(a.size() == 1, "exactly one frame at the due slot");
    check(sched.next_due_ms() == 200, "next slot rescheduled to +interval");
    check(a.size() == 1 && is_dummy(a[0]), "idle slot carries a dummy");

    check(sched.poll(150).empty(), "no frame between slots");
    auto b = sched.poll(200);
    check(b.size() == 1, "one frame at the second slot");
    check(sched.next_due_ms() == 300, "rescheduled again to +interval");
}

// poll() emits at most one frame per call regardless of how far now_ms has
// jumped: an idle gap must not flush a backlog burst.
void test_no_catch_up_burst() {
    std::printf("no catch-up burst after a long idle gap\n");
    CoverScheduler sched(100, 100, 0, 1);
    auto frames = sched.poll(100000);  // jump far past many missed slots
    check(frames.size() == 1, "only one frame emitted, not a backlog burst");
    check(sched.next_due_ms() == 100100,
          "next slot rescheduled relative to now, not the missed slots");
}

// Delays must be randomized (not all identical) and always within [min, max].
void test_randomized_within_bounds() {
    std::printf("randomized delays stay within [min, max]\n");
    const std::uint64_t lo = 50;
    const std::uint64_t hi = 150;
    CoverScheduler sched(lo, hi, 0, 0xABCDEF);

    bool all_in_range = true;
    bool any_different = false;
    std::uint64_t prev_delay = 0;
    std::uint64_t now = 0;
    for (int i = 0; i < 200; ++i) {
        now = sched.next_due_ms();
        const std::uint64_t before = sched.next_due_ms();
        auto f = sched.poll(now);
        if (f.size() != 1) all_in_range = false;
        const std::uint64_t delay = sched.next_due_ms() - before;
        if (delay < lo || delay > hi) all_in_range = false;
        if (i > 0 && delay != prev_delay) any_different = true;
        prev_delay = delay;
    }
    check(all_in_range, "every sampled delay is within [50, 150]");
    check(any_different, "delays vary (timing is actually randomized)");
}

void test_real_packets_fill_slots() {
    std::printf("real packets ride scheduled slots (FIFO)\n");
    CoverScheduler sched(100, 100, 0, 1);
    check(sched.enqueue(data_packet("first")), "enqueue first");
    check(sched.enqueue(data_packet("second")), "enqueue second");
    check(sched.pending() == 2, "two packets pending");

    auto s1 = sched.poll(100);
    auto s2 = sched.poll(200);
    auto s3 = sched.poll(300);
    check(s1.size() == 1 && s2.size() == 1 && s3.size() == 1,
          "one frame per due slot");

    auto p0 = decode_frame(s1[0]);
    auto p1 = decode_frame(s2[0]);
    auto p2 = decode_frame(s3[0]);
    check(p0 && p0->type == PacketType::Data && p0->payload == bytes_of("first"),
          "slot 1 carries 'first' (FIFO)");
    check(p1 && p1->type == PacketType::Data &&
              p1->payload == bytes_of("second"),
          "slot 2 carries 'second'");
    check(p2 && p2->type == PacketType::Dummy,
          "slot 3 is a dummy (queue drained)");
    check(sched.pending() == 0, "queue fully drained");
}

// Per poll, exactly one frame is emitted whether the queue is empty or full, so
// instantaneous volume never leaks whether the user is sending.
void test_per_slot_volume_independent_of_load() {
    std::printf("per-slot volume independent of load\n");
    CoverScheduler idle(100, 100, 0, 1);
    CoverScheduler busy(100, 100, 0, 1);
    busy.enqueue(data_packet("x"));
    busy.enqueue(data_packet("y"));
    auto fi = idle.poll(100);
    auto fb = busy.poll(100);
    check(fi.size() == fb.size() && fi.size() == 1,
          "same frame count whether idle or busy (no volume leak)");
}

void test_oversize_rejected() {
    std::printf("oversize payload rejected\n");
    CoverScheduler sched(100, 100, 0, 1);
    Packet big;
    big.type = PacketType::Data;
    big.payload.assign(kMaxPayloadSize + 1, std::byte{0});
    check(!sched.enqueue(big), "payload over kMaxPayloadSize rejected");
    check(sched.pending() == 0, "rejected packet is not queued");
}

void test_queue_is_bounded() {
    std::printf("queue is hard-capped at kMaxQueueDepth\n");
    CoverScheduler sched(100, 100, 0, 1);
    for (std::size_t i = 0; i < CoverScheduler::kMaxQueueDepth; ++i) {
        if (!sched.enqueue(data_packet("m"))) {
            check(false, "enqueue should succeed below the cap");
            break;
        }
    }
    check(sched.pending() == CoverScheduler::kMaxQueueDepth,
          "queue filled exactly to the cap");
    check(sched.full(), "scheduler reports full at the cap");
    check(!sched.enqueue(data_packet("overflow")),
          "enqueue past the cap is refused");
    check(sched.pending() == CoverScheduler::kMaxQueueDepth,
          "refused packet did not grow the queue");
}

void test_deterministic_seed() {
    std::printf("same seed yields the same delay sequence\n");
    CoverScheduler a(20, 200, 0, 12345);
    CoverScheduler b(20, 200, 0, 12345);
    CoverScheduler c(20, 200, 0, 99999);
    bool same = true;
    bool c_differs = false;
    std::uint64_t ta = 0, tb = 0, tc = 0;
    for (int i = 0; i < 64; ++i) {
        ta = a.next_due_ms();
        tb = b.next_due_ms();
        tc = c.next_due_ms();
        a.poll(ta);
        b.poll(tb);
        c.poll(tc);
        if (a.next_due_ms() != b.next_due_ms()) same = false;
        if (a.next_due_ms() != c.next_due_ms()) c_differs = true;
    }
    check(same, "identical seeds produce identical timing");
    check(c_differs, "a different seed produces a different sequence");
}

} // namespace

int main() {
    if (!aeromesh::init_crypto()) {
        std::printf("  [FAIL] libsodium init\n");
        return EXIT_FAILURE;
    }
    test_fixed_cadence_when_min_equals_max();
    test_no_catch_up_burst();
    test_randomized_within_bounds();
    test_real_packets_fill_slots();
    test_per_slot_volume_independent_of_load();
    test_oversize_rejected();
    test_queue_is_bounded();
    test_deterministic_seed();
    if (g_failures == 0) {
        std::printf("\nALL TESTS PASSED\n");
        return EXIT_SUCCESS;
    }
    std::printf("\n%d TEST(S) FAILED\n", g_failures);
    return EXIT_FAILURE;
}
