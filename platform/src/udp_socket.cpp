#include "aeromesh/udp_socket.hpp"

#include <cstdlib>
#include <cstring>
#include <utility>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <cerrno>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace aeromesh {

namespace {

#if defined(_WIN32)
using socket_t = SOCKET;
constexpr socket_t kInvalidSocket = INVALID_SOCKET;

// Ensures WSAStartup is called once for the lifetime of the process.
struct WsaGuard {
    bool ok = false;
    WsaGuard() {
        WSADATA data;
        ok = (WSAStartup(MAKEWORD(2, 2), &data) == 0);
    }
    ~WsaGuard() {
        if (ok) {
            WSACleanup();
        }
    }
};

bool ensure_startup() {
    static WsaGuard guard;
    return guard.ok;
}

void close_socket(socket_t s) {
    ::closesocket(s);
}

bool set_non_blocking(socket_t s) {
    u_long mode = 1;
    return ::ioctlsocket(s, FIONBIO, &mode) == 0;
}
#else
using socket_t = int;
constexpr socket_t kInvalidSocket = -1;

bool ensure_startup() {
    return true;
}

void close_socket(socket_t s) {
    ::close(s);
}

bool set_non_blocking(socket_t s) {
    int flags = ::fcntl(s, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    return ::fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
}
#endif

socket_t to_socket(std::intptr_t fd) {
    return static_cast<socket_t>(fd);
}

} // namespace

std::expected<UdpSocket, NetError> UdpSocket::bind(const std::string& bind_host,
                                                   std::uint16_t bind_port) {
    if (!ensure_startup()) {
        return std::unexpected(NetError::StartupFailed);
    }

    addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    hints.ai_flags = AI_PASSIVE | AI_NUMERICSERV;

    const std::string port_str = std::to_string(bind_port);
    const char* node = bind_host.empty() ? nullptr : bind_host.c_str();

    addrinfo* res = nullptr;
    if (::getaddrinfo(node, port_str.c_str(), &hints, &res) != 0 || res == nullptr) {
        return std::unexpected(NetError::ResolveFailed);
    }

    socket_t sock = kInvalidSocket;
    bool bound = false;
    for (addrinfo* it = res; it != nullptr; it = it->ai_next) {
        sock = ::socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (sock == kInvalidSocket) {
            continue;
        }
        if (::bind(sock, it->ai_addr, static_cast<int>(it->ai_addrlen)) == 0) {
            bound = true;
            break;
        }
        close_socket(sock);
        sock = kInvalidSocket;
    }
    ::freeaddrinfo(res);

    if (!bound) {
        if (sock != kInvalidSocket) {
            close_socket(sock);
        }
        return std::unexpected(NetError::BindFailed);
    }

    if (!set_non_blocking(sock)) {
        close_socket(sock);
        return std::unexpected(NetError::SocketCreateFailed);
    }

    // Resolve the actual bound port (covers ephemeral port 0 binds).
    std::uint16_t resolved_port = bind_port;
    sockaddr_storage local;
    std::memset(&local, 0, sizeof(local));
    socklen_t local_len = sizeof(local);
    if (::getsockname(sock, reinterpret_cast<sockaddr*>(&local), &local_len) == 0) {
        if (local.ss_family == AF_INET) {
            resolved_port = ntohs(reinterpret_cast<sockaddr_in*>(&local)->sin_port);
        } else if (local.ss_family == AF_INET6) {
            resolved_port = ntohs(reinterpret_cast<sockaddr_in6*>(&local)->sin6_port);
        }
    }

    UdpSocket out;
    out.fd_ = static_cast<std::intptr_t>(sock);
    out.local_port_ = resolved_port;
    return out;
}

UdpSocket::UdpSocket(UdpSocket&& other) noexcept
    : fd_(other.fd_), local_port_(other.local_port_) {
    other.fd_ = -1;
    other.local_port_ = 0;
}

UdpSocket& UdpSocket::operator=(UdpSocket&& other) noexcept {
    if (this != &other) {
        close_fd();
        fd_ = other.fd_;
        local_port_ = other.local_port_;
        other.fd_ = -1;
        other.local_port_ = 0;
    }
    return *this;
}

UdpSocket::~UdpSocket() {
    close_fd();
}

void UdpSocket::close_fd() {
    if (fd_ != -1) {
        close_socket(to_socket(fd_));
        fd_ = -1;
    }
}

std::uint16_t UdpSocket::local_port() const {
    return local_port_;
}

bool UdpSocket::valid() const {
    return fd_ != -1;
}

bool UdpSocket::send(const Endpoint& to, std::span<const std::byte> data) {
    if (fd_ == -1) {
        return false;
    }

    addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    hints.ai_flags = AI_NUMERICSERV;

    const std::string port_str = std::to_string(to.port);

    addrinfo* res = nullptr;
    if (::getaddrinfo(to.host.c_str(), port_str.c_str(), &hints, &res) != 0 || res == nullptr) {
        return false;
    }

    bool sent = false;
    for (addrinfo* it = res; it != nullptr; it = it->ai_next) {
        const auto n = ::sendto(to_socket(fd_),
                                reinterpret_cast<const char*>(data.data()),
                                static_cast<int>(data.size()), 0,
                                it->ai_addr, static_cast<int>(it->ai_addrlen));
        if (n == static_cast<decltype(n)>(data.size())) {
            sent = true;
            break;
        }
    }
    ::freeaddrinfo(res);
    return sent;
}

bool UdpSocket::poll(Endpoint& from, std::vector<std::byte>& out) {
    if (fd_ == -1) {
        return false;
    }

    sockaddr_storage src;
    std::memset(&src, 0, sizeof(src));
    socklen_t src_len = sizeof(src);

    std::vector<std::byte> buf(65536);
    const auto n = ::recvfrom(to_socket(fd_),
                              reinterpret_cast<char*>(buf.data()),
                              static_cast<int>(buf.size()), 0,
                              reinterpret_cast<sockaddr*>(&src), &src_len);
    if (n <= 0) {
        return false;
    }

    char host[NI_MAXHOST];
    char serv[NI_MAXSERV];
    if (::getnameinfo(reinterpret_cast<sockaddr*>(&src), src_len,
                      host, sizeof(host), serv, sizeof(serv),
                      NI_NUMERICHOST | NI_NUMERICSERV) != 0) {
        return false;
    }

    from.host = host;
    from.port = static_cast<std::uint16_t>(std::strtoul(serv, nullptr, 10));

    out.assign(buf.begin(), buf.begin() + n);
    return true;
}

} // namespace aeromesh
