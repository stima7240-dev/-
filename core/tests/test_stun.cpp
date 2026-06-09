// Tests for the STUN client (RFC 5389) used in NAT-traversal discovery.
//
// The headline test feeds parse_binding_response a Binding success response
// whose XOR-MAPPED-ADDRESS bytes were computed BY HAND (independently of the
// production XOR logic) for 203.0.113.7:51234, so a bug in the parser cannot be
// masked by a matching bug in an encoder. A fake in-memory socket then drives
// the full make-request -> send -> poll -> parse round trip.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#include "aeromesh/identity.hpp"
#include "aeromesh/stun.hpp"
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

void push(std::vector<std::byte>& m, std::uint8_t x) {
    m.push_back(std::byte{x});
}

// Build a Binding success response carrying an XOR-MAPPED-ADDRESS for the given
// IPv4 octets and port, using the supplied transaction id. The XOR encoding
// here mirrors the wire format; the hand-built test below cross-checks it with
// literal bytes.
std::vector<std::byte> build_xor_response(const aeromesh::StunTxnId& txn,
                                          std::uint8_t a, std::uint8_t b,
                                          std::uint8_t c, std::uint8_t d,
                                          std::uint16_t port) {
    std::vector<std::byte> m;
    push(m, 0x01);
    push(m, 0x01); // Binding success (0x0101)
    push(m, 0x00);
    push(m, 0x0C); // message length = 12
    push(m, 0x21);
    push(m, 0x12);
    push(m, 0xA4);
    push(m, 0x42); // magic cookie
    for (std::size_t i = 0; i < txn.size(); ++i) {
        m.push_back(txn[i]);
    }
    push(m, 0x00);
    push(m, 0x20); // attribute type XOR-MAPPED-ADDRESS
    push(m, 0x00);
    push(m, 0x08); // attribute length = 8
    push(m, 0x00); // reserved
    push(m, 0x01); // family IPv4
    const std::uint16_t xport = static_cast<std::uint16_t>(port ^ 0x2112u);
    push(m, static_cast<std::uint8_t>(xport >> 8));
    push(m, static_cast<std::uint8_t>(xport & 0xFF));
    const std::array<std::uint8_t, 4> cookie = {0x21, 0x12, 0xA4, 0x42};
    const std::array<std::uint8_t, 4> addr = {a, b, c, d};
    for (std::size_t i = 0; i < 4; ++i) {
        push(m, static_cast<std::uint8_t>(addr[i] ^ cookie[i]));
    }
    return m;
}

// Fake datagram socket: captures the request's transaction id and, on the next
// poll, returns a synthesized Binding success response for a fixed reflexive
// address. Models a STUN server without any real networking.
class FakeStunSocket : public aeromesh::IDatagramSocket {
public:
    FakeStunSocket(std::uint8_t a, std::uint8_t b, std::uint8_t c,
                   std::uint8_t d, std::uint16_t port, aeromesh::Endpoint server)
        : a_(a), b_(b), c_(c), d_(d), port_(port), server_(std::move(server)) {}

    bool send(const aeromesh::Endpoint& to,
              std::span<const std::byte> data) override {
        last_dest_ = to;
        ++sends_;
        if (data.size() >= aeromesh::kStunHeaderLen) {
            aeromesh::StunTxnId txn{};
            for (std::size_t i = 0; i < txn.size(); ++i) {
                txn[i] = data[8 + i];
            }
            response_ = build_xor_response(txn, a_, b_, c_, d_, port_);
            has_response_ = true;
        }
        return true;
    }

    bool poll(aeromesh::Endpoint& from, std::vector<std::byte>& out) override {
        if (!has_response_) {
            return false;
        }
        has_response_ = false;
        from = server_;
        out = response_;
        return true;
    }

    int sends() const { return sends_; }
    const aeromesh::Endpoint& last_dest() const { return last_dest_; }

private:
    std::uint8_t a_;
    std::uint8_t b_;
    std::uint8_t c_;
    std::uint8_t d_;
    std::uint16_t port_;
    aeromesh::Endpoint server_;
    aeromesh::Endpoint last_dest_;
    std::vector<std::byte> response_;
    bool has_response_ = false;
    int sends_ = 0;
};

} // namespace

