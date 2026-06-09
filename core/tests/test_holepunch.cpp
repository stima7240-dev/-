// Tests for UDP hole punching (NAT traversal).
//
// Covers the wire codec (with negative cases), the single-peer state machine
// (probe -> ack -> Established, plus timeout -> Failed), and a full two-peer
// rendezvous driven over an in-memory packet network until both sides reach
// Established.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <span>
#include <vector>

#include "aeromesh/holepunch.hpp"
#include "aeromesh/identity.hpp"
#include "aeromesh/transport.hpp"

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) {
        std::cout << "[ ok ] " << what << "\n";
    } else {
        std::cout << "[FAIL] " << what << "\n";
        ++g_failures;
    }
}

// Captures every datagram sent; never delivers anything inbound.
class CaptureSocket : public aeromesh::IDatagramSocket {
public:
    struct Sent {
        aeromesh::Endpoint to;
        std::vector<std::byte> data;
    };

    bool send(const aeromesh::Endpoint& to,
              std::span<const std::byte> data) override {
        Sent s;
        s.to = to;
        s.data.assign(data.begin(), data.end());
        sent.push_back(std::move(s));
        return true;
    }
    bool poll(aeromesh::Endpoint&, std::vector<std::byte>&) override {
        return false;
    }

    std::vector<Sent> sent;
};

// In-memory packet network: routes datagrams between endpoints by mailbox.
struct Datagram {
    aeromesh::Endpoint from;
    std::vector<std::byte> data;
};

class FakeNet {
public:
    void send(const aeromesh::Endpoint& from, const aeromesh::Endpoint& to,
              std::span<const std::byte> data) {
        Datagram dg;
        dg.from = from;
        dg.data.assign(data.begin(), data.end());
        inboxes_[to].push_back(std::move(dg));
    }
    bool poll(const aeromesh::Endpoint& self, aeromesh::Endpoint& from,
              std::vector<std::byte>& out) {
        auto it = inboxes_.find(self);
        if (it == inboxes_.end() || it->second.empty()) {
            return false;
        }
        Datagram dg = std::move(it->second.front());
        it->second.erase(it->second.begin());
        from = dg.from;
        out = dg.data;
        return true;
    }

private:
    std::map<aeromesh::Endpoint, std::vector<Datagram>> inboxes_;
};

class NetSocket : public aeromesh::IDatagramSocket {
public:
    NetSocket(FakeNet& net, aeromesh::Endpoint self)
        : net_(net), self_(std::move(self)) {}
    bool send(const aeromesh::Endpoint& to,
              std::span<const std::byte> data) override {
        net_.send(self_, to, data);
        return true;
    }
    bool poll(aeromesh::Endpoint& from, std::vector<std::byte>& out) override {
        return net_.poll(self_, from, out);
    }

private:
    FakeNet& net_;
    aeromesh::Endpoint self_;
};

} // namespace

