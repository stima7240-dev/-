#pragma once

// Chunked, erasure-coded file transfer.
//
// A file is divided into fixed-size chunks and grouped into stripes. Each
// stripe holds `data_shards` data chunks plus `parity_shards` Reed-Solomon
// parity chunks, so any `data_shards` chunks of a stripe reconstruct it.
// Striping is required because GF(256) Reed-Solomon caps a stripe at 255
// shards, while files may be up to 4 GiB.
//
// Integrity is layered:
//   * Every chunk carries a BLAKE2b tag; a chunk whose contents do not match
//     its tag is treated as missing (so corruption never silently poisons a
//     stripe -- erasure coding only tolerates *known* losses).
//   * The manifest carries a BLAKE2b hash of the whole file (`file_id`); the
//     reassembled bytes are verified against it end to end.

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

namespace aeromesh {

// 4 GiB hard cap, per the AeroMesh spec.
inline constexpr std::uint64_t kMaxFileSize = 4ull * 1024 * 1024 * 1024;
inline constexpr std::size_t kFileHashLen = 32;
inline constexpr std::size_t kChunkTagLen = 32;

enum class FileError {
    Empty,
    TooLarge,
    InvalidParams,
    ShardMissing,    // a stripe has fewer than data_shards valid chunks
    HashMismatch,    // reassembled file fails its end-to-end hash check
    Malformed,
};

// BLAKE2b digest of the whole file; doubles as the content-addressed file id.
using FileId = std::array<std::byte, kFileHashLen>;

struct ChunkId {
    std::uint32_t stripe = 0;
    std::uint16_t shard = 0;   // 0..(data_shards + parity_shards - 1)
};

struct Chunk {
    ChunkId id{};
    std::vector<std::byte> data;                 // exactly chunk_size bytes
    std::array<std::byte, kChunkTagLen> tag{};   // BLAKE2b(stripe||shard||data)
};

struct FileManifest {
    FileId file_id{};
    std::uint64_t file_size = 0;
    std::uint32_t chunk_size = 0;
    std::uint16_t data_shards = 0;
    std::uint16_t parity_shards = 0;
    std::uint32_t stripe_count = 0;
};

struct EncodedFile {
    FileManifest manifest{};
    std::vector<Chunk> chunks;   // all data + parity chunks, every stripe
};

// Split and Reed-Solomon-encode a file into chunks.
// chunk_size >= 1; data_shards >= 1; data_shards + parity_shards <= 255.
std::expected<EncodedFile, FileError> encode_file(
    std::span<const std::byte> file, std::uint32_t chunk_size,
    std::uint16_t data_shards, std::uint16_t parity_shards);

// Reassemble a file from any sufficient subset of its chunks. Chunks failing
// their BLAKE2b tag are ignored; each stripe needs >= data_shards survivors.
// The result is verified against manifest.file_id.
std::expected<std::vector<std::byte>, FileError> decode_file(
    const FileManifest& manifest, const std::vector<Chunk>& chunks);

// Recompute a chunk's BLAKE2b tag (the value encode_file stores in Chunk::tag).
std::array<std::byte, kChunkTagLen> chunk_tag(const ChunkId& id,
                                              std::span<const std::byte> data);

} // namespace aeromesh
