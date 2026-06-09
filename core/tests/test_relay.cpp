// Tests for the TURN-like relay fallback (NAT traversal).
//
// Covers the wire codec (with negatives), the server's pairing + forwarding
// logic (including rejection of DATA from a non-member), and a full two-client
// rendezvous over an in-memory network: both clients BIND, then exchange
// opaque payloads in both directions through the relay.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <span>
#include <string>
#include <vector>

#include "aeromesh/identity.hpp"
#include "aeromesh/relay.hpp"
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

std::vector<std::byte> to_bytes(std::string_view s) {
    std::vector<std::byte> v;
    v.reserve(s.size());
    for (const char c : s) {
        v.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
    }
    return v;
}

bool bytes_equal(std::span<const std::byte> a, std::span<const std::byte> b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
}

// In-memory packet network shared by all sockets in a test.
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

    const aeromesh::RelaySessionId session =
        aeromesh::generate_relay_session_id();

    // --- Wire codec round trip ------------------------------------------------
    {
        const auto bind = aeromesh::encode_relay_bind(session);
        const auto bind_ok = aeromesh::encode_relay_bind_ok(session);
        const auto payload = to_bytes("ciphertext-blob");
        const auto data = aeromesh::encode_relay_data(session, payload);

        const auto pb = aeromesh::parse_relay_message(bind);
        const auto po = aeromesh::parse_relay_message(bind_ok);
        const auto pd = aeromesh::parse_relay_message(data);
        check(pb.has_value() && pb->type == aeromesh::RelayMessageType::Bind &&
                  pb->session == session,
              "bind decodes with session");
        check(po.has_value() && po->type == aeromesh::RelayMessageType::BindOk,
              "bind_ok decodes");
        check(pd.has_value() && pd->type == aeromesh::RelayMessageType::Data &&
                  bytes_equal(pd->payload, payload),
              "data decodes with payload intact");
    }

    // --- Codec negatives ------------------------------------------------------
    {
        auto bind = aeromesh::encode_relay_bind(session);
        auto bad_magic = bind;
        bad_magic[0] = std::byte{0x00};
        check(!aeromesh::parse_relay_message(bad_magic).has_value(),
              "bad magic is rejected");
        auto bad_version = bind;
        bad_version[4] = std::byte{0x09};
        check(!aeromesh::parse_relay_message(bad_version).has_value(),
              "bad version is rejected");
        auto bad_type = bind;
        bad_type[5] = std::byte{0x7F};
        check(!aeromesh::parse_relay_message(bad_type).has_value(),
              "unknown type is rejected");
        const std::vector<std::byte> too_short(10, std::byte{0x00});
        check(!aeromesh::parse_relay_message(too_short).has_value(),
              "short packet is rejected");
    }

    const auto relay_ep = aeromesh::Endpoint{"203.0.113.1", 3478};
    const auto ea = aeromesh::Endpoint{"198.51.100.7", 40000};
    const auto eb = aeromesh::Endpoint{"192.0.2.9", 50000};
    const auto ec = aeromesh::Endpoint{"192.0.2.250", 60000};

    // --- Server rejects DATA from a non-member --------------------------------
    {
        FakeNet net;
        NetSocket sr(net, relay_ep);
        aeromesh::RelayServer server(sr);
        // Register A and B directly.
        server.on_datagram(ea, aeromesh::encode_relay_bind(session));
        server.on_datagram(eb, aeromesh::encode_relay_bind(session));
        check(server.session_count() == 1, "one session after two binds");
        const std::uint64_t before = server.stats().forwarded;
        // C is not a member: its DATA must not be forwarded.
        server.on_datagram(ec, aeromesh::encode_relay_data(session,
                                                           to_bytes("x")));
        check(server.stats().forwarded == before,
              "data from a non-member is not forwarded");
        // A member's DATA is forwarded.
        server.on_datagram(ea, aeromesh::encode_relay_data(session,
                                                           to_bytes("y")));
        check(server.stats().forwarded == before + 1,
              "data from a member is forwarded");
    }

    // --- Full two-client rendezvous over the relay ----------------------------
    {
        FakeNet net;
        NetSocket sr(net, relay_ep);
        NetSocket sa(net, ea);
        NetSocket sb(net, eb);
        aeromesh::RelayServer server(sr);
        aeromesh::RelayClient ca(sa, relay_ep, session, 0, 100, 5000);
        aeromesh::RelayClient cb(sb, relay_ep, session, 0, 100, 5000);

        auto drain = [](NetSocket& sock, aeromesh::RelayClient& client) {
            aeromesh::Endpoint from;
            std::vector<std::byte> buf;
            while (sock.poll(from, buf)) {
                client.on_datagram(from, buf);
            }
        };

        // Bind both clients.
        std::uint64_t t = 0;
        for (int i = 0; i < 20; ++i) {
            if (ca.state() == aeromesh::RelayClientState::Bound &&
                cb.state() == aeromesh::RelayClientState::Bound) {
                break;
            }
            ca.tick(t);
            cb.tick(t);
            server.pump();
            drain(sa, ca);
            drain(sb, cb);
            t += 100;
        }
        check(ca.state() == aeromesh::RelayClientState::Bound,
              "client A is bound");
        check(cb.state() == aeromesh::RelayClientState::Bound,
              "client B is bound");

        // A -> B.
        const auto m1 = to_bytes("hello-from-A");
        check(ca.send_payload(m1), "A sends a payload while bound");
        server.pump();
        drain(sb, cb);
        std::vector<std::byte> got_b;
        check(cb.poll_received(got_b) && bytes_equal(got_b, m1),
              "B receives A's payload through the relay");

        // B -> A.
        const auto m2 = to_bytes("reply-from-B");
        check(cb.send_payload(m2), "B sends a payload while bound");
        server.pump();
        drain(sa, ca);
        std::vector<std::byte> got_a;
        check(ca.poll_received(got_a) && bytes_equal(got_a, m2),
              "A receives B's reply through the relay");
    }

    // --- Client refuses to send before it is bound ----------------------------
    {
        FakeNet net;
        NetSocket sa(net, ea);
        aeromesh::RelayClient ca(sa, relay_ep, session, 0, 100, 5000);
        check(!ca.send_payload(to_bytes("too-early")),
              "send_payload fails before Bound");
    }

    // --- Client BIND times out without a server -------------------------------
    {
        FakeNet net;
        NetSocket sa(net, ea);
        aeromesh::RelayClient ca(sa, relay_ep, session, 0, 100, 1000);
        ca.tick(0);
        ca.tick(2000);
        check(ca.state() == aeromesh::RelayClientState::Failed,
              "client fails after the bind timeout");
    }

    if (g_failures != 0) {
        std::cout << g_failures << " check(s) FAILED\n";
        return EXIT_FAILURE;
    }
    std::cout << "all relay tests passed\n";
    return EXIT_SUCCESS;
}
