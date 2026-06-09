#include "aeromesh/holepunch.hpp"

#include <utility>

#include <sodium.h>

namespace aeromesh {

namespace {

constexpr std::array<std::byte, 4> kPunchMagic = {
    std::byte{0x41}, std::byte{0x4D}, std::byte{0x50}, std::byte{0x4E}}; // AMPN
constexpr std::byte kPunchVersion{0x01};
constexpr std::byte kWireProbe{0x01};
constexpr std::byte kWireAck{0x02};

} // namespace

PunchToken generate_punch_token() {
    PunchToken t{};
    randombytes_buf(t.data(), t.size());
    return t;
}

std::array<std::byte, kPunchPacketLen> encode_punch(PunchType type,
                                                    const PunchToken& token) {
    std::array<std::byte, kPunchPacketLen> p{};
    p[0] = kPunchMagic[0];
    p[1] = kPunchMagic[1];
    p[2] = kPunchMagic[2];
    p[3] = kPunchMagic[3];
    p[4] = kPunchVersion;
    p[5] = (type == PunchType::Probe) ? kWireProbe : kWireAck;
    for (std::size_t i = 0; i < kPunchTokenLen; ++i) {
        p[6 + i] = token[i];
    }
    return p;
}

std::optional<PunchPacket> parse_punch(std::span<const std::byte> data,
                                       const PunchToken& expected) {
    if (data.size() < kPunchPacketLen) {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < kPunchMagic.size(); ++i) {
        if (data[i] != kPunchMagic[i]) {
            return std::nullopt;
        }
    }
    if (data[4] != kPunchVersion) {
        return std::nullopt;
    }
    PunchType type;
    if (data[5] == kWireProbe) {
        type = PunchType::Probe;
    } else if (data[5] == kWireAck) {
        type = PunchType::Ack;
    } else {
        return std::nullopt;
    }
    // Constant-time token comparison to avoid leaking via timing.
    if (sodium_memcmp(data.data() + 6, expected.data(), kPunchTokenLen) != 0) {
        return std::nullopt;
    }
    return PunchPacket{type};
}

HolePuncher::HolePuncher(IDatagramSocket& socket, PunchConfig config,
                         std::uint64_t now_ms)
    : socket_(socket),
      config_(std::move(config)),
      start_ms_(now_ms),
      confirmed_peer_(config_.peer) {}

void HolePuncher::send_probe(std::uint64_t now_ms) {
    const auto pkt = encode_punch(PunchType::Probe, config_.token);
    socket_.send(config_.peer, pkt);
    last_send_ms_ = now_ms;
    first_probe_sent_ = true;
    ++probes_sent_;
}

void HolePuncher::tick(std::uint64_t now_ms) {
    if (state_ != PunchState::Punching) {
        return;
    }
    if (now_ms - start_ms_ >= config_.timeout_ms) {
        state_ = PunchState::Failed;
        return;
    }
    if (!first_probe_sent_ || (now_ms - last_send_ms_) >= config_.interval_ms) {
        send_probe(now_ms);
    }
}

bool HolePuncher::on_datagram(const Endpoint& from,
                              std::span<const std::byte> data) {
    const auto parsed = parse_punch(data, config_.token);
    if (!parsed) {
        return false;
    }
    // Record the path we actually heard the peer on.
    confirmed_peer_ = from;
    if (state_ == PunchState::Punching) {
        if (parsed->type == PunchType::Probe) {
            // Peer reached us; acknowledge on the path we actually saw so the
            // peer learns its probe to us succeeded.
            const auto ack = encode_punch(PunchType::Ack, config_.token);
            socket_.send(from, ack);
            ++acks_sent_;
        } else {
            // Ack: the peer received our probe, so both directions now work.
            state_ = PunchState::Established;
        }
    }
    return true;
}

} // namespace aeromesh
