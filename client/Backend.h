#pragma once

// AeroMesh client backend bridge.
//
// This QObject is the first real connection between the QML user interface and
// the cryptographic core (aeromesh::core). On startup it initialises libsodium,
// loads (or creates) the device's long-term identity, and exposes only the
// derived, network-safe values to QML: the human-comparable fingerprint, the
// 256-bit Kademlia node id, and the shareable contact string used for QR
// exchange. Secret key material is NEVER exposed to the UI layer.

#include <QObject>
#include <QSettings>
#include <QString>
#include <QVariantMap>

#include <cstddef>
#include <optional>
#include <span>
#include <utility>

#include "aeromesh/identity.hpp"
#include "aeromesh/node_id.hpp"

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY identityChanged)
    Q_PROPERTY(QString fingerprint READ fingerprint NOTIFY identityChanged)
    Q_PROPERTY(QString nodeId READ nodeId NOTIFY identityChanged)
    Q_PROPERTY(QString shareString READ shareString NOTIFY identityChanged)

public:
    explicit Backend(QObject* parent = nullptr) : QObject(parent) {
        if (!aeromesh::init_crypto()) {
            return;
        }
        loadOrCreate();
    }

    [[nodiscard]] bool ready() const { return ready_; }
    [[nodiscard]] QString fingerprint() const { return fingerprint_; }
    [[nodiscard]] QString nodeId() const { return nodeId_; }
    [[nodiscard]] QString shareString() const { return shareString_; }

    // Discard the current identity and mint a fresh one. The new secret key is
    // persisted to the local store and the public values are re-published.
    Q_INVOKABLE void regenerateIdentity() {
        auto fresh = aeromesh::Identity::generate();
        if (!fresh) {
            return;
        }
        identity_ = std::move(*fresh);
        persist();
        publish();
    }

    // Validate and inspect a peer's share string (their public key, base64).
    // Returns { valid, fingerprint, nodeId, share } so the UI can confirm the
    // contact's cryptographic identity before saving it. Pure read-only.
    Q_INVOKABLE QVariantMap inspectShare(const QString& share) const {
        QVariantMap out;
        out["valid"] = false;
        const QString trimmed = share.trimmed();
        auto parsed = aeromesh::parse_share_string(trimmed.toStdString());
        if (!parsed) {
            return out;
        }
        const auto& pk = *parsed;
        const auto node = aeromesh::NodeId::from_public_key(
            std::span<const std::byte>(pk.data(), pk.size()));
        out["valid"] = true;
        out["fingerprint"] = fingerprintFromKey(pk);
        out["nodeId"] = QString::fromStdString(node.to_hex());
        out["share"] = trimmed;
        return out;
    }

signals:
    void identityChanged();

private:
    void loadOrCreate() {
        QSettings store;
        const QString saved = store.value(kSecretKey).toString();
        if (!saved.isEmpty()) {
            auto restored =
                aeromesh::Identity::from_secret_b64(saved.toStdString());
            if (restored) {
                identity_ = std::move(*restored);
                publish();
                return;
            }
        }

        auto fresh = aeromesh::Identity::generate();
        if (!fresh) {
            return;
        }
        identity_ = std::move(*fresh);
        persist();
        publish();
    }

    void persist() {
        if (!identity_) {
            return;
        }
        QSettings store;
        store.setValue(
            kSecretKey,
            QString::fromStdString(identity_->export_secret_b64()));
    }

    void publish() {
        if (!identity_) {
            return;
        }
        const auto& pk = identity_->public_key();
        const auto node = aeromesh::NodeId::from_public_key(
            std::span<const std::byte>(pk.data(), pk.size()));

        fingerprint_ = QString::fromStdString(identity_->fingerprint());
        nodeId_ = QString::fromStdString(node.to_hex());
        shareString_ = QString::fromStdString(identity_->share_string());
        ready_ = true;
        emit identityChanged();
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

    static constexpr auto kSecretKey = "identity/secret";

    std::optional<aeromesh::Identity> identity_;
    bool ready_ = false;
    QString fingerprint_;
    QString nodeId_;
    QString shareString_;
};
