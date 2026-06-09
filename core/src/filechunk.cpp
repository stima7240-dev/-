#include "aeromesh/filechunk.hpp"

#include "aeromesh/reed_solomon.hpp"

#include <sodium.h>

#include <array>
#include <cstring>
#include <map>

namespace aeromesh {
namespace {

const unsigned char* uc(const std::byte* p) {
    return reinterpret_cast<const unsigned char*>(p);
}
unsigned char* uc(std::byte* p) {
    return reinterpret_cast<unsigned char*>(p);
}

void put_u32(std::vector<unsigned char>& out, std::uint32_t v) {
    out.push_back(static_cast<unsigned char>((v >> 24) & 0xFF));
    out.push_back(static_cast<unsigned char>((v >> 16) & 0xFF));
    out.push_back(static_cast<unsigned char>((v >> 8) & 0xFF));
    out.push_back(static_cast<unsigned char>(v & 0xFF));
}

void put_u16(std::vector<unsigned char>& out, std::uint16_t v) {
    out.push_back(static_cast<unsigned char>((v >> 8) & 0xFF));
    out.push_back(static_cast<unsigned char>(v & 0xFF));
}

FileId file_hash(std::span<const std::byte> data) {
    FileId id{};
    crypto_generichash(uc(id.data()), id.size(),
                       data.empty() ? nullptr : uc(data.data()), data.size(),
                       nullptr, 0);
    return id;
}

} // namespace

std::array<std::byte, kChunkTagLen> chunk_tag(const ChunkId& id,
                                              std::span<const std::byte> data) {
    // Tag binds the chunk's position (stripe, shard) to its contents so a
    // chunk cannot be silently relabeled or corrupted.
    std::vector<unsigned char> prefix;
    prefix.reserve(6);
    put_u32(prefix, id.stripe);
    put_u16(prefix, id.shard);

    crypto_generichash_state st;
    crypto_generichash_init(&st, nullptr, 0, kChunkTagLen);
    crypto_generichash_update(&st, prefix.data(), prefix.size());
    crypto_generichash_update(&st, data.empty() ? nullptr : uc(data.data()),
                              data.size());
    std::array<std::byte, kChunkTagLen> tag{};
    crypto_generichash_final(&st, uc(tag.data()), tag.size());
    return tag;
}

std::expected<EncodedFile, FileError> encode_file(
    std::span<const std::byte> file, std::uint32_t chunk_size,
    std::uint16_t data_shards, std::uint16_t parity_shards) {
    if (file.empty())
        return std::unexpected(FileError::Empty);
    if (file.size() > kMaxFileSize)
        return std::unexpected(FileError::TooLarge);
    if (chunk_size < 1 || data_shards < 1 ||
        static_cast<int>(data_shards) + parity_shards > 255)
        return std::unexpected(FileError::InvalidParams);

    auto rs = ReedSolomon::create(data_shards, parity_shards);
    if (!rs)
        return std::unexpected(FileError::InvalidParams);

    EncodedFile out;
    out.manifest.file_id = file_hash(file);
    out.manifest.file_size = file.size();
    out.manifest.chunk_size = chunk_size;
    out.manifest.data_shards = data_shards;
    out.manifest.parity_shards = parity_shards;

    const std::uint64_t stripe_bytes =
        static_cast<std::uint64_t>(chunk_size) * data_shards;
    const std::uint32_t stripe_count = static_cast<std::uint32_t>(
        (file.size() + stripe_bytes - 1) / stripe_bytes);
    out.manifest.stripe_count = stripe_count;

    const int total = data_shards + parity_shards;
    for (std::uint32_t s = 0; s < stripe_count; ++s) {
        const std::uint64_t base = static_cast<std::uint64_t>(s) * stripe_bytes;

        // Build the data shards for this stripe (zero-padded as needed).
        std::vector<std::vector<std::byte>> shards(
            static_cast<std::size_t>(total));
        for (int d = 0; d < data_shards; ++d) {
            shards[d].assign(chunk_size, std::byte{0});
            const std::uint64_t off = base + static_cast<std::uint64_t>(d) *
                                                 chunk_size;
            if (off < file.size()) {
                const std::size_t n = static_cast<std::size_t>(
                    std::min<std::uint64_t>(chunk_size, file.size() - off));
                std::memcpy(shards[d].data(), file.data() + off, n);
            }
        }

        auto e = rs->encode(shards);
        if (!e)
            return std::unexpected(FileError::Malformed);

        for (int idx = 0; idx < total; ++idx) {
            Chunk c;
            c.id.stripe = s;
            c.id.shard = static_cast<std::uint16_t>(idx);
            c.data = shards[idx];
            c.tag = chunk_tag(c.id, std::span<const std::byte>(c.data));
            out.chunks.push_back(std::move(c));
        }
    }
    return out;
}

std::expected<std::vector<std::byte>, FileError> decode_file(
    const FileManifest& manifest, const std::vector<Chunk>& chunks) {
    if (manifest.file_size == 0)
        return std::unexpected(FileError::Empty);
    if (manifest.file_size > kMaxFileSize)
        return std::unexpected(FileError::TooLarge);
    if (manifest.chunk_size < 1 || manifest.data_shards < 1 ||
        static_cast<int>(manifest.data_shards) + manifest.parity_shards > 255)
        return std::unexpected(FileError::InvalidParams);

    auto rs = ReedSolomon::create(manifest.data_shards, manifest.parity_shards);
    if (!rs)
        return std::unexpected(FileError::InvalidParams);

    const int total = manifest.data_shards + manifest.parity_shards;
    const std::uint32_t chunk_size = manifest.chunk_size;

    // Collect valid chunks per stripe. A chunk whose tag does not match its
    // contents is dropped (treated as a loss, never as good data).
    struct StripeShards {
        std::vector<std::vector<std::byte>> data;
        std::vector<bool> present;
        StripeShards(int t, std::uint32_t cs)
            : data(static_cast<std::size_t>(t)),
              present(static_cast<std::size_t>(t), false) {
            (void)cs;
        }
    };
    std::map<std::uint32_t, StripeShards> stripes;

    for (const Chunk& c : chunks) {
        if (c.id.stripe >= manifest.stripe_count)
            continue;
        if (c.id.shard >= total)
            continue;
        if (c.data.size() != chunk_size)
            continue;
        const auto expect = chunk_tag(c.id, std::span<const std::byte>(c.data));
        if (expect != c.tag)
            continue;  // corrupted or mislabeled -> ignore
        auto it = stripes.find(c.id.stripe);
        if (it == stripes.end())
            it = stripes.emplace(c.id.stripe, StripeShards(total, chunk_size))
                     .first;
        if (!it->second.present[c.id.shard]) {
            it->second.data[c.id.shard] = c.data;
            it->second.present[c.id.shard] = true;
        }
    }

    std::vector<std::byte> result;
    result.reserve(manifest.file_size);

    for (std::uint32_t s = 0; s < manifest.stripe_count; ++s) {
        auto it = stripes.find(s);
        if (it == stripes.end())
            return std::unexpected(FileError::ShardMissing);
        StripeShards& ss = it->second;

        int present_count = 0;
        for (int i = 0; i < total; ++i)
            if (ss.present[i])
                ++present_count;
        if (present_count < manifest.data_shards)
            return std::unexpected(FileError::ShardMissing);

        // Ensure every present shard has the right length for reconstruct().
        auto r = rs->reconstruct(ss.data, ss.present);
        if (!r)
            return std::unexpected(FileError::ShardMissing);

        for (int d = 0; d < manifest.data_shards; ++d)
            result.insert(result.end(), ss.data[d].begin(), ss.data[d].end());
    }

    if (result.size() < manifest.file_size)
        return std::unexpected(FileError::Malformed);
    result.resize(static_cast<std::size_t>(manifest.file_size));

    const FileId got = file_hash(std::span<const std::byte>(result));
    if (got != manifest.file_id)
        return std::unexpected(FileError::HashMismatch);
    return result;
}

} // namespace aeromesh
