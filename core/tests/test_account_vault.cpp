// Tests for the password-protected account vault (account_vault.hpp / .cpp).

#include "aeromesh/account_vault.hpp"
#include "aeromesh/identity.hpp"

#include <cstdio>
#include <span>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void check(bool cond, const char* what) {
    if (cond) {
        std::printf("[ ok ] %s\n", what);
    } else {
        std::printf("[FAIL] %s\n", what);
        ++g_failures;
    }
}

using aeromesh::AccountVault;
using aeromesh::VaultError;

void test_round_trip() {
    const std::string secret = "this-is-a-base64-identity-secret==";
    auto sealed = AccountVault::seal("correct horse battery", secret);
    check(sealed.has_value(), "seal succeeds with a strong password");
    if (!sealed.has_value()) {
        return;
    }
    auto opened = AccountVault::open("correct horse battery",
                                     std::span<const std::byte>(*sealed));
    check(opened.has_value(), "open succeeds with the correct password");
    check(opened.has_value() && *opened == secret,
          "recovered secret matches the original");
}

void test_wrong_password() {
    auto sealed = AccountVault::seal("correct horse battery", "top-secret");
    check(sealed.has_value(), "seal succeeds");
    if (!sealed.has_value()) {
        return;
    }
    auto opened = AccountVault::open("wrong password here",
                                     std::span<const std::byte>(*sealed));
    check(!opened.has_value() && opened.error() == VaultError::WrongPassword,
          "open with wrong password is rejected");
}

void test_weak_password_rejected() {
    auto sealed = AccountVault::seal("short", "top-secret");
    check(!sealed.has_value() && sealed.error() == VaultError::WeakPassword,
          "seal rejects a password shorter than the minimum");
}

void test_tampered_file_rejected() {
    auto sealed = AccountVault::seal("correct horse battery", "top-secret");
    check(sealed.has_value(), "seal succeeds");
    if (!sealed.has_value()) {
        return;
    }
    std::vector<std::byte> bad = *sealed;
    // Flip a byte inside the ciphertext (the very last byte of the blob).
    bad[bad.size() - 1] ^= std::byte{0x01};
    auto opened =
        AccountVault::open("correct horse battery", std::span<const std::byte>(bad));
    check(!opened.has_value() && opened.error() == VaultError::WrongPassword,
          "tampered ciphertext fails authentication");
}

void test_corrupt_blob_rejected() {
    auto sealed = AccountVault::seal("correct horse battery", "top-secret");
    check(sealed.has_value(), "seal succeeds");
    if (!sealed.has_value()) {
        return;
    }
    // Corrupt the magic marker.
    std::vector<std::byte> bad = *sealed;
    bad[0] = std::byte{0x00};
    auto opened =
        AccountVault::open("correct horse battery", std::span<const std::byte>(bad));
    check(!opened.has_value() && opened.error() == VaultError::Corrupt,
          "blob with bad magic is rejected as corrupt");

    // A blob that is far too short to hold a header is corrupt too.
    std::vector<std::byte> tiny(10, std::byte{0});
    auto opened_tiny =
        AccountVault::open("correct horse battery", std::span<const std::byte>(tiny));
    check(!opened_tiny.has_value() && opened_tiny.error() == VaultError::Corrupt,
          "truncated blob is rejected as corrupt");
}

void test_unique_ciphertext() {
    auto a = AccountVault::seal("correct horse battery", "top-secret");
    auto b = AccountVault::seal("correct horse battery", "top-secret");
    check(a.has_value() && b.has_value(), "two seals succeed");
    check(a.has_value() && b.has_value() && *a != *b,
          "sealing the same secret twice yields different blobs (random salt/nonce)");
}

