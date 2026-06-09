#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

#include "aeromesh/transport.hpp"

namespace aeromesh {

enum class NetError {
    StartupFailed,
    SocketCreateFailed,
    BindFailed,
    ResolveFailed,
    InvalidState
};

// A real UDP datagram socket implementing IDatagramSocket over the host OS
// networking stack (Winsock on Windows, BSD sockets on POSIX). Non-blocking.
class UdpSocket : public IDatagramSocket {
public:
    // Bind a UDP socket. Port 0 selects an ephemeral port; host "0.0.0.0"/"::"
    // binds all interfaces. The socket is set to non-blocking mode.
    static std::expected<UdpSocket, NetError> bind(const std::string& bind_host,
                                                   std::uint16_t bind_port);

    UdpSocket(UdpSocket&& other) noexcept;
    UdpSocket& operator=(UdpSocket&& other) noexcept;
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    ~UdpSocket() override;

    // The locally bound port (resolved even when binding to port 0).
    std::uint16_t local_port() const;

    // True when this socket owns a valid underlying handle.
    bool valid() const;

    bool send(const Endpoint& to, std::span<const std::byte> data) override;
    bool poll(Endpoint& from, std::vector<std::byte>& out) override;

private:
    UdpSocket() = default;
    void close_fd();

    // Width-safe handle storage so this header pulls in no platform sockets API.
    // -1 represents an invalid/closed socket on all platforms.
    std::intptr_t fd_ = -1;
    std::uint16_t local_port_ = 0;
};

} // namespace aeromesh