int main() {
    if (!aeromesh::init_crypto()) {
        std::cerr << "init_crypto failed\n";
        return EXIT_FAILURE;
    }

    const aeromesh::PunchToken token = aeromesh::generate_punch_token();

    // --- Wire codec round trip ------------------------------------------------
    {
        const auto probe = aeromesh::encode_punch(aeromesh::PunchType::Probe, token);
        const auto ack = aeromesh::encode_punch(aeromesh::PunchType::Ack, token);
        check(probe.size() == aeromesh::kPunchPacketLen,
              "probe packet is the expected length");
        const auto pp = aeromesh::parse_punch(probe, token);
        const auto pa = aeromesh::parse_punch(ack, token);
        check(pp.has_value() && pp->type == aeromesh::PunchType::Probe,
              "probe decodes as Probe");
        check(pa.has_value() && pa->type == aeromesh::PunchType::Ack,
              "ack decodes as Ack");
    }

    // --- Codec negatives ------------------------------------------------------
    {
        aeromesh::PunchToken other = token;
        other[0] = static_cast<std::byte>(
            std::to_integer<std::uint8_t>(other[0]) ^ 0xFFu);
        const auto probe = aeromesh::encode_punch(aeromesh::PunchType::Probe, token);
        check(!aeromesh::parse_punch(probe, other).has_value(),
              "wrong token is rejected");

        auto bad_magic = probe;
        bad_magic[0] = std::byte{0x00};
        check(!aeromesh::parse_punch(bad_magic, token).has_value(),
              "bad magic is rejected");

        auto bad_version = probe;
        bad_version[4] = std::byte{0x09};
        check(!aeromesh::parse_punch(bad_version, token).has_value(),
              "bad version is rejected");

        auto bad_type = probe;
        bad_type[5] = std::byte{0x09};
        check(!aeromesh::parse_punch(bad_type, token).has_value(),
              "unknown type is rejected");

        const std::vector<std::byte> too_short(4, std::byte{0x00});
        check(!aeromesh::parse_punch(too_short, token).has_value(),
              "short packet is rejected");
    }

    const auto ea = aeromesh::Endpoint{"203.0.113.7", 40000};
    const auto eb = aeromesh::Endpoint{"198.51.100.9", 50000};

    // --- Single-peer state machine --------------------------------------------
    {
        CaptureSocket cap;
        aeromesh::PunchConfig cfg;
        cfg.peer = eb;
        cfg.token = token;
        cfg.interval_ms = 100;
        cfg.timeout_ms = 5000;
        aeromesh::HolePuncher hp(cap, cfg, 0);

        hp.tick(0);
        check(cap.sent.size() == 1, "first tick sends one probe");
        check(!cap.sent.empty() && cap.sent[0].to == eb,
              "probe is sent to the peer");
        const auto first = aeromesh::parse_punch(cap.sent[0].data, token);
        check(first.has_value() && first->type == aeromesh::PunchType::Probe,
              "sent packet is a Probe");

        // Receiving the peer's probe should trigger an Ack but not establish.
        const auto peer_probe =
            aeromesh::encode_punch(aeromesh::PunchType::Probe, token);
        const bool consumed = hp.on_datagram(eb, peer_probe);
        check(consumed, "peer probe is consumed");
        check(hp.state() == aeromesh::PunchState::Punching,
              "still punching after only a probe");
        check(hp.acks_sent() == 1, "an ack was sent in response to the probe");
        const auto last = cap.sent.back();
        const auto last_pkt = aeromesh::parse_punch(last.data, token);
        check(last_pkt.has_value() && last_pkt->type == aeromesh::PunchType::Ack,
              "the response packet is an Ack");

        // Receiving the peer's ack establishes the session.
        const auto peer_ack =
            aeromesh::encode_punch(aeromesh::PunchType::Ack, token);
        hp.on_datagram(eb, peer_ack);
        check(hp.state() == aeromesh::PunchState::Established,
              "ack establishes the session");
        check(hp.confirmed_peer() == eb, "confirmed peer is the sender");
    }

    // --- Timeout --------------------------------------------------------------
    {
        CaptureSocket cap;
        aeromesh::PunchConfig cfg;
        cfg.peer = eb;
        cfg.token = token;
        cfg.interval_ms = 100;
        cfg.timeout_ms = 1000;
        aeromesh::HolePuncher hp(cap, cfg, 0);
        hp.tick(0);
        hp.tick(2000);
        check(hp.state() == aeromesh::PunchState::Failed,
              "puncher fails after the timeout");
    }

    // --- Full two-peer rendezvous over an in-memory network -------------------
    {
        FakeNet net;
        NetSocket sa(net, ea);
        NetSocket sb(net, eb);

        aeromesh::PunchConfig ca;
        ca.peer = eb;
        ca.token = token;
        ca.interval_ms = 100;
        ca.timeout_ms = 5000;
        aeromesh::PunchConfig cb;
        cb.peer = ea;
        cb.token = token;
        cb.interval_ms = 100;
        cb.timeout_ms = 5000;

        aeromesh::HolePuncher hpa(sa, ca, 0);
        aeromesh::HolePuncher hpb(sb, cb, 0);

        auto drain = [](NetSocket& sock, aeromesh::HolePuncher& hp) {
            aeromesh::Endpoint from;
            std::vector<std::byte> buf;
            while (sock.poll(from, buf)) {
                hp.on_datagram(from, buf);
            }
        };

        std::uint64_t t = 0;
        for (int i = 0; i < 50; ++i) {
            if (hpa.state() == aeromesh::PunchState::Established &&
                hpb.state() == aeromesh::PunchState::Established) {
                break;
            }
            hpa.tick(t);
            hpb.tick(t);
            drain(sa, hpa);
            drain(sb, hpb);
            t += 100;
        }

        check(hpa.state() == aeromesh::PunchState::Established,
              "peer A reaches Established");
        check(hpb.state() == aeromesh::PunchState::Established,
              "peer B reaches Established");
        check(hpa.confirmed_peer() == eb, "A confirmed peer is B");
        check(hpb.confirmed_peer() == ea, "B confirmed peer is A");
    }

    if (g_failures != 0) {
        std::cout << g_failures << " check(s) FAILED\n";
        return EXIT_FAILURE;
    }
    std::cout << "all hole-punch tests passed\n";
    return EXIT_SUCCESS;
}
