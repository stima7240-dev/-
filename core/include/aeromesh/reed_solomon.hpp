#pragma once

// Reed-Solomon erasure coding over GF(2^8).
//
// A blob is split into `data_shards` equal-length data shards; the coder then
// produces `parity_shards` additional shards. Any `data_shards` of the total
// `data_shards + parity_shards` shards are sufficient to reconstruct the
// original data. The receiver always knows which shards are missing (erasure
// model, not error correction), which is exactly the situation for chunked
// file transfer over a lossy peer-to-peer network.
//
// The construction is systematic (the first `data_shards` shards are the
// original data unchanged) and uses a Vandermonde-derived encoding matrix,
// inverted over GF(256) to reconstruct. No external dependencies.

#include <cstddef>
#include <cstdint>
#include <expected>
#include <vector>

namespace aeromesh {

enum class RsError {
    InvalidParams,        // shard counts out of range
    ShardSizeMismatch,    // present shards have differing lengths
    TooFewShards,         // fewer than data_shards available to reconstruct
    SingularMatrix,       // decode matrix not invertible (should not happen)
};

class ReedSolomon {
public:
    // data_shards >= 1, parity_shards >= 0, and the total must be <= 255.
    static std::expected<ReedSolomon, RsError> create(int data_shards,
                                                      int parity_shards);

    int data_shards() const { return data_shards_; }
    int parity_shards() const { return parity_shards_; }
    int total_shards() const { return data_shards_ + parity_shards_; }

    // Fill in the parity shards from the data shards.
    // `shards` must have exactly total_shards() entries, each of equal length.
    // Entries [0, data_shards) are inputs; [data_shards, total) are written.
    std::expected<void, RsError> encode(
        std::vector<std::vector<std::byte>>& shards) const;

    // Reconstruct missing shards in place.
    // `present[i]` indicates whether shard i currently holds valid data.
    // At least data_shards() entries must be present. Missing entries are
    // resized and filled. Requires every present shard to share one length.
    std::expected<void, RsError> reconstruct(
        std::vector<std::vector<std::byte>>& shards,
        const std::vector<bool>& present) const;

private:
    ReedSolomon() = default;

    int data_shards_ = 0;
    int parity_shards_ = 0;
    // Row-major (total_shards x data_shards) systematic encoding matrix.
    std::vector<std::uint8_t> matrix_;
};

} // namespace aeromesh
