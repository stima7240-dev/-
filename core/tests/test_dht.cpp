// Dependency-free tests for the Kademlia DHT layer: node ids, routing table,
// contact serialisation, and iterative lookup over a simulated network.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "aeromesh/identity.hpp"
#include "aeromesh/kademlia.hpp"
#include "aeromesh/node_id.hpp"
#include "aeromesh/routing_table.hpp"

namespace {

int g_failures = 0;

void check(bool cond, const char* what) {
    if (!cond) {
        std::printf("  [FAIL] %s\n", what);
        ++g_failures;
    } else {
        std::printf("  [ ok ] %s\n", what);
    }
}

using namespace aeromesh;

std::vector<Contact> brute_force_closest(std::vector<Contact> v,
                                         const NodeId& target,
                                         std::size_t n) {
    const CloserTo cmp{target};
    std::sort(v.begin(), v.end(),
              [&](const Contact& a, const Contact& b) { return cmp(a.id, b.id); });
    if (v.size() > n) v.resize(n);
    return v;
}

bool same_ids(const std::vector<Contact>& a, const std::vector<Contact>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (!(a[i].id == b[i].id)) return false;
    return true;
}

void test_node_id() {
    std::printf("node id + xor distance\n");
    const auto a = NodeId::random();
    const auto b = NodeId::random();
    check(NodeId::distance(a, a) == NodeId{}, "distance to self is zero");
    check(NodeId::distance(a, b) == NodeId::distance(b, a), "distance is symmetric");
    check(NodeId::shared_prefix_length(a, a) == kIdBits,
          "id shares all bits with itself");

    std::array<std::byte, 32> pk{};
    for (std::size_t i = 0; i < pk.size(); ++i)
        pk[i] = static_cast<std::byte>(i + 1);
    check(NodeId::from_public_key(pk) == NodeId::from_public_key(pk),
          "id derivation from a key is deterministic");

    const auto hx = a.to_hex();
    check(hx.size() == kIdBytes * 2, "hex is 64 chars");
    const auto parsed = NodeId::from_hex(hx);
    check(parsed.has_value() && *parsed == a, "hex round-trips");
    check(!NodeId::from_hex("zz").has_value(), "malformed hex rejected");
}

void test_routing_table() {
    std::printf("routing table closest()\n");
    const auto self = NodeId::random();
    // Large k so nothing is evicted -- we want to compare against brute force.
    RoutingTable rt(self, 4096);
    std::vector<Contact> all;
    for (int i = 0; i < 50; ++i) {
        Contact c{NodeId::random(), "node-" + std::to_string(i)};
        all.push_back(c);
        rt.update(c);
    }
    check(rt.size() == all.size(), "all distinct contacts stored");

    const auto self_contact = Contact{self, "me"};
    check(!rt.update(self_contact), "self is never stored");
    check(rt.size() == all.size(), "self did not change table size");

    const auto target = NodeId::random();
    const auto got = rt.closest(target, 8);
    const auto expected = brute_force_closest(all, target, 8);
    check(got.size() == 8, "closest() returns the requested count");
    check(same_ids(got, expected), "closest() matches brute-force ordering");
}

void test_contact_serialisation() {
    std::printf("contact serialisation\n");
    std::vector<Contact> cs{
        {NodeId::random(), "192.168.0.1:4000"},
        {NodeId::random(), ""},
        {NodeId::random(), "seed.aeromesh.example:9999"},
    };
    const auto enc = encode_contacts(cs);
    check(enc.has_value(), "encode succeeds");
    if (!enc) return;
    const auto dec = decode_contacts(std::span<const std::byte>(*enc));
    check(dec.has_value(), "decode succeeds");
    if (!dec) return;
    check(dec->size() == cs.size(), "contact count preserved");
    bool ok = dec->size() == cs.size();
    for (std::size_t i = 0; ok && i < cs.size(); ++i)
        ok = (*dec)[i].id == cs[i].id && (*dec)[i].endpoint == cs[i].endpoint;
    check(ok, "ids and endpoints preserved");

    std::vector<std::byte> truncated{std::byte{0}};
    check(!decode_contacts(std::span<const std::byte>(truncated)).has_value(),
          "truncated buffer rejected");
}

void test_lookup_multihop() {
    std::printf("iterative lookup over simulated network\n");
    constexpr std::size_t kBig = 256;  // big k: responders retain full knowledge
    std::vector<Contact> all;
    for (int i = 0; i < 30; ++i)
        all.push_back(Contact{NodeId::random(), "node-" + std::to_string(i)});

    // Build a responder Dht per contact.
    std::unordered_map<std::string, Dht> net;
    for (const auto& c : all) net.emplace(c.id.to_hex(), Dht(c.id, kBig));

    // all[0] is a near-empty relay that only knows all[1]; every other node
    // has full knowledge. So the only path to the network is all[0] -> all[1],
    // forcing a genuine multi-hop lookup.
    for (const auto& c : all) {
        Dht& d = net.at(c.id.to_hex());
        if (c.id == all[0].id) {
            d.table().update(all[1]);
        } else {
            for (const auto& other : all)
                if (!(other.id == c.id)) d.table().update(other);
        }
    }

    const QueryFn query = [&](const Contact& peer,
                              const NodeId& target) -> std::vector<Contact> {
        const auto it = net.find(peer.id.to_hex());
        if (it == net.end()) return {};
        return it->second.handle_find_node(target);
    };

    // A fresh node that initially knows only the all[0] relay.
    Dht newbie(NodeId::random(), kBig);
    const std::size_t known = newbie.bootstrap({all[0]}, query);
    check(known == all.size(),
          "bootstrap discovered the whole network via multi-hop");

    const auto target = NodeId::random();
    const auto result = newbie.lookup(target, query);
    const auto expected = brute_force_closest(all, target, kBig);
    check(same_ids(result, expected),
          "lookup returns the globally closest contacts");
    check(!result.empty() && result.front().id == expected.front().id,
          "closest contact found first");
}

} // namespace

int main() {
    if (!aeromesh::init_crypto()) {
        std::printf("  [FAIL] libsodium init\n");
        return EXIT_FAILURE;
    }
    test_node_id();
    test_routing_table();
    test_contact_serialisation();
    test_lookup_multihop();
    if (g_failures == 0) {
        std::printf("\nALL TESTS PASSED\n");
        return EXIT_SUCCESS;
    }
    std::printf("\n%d TEST(S) FAILED\n", g_failures);
    return EXIT_FAILURE;
}
