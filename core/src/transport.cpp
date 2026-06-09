#include "aeromesh/transport.hpp"

#include <charconv>
#include <tuple>
#include <utility>

namespace aeromesh {

std::string Endpoint::to_string() const {
    return host + ":" + std::to_string(port);
}

std::optional<Endpoint> Endpoint::parse(std::string_view s) {
    // Split on the LAST colon so bare IPv4/hostnames work; bracketed IPv6
    // literals ([::1]:9000) are also handled.
    const auto pos = s.rfind(':');
    if (pos == std::string_view::npos || pos == 0 || pos + 1 >= s.size())
        return std::nullopt;

    std::string_view host = s.substr(0, pos);
    std::string_view port_str = s.substr(pos + 1);

    // Strip [] around an IPv6 literal.
    if (host.size() >= 2 && host.front() == '[' && host.back() == ']')
        host = host.substr(1, host.size() - 2);
    if (host.empty())
        return std::nullopt;

    unsigned long value = 0;
    const char* begin = port_str.data();
    const char* end = begin + port_str.size();
    auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc{} || ptr != end || value == 0 || value > 65535)
        return std::nullopt;

    Endpoint ep;
    ep.host = std::string(host);
    ep.port = static_cast<std::uint16_t>(value);
    return ep;
}

Transport::Transport(IDatagramSocket& socket, std::uint64_t min_interval_ms,
                     std::uint64_t max_interval_ms, std::uint64_t seed)
    : socket_(socket),
      min_interval_ms_(min_interval_ms),
      max_interval_ms_(max_interval_ms),
      seed_base_(seed) {
    // CoverScheduler clamps the interval bounds (1 <= min <= max), so we can
    // forward the raw values unchecked.
}

void Transport::add_peer(const Endpoint& peer, std::uint64_t start_ms) {
    if (links_.find(peer) != links_.end())
        return;
    // Derive a distinct per-peer seed from the base seed so that two links
    // never share an identical (correlatable) delay sequence. Unsigned wrap is
    // intentional and well-defined.
    const std::uint64_t peer_seed =
        seed_base_ + 0x9E3779B97F4A7C15ULL * (peer_seq_ + 1);
    ++peer_seq_;
    links_.emplace(std::piecewise_construct,
                   std::forward_as_tuple(peer),
                   std::forward_as_tuple(min_interval_ms_, max_interval_ms_,
                                         start_ms, peer_seed));
}

bool Transport::has_peer(const Endpoint& peer) const {
    return links_.find(peer) != links_.end();
}

std::size_t Transport::pending(const Endpoint& peer) const {
    auto it = links_.find(peer);
    return it == links_.end() ? 0 : it->second.pending();
}

void Transport::on(PacketType type, Handler handler) {
    handlers_[type] = std::move(handler);
}

bool Transport::send(const Endpoint& peer, const Packet& packet) {
    auto it = links_.find(peer);
    if (it == links_.end())
        return false;
    return it->second.enqueue(packet);
}

void Transport::pump(std::uint64_t now_ms) {
    for (auto& kv : links_) {
        const Endpoint& peer = kv.first;
        CoverScheduler& sched = kv.second;

        const std::size_t real_before = sched.pending();
        std::vector<Frame> frames = sched.poll(now_ms);
        if (frames.empty())
            continue;
        const std::size_t real_after = sched.pending();
        // Real packets fill slots FIFO, so this many of the emitted frames are
        // real; the rest are cover.
        std::size_t real_emitted =
            real_before > real_after ? real_before - real_after : 0;
        if (real_emitted > frames.size())
            real_emitted = frames.size();

        for (const Frame& f : frames) {
            socket_.send(peer, std::span<const std::byte>(f.data(), f.size()));
            ++stats_.frames_sent;
        }
        stats_.real_sent += real_emitted;
        stats_.dummy_sent += frames.size() - real_emitted;
    }
}

void Transport::receive() {
    Endpoint from;
    std::vector<std::byte> buf;
    while (socket_.poll(from, buf)) {
        if (buf.size() != kFrameSize) {
            ++stats_.dropped;
            continue;
        }
        std::span<const std::byte, kFrameSize> frame(buf.data(), kFrameSize);
        auto pkt = decode(frame);
        if (!pkt) {
            ++stats_.dropped;
            continue;
        }
        if (pkt->type == PacketType::Dummy) {
            ++stats_.dropped;  // cover traffic: discard silently
            continue;
        }
        ++stats_.real_received;
        auto h = handlers_.find(pkt->type);
        if (h != handlers_.end() && h->second)
            h->second(from, *pkt);
    }
}

} // namespace aeromesh
