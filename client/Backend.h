#pragma once

// AeroMesh client backend bridge (multi-account).
//
// Connects the QML user interface to the cryptographic core (aeromesh::core).
//
// The device identity (the long-term secret key) is NEVER stored in the clear.
// Each account lives in its OWN encrypted file inside an accounts directory.
// A file holds, encrypted together: the account display name AND the secret
// identity key, protected by the AccountVault (Argon2id password stretching +
// XSalsa20-Poly1305 secretbox). Because the name is encrypted too, nothing
// about an account is readable on disk without its password.
//
// Login is by password only: the backend tries each on-disk account file with
// the entered password and unlocks whichever one it decrypts. Unlocked accounts
// stay in memory for the session, so switching between them is instant. Adding
// an account is a full registration: pick a name and a password, a brand-new
// identity is minted and written to a new encrypted file.
//
// accountState is the single source of truth for which screen QML shows:
//   "register" -- no account files yet; ask for a name + password
//   "locked"   -- account files exist but none is unlocked; ask for a password
//   "ready"    -- at least one account is unlocked; show the messenger
//
// Only derived, network-safe values (name, fingerprint, node id, share string)
// are ever exposed to QML -- never the secret key. Methods that can fail return
// a short ASCII error code ("" on success); QML maps the code to a localized
// message so no human-language text lives in C++.

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <QVariantList>
#include <QVariantMap>

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "aeromesh/account_vault.hpp"
#include "aeromesh/identity.hpp"
#include "aeromesh/node_id.hpp"

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString accountState READ accountState NOTIFY stateChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged)
    Q_PROPERTY(bool hasAccount READ hasAccount NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
    Q_PROPERTY(QString accountName READ accountName NOTIFY identityChanged)
    Q_PROPERTY(QString fingerprint READ fingerprint NOTIFY identityChanged)
    Q_PROPERTY(QString nodeId READ nodeId NOTIFY identityChanged)
    Q_PROPERTY(QString shareString READ shareString NOTIFY identityChanged)
    Q_PROPERTY(QVariantList accounts READ accounts NOTIFY accountsChanged)

public:
    explicit Backend(QObject* parent = nullptr) : QObject(parent) {
        accountsDir_ = defaultAccountsDir();
        migrateLegacyAccount();
        if (!aeromesh::init_crypto()) {
            accountState_ = QStringLiteral("error");
            lastError_ = QStringLiteral("crypto");
            return;
        }
        accountState_ = hasAccount() ? QStringLiteral("locked")
                                     : QStringLiteral("register");
    }

    [[nodiscard]] QString accountState() const { return accountState_; }
    [[nodiscard]] bool ready() const { return !sessions_.empty(); }
    [[nodiscard]] bool hasAccount() const { return !accountFiles().isEmpty(); }
    [[nodiscard]] QString lastError() const { return lastError_; }
    [[nodiscard]] QString accountName() const { return accountName_; }
    [[nodiscard]] QString fingerprint() const { return fingerprint_; }
    [[nodiscard]] QString nodeId() const { return nodeId_; }
    [[nodiscard]] QString shareString() const { return shareString_; }

    // Non-owning pointer to the active account's long-term identity, or nullptr
    // when nothing is unlocked. This is C++-only and is NEVER exposed to QML --
    // the secret key must never leave the backend. The chat layer uses it to
    // run handshakes. The pointer is stable while that session lives, but it
    // dangles after lock/switch/delete, so consumers must refresh whenever the
    // identityChanged() signal fires.
    [[nodiscard]] const aeromesh::Identity* activeIdentity() const {
        const Session* a = active();
        return a ? &a->identity : nullptr;
    }

    // The list of currently unlocked accounts, for the account switcher.
    // Each entry: { name, fingerprint, active }.
    [[nodiscard]] QVariantList accounts() const {
        QVariantList list;
        for (int i = 0; i < static_cast<int>(sessions_.size()); ++i) {
            QVariantMap m;
            m[QStringLiteral("name")] = sessions_[i]->name;
            m[QStringLiteral("fingerprint")] = sessions_[i]->fingerprint;
            m[QStringLiteral("active")] = (i == activeIndex_);
            list.append(m);
        }
        return list;
    }

    // Register a brand-new account: mint an identity, then write the display
    // name + secret, encrypted with `password`, to a new account file. The new
    // account becomes the active one. Returns "" on success or an error code:
    //   "weak"   -- password shorter than the minimum length
    //   "crypto" -- identity generation or sealing failed
    //   "io"     -- the encrypted file could not be written
    Q_INVOKABLE QString createAccount(const QString& name,
                                      const QString& password) {
        if (static_cast<std::size_t>(password.toUtf8().size()) <
            aeromesh::AccountVault::kMinPasswordLength) {
            return setError(QStringLiteral("weak"));
        }
        auto fresh = aeromesh::Identity::generate();
        if (!fresh) {
            return setError(QStringLiteral("crypto"));
        }
        const std::string secret = fresh->export_secret_b64();
        const std::string payload = makePayload(name, secret);
        aeromesh::AccountVault::Salt salt{};
        auto key = aeromesh::AccountVault::deriveKey(password.toStdString(), salt);
        if (!key) {
            return setError(QStringLiteral("crypto"));
        }
        auto sealed = aeromesh::AccountVault::sealWithKey(*key, salt, payload);
        if (!sealed) {
            aeromesh::AccountVault::wipe(*key);
            return setError(QStringLiteral("crypto"));
        }
        const QString path = newAccountPath();
        if (!writeBlob(path, *sealed)) {
            aeromesh::AccountVault::wipe(*key);
            return setError(QStringLiteral("io"));
        }
        QString fp, nid, ss;
        deriveStrings(*fresh, fp, nid, ss);
        sessions_.push_back(std::unique_ptr<Session>(new Session{
            path, sanitizedName(name), fp, nid, ss, std::move(*fresh), *key,
            salt}));
        aeromesh::AccountVault::wipe(*key);
        activeIndex_ = static_cast<int>(sessions_.size()) - 1;
        lastError_.clear();
        publishActive();
        setState(QStringLiteral("ready"));
        emit accountsChanged();
        return QString();
    }

    // Unlock an existing account by password. Every on-disk account file that is
    // not already unlocked is tried with this password; the one that decrypts
    // becomes the active account. Returns "" on success or an error code:
    //   "missing" -- no account files present
    //   "wrong"   -- no account file matched this password
    //   "corrupt" -- a matching file decrypted but held an invalid identity
    Q_INVOKABLE QString unlock(const QString& password) {
        const QStringList files = accountFiles();
        if (files.isEmpty()) {
            return setError(QStringLiteral("missing"));
        }
        for (const QString& fileName : files) {
            const QString path = accountsDir_ + QStringLiteral("/") + fileName;
            if (isUnlocked(path)) {
                continue;
            }
            QByteArray data;
            if (!readBlob(path, data)) {
                continue;
            }
            const auto* bytes =
                reinterpret_cast<const std::byte*>(data.constData());
            std::span<const std::byte> blob(
                bytes, static_cast<std::size_t>(data.size()));
            auto salt = aeromesh::AccountVault::saltOf(blob);
            if (!salt) {
                continue;
            }
            auto key = aeromesh::AccountVault::deriveKeyWithSalt(
                password.toStdString(), *salt);
            if (!key) {
                continue;
            }
            auto opened = aeromesh::AccountVault::openWithKey(*key, blob);
            if (!opened) {
                aeromesh::AccountVault::wipe(*key);
                continue;  // wrong password for this file; try the next one
            }
            auto split = splitPayload(*opened);
            auto restored = aeromesh::Identity::from_secret_b64(split.second);
            if (!restored) {
                aeromesh::AccountVault::wipe(*key);
                return setError(QStringLiteral("corrupt"));
            }
            QString fp, nid, ss;
            deriveStrings(*restored, fp, nid, ss);
            sessions_.push_back(std::unique_ptr<Session>(new Session{
                path, split.first, fp, nid, ss, std::move(*restored), *key,
                *salt}));
            aeromesh::AccountVault::wipe(*key);
            activeIndex_ = static_cast<int>(sessions_.size()) - 1;
            lastError_.clear();
            publishActive();
            setState(QStringLiteral("ready"));
            emit accountsChanged();
            return QString();
        }
        return setError(QStringLiteral("wrong"));
    }

    // Switch the active account among the unlocked sessions.
    Q_INVOKABLE void switchAccount(int index) {
        if (index < 0 || index >= static_cast<int>(sessions_.size())) {
            return;
        }
        activeIndex_ = index;
        publishActive();
        emit stateChanged();
        emit accountsChanged();
    }

    // Forget all unlocked identities and return to the lock screen. The
    // encrypted files on disk are untouched.
    Q_INVOKABLE void lock() {
        for (auto& session : sessions_) {
            wipeSession(*session);
        }
        sessions_.clear();
        activeIndex_ = -1;
        clearIdentity();
        setState(hasAccount() ? QStringLiteral("locked")
                              : QStringLiteral("register"));
        emit accountsChanged();
    }

    // Rename the active account and re-encrypt its file with the cached key.
    // Returns "" on success or an error code ("locked" if nothing is unlocked).
    Q_INVOKABLE QString renameAccount(const QString& name) {
        if (activeIndex_ < 0) {
            return setError(QStringLiteral("locked"));
        }
        Session* s = sessions_[activeIndex_].get();
        const std::string secret = s->identity.export_secret_b64();
        const std::string payload = makePayload(name, secret);
        auto sealed =
            aeromesh::AccountVault::sealWithKey(s->key, s->salt, payload);
        if (!sealed) {
            return setError(QStringLiteral("crypto"));
        }
        if (!writeBlob(s->file, *sealed)) {
            return setError(QStringLiteral("io"));
        }
        s->name = sanitizedName(name);
        publishActive();
        emit accountsChanged();
        return QString();
    }

    // Permanently delete the active account file and drop it from the session.
    // This destroys the only copy of that identity, so the UI must confirm.
    Q_INVOKABLE bool deleteAccount() {
        if (activeIndex_ < 0) {
            return false;
        }
        const QString path = sessions_[activeIndex_]->file;
        wipeSession(*sessions_[activeIndex_]);
        sessions_.erase(sessions_.begin() + activeIndex_);
        bool ok = true;
        if (QFileInfo::exists(path)) {
            ok = QFile::remove(path);
        }
        if (sessions_.empty()) {
            activeIndex_ = -1;
            clearIdentity();
            setState(hasAccount() ? QStringLiteral("locked")
                                  : QStringLiteral("register"));
        } else {
            activeIndex_ = 0;
            publishActive();
        }
        emit accountsChanged();
        return ok;
    }

    // Mint a fresh identity for the active account and re-encrypt its file with
    // the cached key. Returns "" on success or an error code.
    Q_INVOKABLE QString regenerateIdentity() {
        if (activeIndex_ < 0) {
            return setError(QStringLiteral("locked"));
        }
        Session* old = sessions_[activeIndex_].get();
        auto fresh = aeromesh::Identity::generate();
        if (!fresh) {
            return setError(QStringLiteral("crypto"));
        }
        const std::string secret = fresh->export_secret_b64();
        const std::string payload = makePayload(old->name, secret);
        auto sealed =
            aeromesh::AccountVault::sealWithKey(old->key, old->salt, payload);
        if (!sealed) {
            return setError(QStringLiteral("crypto"));
        }
        if (!writeBlob(old->file, *sealed)) {
            return setError(QStringLiteral("io"));
        }
        QString fp, nid, ss;
        deriveStrings(*fresh, fp, nid, ss);
        sessions_[activeIndex_] = std::unique_ptr<Session>(new Session{
            old->file, old->name, fp, nid, ss, std::move(*fresh), old->key,
            old->salt});
        publishActive();
        emit accountsChanged();
        return QString();
    }

    // Validate and inspect a peer's share string (their public key, base64).
    // Returns { valid, fingerprint, nodeId, share } so the UI can confirm the
    // contact's cryptographic identity before saving it. Pure read-only.
    Q_INVOKABLE QVariantMap inspectShare(const QString& share) const {
        QVariantMap out;
        out[QStringLiteral("valid")] = false;
        const QString trimmed = share.trimmed();
        auto parsed = aeromesh::parse_share_string(trimmed.toStdString());
        if (!parsed) {
            return out;
        }
        const auto& pk = *parsed;
        const auto node = aeromesh::NodeId::from_public_key(
            std::span<const std::byte>(pk.data(), pk.size()));
        out[QStringLiteral("valid")] = true;
        out[QStringLiteral("fingerprint")] = fingerprintFromKey(pk);
        out[QStringLiteral("nodeId")] = QString::fromStdString(node.to_hex());
        out[QStringLiteral("share")] = trimmed;
        return out;
    }

signals:
    void stateChanged();
    void identityChanged();
    void accountsChanged();

private:
    // One unlocked account held in memory for this session. The cached key/salt
    // let us re-seal (rename, regenerate) without re-deriving from the password.
    struct Session {
        QString file;
        QString name;
        QString fingerprint;
        QString nodeId;
        QString shareString;
        aeromesh::Identity identity;
        aeromesh::AccountVault::Key key;
        aeromesh::AccountVault::Salt salt;
    };

    static QString defaultAccountsDir() {
        QString dir =
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (dir.isEmpty()) {
            dir = QDir::homePath() + QStringLiteral("/.aeromesh");
        }
        return dir + QStringLiteral("/accounts");
    }

    static QString legacyAccountPath() {
        QString dir =
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (dir.isEmpty()) {
            dir = QDir::homePath() + QStringLiteral("/.aeromesh");
        }
        return dir + QStringLiteral("/account.amv");
    }

    // Move a single-account file from the old layout into the accounts folder so
    // an account created by an earlier build is not lost.
    void migrateLegacyAccount() {
        const QString legacy = legacyAccountPath();
        if (QFileInfo::exists(legacy) && accountFiles().isEmpty()) {
            QDir().mkpath(accountsDir_);
            QFile::rename(legacy,
                          accountsDir_ + QStringLiteral("/acc_legacy.amv"));
        }
    }

    [[nodiscard]] QStringList accountFiles() const {
        QDir dir(accountsDir_);
        if (!dir.exists()) {
            return {};
        }
        return dir.entryList(QStringList{QStringLiteral("*.amv")}, QDir::Files,
                             QDir::Name);
    }

    [[nodiscard]] QString newAccountPath() const {
        return accountsDir_ + QStringLiteral("/acc_") +
               QUuid::createUuid().toString(QUuid::WithoutBraces) +
               QStringLiteral(".amv");
    }

    [[nodiscard]] bool isUnlocked(const QString& path) const {
        for (const auto& session : sessions_) {
            if (session->file == path) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] Session* active() const {
        if (activeIndex_ < 0 || activeIndex_ >= static_cast<int>(sessions_.size())) {
            return nullptr;
        }
        return sessions_[activeIndex_].get();
    }

    // Encode the encrypted payload as "<name-utf8>\n<secret-base64>". The name
    // is sanitized so it never contains a newline; base64 never does either.
    std::string makePayload(const QString& name,
                            const std::string& secret) const {
        const QByteArray n = sanitizedName(name).toUtf8();
        std::string out(n.constData(), static_cast<std::size_t>(n.size()));
        out.push_back('\n');
        out += secret;
        return out;
    }

    static std::pair<QString, std::string> splitPayload(
        const std::string& payload) {
        const auto pos = payload.find('\n');
        if (pos == std::string::npos) {
            return {QString(), payload};  // legacy file: secret only, no name
        }
        QString name =
            QString::fromUtf8(payload.data(), static_cast<int>(pos));
        std::string secret = payload.substr(pos + 1);
        return {name, secret};
    }

    static QString sanitizedName(const QString& raw) {
        QString s = raw;
        s.replace(QChar('\n'), QChar(' '));
        s.replace(QChar('\r'), QChar(' '));
        return s.trimmed();
    }

    static void deriveStrings(const aeromesh::Identity& id, QString& fingerprint,
                              QString& nodeId, QString& shareString) {
        const auto& pk = id.public_key();
        const auto node = aeromesh::NodeId::from_public_key(
            std::span<const std::byte>(pk.data(), pk.size()));
        fingerprint = QString::fromStdString(id.fingerprint());
        nodeId = QString::fromStdString(node.to_hex());
        shareString = QString::fromStdString(id.share_string());
    }

    bool writeBlob(const QString& path, const std::vector<std::byte>& blob) {
        const QFileInfo info(path);
        QDir().mkpath(info.absolutePath());
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            return false;
        }
        const char* data = reinterpret_cast<const char*>(blob.data());
        const qint64 size = static_cast<qint64>(blob.size());
        if (file.write(data, size) != size) {
            file.cancelWriting();
            return false;
        }
        if (!file.commit()) {
            return false;
        }
        // Restrict the encrypted file to the owner only.
        QFile::setPermissions(path,
                              QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        emit stateChanged();  // hasAccount may have changed
        return true;
    }

    bool readBlob(const QString& path, QByteArray& out) const {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return false;
        }
        out = file.readAll();
        return true;
    }

    void publishActive() {
        const Session* a = active();
        if (a) {
            accountName_ = a->name;
            fingerprint_ = a->fingerprint;
            nodeId_ = a->nodeId;
            shareString_ = a->shareString;
        } else {
            accountName_.clear();
            fingerprint_.clear();
            nodeId_.clear();
            shareString_.clear();
        }
        emit identityChanged();
    }

    void clearIdentity() {
        accountName_.clear();
        fingerprint_.clear();
        nodeId_.clear();
        shareString_.clear();
        emit identityChanged();
    }

    void setState(const QString& state) {
        accountState_ = state;
        emit stateChanged();
    }

    QString setError(const QString& code) {
        lastError_ = code;
        emit stateChanged();
        return code;
    }

    static void wipeSession(Session& s) {
        aeromesh::AccountVault::wipe(s.key);
        aeromesh::AccountVault::wipe(s.salt);
    }

    // Format the first 8 bytes of a public key as "A1B2 C3D4 E5F6 0718",
    // matching Identity::fingerprint() so both sides display the same value.
    static QString fingerprintFromKey(const aeromesh::Identity::PublicKey& pk) {
        QString hex;
        for (std::size_t i = 0; i < 8; ++i) {
            hex += QString("%1").arg(
                static_cast<unsigned>(std::to_integer<unsigned char>(pk[i])),
                2, 16, QChar('0'));
            if (i % 2 == 1 && i != 7) {
                hex += QChar(' ');
            }
        }
        return hex.toUpper();
    }

    QString accountState_ = QStringLiteral("register");
    QString lastError_;
    QString accountsDir_;
    QString accountName_;
    QString fingerprint_;
    QString nodeId_;
    QString shareString_;
    std::vector<std::unique_ptr<Session>> sessions_;
    int activeIndex_ = -1;
};
