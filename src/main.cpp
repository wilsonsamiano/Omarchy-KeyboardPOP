// PressPop — a touch keyboard for Omarchy with key popups and swipe typing.

#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QTextStream>
#include <QUrl>
#include <unistd.h>

#include <LayerShellQt/window.h>

#include "backend.h"
#include "dictionary.h"
#include "keyboardview.h"
#include "personal.h"
#include "theme.h"
#include "touchwatcher.h"
#include "translator.h"
#include "waylandinput.h"

namespace {

const char *Usage =
    "Usage: presspop [command]\n"
    "\n"
    "Commands:\n"
    "  toggle   Show the keyboard, or hide it if it's showing (the default)\n"
    "  show     Show the keyboard\n"
    "  hide     Hide the keyboard\n"
    "  quit     Stop the running keyboard\n"
    "  import <file>  Merge a SafePop/PressPop personal dictionary backup\n"
    "  export <file>  Save the personal dictionary as a backup\n";

QString socketName() {
    return QStringLiteral("presspop-%1").arg(getuid());
}

int dictionaryCommand(const QString &command, const QString &path) {
    QTextStream err(stderr);
    if (path.isEmpty()) {
        err << "presspop " << command << ": a file is needed\n";
        return 1;
    }
    Dictionary dictionary;
    Personal personal(dictionary);
    if (command == "export") {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(personal.exportJson()) < 0) {
            err << "presspop export: can't write " << path << "\n";
            return 1;
        }
        return 0;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        err << "presspop import: can't read " << path << "\n";
        return 1;
    }
    const int added = personal.importJson(file.readAll());
    personal.flush();
    QTextStream(stdout) << "Imported " << added << " words\n";
    return 0;
}

} // namespace

