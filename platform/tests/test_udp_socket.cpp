#include "aeromesh/udp_socket.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

int g_failures = 0;

void check(bool cond, const char* what) {
    if (cond) {
        std::cout << "[ ok ] " << what << "\n";
    } else {
        std::cout << "[FAIL] " << what << "\n";
        ++g_failures;
    }
}

} // namespace

int main() {
    using namespace aeromesh;

    auto a_res = UdpSocket::bind("127.0.0.1", 0);
    check(a_res.has_value(), "bind socket A on loopback");
    auto b_res = UdpSocket::bind("127.0.0.1", 0);
    check(b_res.has_value(), "bind socket B on loopback");

    if (!a_res.has_value() || !b_res.has_value()) {
        std::cout << "[FAIL] cannot continue without two bound sockets\n";
        return EXIT_FAILURE;
    }

    UdpSocket a = std::move(a_res.value());
    UdpSocket b = std::move(b_res.value());

    check(a.valid() && b.valid(), "both sockets valid");
    check(a.local_port() != 0, "socket A resolved an ephemeral port");
    check(b.local_port() != 0, "socket B resolved an ephemeral port");

    const std::string payload = "aeromesh-udp-loopback";
    std::vector<std::byte> bytes;
    bytes.reserve(payload.size());
    for (char c : payload) {
        bytes.push_back(static_cast<std::byte>(c));
    }

    Endpoint dst;
    dst.host = "127.0.0.1";
    dst.port = b.local_port();

    const bool sent = a.send(dst, std::span<const std::byte>(bytes.data(), bytes.size()));
    check(sent, "send datagram A -> B");

    Endpoint from;
    std::vector<std::byte> received;
    bool got = false;
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (b.poll(from, received)) {
            got = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    check(got, "poll received a datagram on B");

    check(received.size() == bytes.size(), "received size matches sent size");

    bool same = received.size() == bytes.size();
    for (std::size_t i = 0; same && i < bytes.size(); ++i) {
        if (received[i] != bytes[i]) {
            same = false;
        }
    }
    check(same, "received bytes match sent bytes");

    check(from.port == a.local_port(), "source port matches sender's local port");

    if (g_failures == 0) {
        std::cout << "All UDP socket tests passed.\n";
        return EXIT_SUCCESS;
    }
    std::cout << g_failures << " UDP socket test(s) failed.\n";
    return EXIT_FAILURE;
}