int main() {
    if (!aeromesh::init_crypto()) {
        std::cerr << "init_crypto failed\n";
        return EXIT_FAILURE;
    }

    // A fixed transaction id for the deterministic parse tests.
    aeromesh::StunTxnId txn{};
    for (std::size_t i = 0; i < txn.size(); ++i) {
        txn[i] = std::byte{static_cast<std::uint8_t>(i + 1)};
    }

    // --- Request encoding -----------------------------------------------------
    {
        const aeromesh::StunBindingRequest req =
            aeromesh::make_binding_request(txn);
        check(req.datagram[0] == std::byte{0x00} &&
                  req.datagram[1] == std::byte{0x01},
              "request type is Binding request 0x0001");
        check(req.datagram[2] == std::byte{0x00} &&
                  req.datagram[3] == std::byte{0x00},
              "request message length is 0");
        check(req.datagram[4] == std::byte{0x21} &&
                  req.datagram[5] == std::byte{0x12} &&
                  req.datagram[6] == std::byte{0xA4} &&
                  req.datagram[7] == std::byte{0x42},
              "request carries the magic cookie");
        bool txn_ok = true;
        for (std::size_t i = 0; i < txn.size(); ++i) {
            if (req.datagram[8 + i] != txn[i]) {
                txn_ok = false;
            }
        }
        check(txn_ok, "request embeds the transaction id");
        check(req.txn_id == txn, "request reports its transaction id");
    }

    // --- Hand-built XOR-MAPPED-ADDRESS vector (independent of the encoder) -----
    // Reflexive address 203.0.113.7:51234.
    //   port  0xC822 ^ 0x2112        = 0xE930
    //   addr  CB.00.71.07 ^ 21.12.A4.42 = EA.12.D5.45
    {
        std::vector<std::byte> resp;
        push(resp, 0x01);
        push(resp, 0x01); // Binding success
        push(resp, 0x00);
        push(resp, 0x0C); // length 12
        push(resp, 0x21);
        push(resp, 0x12);
        push(resp, 0xA4);
        push(resp, 0x42); // cookie
        for (std::size_t i = 0; i < txn.size(); ++i) {
            resp.push_back(txn[i]);
        }
        push(resp, 0x00);
        push(resp, 0x20); // XOR-MAPPED-ADDRESS
        push(resp, 0x00);
        push(resp, 0x08); // length 8
        push(resp, 0x00); // reserved
        push(resp, 0x01); // IPv4
        push(resp, 0xE9);
        push(resp, 0x30); // X-Port
        push(resp, 0xEA);
        push(resp, 0x12);
        push(resp, 0xD5);
        push(resp, 0x45); // X-Address

        const auto r = aeromesh::parse_binding_response(resp, txn);
        check(r.has_value(), "hand-built XOR response parses");
        check(r.has_value() && r->host == "203.0.113.7",
              "parsed host == 203.0.113.7");
        check(r.has_value() && r->port == 51234, "parsed port == 51234");
    }

    // --- MAPPED-ADDRESS (non-XOR) fallback ------------------------------------
    {
        std::vector<std::byte> resp;
        push(resp, 0x01);
        push(resp, 0x01);
        push(resp, 0x00);
        push(resp, 0x0C);
        push(resp, 0x21);
        push(resp, 0x12);
        push(resp, 0xA4);
        push(resp, 0x42);
        for (std::size_t i = 0; i < txn.size(); ++i) {
            resp.push_back(txn[i]);
        }
        push(resp, 0x00);
        push(resp, 0x01); // MAPPED-ADDRESS
        push(resp, 0x00);
        push(resp, 0x08);
        push(resp, 0x00);
        push(resp, 0x01); // IPv4
        push(resp, 0x12);
        push(resp, 0x34); // port 0x1234 = 4660
        push(resp, 0xC0);
        push(resp, 0x00);
        push(resp, 0x02);
        push(resp, 0x01); // 192.0.2.1
        const auto r = aeromesh::parse_binding_response(resp, txn);
        check(r.has_value() && r->host == "192.0.2.1" && r->port == 4660,
              "plain MAPPED-ADDRESS parses to 192.0.2.1:4660");
    }

    // --- Negative: wrong transaction id ---------------------------------------
    {
        aeromesh::StunTxnId other = txn;
        other[0] = std::byte{0xFF};
        const std::vector<std::byte> resp =
            build_xor_response(other, 198, 51, 100, 9, 1234);
        const auto r = aeromesh::parse_binding_response(resp, txn);
        check(!r.has_value() &&
                  r.error() == aeromesh::StunError::TransactionMismatch,
              "transaction id mismatch is rejected");
    }

    // --- Negative: not a success response -------------------------------------
    {
        std::vector<std::byte> resp =
            build_xor_response(txn, 198, 51, 100, 9, 1234);
        resp[0] = std::byte{0x00};
        resp[1] = std::byte{0x01}; // turn it into a Binding request type
        const auto r = aeromesh::parse_binding_response(resp, txn);
        check(!r.has_value() &&
                  r.error() == aeromesh::StunError::NotASuccessResponse,
              "non-success message type is rejected");
    }

    // --- Negative: truncated datagram -----------------------------------------
    {
        const std::vector<std::byte> resp(8, std::byte{0x00});
        const auto r = aeromesh::parse_binding_response(resp, txn);
        check(!r.has_value() && r.error() == aeromesh::StunError::Malformed,
              "short datagram is rejected as malformed");
    }

    // --- Full round trip over a fake socket -----------------------------------
    {
        const auto server = aeromesh::Endpoint{"198.51.100.1", 3478};
        FakeStunSocket sock(203, 0, 113, 7, 51234, server);
        const auto r =
            aeromesh::discover_reflexive_address(sock, server, 16);
        check(r.has_value(), "discover_reflexive_address succeeds");
        check(r.has_value() && r->host == "203.0.113.7",
              "discovered host == 203.0.113.7");
        check(r.has_value() && r->port == 51234,
              "discovered port == 51234");
        check(sock.sends() == 1, "exactly one Binding request was sent");
        check(sock.last_dest() == server, "request was sent to the STUN server");
    }

    // --- Timeout when the socket never answers --------------------------------
    {
        struct SilentSocket : aeromesh::IDatagramSocket {
            bool send(const aeromesh::Endpoint&,
                      std::span<const std::byte>) override {
                return true;
            }
            bool poll(aeromesh::Endpoint&, std::vector<std::byte>&) override {
                return false;
            }
        } silent;
        const auto server = aeromesh::Endpoint{"198.51.100.1", 3478};
        const auto r = aeromesh::discover_reflexive_address(silent, server, 4);
        check(!r.has_value() && r.error() == aeromesh::StunError::Timeout,
              "no response yields Timeout");
    }

    if (g_failures != 0) {
        std::cout << g_failures << " check(s) FAILED\n";
        return EXIT_FAILURE;
    }
    std::cout << "all STUN tests passed\n";
    return EXIT_SUCCESS;
}
