// AeroMesh Qt Quick client entry point.
//
// Boots the QML engine and shows the main window. The UI layer is pure QML so
// the same screens run on desktop now and on Android/iOS later. Network wiring
// to aeromesh::core is added in a later milestone; for now the screens render
// from sample data so the look-and-feel can be iterated quickly.

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QList>

#include "Backend.h"
#include "ChatService.h"
#include "DhtService.h"
#include "NetworkEngine.h"
#include "WinFrame.h"

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);

    QGuiApplication::setApplicationName("AeroMesh");
    QGuiApplication::setOrganizationName("AeroMesh");

    // Use the style-agnostic "Basic" controls so our custom dark theme is not
    // overridden by the host platform's native style.
    QQuickStyle::setStyle("Basic");

    QQmlApplicationEngine engine;

    // Fail fast if the root QML object could not be created.
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

    // Wire the cryptographic core into the UI. The backend owns the device
    // identity and publishes only network-safe public values to QML.
    Backend backend;
    engine.rootContext()->setContextProperty("backend", &backend);

    // Native window frame helper. On Windows it strips the system title bar
    // while keeping the window "normal" so Windows 11 Snap Layouts appear over
    // the maximize button. QML reports the button's rectangle to it.
    WinFrame winFrame;
    engine.rootContext()->setContextProperty("winframe", &winFrame);

    // Bring up the live network engine: bind a real UDP socket and start the
    // cover-traffic / packet pump. An ephemeral port (0) is chosen so multiple
    // instances can run on one machine during testing; a fixed listen port is
    // configured later from the server/settings screen.
    NetworkEngine net;
    engine.rootContext()->setContextProperty("net", &net);
    net.start(0);

    // Conversation layer: runs the authenticated handshake with peers and moves
    // chat messages through the Double Ratchet on top of the network engine.
    // attach() must follow net.start() because it binds to the live transport;
    // the active identity is handed in by the backend once an account unlocks.
    ChatService chat;
    chat.attach(&net);
    engine.rootContext()->setContextProperty("chat", &chat);

    // Address discovery layer: a Kademlia DHT that resolves a contact's
    // network address from their key alone, so users no longer type host:port.
    // Like the chat layer it attaches after net.start() and is fed the active
    // identity by the backend.
    DhtService dht;
    dht.attach(&net);
    engine.rootContext()->setContextProperty("dht", &dht);

    // Hand the active account's identity to the chat layer, and refresh it
    // whenever the account changes (unlock, switch, lock, delete). On lock the
    // identity becomes null and the conversation state is cleared so nothing
    // leaks across accounts.
    auto syncChatIdentity = [&chat, &dht, &backend]() {
        const aeromesh::Identity* id = backend.activeIdentity();
        chat.setIdentity(id);
        dht.setIdentity(id);
        if (!id) {
            chat.reset();
        }
    };
    QObject::connect(&backend, &Backend::identityChanged, &chat,
                     syncChatIdentity);
    syncChatIdentity();

    engine.loadFromModule("AeroMesh", "Main");

    // Bind the helper to the top-level window once it exists.
    const QList<QObject*> roots = engine.rootObjects();
    if (!roots.isEmpty()) {
        if (auto* window = qobject_cast<QQuickWindow*>(roots.first()))
            winFrame.attach(window);
    }

    return app.exec();
}
