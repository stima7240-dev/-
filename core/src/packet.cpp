#include "aeromesh/packet.hpp"

#include <cstring>

#include <sodium.h>

namespace aeromesh {

bool is_known_type(std::uint8_t raw) noexcept {
    switch (static_cast<PacketType>(raw)) {
        case PacketType::Hello:
        case PacketType::Data:
        case PacketType::Ack:
        case PacketType::FileChunk:
        case PacketType::DhtQuery:
        case PacketType::DhtReply:
        case PacketType::Ping:
        case PacketType::Pong:
        case PacketType::Dummy:
            return true;
    }
    return false;
}

std::string_view to_string(PacketType t) noexcept {
    switch (t) {
        case PacketType::Hello:     return "Hello";
        case PacketType::Data:      return "Data";
        case PacketType::Ack:       return "Ack";
        case PacketType::FileChunk: return "FileChunk";
        case PacketType::DhtQuery:  return "DhtQuery";
        case PacketType::DhtReply:  return "DhtReply";
        case PacketType::Ping:      return "Ping";
        case PacketType::Pong:      return "Pong";
        case PacketType::Dummy:     return "Dummy";
    }
    return "Unknown";
}

std::expected<Frame, FrameError> encode(const Packet& pkt) {
    if (pkt.payload.size() > kMaxPayloadSize) {
        return std::unexpected(FrameError::PayloadTooLarge);
    }

    Frame frame{};
    // Fill the whole frame with random bytes first; the header + payload then
    // overwrite the front, leaving the tail as indistinguishable padding. This
    // makes a 10-byte ack and a 1397-byte chunk look identical on the wire.
    randombytes_buf(frame.data(), frame.size());

    const auto len = static_cast<std::uint16_t>(pkt.payload.size());
    frame[0] = static_cast<std::byte>(pkt.type);
    frame[1] = static_cast<std::byte>((len >> 8) & 0xFF);
    frame[2] = static_cast<std::byte>(len & 0xFF);

    if (!pkt.payload.empty()) {
        std::memcpy(frame.data() + kHeaderSize, pkt.payload.data(),
                    pkt.payload.size());
    }
    return frame;
}

std::expected<Packet, FrameError> decode(
    std::span<const std::byte, kFrameSize> frame) {
    const auto raw = std::to_integer<std::uint8_t>(frame[0]);
    if (!is_known_type(raw)) {
        return std::unexpected(FrameError::UnknownType);
    }

    const auto len = static_cast<std::uint16_t>(
        (std::to_integer<std::uint16_t>(frame[1]) << 8) |
        std::to_integer<std::uint16_t>(frame[2]));
    if (len > kMaxPayloadSize) {
        return std::unexpected(FrameError::LengthMismatch);
    }

    Packet pkt;
    pkt.type = static_cast<PacketType>(raw);
    pkt.payload.assign(frame.begin() + kHeaderSize,
                       frame.begin() + kHeaderSize + len);
    return pkt;
}

Frame make_dummy_frame() {
    Frame frame{};
    randombytes_buf(frame.data(), frame.size());
    // Mark the type so a peer can drop it cheaply, and declare a full-size
    // payload so the frame round-trips through decode(). Crucially, the length
    // bytes must NOT stay random -- otherwise decode() reads a bogus length and
    // fails. The payload region itself stays random, leaving the dummy
    // byte-for-byte indistinguishable from an encrypted payload on the wire.
    constexpr auto len = static_cast<std::uint16_t>(kMaxPayloadSize);
    frame[0] = static_cast<std::byte>(PacketType::Dummy);
    frame[1] = static_cast<std::byte>((len >> 8) & 0xFF);
    frame[2] = static_cast<std::byte>(len & 0xFF);
    return frame;
}

} // namespace aeromesh