void test_real_identity_secret() {
    auto identity = aeromesh::Identity::generate();
    check(identity.has_value(), "identity generated");
    if (!identity.has_value()) {
        return;
    }
    const std::string secret = identity->export_secret_b64();
    auto sealed = AccountVault::seal("my-account-password", secret);
    check(sealed.has_value(), "real identity secret sealed");
    if (!sealed.has_value()) {
        return;
    }
    auto opened = AccountVault::open("my-account-password",
                                     std::span<const std::byte>(*sealed));
    check(opened.has_value() && *opened == secret,
          "identity secret recovered, so the identity can be restored");
}

void test_key_api_round_trip() {
    const std::string secret = "key-api-secret==";
    AccountVault::Salt salt{};
    auto key = AccountVault::deriveKey("correct horse battery", salt);
    check(key.has_value(), "deriveKey succeeds with a strong password");
    if (!key.has_value()) {
        return;
    }
    auto sealed = AccountVault::sealWithKey(*key, salt, secret);
    check(sealed.has_value(), "sealWithKey succeeds");
    if (!sealed.has_value()) {
        return;
    }
    auto storedSalt = AccountVault::saltOf(std::span<const std::byte>(*sealed));
    check(storedSalt.has_value() && *storedSalt == salt,
          "saltOf recovers the salt embedded in the blob");
    auto opened =
        AccountVault::openWithKey(*key, std::span<const std::byte>(*sealed));
    check(opened.has_value() && *opened == secret,
          "openWithKey recovers the secret with the cached key");
}

void test_derive_deterministic() {
    AccountVault::Salt salt{};
    auto a = AccountVault::deriveKey("correct horse battery", salt);
    check(a.has_value(), "deriveKey produces a key");
    if (!a.has_value()) {
        return;
    }
    auto b = AccountVault::deriveKeyWithSalt("correct horse battery", salt);
    check(b.has_value() && *a == *b,
          "deriveKeyWithSalt reproduces the same key for the same salt");
    auto c = AccountVault::deriveKeyWithSalt("different password!!", salt);
    check(c.has_value() && *c != *a,
          "a different password yields a different key for the same salt");
}

void test_wrong_key_rejected() {
    AccountVault::Salt salt{};
    auto key = AccountVault::deriveKey("correct horse battery", salt);
    auto wrong = AccountVault::deriveKeyWithSalt("totally wrong pass!", salt);
    check(key.has_value() && wrong.has_value(), "two keys derived");
    if (!key.has_value() || !wrong.has_value()) {
        return;
    }
    auto sealed = AccountVault::sealWithKey(*key, salt, "secret-data");
    check(sealed.has_value(), "sealWithKey succeeds");
    if (!sealed.has_value()) {
        return;
    }
    auto opened =
        AccountVault::openWithKey(*wrong, std::span<const std::byte>(*sealed));
    check(!opened.has_value() && opened.error() == VaultError::WrongPassword,
          "openWithKey with the wrong key is rejected");
}

void test_wipe_clears_key() {
    AccountVault::Salt salt{};
    auto key = AccountVault::deriveKey("correct horse battery", salt);
    check(key.has_value(), "deriveKey succeeds");
    if (!key.has_value()) {
        return;
    }
    AccountVault::Key copy = *key;
    AccountVault::wipe(copy);
    bool all_zero = true;
    for (auto byte : copy) {
        if (byte != 0) {
            all_zero = false;
            break;
        }
    }
    check(all_zero, "wipe zeroes the key bytes");
}

}  // namespace

int main() {
    if (!aeromesh::init_crypto()) {
        std::printf("[FAIL] init_crypto\n");
        return 1;
    }
    test_round_trip();
    test_wrong_password();
    test_weak_password_rejected();
    test_tampered_file_rejected();
    test_corrupt_blob_rejected();
    test_unique_ciphertext();
    test_real_identity_secret();
    test_key_api_round_trip();
    test_derive_deterministic();
    test_wrong_key_rejected();
    test_wipe_clears_key();
    return g_failures == 0 ? 0 : 1;
}
