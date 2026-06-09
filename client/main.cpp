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

#include "Backend.h"

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

    engine.loadFromModule("AeroMesh", "Main");

    return app.exec();
}
