#include "aeromesh/relay.hpp"

#include <utility>

#include <sodium.h>

namespace aeromesh {

namespace {

constexpr std::array<std::byte, 4> kRelayMagic = {
    std::byte{0x41}, std::byte{0x4D}, std::byte{0x52}, std::byte{0x4C}}; // AMRL
constexpr std::byte kRelayVersion{0x01};
constexpr std::byte kWireBind{0x01};
constexpr std::byte kWireBindOk{0x02};
constexpr std::byte kWireData{0x03};

std::vector<std::byte> encode_header(std::byte type,
                                     const RelaySessionId& session) {
    std::vector<std::byte> m;
    m.reserve(kRelayHeaderLen + kRelaySessionIdLen);
    m.push_back(kRelayMagic[0]);
    m.push_back(kRelayMagic[1]);
    m.push_back(kRelayMagic[2]);
    m.push_back(kRelayMagic[3]);
    m.push_back(kRelayVersion);
    m.push_back(type);
    for (std::size_t i = 0; i < kRelaySessionIdLen; ++i) {
        m.push_back(session[i]);
    }
    return m;
}

} // namespace

RelaySessionId generate_relay_session_id() {
    RelaySessionId s{};
    randombytes_buf(s.data(), s.size());
    return s;
}

std::vector<std::byte> encode_relay_bind(const RelaySessionId& session) {
    return encode_header(kWireBind, session);
}

std::vector<std::byte> encode_relay_bind_ok(const RelaySessionId& session) {
    return encode_header(kWireBindOk, session);
}

std::vector<std::byte> encode_relay_data(const RelaySessionId& session,
                                         std::span<const std::byte> payload) {
    std::vector<std::byte> m = encode_header(kWireData, session);
    m.insert(m.end(), payload.begin(), payload.end());
    return m;
}

std::optional<RelayMessage> parse_relay_message(
    std::span<const std::byte> data) {
    if (data.size() < kRelayHeaderLen + kRelaySessionIdLen) {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < kRelayMagic.size(); ++i) {
        if (data[i] != kRelayMagic[i]) {
            return std::nullopt;
        }
    }
    if (data[4] != kRelayVersion) {
        return std::nullopt;
    }
    RelayMessage msg;
    if (data[5] == kWireBind) {
        msg.type = RelayMessageType::Bind;
    } else if (data[5] == kWireBindOk) {
        msg.type = RelayMessageType::BindOk;
    } else if (data[5] == kWireData) {
        msg.type = RelayMessageType::Data;
    } else {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < kRelaySessionIdLen; ++i) {
        msg.session[i] = data[kRelayHeaderLen + i];
    }
    if (msg.type == RelayMessageType::Data) {
        const std::size_t off = kRelayHeaderLen + kRelaySessionIdLen;
        msg.payload.assign(data.begin() + static_cast<std::ptrdiff_t>(off),
                           data.end());
    }
    return msg;
}

// --- Relay server ------------------------------------------------------------

RelayServer::RelayServer(IDatagramSocket& socket) : socket_(socket) {}

bool RelayServer::on_datagram(const Endpoint& from,
                              std::span<const std::byte> data) {
    const auto parsed = parse_relay_message(data);
    if (!parsed) {
        ++stats_.dropped;
        return false;
    }

    if (parsed->type == RelayMessageType::Bind) {
        Allocation& alloc = allocations_[parsed->session];
        bool member = false;
        for (std::size_t i = 0; i < alloc.count; ++i) {
            if (alloc.peers[i] == from) {
                member = true;
                break;
            }
        }
        if (!member) {
            if (alloc.count >= alloc.peers.size()) {
                // Session already has two peers; reject extra binder.
                ++stats_.dropped;
                return true;
            }
            alloc.peers[alloc.count] = from;
            ++alloc.count;
        }
        ++stats_.binds;
        const auto reply = encode_relay_bind_ok(parsed->session);
        socket_.send(from, reply);
        return true;
    }

    if (parsed->type == RelayMessageType::Data) {
        auto it = allocations_.find(parsed->session);
        if (it == allocations_.end() || it->second.count < 2) {
            ++stats_.dropped;
            return true;
        }
        const Allocation& alloc = it->second;
        const Endpoint* other = nullptr;
        bool sender_is_member = false;
        for (std::size_t i = 0; i < alloc.count; ++i) {
            if (alloc.peers[i] == from) {
                sender_is_member = true;
            } else {
                other = &alloc.peers[i];
            }
        }
        if (!sender_is_member || other == nullptr) {
            ++stats_.dropped;
            return true;
        }
        // Forward the original DATA packet unchanged to the paired peer.
        socket_.send(*other, data);
        ++stats_.forwarded;
        return true;
    }

    // BindOk or anything else is not expected at the server.
    ++stats_.dropped;
    return true;
}

void RelayServer::pump() {
    Endpoint from;
    std::vector<std::byte> buf;
    while (socket_.poll(from, buf)) {
        on_datagram(from, buf);
    }
}

// --- Relay client ------------------------------------------------------------

RelayClient::RelayClient(IDatagramSocket& socket, Endpoint relay,
                         RelaySessionId session, std::uint64_t now_ms,
                         std::uint64_t retry_ms, std::uint64_t timeout_ms)
    : socket_(socket),
      relay_(std::move(relay)),
      session_(session),
      start_ms_(now_ms),
      retry_ms_(retry_ms),
      timeout_ms_(timeout_ms) {}

void RelayClient::send_bind(std::uint64_t now_ms) {
    const auto pkt = encode_relay_bind(session_);
    socket_.send(relay_, pkt);
    last_bind_ms_ = now_ms;
    first_bind_sent_ = true;
}

void RelayClient::tick(std::uint64_t now_ms) {
    if (state_ != RelayClientState::Binding) {
        return;
    }
    if (now_ms - start_ms_ >= timeout_ms_) {
        state_ = RelayClientState::Failed;
        return;
    }
    if (!first_bind_sent_ || (now_ms - last_bind_ms_) >= retry_ms_) {
        send_bind(now_ms);
    }
}

bool RelayClient::on_datagram(const Endpoint& from,
                              std::span<const std::byte> data) {
    if (!(from == relay_)) {
        return false;
    }
    const auto parsed = parse_relay_message(data);
    if (!parsed) {
        return false;
    }
    if (parsed->session != session_) {
        return false;
    }
    if (parsed->type == RelayMessageType::BindOk) {
        if (state_ == RelayClientState::Binding) {
            state_ = RelayClientState::Bound;
        }
        return true;
    }
    if (parsed->type == RelayMessageType::Data) {
        received_.push_back(parsed->payload);
        return true;
    }
    return false;
}

bool RelayClient::send_payload(std::span<const std::byte> payload) {
    if (state_ != RelayClientState::Bound) {
        return false;
    }
    const auto pkt = encode_relay_data(session_, payload);
    socket_.send(relay_, pkt);
    return true;
}

bool RelayClient::poll_received(std::vector<std::byte>& out) {
    if (received_.empty()) {
        return false;
    }
    out = std::move(received_.front());
    received_.erase(received_.begin());
    return true;
}

} // namespace aeromesh
