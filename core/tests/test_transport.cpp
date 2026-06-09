// Tests for the OS-independent transport router using an in-memory loopback
// socket: constant-rate cover traffic, real-packet delivery with per-type
// dispatch, dummy dropping, unknown-peer rejection, and Endpoint parsing.

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <map>
#include <utility>
#include <vector>

#include "aeromesh/identity.hpp"
#include "aeromesh/packet.hpp"
#include "aeromesh/transport.hpp"

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

// Shared in-memory network: one mailbox (FIFO of <from, bytes>) per endpoint.
struct LoopbackBus {
    std::map<Endpoint, std::deque<std::pair<Endpoint, std::vector<std::byte>>>>
        mailboxes;
};

class LoopbackSocket : public IDatagramSocket {
public:
    LoopbackSocket(LoopbackBus& bus, Endpoint self)
        : bus_(bus), self_(std::move(self)) {}

    bool send(const Endpoint& to, std::span<const std::byte> data) override {
        bus_.mailboxes[to].emplace_back(
            self_, std::vector<std::byte>(data.begin(), data.end()));
        return true;
    }

    bool poll(Endpoint& from, std::vector<std::byte>& out) override {
        auto it = bus_.mailboxes.find(self_);
        if (it == bus_.mailboxes.end() || it->second.empty())
            return false;
        from = it->second.front().first;
        out = std::move(it->second.front().second);
        it->second.pop_front();
        return true;
    }

private:
    LoopbackBus& bus_;
    Endpoint self_;
};

Endpoint ep(const char* host, std::uint16_t port) {
    return Endpoint{host, port};
}

void test_cover_only() {
    std::printf("constant-rate cover traffic\n");
    LoopbackBus bus;
    Endpoint a = ep("10.0.0.1", 9001);
    Endpoint b = ep("10.0.0.2", 9002);
    LoopbackSocket sa(bus, a);
    LoopbackSocket sb(bus, b);
    Transport ta(sa, 100, 100, 1);  // min == max -> fixed 100ms cadence
    ta.add_peer(b, 0);

    // pump() emits at most one frame per call, so drive it once per slot.
    for (std::uint64_t t = 100; t <= 500; t += 100)
        ta.pump(t);  // slots at 100,200,300,400,500 -> 5 frames, all dummy
    check(ta.stats().frames_sent == 5, "5 frames emitted over 5 slots");
    check(ta.stats().dummy_sent == 5, "all 5 are cover frames");
    check(ta.stats().real_sent == 0, "no real frames");

    // A single far-future pump must NOT flush a backlog burst.
    LoopbackBus bus2;
    LoopbackSocket sa2(bus2, a);
    Transport tburst(sa2, 100, 100, 1);
    tburst.add_peer(b, 0);
    tburst.pump(100000);
    check(tburst.stats().frames_sent == 1,
          "no catch-up burst: one frame despite a long idle gap");

    Transport tb(sb, 100, 100, 1);
    bool any = false;
    tb.on(PacketType::Data, [&](const Endpoint&, const Packet&) { any = true; });
    tb.receive();
    check(!any, "no real packet dispatched from cover traffic");
    check(tb.stats().real_received == 0, "receiver counts zero real");
    check(tb.stats().dropped == 5, "receiver dropped all 5 dummies");
}

void test_real_delivery() {
    std::printf("real packet delivery + dispatch\n");
    LoopbackBus bus;
    Endpoint a = ep("10.0.0.1", 9001);
    Endpoint b = ep("10.0.0.2", 9002);
    LoopbackSocket sa(bus, a);
    LoopbackSocket sb(bus, b);
    Transport ta(sa, 100, 100, 1);
    ta.add_peer(b, 0);
    Transport tb(sb, 100, 100, 1);
    tb.add_peer(a, 0);

    Packet p;
    p.type = PacketType::Data;
    const char* msg = "hello-aeromesh";
    for (const char* c = msg; *c; ++c)
        p.payload.push_back(static_cast<std::byte>(*c));

    check(ta.send(b, p), "queue real packet");
    ta.pump(100);  // one slot elapses -> the real packet rides it
    check(ta.stats().real_sent == 1, "one real frame sent");
    check(ta.stats().dummy_sent == 0, "no dummy on that slot");

    std::vector<std::byte> got;
    Endpoint from_seen;
    int hits = 0;
    tb.on(PacketType::Data, [&](const Endpoint& from, const Packet& pkt) {
        ++hits;
        got = pkt.payload;
        from_seen = from;
    });
    tb.receive();
    check(hits == 1, "handler invoked once");
    check(got == p.payload, "payload preserved end to end");
    check(from_seen == a, "source endpoint reported correctly");
    check(tb.stats().real_received == 1, "one real packet received");
}

void test_unknown_peer() {
    std::printf("send to unknown peer rejected\n");
    LoopbackBus bus;
    Endpoint a = ep("10.0.0.1", 9001);
    LoopbackSocket sa(bus, a);
    Transport ta(sa, 100, 100, 1);
    Packet p;
    p.type = PacketType::Ping;
    check(!ta.send(ep("10.0.0.9", 1234), p), "send fails for unknown peer");
    check(!ta.has_peer(ep("10.0.0.9", 1234)), "peer not registered");
}

void test_endpoint_parse() {
    std::printf("endpoint parse / to_string\n");
    auto v4 = Endpoint::parse("1.2.3.4:8080");
    check(v4.has_value() && v4->host == "1.2.3.4" && v4->port == 8080,
          "parse ipv4:port");
    check(v4 && v4->to_string() == "1.2.3.4:8080", "round-trip to_string");
    auto v6 = Endpoint::parse("[::1]:9000");
    check(v6.has_value() && v6->host == "::1" && v6->port == 9000,
          "parse bracketed ipv6");
    check(!Endpoint::parse("nohost").has_value(), "reject missing port");
    check(!Endpoint::parse("host:0").has_value(), "reject port 0");
    check(!Endpoint::parse("host:70000").has_value(), "reject port > 65535");
    check(!Endpoint::parse(":1234").has_value(), "reject empty host");
}

} // namespace

int main() {
    if (!aeromesh::init_crypto()) {
        std::printf("  [FAIL] init_crypto\n");
        return EXIT_FAILURE;
    }
    test_cover_only();
    test_real_delivery();
    test_unknown_peer();
    test_endpoint_parse();
    if (g_failures == 0) {
        std::printf("\nALL TESTS PASSED\n");
        return EXIT_SUCCESS;
    }
    std::printf("\n%d TEST(S) FAILED\n", g_failures);
    return EXIT_FAILURE;
}