int main(int argc, char *argv[]) {
    const QString command = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("toggle");
    const QStringList commands{"toggle", "show", "hide", "quit", "import", "export"};
    if (command == "--help" || command == "-h" || !commands.contains(command)) {
        QTextStream(command.startsWith("-h") || command == "--help" ? stdout : stderr) << Usage;
        return commands.contains(command) || command.startsWith("-h") || command == "--help" ? 0 : 1;
    }

    const bool offscreen = qgetenv("QT_QPA_PLATFORM") == "offscreen";

    QGuiApplication app(argc, argv);
    app.setApplicationName("presspop");
    app.setOrganizationName("presspop");
    app.setApplicationVersion("0.1.0");
    app.setDesktopFileName("presspop");
    app.setWindowIcon(QIcon::fromTheme("presspop"));
    app.setQuitOnLastWindowClosed(false);

    if (command == "import" || command == "export")
        return dictionaryCommand(command, argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString());

    // One keyboard at a time: a second launch (the app launcher, a key
    // binding) tells the running one what to do and exits.
    {
        QLocalSocket socket;
        socket.connectToServer(socketName());
        if (socket.waitForConnected(300)) {
            socket.write(command.toUtf8() + '\n');
            socket.waitForBytesWritten(300);
            socket.waitForDisconnected(300);
            return 0;
        }
    }
    // Nothing to stop. ("hide" goes on to start the keyboard hidden, which
    // is how it waits at login for a text field.)
    if (command == "quit")
        return 0;
    QLocalServer server;
    QLocalServer::removeServer(socketName());
    if (!server.listen(socketName())) {
        QTextStream(stderr) << "presspop: can't listen on " << socketName() << ": " << server.errorString() << "\n";
        return 1;
    }

    QQuickStyle::setStyle("Material");
    Translator translator;
    app.installTranslator(&translator);
    Theme theme;

    Dictionary dictionary;
    Personal personal(dictionary);
    Backend backend(dictionary, personal);
    QSettings settings;
    backend.setLanguage(settings.value("language", "en").toString());
    QObject::connect(&backend, &Backend::languageChanged, [&] { settings.setValue("language", backend.language()); });

    auto *wayland = offscreen ? nullptr : app.nativeInterface<QNativeInterface::QWaylandApplication>();
    WaylandInput input(wayland ? wayland->display() : nullptr);
    backend.setSink(&input);

    auto updateStatus = [&] {
        switch (input.mode()) {
        case WaylandInput::Mode::Connecting:
            break;
        case WaylandInput::Mode::InputMethod:
            backend.setStatus(QObject::tr("PressPop opens by itself when you tap a text field."));
            break;
        case WaylandInput::Mode::VirtualKeyboard:
            backend.setStatus(QObject::tr("Typing into the focused window. Another input method (like fcitx5) is "
                                          "running, so PressPop can't open by itself on text fields."));
            break;
        case WaylandInput::Mode::Unavailable:
            backend.setStatus(QObject::tr("This compositor has no virtual keyboard support, so PressPop can't type."),
                              true);
            break;
        }
    };
    QObject::connect(&input, &WaylandInput::modeChanged, updateStatus);
    QObject::connect(&translator, &Translator::changed, updateStatus);
    updateStatus();

    qmlRegisterType<KeyboardView>("PressPop", 1, 0, "KeyboardView");
    qmlRegisterUncreatableType<Backend>("PressPop", 1, 0, "Backend", "The app provides the backend");

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());

    // The keyboard is a layer surface docked to the bottom of the screen,
    // not a regular window, so it never takes focus from the app being
    // typed into. It has to be set up before the window first shows.
    if (auto *layer = offscreen ? nullptr : LayerShellQt::Window::get(window)) {
        layer->setLayer(LayerShellQt::Window::LayerTop);
        layer->setAnchors({LayerShellQt::Window::AnchorBottom, LayerShellQt::Window::AnchorLeft,
                           LayerShellQt::Window::AnchorRight});
        layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
        layer->setScope("presspop");
        // Windows make room for the keyboard instead of hiding under it.
        layer->setExclusiveZone(window->height());
        QObject::connect(window, &QWindow::heightChanged, layer, [layer](int h) { layer->setExclusiveZone(h); });
    }

    auto show = [&] {
        if (window->isVisible())
            return;
        input.resetContext();
        backend.reset();
        window->show();
    };
    auto hide = [&] { window->hide(); };
    auto run = [&](const QString &c) {
        if (c == "show")
            show();
        else if (c == "hide")
            hide();
        else if (c == "toggle")
            window->isVisible() ? hide() : show();
        else if (c == "quit")
            app.quit();
    };
    QObject::connect(&server, &QLocalServer::newConnection, [&] {
        while (QLocalSocket *client = server.nextPendingConnection()) {
            QObject::connect(client, &QLocalSocket::readyRead, client, [&, client] {
                while (client->canReadLine())
                    run(QString::fromUtf8(client->readLine()).trimmed());
                client->disconnectFromServer();
            });
            QObject::connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
        }
    });

    // As the input method, the keyboard comes and goes with text fields, but
    // only when the field was tapped with a finger: clicking one with the
    // mouse leaves it hidden. Without access to the touchscreen there is no
    // telling the two apart, so any focused field brings it up.
    TouchWatcher touch;
    QElapsedTimer activatedAt;
    constexpr qint64 TapWindowMs = 700;
    auto popUp = [&] {
        if (window->isVisible())
            return;
        backend.reset();
        window->show();
    };
    QObject::connect(&input, &WaylandInput::fieldActivated, [&] {
        activatedAt.start();
        touch.rescanIfNeeded();
        if (!touch.available() || touch.touchedWithin(TapWindowMs))
            popUp();
    });
    QObject::connect(&input, &WaylandInput::fieldDeactivated, [&] {
        activatedAt.invalidate();
        hide();
    });
    // Tapping a field that already has focus (say, one clicked earlier) moves
    // its cursor, which arrives as a context change.
    QObject::connect(&input, &WaylandInput::contextChanged, [&] {
        if (input.fieldActive() && touch.touchedWithin(TapWindowMs))
            popUp();
    });
    // The touch can also be read after the field reports focus.
    QObject::connect(&touch, &TouchWatcher::touched, [&] {
        if (input.fieldActive() && activatedAt.isValid() && activatedAt.elapsed() <= TapWindowMs)
            popUp();
    });
    QObject::connect(&input, &WaylandInput::contextChanged, &backend, &Backend::contextChanged);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &personal, &Personal::flush);

    run(command == "toggle" ? "show" : command);
    return app.exec();
}
